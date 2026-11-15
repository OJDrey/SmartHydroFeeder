#include "settings.h"

#include <Preferences.h>

#include "config.h"

DeviceSettings settings = {
    Config::DEFAULT_HEIGHT_CM,
    Config::DEFAULT_INTERVAL_MS,
    Config::DEFAULT_AUTO_ENABLED,
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
  prefs.end();
}

void saveSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat("height", settings.heightCm);
  prefs.putULong("interval", settings.intervalMs);
  prefs.putBool("auto", settings.autoEnabled);
  prefs.end();
}
