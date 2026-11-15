// Загальний інтерфейс датчика рівня води.
// Решта прошивки працює лише з цим інтерфейсом, тому датчики (HC-SR04, тензодатчик з HX711)
// взаємозамінні: вибір робиться одним рядком у main.cpp.
#pragma once
#include <math.h>

struct LevelReading {
  bool ok = false;
  const char* error = "";  // причина помилки, якщо ok == false
  float percent = 0.0f;    // наповнення пляшки, 0..100 %
  float distanceCm = NAN;  // лише для ультразвукового датчика
  float volumeMl = NAN;    // лише для ваг
};

// Параметр, який користувач налаштовує кнопкою або з Telegram:
// для HC-SR04 це висота пляшки, для ваг — її об'єм.
struct SensorParam {
  const char* key;    // ключ JSON-команди, напр. "set_height"
  const char* label;  // підпис на екрані
  const char* unit;
  float* value;       // значення в налаштуваннях
  float step;         // крок кнопки
  float min;
  float max;
};

class LevelSensor {
 public:
  virtual ~LevelSensor() = default;
  virtual void begin() = 0;
  virtual LevelReading read() = 0;
  virtual const char* name() const = 0;  // для повідомлень про помилки
  virtual SensorParam param() = 0;

  // Тарування і калібрування мають сенс лише для ваг
  virtual bool tare() { return false; }
  virtual bool calibrate(float knownGrams) {
    (void)knownGrams;
    return false;
  }
};
