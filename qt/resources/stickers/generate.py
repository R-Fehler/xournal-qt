#!/usr/bin/env python3
"""Generate xournal-qt's built-in sticker collections (qt/docs/features/stickers.md, "Built in").

Each sticker is written as the app writes a sticker file: a gzipped .xopp of one page, as large as the content plus
6 points around it (stickers::MARGIN), plain white paper, the content moved so its top left is at (6, 6), in one
unnamed layer ("Layer 1"), its strokes one group (the attribute xqt-group, qt/docs/features/groups.md; Xournal++
ignores it). Black ink, 1.2 pt lines (labels 1.0 pt), geometry in millimetres. No preview picture is written (the
app draws the covers itself).

`collections.json` lists the collections in the picker's order. Each collection's folder also gets `names.json`: its title and every sticker's names in English and German (the
first name of a language is the one shown, the others are found by the picker's search too), in the order the
picker shows them. The drawings are our own work, CC0 (LICENCE.md).

Run it from anywhere: `python3 qt/resources/stickers/generate.py`. Python's standard library only; the output does
not depend on the machine (no time stamps: gzip mtime 0).
"""

import gzip
import hashlib
import json
import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PT = 72.0 / 25.4  # points per millimetre
MARGIN = 6.0  # points, stickers::MARGIN
LINE = 1.2  # points
LABEL = 1.0  # points
DASH = "cust: 3.00 2.00"  # hidden edges (points)


# --- strokes ------------------------------------------------------------------------------------------------------

class Stroke:
    def __init__(self, pts, width=LINE, dash=None, fill=False):
        self.pts = [(float(x), float(y)) for x, y in pts]
        self.width = width
        self.dash = dash
        self.fill = fill


class Sticker:
    def __init__(self):
        self.strokes = []

    def add(self, pts, width=LINE, dash=None, fill=False, closed=False):
        pts = list(pts)
        if closed and pts and pts[0] != pts[-1]:
            pts.append(pts[0])
        if len(pts) == 1:
            pts.append(pts[0])
        self.strokes.append(Stroke(pts, width, dash, fill))
        return self

    def line(self, *pts, **kw):
        return self.add(pts, **kw)

    def many(self, polylines, **kw):
        for p in polylines:
            self.add(p, **kw)
        return self

    def transformed(self, f):
        s = Sticker()
        for st in self.strokes:
            s.strokes.append(Stroke([f(p) for p in st.pts], st.width, st.dash, st.fill))
        return s


def arc(cx, cy, rx, ry, a0, a1, step=6.0):
    """Points of an elliptic arc (degrees, y down: 0 = right, 90 = down)."""
    n = max(2, int(math.ceil(abs(a1 - a0) / step)) + 1)
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / (n - 1))),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / (n - 1)))) for i in range(n)]


def circle(cx, cy, r, step=5.0):
    return arc(cx, cy, r, r, 0, 360, step)


def quad(p0, p1, p2, n=16):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]


def cubic(p0, p1, p2, p3, n=20):
    out = []
    for i in range(n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def spline(pts, n=8):
    """A Catmull-Rom curve through the points."""
    out = [pts[0]]
    p = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        for k in range(1, n + 1):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in (0, 1)))
    return out


def rect(x, y, w, h):
    return [(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]


def arrowhead(tip, frm, length=1.6, width=1.2):
    """A filled triangle with its tip at `tip`, pointing away from `frm`."""
    dx, dy = tip[0] - frm[0], tip[1] - frm[1]
    d = math.hypot(dx, dy)
    ux, uy = dx / d, dy / d
    bx, by = tip[0] - ux * length, tip[1] - uy * length
    return [tip, (bx - uy * width / 2, by + ux * width / 2), (bx + uy * width / 2, by - ux * width / 2), tip]


def arrow(s, frm, to, length=1.6, width=1.2, w=LINE):
    """A line from `frm` with a filled head at `to` (the line stops inside the head)."""
    dx, dy = to[0] - frm[0], to[1] - frm[1]
    d = math.hypot(dx, dy)
    stop = (to[0] - dx / d * length * 0.6, to[1] - dy / d * length * 0.6)
    s.line(frm, stop, width=w)
    s.add(arrowhead(to, frm, length, width), width=w * 0.6, fill=True)


# --- letters as strokes (in a box of height h, top left at x, y) ---------------------------------------------------

def glyph(ch, x, y, h):
    """The strokes of a character, its top left at (x, y), `h` high; returns (polylines, advance)."""
    def P(u, v):
        return (x + u * h, y + v * h)

    if ch == "A":
        return [[P(0, 1), P(0.36, 0), P(0.72, 1)], [P(0.13, 0.64), P(0.59, 0.64)]], 0.72
    if ch == "V":
        return [[P(0, 0), P(0.36, 1), P(0.72, 0)]], 0.72
    if ch == "1":
        return [[P(0.05, 0.22), P(0.32, 0), P(0.32, 1)]], 0.5
    if ch == "=":
        return [[P(0, 0.33), P(0.6, 0.33)], [P(0, 0.67), P(0.6, 0.67)]], 0.6
    if ch == "+":
        return [[P(0, 0.5), P(0.6, 0.5)], [P(0.3, 0.2), P(0.3, 0.8)]], 0.6
    if ch == "-":
        return [[P(0, 0.5), P(0.6, 0.5)]], 0.6
    if ch == ">":  # "≥"
        return [[P(0, 0.08), P(0.6, 0.38), P(0, 0.68)], [P(0, 0.9), P(0.6, 0.9)]], 0.6
    if ch == "&":
        pts = [(0.72, 1.0), (0.42, 0.62), (0.2, 0.35), (0.18, 0.15), (0.32, 0.0), (0.48, 0.06), (0.5, 0.22),
               (0.36, 0.38), (0.1, 0.58), (0.03, 0.8), (0.16, 0.98), (0.36, 1.0), (0.56, 0.86), (0.7, 0.6)]
        return [[P(u, v) for u, v in spline(pts, 6)]], 0.75
    if ch == "~":  # "∞"
        pts = []
        for i in range(73):
            t = 2 * math.pi * i / 72
            d = 1 + math.sin(t) ** 2
            pts.append(P(0.45 + 0.45 * math.cos(t) / d, 0.5 + 0.5 * math.sin(t) * math.cos(t) / d))
        return [pts], 0.9
    if ch == "|":  # "▷"
        return [[P(0, 0), P(0.7, 0.5), P(0, 1), P(0, 0)]], 0.7
    raise ValueError(ch)


def text(s, string, x, y, h, w=LABEL, centre=False, gap=0.18):
    """Characters as strokes: the box's top left at (x, y) (centre: its middle at x, y)."""
    advance = 0
    for ch in string:
        advance += glyph(ch, 0, 0, h)[1] * h + gap * h
    advance -= gap * h
    if centre:
        x -= advance / 2
        y -= h / 2
    for ch in string:
        lines, a = glyph(ch, x, y, h)
        s.many(lines, width=w)
        x += a * h + gap * h
    return s


# --- circuit symbols (IEC 60617 style), leads along x ---------------------------------------------------------------

LEAD = 4.0  # mm of wire at each end


def two_pole(body_w, draw, lead=LEAD, h=0):
    """A symbol between two leads: the leads, then `draw(s, x0)` draws the body from x0 to x0 + body_w at y = 0."""
    s = Sticker()
    s.line((0, 0), (lead, 0))
    s.line((lead + body_w, 0), (2 * lead + body_w, 0))
    draw(s, lead)
    return s


def resistor():
    return two_pole(7.0, lambda s, x: s.add(rect(x, -1.4, 7.0, 2.8)))


def variable_resistor():
    s = resistor()
    arrow(s, (LEAD - 0.8, 3.0), (LEAD + 7.8, -3.4), 1.6, 1.2)
    return s


def potentiometer():
    s = resistor()
    mid = LEAD + 3.5
    s.line((mid, -5.5), (mid + 3.0, -5.5))
    arrow(s, (mid, -5.5), (mid, -1.4), 1.6, 1.2)
    return s


def capacitor():
    def body(s, x):
        s.line((x, -3.0), (x, 3.0))
        s.line((x + 1.6, -3.0), (x + 1.6, 3.0))
    return two_pole(1.6, body, LEAD + 1)


def polarised_capacitor():
    def body(s, x):
        s.add(rect(x - 0.7, -3.0, 0.7, 6.0))       # +: hollow
        s.add(rect(x + 1.6, -3.0, 0.7, 6.0), fill=True)  # -: filled
        text(s, "+", x - 2.4, -4.6, 1.8, centre=True)
    s = Sticker()
    x = LEAD + 1.7
    s.line((0, 0), (x - 0.7, 0))
    s.line((x + 2.3, 0), (x + 2.3 + LEAD + 1.0, 0))
    body(s, x)
    return s


def inductor():
    def body(s, x):
        pts = []
        for i in range(4):
            pts += arc(x + 1.0 + 2.0 * i, 0, 1.0, 1.0, 180, 360, 10)[(1 if i else 0):]
        s.add(pts)
    return two_pole(8.0, body)


def diode_body(s, x, size=3.4):
    s.add([(x, -size / 2), (x + size * 0.87, 0), (x, size / 2), (x, -size / 2)])
    s.line((x + size * 0.87, -size / 2), (x + size * 0.87, size / 2))


def diode():
    s = Sticker()
    s.line((0, 0), (LEAD + 1 + 2.96, 0))
    s.line((LEAD + 1 + 2.96, 0), (2 * LEAD + 2 + 2.96, 0))
    diode_body(s, LEAD + 1)
    return s


def zener_diode():
    s = Sticker()
    x = LEAD + 1
    s.line((0, 0), (2 * LEAD + 2 + 2.96, 0))
    s.add([(x, -1.7), (x + 2.96, 0), (x, 1.7), (x, -1.7)])
    s.line((x + 2.96 - 0.9, 1.7), (x + 2.96, 1.7), (x + 2.96, -1.7), (x + 2.96 + 0.9, -1.7))
    return s


def led(inward=False):
    s = diode()
    x = LEAD + 1
    for k in (0, 1.4):
        a, b = (x + 1.2 + k, -2.2), (x + 3.0 + k, -4.4)
        if inward:
            a, b = (x + 3.6 + k, -5.0), (x + 1.8 + k, -2.8)
        arrow(s, a, b, 1.2, 1.0, LABEL)
    return s


def transistor(npn=True):
    s = Sticker()
    cx, cy, r = 6.5, 0.0, 4.2
    s.add(circle(cx, cy, r))
    bx = cx - 1.4
    s.line((0, 0), (bx, 0))                     # base lead
    s.line((bx, -2.2), (bx, 2.2), width=LINE * 1.6)  # base bar
    top = (cx + 1.6, -math.sqrt(r * r - 1.6 * 1.6))
    bot = (cx + 1.6, math.sqrt(r * r - 1.6 * 1.6))
    s.line((bx, -1.0), top)
    s.line(top, (top[0], -r - LEAD))            # collector
    s.line(bot, (bot[0], r + LEAD))             # emitter
    if npn:
        s.line((bx, 1.0), bot)
        mid = (bx + (bot[0] - bx) * 0.72, 1.0 + (bot[1] - 1.0) * 0.72)
        s.add(arrowhead(mid, (bx, 1.0), 1.6, 1.3), width=LINE * 0.6, fill=True)
    else:
        s.line((bx, 1.0), bot)
        mid = (bx + (bot[0] - bx) * 0.3, 1.0 + (bot[1] - 1.0) * 0.3)
        s.add(arrowhead(mid, bot, 1.6, 1.3), width=LINE * 0.6, fill=True)
    return s


def cell_plates(s, x):
    s.line((x, -3.2), (x, 3.2))                         # +: long, thin
    s.line((x + 1.6, -1.6), (x + 1.6, 1.6), width=LINE * 2.2)  # -: short, thick


def cell():
    s = Sticker()
    x = LEAD + 1
    s.line((0, 0), (x, 0))
    s.line((x + 1.6, 0), (x + 1.6 + LEAD + 1, 0))
    cell_plates(s, x)
    return s


def battery():
    s = Sticker()
    x = LEAD + 1
    s.line((0, 0), (x, 0))
    cell_plates(s, x)
    s.line((x + 2.4, 0), (x + 5.2, 0), dash="cust: 1.50 1.50")
    cell_plates(s, x + 6.0)
    s.line((x + 7.6, 0), (x + 7.6 + LEAD + 1, 0))
    return s


def source(inside):
    def body(s, x):
        s.add(circle(x + 3.0, 0, 3.0))
        inside(s, x + 3.0)
    return two_pole(6.0, body)


def dc_source():
    return source(lambda s, c: s.line((c - 3.0, 0), (c + 3.0, 0)))


def ac_source():
    def sine(s, c):
        s.add([(c - 1.8 + 3.6 * i / 32, -0.9 * math.sin(2 * math.pi * i / 32)) for i in range(33)])
    return source(sine)


def current_source():
    return source(lambda s, c: s.line((c, -3.0), (c, 3.0)))


def switch_contact(s, x):
    s.line((0, 0), (x, 0))
    s.line((x + 6.0, 0), (x + 6.0 + LEAD, 0))
    s.line((x, 0), (x + 6.3, -2.6))


def switch_open():
    s = Sticker()
    switch_contact(s, LEAD)
    return s


def push_button():
    s = Sticker()
    switch_contact(s, LEAD)
    mid = (LEAD + 3.15, -1.3)
    s.line(mid, (mid[0], -5.5), dash="cust: 1.20 1.00")
    s.line((mid[0] - 1.6, -4.5), (mid[0] - 1.6, -5.5), (mid[0] + 1.6, -5.5), (mid[0] + 1.6, -4.5))
    return s


def lamp():
    def cross(s, c):
        d = 3.0 / math.sqrt(2)
        s.line((c - d, -d), (c + d, d))
        s.line((c - d, d), (c + d, -d))
    return source(cross)


def meter(letter):
    return source(lambda s, c: text(s, letter, c, 0, 2.8, centre=True, gap=0))


def ground():
    s = Sticker()
    s.line((0, 0), (0, LEAD + 1))
    for i, half in enumerate((3.0, 2.0, 1.0)):
        s.line((-half, LEAD + 1 + i * 1.0), (half, LEAD + 1 + i * 1.0))
    return s


def opamp():
    """IEC: a box with ▷ ∞, the inputs − and + at the left, the output at the right."""
    s = Sticker()
    x, w, h = LEAD, 8.0, 10.0
    s.add(rect(x, -h / 2, w, h))
    s.line((0, -2.5), (x, -2.5))
    s.line((0, 2.5), (x, 2.5))
    s.line((x + w, 0), (x + w + LEAD, 0))
    text(s, "-", x + 0.8, -2.5 - 0.9, 1.8)
    text(s, "+", x + 0.8, 2.5 - 0.9, 1.8)
    text(s, "|", x + 3.4, -h / 2 + 0.9, 2.0)
    text(s, "~", x + 5.0, -h / 2 + 0.9, 2.0)
    return s


def opamp_triangle():
    s = Sticker()
    x, w, h = LEAD, 9.0, 10.0
    s.add([(x, -h / 2), (x + w, 0), (x, h / 2), (x, -h / 2)])
    s.line((0, -2.5), (x, -2.5))
    s.line((0, 2.5), (x, 2.5))
    s.line((x + w, 0), (x + w + LEAD, 0))
    text(s, "-", x + 0.7, -2.5 - 0.9, 1.8)
    text(s, "+", x + 0.7, 2.5 - 0.9, 1.8)
    return s


def fuse():
    def body(s, x):
        s.add(rect(x, -1.4, 7.0, 2.8))
        s.line((x, 0), (x + 7.0, 0))
    return two_pole(7.0, body)


CIRCUITS = [
    ("Resistor", resistor, ["Resistor"], ["Widerstand"]),
    ("Variable resistor", variable_resistor, ["Variable resistor", "Rheostat"],
     ["Einstellbarer Widerstand", "Veränderbarer Widerstand"]),
    ("Potentiometer", potentiometer, ["Potentiometer"], ["Potentiometer", "Poti"]),
    ("Capacitor", capacitor, ["Capacitor", "Condenser"], ["Kondensator"]),
    ("Polarised capacitor", polarised_capacitor, ["Polarised capacitor", "Electrolytic capacitor", "Polarized capacitor"],
     ["Gepolter Kondensator", "Elektrolytkondensator", "Elko"]),
    ("Inductor", inductor, ["Inductor", "Coil"], ["Spule", "Induktivität"]),
    ("Diode", diode, ["Diode"], ["Diode"]),
    ("Zener diode", zener_diode, ["Zener diode"], ["Z-Diode", "Zenerdiode"]),
    ("LED", led, ["LED", "Light-emitting diode"], ["Leuchtdiode", "LED"]),
    ("Photodiode", lambda: led(True), ["Photodiode"], ["Fotodiode", "Photodiode"]),
    ("NPN transistor", transistor, ["NPN transistor"], ["NPN-Transistor"]),
    ("PNP transistor", lambda: transistor(False), ["PNP transistor"], ["PNP-Transistor"]),
    ("Cell", cell, ["Cell"], ["Zelle", "Galvanisches Element", "Batteriezelle"]),
    ("Battery", battery, ["Battery"], ["Batterie"]),
    ("DC voltage source", dc_source, ["DC voltage source", "Voltage source"],
     ["Gleichspannungsquelle", "Spannungsquelle"]),
    ("AC source", ac_source, ["AC source", "AC voltage source"], ["Wechselspannungsquelle"]),
    ("Current source", current_source, ["Current source"], ["Stromquelle"]),
    ("Switch (open)", switch_open, ["Switch (open)", "Switch"], ["Schalter (offen)", "Schalter", "Schließer"]),
    ("Push button", push_button, ["Push button"], ["Taster"]),
    ("Lamp", lamp, ["Lamp", "Bulb"], ["Lampe", "Glühlampe"]),
    ("Ammeter", lambda: meter("A"), ["Ammeter"], ["Strommesser", "Amperemeter"]),
    ("Voltmeter", lambda: meter("V"), ["Voltmeter"], ["Spannungsmesser", "Voltmeter"]),
    ("Ground", ground, ["Ground", "Earth"], ["Erde", "Masse", "Erdung"]),
    ("Op-amp", opamp, ["Op-amp", "Operational amplifier"], ["Operationsverstärker", "OPV", "OP-Amp"]),
    ("Op-amp (triangle)", opamp_triangle, ["Op-amp (triangle)", "Operational amplifier"],
     ["Operationsverstärker (Dreieck)", "OPV"]),
    ("Fuse", fuse, ["Fuse"], ["Sicherung", "Schmelzsicherung"]),
]


# --- logic gates ----------------------------------------------------------------------------------------------------

GW, GH = 9.0, 8.0  # the body
GIN, GOUT = 3.5, 3.5  # leads
BUBBLE = 0.8  # radius of a negation circle


def gate_leads(s, back_x, out_x, inputs=2):
    """Input leads to x = back_x(y) (a function), the output lead from out_x."""
    ys = [GH / 2] if inputs == 1 else [GH * 0.27, GH * 0.73]
    for y in ys:
        s.line((0, y), (GIN + back_x(y), y))
    s.line((out_x, GH / 2), (GIN + GW + (2 * BUBBLE if inputs >= 0 else 0) + GOUT, GH / 2))


def ansi_gate(kind):
    s = Sticker()
    x0 = GIN
    neg = kind in ("NAND", "NOR", "XNOR", "NOT")
    if kind in ("AND", "NAND"):
        body = [(x0 + GW * 0.5, 0), (x0, 0), (x0, GH), (x0 + GW * 0.5, GH)] + \
            arc(x0 + GW * 0.5, GH / 2, GW * 0.5, GH / 2, 90, -90, 5)
        s.add(body)
        back = lambda y: 0.0
        tip = x0 + GW
    elif kind == "NOT":
        s.add([(x0, 0.6), (x0 + GW * 0.8, GH / 2), (x0, GH - 0.6), (x0, 0.6)])
        back = lambda y: 0.0
        tip = x0 + GW * 0.8
    else:  # OR, NOR, XOR, XNOR
        depth = GW * 0.28

        def back(y):
            t = y / GH  # on the quadratic back curve: x = 2 (1 - t) t depth
            return 2 * (1 - t) * t * depth

        top = quad((x0, 0), (x0 + GW * 0.62, 0), (x0 + GW, GH / 2), 20)
        bottom = quad((x0 + GW, GH / 2), (x0 + GW * 0.62, GH), (x0, GH), 20)
        backc = quad((x0, GH), (x0 + depth, GH / 2), (x0, 0), 16)
        s.add(top + bottom[1:] + backc[1:])
        tip = x0 + GW
        if kind in ("XOR", "XNOR"):
            s.add(quad((x0 - 1.4, GH), (x0 - 1.4 + depth, GH / 2), (x0 - 1.4, 0), 16))
            b = back
            back = lambda y: b(y) - 1.4
    out = tip
    if neg:
        s.add(circle(tip + BUBBLE, GH / 2, BUBBLE, 15))
        out = tip + 2 * BUBBLE
    inputs = 1 if kind == "NOT" else 2
    ys = [GH / 2] if inputs == 1 else [GH * 0.27, GH * 0.73]
    for y in ys:
        s.line((0, y), (x0 + back(y), y))
    s.line((out, GH / 2), (out + GOUT, GH / 2))
    return s


def iec_gate(label, neg, inputs=2):
    s = Sticker()
    x0, w, h = GIN, 6.0, 9.0
    s.add(rect(x0, 0, w, h))
    ys = [h / 2] if inputs == 1 else [h * 0.25, h * 0.75]
    for y in ys:
        s.line((0, y), (x0, y))
    out = x0 + w
    if neg:
        s.add(circle(out + BUBBLE, h / 2, BUBBLE, 15))
        out += 2 * BUBBLE
    s.line((out, h / 2), (out + GOUT, h / 2))
    text(s, label, x0 + w / 2, 2.2, 2.4, centre=True, gap=0.15)
    return s


GATES = [
    ("AND", ["AND", "AND gate"], ["UND", "UND-Gatter", "AND-Gatter"]),
    ("OR", ["OR", "OR gate"], ["ODER", "ODER-Gatter", "OR-Gatter"]),
    ("NOT", ["NOT", "NOT gate", "Inverter"], ["NICHT", "NICHT-Gatter", "NOT-Gatter", "Inverter"]),
    ("NAND", ["NAND", "NAND gate"], ["NAND", "NAND-Gatter", "NICHT-UND"]),
    ("NOR", ["NOR", "NOR gate"], ["NOR", "NOR-Gatter", "NICHT-ODER"]),
    ("XOR", ["XOR", "XOR gate", "Exclusive OR"], ["XOR", "XOR-Gatter", "Exklusiv-ODER", "Antivalenz"]),
    ("XNOR", ["XNOR", "XNOR gate", "Exclusive NOR"], ["XNOR", "XNOR-Gatter", "Äquivalenz"]),
]
IEC_LABELS = {"AND": ("&", False), "OR": (">1", False), "NOT": ("1", True), "NAND": ("&", True),
              "NOR": (">1", True), "XOR": ("=1", False), "XNOR": ("=1", True)}


# --- 3D solids: oblique (cabinet) projection, hidden edges dashed ---------------------------------------------------

K = 0.5  # depth shortened to half
ANG = math.radians(45)


def project(p):
    """x right, y up, z into the page (mm) -> the page (y down)."""
    x, y, z = p
    return (x + K * z * math.cos(ANG), -y - K * z * math.sin(ANG))


def area2(poly):
    return sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
               for i in range(len(poly)))


def polyhedron(vertices, faces):
    """Faces: vertex indices, counter-clockwise seen from outside (x right, y up). An edge is drawn solid when a
    face it bounds faces the viewer, dashed when both are turned away."""
    s = Sticker()
    proj = [project(v) for v in vertices]
    facing = []
    for f in faces:
        # (the page's y is down: a face seen from outside, counter-clockwise in x/y up, is clockwise on the page)
        facing.append(area2([proj[i] for i in f]) < 0)
    edges = {}
    for fi, f in enumerate(faces):
        for i in range(len(f)):
            e = tuple(sorted((f[i], f[(i + 1) % len(f)])))
            edges.setdefault(e, []).append(fi)
    hidden, shown = [], []
    for e, fs in sorted(edges.items()):
        (shown if any(facing[fi] for fi in fs) else hidden).append(e)
    for a, b in hidden:
        s.line(proj[a], proj[b], dash=DASH)
    for a, b in shown:
        s.line(proj[a], proj[b])
    return s


def box(w, h, d):
    v = [(0, 0, 0), (w, 0, 0), (w, h, 0), (0, h, 0), (0, 0, d), (w, 0, d), (w, h, d), (0, h, d)]
    faces = [[0, 1, 2, 3], [5, 4, 7, 6], [4, 0, 3, 7], [1, 5, 6, 2], [3, 2, 6, 7], [4, 5, 1, 0]]
    return polyhedron(v, faces)


def square_pyramid(a=22.0, h=24.0):
    v = [(0, 0, 0), (a, 0, 0), (a, 0, a), (0, 0, a), (a / 2, h, a / 2)]
    faces = [[0, 3, 2, 1], [0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]]
    return polyhedron(v, faces)


def triangular_prism(a=22.0, h=16.0, d=26.0):
    v = [(0, 0, 0), (a, 0, 0), (a / 2, h, 0), (0, 0, d), (a, 0, d), (a / 2, h, d)]
    faces = [[0, 1, 2], [3, 5, 4], [0, 3, 4, 1], [1, 4, 5, 2], [2, 5, 3, 0]]
    return polyhedron(v, faces)


def tetrahedron(a=28.0, turn=-30.0):
    h_base = a * math.sqrt(3) / 2
    # The base in the ground plane, a corner at the back, turned about the vertical so that corner is not hidden
    # behind an edge; the apex above the base's centre
    v = [(0, 0, 0), (a, 0, 0), (a / 2, 0, h_base), (a / 2, a * math.sqrt(2 / 3), h_base / 3)]
    c, sn = math.cos(math.radians(turn)), math.sin(math.radians(turn))
    v = [(x * c - z * sn, y, x * sn + z * c) for x, y, z in v]
    faces = [[0, 2, 1], [0, 1, 3], [1, 2, 3], [2, 0, 3]]
    return polyhedron(v, faces)


def ellipse_split(s, cx, cy, rx, ry, a_from, a_to):
    """An ellipse: solid from a_from to a_to (degrees, the front), dashed for the rest (the back)."""
    s.add(arc(cx, cy, rx, ry, a_from, a_to, 4))
    s.add(arc(cx, cy, rx, ry, a_to, a_from + 360, 4), dash=DASH)


def cylinder(r=10.0, h=24.0):
    s = Sticker()
    ry = r * 0.35
    s.add(arc(r, ry, r, ry, 0, 360, 4))                # the top, whole
    ellipse_split(s, r, ry + h, r, ry, 0, 180)          # the bottom: its back half hidden
    s.line((0, ry), (0, ry + h))
    s.line((2 * r, ry), (2 * r, ry + h))
    return s


def cone(r=11.0, h=26.0):
    s = Sticker()
    ry = r * 0.35
    cy = h  # the base's centre, the apex at (r, 0)
    # The tangents from the apex to the ellipse touch it at y = cy - ry^2 / h
    yt = -ry * ry / h
    xt = r * math.sqrt(1 - (yt / ry) ** 2)
    a = math.degrees(math.atan2(yt / ry, xt / r))  # (negative: just above the middle)
    ellipse_split(s, r, cy, r, ry, a, 180 - a)
    s.line((r, 0), (r + xt, cy + yt))
    s.line((r, 0), (r - xt, cy + yt))
    return s


def sphere(r=12.0):
    s = Sticker()
    s.add(circle(r, r, r, 3))
    ellipse_split(s, r, r, r, r * 0.3, 0, 180)
    return s


SOLIDS = [
    ("Cube", lambda: box(20, 20, 20), ["Cube"], ["Würfel"]),
    ("Cuboid", lambda: box(28, 14, 18), ["Cuboid", "Rectangular box", "Rectangular prism"], ["Quader"]),
    ("Cylinder", cylinder, ["Cylinder"], ["Zylinder"]),
    ("Cone", cone, ["Cone"], ["Kegel"]),
    ("Sphere", sphere, ["Sphere", "Ball"], ["Kugel"]),
    ("Square pyramid", square_pyramid, ["Square pyramid", "Pyramid"], ["Quadratische Pyramide", "Pyramide"]),
    ("Triangular prism", triangular_prism, ["Triangular prism", "Prism"], ["Dreiecksprisma", "Prisma"]),
    ("Tetrahedron", tetrahedron, ["Tetrahedron", "Triangular pyramid"], ["Tetraeder", "Dreieckspyramide"]),
]


# --- lab glassware and benzene ----------------------------------------------------------------------------------------

def mirrored(right):
    """A symmetric outline from its right half (top to bottom, x from the axis): the left half, then the right."""
    left = [(-x, y) for x, y in reversed(right)]
    return left + right


def graduations(s, x, ys, length=2.0, w=LABEL):
    for i, y in enumerate(ys):
        s.line((x, y), (x + (length if i % 2 == 0 else length * 0.6), y), width=w)


def beaker():
    s = Sticker()
    w, h, r = 20.0, 24.0, 1.6
    right = [(w / 2 + 0.6, 0), (w / 2, 0.8)] + arc(w / 2 - r, h - r, r, r, 0, 90, 10) + [(0, h)]
    left = [(-w / 2 - 1.8, -0.9), (-w / 2 - 0.4, 0.2), (-w / 2, 1.4)] + \
        arc(-w / 2 + r, h - r, r, r, 180, 90, 10) + [(0, h)]
    s.add(left)
    s.add(right)
    graduations(s, -w / 2 + 2.0, [6.0 + 3.0 * i for i in range(5)])
    return s


def erlenmeyer():
    s = Sticker()
    neck, top, h, base = 3.2, 9.0, 30.0, 12.0
    # the lip, the neck, the cone down to a rounded corner, the bottom
    t = 1.6 / math.hypot(base - neck, h - top)  # (where the side ends: 1.6 mm before the corner)
    side_end = (base - (base - neck) * t, h - (h - top) * t)
    right = [(neck + 0.7, 0), (neck, 0.6), (neck, top), side_end] + quad(side_end, (base, h), (base - 1.8, h), 8)[1:] + \
        [(0, h)]
    s.add(mirrored(right))
    graduations(s, -6.0, [h - 6.0, h - 9.0, h - 12.0], 2.0)
    return s


def round_flask():
    s = Sticker()
    neck, nl, R = 2.8, 10.0, 11.0
    cy = nl + math.sqrt(R * R - neck * neck)
    a0 = math.degrees(math.atan2(nl - cy, neck))  # where the neck meets the ball
    right = [(neck + 0.7, 0), (neck, 0.6), (neck, nl)] + arc(0, cy, R, R, a0, 90, 3)
    s.add(mirrored(right))
    return s


def test_tube():
    s = Sticker()
    w, h = 6.0, 34.0
    right = [(w / 2 + 0.7, 0), (w / 2, 0.6)] + arc(0, h - w / 2, w / 2, w / 2, 0, 90, 6)
    s.add(mirrored(right))
    return s


def burette():
    s = Sticker()
    w, h = 4.0, 48.0
    s.add([(-w / 2, 0), (-w / 2, h)])
    s.add([(w / 2, 0), (w / 2, h)])
    graduations(s, -w / 2, [4.0 + 3.0 * i for i in range(14)], 1.6)
    # the stopcock: a short barrel with its handle, then the tip
    s.add([(-w / 2, h), (-0.8, h + 1.6), (-0.8, h + 3.0)])
    s.add([(w / 2, h), (0.8, h + 1.6), (0.8, h + 3.0)])
    s.add(rect(-1.8, h + 3.0, 3.6, 2.4))
    s.add(rect(-6.2, h + 3.6, 4.4, 1.2))
    s.add([(-0.8, h + 5.4), (-0.8, h + 6.4), (-0.3, h + 11.0)])
    s.add([(0.8, h + 5.4), (0.8, h + 6.4), (0.3, h + 11.0)])
    return s


def pipette():
    s = Sticker()
    t, bw, top, b0, b1, h = 0.9, 4.2, 0.0, 18.0, 32.0, 54.0
    right = [(t, top), (t, b0 - 1.5)] + \
        spline([(t, b0 - 1.5), (bw * 0.7, b0 + 0.8), (bw, b0 + 4.0), (bw, b1 - 4.0), (bw * 0.7, b1 - 0.8),
                (t, b1 + 1.5)], 8)[1:] + [(t, h - 6.0), (0.25, h)]
    s.add([(-x, y) for x, y in right])
    s.add(right)
    s.line((-1.6, 10.0), (1.6, 10.0), width=LABEL)  # the mark
    return s


def measuring_cylinder():
    s = Sticker()
    w, h = 10.0, 40.0
    s.add([(-w / 2 - 1.4, -0.6), (-w / 2, 0.8), (-w / 2, h)])
    s.add([(w / 2 + 0.5, 0), (w / 2, 0.6), (w / 2, h)])
    # the foot
    s.add([(-w / 2, h), (-w / 2 - 3.0, h + 0.6), (-w / 2 - 3.0, h + 2.0), (w / 2 + 3.0, h + 2.0),
           (w / 2 + 3.0, h + 0.6), (w / 2, h)])
    graduations(s, -w / 2, [5.0 + 2.5 * i for i in range(14)], 2.4)
    return s


def bunsen_burner():
    s = Sticker()
    tw, top, collar, foot_top, h = 2.6, 0.0, 16.0, 26.0, 29.0
    # the tube, the air-hole collar, the foot
    s.add([(-tw, top), (-tw, collar)])
    s.add([(tw, top), (tw, collar)])
    s.add(rect(-tw - 0.8, collar, 2 * tw + 1.6, 5.0))
    s.add(rect(-1.2, collar + 1.6, 2.4, 1.8))  # the air hole
    s.add([(-tw, collar + 5.0), (-tw, foot_top)])
    s.add([(tw, collar + 5.0), (tw, foot_top)])
    s.add([(-tw, foot_top), (-9.0, h - 1.2), (-9.0, h), (9.0, h), (9.0, h - 1.2), (tw, foot_top)])
    # the gas inlet at the side
    s.add([(tw, foot_top - 3.2), (tw + 7.0, foot_top - 3.2)])
    s.add([(tw, foot_top - 1.4), (tw + 7.0, foot_top - 1.4)])
    return s


def condenser():
    """A Liebig condenser, lying: the inner tube through the jacket, water in at the bottom right, out at the top
    left."""
    s = Sticker()
    L, jl, jr = 52.0, 8.0, 44.0
    ti, jo = 1.5, 4.5  # half widths: the inner tube, the jacket
    s.line((0, -ti), (L, -ti))
    s.line((0, ti), (L, ti))
    # the jacket, closed at both ends around the tube
    s.add([(jl, -ti), (jl, -jo), (jl + 6.0, -jo)])
    s.add([(jl + 9.0, -jo), (jr, -jo), (jr, -ti)])
    s.add([(jl, ti), (jl, jo), (jr - 9.0, jo)])
    s.add([(jr - 6.0, jo), (jr, jo), (jr, ti)])
    # the ports
    s.add([(jl + 6.0, -jo), (jl + 6.0, -jo - 5.0)])
    s.add([(jl + 9.0, -jo), (jl + 9.0, -jo - 5.0)])
    s.add([(jr - 9.0, jo), (jr - 9.0, jo + 5.0)])
    s.add([(jr - 6.0, jo), (jr - 6.0, jo + 5.0)])
    return s


def funnel():
    s = Sticker()
    w, cone_h, stem_w, stem = 24.0, 14.0, 1.4, 16.0
    right = [(w / 2 + 0.5, -0.5), (w / 2, 0), (stem_w, cone_h), (stem_w, cone_h + stem - 1.0), (stem_w * 0.4, cone_h + stem)]
    s.add([(-x, y) for x, y in right][1:4])
    s.add([(-stem_w, cone_h + stem - 1.0), (-stem_w * 0.4, cone_h + stem)])
    s.add(right[1:])
    s.add([(-w / 2 - 0.5, -0.5), (-w / 2, 0)])
    return s


def hexagon(r):
    return [(r * math.cos(math.radians(90 + 60 * i)), r * math.sin(math.radians(90 + 60 * i))) for i in range(7)]


def benzene_kekule(r=7.0):
    s = Sticker()
    hx = hexagon(r)
    s.add(hx)
    ri = r * 0.74
    for i in (0, 2, 4):
        a, b = hx[i], hx[i + 1]
        # a line parallel to the side, inside, a little shorter
        ca = (a[0] * ri / r, a[1] * ri / r)
        cb = (b[0] * ri / r, b[1] * ri / r)
        sh = 0.14
        s.line((ca[0] + (cb[0] - ca[0]) * sh, ca[1] + (cb[1] - ca[1]) * sh),
               (cb[0] + (ca[0] - cb[0]) * sh, cb[1] + (ca[1] - cb[1]) * sh))
    return s


def benzene_circle(r=7.0):
    s = Sticker()
    s.add(hexagon(r))
    s.add(circle(0, 0, r * 0.55, 4))
    return s


LAB = [
    ("Beaker", beaker, ["Beaker"], ["Becherglas"]),
    ("Erlenmeyer flask", erlenmeyer, ["Erlenmeyer flask", "Conical flask"], ["Erlenmeyerkolben"]),
    ("Round-bottom flask", round_flask, ["Round-bottom flask", "Florence flask"], ["Rundkolben"]),
    ("Test tube", test_tube, ["Test tube"], ["Reagenzglas"]),
    ("Burette", burette, ["Burette", "Buret"], ["Bürette"]),
    ("Pipette", pipette, ["Pipette", "Volumetric pipette", "Pipet"], ["Pipette", "Vollpipette"]),
    ("Measuring cylinder", measuring_cylinder, ["Measuring cylinder", "Graduated cylinder"],
     ["Messzylinder"]),
    ("Bunsen burner", bunsen_burner, ["Bunsen burner", "Burner"], ["Bunsenbrenner", "Brenner"]),
    ("Condenser", condenser, ["Condenser", "Liebig condenser"], ["Kühler", "Liebigkühler"]),
    ("Funnel", funnel, ["Funnel"], ["Trichter"]),
    ("Benzene (Kekule)", benzene_kekule, ["Benzene (Kekulé)", "Benzene", "Benzene ring", "Kekule"],
     ["Benzol (Kekulé)", "Benzol", "Benzolring"]),
    ("Benzene (circle)", benzene_circle, ["Benzene (circle)", "Benzene", "Benzene ring", "Aromatic ring"],
     ["Benzol (Kreis)", "Benzol", "Benzolring", "Aromat"]),
]


# --- writing ---------------------------------------------------------------------------------------------------------

def num(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def xopp(sticker):
    """The sticker's file, as StickerFile::makeDocument and SaveHandler make it."""
    pts_bounds = None
    for st in sticker.strokes:
        half = st.width / 2
        for x, y in st.pts:
            x, y = x * PT, y * PT
            b = (x - half, y - half, x + half, y + half)
            pts_bounds = b if pts_bounds is None else (min(pts_bounds[0], b[0]), min(pts_bounds[1], b[1]),
                                                      max(pts_bounds[2], b[2]), max(pts_bounds[3], b[3]))
    dx, dy = MARGIN - pts_bounds[0], MARGIN - pts_bounds[1]
    width = (pts_bounds[2] - pts_bounds[0]) + 2 * MARGIN
    height = (pts_bounds[3] - pts_bounds[1]) + 2 * MARGIN
    out = ['<?xml version="1.0" standalone="no"?>\n',
           '<xournal creator="xournal-qt" fileversion="4">\n',
           '<title>Xournal++ document - see </title>\n',
           '<page width="%s" height="%s">\n' % (num(width), num(height)),
           '<background type="solid" color="#ffffffff" style="plain"/>\n',
           '<layer>\n']
    for st in sticker.strokes:
        attrs = 'xqt-group="1" tool="pen" color="#000000ff" width="%s"' % num(st.width)
        if st.fill:
            attrs += ' fill="255"'
        attrs += ' capStyle="round"'
        if st.dash:
            attrs += ' style="%s"' % st.dash
        coords = " ".join("%s %s" % (num(x * PT + dx), num(y * PT + dy)) for x, y in st.pts)
        out.append("<stroke %s>%s</stroke>\n" % (attrs, coords))
    out.append("</layer>\n</page>\n</xournal>\n")
    return "".join(out).encode("utf-8"), (width, height)


COLLECTIONS = [
    ("circuits-iec", {"en": "Circuit symbols (IEC)", "de": "Schaltzeichen (IEC)"},
     [(name, f, en, de) for name, f, en, de in CIRCUITS]),
    ("logic-gates", {"en": "Logic gates", "de": "Logikgatter"},
     [(k, (lambda k=k: ansi_gate(k)), en, de) for k, en, de in GATES]),
    ("logic-gates-iec", {"en": "Logic gates (IEC)", "de": "Logikgatter (IEC)"},
     [(k + " (IEC)", (lambda k=k: iec_gate(*IEC_LABELS[k], inputs=1 if k == "NOT" else 2)),
       [n + " (IEC)" for n in en[:1]] + en, [n + " (IEC)" for n in de[:1]] + de) for k, en, de in GATES]),
    ("solids", {"en": "3D solids", "de": "Körper (3D)"}, SOLIDS),
    ("lab", {"en": "Lab glassware", "de": "Laborgeräte"}, LAB),
]


def write(out_dir):
    sizes = {}
    digest = hashlib.sha256()
    for cid, title, items in COLLECTIONS:
        folder = os.path.join(out_dir, cid)
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(folder)
        names = {"title": title, "stickers": []}
        for name, make, en, de in items:
            data, size = xopp(make())
            with open(os.path.join(folder, name + ".xopp"), "wb") as f:
                f.write(gzip.compress(data, mtime=0))
            digest.update(cid.encode() + b"/" + name.encode() + b"\0" + data)
            names["stickers"].append({"file": name + ".xopp", "en": en, "de": de})
            sizes[cid + "/" + name] = size
        with open(os.path.join(folder, "names.json"), "w", encoding="utf-8") as f:
            # (one line per sticker)
            f.write('{\n "title": %s,\n "stickers": [\n  ' % json.dumps(title, ensure_ascii=False))
            f.write(",\n  ".join(json.dumps(e, ensure_ascii=False) for e in names["stickers"]))
            f.write("\n ]\n}\n")
        digest.update(json.dumps(names, ensure_ascii=False, sort_keys=True).encode())
    # The collections in the picker's order, and a hash of everything (an Android install copies the set again when
    # it changes: AndroidSetup.cpp)
    with open(os.path.join(out_dir, "collections.json"), "w", encoding="utf-8") as f:
        f.write('{\n "order": %s,\n "sha256": "%s"\n}\n'
                % (json.dumps([cid for cid, _, _ in COLLECTIONS]), digest.hexdigest()))
    return sizes


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else HERE
    for key, (w, h) in sorted(write(out).items()):
        print("%-40s %6.1f x %5.1f mm" % (key, w / PT, h / PT))
