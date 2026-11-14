"""Generate examples/05_Test_CNN/test_vectors.{h,cpp} from REAL images in dataset/test.

Part A vectors  : image -> exact training pipeline (tf.image.resize bilinear 96x96, np.rint quantization)
                  -> int8 tensor + expected raw int8 output of potato_int8.tflite.
Part B frame    : one test image -> simulated 320x240 RGB565 camera frame (preprocess_reference.simulate_camera_frame)
                  -> expected int8 tensor CRC32 for STRETCH / LETTERBOX / CENTER_CROP computed by the NumPy
                  reference of image_preprocessing.cpp, plus expected raw outputs.

Expected outputs come from the TFLite interpreter with REFERENCE kernels (BUILTIN_REF, closest to TFLM);
the default optimized kernels are recorded too so differences between runtimes are visible.

    .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\export_test_vectors.py
"""

import argparse
import json
import os
import sys
import zlib
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "3")
sys.path.insert(0, str(Path(__file__).resolve().parent))
import preprocess_reference as pr  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "ESP32_S3_CAM" / "examples" / "05_Test_CNN"
CLASSES = ("good", "not_good")


def c_bytes(data, per_line=24):
    rows = []
    for i in range(0, len(data), per_line):
        rows.append("  " + ",".join(str(int(v)) for v in data[i:i + per_line]) + ",")
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", type=Path, default=ROOT / "models" / "potato_int8.tflite")
    ap.add_argument("--metadata", type=Path, default=ROOT / "models" / "potato_metadata.json")
    ap.add_argument("--test-dir", type=Path, default=ROOT / "dataset" / "test")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    a = ap.parse_args()

    import numpy as np
    import tensorflow as tf

    meta = json.loads(a.metadata.read_text(encoding="utf-8"))
    blob = a.model.read_bytes()
    ref = tf.lite.Interpreter(model_content=blob,
                              experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    opt = tf.lite.Interpreter(model_content=blob)
    for it in (ref, opt):
        it.allocate_tensors()
    inp, out = ref.get_input_details()[0], ref.get_output_details()[0]
    s_in, z_in = inp["quantization"]
    assert (float(s_in), int(z_in)) == (meta["tflite_test"]["input"]["scale"], meta["tflite_test"]["input"]["zero_point"])

    def run(it, q):
        d_in, d_out = it.get_input_details()[0], it.get_output_details()[0]
        it.set_tensor(d_in["index"], q[None])
        it.invoke()
        return [int(v) for v in it.get_tensor(d_out["index"])[0]]

    def load(p):
        return tf.io.decode_image(tf.io.read_file(str(p)), channels=3, expand_animations=False).numpy()

    # ---- pick real test images: different sources of each class ---------------------------------
    picks = []
    good = sorted((a.test_dir / "good").iterdir())
    bad = sorted((a.test_dir / "not_good").iterdir())
    seen_sizes = set()
    for p in good:
        size = load(p).shape[:2]
        key = size if size in ((224, 224), (512, 512)) else "full"
        if key not in seen_sizes:
            seen_sizes.add(key)
            picks.append((p, 0))
        if len(seen_sizes) == 3:
            break
    picks += [(p, 1) for p in bad[:3]]

    def pipeline(p):
        resized = tf.image.resize(load(p), (96, 96), method="bilinear").numpy()
        return np.clip(np.rint(resized / s_in + z_in), -128, 127).astype(np.int8)

    # + the least confident real test images (non-saturated outputs stress numerical equivalence)
    margins = []
    for label, files in ((0, good), (1, bad)):
        for p in files:
            r = run(ref, pipeline(p))
            margins.append((abs(r[0] - r[1]), str(p), label))
    margins.sort()
    chosen = {str(p) for p, _ in picks}
    for _, p, label in margins:
        if p not in chosen:
            picks.append((Path(p), label))
            chosen.add(p)
        if len(picks) >= 8:
            break

    vectors = []
    for p, label in picks:
        img = load(p)
        q = pipeline(p)
        r_ref, r_opt = run(ref, q), run(opt, q)
        vectors.append({"name": f"{p.parent.name}/{p.name} ({img.shape[1]}x{img.shape[0]})", "label": label,
                        "tensor": q.reshape(-1), "crc": zlib.crc32(q.tobytes()), "ref": r_ref, "opt": r_opt})

    # ---- camera-like frame from a real not_good test image (letterbox source) ---------------------
    frame_src = bad[0]
    frame_buf, _ = pr.simulate_camera_frame(load(frame_src))
    frame_exp = []
    letterbox_tensor = None
    for fm in (pr.FRAMING_STRETCH, pr.FRAMING_LETTERBOX, pr.FRAMING_CENTER_CROP):
        q, _ = pr.preprocess_rgb565(frame_buf, 320, 240, fm, 96, float(s_in), int(z_in))
        frame_exp.append({"framing": fm, "crc": zlib.crc32(q.tobytes()), "ref": run(ref, q), "opt": run(opt, q)})
        if fm == pr.FRAMING_LETTERBOX:
            letterbox_tensor = q.reshape(-1)

    # ---- write C++ -------------------------------------------------------------------------------
    a.out.mkdir(parents=True, exist_ok=True)
    h = [
        "// test_vectors.h - GENERATED by tools/export_test_vectors.py from real images in dataset/test.",
        "// Do not edit by hand.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "struct CnnTestVector {",
        "  const char* name;",
        "  int label;                    // 0 = good, 1 = not_good (ground truth folder)",
        "  const int8_t* tensor;         // 96*96*3 int8, exact training pipeline",
        "  uint32_t crc32;",
        "  int8_t expectedRaw[2];        // TFLite reference kernels (BUILTIN_REF)",
        "  int8_t expectedRawOptimized[2];  // TFLite default (optimized) kernels",
        "};",
        "",
        "struct FrameExpectation {",
        "  int framing;                  // FRAMING_* in config.h",
        "  uint32_t crc32;               // CRC32 of the int8 tensor from tools/preprocess_reference.py",
        "  int8_t expectedRaw[2];",
        "  int8_t expectedRawOptimized[2];",
        "};",
        "",
        "extern const CnnTestVector kCnnTestVectors[];",
        "extern const int kCnnTestVectorCount;",
        "extern const char* const kTestFrameSource;",
        "extern const int kTestFrameLabel;",
        "extern const int kTestFrameWidth;",
        "extern const int kTestFrameHeight;",
        "extern const uint8_t kTestFrameRgb565[];",
        "extern const size_t kTestFrameLen;",
        "extern const FrameExpectation kFrameExpectations[];",
        "extern const int kFrameExpectationCount;",
        "extern const int8_t kFrameLetterboxTensor[];",
        "",
    ]
    c = [
        "// test_vectors.cpp - GENERATED by tools/export_test_vectors.py. Do not edit by hand.",
        "#include \"test_vectors.h\"",
        "",
    ]
    for i, v in enumerate(vectors):
        c += [f"alignas(16) static const int8_t kVec{i}[] = {{", c_bytes(v["tensor"]), "};", ""]
    c.append("const CnnTestVector kCnnTestVectors[] = {")
    for i, v in enumerate(vectors):
        c.append(f'  {{"{v["name"]}", {v["label"]}, kVec{i}, 0x{v["crc"]:08x}u, {{{v["ref"][0]}, {v["ref"][1]}}}, '
                 f'{{{v["opt"][0]}, {v["opt"][1]}}}}},')
    c += ["};", f"const int kCnnTestVectorCount = {len(vectors)};", ""]
    c += [f'const char* const kTestFrameSource = "{frame_src.parent.name}/{frame_src.name} -> simulated 320x240 RGB565";',
          "const int kTestFrameLabel = 1;", "const int kTestFrameWidth = 320;", "const int kTestFrameHeight = 240;",
          "alignas(16) const uint8_t kTestFrameRgb565[] = {", c_bytes(frame_buf), "};",
          "const size_t kTestFrameLen = sizeof(kTestFrameRgb565);", ""]
    c.append("const FrameExpectation kFrameExpectations[] = {")
    for e in frame_exp:
        c.append(f'  {{{e["framing"]}, 0x{e["crc"]:08x}u, {{{e["ref"][0]}, {e["ref"][1]}}}, '
                 f'{{{e["opt"][0]}, {e["opt"][1]}}}}},  // {pr.FRAMING_NAMES[e["framing"]]}')
    c += ["};", f"const int kFrameExpectationCount = {len(frame_exp)};", "",
          "alignas(16) const int8_t kFrameLetterboxTensor[] = {", c_bytes(letterbox_tensor), "};", ""]

    (a.out / "test_vectors.h").write_text("\n".join(h), encoding="ascii")
    (a.out / "test_vectors.cpp").write_text("\n".join(c), encoding="ascii")

    summary = {
        "vectors": [{k: v[k] for k in ("name", "label", "ref", "opt")} | {"crc32": f"{v['crc']:08x}"} for v in vectors],
        "frame_source": str(frame_src.name),
        "frame_expectations": [{"framing": pr.FRAMING_NAMES[e["framing"]], "crc32": f"{e['crc']:08x}",
                                "ref": e["ref"], "opt": e["opt"]} for e in frame_exp],
    }
    print(json.dumps(summary, indent=2))
    (a.out.parent.parent / "tools" / "test_vectors_report.json").write_text(json.dumps(summary, indent=2) + "\n",
                                                                              encoding="utf-8")


if __name__ == "__main__":
    main()
