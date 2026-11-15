// SmartHydroFeeder: розумна поїлка для дрібних домашніх тварин (ESP32).
// Тут лише збирання модулів докупи; логіка розкладена по файлах:
//   config.h            піни та константи
//   settings.*          налаштування в NVS
//   level_sensor.h      інтерфейс датчика рівня
//   ultrasonic_sensor.* HC-SR04
//   scale_sensor.*      тензодатчик з HX711
//   lib/refill/         автомат доливу (чиста логіка, покрита тестами)
//   buzzer.*            зумер
//   button.*            кнопка з антибрязкотом
//   screen.*            екрани OLED
//   net.*               Wi-Fi і MQTT
//   feeder.*            логіка поїлки
#include <Arduino.h>

#include "button.h"
#include "config.h"
#include "feeder.h"
#include "net.h"
#include "screen.h"
#include "settings.h"

// Датчик рівня вибирається прапорцем збірки USE_SCALE_SENSOR (див. platformio.ini)
#ifdef USE_SCALE_SENSOR
#include "scale_sensor.h"
static ScaleSensor levelSensor(Pins::HX711_DT, Pins::HX711_SCK, settings.scaleOffset, settings.scaleFactor,
                               settings.capacityMl);
#else
#include "ultrasonic_sensor.h"
static UltrasonicSensor levelSensor(Pins::TRIG, Pins::ECHO, settings.heightCm);
#endif
static Button button(Pins::BUTTON, Config::DEBOUNCE_MS, Config::LONG_PRESS_MS);

// Кнопка, затиснута під час увімкнення і втримана 3 с, стирає збережену мережу Wi-Fi
static void checkWifiReset() {
  Screen::showWifiResetPrompt();
  unsigned long start = millis();
  while (millis() - start < Config::WIFI_RESET_HOLD_MS + 1000) {
    if (!button.isPressed()) return;
    if (millis() - start >= Config::WIFI_RESET_HOLD_MS) {
      Net::resetWifiSettings();
      Screen::showWifiResetDone();
      delay(1500);
      ESP.restart();
    }
    delay(20);
  }
}

void setup() {
  Serial.begin(115200);
  loadSettings();
  Feeder::begin(levelSensor);  // першим ділом вимикає насос
  levelSensor.begin();
  button.begin();

  if (!Screen::begin()) {
    Serial.println("OLED error");
    for (;;) delay(1000);
  }

  if (button.isPressed()) checkWifiReset();

  Screen::showMain();
  bool wifiOk = Net::connectWifi(Screen::showPortalInfo);
  Net::beginMqtt(Feeder::onMessage);
  Net::beginClock();

  if (wifiOk) Screen::showMain();
  else Screen::showError("WiFi connect fail");
}

void loop() {
  Net::loop();

  unsigned long now = millis();
  ButtonEvent event = button.poll(now);
  if (event != ButtonEvent::None) Feeder::onButton(event, now);

  Feeder::loop(now);
  Screen::update(now);

  delay(10);
}
