// Скінченний автомат автодоливу води.
//
// Чиста логіка без Arduino: на вході час і показ датчиків, на виході стан насоса і подія.
// Завдяки цьому автомат перевіряється юніт-тестами на ПК (pio test -e test).
//
//            рівень < startPercent (авто) або команда /refill
//   ┌──────┐ ───────────────────────────────────────────────► ┌─────────┐
//   │ Idle │                                                  │ Filling │ насос увімкнено
//   └──────┘ ◄─────────────────────────────────────────────── └─────────┘
//      ▲      досягнуто targetPercent / поплавок / /stop           │
//      │                                                           │ тайм-аут, "насухо",
//      │  команда /refill                 ┌───────┐                │ помилка датчика
//      └───────────────────────────────── │ Error │ ◄──────────────┘
//                                         └───────┘ автодолив заблоковано
#pragma once

enum class RefillState { Idle, Filling, Error };

enum class RefillEvent {
  None,
  Started,      // насос увімкнено
  Done,         // досягнуто цільового рівня
  FloatStop,    // спрацював поплавок (аварійна зупинка, але не помилка)
  Stopped,      // зупинено вручну
  AlreadyFull,  // долив не потрібен
  DryRun,       // насос працює, а рівень не росте: резервуар порожній
  Timeout,      // насос працює надто довго
  SensorError,  // датчик рівня не відповідає
};

struct RefillConfig {
  float startPercent;        // автодолив, коли рівень нижче
  float targetPercent;       // зупинка, коли рівень досяг
  unsigned long maxRunMs;    // найдовша робота насоса за один долив
  unsigned long dryCheckMs;  // вікно перевірки "насухо"
  float minRisePercent;      // мінімальний приріст рівня за це вікно
  unsigned long cooldownMs;  // пауза між автоматичними доливами
};

struct LevelSample {
  bool ok;         // чи вдалося виміряти рівень
  float percent;   // рівень, 0..100 %
  bool floatHigh;  // поплавок: вода дійшла до верху
};

class RefillController {
 public:
  explicit RefillController(const RefillConfig& config);

  // Викликати після кожного виміру рівня (під час доливу — часто, напр. раз на 0,5 с)
  RefillEvent update(unsigned long now, const LevelSample& sample, bool autoEnabled);

  // Ручний запуск (знімає блокування після помилки)
  RefillEvent requestStart(unsigned long now, const LevelSample& sample);

  RefillEvent stop(unsigned long now);

  RefillState state() const { return state_; }
  bool pumpOn() const { return state_ == RefillState::Filling; }

 private:
  RefillEvent start(unsigned long now, const LevelSample& sample);
  RefillEvent finish(unsigned long now, RefillEvent reason);
  RefillEvent fail(unsigned long now, RefillEvent reason);
  bool cooldownOver(unsigned long now) const;

  RefillConfig config_;
  RefillState state_ = RefillState::Idle;
  unsigned long startedAt_ = 0;
  unsigned long windowStart_ = 0;
  float windowPercent_ = 0.0f;
  unsigned long lastFinish_ = 0;
  bool finishedOnce_ = false;
};
