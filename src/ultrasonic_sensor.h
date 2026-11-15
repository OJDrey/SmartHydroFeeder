// Ультразвуковий датчик HC-SR04: міряє відстань від кришки до поверхні води.
#pragma once
#include <Arduino.h>

#include "level_sensor.h"

class UltrasonicSensor : public LevelSensor {
 public:
  // heightCm — посилання на висоту пляшки з налаштувань, щоб зміни
  // через кнопку чи Telegram одразу враховувалися в розрахунку відсотків.
  UltrasonicSensor(uint8_t trigPin, uint8_t echoPin, float& heightCm);

  void begin() override;
  LevelReading read() override;
  const char* name() const override { return "HC-SR04"; }
  SensorParam param() override;

 private:
  float measureDistanceCm();  // NAN, якщо відлуння не прийшло

  uint8_t trigPin_;
  uint8_t echoPin_;
  float& heightCm_;
};
