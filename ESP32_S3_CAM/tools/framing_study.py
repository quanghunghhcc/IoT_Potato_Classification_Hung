"""Measure how the firmware framing mode (STRETCH / LETTERBOX / CENTER_CROP) changes the INT8 model's
predictions on camera-like frames simulated from the REAL test split (dataset/test).

Camera simulation (approximation of what the OV3660 delivers at QVGA):
  test image -> centre crop to 4:3 -> resize (antialiased) to 320x240 -> RGB565 -> RGB888 (bit replication)
For the 512x512 letterboxed images this crop removes exactly the 64-px pad bands (the original 4:3 photo).

It also feeds background-only / solid-colour inputs to show whether the model reacts to background.
Nothing here changes the model. Results are printed and optionally saved as JSON.

    .venv\\Scripts\\python.exe ESP32_S3_CAM\\tools\\framing_study.py --json ESP32_S3_CAM\\tools\\framing_report.json
"""

import argparse
import json
import os
import sys
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "3")
sys.path.insert(0, str(Path(__file__).resolve().parent))
import preprocess_reference as pr  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CLASSES = ("good", "not_good")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", type=Path, default=ROOT / "models" / "potato_int8.tflite")
    ap.add_argument("--test-dir", type=Path, default=ROOT / "dataset" / "test")
    ap.add_argument("--json", type=Path)
    a = ap.parse_args()

    import numpy as np
    import tensorflow as tf

    it = tf.lite.Interpreter(model_content=a.model.read_bytes())
    it.allocate_tensors()
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    s_in, z_in = inp["quantization"]
    s_out, z_out = out["quantization"]

    def run(q):
        it.set_tensor(inp["index"], q[None])
        it.invoke()
        raw = it.get_tensor(out["index"])[0].astype(int)
        return int(np.argmax(raw)), (raw - z_out) * s_out

    def camera_sim(img):
        h, w = img.shape[:2]
        if w * 3 >= h * 4:   # wider than 4:3 -> crop width
            cw, ch = (h * 4) // 3, h
        else:                # taller -> crop height
            cw, ch = w, (w * 3) // 4
        y0, x0 = (h - ch) // 2, (w - cw) // 2
        crop = img[y0:y0 + ch, x0:x0 + cw]
        sim = tf.image.resize(crop, (240, 320), method="bilinear", antialias=True).numpy()
        sim = np.clip(np.rint(sim), 0, 255).astype(np.uint8)
        return pr.rgb565_be_to_rgb888(pr.rgb888_to_rgb565_be(sim), 320, 240)

    modes = {"TRAINING_PIPELINE": None, **{v: k for k, v in pr.FRAMING_NAMES.items()}}
    stats = {m: {c: {"n": 0, "correct": 0, "p_not_good_sum": 0.0} for c in CLASSES} for m in modes}
    by_size = {}

    for label, cname in enumerate(CLASSES):
        for f in sorted((a.test_dir / cname).iterdir()):
            img = tf.io.decode_image(tf.io.read_file(str(f)), channels=3, expand_animations=False).numpy()
            key = f"{cname}:{'x'.join(map(str, img.shape[1::-1]))}" if img.shape[0] in (224, 512) else f"{cname}:full-res"
            ref = tf.image.resize(img, (96, 96), method="bilinear").numpy()
            q_train = np.clip(np.rint(ref / s_in + z_in), -128, 127).astype(np.int8)
            frame = camera_sim(img)
            for m, fm in modes.items():
                q = q_train if fm is None else pr.preprocess_rgb(frame, fm, 96, s_in, z_in)[0]
                pred, probs = run(q)
                st = stats[m][cname]
                st["n"] += 1
                st["correct"] += int(pred == label)
                st["p_not_good_sum"] += float(probs[1])
                bs = by_size.setdefault(key, {}).setdefault(m, [0, 0])
                bs[0] += int(pred == label)
                bs[1] += 1

    # Background-only / solid colour probes
    probes = {}
    for name, rgb in {"solid_white_240": (240, 240, 240), "solid_light_grey_200": (200, 200, 200),
                      "solid_concrete_grey_170": (170, 165, 160), "solid_black_0": (0, 0, 0)}.items():
        q = pr.quantize(np.full((96, 96, 3), rgb, np.float32), s_in, z_in)
        pred, probs = run(q)
        probes[name] = {"pred": CLASSES[pred], "p_good": round(float(probs[0]), 4), "p_not_good": round(float(probs[1]), 4)}
    # Pure background patches cut from real images (top-left 120x120 of the 4:3 content, no potato)
    for cname in CLASSES:
        preds = []
        for f in sorted((a.test_dir / cname).iterdir())[:60]:
            img = tf.io.decode_image(tf.io.read_file(str(f)), channels=3, expand_animations=False).numpy()
            if img.shape[:2] != (512, 512):
                continue
            patch = img[70:190, 4:124]  # inside the 4:3 content, away from centre
            q = pr.quantize(tf.image.resize(patch, (96, 96), method="bilinear").numpy(), s_in, z_in)
            preds.append(run(q)[0])
        if preds:
            probes[f"background_patch_from_{cname}_images"] = {
                "n": len(preds), "pred_not_good_ratio": round(sum(preds) / len(preds), 4)}

    report = {"modes": {}, "by_source": by_size, "probes": probes}
    for m in modes:
        report["modes"][m] = {c: {"n": s["n"], "accuracy": round(s["correct"] / s["n"], 4),
                                  "mean_p_not_good": round(s["p_not_good_sum"] / s["n"], 4)}
                              for c, s in stats[m].items()}
        tot = sum(s["correct"] for s in stats[m].values()), sum(s["n"] for s in stats[m].values())
        report["modes"][m]["overall_accuracy"] = round(tot[0] / tot[1], 4)
    print(json.dumps(report, indent=2))
    if a.json:
        a.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
