"""Inspect potato_int8.tflite: schema, tensors, quantization, operators (with versions),
activation memory estimate and consistency with potato_metadata.json / potato_model_data.h.

Usage (from the project root):
    .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\inspect_model.py
    .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\inspect_model.py --json ESP32_S3_CAM\\tools\\model_report.json

Only reads files; it never modifies or re-creates the model.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

TYPE_NAMES = {0: "FLOAT32", 1: "FLOAT16", 2: "INT32", 3: "UINT8", 4: "INT64", 7: "INT16", 9: "INT8"}
TYPE_SIZES = {0: 4, 1: 2, 2: 4, 3: 1, 4: 8, 7: 2, 9: 1}


def builtin_name(code):
    from tensorflow.lite.python import schema_py_generated as s

    for k, v in vars(s.BuiltinOperator).items():
        if not k.startswith("_") and v == code:
            return k
    return f"UNKNOWN_{code}"


def parse_header(path):
    text = path.read_text(encoding="ascii")
    body = text[text.index("{") + 1: text.index("};")]
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", body)), text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", type=Path, default=ROOT / "models" / "potato_int8.tflite")
    ap.add_argument("--header", type=Path, default=ROOT / "models" / "potato_model_data.h")
    ap.add_argument("--metadata", type=Path, default=ROOT / "models" / "potato_metadata.json")
    ap.add_argument("--json", type=Path, help="optional path to write the report as JSON")
    a = ap.parse_args()

    import numpy as np
    from tensorflow.lite.python import schema_py_generated as s

    blob = a.model.read_bytes()
    report = {"model_file": str(a.model.relative_to(ROOT)), "model_bytes": len(blob),
              "sha256": hashlib.sha256(blob).hexdigest(), "file_identifier": blob[4:8].decode("ascii", "replace")}

    m = s.Model.GetRootAsModel(blob, 0)
    report["schema_version"] = m.Version()
    report["subgraphs"] = m.SubgraphsLength()
    report["buffers"] = m.BuffersLength()
    sg = m.Subgraphs(0)

    # ---- operator codes (name + version) -------------------------------------------------
    codes = []
    for i in range(m.OperatorCodesLength()):
        oc = m.OperatorCodes(i)
        code = max(oc.BuiltinCode(), oc.DeprecatedBuiltinCode())
        codes.append({"name": builtin_name(code), "builtin_code": code, "version": oc.Version(),
                      "custom": oc.CustomCode().decode() if oc.CustomCode() else None})
    report["operator_codes"] = codes

    def tensor_info(idx):
        t = sg.Tensors(idx)
        q = t.Quantization()
        scale = [float(v) for v in q.ScaleAsNumpy()] if q is not None and q.ScaleLength() else []
        zp = [int(v) for v in q.ZeroPointAsNumpy()] if q is not None and q.ZeroPointLength() else []
        shape = [int(v) for v in t.ShapeAsNumpy()] if t.ShapeLength() else []
        n = int(np.prod(shape)) if shape else 1
        return {"index": idx, "name": t.Name().decode(), "shape": shape, "type": TYPE_NAMES.get(t.Type(), t.Type()),
                "bytes": n * TYPE_SIZES.get(t.Type(), 1), "buffer": t.Buffer(),
                "scale": scale[:4] + (["..."] if len(scale) > 4 else []), "zero_point": zp[:4] + (["..."] if len(zp) > 4 else []),
                "per_channel": len(scale) > 1}

    report["inputs"] = [tensor_info(int(i)) for i in sg.InputsAsNumpy()]
    report["outputs"] = [tensor_info(int(i)) for i in sg.OutputsAsNumpy()]

    ops = []
    for i in range(sg.OperatorsLength()):
        op = sg.Operators(i)
        c = codes[op.OpcodeIndex()]
        ops.append({"i": i, "op": c["name"], "version": c["version"],
                    "inputs": [int(x) for x in op.InputsAsNumpy()], "outputs": [int(x) for x in op.OutputsAsNumpy()]})
    report["operators"] = [{"i": o["i"], "op": o["op"], "version": o["version"],
                            "out_shape": tensor_info(o["outputs"][0])["shape"]} for o in ops]

    # ---- activation (non-constant) tensors & naive peak memory estimate --------------------
    def is_const(idx):
        b = m.Buffers(sg.Tensors(idx).Buffer())
        return b is not None and b.DataLength() > 0

    activ = sorted({t for o in ops for t in o["inputs"] + o["outputs"] if t >= 0 and not is_const(t)})
    first = {t: None for t in activ}
    last = {t: None for t in activ}
    for o in ops:
        for t in o["inputs"] + o["outputs"]:
            if t in first:
                first[t] = o["i"] if first[t] is None else first[t]
                last[t] = o["i"]
    for t in sg.InputsAsNumpy():
        first[int(t)] = -1
    for t in sg.OutputsAsNumpy():
        last[int(t)] = len(ops)
    peak, peak_at = 0, None
    for step in range(-1, len(ops) + 1):
        live = sum(tensor_info(t)["bytes"] for t in activ if first[t] <= step <= last[t])
        if live > peak:
            peak, peak_at = live, step
    report["activation_tensors"] = len(activ)
    report["activation_peak_bytes_naive"] = peak
    report["activation_peak_at_op"] = peak_at
    report["weights_bytes"] = sum(m.Buffers(i).DataLength() for i in range(m.BuffersLength()))

    # ---- TFLite interpreter check -------------------------------------------------------------
    try:
        import tensorflow as tf

        interp = tf.lite.Interpreter(model_content=blob)
        interp.allocate_tensors()
        inp, out = interp.get_input_details()[0], interp.get_output_details()[0]
        report["interpreter"] = {
            "input": {"shape": inp["shape"].tolist(), "dtype": np.dtype(inp["dtype"]).name,
                      "scale": float(inp["quantization"][0]), "zero_point": int(inp["quantization"][1])},
            "output": {"shape": out["shape"].tolist(), "dtype": np.dtype(out["dtype"]).name,
                       "scale": float(out["quantization"][0]), "zero_point": int(out["quantization"][1])},
            "tensor_count": len(interp.get_tensor_details()),
        }
    except Exception as exc:  # pragma: no cover - diagnostic only
        report["interpreter_error"] = repr(exc)

    # ---- metadata + header consistency ------------------------------------------------------
    meta = json.loads(a.metadata.read_text(encoding="utf-8"))
    hdr, hdr_text = parse_header(a.header)
    report["checks"] = {
        "metadata_class_names": meta["class_names"],
        "metadata_model_bytes_match": meta["model_bytes"] == len(blob),
        "metadata_input_match": meta["tflite_test"]["input"] == report.get("interpreter", {}).get("input"),
        "metadata_output_match": meta["tflite_test"]["output"] == report.get("interpreter", {}).get("output"),
        "header_bytes": len(hdr),
        "header_identical_to_tflite": hdr == blob,
        "header_alignas16": "alignas(16)" in hdr_text,
    }

    text = json.dumps(report, indent=2)
    print(text)
    if a.json:
        a.json.write_text(text + "\n", encoding="utf-8")
    ok = all(v for k, v in report["checks"].items() if isinstance(v, bool))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
