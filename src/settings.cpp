#include "settings.h"

#include <Preferences.h>

#include "config.h"

DeviceSettings settings = {
    Config::DEFAULT_HEIGHT_CM,
    Config::DEFAULT_INTERVAL_MS,
    Config::DEFAULT_AUTO_ENABLED,
    Config::DEFAULT_CAPACITY_ML,
    0,
    Config::DEFAULT_SCALE_FACTOR,
};

namespace {
Preferences prefs;
constexpr const char* NVS_NAMESPACE = "feeder";
}  // namespace

void loadSettings() {
  prefs.begin(NVS_NAMESPACE, true);
  settings.heightCm = prefs.getFloat("height", Config::DEFAULT_HEIGHT_CM);
  settings.intervalMs = prefs.getULong("interval", Config::DEFAULT_INTERVAL_MS);
  settings.autoEnabled = prefs.getBool("auto", Config::DEFAULT_AUTO_ENABLED);
  settings.capacityMl = prefs.getFloat("capacity", Config::DEFAULT_CAPACITY_ML);
  settings.scaleOffset = prefs.getLong("s_offset", 0);
  settings.scaleFactor = prefs.getFloat("s_factor", Config::DEFAULT_SCALE_FACTOR);
  prefs.end();
}

void saveSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat("height", settings.heightCm);
  prefs.putULong("interval", settings.intervalMs);
  prefs.putBool("auto", settings.autoEnabled);
  prefs.putFloat("capacity", settings.capacityMl);
  prefs.putLong("s_offset", settings.scaleOffset);
  prefs.putFloat("s_factor", settings.scaleFactor);
  prefs.end();
}
