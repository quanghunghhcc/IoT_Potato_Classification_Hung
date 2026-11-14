// Host harness: runs the REAL firmware image_preprocessing.cpp on frames produced by Python.
// frames.bin: repeated [u32 w][u32 h][u32 layout][bytes]; writes out.bin = for each frame, 3 framings x (w=96*96*3 int8)
#include <Arduino.h>
#include <cstdio>
#include <vector>
#include "image_preprocessing.h"
#include "diagnostics.h"
#include "config.h"
#include "test_vectors.h"
int g_stub_pin_level[64];
HardwareSerial Serial;
EspClass ESP;
int main(int argc, char** argv) {
  // 1) Same check as 05_Test_CNN Part B, with the embedded test frame
  ImageView tf = {kTestFrameRgb565, kTestFrameLen, kTestFrameWidth, kTestFrameHeight, PIXEL_RGB565_BE};
  static int8_t t[96 * 96 * 3];
  int partb = 0;
  for (int i = 0; i < kFrameExpectationCount; ++i) {
    PreprocStats st{};
    PreprocError e = preprocessToInt8(tf, kFrameExpectations[i].framing, t, sizeof(t), 96, 96, 1.0f, -128, &st);
    bool ok = e == PREPROC_OK && st.tensorCrc32 == kFrameExpectations[i].crc32;
    if (kFrameExpectations[i].framing == FRAMING_LETTERBOX) ok = ok && memcmp(t, kFrameLetterboxTensor, sizeof(t)) == 0;
    partb += ok;
    printf("PartB %-11s crc host=%08lx python=%08lx %s pad=(%u,%u,%u)\n", framingName(kFrameExpectations[i].framing),
           (unsigned long)st.tensorCrc32, (unsigned long)kFrameExpectations[i].crc32, ok ? "IDENTICAL" : "DIFFERENT",
           st.padColor[0], st.padColor[1], st.padColor[2]);
  }
  // Part A vector CRCs (validates crc32Update against zlib)
  int crcok = 0;
  for (int i = 0; i < kCnnTestVectorCount; ++i)
    crcok += crc32Update(0, (const uint8_t*)kCnnTestVectors[i].tensor, 27648) == kCnnTestVectors[i].crc32;
  printf("crc32Update vs zlib on %d vectors: %d match\n", kCnnTestVectorCount, crcok);
  // 2) Bulk frames from Python
  FILE* in = fopen(argv[1], "rb"); FILE* out = fopen(argv[2], "wb");
  uint32_t hdr[3]; int n = 0;
  while (fread(hdr, 4, 3, in) == 3) {
    size_t bpp = hdr[2] == PIXEL_RGB565_BE ? 2 : 3;
    std::vector<uint8_t> buf(hdr[0] * hdr[1] * bpp);
    if (fread(buf.data(), 1, buf.size(), in) != buf.size()) return 2;
    ImageView v = {buf.data(), buf.size(), (int)hdr[0], (int)hdr[1], (PixelLayout)hdr[2]};
    for (int f = 0; f < 3; ++f) {
      PreprocError e = preprocessToInt8(v, f, t, sizeof(t), 96, 96, 1.0f, -128, nullptr);
      if (e != PREPROC_OK) { printf("error %s\n", preprocErrorString(e)); return 3; }
      fwrite(t, 1, sizeof(t), out);
    }
    ++n;
  }
  fclose(out);
  printf("bulk frames processed: %d\n", n);
  return (partb == kFrameExpectationCount && crcok == kCnnTestVectorCount) ? 0 : 1;
}
