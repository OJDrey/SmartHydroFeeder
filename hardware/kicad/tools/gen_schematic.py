"""Генератор принципової схеми SmartHydroFeeder для KiCad 10.

Створює в hardware/kicad/:
  SHF.kicad_sym                 бібліотека символів за ЄСКД
  gost_form1.kicad_wks          рамка зі штампом за ГОСТ 2.104 (форма 1)
  smarthydrofeeder.kicad_sch    схема (аркуш A3)
  smarthydrofeeder.kicad_pro    проєкт
  sym-lib-table

УВАГА: повторний запуск перезапише схему. Після ручних правок у KiCad
не запускайте скрипт або спершу збережіть копію.
"""
import math
import os
import sys
import uuid
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parts import VALUES  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
PROJECT = "smarthydrofeeder"
LIB = "SHF"
SHEET_UUID = "6f1c2a52-1d3e-4b8a-9d8e-5a1b7c3e2f10"

LW = 0.3          # товщина ліній символів, мм
WIRE_W = 0.3      # товщина ліній зв'язку
REF_SIZE = 2.5    # шрифт позиційних позначень
PIN_NAME_SIZE = 1.8


def uid():
    return str(uuid.uuid4())


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def f(v):
    v = round(v, 4)
    return ("%.4f" % v).rstrip("0").rstrip(".") if v != int(v) else str(int(v))


# ---------------------------------------------------------------- символи

class Sym:
    def __init__(self, name, ref, ref_at=(0, 0), ref_just="left", power=False,
                 show_pin_names=False, jumpers=False, value_at=None):
        self.name, self.ref = name, ref
        self.ref_at, self.ref_just = ref_at, ref_just
        self.power = power
        self.show_pin_names = show_pin_names
        self.jumpers = jumpers
        self.value_at = value_at  # для символів живлення: показувати значення
        self.items = []
        self.jumper_groups = []
        self.pins = []  # (number, name, x, y, angle, length, hide)

    def poly(self, pts, w=LW, fill="none", dash=False):
        self.items.append(("poly", pts, w, fill, dash))
        return self

    def rect(self, x1, y1, x2, y2, w=LW):
        self.items.append(("rect", (x1, y1, x2, y2), w))
        return self

    def circle(self, cx, cy, r, w=LW):
        self.items.append(("circle", (cx, cy, r), w))
        return self

    def text(self, s, x, y, size=1.8):
        self.items.append(("text", s, x, y, size))
        return self

    def pin(self, num, name, x, y, angle, length, hide=False):
        self.pins.append((str(num), name, x, y, angle, length, hide))
        return self

    def pin_end(self, key):
        for num, name, x, y, *_ in self.pins:
            if key in (num, name):
                return x, y
        raise KeyError(f"{self.name}: немає виводу {key}")

    def sexpr(self, full_name):
        o = [f"(symbol {q(full_name)}"]
        if self.power:
            o.append("(power global)")
        o.append("(pin_numbers (hide yes))")
        o.append("(pin_names (offset 0.6)" + ("" if self.show_pin_names else " (hide yes)") + ")")
        o.append("(exclude_from_sim no) (in_bom yes) (on_board yes) (in_pos_files yes)")
        o.append("(duplicate_pin_numbers_are_jumpers no)")
        if self.jumper_groups:
            o.append("(jumper_pin_groups " + " ".join(
                "(" + " ".join(q(n) for n in g) + ")" for g in self.jumper_groups) + ")")
        rx, ry = self.ref_at
        hide_ref = " (hide yes)" if self.power else ""
        o.append(f"(property \"Reference\" {q(self.ref)} (at {f(rx)} {f(ry)} 0){hide_ref} "
                 f"(effects (font (size {REF_SIZE} {REF_SIZE}) (italic yes)) (justify {self.ref_just})))")
        if self.value_at:
            vx, vy = self.value_at
            o.append(f"(property \"Value\" {q(self.name)} (at {f(vx)} {f(vy)} 0) "
                     f"(effects (font (size {REF_SIZE} {REF_SIZE}) (italic yes))))")
        else:
            o.append(f"(property \"Value\" {q(self.name)} (at 0 0 0) (hide yes) "
                     f"(effects (font (size 1.27 1.27))))")
        for p in ("Footprint", "Datasheet", "Description"):
            o.append(f"(property {q(p)} \"\" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))")
        o.append(f"(symbol {q(self.name + '_1_1')}")
        for it in self.items:
            o.append(item_sexpr(it))
        for num, name, x, y, ang, ln, hide in self.pins:
            ptype = getattr(self, "pin_type", None) or ("power_in" if self.power else "passive")
            o.append(f"(pin {ptype} line (at {f(x)} {f(y)} {ang}) (length {f(ln)})"
                     + (" (hide yes)" if hide else "")
                     + f" (name {q(name)} (effects (font (size {PIN_NAME_SIZE} {PIN_NAME_SIZE}) (italic yes))))"
                     + f" (number {q(num)} (effects (font (size 1.27 1.27)))))")
        o.append(")")
        o.append("(embedded_fonts no)")
        o.append(")")
        return "\n".join(o)


def item_sexpr(it):
    kind = it[0]
    if kind == "poly":
        _, pts, w, fill, dash = it
        p = " ".join(f"(xy {f(x)} {f(y)})" for x, y in pts)
        return (f"(polyline (pts {p}) (stroke (width {w}) (type {'dash' if dash else 'default'})) "
                f"(fill (type {fill})))")
    if kind == "rect":
        _, (x1, y1, x2, y2), w = it
        return (f"(rectangle (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) "
                f"(stroke (width {w}) (type default)) (fill (type none)))")
    if kind == "circle":
        _, (cx, cy, r), w = it
        return (f"(circle (center {f(cx)} {f(cy)}) (radius {f(r)}) "
                f"(stroke (width {w}) (type default)) (fill (type none)))")
    if kind == "text":
        _, s, x, y, size = it
        return (f"(text {q(s)} (at {f(x)} {f(y)} 0) "
                f"(effects (font (size {size} {size}) (italic yes))))")
    raise ValueError(kind)


SYMS = {}


def define(sym):
    SYMS[sym.name] = sym
    return sym


# Резистор, вертикальний і горизонтальний (ГОСТ 2.728: 10 × 4 мм)
define(Sym("R", "R", ref_at=(3.2, 0)).rect(-2, -5, 2, 5)
       .pin(1, "1", 0, 7.62, 270, 2.62).pin(2, "2", 0, -7.62, 90, 2.62))
define(Sym("RH", "R", ref_at=(0, 3.4), ref_just="bottom").rect(-5, -2, 5, 2)
       .pin(1, "1", -7.62, 0, 0, 2.62).pin(2, "2", 7.62, 0, 180, 2.62))

# Конденсатор неполярний
define(Sym("C", "C", ref_at=(3.8, 0))
       .poly([(-3, 0.75), (3, 0.75)], 0.4).poly([(-3, -0.75), (3, -0.75)], 0.4)
       .pin(1, "1", 0, 5.08, 270, 4.33).pin(2, "2", 0, -5.08, 90, 4.33))

# Конденсатор оксидний полярний: додатна обкладка — прямокутник
define(Sym("CP", "C", ref_at=(3.8, 0))
       .rect(-3, 0.6, 3, 1.4).poly([(-3, -0.75), (3, -0.75)], 0.4)
       .text("+", -2.2, 2.6, 1.8)
       .pin(1, "+", 0, 5.08, 270, 3.68).pin(2, "-", 0, -5.08, 90, 4.33))

# Діод Шотткі горизонтальний (анод зліва) і вертикальний (катод вгорі)
define(Sym("D_H", "VD", ref_at=(0, 3.4), ref_just="bottom")
       .poly([(-2, 2), (-2, -2), (2, 0), (-2, 2)])
       .poly([(2, 2), (2, -2)], 0.4)
       .poly([(2, 2), (1.3, 2), (1.3, 1.4)]).poly([(2, -2), (2.7, -2), (2.7, -1.4)])
       .poly([(-3, 0), (-2, 0)]).poly([(2, 0), (3, 0)])
       .pin(2, "A", -5.08, 0, 0, 2.08).pin(1, "K", 5.08, 0, 180, 2.08))
define(Sym("D_V", "VD", ref_at=(-3.4, 0), ref_just="right")
       .poly([(-2, -2), (2, -2), (0, 2), (-2, -2)])
       .poly([(-2, 2), (2, 2)], 0.4)
       .poly([(-2, 2), (-2, 1.3), (-1.4, 1.3)]).poly([(2, 2), (2, 2.7), (1.4, 2.7)])
       .pin(1, "K", 0, 5.08, 270, 3.08).pin(2, "A", 0, -5.08, 90, 3.08))

# Польовий транзистор з ізольованим затвором, індукований n-канал (ГОСТ 2.730)
vt1 = Sym("NMOS", "VT", ref_at=(7.2, 2.5))
vt1.circle(1.27, 0, 5.08)
vt1.poly([(-0.6, -2.54), (-0.6, 2.54)], 0.4)
for y1, y2 in ((1.6, 3.4), (-0.9, 0.9), (-3.4, -1.6)):
    vt1.poly([(0.6, y1), (0.6, y2)], 0.5)
vt1.poly([(0.6, 2.54), (2.54, 2.54)])
vt1.poly([(0.6, -2.54), (2.54, -2.54)])
vt1.poly([(0.6, 0), (2.54, 0), (2.54, -2.54)])
vt1.poly([(0.7, 0), (1.7, 0.5), (1.7, -0.5), (0.7, 0)], 0.2, fill="outline")
vt1.pin(1, "G", -7.62, -2.54, 0, 7.02).pin(2, "D", 2.54, 7.62, 270, 5.08).pin(3, "S", 2.54, -7.62, 90, 5.08)
define(vt1)

# Біполярний транзистор n-p-n
vt2 = Sym("NPN", "VT", ref_at=(6.6, 2.5))
vt2.circle(0.9, 0, 4.6)
vt2.poly([(-1.27, -2.54), (-1.27, 2.54)], 0.5)
vt2.poly([(-1.27, 1.0), (2.54, 3.6)])
vt2.poly([(-1.27, -1.0), (2.54, -3.6)])
_dx, _dy = 3.81, -2.6
_n = math.hypot(_dx, _dy)
_ux, _uy = _dx / _n, _dy / _n
_tip = (-1.27 + _ux * 3.4, -1.0 + _uy * 3.4)
_base = (_tip[0] - _ux * 1.4, _tip[1] - _uy * 1.4)
_px, _py = -_uy * 0.55, _ux * 0.55
vt2.poly([_tip, (_base[0] + _px, _base[1] + _py), (_base[0] - _px, _base[1] - _py), _tip], 0.2, fill="outline")
vt2.pin(1, "B", -7.62, 0, 0, 6.35).pin(2, "C", 2.54, 7.62, 270, 4.02).pin(3, "E", 2.54, -7.62, 90, 4.02)
define(vt2)

# Зумер (ГОСТ 2.741): напівкруг на основі
ha = Sym("BUZZER", "HA", ref_at=(-2.2, 0), ref_just="right")
ha.poly([(0, -3), (0, 3)], 0.4)
_arc = [(3 * math.sin(math.radians(a)), 3 * math.cos(math.radians(a))) for a in range(0, 181, 15)]
ha.poly(_arc)
ha.text("+", -1.5, 4.2, 1.6)
ha.pin(1, "+", 0, 5.08, 270, 2.08).pin(2, "-", 0, -5.08, 90, 2.08)
define(ha)

# Електродвигун (насос): коло з літерою M, виводи зліва
m = Sym("MOTOR", "M", ref_at=(0, 6.4), ref_just="bottom")
m.circle(0, 0, 4.5).text("M", 0, 0, 3.0)
_ln = 7.62 - math.sqrt(4.5 ** 2 - 2.54 ** 2)
m.text("+", -5.2, 4.0, 1.6)
m.pin(1, "+", -7.62, 2.54, 0, _ln).pin(2, "-", -7.62, -2.54, 0, _ln)
define(m)

# Кнопка з замикальним контактом (ГОСТ 2.755), привід зліва
sb = Sym("BUTTON", "SB", ref_at=(-6.4, -2.2), ref_just="right")
sb.poly([(0, -2.54), (-2.3, 2.9)])
sb.poly([(-1.15, 0.18), (-4.6, 0.18)], 0.25, dash=True)
sb.poly([(-3.9, 1.5), (-4.6, 1.5), (-4.6, -1.1), (-3.9, -1.1)])
sb.pin(1, "1", 0, 7.62, 270, 5.08).pin(2, "2", 0, -7.62, 90, 5.08)
define(sb)

# Поплавковий вимикач рівня з розмикальним контактом (ГОСТ 2.755, 2.756), привід справа.
# Показано стан без води: контакт замкнений.
sl = Sym("FLOAT", "SL", ref_at=(3.2, -5.2))
sl.poly([(0, 2.54), (2.5, 2.54)])
sl.poly([(0, -2.54), (2.9, 3.3)])
sl.poly([(1.45, 0.38), (4.4, 0.38)], 0.25, dash=True)
sl.circle(5.6, 0.38, 1.2)
sl.poly([(3.8, -1.6), (7.4, -1.6)])
sl.poly([(4.2, -2.3), (7.0, -2.3)], 0.2)
sl.pin(1, "1", 0, 7.62, 270, 5.08).pin(2, "2", 0, -7.62, 90, 5.08)
define(sl)

# Тензодатчик: міст із чотирьох тензорезисторів
b = Sym("LOADCELL", "B", ref_at=(-7.62, 11.4), ref_just="left bottom")
b.rect(-7.62, -10.16, 7.62, 10.16)
_c = (-1.0, 0.0)
_r = 5.4
_nodes = [(_c[0], _c[1] + _r), (_c[0] + _r, _c[1]), (_c[0], _c[1] - _r), (_c[0] - _r, _c[1])]
for i in range(4):
    (x1, y1), (x2, y2) = _nodes[i], _nodes[(i + 1) % 4]
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    ux, uy = (x2 - x1), (y2 - y1)
    L = math.hypot(ux, uy)
    ux, uy = ux / L, uy / L
    vx, vy = -uy, ux
    hl, hw = 1.4, 0.6
    a = (mx - ux * hl, my - uy * hl)
    bb = (mx + ux * hl, my + uy * hl)
    b.poly([(x1, y1), a])
    b.poly([bb, (x2, y2)])
    b.poly([(a[0] + vx * hw, a[1] + vy * hw), (bb[0] + vx * hw, bb[1] + vy * hw),
            (bb[0] - vx * hw, bb[1] - vy * hw), (a[0] - vx * hw, a[1] - vy * hw),
            (a[0] + vx * hw, a[1] + vy * hw)], 0.25)
for i, (nm, y) in enumerate((("E+", 7.62), ("E-", 2.54), ("A-", -2.54), ("A+", -7.62)), start=1):
    b.text(nm, 10.0, y + 1.1, 1.4)
    b.pin(i, nm, 12.7, y, 180, 5.08)
define(b)


def connector(name, n, left=True, right=True):
    """Роз'єм у вигляді таблиці (ГОСТ 2.755): клітинка на кожен контакт."""
    s = Sym(name, "X", ref_at=(0, n * 2.54 + 1.4), ref_just="bottom", jumpers=True)
    top = n * 2.54
    s.rect(-2.54, -top, 2.54, top)
    for k in range(1, n):
        s.poly([(-2.54, top - k * 5.08), (2.54, top - k * 5.08)])
    for k in range(1, n + 1):
        y = top - (k - 0.5) * 5.08
        s.text(str(k), 0, y, 2.0)
        if left:
            s.pin(k, str(k), -7.62, y, 0, 5.08)
        if right:
            # окремий номер виводу, але в одній групі-перемичці з лівим: це той самий контакт
            s.pin(n + k if left else k, str(k), 7.62, y, 180, 5.08)
            if left:
                s.jumper_groups.append((str(k), str(n + k)))
    return define(s)


connector("XR2", 2, left=False)
connector("X2S", 2)
connector("X4S", 4)


def module(name, ref, w, h, left=(), right=(), top=(), bottom=(), pitch=None):
    """Функціональний вузол (модуль) — прямокутник із назвами виводів."""
    s = Sym(name, ref, ref_at=(-w / 2, h / 2 + 1.0), ref_just="left bottom", show_pin_names=True)
    s.rect(-w / 2, -h / 2, w / 2, h / 2)
    for nm, y in left:
        s.pin(nm, nm, -w / 2 - 7.62, y, 0, 7.62)
    for nm, y in right:
        s.pin(nm, nm, w / 2 + 7.62, y, 180, 7.62)
    for nm, x in top:
        s.pin(nm, nm, x, h / 2 + 5.08, 270, 5.08)
    for nm, x in bottom:
        s.pin(nm, nm, x, -h / 2 - 5.08, 90, 5.08)
    return define(s)


module("ESP32_DEVKIT", "A", 30.48, 50.8,
       left=[("GPIO19", 12.7), ("GPIO23", 7.62), ("GPIO32", -5.08), ("GPIO35", -12.7)],
       right=[("GPIO26", 15.24), ("GPIO27", 10.16), ("GPIO33", -2.54), ("GPIO4", -7.62),
              ("GPIO34", -12.7), ("GPIO25", -17.78)],
       top=[("VIN", -5.08), ("3V3", 5.08)], bottom=[("GND", 0)])
module("HX711", "A", 20.32, 20.32,
       left=[("E+", 7.62), ("E-", 2.54), ("A-", -2.54), ("A+", -7.62)],
       right=[("VCC", 7.62), ("DT", 2.54), ("SCK", -2.54), ("GND", -7.62)])
module("OLED_I2C", "A", 20.32, 20.32,
       left=[("VCC", 7.62), ("SDA", 2.54), ("SCL", -2.54), ("GND", -7.62)])
module("HC_SR04", "A", 20.32, 20.32,
       right=[("VCC", 7.62), ("TRIG", 2.54), ("ECHO", -2.54), ("GND", -7.62)])


def power_sym(name, up=True):
    s = Sym(name, "#PWR", ref_at=(0, -5), power=True,
            value_at=(0, 4.6) if up else None)
    if up:
        s.poly([(0, 0), (0, 2.54)]).poly([(-1.6, 2.54), (1.6, 2.54)], 0.4)
    else:
        s.poly([(0, 0), (0, -2.54)])
        s.poly([(-2.5, -2.54), (2.5, -2.54)], 0.4)
        s.poly([(-1.5, -3.3), (1.5, -3.3)], 0.35)
        s.poly([(-0.5, -4.06), (0.5, -4.06)], 0.3)
    s.pin(1, name, 0, 0, 90 if up else 270, 0, hide=True)
    return define(s)


power_sym("+5V")
power_sym("+3V3")
power_sym("GND", up=False)

# Позначка джерела живлення для ERC; графіки немає, на друк не виходить
_flag = Sym("PWR_FLAG", "#FLG", power=True)
_flag.pin_type = "power_out"
_flag.pin(1, "pwr", 0, 0, 90, 0, hide=True)
define(_flag)


# ---------------------------------------------------------------- розміщення

class Placed:
    def __init__(self, ref, sym, x, y):
        self.ref, self.sym, self.x, self.y = ref, SYMS[sym], x, y

    def pin(self, key):
        px, py = self.sym.pin_end(key)
        return (round(self.x + px, 4), round(self.y - py, 4))


PARTS = {}
POWER = []
WIRES = []
TEXTS = []


def place(ref, sym, x, y):
    PARTS[ref] = Placed(ref, sym, x, y)


def resolve(p):
    if isinstance(p, tuple) and isinstance(p[0], str):
        return PARTS[p[0]].pin(p[1])
    return (round(p[0], 4), round(p[1], 4))


def wire(*path):
    """Шлях: вивід або точка, далі ('H', x) / ('V', y) / точка, у кінці вивід або точка."""
    pts = [resolve(path[0])]
    for step in path[1:]:
        cx, cy = pts[-1]
        if isinstance(step, tuple) and step[0] == "H":
            pts.append((round(step[1], 4), cy))
        elif isinstance(step, tuple) and step[0] == "V":
            pts.append((cx, round(step[1], 4)))
        else:
            pts.append(resolve(step))
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        assert x1 == x2 or y1 == y2, f"Похилий відрізок {pts}"
        if (x1, y1) != (x2, y2):
            WIRES.append(((x1, y1), (x2, y2)))
    return pts[-1]


def pwr(name, at):
    POWER.append((name, resolve(at)))


def note(s, x, y, size=2.5):
    TEXTS.append((s, x, y, size))


# Живлення
place("X1", "XR2", 38.1, 45.72)
place("VD2", "D_H", 55.88, 43.18)
place("C1", "CP", 68.58, 48.26)
place("C2", "C", 86.36, 48.26)
wire(("X1", "1"), ("VD2", "A"))
wire(("VD2", "K"), ("H", 86.36))
pwr("+5V", (86.36, 43.18))
wire(("X1", "2"), ("V", 58.42)); pwr("GND", (45.72, 58.42))
wire(("C1", "-"), ("V", 58.42)); pwr("GND", (68.58, 58.42))
wire(("C2", "2"), ("V", 58.42)); pwr("GND", (86.36, 58.42))

# Мікроконтролерний модуль
place("A1", "ESP32_DEVKIT", 190.5, 124.46)
wire(("A1", "VIN"), ("V", 88.9)); pwr("+5V", (185.42, 88.9))
wire(("A1", "3V3"), ("V", 88.9)); pwr("+3V3", (195.58, 88.9))
wire(("A1", "GND"), ("V", 160.02)); pwr("GND", (190.5, 160.02))

# Ваги: тензодатчик, роз'єм, АЦП
place("B1", "LOADCELL", 60.96, 114.3)
place("X2", "X4S", 88.9, 114.3)
place("A2", "HX711", 119.38, 114.3)
for k, nm in enumerate(("E+", "E-", "A-", "A+"), start=1):
    wire(("B1", nm), ("X2", str(k)))
# праві виводи X2 (той самий номер контакту) з'єднуються з A2
_x2r = SYMS["X4S"]
for k, nm in enumerate(("E+", "E-", "A-", "A+"), start=1):
    px, py = [(x, y) for num, nm_, x, y, ang, *_ in _x2r.pins if nm_ == str(k) and ang == 180][0]
    start = (round(88.9 + px, 4), round(114.3 - py, 4))
    wire(start, ("A2", nm))
wire(("A2", "DT"), ("A1", "GPIO19"))
wire(("A2", "SCK"), ("A1", "GPIO23"))
wire(("A2", "VCC"), ("H", 142.24), ("V", 101.6)); pwr("+3V3", (142.24, 101.6))
wire(("A2", "GND"), ("H", 142.24), ("V", 127.0)); pwr("GND", (142.24, 127.0))

# Ультразвуковий датчик (варіант замість B1, A2)
place("A4", "HC_SR04", 119.38, 152.4)
place("R1", "RH", 147.32, 154.94)
place("R2", "R", 162.56, 162.56)
wire(("A4", "VCC"), ("H", 142.24), ("V", 139.7)); pwr("+5V", (142.24, 139.7))
wire(("A4", "GND"), ("H", 142.24), ("V", 165.1)); pwr("GND", (142.24, 165.1))
wire(("A4", "TRIG"), ("H", 157.48), ("V", 129.54), ("A1", "GPIO32"))
wire(("A4", "ECHO"), ("R1", "1"))
wire(("R1", "2"), ("H", 162.56), ("V", 137.16), ("A1", "GPIO35"))
wire(("R2", "2"), ("V", 175.26)); pwr("GND", (162.56, 175.26))

# Дисплей
place("A3", "OLED_I2C", 248.92, 88.9)
wire(("A1", "GPIO26"), ("H", 220.98), ("V", 86.36), ("A3", "SDA"))
wire(("A1", "GPIO27"), ("H", 223.52), ("V", 91.44), ("A3", "SCL"))
wire(("A3", "VCC"), ("H", 228.6), ("V", 76.2)); pwr("+3V3", (228.6, 76.2))
wire(("A3", "GND"), ("H", 228.6), ("V", 101.6)); pwr("GND", (228.6, 101.6))

# Насос
place("R3", "RH", 226.06, 127.0)
place("R4", "R", 236.22, 134.62)
place("VT1", "NMOS", 251.46, 124.46)
place("VD1", "D_V", 264.16, 106.68)
place("X3", "X2S", 281.94, 106.68)
place("M1", "MOTOR", 302.26, 106.68)
wire(("A1", "GPIO33"), ("R3", "1"))
wire(("R3", "2"), ("VT1", "G"))
wire(("R4", "2"), ("V", 147.32)); pwr("GND", (236.22, 147.32))
wire(("VT1", "S"), ("V", 137.16)); pwr("GND", (254.0, 137.16))
wire(("VT1", "D"), ("V", 111.76), ("VD1", "A"))
wire(("VD1", "A"), ("H", 271.78), ("V", 109.22), ("X3", "2"))
wire(("VD1", "K"), ("V", 96.52)); pwr("+5V", (264.16, 96.52))
wire(("VD1", "K"), ("H", 269.24), ("V", 104.14), ("X3", "1"))
_x2s = SYMS["X2S"]
for k, nm in ((1, "+"), (2, "-")):
    px, py = [(x, y) for num, nm_, x, y, ang, *_ in _x2s.pins if nm_ == str(k) and ang == 180][0]
    wire((round(281.94 + px, 4), round(106.68 - py, 4)), ("M1", nm))

# Зумер
place("R6", "RH", 256.54, 165.1)
place("VT2", "NPN", 274.32, 165.1)
place("HA1", "BUZZER", 276.86, 147.32)
wire(("A1", "GPIO4"), ("H", 223.52), ("V", 165.1), ("R6", "1"))
wire(("R6", "2"), ("VT2", "B"))
wire(("VT2", "C"), ("HA1", "-"))
wire(("HA1", "+"), ("V", 137.16)); pwr("+5V", (276.86, 137.16))
wire(("VT2", "E"), ("V", 177.8)); pwr("GND", (276.86, 177.8))

# Поплавок
place("R5", "R", 233.68, 187.96)
place("C3", "C", 243.84, 200.66)
place("X4", "X2S", 269.24, 198.12)
place("SL1", "FLOAT", 287.02, 203.2)
wire(("A1", "GPIO34"), ("H", 220.98), ("V", 195.58), ("X4", "1"))
wire(("R5", "1"), ("V", 175.26)); pwr("+3V3", (233.68, 175.26))
wire(("C3", "2"), ("V", 210.82)); pwr("GND", (243.84, 210.82))
wire(("X4", "2"), ("H", 259.08), ("V", 205.74)); pwr("GND", (259.08, 205.74))
for k, target in ((1, ("SL1", "1")), (2, ("SL1", "2"))):
    px, py = [(x, y) for num, nm_, x, y, ang, *_ in _x2s.pins if nm_ == str(k) and ang == 180][0]
    start = (round(269.24 + px, 4), round(198.12 - py, 4))
    if k == 1:
        wire(start, target)
    else:
        wire(start, ("H", 281.94), ("V", 210.82), target)

# Кнопка
place("SB1", "BUTTON", 218.44, 215.9)
wire(("A1", "GPIO25"), ("H", 218.44), ("SB1", "1"))
wire(("SB1", "2"), ("V", 228.6)); pwr("GND", (218.44, 228.6))

note("1. A4, R1, R2 встановлюються замість B1, A2 (варіант з ультразвуковим датчиком рівня).", 25.4, 252)
note("2. Модулі A1–A4 підключаються через штирьові роз'єми.", 25.4, 258)
note("3. Колір проводів тензодатчика B1: E+ червоний, E- чорний, A- білий, A+ зелений.", 25.4, 264)


# ---------------------------------------------------------------- з'єднувальні точки

def split_wires():
    """Ділить відрізки в точках, де на них лежить кінець іншого відрізка або вивід."""
    points = set()
    for a, b in WIRES:
        points.add(a); points.add(b)
    pin_pts = set()
    for p in PARTS.values():
        for num, name, x, y, *_ in p.sym.pins:
            pin_pts.add((round(p.x + x, 4), round(p.y - y, 4)))
    for _, at in POWER:
        pin_pts.add(at)
    points |= pin_pts
    out = []
    for a, b in WIRES:
        cuts = [a, b]
        for pt in points:
            if pt in (a, b):
                continue
            if a[0] == b[0] == pt[0] and min(a[1], b[1]) < pt[1] < max(a[1], b[1]):
                cuts.append(pt)
            elif a[1] == b[1] == pt[1] and min(a[0], b[0]) < pt[0] < max(a[0], b[0]):
                cuts.append(pt)
        cuts.sort(key=lambda p: (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2)
        for p1, p2 in zip(cuts, cuts[1:]):
            out.append((p1, p2))
    # прибрати дублікати
    seen, res = set(), []
    for a, b in out:
        k = tuple(sorted((a, b)))
        if k not in seen:
            seen.add(k); res.append((a, b))
    return res, pin_pts


def junctions(wires, pin_pts):
    cnt = defaultdict(int)
    for a, b in wires:
        cnt[a] += 1; cnt[b] += 1
    for pt in pin_pts:
        if pt in cnt:
            cnt[pt] += 1
    return sorted(pt for pt, c in cnt.items() if c >= 3)


# ---------------------------------------------------------------- запис файлів

def write_lib():
    body = "\n".join(s.sexpr(s.name) for s in SYMS.values())
    with open(os.path.join(OUT, f"{LIB}.kicad_sym"), "w", encoding="utf-8") as fh:
        fh.write(f'(kicad_symbol_lib (version 20251024) (generator "shf_gen") (generator_version "10.0")\n{body}\n)\n')


def write_sch():
    wires, pin_pts = split_wires()
    juncs = junctions(wires, pin_pts)
    o = ["(kicad_sch (version 20260306) (generator \"eeschema\") (generator_version \"10.0\")",
         f"(uuid {q(SHEET_UUID)})", '(paper "A3")',
         "(title_block",
         '(title "Поїлка автоматична для дрібних тварин")',
         '(company "ТФК ТНТУ, гр. КІ-305п")',
         '(comment 1 "ХХХХ.ХХХХХХ.001 Е3")',
         '(comment 2 "Схема електрична принципова")',
         '(comment 3 "Репета А. Н.")',
         '(comment 4 "Ляпандра А. С.")',
         '(comment 5 "")',
         '(comment 6 "")',
         ")",
         "(lib_symbols"]
    for s in SYMS.values():
        o.append(s.sexpr(f"{LIB}:{s.name}"))
    o.append(")")
    for (x, y) in juncs:
        o.append(f"(junction (at {f(x)} {f(y)}) (diameter 1.2) (color 0 0 0 0) (uuid {q(uid())}))")
    for (x1, y1), (x2, y2) in wires:
        o.append(f"(wire (pts (xy {f(x1)} {f(y1)}) (xy {f(x2)} {f(y2)})) "
                 f"(stroke (width {WIRE_W}) (type default)) (uuid {q(uid())}))")
    for s, x, y, size in TEXTS:
        o.append(f"(text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) "
                 f"(effects (font (size {size} {size}) (italic yes)) (justify left bottom)) (uuid {q(uid())}))")

    def symbol_inst(ref, sym, x, y, show_value=False, ref_hidden=False):
        rx, ry = sym.ref_at
        vis = " (hide yes)" if ref_hidden else ""
        lines = [f"(symbol (lib_id {q(LIB + ':' + sym.name)}) (at {f(x)} {f(y)} 0) (unit 1) (body_style 1)",
                 "(exclude_from_sim no) (in_bom yes) (on_board yes) (in_pos_files yes) (dnp no)",
                 f"(uuid {q(uid())})",
                 f"(property \"Reference\" {q(ref)} (at {f(x + rx)} {f(y - ry)} 0){vis} "
                 f"(effects (font (size {REF_SIZE} {REF_SIZE}) (italic yes)) (justify {sym.ref_just})))"]
        if show_value:
            vx, vy = sym.value_at
            lines.append(f"(property \"Value\" {q(sym.name)} (at {f(x + vx)} {f(y - vy)} 0) "
                         f"(effects (font (size {REF_SIZE} {REF_SIZE}) (italic yes))))")
        else:
            lines.append(f"(property \"Value\" {q(VALUES.get(ref, sym.name))} (at {f(x)} {f(y)} 0) (hide yes) "
                         f"(effects (font (size 1.27 1.27))))")
        for p in ("Footprint", "Datasheet", "Description"):
            lines.append(f"(property {q(p)} \"\" (at {f(x)} {f(y)} 0) (hide yes) (effects (font (size 1.27 1.27))))")
        nums = []
        for num, *_ in sym.pins:
            if num not in nums:
                nums.append(num)
        for num in nums:
            lines.append(f"(pin {q(num)} (uuid {q(uid())}))")
        lines.append(f"(instances (project {q(PROJECT)} (path {q('/' + SHEET_UUID)} (reference {q(ref)}) (unit 1))))")
        lines.append(")")
        return "\n".join(lines)

    for p in PARTS.values():
        o.append(symbol_inst(p.ref, p.sym, p.x, p.y))
    for i, (name, (x, y)) in enumerate(POWER, start=1):
        sym = SYMS[name]
        o.append(symbol_inst(f"#PWR{i:02d}", sym, x, y, show_value=sym.value_at is not None, ref_hidden=True))
    # джерела: +5V після VD2, +3V3 зі стабілізатора A1, GND з роз'єму X1
    for i, at in enumerate(((86.36, 43.18), (195.58, 88.9), (45.72, 58.42)), start=1):
        o.append(symbol_inst(f"#FLG{i:02d}", SYMS["PWR_FLAG"], at[0], at[1], ref_hidden=True))
    o.append('(sheet_instances (path "/" (page "1")))')
    o.append("(embedded_fonts no)")
    o.append(")")
    with open(os.path.join(OUT, f"{PROJECT}.kicad_sch"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(o) + "\n")
    return wires, juncs


def write_project():
    import json
    pro = {
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 3},
        "schematic": {"page_layout_descr_file": "gost_form1.kicad_wks"},
        "sheets": [[SHEET_UUID, "Root"]],
    }
    with open(os.path.join(OUT, f"{PROJECT}.kicad_pro"), "w", encoding="utf-8") as fh:
        json.dump(pro, fh, indent=2, ensure_ascii=False)
    with open(os.path.join(OUT, "sym-lib-table"), "w", encoding="utf-8") as fh:
        fh.write('(sym_lib_table\n  (version 7)\n'
                 f'  (lib (name "{LIB}")(type "KiCad")(uri "${{KIPRJMOD}}/{LIB}.kicad_sym")(options "")(descr "Символи ЄСКД для SmartHydroFeeder"))\n)\n')


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    write_lib()
    wires, juncs = write_sch()
    write_project()
    print(f"символів: {len(SYMS)}, елементів: {len(PARTS)}, живлення: {len(POWER)}, "
          f"відрізків: {len(wires)}, вузлів: {len(juncs)}")
