// Налаштування користувача, що зберігаються в NVS (переживають перезавантаження).
#pragma once

struct DeviceSettings {
  float heightCm;            // висота пляшки (відстань від датчика до дна)
  unsigned long intervalMs;  // інтервал автоматичних замірів
  bool autoEnabled;          // чи ввімкнені автоматичні заміри
};

extern DeviceSettings settings;

void loadSettings();
void saveSettings();
