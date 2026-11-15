#include "scale_sensor.h"

#include "config.h"

ScaleSensor::ScaleSensor(uint8_t dtPin, uint8_t sckPin, long& offset, float& factor, float& capacityMl)
    : dtPin_(dtPin), sckPin_(sckPin), offset_(offset), factor_(factor), capacityMl_(capacityMl) {}

void ScaleSensor::begin() { hx_.begin(dtPin_, sckPin_); }

SensorParam ScaleSensor::param() {
  return {"set_capacity_ml", "Capacity", "ml", &capacityMl_, Config::CAPACITY_STEP_ML,
          Config::CAPACITY_MIN_ML, Config::CAPACITY_MAX_ML};
}

bool ScaleSensor::readRaw(long& raw) {
  // Без цієї перевірки бібліотека чекала б на HX711 вічно, якщо він не підключений
  if (!hx_.wait_ready_timeout(Config::SCALE_READY_TIMEOUT_MS)) return false;
  raw = hx_.read_average(Config::SCALE_SAMPLES);
  return true;
}

LevelReading ScaleSensor::read() {
  LevelReading r;
  long raw;
  if (!readRaw(raw)) {
    r.error = "HX711 not responding";
    return r;
  }
  if (factor_ == 0.0f) factor_ = Config::DEFAULT_SCALE_FACTOR;

  float grams = (raw - offset_) / factor_;
  Serial.printf("Scale: raw=%ld grams=%.1f\n", raw, grams);

  if (grams < -Config::SCALE_REMOVED_G) {
    r.error = "Bottle removed?";  // вага менша за порожню пляшку
  } else if (grams > capacityMl_ * 1.5f) {
    r.error = "Overload";
  } else {
    r.ok = true;
    r.volumeMl = max(0.0f, grams);
    r.percent = constrain(r.volumeMl / capacityMl_ * 100.0f, 0.0f, 100.0f);
  }
  return r;
}

bool ScaleSensor::tare() {
  long raw;
  if (!readRaw(raw)) return false;
  offset_ = raw;
  Serial.printf("Scale tared: offset=%ld\n", offset_);
  return true;
}

bool ScaleSensor::calibrate(float knownGrams) {
  if (knownGrams <= 0.0f) return false;
  long raw;
  if (!readRaw(raw)) return false;
  long delta = raw - offset_;
  if (delta == 0) return false;  // вантаж не відчувається
  factor_ = delta / knownGrams;
  Serial.printf("Scale calibrated: factor=%.4f counts/g\n", factor_);
  return true;
}
