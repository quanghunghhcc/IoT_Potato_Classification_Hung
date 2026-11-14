# Potato CNN for ESP32-S3-CAM

The script trains on `dataset/train`, selects the best checkpoint with `dataset/val`, and evaluates on `dataset/test`. Class indices are fixed: `0 = good`, `1 = not_good`. Images directly under `dataset/good` and `dataset/not_good` are not used.

## Train

Use 64-bit CPython 3.10–3.12 with TensorFlow support:

```powershell
python -m pip install -r requirements.txt
python train.py --dataset dataset --output models
```

Options include `--epochs 30 --batch-size 32 --image-size 96 --representative-samples 200`.

Outputs in `models/` are `potato_best.keras` (training checkpoint), `potato_int8.tflite` (fully integer model), `potato_model_data.h` (C++ byte array), and `potato_metadata.json` (class order, dimensions, quantization, and test accuracy).

## Firmware input

Capture RGB888 pixels and resize to the model input size (default 96 × 96) with bilinear interpolation. The model performs `/255` normalization internally. Quantize each raw RGB byte for the signed int8 input tensor:

```cpp
int q = lroundf(pixel / input->params.scale) + input->params.zero_point;
input->data.int8[index] = static_cast<int8_t>(std::max(-128, std::min(127, q)));
```

The output order is `good`, then `not_good`. Compute each approximate probability as `(output->data.int8[i] - output->params.zero_point) * output->params.scale`, or compare the two int8 scores directly. Include the generated header in exactly one C++ source file. Register the operators required by the TFLite model in TensorFlow Lite Micro and size the tensor arena on the actual board; PSRAM is recommended. The chip runs inference only.
