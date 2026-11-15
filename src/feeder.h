// Логіка поїлки: заміри, сповіщення, команди з Telegram, меню налаштувань.
#pragma once

#include "button.h"
#include "level_sensor.h"

namespace Feeder {

void begin(LevelSensor& sensor);
void loop(unsigned long now);  // автозаміри, сторож бота, подвійне натискання

void measure(bool isAuto);
void onButton(ButtonEvent event, unsigned long now);
void onMessage(const char* subtopic, const uint8_t* payload, unsigned int length);

}  // namespace Feeder
