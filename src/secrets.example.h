// Шаблон секретів прошивки. Скопіюйте цей файл у src/secrets.h і впишіть свої значення.
// secrets.h у репозиторій не потрапляє (.gitignore).
//
// Без secrets.h прошивка підключається до відкритого broker.hivemq.com без TLS і пароля,
// як у курсовій: так проєкт збирається в будь-кого, хто його клонує.
#pragma once

// Адреса брокера EMQX Cloud Serverless (Overview → Connection Information → Address),
// без "mqtts://" і порту
#define SECRET_MQTT_HOST "xxxxxxxx.ala.eu-central-1.emqxsl.com"
#define SECRET_MQTT_PORT 8883

// Облікові дані пристрою (Access Control → Authentication)
#define SECRET_MQTT_USER "device123"
#define SECRET_MQTT_PASS "ПАРОЛЬ_ПРИСТРОЮ"
