# SmartHydroFeeder

Розумна поїлка для дрібних домашніх тварин на базі ESP32. Пристрій вимірює рівень води в пляшці, показує його на OLED-дисплеї та надсилає сповіщення власнику через Telegram-бота.

## Склад

| Файл | Призначення |
|---|---|
| `SmartHydroFeeder.ino` | Прошивка ESP32: датчик HC-SR04, OLED SSD1306, кнопка, Wi-Fi (WiFiManager), MQTT |
| `bot_v5.py` | Telegram-бот на Python: пересилає дані з MQTT у Telegram і приймає команди |
| `.env.example` | Шаблон налаштувань бота |

Обмін даними: ESP32 ⇄ MQTT-брокер ⇄ Python-бот ⇄ Telegram.

## Підключення (ESP32)

| Пристрій | Пін |
|---|---|
| HC-SR04 TRIG | GPIO32 |
| HC-SR04 ECHO | GPIO35 (через дільник напруги 1 кОм / 2 кОм) |
| Кнопка | GPIO25 (на GND) |
| OLED SDA / SCL | GPIO26 / GPIO27 |
| Світлодіод | GPIO2 (вбудований) |

## Прошивка

Бібліотеки Arduino: Adafruit SSD1306, Adafruit GFX, WiFiManager (tzapu), PubSubClient, ArduinoJson. Плата: `ESP32 Dev Module`.

Після першого запуску пристрій створює точку доступу `SmartHydroFeeder`. Підключіться до неї телефоном, відкрийте `192.168.4.1` і виберіть свою мережу Wi-Fi. Щоб скинути Wi-Fi, утримуйте кнопку під час увімкнення 3 с.

## Telegram-бот

```bash
pip install pyTelegramBotAPI paho-mqtt python-dotenv
cp .env.example .env   # вписати токен від @BotFather і свій chat_id
python bot_v5.py
```

Команди: `/measure`, `/setheight 20`, `/autoon`, `/autooff`, `/interval 10`.

Файл `.env` містить секрети і в репозиторій не потрапляє.
