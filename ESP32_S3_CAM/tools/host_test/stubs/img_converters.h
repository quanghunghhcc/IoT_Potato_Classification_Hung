#pragma once
#include "esp_camera.h"
bool fmt2rgb888(const uint8_t *src_buf, size_t src_len, pixformat_t format, uint8_t * rgb_buf);
