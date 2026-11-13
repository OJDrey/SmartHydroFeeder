#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define TRIG_PIN 32
#define ECHO_PIN 35
#define BTN_PIN 25
#define LED_PIN 2
#define SDA_PIN 26
#define SCL_PIN 27

const char* mqtt_server = "broker.hivemq.com";
const char* device_id = "device123";
const char* AP_NAME = "SmartHydroFeeder";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

float height = 18.0;
unsigned long lastMeasure = 0;
unsigned long interval = 10UL * 60UL * 1000UL;
bool autoEnabled = true;

const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long LONG_PRESS_THRESHOLD = 1500;
const unsigned long DOUBLE_CLICK_MS = 400;
const unsigned long WIFI_RESET_HOLD_MS = 3000;

int rawButtonState = HIGH;
int stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long pressStartTime = 0;
unsigned long doubleClickTimer = 0;
bool waitingForDouble = false;

enum ScreenState { SCREEN_MAIN, SCREEN_SETTING, SCREEN_DATA };
ScreenState currentScreen = SCREEN_MAIN;
unsigned long dataDisplayUntil = 0;
unsigned long settingDisplayUntil = 0;
const unsigned long SETTING_DISPLAY_MS_DEFAULT = 10000;
const unsigned long SETTING_DISPLAY_MS_AFTER_MQTT = 5000;
unsigned long errorDisplayUntil = 0;
const unsigned long ERROR_DISPLAY_MS = 10000;
unsigned long botLastSeen = 0;
const unsigned long BOT_TIMEOUT = 120000;
unsigned long lastBotErrorShownAt = 0;
const unsigned long BOT_ERROR_SUPPRESSION_MS = 15000;

void loadSettings() {
  prefs.begin("feeder", true);
  height = prefs.getFloat("height", 18.0);
  interval = prefs.getULong("interval", 10UL * 60UL * 1000UL);
  autoEnabled = prefs.getBool("auto", true);
  prefs.end();
}

void saveSettings() {
  prefs.begin("feeder", false);
  prefs.putFloat("height", height);
  prefs.putULong("interval", interval);
  prefs.putBool("auto", autoEnabled);
  prefs.end();
}

void beginText() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void showMainMenu() {
  currentScreen = SCREEN_MAIN;
  beginText();
  display.println("MAIN MODE");
  display.println("Short press: Measure");
  display.println("Long press: Settings");
  display.println();
  // Стан зв'язку показуємо рядком, а не окремим екраном помилки,
  // щоб без мережі пристроєм можна було користуватися
  display.printf("WiFi:%s MQTT:%s\n",
                 WiFi.status() == WL_CONNECTED ? "OK" : "--",
                 mqttClient.connected() ? "OK" : "--");
  display.display();
}

void showSettingMenu(unsigned long timeout_ms = SETTING_DISPLAY_MS_DEFAULT) {
  currentScreen = SCREEN_SETTING;
  beginText();
  display.println("SETTINGS");
  display.printf("Height: %.1f cm\n", height);
  display.printf("Interval: %.1f min\n", interval / 60000.0);
  display.printf("Auto: %s\n", autoEnabled ? "ON" : "OFF");
  display.println();
  display.println("Short: +1cm");
  display.println("Double: -1cm");
  display.display();
  settingDisplayUntil = millis() + timeout_ms;
}

void showData(float dist, float percent) {
  currentScreen = SCREEN_DATA;
  beginText();
  if (dist > 500.0 || dist < 2.0) {
    display.println("Measure error");
  } else {
    display.printf("Dist: %.1f cm\n", dist);
    display.printf("Height: %.1f cm\n", height);
    display.printf("Fill: %.1f %%\n", percent);
  }
  display.display();
}

void showLowWaterWarning(float percent) {
  currentScreen = SCREEN_DATA;
  beginText();
  display.println("! LOW WATER !");
  display.printf("Fill: %.1f %%\n", percent);
  display.println();
  display.println("Please refill!");
  display.display();
}

void showTransientError(const char* msg, unsigned long ms = ERROR_DISPLAY_MS) {
  Serial.println(msg);
  beginText();
  display.println("ERROR:");
  display.println(msg);
  display.display();
  errorDisplayUntil = millis() + ms;
}

void showPortalInfo(WiFiManager* wm) {
  beginText();
  display.println("WiFi setup");
  display.println("Connect phone to:");
  display.println(AP_NAME);
  display.println("Open 192.168.4.1");
  display.display();
}

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999.0;
  return duration * 0.034 / 2.0;
}

void publishJsonTopic(const char* topic, JsonDocument& doc) {
  if (!(WiFi.status() == WL_CONNECTED && mqttClient.connected())) {
    Serial.println("MQTT not connected, skip publish");
    return;  // екран не чіпаємо: інакше результат заміру одразу зникає
  }
  char buf[512];
  size_t n = serializeJson(doc, buf);
  Serial.print("Publishing to ");
  Serial.print(topic);
  Serial.print(" : ");
  Serial.println(buf);
  mqttClient.publish(topic, buf, n);
}

void publishError(const char* errMsg) {
  StaticJsonDocument<200> doc;
  doc["device_id"] = device_id;
  doc["error"] = errMsg;
  doc["timestamp"] = millis() / 1000;
  String topic = String("smartfeeder/") + device_id + "/error";
  publishJsonTopic(topic.c_str(), doc);
}

void publishHeightChange() {
  StaticJsonDocument<200> doc;
  doc["device_id"] = device_id;
  doc["set_height"] = height;
  doc["timestamp"] = millis() / 1000;
  String topic = String("smartfeeder/") + device_id + "/setting";
  publishJsonTopic(topic.c_str(), doc);
}

void publishMeasurement(float dist, float percent) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = device_id;
  doc["dist_cm"] = dist;
  doc["level_percent"] = percent;
  doc["height_cm"] = height;
  doc["low_water"] = percent < 20.0;
  doc["timestamp"] = millis() / 1000;
  doc["interval_min"] = (float)interval / 60000.0;
  String topic = String("smartfeeder/") + device_id + "/measure";
  publishJsonTopic(topic.c_str(), doc);
}

void publishLowWaterAlert(float dist, float percent, bool isAuto) {
  StaticJsonDocument<256> alert;
  alert["device_id"] = device_id;
  alert["dist_cm"] = dist;
  alert["level_percent"] = percent;
  alert["height_cm"] = height;
  alert["low_water"] = true;
  alert["timestamp"] = millis() / 1000;
  alert["interval_min"] = (float)interval / 60000.0;
  alert["triggered_by"] = isAuto ? "auto" : "manual";
  String topic = String("smartfeeder/") + device_id + "/alert";
  publishJsonTopic(topic.c_str(), alert);
}

void blinkLed() {
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
}

void performMeasurement(bool isAuto) {
  unsigned long now = millis();
  float d = measureDistance();

  if (d == 999.0 || d > 500.0 || d < 2.0) {
    // Причина помилки: немає відлуння (ECHO не піднявся) або відстань поза діапазоном
    const char* reason = (d == 999.0) ? "No echo" : (d < 2.0 ? "Too close (<2cm)" : "Too far (>5m)");
    Serial.printf("Measure error: %s (d=%.1f)\n", reason, d);
    publishError("Sensor measure error or out of range");
    if (!isAuto) {
      // Ручний замір: показати помилку, щоб було видно, що кнопка спрацювала
      currentScreen = SCREEN_DATA;
      beginText();
      display.println("SENSOR ERROR");
      display.println();
      display.println(reason);
      display.println();
      display.println("Check HC-SR04 wiring");
      display.display();
      dataDisplayUntil = millis() + 5000;
    }
    if (isAuto) lastMeasure = now;
    return;
  }

  float percent = constrain(((height - d) / height) * 100.0, 0.0, 100.0);
  Serial.printf("Measure: Dist=%.1f cm, Fill=%.1f %% (isAuto=%d)\n", d, percent, isAuto);

  if (isAuto) {
    if (percent < 20.0) {
      showLowWaterWarning(percent);
      dataDisplayUntil = millis() + 5000;
      publishLowWaterAlert(d, percent, true);
      blinkLed();
    }
    lastMeasure = now;
  } else {
    showData(d, percent);
    dataDisplayUntil = millis() + 5000;
    publishMeasurement(d, percent);
    if (percent < 20.0) {
      publishLowWaterAlert(d, percent, false);
      blinkLed();
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);

  if (t.endsWith("/bot")) {
    botLastSeen = millis();
    Serial.println("Bot heartbeat received");
    if (errorDisplayUntil != 0) {
      errorDisplayUntil = 0;
      showMainMenu();
    }
    lastBotErrorShownAt = 0;
    return;
  }

  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.println("JSON parse error in mqttCallback");
    return;
  }

  bool anySettingChanged = false;

  if (doc.containsKey("set_height")) {
    float h = doc["set_height"];
    if (h > 2.0 && h < 200.0) {
      height = h;
      Serial.printf("Height updated via MQTT: %.1f cm\n", height);
      anySettingChanged = true;
      showSettingMenu(SETTING_DISPLAY_MS_AFTER_MQTT);
    } else {
      publishError("Invalid set_height value received");
    }
  }

  if (doc.containsKey("auto_enabled")) {
    autoEnabled = doc["auto_enabled"];
    Serial.printf("Auto mode: %s\n", autoEnabled ? "ON" : "OFF");
    anySettingChanged = true;
  }

  if (doc.containsKey("set_interval_min")) {
    float m = doc["set_interval_min"];
    if (m >= 0.1 && m <= 10080.0) {
      interval = (unsigned long)(m * 60.0 * 1000.0);
      Serial.printf("Interval set: %.1f min\n", m);
      anySettingChanged = true;
    }
  }

  if (anySettingChanged) {
    saveSettings();
    showSettingMenu();
  }

  if (doc.containsKey("measure") && doc["measure"] == true) {
    performMeasurement(false);
  }
}

void setup_wifi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setAPCallback(showPortalInfo);
  if (!wm.autoConnect(AP_NAME)) {
    Serial.println("WiFi connect fail - continuing without WiFi");
    showTransientError("WiFi connect fail");
    WiFi.mode(WIFI_STA);  // далі maintainWifi() пробуватиме підключитися у фоні
    return;
  }
  WiFi.setAutoReconnect(true);
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void checkWifiReset() {
  beginText();
  display.println("Hold button 3s");
  display.println("to reset WiFi");
  display.display();
  unsigned long start = millis();
  while (millis() - start < WIFI_RESET_HOLD_MS + 1000) {
    if (digitalRead(BTN_PIN) == HIGH) return;
    if (millis() - start >= WIFI_RESET_HOLD_MS) {
      WiFiManager wm;
      wm.resetSettings();
      beginText();
      display.println("WiFi reset done");
      display.println("Restarting...");
      display.display();
      delay(1500);
      ESP.restart();
    }
    delay(20);
  }
}

void mqttReconnect() {
  static unsigned long lastAttempt = 0;
  unsigned long now = millis();
  if (mqttClient.connected()) return;
  if (now - lastAttempt < 5000) return;
  lastAttempt = now;

  Serial.print("Connecting MQTT...");
  if (mqttClient.connect(device_id)) {
    Serial.println("connected");
    mqttClient.subscribe((String("smartfeeder/") + device_id + "/cmd").c_str());
    mqttClient.subscribe((String("smartfeeder/") + device_id + "/bot").c_str());
  } else {
    Serial.print("failed rc=");
    Serial.println(mqttClient.state());
  }
}

// Якщо Wi-Fi немає (наприклад, роутер увімкнувся пізніше за ESP32),
// кожні 30 с пробуємо підключитися до збереженої мережі.
void maintainWifi() {
  static unsigned long lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastTry < 30000) return;
  lastTry = millis();
  Serial.println("WiFi lost, reconnecting...");
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);  // вимкнути точку доступу після порталу
  WiFi.begin();  // без параметрів: мережа і пароль, збережені WiFiManager
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED error");
    for (;;);
  }
  loadSettings();
  if (digitalRead(BTN_PIN) == LOW) checkWifiReset();
  showMainMenu();
  setup_wifi();
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
  showMainMenu();
  lastMeasure = millis();
}

void loop() {
  maintainWifi();
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) mqttReconnect();
    mqttClient.loop();
  }

  unsigned long now = millis();

  // Оновити рядок стану на головному екрані, коли змінився стан зв'язку
  static int lastLinkState = -1;
  int linkState = (WiFi.status() == WL_CONNECTED ? 1 : 0) + (mqttClient.connected() ? 2 : 0);
  if (linkState != lastLinkState) {
    lastLinkState = linkState;
    if (currentScreen == SCREEN_MAIN && errorDisplayUntil == 0) showMainMenu();
  }

  if (botLastSeen != 0) {
    unsigned long ago = (now >= botLastSeen) ? (now - botLastSeen) : 0;
    if (ago > BOT_TIMEOUT) {
      if (lastBotErrorShownAt == 0 || (now - lastBotErrorShownAt) > BOT_ERROR_SUPPRESSION_MS) {
        Serial.println("Telegram bot not responding");
        publishError("Telegram bot not responding");
        lastBotErrorShownAt = now;
      }
    }
  }

  static unsigned long lastStatusLog = 0;
  if (now - lastStatusLog > 5000) {
    lastStatusLog = now;
    Serial.printf("WiFi:%d MQTT:%d botLastSeenAgo:%lu\n",
                  WiFi.status() == WL_CONNECTED,
                  mqttClient.connected(),
                  (botLastSeen == 0) ? 0 : ((now >= botLastSeen) ? (now - botLastSeen) : 0));
  }

  int reading = digitalRead(BTN_PIN);
  if (reading != rawButtonState) {
    lastDebounceTime = now;
    rawButtonState = reading;
  }
  if ((now - lastDebounceTime) > DEBOUNCE_DELAY && reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW) {
      pressStartTime = now;
    } else {
      unsigned long pressDuration = now - pressStartTime;
      if (pressDuration >= LONG_PRESS_THRESHOLD) {
        if (currentScreen == SCREEN_SETTING) {
          showMainMenu();
          settingDisplayUntil = 0;
          waitingForDouble = false;
          doubleClickTimer = 0;
        } else {
          showSettingMenu();
        }
      } else if (currentScreen == SCREEN_SETTING) {
        if (!waitingForDouble) {
          waitingForDouble = true;
          doubleClickTimer = now + DOUBLE_CLICK_MS;
        } else {
          waitingForDouble = false;
          height = max(2.0f, height - 1.0f);
          saveSettings();
          Serial.printf("Height decreased: %.1f cm\n", height);
          showSettingMenu();
          publishHeightChange();
        }
      } else {
        performMeasurement(false);
      }
    }
  }

  if (waitingForDouble && doubleClickTimer != 0 && now >= doubleClickTimer) {
    waitingForDouble = false;
    doubleClickTimer = 0;
    if (currentScreen == SCREEN_SETTING) {
      height = min(200.0f, height + 1.0f);
      saveSettings();
      Serial.printf("Height increased: %.1f cm\n", height);
      showSettingMenu();
      publishHeightChange();
    }
  }

  if (autoEnabled && (now - lastMeasure >= interval)) performMeasurement(true);

  if (currentScreen == SCREEN_DATA && now >= dataDisplayUntil) showMainMenu();

  if (currentScreen == SCREEN_SETTING && settingDisplayUntil != 0 && now >= settingDisplayUntil) {
    showMainMenu();
    settingDisplayUntil = 0;
    waitingForDouble = false;
    doubleClickTimer = 0;
  }

  if (errorDisplayUntil != 0 && now >= errorDisplayUntil) {
    errorDisplayUntil = 0;
    showMainMenu();
  }

  delay(10);
}