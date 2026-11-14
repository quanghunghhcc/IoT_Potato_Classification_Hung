import os, sys, struct, glob, random
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
sys.path.insert(0, sys.argv[1])
import numpy as np, tensorflow as tf
import preprocess_reference as pr
mode = sys.argv[2]; frames_path, out_path = sys.argv[3], sys.argv[4]
root = sys.argv[5]
random.seed(1)
files = sorted(glob.glob(f"{root}/dataset/test/good/*")) + sorted(glob.glob(f"{root}/dataset/test/not_good/*"))
files = random.sample(files, 60)
cases = []   # (w, h, layout, bytes, rgb888 for reference)
for f in files:
    img = tf.io.decode_image(tf.io.read_file(f), channels=3, expand_animations=False).numpy()
    buf, rgb = pr.simulate_camera_frame(img)
    cases.append((320, 240, 0, buf, rgb))
rng = np.random.default_rng(7)
for w, h in ((320, 240), (160, 120), (96, 96), (400, 296), (240, 320)):
    noise = rng.integers(0, 256, (h, w, 3), dtype=np.uint8)
    buf = pr.rgb888_to_rgb565_be(noise); cases.append((w, h, 0, buf, pr.rgb565_be_to_rgb888(buf, w, h)))
for val in (0, 255, 128):
    rgb = np.full((240, 320, 3), val, np.uint8); buf = pr.rgb888_to_rgb565_be(rgb)
    cases.append((320, 240, 0, buf, pr.rgb565_be_to_rgb888(buf, 320, 240)))
# RGB888 and BGR888 layouts on 10 real frames
for (_, _, _, _, rgb) in cases[:10]:
    cases.append((320, 240, 1, rgb.tobytes(), rgb))
    cases.append((320, 240, 2, rgb[..., ::-1].copy().tobytes(), rgb))
if mode == "write":
    with open(frames_path, "wb") as fh:
        for w, h, lay, buf, _ in cases:
            fh.write(struct.pack("<III", w, h, lay)); fh.write(buf)
    print("frames written:", len(cases))
else:
    got = np.fromfile(out_path, np.int8).reshape(len(cases), 3, 96, 96, 3)
    worst, mism, total, bad = 0, 0, 0, 0
    for i, (w, h, lay, buf, rgb) in enumerate(cases):
        for fm in range(3):
            exp, _ = pr.preprocess_rgb(rgb, fm)
            d = np.abs(got[i, fm].astype(int) - exp.astype(int))
            worst = max(worst, int(d.max())); mism += int((d > 0).sum()); total += d.size
            bad += int(d.max() > 0)
    print(f"cases={len(cases)} x 3 framings: tensors not identical={bad}, element mismatches={mism}/{total}, max |diff|={worst}")
    # TF bilinear vs reference on the STRETCH path, for context
