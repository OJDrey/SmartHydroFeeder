#include "refill_controller.h"

RefillController::RefillController(const RefillConfig& config) : config_(config) {}

bool RefillController::cooldownOver(unsigned long now) const {
  return !finishedOnce_ || now - lastFinish_ >= config_.cooldownMs;
}

RefillEvent RefillController::start(unsigned long now, const LevelSample& sample) {
  state_ = RefillState::Filling;
  startedAt_ = now;
  windowStart_ = now;
  windowPercent_ = sample.percent;
  return RefillEvent::Started;
}

RefillEvent RefillController::finish(unsigned long now, RefillEvent reason) {
  state_ = RefillState::Idle;
  lastFinish_ = now;
  finishedOnce_ = true;
  return reason;
}

RefillEvent RefillController::fail(unsigned long now, RefillEvent reason) {
  state_ = RefillState::Error;
  lastFinish_ = now;
  finishedOnce_ = true;
  return reason;
}

RefillEvent RefillController::update(unsigned long now, const LevelSample& sample, bool autoEnabled) {
  switch (state_) {
    case RefillState::Idle:
      if (!autoEnabled || !sample.ok || sample.percent >= config_.startPercent) return RefillEvent::None;
      if (!cooldownOver(now)) return RefillEvent::None;
      if (sample.floatHigh) {
        // Датчик рівня каже "мало", а поплавок "повно": не ризикуємо, чекаємо паузу
        lastFinish_ = now;
        finishedOnce_ = true;
        return RefillEvent::AlreadyFull;
      }
      return start(now, sample);

    case RefillState::Filling:
      // Поплавок перевіряється першим: він зупиняє насос навіть при збої датчика рівня
      if (sample.floatHigh) return finish(now, RefillEvent::FloatStop);
      if (!sample.ok) return fail(now, RefillEvent::SensorError);
      if (sample.percent >= config_.targetPercent) return finish(now, RefillEvent::Done);
      if (now - startedAt_ >= config_.maxRunMs) return fail(now, RefillEvent::Timeout);
      if (now - windowStart_ >= config_.dryCheckMs) {
        if (sample.percent - windowPercent_ < config_.minRisePercent) return fail(now, RefillEvent::DryRun);
        windowStart_ = now;
        windowPercent_ = sample.percent;
      }
      return RefillEvent::None;

    case RefillState::Error:
      return RefillEvent::None;  // автодолив заблоковано до ручного /refill
  }
  return RefillEvent::None;
}

RefillEvent RefillController::requestStart(unsigned long now, const LevelSample& sample) {
  if (state_ == RefillState::Filling) return RefillEvent::None;
  if (!sample.ok) return fail(now, RefillEvent::SensorError);
  if (sample.floatHigh || sample.percent >= config_.targetPercent) {
    state_ = RefillState::Idle;  // ручний запит знімає блокування після помилки
    return RefillEvent::AlreadyFull;
  }
  return start(now, sample);
}

RefillEvent RefillController::stop(unsigned long now) {
  if (state_ != RefillState::Filling) return RefillEvent::None;
  return finish(now, RefillEvent::Stopped);
}
