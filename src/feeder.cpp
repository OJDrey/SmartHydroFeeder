#include "feeder.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <refill_controller.h>

#include "buzzer.h"
#include "config.h"
#include "net.h"
#include "screen.h"
#include "settings.h"

namespace {
LevelSensor* sensor = nullptr;
Buzzer buzzer(Pins::BUZZER);
RefillController refill(RefillConfig{
    Config::REFILL_START_PERCENT,
    Config::REFILL_TARGET_PERCENT,
    Config::REFILL_MAX_RUN_MS,
    Config::REFILL_DRY_CHECK_MS,
    Config::REFILL_MIN_RISE_PERCENT,
    Config::REFILL_COOLDOWN_MS,
});
unsigned long lastRefillPoll = 0;
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
  uint32_t epoch = Net::epochTime();
  if (epoch) doc["timestamp"] = epoch;  // Unix-час UTC; без NTP поле відсутнє, бот бере час отримання
  doc["uptime_s"] = millis() / 1000;
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

void publishMeasurement(const LevelReading& r, bool isAuto) {
  JsonDocument doc;
  addReading(doc, r);
  doc["source"] = isAuto ? "auto" : "manual";  // бот пише в чат лише ручні заміри, а зберігає всі
  Net::publish("measure", doc);
}

// Сповіщення про низький рівень надсилається один раз при переході вниз через поріг,
// а не на кожному замірі; знову "озброюється", коли рівень піднявся вище порогу + гістерезис.
bool lowAlertSent = false;

void publishLowWaterAlert(const LevelReading& r, bool isAuto);

void updateLowAlert(const LevelReading& r, bool isAuto) {
  if (r.percent >= Config::LOW_WATER_PERCENT + Config::LOW_WATER_HYSTERESIS) {
    lowAlertSent = false;
    return;
  }
  if (r.percent >= Config::LOW_WATER_PERCENT || lowAlertSent) return;
  lowAlertSent = true;
  publishLowWaterAlert(r, isAuto);
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

// ---------- Автодолив ----------

bool floatHigh() { return digitalRead(Pins::FLOAT_SW) == Config::FLOAT_ACTIVE_LEVEL; }

LevelSample sampleFrom(const LevelReading& r) { return {r.ok, r.percent, floatHigh()}; }

const char* refillEventName(RefillEvent e) {
  switch (e) {
    case RefillEvent::Started: return "started";
    case RefillEvent::Done: return "done";
    case RefillEvent::FloatStop: return "float_stop";
    case RefillEvent::Stopped: return "stopped";
    case RefillEvent::AlreadyFull: return "already_full";
    case RefillEvent::DryRun: return "dry_run";
    case RefillEvent::Timeout: return "timeout";
    case RefillEvent::SensorError: return "sensor_error";
    default: return "none";
  }
}

// Насос, екран, зумер і повідомлення в Telegram за подією автомата
void handleRefillEvent(RefillEvent e, float percent) {
  digitalWrite(Pins::PUMP, refill.pumpOn() ? HIGH : LOW);
  if (e == RefillEvent::None) return;

  const char* name = refillEventName(e);
  Serial.printf("Refill: %s (%.1f %%)\n", name, percent);

  JsonDocument doc;
  addHeader(doc);
  doc["event"] = name;
  if (!isnan(percent)) doc["level_percent"] = percent;
  Net::publish("refill", doc);

  switch (e) {
    case RefillEvent::Started:
      Screen::showRefill(percent);
      buzzer.beep(1);
      break;
    case RefillEvent::Done:
      Screen::showMessage("REFILL DONE", "Bottle is full");
      buzzer.beep(2);
      break;
    case RefillEvent::FloatStop:
      Screen::showMessage("REFILL DONE", "Stopped by float");
      buzzer.beep(2);
      break;
    case RefillEvent::Stopped:
      Screen::showMessage("REFILL STOPPED", "Stopped manually");
      buzzer.beep(1);
      break;
    case RefillEvent::AlreadyFull:
      Screen::showMessage("NO REFILL", "Bottle is full");
      break;
    case RefillEvent::DryRun:
      Screen::showError("Refill: reservoir empty?");
      buzzer.beep(3, 400, 200);
      break;
    case RefillEvent::Timeout:
      Screen::showError("Refill: pump timeout");
      buzzer.beep(3, 400, 200);
      break;
    case RefillEvent::SensorError:
      Screen::showError("Refill: sensor error");
      buzzer.beep(3, 400, 200);
      break;
    default:
      break;
  }
}

// Під час доливу рівень міряється часто, а поплавок — у кожному loop()
void pollRefill(unsigned long now) {
  if (!refill.pumpOn()) return;
  if (floatHigh()) {
    LevelSample s = {false, 0.0f, true};
    handleRefillEvent(refill.update(now, s, settings.refillAuto), NAN);
    return;
  }
  if (now - lastRefillPoll < Config::REFILL_POLL_MS) return;
  lastRefillPoll = now;

  LevelReading r = sensor->read();
  RefillEvent e = refill.update(now, sampleFrom(r), settings.refillAuto);
  if (e == RefillEvent::None) Screen::showRefill(r.percent);
  else handleRefillEvent(e, r.ok ? r.percent : NAN);
}

void startRefillManually() {
  LevelReading r = sensor->read();
  handleRefillEvent(refill.requestStart(millis(), sampleFrom(r)), r.ok ? r.percent : NAN);
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

  if (!doc["refill_auto"].isNull()) {
    settings.refillAuto = doc["refill_auto"].as<bool>();
    Serial.printf("Auto refill: %s\n", settings.refillAuto ? "ON" : "OFF");
    changed = true;
  }

  if (changed) {
    saveSettings();
    Screen::showSettings(sensor->param(), Config::SETTINGS_SCREEN_AFTER_MQTT_MS);
  }

  if (doc["refill_stop"] == true) handleRefillEvent(refill.stop(millis()), NAN);
  if (doc["refill"] == true) startRefillManually();
  if (doc["tare"] == true) handleTare();
  if (!doc["calibrate_g"].isNull()) handleCalibrate(doc["calibrate_g"].as<float>());
  if (doc["measure"] == true) Feeder::measure(false);
}
}  // namespace

namespace Feeder {

void begin(LevelSensor& levelSensor) {
  sensor = &levelSensor;
  // Насос вимикаємо найпершим: до цього пін "висить", і затвор тримає лише резистор на GND
  pinMode(Pins::PUMP, OUTPUT);
  digitalWrite(Pins::PUMP, LOW);
  pinMode(Pins::FLOAT_SW, INPUT);
  pinMode(Pins::LED, OUTPUT);
  digitalWrite(Pins::LED, LOW);
  buzzer.begin();
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
  RefillEvent refillEvent = refill.update(now, sampleFrom(r), settings.refillAuto);

  if (isAuto) {
    // Автозамір не заважає користувачу: екран змінюється лише при нестачі води
    if (low) Screen::showLowWater(r.percent);
    lastMeasure = now;
  } else {
    Screen::showReading(r);
  }
  if (low) blinkLed();
  publishMeasurement(r, isAuto);
  updateLowAlert(r, isAuto);

  handleRefillEvent(refillEvent, r.percent);  // після екрана заміру, щоб показати долив
}

void onButton(ButtonEvent event, unsigned long now) {
  // Будь-яке натискання під час доливу зупиняє насос
  if (refill.pumpOn()) {
    handleRefillEvent(refill.stop(now), NAN);
    return;
  }

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

  pollRefill(now);
  buzzer.update(now);
  checkBot(now);
  logStatus(now);
}

}  // namespace Feeder
