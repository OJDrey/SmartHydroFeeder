// Активний зумер з неблокуючими серіями сигналів.
#pragma once
#include <Arduino.h>

class Buzzer {
 public:
  explicit Buzzer(uint8_t pin) : pin_(pin) {}

  void begin();
  void beep(uint8_t count, uint16_t onMs = 120, uint16_t offMs = 120);
  void update(unsigned long now);  // викликати в кожному loop()

 private:
  void set(bool on);

  uint8_t pin_;
  uint8_t togglesLeft_ = 0;
  uint16_t onMs_ = 0;
  uint16_t offMs_ = 0;
  unsigned long nextAt_ = 0;
  bool on_ = false;
};
