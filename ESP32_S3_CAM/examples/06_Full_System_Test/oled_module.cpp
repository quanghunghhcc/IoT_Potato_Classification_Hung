// oled_module.cpp — xem oled_module.h
#include "oled_module.h"

#include <Wire.h>
#include "config.h"

#if ENABLE_OLED

#if SOC_I2C_NUM > 1
#define OLED_WIRE Wire1
#else
#define OLED_WIRE Wire
#endif

namespace {

// Bảng mã font 5x7 chuẩn cho ký tự ASCII 32..126 (95 ký tự, 475 bytes)
// Mỗi ký tự gồm 5 cột (cột 0 -> cột 4), bit 0 là pixel trên cùng, bit 6 là dưới cùng.
const uint8_t kFont5x7[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00,  // ' '
  0x00, 0x00, 0x5F, 0x00, 0x00,  // '!'
  0x00, 0x07, 0x00, 0x07, 0x00,  // '"'
  0x14, 0x7F, 0x14, 0x7F, 0x14,  // '#'
  0x24, 0x2A, 0x7F, 0x2A, 0x12,  // '$'
  0x23, 0x13, 0x08, 0x64, 0x62,  // '%'
  0x36, 0x49, 0x55, 0x22, 0x50,  // '&'
  0x00, 0x05, 0x03, 0x00, 0x00,  // '\''
  0x00, 0x1C, 0x22, 0x41, 0x00,  // '('
  0x00, 0x41, 0x22, 0x1C, 0x00,  // ')'
  0x14, 0x08, 0x3E, 0x08, 0x14,  // '*'
  0x08, 0x08, 0x3E, 0x08, 0x08,  // '+'
  0x00, 0x50, 0x30, 0x00, 0x00,  // ','
  0x08, 0x08, 0x08, 0x08, 0x08,  // '-'
  0x00, 0x60, 0x60, 0x00, 0x00,  // '.'
  0x20, 0x10, 0x08, 0x04, 0x02,  // '/'
  0x3E, 0x51, 0x49, 0x45, 0x3E,  // '0'
  0x00, 0x42, 0x7F, 0x40, 0x00,  // '1'
  0x42, 0x61, 0x51, 0x49, 0x46,  // '2'
  0x21, 0x41, 0x45, 0x4B, 0x31,  // '3'
  0x18, 0x14, 0x12, 0x7F, 0x10,  // '4'
  0x27, 0x45, 0x45, 0x45, 0x39,  // '5'
  0x3C, 0x4A, 0x49, 0x49, 0x30,  // '6'
  0x01, 0x71, 0x09, 0x05, 0x03,  // '7'
  0x36, 0x49, 0x49, 0x49, 0x36,  // '8'
  0x06, 0x49, 0x49, 0x29, 0x1E,  // '9'
  0x00, 0x36, 0x36, 0x00, 0x00,  // ':'
  0x00, 0x56, 0x36, 0x00, 0x00,  // ';'
  0x08, 0x14, 0x22, 0x41, 0x00,  // '<'
  0x14, 0x14, 0x14, 0x14, 0x14,  // '='
  0x00, 0x41, 0x22, 0x14, 0x08,  // '>'
  0x02, 0x01, 0x51, 0x09, 0x06,  // '?'
  0x32, 0x49, 0x79, 0x41, 0x3E,  // '@'
  0x7E, 0x11, 0x11, 0x11, 0x7E,  // 'A'
  0x7F, 0x49, 0x49, 0x49, 0x36,  // 'B'
  0x3E, 0x41, 0x41, 0x41, 0x22,  // 'C'
  0x7F, 0x41, 0x41, 0x22, 0x1C,  // 'D'
  0x7F, 0x49, 0x49, 0x49, 0x41,  // 'E'
  0x7F, 0x09, 0x09, 0x09, 0x01,  // 'F'
  0x3E, 0x41, 0x49, 0x49, 0x7A,  // 'G'
  0x7F, 0x08, 0x08, 0x08, 0x7F,  // 'H'
  0x00, 0x41, 0x7F, 0x41, 0x00,  // 'I'
  0x20, 0x40, 0x41, 0x3F, 0x01,  // 'J'
  0x7F, 0x08, 0x14, 0x22, 0x41,  // 'K'
  0x7F, 0x40, 0x40, 0x40, 0x40,  // 'L'
  0x7F, 0x02, 0x0C, 0x02, 0x7F,  // 'M'
  0x7F, 0x04, 0x08, 0x10, 0x7F,  // 'N'
  0x3E, 0x41, 0x41, 0x41, 0x3E,  // 'O'
  0x7F, 0x09, 0x09, 0x09, 0x06,  // 'P'
  0x3E, 0x41, 0x51, 0x21, 0x5E,  // 'Q'
  0x7F, 0x09, 0x19, 0x29, 0x46,  // 'R'
  0x46, 0x49, 0x49, 0x49, 0x31,  // 'S'
  0x01, 0x01, 0x7F, 0x01, 0x01,  // 'T'
  0x3F, 0x40, 0x40, 0x40, 0x3F,  // 'U'
  0x1F, 0x20, 0x40, 0x20, 0x1F,  // 'V'
  0x7F, 0x20, 0x18, 0x20, 0x7F,  // 'W'
  0x63, 0x14, 0x08, 0x14, 0x63,  // 'X'
  0x07, 0x08, 0x70, 0x08, 0x07,  // 'Y'
  0x61, 0x51, 0x49, 0x45, 0x43,  // 'Z'
  0x00, 0x7F, 0x41, 0x41, 0x00,  // '['
  0x02, 0x04, 0x08, 0x10, 0x20,  // '\'
  0x00, 0x41, 0x41, 0x7F, 0x00,  // ']'
  0x04, 0x02, 0x01, 0x02, 0x04,  // '^'
  0x40, 0x40, 0x40, 0x40, 0x40,  // '_'
  0x00, 0x01, 0x02, 0x04, 0x00,  // '`'
  0x20, 0x54, 0x54, 0x54, 0x78,  // 'a'
  0x7F, 0x48, 0x44, 0x44, 0x38,  // 'b'
  0x38, 0x44, 0x44, 0x44, 0x20,  // 'c'
  0x38, 0x44, 0x44, 0x48, 0x7F,  // 'd'
  0x38, 0x54, 0x54, 0x54, 0x18,  // 'e'
  0x08, 0x7E, 0x09, 0x01, 0x02,  // 'f'
  0x0C, 0x52, 0x52, 0x52, 0x3E,  // 'g'
  0x7F, 0x08, 0x04, 0x04, 0x78,  // 'h'
  0x00, 0x44, 0x7D, 0x40, 0x00,  // 'i'
  0x20, 0x40, 0x44, 0x3D, 0x00,  // 'j'
  0x7F, 0x10, 0x28, 0x44, 0x00,  // 'k'
  0x00, 0x41, 0x7F, 0x40, 0x00,  // 'l'
  0x7C, 0x04, 0x18, 0x04, 0x78,  // 'm'
  0x7C, 0x08, 0x04, 0x04, 0x78,  // 'n'
  0x38, 0x44, 0x44, 0x44, 0x38,  // 'o'
  0x7C, 0x14, 0x14, 0x14, 0x08,  // 'p'
  0x08, 0x14, 0x14, 0x18, 0x7C,  // 'q'
  0x7C, 0x08, 0x04, 0x04, 0x08,  // 'r'
  0x48, 0x54, 0x54, 0x54, 0x20,  // 's'
  0x04, 0x3F, 0x44, 0x40, 0x20,  // 't'
  0x3C, 0x40, 0x40, 0x20, 0x7C,  // 'u'
  0x1C, 0x20, 0x40, 0x20, 0x1C,  // 'v'
  0x3C, 0x40, 0x30, 0x40, 0x3C,  // 'w'
  0x44, 0x28, 0x10, 0x28, 0x44,  // 'x'
  0x0C, 0x50, 0x50, 0x50, 0x3C,  // 'y'
  0x44, 0x64, 0x54, 0x4C, 0x44,  // 'z'
  0x00, 0x08, 0x36, 0x41, 0x00,  // '{'
  0x00, 0x00, 0x7F, 0x00, 0x00,  // '|'
  0x00, 0x41, 0x36, 0x08, 0x00,  // '}'
  0x08, 0x08, 0x2A, 0x1C, 0x08   // '~'
};

const uint8_t kInitCommands[] PROGMEM = {
  0xAE,        // Display off
  0xD5, 0x80,  // Set display clock divide ratio / osc frequency
  0xA8, 0x3F,  // Set multiplex ratio (1 to 64) -> 64 MUX
  0xD3, 0x00,  // Set display offset = 0
  0x40,        // Set start line = 0
  0x8D, 0x14,  // Enable charge pump regulator (0x14)
  0x20, 0x02,  // Set Memory Addressing Mode -> 0x02 = Page Addressing Mode (tương thích SSD1306 & SH1106)
  0xA1,        // Column 127 mapped to SEG0 (segment remap)
  0xC8,        // COM output scan direction remapped
  0xDA, 0x12,  // Set COM pins hardware configuration
  0x81, 0xCF,  // Set contrast control (0xCF)
  0xD9, 0xF1,  // Set pre-charge period
  0xDB, 0x40,  // Set Vcomh deselect level
  0xA4,        // Entire display ON (output follows RAM content)
  0xA6,        // Normal display (non-inverted)
  0x2E,        // Deactivate scroll
  0xAF         // Display ON
};

}  // namespace

void OledModule::writeCommand(uint8_t cmd) {
  OLED_WIRE.beginTransmission(addr_);
  OLED_WIRE.write(0x80);  // Co = 1, D/C# = 0 (single command)
  OLED_WIRE.write(cmd);
  OLED_WIRE.endTransmission();
}

void OledModule::writeData(const uint8_t* data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    const size_t chunk = (size - sent > 32) ? 32 : (size - sent);
    OLED_WIRE.beginTransmission(addr_);
    OLED_WIRE.write(0x40);  // Co = 0, D/C# = 1 (data)
    for (size_t i = 0; i < chunk; ++i) {
      OLED_WIRE.write(data[sent + i]);
    }
    OLED_WIRE.endTransmission();
    sent += chunk;
  }
}

bool OledModule::begin(int sda, int scl, uint8_t addr) {
  addr_ = addr;
  OLED_WIRE.setPins(sda, scl);
  if (!OLED_WIRE.begin(sda, scl, 100000)) {
    Serial.printf("[OLED] ERROR: Wire.begin(SDA=%d, SCL=%d) failed\n", sda, scl);
    ready_ = false;
    return false;
  }
  OLED_WIRE.setClock(100000);

  // 1. Kiểm tra địa chỉ OLED (thường là 0x3C, một số module là 0x3D)
  uint8_t target = addr_;
  OLED_WIRE.beginTransmission(target);
  if (OLED_WIRE.endTransmission() != 0) {
    const uint8_t alt = (target == 0x3C) ? 0x3D : 0x3C;
    OLED_WIRE.beginTransmission(alt);
    if (OLED_WIRE.endTransmission() == 0) {
      target = alt;
      Serial.printf("[OLED] Found SSD1306 at alternative address 0x%02X\n", target);
    } else {
      // Quét toàn bộ bus I2C để chẩn đoán
      Serial.printf("[OLED] Warning: No response at 0x%02X or 0x%02X. Scanning I2C bus (SDA=IO%d, SCL=IO%d)...\n",
                    addr_, alt, sda, scl);
      uint8_t found = 0;
      for (uint8_t a = 1; a < 127; ++a) {
        OLED_WIRE.beginTransmission(a);
        if (OLED_WIRE.endTransmission() == 0) {
          Serial.printf("[OLED] -> Found I2C device at address 0x%02X!\n", a);
          if (found == 0) found = a;
        }
      }
      if (found != 0) {
        target = found;
        Serial.printf("[OLED] Using device found at 0x%02X\n", target);
      } else {
        Serial.printf("[OLED] ERROR: No I2C device responded on SDA=GPIO%d, SCL=GPIO%d!\n", sda, scl);
        Serial.println("[OLED] Huong dan kiem tra:");
        Serial.println("[OLED]   1. Thu dao 2 day: doi SDA sang GPIO 2, SCL sang GPIO 1.");
        Serial.println("[OLED]   2. Kiem tra thu tu chan VCC/GND tren module OLED (co loai GND o chan 1, VCC o chan 2).");
        Serial.println("[OLED]   3. Dam bao VCC noi nguon 3.3V va GND noi chung mass.");
        ready_ = false;
        return false;
      }
    }
  }
  addr_ = target;

  // Gửi toàn bộ lệnh khởi tạo trong 1 transaction liên tục
  OLED_WIRE.beginTransmission(addr_);
  OLED_WIRE.write(0x00);  // Co = 0, D/C# = 0: stream of commands
  for (size_t i = 0; i < sizeof(kInitCommands); ++i) {
    OLED_WIRE.write(pgm_read_byte(&kInitCommands[i]));
  }
  const uint8_t err = OLED_WIRE.endTransmission();
  if (err != 0) {
    Serial.printf("[OLED] ERROR: Init command transmission failed (code %d)\n", err);
    ready_ = false;
    return false;
  }

  ready_ = true;
  clear();
  display();
  Serial.printf("[OLED] SSD1306 128x64 ready at 0x%02X (SDA=IO%d, SCL=IO%d)\n", addr_, sda, scl);
  return true;
}

void OledModule::clear() {
  memset(buffer_, 0, sizeof(buffer_));
}

void OledModule::display() {
  if (!ready_) return;

  // Sử dụng Page Addressing Mode: gửi từng page 0..7 (tương thích cả SSD1306 và SH1106)
  for (uint8_t page = 0; page < 8; ++page) {
    OLED_WIRE.beginTransmission(addr_);
    OLED_WIRE.write(0x00);         // Command stream
    OLED_WIRE.write(0xB0 + page);  // Set Page Address (B0..B7)
    OLED_WIRE.write(0x00);         // Set Lower Column Address
    OLED_WIRE.write(0x10);         // Set Higher Column Address
    OLED_WIRE.endTransmission();

    const uint8_t* ptr = &buffer_[page * 128];
    // Gửi 128 bytes của page thành 4 khối 32 bytes
    for (uint8_t chunk = 0; chunk < 4; ++chunk) {
      OLED_WIRE.beginTransmission(addr_);
      OLED_WIRE.write(0x40);  // Data stream
      for (uint8_t i = 0; i < 32; ++i) {
        OLED_WIRE.write(*ptr++);
      }
      OLED_WIRE.endTransmission();
    }
  }
}

void OledModule::drawPixel(int16_t x, int16_t y, uint8_t color) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
  const uint16_t idx = x + (y / 8) * 128;
  if (color) {
    buffer_[idx] |= (1 << (y & 7));
  } else {
    buffer_[idx] &= ~(1 << (y & 7));
  }
}

void OledModule::drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
  if (y < 0 || y >= 64 || x >= 128) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > 128) w = 128 - x;
  if (w <= 0) return;
  for (int16_t i = 0; i < w; ++i) {
    drawPixel(x + i, y, color);
  }
}

void OledModule::drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
  if (x < 0 || x >= 128 || y >= 64) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > 64) h = 64 - y;
  if (h <= 0) return;
  for (int16_t i = 0; i < h; ++i) {
    drawPixel(x, y + i, color);
  }
}

void OledModule::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void OledModule::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
  for (int16_t i = 0; i < h; ++i) {
    drawFastHLine(x, y + i, w, color);
  }
}

void OledModule::drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t bg, uint8_t size) {
  if (c < 32 || c > 126) c = ' ';
  const uint16_t charOffset = (c - 32) * 5;

  for (int8_t i = 0; i < 6; ++i) {
    uint8_t line = (i < 5) ? pgm_read_byte(&kFont5x7[charOffset + i]) : 0x00;
    for (int8_t j = 0; j < 8; ++j) {
      const bool pixel = (line & 0x01);
      const uint8_t drawColor = pixel ? color : bg;
      if (size == 1) {
        if (pixel || bg != color) drawPixel(x + i, y + j, drawColor);
      } else {
        if (pixel || bg != color) {
          fillRect(x + i * size, y + j * size, size, size, drawColor);
        }
      }
      line >>= 1;
    }
  }
}

void OledModule::print(int16_t x, int16_t y, const char* text, uint8_t size, uint8_t color, uint8_t bg) {
  if (text == nullptr) return;
  int16_t cursorX = x;
  int16_t cursorY = y;
  const int16_t charWidth = 6 * size;
  const int16_t charHeight = 8 * size;

  while (*text) {
    const char c = *text++;
    if (c == '\n') {
      cursorX = x;
      cursorY += charHeight;
    } else if (c == '\r') {
      // bỏ qua carriage return
    } else {
      drawChar(cursorX, cursorY, c, color, bg, size);
      cursorX += charWidth;
      if (cursorX + charWidth > 128) {
        cursorX = x;
        cursorY += charHeight;
      }
    }
  }
}

void OledModule::showBoot(const char* title, const char* version) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 12, 1);
  print(8, 2, "POTATO CLASSIFIER", 1, 0, 1);  // Chữ đen nền trắng
  print(10, 20, "ESP32-S3 AI System", 1, 1, 0);
  print(10, 34, version ? version : "v1.0.0", 1, 1, 0);
  drawRect(10, 48, 108, 10, 1);
  fillRect(12, 50, 50, 6, 1);  // Thanh tiến trình khởi động
  display();
}

void OledModule::showReady(const char* ip, uint32_t total, uint32_t good, uint32_t notGood) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 11, 1);
  print(16, 2, "POTATO AI: READY", 1, 0, 1);

  print(4, 16, "Waiting for potato...", 1, 1, 0);

  char buf[32];
  snprintf(buf, sizeof(buf), "IP: %s", (ip && strlen(ip) > 0) ? ip : "No Wi-Fi");
  print(4, 32, buf, 1, 1, 0);

  drawFastHLine(0, 46, 128, 1);
  snprintf(buf, sizeof(buf), "Tot:%lu G:%lu B:%lu", (unsigned long)total, (unsigned long)good, (unsigned long)notGood);
  print(4, 52, buf, 1, 1, 0);
  display();
}

void OledModule::showDetecting(uint32_t seq) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 11, 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "CYCLE #%lu DETECTED", (unsigned long)seq);
  print(4, 2, buf, 1, 0, 1);

  print(8, 22, "Potato in position!", 1, 1, 0);
  print(8, 38, "Stabilizing...", 1, 1, 0);

  drawRect(14, 52, 100, 8, 1);
  fillRect(16, 54, 30, 4, 1);
  display();
}

void OledModule::showCapturing(uint32_t seq) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 11, 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "CYCLE #%lu CAPTURE", (unsigned long)seq);
  print(4, 2, buf, 1, 0, 1);

  print(12, 22, "Capturing OV3660...", 1, 1, 0);
  print(12, 38, "Preprocessing image", 1, 1, 0);

  drawRect(14, 52, 100, 8, 1);
  fillRect(16, 54, 65, 4, 1);
  display();
}

void OledModule::showInferring(uint32_t seq) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 11, 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "CYCLE #%lu INFERENCE", (unsigned long)seq);
  print(4, 2, buf, 1, 0, 1);

  print(14, 24, "Running CNN Model...", 1, 1, 0);
  print(14, 38, "TensorFlow Lite", 1, 1, 0);

  drawRect(14, 52, 100, 8, 1);
  fillRect(16, 54, 90, 4, 1);
  display();
}

void OledModule::showResult(const char* quality, float confidence, uint32_t seq, float totalMs) {
  if (!ready_) return;
  clear();
  // Header: Lượt và thời gian
  fillRect(0, 0, 128, 11, 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "#%lu  %.0fms", (unsigned long)seq, totalMs);
  print(4, 2, "RESULT", 1, 0, 1);
  print(64, 2, buf, 1, 0, 1);

  // Kết quả chữ LỚN (size 2) ở giữa màn hình
  const bool isGood = (strcmp(quality, "GOOD") == 0);
  const bool isBad = (strcmp(quality, "NOT GOOD") == 0);

  if (isGood) {
    print(18, 16, ">> GOOD <<", 2, 1, 0);
  } else if (isBad) {
    print(8, 16, "> NOT GOOD <", 2, 1, 0);
  } else {
    print(10, 16, "UNCERTAIN", 2, 1, 0);
  }

  // Độ tin cậy và thanh tiến trình
  snprintf(buf, sizeof(buf), "Confidence: %.1f%%", confidence * 100.0f);
  print(12, 38, buf, 1, 1, 0);

  // Thanh Bar hiển thị độ tin cậy từ 0 -> 100%
  drawRect(10, 50, 108, 11, 1);
  int16_t fillW = static_cast<int16_t>(confidence * 104.0f);
  if (fillW < 0) fillW = 0;
  if (fillW > 104) fillW = 104;
  fillRect(12, 52, fillW, 7, 1);

  display();
}

void OledModule::showError(const char* errorMsg) {
  if (!ready_) return;
  clear();
  fillRect(0, 0, 128, 13, 1);
  print(12, 3, "! SYSTEM ERROR !", 1, 0, 1);

  print(4, 22, errorMsg ? errorMsg : "Unknown error", 1, 1, 0);
  print(4, 38, "Please check wiring", 1, 1, 0);
  print(4, 50, "and press RESET", 1, 1, 0);
  display();
}

#else
// Khi ENABLE_OLED = 0
bool OledModule::begin(int, int, uint8_t) { return false; }
void OledModule::clear() {}
void OledModule::display() {}
void OledModule::showBoot(const char*, const char*) {}
void OledModule::showReady(const char*, uint32_t, uint32_t, uint32_t) {}
void OledModule::showDetecting(uint32_t) {}
void OledModule::showCapturing(uint32_t) {}
void OledModule::showInferring(uint32_t) {}
void OledModule::showResult(const char*, float, uint32_t, float) {}
void OledModule::showError(const char*) {}
void OledModule::drawPixel(int16_t, int16_t, uint8_t) {}
void OledModule::drawFastHLine(int16_t, int16_t, int16_t, uint8_t) {}
void OledModule::drawFastVLine(int16_t, int16_t, int16_t, uint8_t) {}
void OledModule::fillRect(int16_t, int16_t, int16_t, int16_t, uint8_t) {}
void OledModule::drawRect(int16_t, int16_t, int16_t, int16_t, uint8_t) {}
void OledModule::drawChar(int16_t, int16_t, char, uint8_t, uint8_t, uint8_t) {}
void OledModule::print(int16_t, int16_t, const char*, uint8_t, uint8_t, uint8_t) {}
#endif
