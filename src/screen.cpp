#include "screen.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "net.h"
#include "settings.h"

namespace {
Adafruit_SSD1306 oled(Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT, &Wire, -1);
Screen::Mode currentMode = Screen::Mode::Main;
unsigned long dataUntil = 0;
unsigned long settingsUntil = 0;
unsigned long errorUntil = 0;
int lastLinkState = -1;

void beginText() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
}

// Чи настав момент deadline (коректно і після переповнення millis())
bool reached(unsigned long now, unsigned long deadline) { return (long)(now - deadline) >= 0; }

void showDataScreen() {
  currentMode = Screen::Mode::Data;
  dataUntil = millis() + Config::DATA_SCREEN_MS;
}
}  // namespace

namespace Screen {

bool begin() {
  Wire.begin(Pins::SDA, Pins::SCL);
  return oled.begin(SSD1306_SWITCHCAPVCC, Config::OLED_ADDR);
}

Mode mode() { return currentMode; }

void showMain() {
  currentMode = Mode::Main;
  settingsUntil = 0;
  errorUntil = 0;
  beginText();
  oled.println("MAIN MODE");
  oled.println("Short press: Measure");
  oled.println("Long press: Settings");
  oled.println();
  // Стан зв'язку показуємо рядком, а не окремим екраном помилки,
  // щоб без мережі пристроєм можна було користуватися
  oled.printf("WiFi:%s MQTT:%s\n", Net::wifiConnected() ? "OK" : "--", Net::mqttConnected() ? "OK" : "--");
  oled.display();
}

void showSettings(const SensorParam& param, unsigned long timeoutMs) {
  currentMode = Mode::Settings;
  beginText();
  oled.println("SETTINGS");
  oled.printf("%s: %.0f %s\n", param.label, *param.value, param.unit);
  oled.printf("Interval: %.1f min\n", settings.intervalMs / 60000.0f);
  oled.printf("Auto: %s\n", settings.autoEnabled ? "ON" : "OFF");
  oled.printf("Refill: %s\n", settings.refillAuto ? "ON" : "OFF");
  oled.printf("Short: +%.0f %s\n", param.step, param.unit);
  oled.printf("Double: -%.0f %s\n", param.step, param.unit);
  oled.display();
  settingsUntil = millis() + timeoutMs;
}

void showReading(const LevelReading& reading) {
  beginText();
  if (!isnan(reading.distanceCm)) {
    oled.printf("Dist: %.1f cm\n", reading.distanceCm);
    oled.printf("Height: %.1f cm\n", settings.heightCm);
  }
  if (!isnan(reading.volumeMl)) {
    oled.printf("Water: %.0f ml\n", reading.volumeMl);
    oled.printf("Bottle: %.0f ml\n", settings.capacityMl);
  }
  oled.printf("Fill: %.1f %%\n", reading.percent);
  oled.display();
  showDataScreen();
}

void showRefill(float percent) {
  beginText();
  oled.println("REFILLING...");
  oled.println();
  oled.printf("Fill: %.1f %%\n", percent);
  oled.println();
  oled.println("Press button to stop");
  oled.display();
  showDataScreen();
}

void showMessage(const char* title, const char* line) {
  beginText();
  oled.println(title);
  oled.println();
  oled.println(line);
  oled.display();
  showDataScreen();
}

void showLowWater(float percent) {
  beginText();
  oled.println("! LOW WATER !");
  oled.printf("Fill: %.1f %%\n", percent);
  oled.println();
  oled.println("Please refill!");
  oled.display();
  showDataScreen();
}

void showSensorError(const char* sensorName, const char* reason) {
  beginText();
  oled.println("SENSOR ERROR");
  oled.println();
  oled.println(reason);
  oled.println();
  oled.printf("Check %s wiring\n", sensorName);
  oled.display();
  showDataScreen();
}

void showError(const char* message) {
  Serial.println(message);
  beginText();
  oled.println("ERROR:");
  oled.println(message);
  oled.display();
  errorUntil = millis() + Config::ERROR_SCREEN_MS;
}

void dismissError() {
  if (errorUntil != 0) showMain();
}

void showPortalInfo() {
  beginText();
  oled.println("WiFi setup");
  oled.println("Connect phone to:");
  oled.println(Config::AP_NAME);
  oled.println("Open 192.168.4.1");
  oled.display();
}

void showWifiResetPrompt() {
  beginText();
  oled.println("Hold button 3s");
  oled.println("to reset WiFi");
  oled.display();
}

void showWifiResetDone() {
  beginText();
  oled.println("WiFi reset done");
  oled.println("Restarting...");
  oled.display();
}

void update(unsigned long now) {
  // Оновити рядок стану на головному екрані, коли змінився стан зв'язку
  int linkState = (Net::wifiConnected() ? 1 : 0) + (Net::mqttConnected() ? 2 : 0);
  if (linkState != lastLinkState) {
    lastLinkState = linkState;
    if (currentMode == Mode::Main && errorUntil == 0) showMain();
  }

  if (errorUntil != 0 && reached(now, errorUntil)) {
    showMain();
  } else if (currentMode == Mode::Data && reached(now, dataUntil)) {
    showMain();
  } else if (currentMode == Mode::Settings && settingsUntil != 0 && reached(now, settingsUntil)) {
    showMain();
  }
}

}  // namespace Screen
