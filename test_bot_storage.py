"""Юніт-тести зберігання і аналітики споживання. Запуск: py -m unittest test_bot_storage"""
import unittest
from datetime import datetime, timedelta

from bot_storage import NOISE_ML, Storage, compute_stats, consumption_events, format_stats

NOW = datetime(2027, 3, 10, 15, 30)


def ts(dt):
    return int(dt.timestamp())


class ConsumptionEventsTest(unittest.TestCase):
    def test_simple_drop(self):
        events = consumption_events([(0, 300), (600, 280)], NOISE_ML)
        self.assertEqual(events, [(600, 20)])

    def test_noise_is_ignored(self):
        events = consumption_events([(0, 300), (600, 301), (1200, 299), (1800, 300.5)], NOISE_ML)
        self.assertEqual(events, [])

    def test_slow_drinking_accumulates(self):
        # По 1 мл між замірами: кожен крок менший за шум (2 мл), але разом 300 -> 294 = 6 мл
        samples = [(i * 600, 300 - i) for i in range(7)]
        total = sum(a for _, a in consumption_events(samples, NOISE_ML))
        self.assertAlmostEqual(total, 6.0)

    def test_refill_is_not_consumption(self):
        samples = [(0, 100), (600, 80), (1200, 450), (1800, 430)]
        events = consumption_events(samples, NOISE_ML)
        self.assertEqual([a for _, a in events], [20, 20])

    def test_missing_values_skipped(self):
        events = consumption_events([(0, 300), (600, None), (1200, 290)], NOISE_ML)
        self.assertEqual(events, [(1200, 10)])


class ComputeStatsTest(unittest.TestCase):
    def test_week_by_days_in_ml(self):
        start = NOW - timedelta(days=2)
        rows = [
            (ts(start), 400.0, 80.0),
            (ts(start + timedelta(days=1)), 370.0, 74.0),
            (ts(NOW - timedelta(minutes=10)), 340.0, 68.0),
        ]
        stats = compute_stats(rows, "week", NOW)
        self.assertEqual(stats.unit, "мл")
        self.assertEqual(len(stats.buckets), 7)
        self.assertAlmostEqual(stats.total, 60.0)
        self.assertAlmostEqual(sum(v for _, v in stats.buckets), 60.0)
        self.assertIsNotNone(stats.avg_daily)
        self.assertAlmostEqual(stats.current, 340.0)

    def test_day_by_hours(self):
        rows = [(ts(NOW - timedelta(hours=3)), 300.0, 60.0), (ts(NOW - timedelta(hours=1)), 290.0, 58.0)]
        stats = compute_stats(rows, "day", NOW)
        self.assertEqual(len(stats.buckets), 24)
        self.assertAlmostEqual(stats.total, 10.0)
        self.assertIsNone(stats.avg_daily)  # даних менше ніж за добу

    def test_percent_fallback_for_ultrasonic(self):
        rows = [(ts(NOW - timedelta(hours=2)), None, 70.0), (ts(NOW - timedelta(hours=1)), None, 60.0)]
        stats = compute_stats(rows, "day", NOW)
        self.assertEqual(stats.unit, "%")
        self.assertAlmostEqual(stats.total, 10.0)

    def test_too_few_samples_message(self):
        stats = compute_stats([(ts(NOW), 300.0, 60.0)], "week", NOW)
        self.assertIn("замало даних", format_stats(stats))


class StorageTest(unittest.TestCase):
    def test_roundtrip(self):
        db = Storage(":memory:")
        db.add_measurement(100, "dev", "auto", volume_ml=250.0, level_percent=50.0)
        db.add_measurement(200, "dev", "manual", volume_ml=240.0, level_percent=48.0)
        db.add_measurement(150, "other", "auto", volume_ml=1.0, level_percent=1.0)
        db.add_event(200, "dev", "refill", "started")
        self.assertEqual(db.measurements("dev", 0), [(100, 250.0, 50.0), (200, 240.0, 48.0)])
        self.assertEqual(db.measurements("dev", 150), [(200, 240.0, 48.0)])
        db.close()


if __name__ == "__main__":
    unittest.main()
