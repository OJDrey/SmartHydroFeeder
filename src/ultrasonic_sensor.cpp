#include "ultrasonic_sensor.h"

#include "config.h"

UltrasonicSensor::UltrasonicSensor(uint8_t trigPin, uint8_t echoPin, const float& heightCm)
    : trigPin_(trigPin), echoPin_(echoPin), heightCm_(heightCm) {}

void UltrasonicSensor::begin() {
  pinMode(trigPin_, OUTPUT);
  pinMode(echoPin_, INPUT);
  digitalWrite(trigPin_, LOW);
}

float UltrasonicSensor::measureDistanceCm() {
  digitalWrite(trigPin_, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin_, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin_, LOW);
  unsigned long duration = pulseIn(echoPin_, HIGH, Config::ULTRASONIC_TIMEOUT_US);
  if (duration == 0) return NAN;
  return duration * 0.034f / 2.0f;  // швидкість звуку 340 м/с, шлях туди й назад
}

LevelReading UltrasonicSensor::read() {
  LevelReading r;
  float d = measureDistanceCm();
  r.distanceCm = d;

  if (isnan(d)) {
    r.error = "No echo";
  } else if (d < Config::ULTRASONIC_MIN_CM) {
    r.error = "Too close (<2cm)";
  } else if (d > Config::ULTRASONIC_MAX_CM) {
    r.error = "Too far (>5m)";
  } else {
    r.ok = true;
    r.percent = constrain((heightCm_ - d) / heightCm_ * 100.0f, 0.0f, 100.0f);
  }
  return r;
}
