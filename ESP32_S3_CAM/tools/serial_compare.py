"""Compare what the ESP32-S3 computed with the PC (TFLite + NumPy reference) using Serial dumps.

The firmware/test sketches print, on request:
  BEGIN_FRAME  seq=.. w=.. h=.. format=RGB565_BE|JPEG   (base64)  END_FRAME  len=.. crc32=..   ('f' command)
  BEGIN_TENSOR seq=.. shape=1x96x96x3 dtype=int8 scale=.. zero_point=.. framing=..   END_TENSOR ...  ('t' command)
  #RESULT,seq=..,pred=..,raw_good=..,raw_not_good=..,...

This tool:
  * verifies each block (length + CRC32),
  * FRAME  -> saves frame_<seq>.png (check colour / orientation / field of view) and recomputes the input tensor
              with tools/preprocess_reference.py (same algorithm as image_preprocessing.cpp),
  * TENSOR -> saves tensor_<seq>.png (what the CNN actually saw) and runs potato_int8.tflite on it with TFLite
              (reference kernels) on the PC,
  * compares:  device TENSOR vs reference-from-FRAME    -> preprocessing equivalence (C++ vs Python)
               device #RESULT raw vs TFLite on TENSOR    -> runtime equivalence (TFLM vs TFLite)

Diagnosis:
  - tensor mismatch           => preprocessing problem (byte order, colour, resize, framing)
  - output mismatch (> few LSB) => model/runtime problem (TFLM, arena, ops)
  - both match, wrong class    => image-domain problem (lighting, background, framing, distance) or model limits

Usage:
  .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\serial_compare.py --log capture.txt --out serial_out
  .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\serial_compare.py --port COM5 --seconds 120 --out serial_out
     (--port needs: pip install pyserial; send 't' and 'f' then trigger a cycle while it records)
"""

import argparse
import base64
import os
import re
import sys
import zlib
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "3")
sys.path.insert(0, str(Path(__file__).resolve().parent))
import preprocess_reference as pr  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
FRAMINGS = {v: k for k, v in pr.FRAMING_NAMES.items()}


def read_port(port, baud, seconds, save, commands):
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial is not installed: .venv\\Scripts\\python.exe -m pip install pyserial  (or use --log)")
    import time

    lines = []
    with serial.Serial(port, baud, timeout=0.5) as s:
        t0 = time.time()
        print(f"Recording {port} for {seconds}s; sending commands {commands!r} "
              "(main/06 firmware: t=dump tensor, f=dump frame, m=manual trigger; 03_Test_Camera: d, p)")
        time.sleep(2.0)  # board may reset when the port opens
        for ch in commands:
            s.write(ch.encode())
            time.sleep(0.3)
        while time.time() - t0 < seconds:
            raw = s.readline()
            if raw:
                lines.append(raw.decode("utf-8", "replace").rstrip("\r\n"))
    if save:
        Path(save).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return lines


def parse(lines):
    blocks, results = [], {}
    cur = None
    for line in lines:
        line = line.strip()
        m = re.match(r"BEGIN_(FRAME|TENSOR)\s*(.*)", line)
        if m:
            cur = {"kind": m.group(1), "header": dict(re.findall(r"(\w+)=(\S+)", m.group(2))), "b64": []}
            continue
        m = re.match(r"END_(FRAME|TENSOR)\s+len=(\d+)\s+crc32=([0-9a-fA-F]+)", line)
        if m and cur is not None:
            data = base64.b64decode("".join(cur["b64"]))
            cur.update(data=data, len_ok=len(data) == int(m.group(2)),
                       crc_ok=(zlib.crc32(data) & 0xFFFFFFFF) == int(m.group(3), 16))
            blocks.append(cur)
            cur = None
            continue
        if cur is not None:
            cur["b64"].append(line)
            continue
        if line.startswith("#RESULT,"):
            kv = dict(p.split("=", 1) for p in line.split(",")[1:] if "=" in p)
            results[kv.get("seq")] = kv
    return blocks, results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", type=Path, help="text file saved from Serial Monitor")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--commands", default="tfm", help="characters sent after opening --port")
    ap.add_argument("--out", type=Path, default=Path("serial_out"))
    ap.add_argument("--model", type=Path, default=ROOT / "models" / "potato_int8.tflite")
    ap.add_argument("--tolerance", type=int, default=8, help="max |raw int8 diff| accepted between TFLM and TFLite")
    a = ap.parse_args()
    if not a.log and not a.port:
        ap.error("give --log or --port")

    import numpy as np
    import tensorflow as tf

    a.out.mkdir(parents=True, exist_ok=True)
    lines = read_port(a.port, a.baud, a.seconds, a.out / "serial_log.txt", a.commands) if a.port else \
        a.log.read_text(encoding="utf-8", errors="replace").splitlines()
    blocks, results = parse(lines)
    print(f"blocks: {len(blocks)} ({sum(b['kind'] == 'FRAME' for b in blocks)} FRAME, "
          f"{sum(b['kind'] == 'TENSOR' for b in blocks)} TENSOR), #RESULT lines: {len(results)}")

    it = tf.lite.Interpreter(model_content=a.model.read_bytes(),
                             experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    it.allocate_tensors()
    d_in, d_out = it.get_input_details()[0], it.get_output_details()[0]
    s_in, z_in = d_in["quantization"]

    def png(path, rgb):
        tf.io.write_file(str(path), tf.io.encode_png(rgb.astype(np.uint8)))

    frames, tensors = {}, {}
    for b in blocks:
        seq = b["header"].get("seq", "?")
        tag = f"{b['kind']} seq={seq}"
        if not (b["len_ok"] and b["crc_ok"]):
            print(f"[CORRUPT] {tag}: len_ok={b['len_ok']} crc_ok={b['crc_ok']} (serial data lost?)")
            continue
        if b["kind"] == "FRAME":
            w, h, fmt = int(b["header"]["w"]), int(b["header"]["h"]), b["header"].get("format", "RGB565_BE")
            if fmt == "JPEG":
                (a.out / f"frame_{seq}.jpg").write_bytes(b["data"])
                rgb = tf.io.decode_jpeg(b["data"], channels=3).numpy()
                note = "(JPEG decoded by TensorFlow; the ESP32 decoder may differ slightly)"
            else:
                rgb = pr.rgb565_be_to_rgb888(b["data"], w, h)
                note = ""
            png(a.out / f"frame_{seq}.png", rgb)
            frames[seq] = (rgb, fmt)
            print(f"[FRAME]  seq={seq}: {w}x{h} {fmt} -> {a.out / f'frame_{seq}.png'} {note}")
        else:
            q = np.frombuffer(b["data"], np.int8).reshape(96, 96, 3)
            scale = float(b["header"].get("scale", s_in))
            zp = int(b["header"].get("zero_point", z_in))
            png(a.out / f"tensor_{seq}.png", np.clip((q.astype(np.float32) - zp) * scale, 0, 255))
            tensors[seq] = (q, b["header"].get("framing", "LETTERBOX"))
            it.set_tensor(d_in["index"], q[None])
            it.invoke()
            raw = [int(v) for v in it.get_tensor(d_out["index"])[0]]
            msg = f"[TENSOR] seq={seq}: TFLite(PC) raw={raw} -> {'GOOD' if raw[0] >= raw[1] else 'NOT_GOOD'}"
            r = results.get(seq)
            if r:
                dev = [int(r["raw_good"]), int(r["raw_not_good"])]
                diff = max(abs(dev[0] - raw[0]), abs(dev[1] - raw[1]))
                same = (dev[0] >= dev[1]) == (raw[0] >= raw[1])
                verdict = "OK" if same and diff <= a.tolerance else "MISMATCH -> model/runtime problem"
                msg += f" | ESP32 raw={dev} pred={r.get('pred')} | diff={diff} {verdict}"
            print(msg)

    for seq, (rgb, fmt) in frames.items():
        if seq not in tensors:
            continue
        q_dev, framing = tensors[seq]
        q_ref, _ = pr.preprocess_rgb(rgb, FRAMINGS.get(framing, pr.FRAMING_LETTERBOX), 96, float(s_in), int(z_in))
        d = np.abs(q_dev.astype(int) - q_ref.astype(int))
        if d.max() == 0:
            verdict = "IDENTICAL"
        elif fmt == "JPEG" and d.max() <= 8:
            verdict = "close (JPEG decoders differ between ESP32 and PC)"
        else:
            verdict = "MISMATCH -> preprocessing problem"
        print(f"[PREPROC] seq={seq}: device tensor vs Python reference from the same frame: "
              f"{int((d > 0).sum())} elements differ, max |diff| {int(d.max())} -> {verdict}")
    print(f"PNG files written to {a.out.resolve()}")


if __name__ == "__main__":
    main()
