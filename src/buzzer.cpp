#include "buzzer.h"

#ifdef WOKWI_SIM
// Зумер у Wokwi пасивний: звучить лише від змінного сигналу, тому живимо його ШІМ 2 кГц.
// Канал LEDC налаштовуємо один раз сам, бо tone()/noTone() до першого tone() сиплють помилками.
static constexpr uint8_t BUZZER_LEDC_CHANNEL = 7;
#endif

void Buzzer::begin() {
#ifdef WOKWI_SIM
  ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
  ledcAttachPin(pin_, BUZZER_LEDC_CHANNEL);
#else
  pinMode(pin_, OUTPUT);
#endif
  set(false);
}

void Buzzer::set(bool on) {
  on_ = on;
#ifdef WOKWI_SIM
  ledcWriteTone(BUZZER_LEDC_CHANNEL, on ? 2000 : 0);
#else
  // Активний зумер сам генерує звук, досить подати живлення
  digitalWrite(pin_, on ? HIGH : LOW);
#endif
}

void Buzzer::beep(uint8_t count, uint16_t onMs, uint16_t offMs) {
  togglesLeft_ = count * 2;  // увімкнути + вимкнути на кожен сигнал
  onMs_ = onMs;
  offMs_ = offMs;
  nextAt_ = millis();
  set(false);
}

void Buzzer::update(unsigned long now) {
  if (togglesLeft_ == 0 || (long)(now - nextAt_) < 0) return;
  set(!on_);
  togglesLeft_--;
  nextAt_ = now + (on_ ? onMs_ : offMs_);
}
