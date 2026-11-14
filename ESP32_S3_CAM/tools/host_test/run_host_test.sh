#!/usr/bin/env bash
# Host (PC) checks — need g++ (e.g. MSYS2 UCRT64) and the project .venv. Run from anywhere:
#   bash ESP32_S3_CAM/tools/host_test/run_host_test.sh
# 1) syntax check of every sketch file against the stub headers in stubs/ (stubs are NOT the real
#    Arduino-ESP32/TFLM headers: this catches C++ mistakes only, it does not replace arduino-cli compile)
# 2) compiles the REAL image_preprocessing.cpp + diagnostics.cpp and compares their output byte-for-byte
#    with tools/preprocess_reference.py on frames simulated from dataset/test (+ noise / solid / RGB888 / BGR888)
set -u
HT="$(cd "$(dirname "$0")" && pwd)"; FW="$(cd "$HT/../.." && pwd)"; ROOT="$(cd "$FW/.." && pwd)"
PY="$ROOT/.venv/Scripts/python.exe"; [ -x "$PY" ] || PY="$ROOT/.venv/bin/python"
TMP="$(mktemp -d)"; fails=0
for d in "$FW/Potato_Classifier_ESP32S3" "$FW"/examples/*; do
  for f in "$d"/*.cpp "$d"/*.ino; do
    [ -e "$f" ] || continue; case "$f" in *test_vectors.cpp) continue;; esac
    g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DBOARD_HAS_PSRAM -I "$HT/stubs" -I "$d" \
        -x c++ -include Arduino.h "$f" || { echo "SYNTAX FAIL: $f"; fails=$((fails+1)); }
  done
done
echo "syntax failures: $fails"
MAIN="$FW/Potato_Classifier_ESP32S3"; EX5="$FW/examples/05_Test_CNN"
g++ -std=gnu++17 -O2 -I "$HT/stubs" -I "$MAIN" -I "$EX5" "$HT/harness.cpp" "$MAIN/image_preprocessing.cpp" \
    "$MAIN/diagnostics.cpp" "$EX5/test_vectors.cpp" -o "$TMP/harness" || exit 1
"$PY" "$HT/bulk.py" "$FW/tools" write "$TMP/frames.bin" "$TMP/out.bin" "$ROOT" 2>/dev/null
"$TMP/harness" "$TMP/frames.bin" "$TMP/out.bin" || fails=$((fails+1))
"$PY" "$HT/bulk.py" "$FW/tools" compare "$TMP/frames.bin" "$TMP/out.bin" "$ROOT" 2>/dev/null
rm -rf "$TMP"; exit $fails
