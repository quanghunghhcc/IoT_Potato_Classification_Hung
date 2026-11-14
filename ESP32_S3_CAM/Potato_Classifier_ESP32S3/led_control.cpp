// led_control.cpp — xem led_control.h
#include "led_control.h"

#include "board_pins.h"
#include "config.h"

void LedControl::write(int pin, bool on) {
  const int active = LED_ACTIVE_LEVEL;
  const int inactive = (LED_ACTIVE_LEVEL == HIGH) ? LOW : HIGH;
  digitalWrite(pin, on ? active : inactive);
}

void LedControl::begin() {
  // Ghi mức TẮT trước khi chuyển sang OUTPUT để LED không lóe sáng khi khởi động.
  write(LED_GREEN_PIN, false);
  write(LED_RED_PIN, false);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  write(LED_GREEN_PIN, false);
  write(LED_RED_PIN, false);
  mode_ = LED_MODE_OFF;
  Serial.printf("[LED] Green GPIO%d, Red GPIO%d, active level = %s -> both OFF\n", LED_GREEN_PIN, LED_RED_PIN,
                LED_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
}

void LedControl::allOff() {
  const LedMode old = mode_;
  write(LED_GREEN_PIN, false);
  write(LED_RED_PIN, false);
  mode_ = LED_MODE_OFF;
  if (!verbose_) return;
  if (old == LED_MODE_GOOD) Serial.println("[LED] GREEN OFF");
  else if (old == LED_MODE_NOT_GOOD) Serial.println("[LED] RED OFF");
  else if (old == LED_MODE_ERROR) Serial.println("[LED] ERROR INDICATION OFF");
}

void LedControl::showGood() {
  write(LED_RED_PIN, false);  // tắt đỏ trước → không bao giờ có lúc cả hai cùng sáng
  write(LED_GREEN_PIN, true);
  mode_ = LED_MODE_GOOD;
  if (verbose_) Serial.println("[LED] GREEN ON");
}

void LedControl::showNotGood() {
  write(LED_GREEN_PIN, false);
  write(LED_RED_PIN, true);
  mode_ = LED_MODE_NOT_GOOD;
  if (verbose_) Serial.println("[LED] RED ON");
}

void LedControl::showError() {
  write(LED_GREEN_PIN, false);
  write(LED_RED_PIN, false);
  mode_ = LED_MODE_ERROR;
  blinkOn_ = false;
  lastToggleMs_ = millis();
#if LED_ERROR_INDICATION
  if (verbose_) Serial.println("[LED] ERROR INDICATION (both LEDs blinking together)");
#else
  if (verbose_) Serial.println("[LED] ERROR -> both LEDs OFF (LED_ERROR_INDICATION = 0)");
#endif
}

void LedControl::update() {
#if LED_ERROR_INDICATION
  if (mode_ != LED_MODE_ERROR) return;
  const uint32_t now = millis();
  if (now - lastToggleMs_ >= LED_ERROR_BLINK_MS) {
    lastToggleMs_ = now;
    blinkOn_ = !blinkOn_;
    write(LED_GREEN_PIN, blinkOn_);
    write(LED_RED_PIN, blinkOn_);
  }
#endif
}
