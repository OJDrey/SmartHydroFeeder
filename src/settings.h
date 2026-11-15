// Налаштування користувача, що зберігаються в NVS (переживають перезавантаження).
#pragma once

struct DeviceSettings {
  float heightCm;            // висота пляшки (HC-SR04: відстань від датчика до дна)
  unsigned long intervalMs;  // інтервал автоматичних замірів
  bool autoEnabled;          // чи ввімкнені автоматичні заміри
  float capacityMl;          // об'єм пляшки (ваги)
  long scaleOffset;          // показ HX711 з порожньою пляшкою (тарування)
  float scaleFactor;         // відліків HX711 на грам (калібрування)
  bool refillAuto;           // автоматичний долив насосом
};

extern DeviceSettings settings;

void loadSettings();
void saveSettings();
