#include "feeder.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "net.h"
#include "screen.h"
#include "settings.h"

namespace {
LevelSensor* sensor = nullptr;
unsigned long lastMeasure = 0;
unsigned long botLastSeen = 0;
unsigned long lastBotErrorAt = 0;
bool waitingForDouble = false;
unsigned long doubleClickDeadline = 0;

// Ключі параметрів усіх датчиків: команду для іншого датчика відхиляємо з поясненням
const char* const PARAM_KEYS[] = {"set_height", "set_capacity_ml"};

// ---------- MQTT-повідомлення ----------

void addHeader(JsonDocument& doc) {
  doc["device_id"] = Config::DEVICE_ID;
  doc["timestamp"] = millis() / 1000;  // секунди від увімкнення
}

void addReading(JsonDocument& doc, const LevelReading& r) {
  addHeader(doc);
  if (!isnan(r.distanceCm)) {
    doc["dist_cm"] = r.distanceCm;
    doc["height_cm"] = settings.heightCm;
  }
  if (!isnan(r.volumeMl)) {
    doc["volume_ml"] = r.volumeMl;
    doc["capacity_ml"] = settings.capacityMl;
  }
  doc["level_percent"] = r.percent;
  doc["low_water"] = r.percent < Config::LOW_WATER_PERCENT;
  doc["interval_min"] = settings.intervalMs / 60000.0f;
}

void publishError(const char* message) {
  JsonDocument doc;
  addHeader(doc);
  doc["error"] = message;
  Net::publish("error", doc);
}

void publishParam(const SensorParam& p) {
  JsonDocument doc;
  addHeader(doc);
  doc[p.key] = *p.value;
  Net::publish("setting", doc);
}

void publishMeasurement(const LevelReading& r) {
  JsonDocument doc;
  addReading(doc, r);
  Net::publish("measure", doc);
}

void publishLowWaterAlert(const LevelReading& r, bool isAuto) {
  JsonDocument doc;
  addReading(doc, r);
  doc["low_water"] = true;
  doc["triggered_by"] = isAuto ? "auto" : "manual";
  Net::publish("alert", doc);
}

// ---------- Допоміжне ----------

void blinkLed() {
  digitalWrite(Pins::LED, HIGH);
  delay(200);
  digitalWrite(Pins::LED, LOW);
}

// Кнопка в меню налаштувань: direction = +1 або -1 крок
void stepParam(int direction) {
  SensorParam p = sensor->param();
  *p.value = constrain(*p.value + direction * p.step, p.min, p.max);
  saveSettings();
  Serial.printf("%s changed: %.1f %s\n", p.label, *p.value, p.unit);
  Screen::showSettings(p);
  publishParam(p);
}

void checkBot(unsigned long now) {
  if (botLastSeen == 0 || now - botLastSeen <= Config::BOT_TIMEOUT_MS) return;
  if (lastBotErrorAt != 0 && now - lastBotErrorAt <= Config::BOT_ERROR_REPEAT_MS) return;
  Serial.println("Telegram bot not responding");
  publishError("Telegram bot not responding");
  lastBotErrorAt = now;
}

void logStatus(unsigned long now) {
  static unsigned long lastLog = 0;
  if (now - lastLog <= Config::STATUS_LOG_MS) return;
  lastLog = now;
  Serial.printf("WiFi:%d MQTT:%d botLastSeenAgo:%lu\n", Net::wifiConnected(), Net::mqttConnected(),
                botLastSeen == 0 ? 0UL : now - botLastSeen);
}

// ---------- Команди з Telegram ----------

bool handleParamCommand(JsonDocument& doc) {
  SensorParam p = sensor->param();
  bool changed = false;
  for (const char* key : PARAM_KEYS) {
    if (doc[key].isNull()) continue;
    if (strcmp(key, p.key) != 0) {
      publishError("This setting is not used by the installed sensor");
      continue;
    }
    float v = doc[key].as<float>();
    if (v >= p.min && v <= p.max) {
      *p.value = v;
      Serial.printf("%s updated via MQTT: %.1f %s\n", p.label, v, p.unit);
      changed = true;
    } else {
      publishError("Setting value out of range");
    }
  }
  return changed;
}

void handleTare() {
  if (!sensor->tare()) {
    publishError("Tare failed (not supported or sensor not responding)");
    return;
  }
  saveSettings();
  JsonDocument doc;
  addHeader(doc);
  doc["tare"] = "ok";
  Net::publish("setting", doc);
  Screen::showMessage("TARE DONE", "Empty bottle = 0 ml");
}

void handleCalibrate(float knownGrams) {
  if (!sensor->calibrate(knownGrams)) {
    publishError("Calibration failed (tare first, then put a known weight)");
    return;
  }
  saveSettings();
  JsonDocument doc;
  addHeader(doc);
  doc["calibrated_g"] = knownGrams;
  doc["scale_factor"] = settings.scaleFactor;
  Net::publish("setting", doc);
  Screen::showMessage("CALIBRATED", "Scale factor saved");
}

void handleCommand(const uint8_t* payload, unsigned int length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println("JSON parse error in command");
    return;
  }

  bool changed = handleParamCommand(doc);

  if (!doc["auto_enabled"].isNull()) {
    settings.autoEnabled = doc["auto_enabled"].as<bool>();
    Serial.printf("Auto mode: %s\n", settings.autoEnabled ? "ON" : "OFF");
    changed = true;
  }

  if (!doc["set_interval_min"].isNull()) {
    float m = doc["set_interval_min"].as<float>();
    if (m >= Config::INTERVAL_MIN_MIN && m <= Config::INTERVAL_MAX_MIN) {
      settings.intervalMs = (unsigned long)(m * 60000.0f);
      Serial.printf("Interval set: %.1f min\n", m);
      changed = true;
    }
  }

  if (changed) {
    saveSettings();
    Screen::showSettings(sensor->param(), Config::SETTINGS_SCREEN_AFTER_MQTT_MS);
  }

  if (doc["tare"] == true) handleTare();
  if (!doc["calibrate_g"].isNull()) handleCalibrate(doc["calibrate_g"].as<float>());
  if (doc["measure"] == true) Feeder::measure(false);
}
}  // namespace

namespace Feeder {

void begin(LevelSensor& levelSensor) {
  sensor = &levelSensor;
  pinMode(Pins::LED, OUTPUT);
  digitalWrite(Pins::LED, LOW);
  lastMeasure = millis();
}

void measure(bool isAuto) {
  unsigned long now = millis();
  LevelReading r = sensor->read();

  if (!r.ok) {
    Serial.printf("Measure error: %s\n", r.error);
    publishError("Sensor measure error or out of range");
    // Ручний замір: показати помилку, щоб було видно, що кнопка спрацювала
    if (isAuto) lastMeasure = now;
    else Screen::showSensorError(sensor->name(), r.error);
    return;
  }

  Serial.printf("Measure: Fill=%.1f %% (isAuto=%d)\n", r.percent, isAuto);
  bool low = r.percent < Config::LOW_WATER_PERCENT;

  if (isAuto) {
    // Автозамір не заважає користувачу: екран змінюється лише при нестачі води
    if (low) {
      Screen::showLowWater(r.percent);
      publishLowWaterAlert(r, true);
      blinkLed();
    }
    lastMeasure = now;
  } else {
    Screen::showReading(r);
    publishMeasurement(r);
    if (low) {
      publishLowWaterAlert(r, false);
      blinkLed();
    }
  }
}

void onButton(ButtonEvent event, unsigned long now) {
  bool inSettings = Screen::mode() == Screen::Mode::Settings;

  if (event == ButtonEvent::LongPress) {
    waitingForDouble = false;
    if (inSettings) Screen::showMain();
    else Screen::showSettings(sensor->param());
    return;
  }

  if (!inSettings) {
    measure(false);
    return;
  }

  // У налаштуваннях: одне натискання +крок, подвійне -крок
  if (!waitingForDouble) {
    waitingForDouble = true;
    doubleClickDeadline = now + Config::DOUBLE_CLICK_MS;
  } else {
    waitingForDouble = false;
    stepParam(-1);
  }
}

void onMessage(const char* subtopic, const uint8_t* payload, unsigned int length) {
  if (strcmp(subtopic, "bot") == 0) {
    botLastSeen = millis();
    lastBotErrorAt = 0;
    Serial.println("Bot heartbeat received");
    Screen::dismissError();
    return;
  }
  if (strcmp(subtopic, "cmd") == 0) handleCommand(payload, length);
}

void loop(unsigned long now) {
  // Друге натискання не прийшло вчасно: це було одинарне
  if (waitingForDouble && (long)(now - doubleClickDeadline) >= 0) {
    waitingForDouble = false;
    if (Screen::mode() == Screen::Mode::Settings) stepParam(+1);
  }

  if (settings.autoEnabled && now - lastMeasure >= settings.intervalMs) measure(true);

  checkBot(now);
  logStatus(now);
}

}  // namespace Feeder
