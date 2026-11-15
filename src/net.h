// Мережевий рівень: Wi-Fi (WiFiManager) і MQTT.
// Модуль нічого не знає про логіку поїлки: він лише доставляє повідомлення.
#pragma once
#include <ArduinoJson.h>

namespace Net {

// subtopic — частина топіка після "smartfeeder/<DEVICE_ID>/", наприклад "cmd" або "bot"
using MessageHandler = void (*)(const char* subtopic, const uint8_t* payload, unsigned int length);

// Підключення до Wi-Fi. Якщо мережа не збережена, відкривається портал
// налаштування, і викликається onPortalStart. Повертає false, якщо не вдалося.
bool connectWifi(void (*onPortalStart)());
void beginMqtt(MessageHandler handler);
void beginClock();             // синхронізація часу через NTP (у фоні, щойно з'явиться мережа)
uint32_t epochTime();          // Unix-час у секундах (UTC); 0, якщо час ще не синхронізовано
void loop();  // підтримка зв'язку, викликати в кожному loop()

bool wifiConnected();
bool mqttConnected();

// Надіслати JSON у топік smartfeeder/<DEVICE_ID>/<subtopic>. Без зв'язку повертає false.
bool publish(const char* subtopic, JsonDocument& doc);

void resetWifiSettings();

}  // namespace Net
