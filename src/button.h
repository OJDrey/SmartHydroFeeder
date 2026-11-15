// Кнопка з антибрязкотом: повертає подію в момент відпускання.
// Подвійне натискання розпізнає логіка поїлки (feeder.cpp), бо воно
// потрібне лише на екрані налаштувань.
#pragma once
#include <Arduino.h>

enum class ButtonEvent { None, Click, LongPress };

class Button {
 public:
  Button(uint8_t pin, unsigned long debounceMs, unsigned long longPressMs);

  void begin();
  ButtonEvent poll(unsigned long now);  // викликати в кожному loop()
  bool isPressed() const;               // миттєвий стан без антибрязкоту

 private:
  uint8_t pin_;
  unsigned long debounceMs_;
  unsigned long longPressMs_;
  int rawState_ = HIGH;
  int stableState_ = HIGH;
  unsigned long lastChange_ = 0;
  unsigned long pressStart_ = 0;
};
