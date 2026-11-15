#include "button.h"

Button::Button(uint8_t pin, unsigned long debounceMs, unsigned long longPressMs)
    : pin_(pin), debounceMs_(debounceMs), longPressMs_(longPressMs) {}

void Button::begin() { pinMode(pin_, INPUT_PULLUP); }

bool Button::isPressed() const { return digitalRead(pin_) == LOW; }

ButtonEvent Button::poll(unsigned long now) {
  int reading = digitalRead(pin_);
  if (reading != rawState_) {
    lastChange_ = now;
    rawState_ = reading;
  }
  if (now - lastChange_ <= debounceMs_ || reading == stableState_) return ButtonEvent::None;

  stableState_ = reading;
  if (stableState_ == LOW) {
    pressStart_ = now;
    return ButtonEvent::None;
  }
  return (now - pressStart_ >= longPressMs_) ? ButtonEvent::LongPress : ButtonEvent::Click;
}
