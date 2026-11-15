// Тензодатчик (балка 1 кг) з АЦП HX711: зважує пляшку.
// Вода має густину 1 г/мл, тож після тарування з порожньою пляшкою
// вага в грамах дорівнює об'єму води в мілілітрах.
#pragma once
#include <Arduino.h>
#include <HX711.h>

#include "level_sensor.h"

class ScaleSensor : public LevelSensor {
 public:
  // offset і factor зберігаються в налаштуваннях (NVS), щоб тарування
  // і калібрування не злітали після перезавантаження.
  //   grams = (raw - offset) / factor
  ScaleSensor(uint8_t dtPin, uint8_t sckPin, long& offset, float& factor, float& capacityMl);

  void begin() override;
  LevelReading read() override;
  const char* name() const override { return "HX711"; }
  SensorParam param() override;

  bool tare() override;                       // запам'ятати поточну вагу як нуль (порожня пляшка)
  bool calibrate(float knownGrams) override;  // на вагах лежить відомий вантаж

 private:
  bool readRaw(long& raw);  // середнє кількох вимірів; false, якщо HX711 не відповідає

  uint8_t dtPin_;
  uint8_t sckPin_;
  HX711 hx_;
  long& offset_;
  float& factor_;
  float& capacityMl_;
};
