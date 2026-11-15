"""Telegram-бот SmartHydroFeeder.

Пересилає повідомлення пристрою з MQTT у Telegram, приймає команди власника,
зберігає всі виміри в SQLite і будує статистику споживання води (/stats).
"""
import json
import logging
import os
import sys
import threading
import time
from datetime import datetime
from functools import wraps

import paho.mqtt.client as mqtt
import telebot

from bot_storage import Storage, compute_stats, format_stats, render_chart

# Секрети читаються з файлу .env поруч зі скриптом (або зі змінних середовища).
# Шаблон: .env.example. Сам .env не можна нікуди викладати.
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(BASE_DIR, ".env"))
except ImportError:
    pass  # python-dotenv не встановлено: беремо лише змінні середовища

TOKEN = os.environ.get("TELEGRAM_TOKEN")
CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID")
DEVICE_ID = os.environ.get("DEVICE_ID", "device123")
MQTT_SERVER = os.environ.get("MQTT_SERVER", "broker.hivemq.com")
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")
# Хмарний брокер з логіном працює через TLS (8883); відкритий — без шифрування (1883)
MQTT_TLS = os.environ.get("MQTT_TLS", "1" if MQTT_USERNAME else "0") == "1"
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883" if MQTT_TLS else "1883"))
DB_PATH = os.environ.get("DB_PATH", os.path.join(BASE_DIR, "smarthydrofeeder.db"))

if not TOKEN or not CHAT_ID:
    raise SystemExit("Не задано TELEGRAM_TOKEN або TELEGRAM_CHAT_ID (див. .env.example)")

# Керувати пристроєм можуть лише власник (TELEGRAM_CHAT_ID) і чати з ALLOWED_CHAT_IDS (через кому)
ALLOWED_CHATS = {CHAT_ID.strip()} | {
    c.strip() for c in os.environ.get("ALLOWED_CHAT_IDS", "").split(",") if c.strip()
}

TOPIC = f"smartfeeder/{DEVICE_ID}"

# Консоль Windows за замовчуванням у cp1251 і не може вивести емодзі з повідомлень
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("bot")

bot = telebot.TeleBot(TOKEN)
storage = Storage(DB_PATH)

# paho-mqtt 2.x вимагає явно вказати версію API колбеків, 1.x такого параметра не має
try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:
    client = mqtt.Client()


# ---------- Допоміжне ----------

def send(text):
    """Повідомлення власнику. Помилка мережі Telegram не повинна зупиняти потік MQTT."""
    try:
        bot.send_message(CHAT_ID, text)
    except Exception as e:  # telebot кидає різні винятки мережі та API
        log.warning("Не вдалося надіслати повідомлення: %s", e)


def publish_cmd(payload):
    client.publish(f"{TOPIC}/cmd", json.dumps(payload))


def event_time(j):
    """Час події: від пристрою (NTP), а якщо пристрій ще не синхронізував годинник — час отримання."""
    ts = j.get("timestamp")
    return int(ts) if isinstance(ts, (int, float)) and ts > 1_700_000_000 else int(time.time())


def fmt_time(ts):
    return datetime.fromtimestamp(ts).strftime("%d.%m.%Y %H:%M")


def owner_only(handler):
    """Команди виконуються лише для дозволених чатів."""
    @wraps(handler)
    def wrapper(message):
        if str(message.chat.id) not in ALLOWED_CHATS:
            log.warning("Відхилено команду з чату %s: %s", message.chat.id, message.text)
            bot.reply_to(message, "⛔ Доступ заборонено")
            return
        handler(message)
    return wrapper


def command_arg(message, cast=float):
    """Перший аргумент команди або None, якщо його немає чи він некоректний."""
    parts = message.text.split()
    if len(parts) < 2:
        return None
    try:
        return cast(parts[1].replace(",", "."))
    except ValueError:
        return None


# ---------- Повідомлення від пристрою ----------

def format_measure_message(j):
    lines = [f"📡 Пристрій: {j.get('device_id', '?')}"]
    if j.get("dist_cm") is not None:
        lines.append(f"📏 Відстань від датчика до води: {j['dist_cm']:.1f} см")
    if j.get("height_cm") is not None:
        lines.append(f"📐 Висота пляшки: {j['height_cm']:.1f} см")
    if j.get("volume_ml") is not None:
        lines.append(f"🥤 Води в пляшці: {j['volume_ml']:.0f} мл")
    if j.get("capacity_ml") is not None:
        lines.append(f"📐 Об'єм пляшки: {j['capacity_ml']:.0f} мл")
    if j.get("level_percent") is not None:
        lines.append(f"💧 Рівень заповнення: {j['level_percent']:.1f}%")
    if j.get("low_water"):
        lines.append("⚠️ Низький рівень води!")
    lines.append(f"⏱ {fmt_time(event_time(j))}")
    return "\n".join(lines)


def format_setting_message(j):
    # Підтвердження від пристрою: що саме змінилося
    if j.get("tare") == "ok":
        return "⚖️ Тарування виконано: порожня пляшка = 0 мл"
    if "calibrated_g" in j:
        return f"⚖️ Ваги відкалібровано вантажем {j['calibrated_g']:.0f} г (коефіцієнт {j.get('scale_factor', 0):.4f})"
    if "set_height" in j:
        return f"⚙️ Висоту пляшки змінено: {j['set_height']:.0f} см"
    if "set_capacity_ml" in j:
        return f"⚙️ Об'єм пляшки змінено: {j['set_capacity_ml']:.0f} мл"
    return None


REFILL_MESSAGES = {
    "started": "💧 Долив розпочато",
    "done": "✅ Долив завершено: пляшка повна",
    "float_stop": "✅ Долив зупинено поплавком",
    "stopped": "⏹ Долив зупинено вручну",
    "already_full": "ℹ️ Долив не потрібен: пляшка повна",
    "dry_run": "🚨 Насос працює, а вода не прибуває. Перевірте резервуар! Автодолив заблоковано до /refill",
    "timeout": "🚨 Насос працював надто довго і зупинений. Автодолив заблоковано до /refill",
    "sensor_error": "🚨 Долив зупинено: помилка датчика рівня. Автодолив заблоковано до /refill",
}


SUBTOPICS = ("measure", "alert", "error", "setting", "refill")


def on_connect(client, userdata, flags, rc, properties=None):  # сигнатура підходить і для paho 1.x, і для 2.x
    failed = rc.is_failure if hasattr(rc, "is_failure") else rc != 0  # paho 2.x: ReasonCode, 1.x: int
    if failed:
        # Найчастіше: неправильний логін/пароль або облікові дані без дозволу на топіки
        log.error("MQTT відмовив у підключенні: %s", rc)
        return
    log.info("MQTT connected: %s", rc)
    # Старі версії прошивки і бота публікували retained-повідомлення, і брокер досі віддає їх
    # кожному новому підписнику. Порожнє retained-повідомлення видаляє збережену копію.
    for sub in SUBTOPICS + ("bot",):
        client.publish(f"{TOPIC}/{sub}", b"", retain=True)
    for sub in SUBTOPICS:
        client.subscribe(f"{TOPIC}/{sub}")


def on_message(client, userdata, msg):
    # Retained-повідомлення — це збережена брокером стара копія, а не нова подія
    if msg.retain or not msg.payload:
        return
    try:
        payload = msg.payload.decode("utf-8")
        j = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError):
        log.warning("Некоректне повідомлення в %s: %r", msg.topic, msg.payload[:200])
        return
    if not isinstance(j, dict):
        return
    log.info("MQTT %s %s", msg.topic, payload)

    kind = msg.topic.rsplit("/", 1)[-1]
    device = j.get("device_id", DEVICE_ID)
    ts = event_time(j)

    try:
        if kind == "measure":
            storage.add_measurement(ts, device, j.get("source"), j.get("volume_ml"),
                                    j.get("level_percent"), j.get("dist_cm"))
            # Автоматичні заміри лише зберігаються, щоб не засипати чат повідомленнями
            if j.get("source") != "auto":
                send(format_measure_message(j))

        elif kind == "alert":
            storage.add_event(ts, device, "alert", j.get("triggered_by"))
            title = "🚨 Автоматичний замір: низький рівень води!" if j.get("triggered_by") == "auto" else "🚨 Тривога"
            send(title + "\n" + format_measure_message(j))

        elif kind == "refill":
            event = j.get("event", "?")
            storage.add_event(ts, device, "refill", event)
            text = REFILL_MESSAGES.get(event, f"Долив: {event}")
            if j.get("level_percent") is not None:
                text += f"\n💧 Рівень: {j['level_percent']:.1f}%"
            send(text)

        elif kind == "setting":
            text = format_setting_message(j)
            if text:
                send(text)

        elif kind == "error":
            err = j.get("error", "Unknown error")
            storage.add_event(ts, device, "error", err)
            send(f"❗️ Помилка пристрою {device}:\n{err}\n⏱ {fmt_time(ts)}")
    except Exception:
        # Збій обробки одного повідомлення не повинен зупиняти отримання наступних
        log.exception("Помилка обробки повідомлення %s", msg.topic)


client.on_connect = on_connect
client.on_message = on_message
if MQTT_USERNAME:
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
if MQTT_TLS:
    # Сертифікат брокера перевіряється за системним сховищем кореневих сертифікатів
    client.tls_set()
log.info("MQTT %s:%d (%s)", MQTT_SERVER, MQTT_PORT, "TLS" if MQTT_TLS else "без шифрування")
client.connect(MQTT_SERVER, MQTT_PORT, 60)
client.loop_start()


def heartbeat_loop():
    while True:
        payload = json.dumps({"bot": "online", "timestamp": int(time.time())})
        # Не retained: інакше пристрій отримував би "бот онлайн" від брокера навіть коли бот вимкнений
        result = client.publish(f"{TOPIC}/bot", payload)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            log.warning("Heartbeat не надіслано, rc=%s", result.rc)
        time.sleep(10)


threading.Thread(target=heartbeat_loop, daemon=True).start()


# ---------- Команди ----------

HELP = """Команди SmartHydroFeeder:
/measure — виміряти рівень зараз
/stats — споживання за тиждень, /stats day — за добу
/refill — долити зараз, /stop — зупинити насос
/refillon, /refilloff — автодолив
/autoon, /autooff — автоматичні заміри
/interval 10 — інтервал замірів, хв
/capacity 500 — об'єм пляшки, мл (ваги)
/setheight 20 — висота пляшки, см (ультразвук)
/tare, /calibrate 200 — налаштування ваг"""


@bot.message_handler(commands=["start", "help"])
@owner_only
def help_cmd(message):
    bot.reply_to(message, HELP)


@bot.message_handler(commands=["measure"])
@owner_only
def measure(message):
    publish_cmd({"measure": True})
    bot.reply_to(message, "Запит на вимірювання надіслано")


@bot.message_handler(commands=["stats"])
@owner_only
def stats_cmd(message):
    period = "day" if "day" in message.text.lower() or "доб" in message.text.lower() else "week"
    seconds = 86400 if period == "day" else 7 * 86400
    rows = storage.measurements(DEVICE_ID, time.time() - seconds)
    stats = compute_stats(rows, period, datetime.now())
    text = format_stats(stats)
    chart = render_chart(stats) if stats.samples >= 2 else None
    if chart:
        bot.send_photo(message.chat.id, chart, caption=text)
    else:
        bot.reply_to(message, text)


@bot.message_handler(commands=["setheight"])
@owner_only
def set_height(message):
    h = command_arg(message)
    if h is None:
        bot.reply_to(message, "Використання: /setheight 20.0")
        return
    publish_cmd({"set_height": h})
    bot.reply_to(message, f"Запит надіслано: висота {h} см")


@bot.message_handler(commands=["autoon"])
@owner_only
def auto_on(message):
    publish_cmd({"auto_enabled": True})
    bot.reply_to(message, "Автоматичні вимірювання УВІМКНЕНО")


@bot.message_handler(commands=["autooff"])
@owner_only
def auto_off(message):
    publish_cmd({"auto_enabled": False})
    bot.reply_to(message, "Автоматичні вимірювання ВИМКНЕНО")


@bot.message_handler(commands=["interval"])
@owner_only
def interval_cmd(message):
    m = command_arg(message)
    if m is None:
        bot.reply_to(message, "Використання: /interval 10  (в хвилинах)")
        return
    publish_cmd({"set_interval_min": m})
    bot.reply_to(message, f"Запит надіслано: інтервал {m} хв")


@bot.message_handler(commands=["capacity"])
@owner_only
def capacity_cmd(message):
    ml = command_arg(message)
    if ml is None:
        bot.reply_to(message, "Використання: /capacity 500  (в мілілітрах)")
        return
    publish_cmd({"set_capacity_ml": ml})
    bot.reply_to(message, f"Запит надіслано: об'єм пляшки {ml:.0f} мл")


@bot.message_handler(commands=["tare"])
@owner_only
def tare_cmd(message):
    publish_cmd({"tare": True})
    bot.reply_to(message, "Запит на тарування надіслано. На вагах має стояти ПОРОЖНЯ пляшка")


@bot.message_handler(commands=["calibrate"])
@owner_only
def calibrate_cmd(message):
    g = command_arg(message)
    if g is None:
        bot.reply_to(message, "Використання: /calibrate 200  (вага вантажу в грамах; спершу /tare)")
        return
    publish_cmd({"calibrate_g": g})
    bot.reply_to(message, f"Запит на калібрування надіслано: вантаж {g:.0f} г")


@bot.message_handler(commands=["refill"])
@owner_only
def refill_cmd(message):
    publish_cmd({"refill": True})
    bot.reply_to(message, "Запит на долив надіслано")


@bot.message_handler(commands=["stop"])
@owner_only
def stop_cmd(message):
    publish_cmd({"refill_stop": True})
    bot.reply_to(message, "Запит на зупинку насоса надіслано")


@bot.message_handler(commands=["refillon"])
@owner_only
def refill_on(message):
    publish_cmd({"refill_auto": True})
    bot.reply_to(message, "Автодолив УВІМКНЕНО")


@bot.message_handler(commands=["refilloff"])
@owner_only
def refill_off(message):
    publish_cmd({"refill_auto": False})
    bot.reply_to(message, "Автодолив ВИМКНЕНО")


if __name__ == "__main__":
    log.info("Telegram bot running, база даних: %s", DB_PATH)
    # infinity_polling сам перепідключається після збоїв мережі, на відміну від polling()
    bot.infinity_polling(timeout=20, long_polling_timeout=20)
