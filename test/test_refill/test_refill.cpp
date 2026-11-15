// Юніт-тести автомата доливу. Запуск на ПК: pio test -e test
#include <unity.h>

#include "refill_controller.h"

// enum class не перетворюється на int сам, тому порівнюємо через приведення
#define ASSERT_EQ(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))

static const RefillConfig CONFIG = {
    20.0f,   // startPercent
    90.0f,   // targetPercent
    60000,   // maxRunMs
    10000,   // dryCheckMs
    1.0f,    // minRisePercent
    600000,  // cooldownMs
};

static LevelSample level(float percent, bool floatHigh = false) { return {true, percent, floatHigh}; }
static const LevelSample SENSOR_FAIL = {false, 0.0f, false};

void setUp() {}
void tearDown() {}

// ---------- Автоматичний запуск ----------

void test_auto_starts_when_level_low() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::Started, c.update(0, level(15), true));
  TEST_ASSERT_TRUE(c.pumpOn());
}

void test_auto_ignores_normal_level() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::None, c.update(0, level(50), true));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_auto_disabled_does_not_start() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::None, c.update(0, level(5), false));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_auto_does_not_start_when_float_high() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::AlreadyFull, c.update(0, level(5, true), true));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_auto_respects_cooldown() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  c.update(5000, level(95), true);  // Done
  ASSERT_EQ(RefillEvent::None, c.update(10000, level(15), true));
  ASSERT_EQ(RefillEvent::Started, c.update(5000 + 600000, level(15), true));
}

// ---------- Зупинка ----------

void test_stops_at_target() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  ASSERT_EQ(RefillEvent::None, c.update(5000, level(60), true));
  ASSERT_EQ(RefillEvent::Done, c.update(9000, level(90), true));
  TEST_ASSERT_FALSE(c.pumpOn());
  ASSERT_EQ(RefillState::Idle, c.state());
}

void test_float_stops_pump_even_if_sensor_failed() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  LevelSample brokenButFull = {false, 0.0f, true};
  ASSERT_EQ(RefillEvent::FloatStop, c.update(1000, brokenButFull, true));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_manual_stop() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  ASSERT_EQ(RefillEvent::Stopped, c.stop(1000));
  TEST_ASSERT_FALSE(c.pumpOn());
  ASSERT_EQ(RefillEvent::None, c.stop(2000));
}

// ---------- Захисти ----------

void test_dry_run_detected() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  ASSERT_EQ(RefillEvent::None, c.update(5000, level(15.5f), true));
  ASSERT_EQ(RefillEvent::DryRun, c.update(10000, level(15.5f), true));
  TEST_ASSERT_FALSE(c.pumpOn());
  ASSERT_EQ(RefillState::Error, c.state());
}

void test_rising_level_is_not_dry_run() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  ASSERT_EQ(RefillEvent::None, c.update(10000, level(20), true));
  ASSERT_EQ(RefillEvent::None, c.update(20000, level(25), true));
  TEST_ASSERT_TRUE(c.pumpOn());
}

void test_timeout() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  float p = 15;
  unsigned long t = 0;
  for (t = 10000; t < 60000; t += 10000) {
    p += 5;  // рівень росте, але повільно
    ASSERT_EQ(RefillEvent::None, c.update(t, level(p), true));
  }
  ASSERT_EQ(RefillEvent::Timeout, c.update(60000, level(p + 5), true));
  ASSERT_EQ(RefillState::Error, c.state());
}

void test_sensor_error_while_filling() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  ASSERT_EQ(RefillEvent::SensorError, c.update(500, SENSOR_FAIL, true));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_error_blocks_auto_refill() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  c.update(10000, level(15), true);  // DryRun
  ASSERT_EQ(RefillEvent::None, c.update(10000 + 600000, level(5), true));
  TEST_ASSERT_FALSE(c.pumpOn());
}

// ---------- Ручний запуск ----------

void test_manual_start_clears_error() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  c.update(10000, level(15), true);  // DryRun
  ASSERT_EQ(RefillEvent::Started, c.requestStart(20000, level(15)));
  TEST_ASSERT_TRUE(c.pumpOn());
}

void test_manual_start_ignores_cooldown() {
  RefillController c(CONFIG);
  c.update(0, level(15), true);
  c.update(5000, level(95), true);  // Done
  ASSERT_EQ(RefillEvent::Started, c.requestStart(6000, level(50)));
}

void test_manual_start_when_full() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::AlreadyFull, c.requestStart(0, level(95)));
  ASSERT_EQ(RefillEvent::AlreadyFull, c.requestStart(0, level(50, true)));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_manual_start_with_broken_sensor() {
  RefillController c(CONFIG);
  ASSERT_EQ(RefillEvent::SensorError, c.requestStart(0, SENSOR_FAIL));
  TEST_ASSERT_FALSE(c.pumpOn());
}

void test_millis_overflow() {
  RefillController c(CONFIG);
  unsigned long nearMax = 0xFFFFFFFFUL - 3000;
  c.update(nearMax, level(15), true);
  // Через 7 с після переповнення лічильника (разом 10 с) має спрацювати перевірка "насухо"
  ASSERT_EQ(RefillEvent::DryRun, c.update(nearMax + 10000UL, level(15), true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_auto_starts_when_level_low);
  RUN_TEST(test_auto_ignores_normal_level);
  RUN_TEST(test_auto_disabled_does_not_start);
  RUN_TEST(test_auto_does_not_start_when_float_high);
  RUN_TEST(test_auto_respects_cooldown);
  RUN_TEST(test_stops_at_target);
  RUN_TEST(test_float_stops_pump_even_if_sensor_failed);
  RUN_TEST(test_manual_stop);
  RUN_TEST(test_dry_run_detected);
  RUN_TEST(test_rising_level_is_not_dry_run);
  RUN_TEST(test_timeout);
  RUN_TEST(test_sensor_error_while_filling);
  RUN_TEST(test_error_blocks_auto_refill);
  RUN_TEST(test_manual_start_clears_error);
  RUN_TEST(test_manual_start_ignores_cooldown);
  RUN_TEST(test_manual_start_when_full);
  RUN_TEST(test_manual_start_with_broken_sensor);
  RUN_TEST(test_millis_overflow);
  return UNITY_END();
}
