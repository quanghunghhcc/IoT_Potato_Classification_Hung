"""Bit-accurate NumPy reference of the firmware preprocessing (image_preprocessing.cpp).

The C++ code and this file must implement the *same* arithmetic, in float32, in the same order:

  RGB565 (big-endian, as delivered by esp32-camera)  ->  RGB888 (5/6-bit -> 8-bit by bit replication)
  -> framing (STRETCH / LETTERBOX / CENTER_CROP)
  -> bilinear resize to 96x96 with half-pixel centers (same formula as tf.image.resize, antialias=False,
     which is what tf.keras.utils.image_dataset_from_directory(interpolation="bilinear") uses)
  -> q = clamp(rint(v / input_scale + input_zero_point), -128, 127)   (rint = round-half-to-even == np.rint)

Used by export_test_vectors.py, framing_study.py and serial_compare.py.
"""

import numpy as np

FRAMING_STRETCH = 0
FRAMING_LETTERBOX = 1
FRAMING_CENTER_CROP = 2
FRAMING_NAMES = {FRAMING_STRETCH: "STRETCH", FRAMING_LETTERBOX: "LETTERBOX", FRAMING_CENTER_CROP: "CENTER_CROP"}

# Must match config.h (PREPROC_PAD_PATCH, PREPROC_PAD_INSET)
PAD_PATCH = 8
PAD_INSET = 4

F32 = np.float32


# ----------------------------------------------------------------------------------------------
# Pixel format helpers
# ----------------------------------------------------------------------------------------------
def rgb888_to_rgb565_be(rgb):
    """uint8 HxWx3 -> bytes, 2 bytes/pixel, high byte first (esp32-camera RGB565 layout)."""
    rgb = rgb.astype(np.uint16)
    v = ((rgb[..., 0] >> 3) << 11) | ((rgb[..., 1] >> 2) << 5) | (rgb[..., 2] >> 3)
    out = np.empty(v.shape + (2,), np.uint8)
    out[..., 0] = v >> 8
    out[..., 1] = v & 0xFF
    return out.tobytes()


def rgb565_be_to_rgb888(buf, width, height):
    """bytes -> uint8 HxWx3. Bit replication: r8 = r5<<3 | r5>>2, g8 = g6<<2 | g6>>4."""
    a = np.frombuffer(buf, np.uint8, count=width * height * 2).reshape(height, width, 2).astype(np.uint16)
    v = (a[..., 0] << 8) | a[..., 1]
    r5, g6, b5 = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    out = np.empty((height, width, 3), np.uint8)
    out[..., 0] = (r5 << 3) | (r5 >> 2)
    out[..., 1] = (g6 << 2) | (g6 >> 4)
    out[..., 2] = (b5 << 3) | (b5 >> 2)
    return out


# ----------------------------------------------------------------------------------------------
# Framing
# ----------------------------------------------------------------------------------------------
def estimate_pad_color(rgb):
    """Mean of four PAD_PATCH x PAD_PATCH patches, PAD_INSET pixels inside each corner.
    Integer mean with round-half-up: (sum + n/2) // n   (identical to the C++ code)."""
    h, w, _ = rgb.shape
    p, k = PAD_PATCH, PAD_INSET
    ys = (k, h - k - p)
    xs = (k, w - k - p)
    acc = np.zeros(3, np.int64)
    for y in ys:
        for x in xs:
            acc += rgb[y:y + p, x:x + p].reshape(-1, 3).astype(np.int64).sum(0)
    n = 4 * p * p
    return ((acc + n // 2) // n).astype(np.uint8)


def frame_to_source(rgb, framing):
    """Return the (virtual) source image the bilinear resize samples from, as float32."""
    h, w, _ = rgb.shape
    if framing == FRAMING_STRETCH:
        return rgb.astype(F32)
    if framing == FRAMING_CENTER_CROP:
        s = min(h, w)
        y0, x0 = (h - s) // 2, (w - s) // 2
        return rgb[y0:y0 + s, x0:x0 + s].astype(F32)
    if framing == FRAMING_LETTERBOX:
        s = max(h, w)
        pad = estimate_pad_color(rgb)
        out = np.empty((s, s, 3), F32)
        out[:] = pad.astype(F32)
        y0, x0 = (s - h) // 2, (s - w) // 2
        out[y0:y0 + h, x0:x0 + w] = rgb.astype(F32)
        return out
    raise ValueError(framing)


# ----------------------------------------------------------------------------------------------
# Bilinear resize, half-pixel centers (TensorFlow ResizeBilinear, half_pixel_centers=True)
# ----------------------------------------------------------------------------------------------
def _weights(out_size, in_size):
    scale = F32(in_size) / F32(out_size)
    lo = np.empty(out_size, np.int64)
    hi = np.empty(out_size, np.int64)
    lerp = np.empty(out_size, F32)
    for i in range(out_size):
        v = (F32(i) + F32(0.5)) * scale - F32(0.5)
        f = np.floor(v)
        lo[i] = max(int(f), 0)
        hi[i] = min(int(np.ceil(v)), in_size - 1)
        lerp[i] = v - f
    return lo, hi, lerp


def resize_bilinear(src, out_h, out_w):
    """src float32 HxWxC -> float32 out_h x out_w x C, same op order as the C++ code."""
    in_h, in_w = src.shape[:2]
    y0, y1, ly = _weights(out_h, in_h)
    x0, x1, lx = _weights(out_w, in_w)
    tl = src[y0][:, x0]
    tr = src[y0][:, x1]
    bl = src[y1][:, x0]
    br = src[y1][:, x1]
    lx = lx[None, :, None]
    ly = ly[:, None, None]
    top = tl + (tr - tl) * lx
    bottom = bl + (br - bl) * lx
    return (top + (bottom - top) * ly).astype(F32)


def quantize(values, scale, zero_point):
    q = np.rint(values.astype(F32) / F32(scale) + F32(zero_point))
    return np.clip(q, -128, 127).astype(np.int8)


def preprocess_rgb(rgb, framing, out_size=96, scale=1.0, zero_point=-128):
    """uint8 HxWx3 RGB -> (int8 out x out x 3 tensor, float32 resized image)."""
    resized = resize_bilinear(frame_to_source(rgb, framing), out_size, out_size)
    return quantize(resized, scale, zero_point), resized


def preprocess_rgb565(buf, width, height, framing, out_size=96, scale=1.0, zero_point=-128):
    return preprocess_rgb(rgb565_be_to_rgb888(buf, width, height), framing, out_size, scale, zero_point)


def simulate_camera_frame(img, width=320, height=240):
    """Approximate an OV3660 QVGA RGB565 frame from a dataset image (uint8 HxWx3):
    centre-crop to 4:3, antialiased resize to width x height, RGB565 round-trip.
    Returns (rgb565_bytes, rgb888_after_roundtrip). Needs TensorFlow."""
    import tensorflow as tf

    h, w = img.shape[:2]
    if w * 3 >= h * 4:
        cw, ch = (h * 4) // 3, h
    else:
        cw, ch = w, (w * 3) // 4
    y0, x0 = (h - ch) // 2, (w - cw) // 2
    crop = img[y0:y0 + ch, x0:x0 + cw]
    sim = tf.image.resize(crop, (height, width), method="bilinear", antialias=True).numpy()
    sim = np.clip(np.rint(sim), 0, 255).astype(np.uint8)
    buf = rgb888_to_rgb565_be(sim)
    return buf, rgb565_be_to_rgb888(buf, width, height)


# ----------------------------------------------------------------------------------------------
# Self test against TensorFlow
# ----------------------------------------------------------------------------------------------
def self_test(image_paths, out_size=96):
    """Compare resize_bilinear() with tf.image.resize(bilinear) on real dataset images."""
    import tensorflow as tf

    worst_float, worst_q, n_diff, n_total = 0.0, 0, 0, 0
    for p in image_paths:
        img = tf.io.decode_image(tf.io.read_file(str(p)), channels=3, expand_animations=False)
        ref = tf.image.resize(img, (out_size, out_size), method="bilinear").numpy()
        mine = resize_bilinear(img.numpy().astype(F32), out_size, out_size)
        worst_float = max(worst_float, float(np.abs(ref - mine).max()))
        qr, qm = quantize(ref, 1.0, -128).astype(int), quantize(mine, 1.0, -128).astype(int)
        worst_q = max(worst_q, int(np.abs(qr - qm).max()))
        n_diff += int((qr != qm).sum())
        n_total += qr.size
    return {"images": len(image_paths), "max_abs_float_diff": worst_float,
            "max_abs_int8_diff": worst_q, "int8_mismatch_ratio": n_diff / max(n_total, 1)}


if __name__ == "__main__":
    import json
    import sys
    from pathlib import Path

    root = Path(__file__).resolve().parents[2]
    files = sorted((root / "dataset" / "test").rglob("*.jpg"))[:: max(1, len(list((root / "dataset" / "test").rglob("*.jpg"))) // 40)]
    print(json.dumps(self_test(files), indent=2))
    sys.exit(0)
