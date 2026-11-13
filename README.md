# SmartHydroFeeder

Розумна поїлка для дрібних домашніх тварин на базі ESP32. Пристрій вимірює рівень води в пляшці, показує його на OLED-дисплеї та надсилає сповіщення власнику через Telegram-бота.

## Склад

| Файл | Призначення |
|---|---|
| `src/main.cpp` | Прошивка ESP32: датчик HC-SR04, OLED SSD1306, кнопка, Wi-Fi (WiFiManager), MQTT |
| `platformio.ini` | Налаштування збірки PlatformIO: плата і версії бібліотек |
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

Проєкт збирається в [PlatformIO](https://platformio.org/). Відкрийте цю папку у VS Code з розширенням PlatformIO IDE і натисніть **Upload** (→) на нижній панелі. Бібліотеки завантажаться самі за списком у `platformio.ini`.

Те саме з терміналу:

```bash
pio run -t upload      # зібрати і прошити
pio device monitor     # Serial Monitor, 115200
```

Якщо прошивка зависає на `Connecting...`, затисніть на платі кнопку BOOT.

Після першого запуску пристрій створює точку доступу `SmartHydroFeeder`. Підключіться до неї телефоном, відкрийте `192.168.4.1` і виберіть свою мережу Wi-Fi. Щоб скинути Wi-Fi, утримуйте кнопку під час увімкнення 3 с.

## Telegram-бот

```bash
pip install pyTelegramBotAPI paho-mqtt python-dotenv
cp .env.example .env   # вписати токен від @BotFather і свій chat_id
python bot_v5.py
```

Команди: `/measure`, `/setheight 20`, `/autoon`, `/autooff`, `/interval 10`.

Файл `.env` містить секрети і в репозиторій не потрапляє.
