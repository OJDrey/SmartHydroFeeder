#include "net.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

#include "ca_certs.h"
#include "config.h"

namespace {
WiFiClient plainClient;
WiFiClientSecure tlsClient;  // шифроване з'єднання з перевіркою сертифіката брокера
PubSubClient mqtt;
Net::MessageHandler messageHandler = nullptr;
void (*portalCallback)() = nullptr;
String topicPrefix;  // "smartfeeder/<DEVICE_ID>/"

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (!messageHandler) return;
  String t(topic);
  if (!t.startsWith(topicPrefix)) return;
  messageHandler(t.c_str() + topicPrefix.length(), payload, length);
}

// Якщо Wi-Fi немає (наприклад, роутер увімкнувся пізніше за ESP32),
// періодично пробуємо підключитися до збереженої мережі.
void maintainWifi() {
  static unsigned long lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastTry < Config::WIFI_RETRY_MS) return;
  lastTry = millis();
  Serial.println("WiFi lost, reconnecting...");
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);  // вимкнути точку доступу після порталу
  WiFi.begin();  // без параметрів: мережа і пароль, збережені WiFiManager
}

void maintainMqtt() {
  static unsigned long lastAttempt = 0;
  if (mqtt.connected()) return;
  if (millis() - lastAttempt < Config::MQTT_RETRY_MS) return;
  lastAttempt = millis();

  Serial.printf("Connecting MQTT %s:%u (%s)...", Config::MQTT_SERVER, Config::MQTT_PORT,
                Config::MQTT_USE_TLS ? "TLS" : "plain");
  bool ok = Config::MQTT_USER[0] ? mqtt.connect(Config::DEVICE_ID, Config::MQTT_USER, Config::MQTT_PASS)
                                 : mqtt.connect(Config::DEVICE_ID);
  if (ok) {
    Serial.println("connected");
    mqtt.subscribe((topicPrefix + "cmd").c_str());
    mqtt.subscribe((topicPrefix + "bot").c_str());
  } else {
    // rc=-2: немає з'єднання (адреса, порт, TLS); rc=4: неправильний логін/пароль; rc=5: немає дозволу
    Serial.print("failed rc=");
    Serial.println(mqtt.state());
    if (Config::MQTT_USE_TLS) {
      char err[100];
      if (tlsClient.lastError(err, sizeof(err)) != 0) Serial.printf("TLS error: %s\n", err);
    }
  }
}
}  // namespace

namespace Net {

bool connectWifi(void (*onPortalStart)()) {
#ifdef WOKWI_SIM
  // У симуляторі Wokwi немає телефона для порталу WiFiManager,
  // тому підключаємося напряму до його віртуальної мережі.
  (void)onPortalStart;
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(250);
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.println(ok ? "WiFi connected (Wokwi)" : "WiFi connect fail (Wokwi)");
  return ok;
#else
  portalCallback = onPortalStart;
  WiFiManager wm;
  wm.setConfigPortalTimeout(Config::PORTAL_TIMEOUT_S);
  wm.setAPCallback([](WiFiManager*) {
    if (portalCallback) portalCallback();
  });
  if (!wm.autoConnect(Config::AP_NAME)) {
    Serial.println("WiFi connect fail - continuing without WiFi");
    WiFi.mode(WIFI_STA);  // далі maintainWifi() пробуватиме підключитися у фоні
    return false;
  }
  WiFi.setAutoReconnect(true);
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  return true;
#endif
}

void beginMqtt(MessageHandler handler) {
  messageHandler = handler;
  topicPrefix = String(Config::TOPIC_ROOT) + "/" + Config::DEVICE_ID + "/";
  if (Config::MQTT_USE_TLS) {
    tlsClient.setCACert(MQTT_CA_CERTS);
    mqtt.setClient(tlsClient);
  } else {
    mqtt.setClient(plainClient);
  }
  mqtt.setServer(Config::MQTT_SERVER, Config::MQTT_PORT);
  mqtt.setBufferSize(512);  // стандартних 256 байт замало для JSON із запасом
  mqtt.setCallback(onMqttMessage);
}

void beginClock() {
  // Час у UTC; у місцевий його переводить бот
  configTime(0, 0, Config::NTP_SERVER_1, Config::NTP_SERVER_2);
}

uint32_t epochTime() {
  time_t now = time(nullptr);
  return now > Config::MIN_VALID_EPOCH ? (uint32_t)now : 0;
}

void loop() {
  maintainWifi();
  if (WiFi.status() != WL_CONNECTED) return;
  maintainMqtt();
  mqtt.loop();
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

bool mqttConnected() { return mqtt.connected(); }

bool publish(const char* subtopic, JsonDocument& doc) {
  if (!(wifiConnected() && mqtt.connected())) {
    Serial.println("MQTT not connected, skip publish");
    return false;
  }
  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  String topic = topicPrefix + subtopic;
  Serial.printf("Publishing to %s : %s\n", topic.c_str(), buf);
  // Перевантаження з uint8_t* і довжиною: повідомлення НЕ retained
  return mqtt.publish(topic.c_str(), reinterpret_cast<const uint8_t*>(buf), n, false);
}

void resetWifiSettings() {
  WiFiManager wm;
  wm.resetSettings();
}

}  // namespace Net
