// Піни та константи пристрою. Усі "магічні числа" прошивки зібрані тут.
#pragma once
#include <Arduino.h>
#include <time.h>

// Облікові дані брокера (див. secrets.example.h). Файла немає — працюємо з відкритим брокером.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace Pins {
constexpr uint8_t TRIG = 32;    // HC-SR04 TRIG
constexpr uint8_t ECHO = 35;    // HC-SR04 ECHO (через дільник 1 кОм / 2 кОм)
constexpr uint8_t BUTTON = 25;  // кнопка на GND, INPUT_PULLUP
constexpr uint8_t LED = 2;      // вбудований світлодіод
constexpr uint8_t SDA = 26;     // OLED I2C
constexpr uint8_t SCL = 27;
constexpr uint8_t HX711_DT = 19;   // тензодатчик
constexpr uint8_t HX711_SCK = 23;
constexpr uint8_t PUMP = 33;       // затвор MOSFET IRLZ44N (з резистором 10 кОм на GND)
constexpr uint8_t FLOAT_SW = 34;   // поплавок; GPIO34 без внутрішньої підтяжки, потрібна 10 кОм на 3V3
constexpr uint8_t BUZZER = 4;      // активний зумер
}  // namespace Pins

namespace Config {
// Мережа
constexpr const char* DEVICE_ID = "device123";
constexpr const char* AP_NAME = "SmartHydroFeeder";
#ifdef SECRET_MQTT_HOST
// Хмарний брокер: TLS (порт 8883) і вхід за логіном і паролем
constexpr const char* MQTT_SERVER = SECRET_MQTT_HOST;
constexpr uint16_t MQTT_PORT = SECRET_MQTT_PORT;
constexpr bool MQTT_USE_TLS = true;
constexpr const char* MQTT_USER = SECRET_MQTT_USER;
constexpr const char* MQTT_PASS = SECRET_MQTT_PASS;
#else
// Відкритий публічний брокер (лише для розробки): без шифрування і пароля
constexpr const char* MQTT_SERVER = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr bool MQTT_USE_TLS = false;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASS = "";
#endif
constexpr const char* TOPIC_ROOT = "smartfeeder";  // топіки: smartfeeder/<DEVICE_ID>/<subtopic>
constexpr unsigned long WIFI_RETRY_MS = 30000;
constexpr unsigned long MQTT_RETRY_MS = 5000;
constexpr unsigned long PORTAL_TIMEOUT_S = 180;
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";
constexpr time_t MIN_VALID_EPOCH = 1700000000;  // до синхронізації ESP32 показує 1970 рік

// Дисплей SSD1306
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr unsigned long DATA_SCREEN_MS = 5000;
constexpr unsigned long SETTINGS_SCREEN_MS = 10000;
constexpr unsigned long SETTINGS_SCREEN_AFTER_MQTT_MS = 5000;
constexpr unsigned long ERROR_SCREEN_MS = 10000;

// Кнопка
constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long LONG_PRESS_MS = 1500;
constexpr unsigned long DOUBLE_CLICK_MS = 400;
constexpr unsigned long WIFI_RESET_HOLD_MS = 3000;

// Налаштування за замовчуванням і допустимі межі
constexpr float DEFAULT_HEIGHT_CM = 18.0f;
constexpr unsigned long DEFAULT_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr bool DEFAULT_AUTO_ENABLED = true;
constexpr float HEIGHT_MIN_CM = 2.0f;
constexpr float HEIGHT_MAX_CM = 200.0f;
constexpr float INTERVAL_MIN_MIN = 0.1f;
constexpr float INTERVAL_MAX_MIN = 10080.0f;  // тиждень

// Логіка поїлки
constexpr float LOW_WATER_PERCENT = 20.0f;
constexpr float LOW_WATER_HYSTERESIS = 5.0f;  // нове сповіщення лише після підйому вище 25 %
constexpr unsigned long BOT_TIMEOUT_MS = 120000;
constexpr unsigned long BOT_ERROR_REPEAT_MS = 15000;
constexpr unsigned long STATUS_LOG_MS = 5000;

// Ультразвуковий датчик
constexpr unsigned long ULTRASONIC_TIMEOUT_US = 30000;
constexpr float ULTRASONIC_MIN_CM = 2.0f;
constexpr float ULTRASONIC_MAX_CM = 500.0f;

// Автодолив. Час роботи залежить від продуктивності насоса: уточнити на реальному залізі
constexpr bool DEFAULT_REFILL_AUTO = false;           // з міркувань безпеки вмикається командою /refillon
constexpr float REFILL_START_PERCENT = 20.0f;
constexpr float REFILL_TARGET_PERCENT = 90.0f;
constexpr unsigned long REFILL_MAX_RUN_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long REFILL_DRY_CHECK_MS = 10000;
constexpr float REFILL_MIN_RISE_PERCENT = 1.0f;       // за 10 с рівень має зрости хоча б на 1 %
constexpr unsigned long REFILL_COOLDOWN_MS = 10UL * 60UL * 1000UL;
constexpr unsigned long REFILL_POLL_MS = 500;          // як часто міряти рівень під час доливу
constexpr int FLOAT_ACTIVE_LEVEL = LOW;               // поплавок замикає на GND, коли вода вгорі

// Ваги (HX711)
constexpr float DEFAULT_CAPACITY_ML = 500.0f;
constexpr float CAPACITY_MIN_ML = 50.0f;
constexpr float CAPACITY_MAX_ML = 2000.0f;
constexpr float CAPACITY_STEP_ML = 50.0f;
constexpr uint8_t SCALE_SAMPLES = 5;                  // ~0,5 с при 10 вимірах/с
constexpr unsigned long SCALE_READY_TIMEOUT_MS = 1000;
constexpr float SCALE_REMOVED_G = 30.0f;              // нижче -30 г: пляшку зняли
#ifdef WOKWI_SIM
// HX711 у Wokwi (тип "5kg") видає 0..2100 на 0..5 кг, тобто 0,42 відліку на грам
constexpr float DEFAULT_SCALE_FACTOR = 0.42f;
#else
// Орієнтовне значення; реальні ваги обов'язково калібрувати командою /calibrate
constexpr float DEFAULT_SCALE_FACTOR = 420.0f;
#endif
}  // namespace Config
