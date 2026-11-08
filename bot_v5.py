import paho.mqtt.client as mqtt
import telebot
import json
import time
import threading
import os

# Секрети читаються з файлу .env поруч зі скриптом (або зі змінних середовища).
# Шаблон: .env.example. Сам .env не можна нікуди викладати.
try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))
except ImportError:
    pass  # python-dotenv не встановлено: беремо лише змінні середовища

TOKEN = os.environ.get("TELEGRAM_TOKEN")
CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID")
DEVICE_ID = os.environ.get("DEVICE_ID", "device123")
MQTT_SERVER = os.environ.get("MQTT_SERVER", "broker.hivemq.com")

if not TOKEN or not CHAT_ID:
    raise SystemExit("Не задано TELEGRAM_TOKEN або TELEGRAM_CHAT_ID (див. .env.example)")

bot = telebot.TeleBot(TOKEN)

# paho-mqtt 2.x вимагає явно вказати версію API колбеків, 1.x такого параметра не має
try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:
    client = mqtt.Client()

def format_measure_message(j):
    dev = j.get("device_id", "?")
    dist = j.get("dist_cm", None)
    level = j.get("level_percent", None)
    height = j.get("height_cm", None)
    low = j.get("low_water", False)
    ts = j.get("timestamp", None)
    lines = []
    lines.append(f"📡 Device: {dev}")
    if dist is not None:
        lines.append(f"📏 Дистанція між водою та датчиком: {dist:.1f} cm")
    if height is not None:
        lines.append(f"📐 Висота пляшки: {height:.1f} cm")
    if level is not None:
        lines.append(f"💧 Рівень заповнення: {level:.1f}%")
    if low:
        lines.append("⚠️ Низький рівень води!")
    if ts is not None:
        lines.append(f"⏱ {ts}")
    return "\n".join(lines)

def on_connect(client, userdata, flags, rc, properties=None):  # сигнатура підходить і для paho 1.x, і для 2.x
    print("MQTT connected", rc)
    client.subscribe(f"smartfeeder/{DEVICE_ID}/measure")
    client.subscribe(f"smartfeeder/{DEVICE_ID}/alert")
    client.subscribe(f"smartfeeder/{DEVICE_ID}/error")

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    print("MQTT msg:", msg.topic, payload)
    try:
        j = json.loads(payload)
    except:
        bot.send_message(CHAT_ID, f"Raw MQTT: {payload}")
        return

    if msg.topic.endswith("/measure"):
        # /measure надходить лише при ручному вимірюванні — завжди показуємо
        text = format_measure_message(j)
        bot.send_message(CHAT_ID, text)

    elif msg.topic.endswith("/alert"):
        # Alert надходить і при авто (тільки якщо низький рівень), і при ручному
        triggered_by = j.get("triggered_by", "manual")
        text = format_measure_message(j)

        if triggered_by == "auto":
            # Автоматичний замір виявив низький рівень
            bot.send_message(CHAT_ID, "🚨 Автоматичний замір: низький рівень води!\n" + text)
        else:
            # Ручний замір — звичайна тривога
            bot.send_message(CHAT_ID, "🚨 Тривога\n" + text)

    elif msg.topic.endswith("/error"):
        err = j.get("error", "Unknown error")
        dev = j.get("device_id", DEVICE_ID)
        ts = j.get("timestamp", None)
        text = f"❗️ Error from {dev}:\n{err}"
        if ts:
            text += f"\n⏱ {ts}"
        bot.send_message(CHAT_ID, text)

client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_SERVER, 1883, 60)
client.loop_start()

def heartbeat_loop():
    while True:
        try:
            payload = json.dumps({"bot": "online", "timestamp": int(time.time())})
            client.publish(f"smartfeeder/{DEVICE_ID}/bot", payload, retain=True)
            print("Heartbeat ->", f"smartfeeder/{DEVICE_ID}/bot", payload)
        except Exception as e:
            print("Heartbeat publish error:", e)
        time.sleep(10)

hb_thread = threading.Thread(target=heartbeat_loop, daemon=True)
hb_thread.start()

@bot.message_handler(commands=['measure'])
def measure(message):
    client.publish(f"smartfeeder/{DEVICE_ID}/cmd", json.dumps({"measure": True}))
    bot.reply_to(message, "Запит на вимірювання відправлено")

@bot.message_handler(commands=['setheight'])
def set_height(message):
    try:
        h = float(message.text.split()[1])
        client.publish(f"smartfeeder/{DEVICE_ID}/cmd", json.dumps({"set_height": h}))
        bot.reply_to(message, f"Висота встановлена: {h} см")
    except:
        bot.reply_to(message, "Використання: /setheight 20.0")

@bot.message_handler(commands=['autoon'])
def auto_on(message):
    client.publish(f"smartfeeder/{DEVICE_ID}/cmd", json.dumps({"auto_enabled": True}))
    bot.reply_to(message, "Автоматичні вимірювання УВІМКНЕНО")

@bot.message_handler(commands=['autooff'])
def auto_off(message):
    client.publish(f"smartfeeder/{DEVICE_ID}/cmd", json.dumps({"auto_enabled": False}))
    bot.reply_to(message, "Автоматичні вимірювання ВИМКНЕНО")

@bot.message_handler(commands=['interval'])
def interval_cmd(message):
    try:
        m = float(message.text.split()[1])
        client.publish(f"smartfeeder/{DEVICE_ID}/cmd", json.dumps({"set_interval_min": m}))
        bot.reply_to(message, f"Інтервал встановлено: {m} хв")
    except:
        bot.reply_to(message, "Використання: /interval 10  (в хвилинах)")

print("Telegram bot running")
bot.polling()
