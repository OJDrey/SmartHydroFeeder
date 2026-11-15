// Екрани OLED SSD1306 і час їх показу.
#pragma once

#include "config.h"
#include "level_sensor.h"

namespace Screen {

enum class Mode { Main, Settings, Data };

bool begin();
Mode mode();

void showMain();
void showSettings(const SensorParam& param, unsigned long timeoutMs = Config::SETTINGS_SCREEN_MS);
void showReading(const LevelReading& reading);
void showMessage(const char* title, const char* line);  // коротке повідомлення на 5 с
void showRefill(float percent);                         // екран під час доливу
void showLowWater(float percent);
void showSensorError(const char* sensorName, const char* reason);
void showError(const char* message);  // тимчасове повідомлення поверх будь-якого екрана
void dismissError();

void showPortalInfo();
void showWifiResetPrompt();
void showWifiResetDone();

// Повернення на головний екран за тайм-аутом і оновлення рядка стану зв'язку
void update(unsigned long now);

}  // namespace Screen
