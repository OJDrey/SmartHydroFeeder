"""Зберігання вимірювань у SQLite і розрахунок споживання води.

Модуль не залежить від Telegram і MQTT, тому аналітика перевіряється юніт-тестами:
    py -m unittest test_bot_storage
"""
import io
import sqlite3
import threading
from dataclasses import dataclass, field
from datetime import datetime, timedelta

SCHEMA = """
CREATE TABLE IF NOT EXISTS measurements (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    ts            INTEGER NOT NULL,   -- Unix-час, секунди
    device_id     TEXT    NOT NULL,
    source        TEXT,               -- manual / auto
    volume_ml     REAL,               -- ваги (HX711)
    level_percent REAL,
    dist_cm       REAL                -- ультразвуковий датчик
);
CREATE INDEX IF NOT EXISTS idx_measurements ON measurements(device_id, ts);

CREATE TABLE IF NOT EXISTS events (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    ts        INTEGER NOT NULL,
    device_id TEXT    NOT NULL,
    kind      TEXT    NOT NULL,       -- alert / refill / error
    detail    TEXT
);
CREATE INDEX IF NOT EXISTS idx_events ON events(device_id, ts);
"""

# Коливання показів, менші за поріг, не вважаються ні споживанням, ні доливом
NOISE_ML = 2.0
NOISE_PERCENT = 0.5


class Storage:
    """Обгортка над SQLite. Безпечна для виклику з потоку MQTT і потоку Telegram одночасно."""

    def __init__(self, path):
        self._conn = sqlite3.connect(path, check_same_thread=False)
        self._lock = threading.Lock()
        with self._lock:
            self._conn.executescript(SCHEMA)

    def add_measurement(self, ts, device_id, source=None, volume_ml=None, level_percent=None, dist_cm=None):
        with self._lock, self._conn:
            self._conn.execute(
                "INSERT INTO measurements (ts, device_id, source, volume_ml, level_percent, dist_cm)"
                " VALUES (?, ?, ?, ?, ?, ?)",
                (int(ts), device_id, source, volume_ml, level_percent, dist_cm),
            )

    def add_event(self, ts, device_id, kind, detail=None):
        with self._lock, self._conn:
            self._conn.execute(
                "INSERT INTO events (ts, device_id, kind, detail) VALUES (?, ?, ?, ?)",
                (int(ts), device_id, kind, detail),
            )

    def measurements(self, device_id, since_ts):
        """Список (ts, volume_ml, level_percent) від since_ts, за зростанням часу."""
        with self._lock:
            rows = self._conn.execute(
                "SELECT ts, volume_ml, level_percent FROM measurements"
                " WHERE device_id = ? AND ts >= ? ORDER BY ts",
                (device_id, int(since_ts)),
            ).fetchall()
        return rows

    def close(self):
        with self._lock:
            self._conn.close()


# ---------- Аналітика ----------

def consumption_events(samples, noise):
    """Розбиває ряд вимірів [(ts, value), ...] на епізоди споживання [(ts, amount), ...].

    Значення порівнюється з опорним, а не з попереднім виміром. Тому повільне
    зменшення (тварина п'є потроху між замірами) накопичується і враховується,
    а дрібний шум у межах noise — ні. Зростання рівня (долив) лише переносить опору.
    """
    events = []
    reference = None
    for ts, value in samples:
        if value is None:
            continue
        if reference is None:
            reference = value
        elif value < reference - noise:
            events.append((ts, reference - value))
            reference = value
        elif value > reference + noise:
            reference = value  # долив
    return events


@dataclass
class Stats:
    unit: str                     # "мл" або "%"
    period: str                   # "day" або "week"
    total: float                  # спожито за період
    avg_daily: float | None       # середнє за добу; None, якщо даних менше ніж за добу
    span_hours: float             # скільки годин покривають дані
    samples: int
    current: float | None         # останній показ
    buckets: list = field(default_factory=list)  # [(datetime початку інтервалу, спожито)]


def _bucket_start(dt, period):
    if period == "day":
        return dt.replace(minute=0, second=0, microsecond=0)
    return dt.replace(hour=0, minute=0, second=0, microsecond=0)


def compute_stats(rows, period, now):
    """rows — результат Storage.measurements(); period — "day" (за годинами) або "week" (за днями).

    now — поточний момент (datetime, місцевий час), задає межі графіка.
    """
    use_volume = any(r[1] is not None for r in rows)
    unit = "мл" if use_volume else "%"
    noise = NOISE_ML if use_volume else NOISE_PERCENT
    samples = [(ts, vol if use_volume else pct) for ts, vol, pct in rows]
    samples = [(ts, v) for ts, v in samples if v is not None]

    if period == "day":
        step, count = timedelta(hours=1), 24
    else:
        step, count = timedelta(days=1), 7
    last = _bucket_start(now, period)
    starts = [last - step * i for i in range(count - 1, -1, -1)]
    totals = {s: 0.0 for s in starts}

    events = consumption_events(samples, noise)
    for ts, amount in events:
        key = _bucket_start(datetime.fromtimestamp(ts), period)
        if key in totals:
            totals[key] += amount

    total = sum(amount for _, amount in events)
    span_hours = (samples[-1][0] - samples[0][0]) / 3600 if len(samples) > 1 else 0.0
    avg_daily = total / (span_hours / 24) if span_hours >= 24 else None

    return Stats(
        unit=unit,
        period=period,
        total=total,
        avg_daily=avg_daily,
        span_hours=span_hours,
        samples=len(samples),
        current=samples[-1][1] if samples else None,
        buckets=[(s, totals[s]) for s in starts],
    )


def format_stats(stats):
    period = "добу" if stats.period == "day" else "тиждень"
    if stats.samples < 2:
        return f"📊 За {period} замало даних для аналізу ({stats.samples} вим.)."
    lines = [f"📊 Споживання води за {period}"]
    lines.append(f"Спожито: {stats.total:.0f} {stats.unit}")
    if stats.avg_daily is not None:
        lines.append(f"Середнє за добу: {stats.avg_daily:.0f} {stats.unit}")
    else:
        lines.append(f"Середнє за добу: даних лише за {stats.span_hours:.1f} год")
    if stats.current is not None:
        lines.append(f"Зараз у пляшці: {stats.current:.0f} {stats.unit}")
    lines.append(f"Вимірів: {stats.samples}")
    return "\n".join(lines)


def render_chart(stats):
    """PNG-графік споживання (bytes) або None, якщо matplotlib не встановлено."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return None

    labels = [s.strftime("%H") if stats.period == "day" else s.strftime("%d.%m") for s, _ in stats.buckets]
    values = [v for _, v in stats.buckets]

    fig, ax = plt.subplots(figsize=(8, 4), dpi=120)
    # Стовпці за номерами позицій, а підписи окремо: інакше matplotlib пише в лог
    # попередження, що рядки "09", "10" схожі на числа
    positions = list(range(len(values)))
    ax.bar(positions, values, color="#2b7bba")
    step = 2 if stats.period == "day" else 1
    ax.set_xticks(positions[::step], labels[::step])
    ax.set_ylabel(f"Спожито, {stats.unit}")
    ax.set_xlabel("Година" if stats.period == "day" else "День")
    ax.set_title("Споживання води за добу" if stats.period == "day" else "Споживання води за тиждень")
    if stats.avg_daily is not None and stats.period == "week":
        ax.axhline(stats.avg_daily, color="#d9534f", linestyle="--", label=f"Середнє {stats.avg_daily:.0f} {stats.unit}")
        ax.legend()
    ax.grid(axis="y", alpha=0.3)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()

    buf = io.BytesIO()
    fig.savefig(buf, format="png")
    plt.close(fig)
    return buf.getvalue()
