"""Генератор переліку елементів (ПЕ3) за ГОСТ 2.701 для KiCad 10.

Створює в hardware/kicad/pe3/:
  gost_form2_pe3.kicad_wks   рамка A4, таблиця переліку, основний напис за формою 2 (ГОСТ 2.104)
  pe3.kicad_sch              записи переліку (текст)
  pe3.kicad_pro

Дані беруться з parts.py — того самого файла, з якого схема бере номінали.
"""
import json
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parts import PARTS, PLURAL  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pe3")
SHEET_UUID = "a3e9c1d4-7b2f-4c8e-9f10-2d6b5e8a4c71"

# Геометрія (мм): рамка A4 з полями 20 / 5 / 5 / 5
FRAME_L, FRAME_T = 20.0, 5.0
COLS = [20, 110, 10, 45]          # Поз. позначення | Найменування | Кіл. | Примітка
HEADER_H = 15.0
ROW_H = 8.0
STAMP_H = 40.0
TABLE_H = 287.0 - STAMP_H         # від верхнього краю рамки до основного напису
ROWS = int((TABLE_H - HEADER_H) // ROW_H)  # 29 рядків на першому аркуші

TEXT = 3.0   # шрифт записів


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def f(v):
    v = round(v, 3)
    return str(int(v)) if v == int(v) else ("%.3f" % v).rstrip("0")


# ---------------------------------------------------------------- рядки переліку

def prefix(ref):
    return re.match(r"[A-Z]+", ref).group(0)


def number(ref):
    return int(re.search(r"\d+", ref).group(0))


def join_refs(refs):
    """R4, R5 — два підряд; R4...R6 — три й більше (ГОСТ 2.701)."""
    if len(refs) == 1:
        return refs[0]
    if len(refs) == 2:
        return f"{refs[0]}, {refs[1]}"
    return f"{refs[0]}...{refs[-1]}"


def cap(s):
    return s[:1].upper() + s[1:]


def build_rows():
    """Повертає список рядків: ('head', назва) або ('item', позначення, найменування, кількість, примітка)."""
    groups = []
    for ref, kind, typ, note in PARTS:
        if groups and groups[-1][0] == prefix(ref):
            groups[-1][1].append((ref, kind, typ, note))
        else:
            groups.append((prefix(ref), [(ref, kind, typ, note)]))

    rows = []
    for _, items in groups:
        kinds = {k for _, k, _, _ in items}
        headed = len(items) > 1 and len(kinds) == 1 and next(iter(kinds)) in PLURAL
        if headed:
            rows.append(("head", PLURAL[next(iter(kinds))]))
        # однакові елементи з послідовними номерами об'єднуються в один рядок
        merged = []
        for ref, kind, typ, note in items:
            if (merged and merged[-1][1:] == (kind, typ, note)
                    and number(ref) == number(merged[-1][0][-1]) + 1):
                merged[-1][0].append(ref)
            else:
                merged.append(([ref], kind, typ, note))
        for refs, kind, typ, note in merged:
            name = cap(typ) if headed else f"{kind} {typ}"
            rows.append(("item", join_refs(refs), name, len(refs), note))
    return rows


# ---------------------------------------------------------------- рамка з таблицею

def write_wks(path):
    xs = [12.0]                       # від лівого поля 8 мм: рамка на 20 мм від краю
    for w in COLS:
        xs.append(xs[-1] + w)
    L = []
    a = L.append
    a("(page_layout")
    a("  (setup (textsize 2.5 2.5)(linewidth 0.3)(textlinewidth 0.25)")
    a("  (left_margin 8)(right_margin 5)(top_margin 5)(bottom_margin 5))")
    a("  (rect (name frame:Rect) (start 12 0 lbcorner) (end 0 0 rtcorner) (linewidth 0.6))")
    # графи 19–23 на лівому полі
    a("  (line (name side1:Line) (start 0 60 lbcorner) (end 12 60 lbcorner) (linewidth 0.6) (repeat 3) (incry 25))")
    a("  (line (name side2:Line) (start 0 145 lbcorner) (end 12 145 lbcorner) (linewidth 0.6))")
    a("  (line (name side3:Line) (start 0 0 lbcorner) (end 12 0 lbcorner) (linewidth 0.6) (repeat 2) (incry 25))")
    a("  (line (name side4:Line) (start 0 145 lbcorner) (end 0 0 lbcorner) (linewidth 0.6) (repeat 2) (incrx 5))")
    for i, (t, y) in enumerate((("Інв. N° ориг.", 12.5), ("Підп. і дата", 42.5), ("Взам. інв. N°", 72.5),
                                 ("Інв. N° дубл.", 97.5), ("Підп. і дата", 127.5))):
        a(f'  (tbtext "{t}" (name side_t{i}:Text) (pos 2.5 {y} lbcorner) (rotate 90) (font italic) (justify center))')
    # таблиця переліку
    for i, x in enumerate(xs[1:-1]):
        a(f"  (line (name col{i}:Line) (start {f(x)} 0 ltcorner) (end {f(x)} {f(TABLE_H)} ltcorner) (linewidth 0.6))")
    a(f"  (line (name head:Line) (start 12 {f(HEADER_H)} ltcorner) (end {f(xs[-1])} {f(HEADER_H)} ltcorner) (linewidth 0.6))")
    a(f"  (line (name rows:Line) (start 12 {f(HEADER_H + ROW_H)} ltcorner) (end {f(xs[-1])} {f(HEADER_H + ROW_H)} ltcorner) "
      f"(linewidth 0.2) (repeat {ROWS - 1}) (incry {f(ROW_H)}))")
    cx = [(xs[i] + xs[i + 1]) / 2 for i in range(4)]
    a(f'  (tbtext "Поз." (name h1a:Text) (pos {f(cx[0])} 4 ltcorner) (font (size 2.5 2.5) italic) (justify center))')
    a(f'  (tbtext "позна-" (name h1b:Text) (pos {f(cx[0])} 7.5 ltcorner) (font (size 2.5 2.5) italic) (justify center))')
    a(f'  (tbtext "чення" (name h1c:Text) (pos {f(cx[0])} 11 ltcorner) (font (size 2.5 2.5) italic) (justify center))')
    a(f'  (tbtext "Найменування" (name h2:Text) (pos {f(cx[1])} 7.5 ltcorner) (font (size 3.5 3.5) italic) (justify center))')
    a(f'  (tbtext "Кіл." (name h3:Text) (pos {f(cx[2])} 7.5 ltcorner) (font (size 2.5 2.5) italic) (justify center))')
    a(f'  (tbtext "Примітка" (name h4:Text) (pos {f(cx[3])} 7.5 ltcorner) (font (size 3.5 3.5) italic) (justify center))')
    # основний напис, форма 2 (185 × 40), від правого нижнього кута рамки
    a("  (line (name s_top:Line) (start 185 40) (end 0 40) (linewidth 0.6))")
    a("  (line (name s_left:Line) (start 185 0) (end 185 40) (linewidth 0.6))")
    a("  (line (name s_v178:Line) (start 178 25) (end 178 40) (linewidth 0.6))")
    for x in (168, 145, 130, 120):
        a(f"  (line (name s_v{x}:Line) (start {x} 0) (end {x} 40) (linewidth 0.6))")
    a("  (line (name s_h5:Line) (start 185 5) (end 120 5) (repeat 4) (incry 5))")
    a("  (line (name s_h25:Line) (start 185 25) (end 120 25) (linewidth 0.6) (repeat 2) (incry 5))")
    a("  (line (name s_h35:Line) (start 185 35) (end 120 35))")
    a("  (line (name s_des:Line) (start 120 25) (end 0 25) (linewidth 0.6))")
    a("  (line (name s_v50:Line) (start 50 0) (end 50 25) (linewidth 0.6))")
    a("  (line (name s_h20:Line) (start 50 20) (end 0 20) (linewidth 0.6))")
    a("  (line (name s_h15:Line) (start 50 15) (end 0 15) (linewidth 0.6))")
    a("  (line (name s_v35:Line) (start 35 15) (end 35 25) (linewidth 0.6))")
    a("  (line (name s_v20:Line) (start 20 15) (end 20 25) (linewidth 0.6))")
    a("  (line (name s_lit:Line) (start 45 15) (end 45 20) (repeat 2) (incrx -5))")
    for t, x in (("Зм.", 181.5), ("Арк.", 173), ("N° докум.", 156.5), ("Підп.", 137.5), ("Дата", 125)):
        a(f'  (tbtext "{t}" (name t_{x}:Text) (pos {x} 27.5) (font italic) (justify center))')
    for t, y in (("Розроб.", 22.5), ("Перев.", 17.5), ("Н.контр.", 7.5), ("Затв.", 2.5)):
        a(f'  (tbtext "{t}" (name r_{y}:Text) (pos 184.5 {y}) (font italic))')
    for c, y in (("%C2", 22.5), ("%C3", 17.5), ("%C4", 7.5), ("%C5", 2.5)):
        a(f"  (tbtext {c} (name f_{y}:Text) (pos 167.5 {y}) (font italic) (maxlen 21.5))")
    a("  (tbtext %C0 (name f_des:Text) (pos 60 32.5) (font (linewidth 0.5) (size 5 5) italic) (justify center) (maxlen 119))")
    a("  (tbtext %T (name f_title:Text) (pos 85 17) (font (linewidth 0.35) (size 3.5 3.5) italic) (justify center) (maxlen 67) (maxheight 8))")
    a("  (tbtext %C1 (name f_doc:Text) (pos 85 8) (font (linewidth 0.35) (size 3.5 3.5) italic) (justify center) (maxlen 67) (maxheight 8))")
    a('  (tbtext "Літ." (name t_lit:Text) (pos 42.5 22.5) (font italic) (justify center))')
    a('  (tbtext "Арк." (name t_sh:Text) (pos 27.5 22.5) (font italic) (justify center))')
    a('  (tbtext "Аркушів" (name t_shs:Text) (pos 10 22.5) (font italic) (justify center))')
    a("  (tbtext %S (name f_sh:Text) (pos 27.5 17.5) (font italic) (justify center))")
    a("  (tbtext %N (name f_shs:Text) (pos 10 17.5) (font italic) (justify center))")
    a("  (tbtext %Y (name f_org:Text) (pos 25 7.5) (font (linewidth 0.35) (size 3.5 3.5) italic) (justify center) (maxlen 48) (maxheight 14))")
    a('  (tbtext "Копіював" (name t_copy:Text) (pos 110 -2.5) (font italic))')
    a('  (tbtext "Формат %Z" (name t_fmt:Text) (pos 40 -2.5) (font italic))')
    a(")")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(L) + "\n")


# ---------------------------------------------------------------- записи

def text(s, x, y, size=TEXT, just="left bottom"):
    return (f"(text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) "
            f"(effects (font (size {size} {size}) (italic yes)) (justify {just})) (uuid {q(str(uuid.uuid4()))}))")


def write_sch(path, rows):
    if len(rows) > ROWS:
        raise SystemExit(f"Рядків {len(rows)}, на аркуші вміщується {ROWS}: потрібен другий аркуш")
    xs = [FRAME_L]
    for w in COLS:
        xs.append(xs[-1] + w)
    o = ['(kicad_sch (version 20260306) (generator "eeschema") (generator_version "10.0")',
         f"(uuid {q(SHEET_UUID)})", '(paper "A4" portrait)',
         "(title_block",
         '(title "Поїлка автоматична для дрібних тварин")',
         '(company "ТФК ТНТУ, гр. КІ-305п")',
         '(comment 1 "ХХХХ.ХХХХХХ.001 ПЕ3")',
         '(comment 2 "Перелік елементів")',
         '(comment 3 "Репета А. Н.")',
         '(comment 4 "Ляпандра А. С.")',
         '(comment 5 "")',
         '(comment 6 "")',
         ")", "(lib_symbols)"]
    for i, row in enumerate(rows):
        base = FRAME_T + HEADER_H + ROW_H * (i + 1) - 2.2   # базова лінія тексту в рядку
        if row[0] == "head":
            o.append(text(row[1], (xs[1] + xs[2]) / 2, base, just="bottom"))
            continue
        _, refs, name, qty, note = row
        o.append(text(refs, xs[0] + 1.5, base))
        o.append(text(name, xs[1] + 1.5, base))
        o.append(text(str(qty), (xs[2] + xs[3]) / 2, base, just="bottom"))
        if note:
            o.append(text(note, xs[3] + 1.5, base))
    o.append('(sheet_instances (path "/" (page "1")))')
    o.append("(embedded_fonts no)")
    o.append(")")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(o) + "\n")


def write_pro(path):
    pro = {"meta": {"filename": "pe3.kicad_pro", "version": 3},
           "schematic": {"page_layout_descr_file": "gost_form2_pe3.kicad_wks"},
           "sheets": [[SHEET_UUID, "Root"]]}
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(pro, fh, indent=2, ensure_ascii=False)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    rows = build_rows()
    write_wks(os.path.join(OUT, "gost_form2_pe3.kicad_wks"))
    write_sch(os.path.join(OUT, "pe3.kicad_sch"), rows)
    write_pro(os.path.join(OUT, "pe3.kicad_pro"))
    for r in rows:
        print(" | ".join(str(x) for x in r[1:]))
    print(f"рядків: {len(rows)} з {ROWS}")
