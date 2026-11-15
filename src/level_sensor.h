// Загальний інтерфейс датчика рівня води.
// Решта прошивки працює лише з цим інтерфейсом, тому заміна HC-SR04
// на тензодатчик (HX711) зводиться до нового класу-нащадка і одного рядка в main.cpp.
#pragma once
#include <math.h>

struct LevelReading {
  bool ok = false;
  const char* error = "";   // причина помилки, якщо ok == false
  float percent = 0.0f;     // наповнення пляшки, 0..100 %
  float distanceCm = NAN;   // лише для ультразвукового датчика
};

class LevelSensor {
 public:
  virtual ~LevelSensor() = default;
  virtual void begin() = 0;
  virtual LevelReading read() = 0;
  virtual const char* name() const = 0;  // для повідомлень про помилки
};
