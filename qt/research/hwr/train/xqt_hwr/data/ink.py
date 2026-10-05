"""xournal-qt ink: lines with their pen strokes (FORMATS.md, `strokes/<id>.json`), drawn as the app draws them for its
recogniser (qt/src/hwr/LineImage.cpp), and varied for training (slant, width, jitter) before drawing.

Strokes: {"width": w, "height": h, "strokes": [{"points": [[x, y, pressure], ...], "width": pt}]} in points relative
to the line's top-left. A segment is drawn `width * pressure` wide (pressure 1 when missing), as upstream draws a
stroke with per-point widths; round caps and joins; at least one pixel.
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

from .images import LINE_PX, PAD

SUPERSAMPLE = 4


def load_strokes(path: str | Path) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _box(strokes: list[dict]) -> tuple[float, float, float, float]:
    xs = [p[0] for s in strokes for p in s["points"]]
    ys = [p[1] for s in strokes for p in s["points"]]
    if not xs:
        return 0.0, 0.0, 1.0, 1.0
    return min(xs), min(ys), max(xs), max(ys)


def render(ink: dict, height: int = LINE_PX, max_width: int = 16 * LINE_PX) -> Image.Image:
    """The line as LineImage draws it: the strokes' box plus a quarter of its height on every side, `height` px."""
    strokes = [s for s in ink.get("strokes", []) if s.get("points")]
    x0, y0, x1, y1 = _box(strokes)
    bh = y1 - y0
    pad = max(2.0, PAD * bh)
    scale = height / max(1.0, bh + 2 * pad)
    w = min(max(1, math.ceil((x1 - x0 + 2 * pad) * scale)), max_width)
    S = SUPERSAMPLE
    img = Image.new("L", (w * S, height * S), 255)
    d = ImageDraw.Draw(img)
    min_w = 1.0 / scale

    def pt(p):
        return ((p[0] - x0 + pad) * scale * S, (p[1] - y0 + pad) * scale * S)

    for s in strokes:
        base = float(s.get("width", 1.0))
        pts = s["points"]
        for k in range(len(pts)):
            pr = pts[k][2] if len(pts[k]) > 2 and pts[k][2] is not None and pts[k][2] > 0 else 1.0
            lw = max(base * pr, min_w) * scale * S
            a = pt(pts[k - 1] if k > 0 else pts[k])
            b = pt(pts[k])
            if k > 0:
                d.line([a, b], fill=0, width=max(1, int(round(lw))))
            r = lw / 2  # round caps and joins
            d.ellipse([b[0] - r, b[1] - r, b[0] + r, b[1] + r], fill=0)
    return img.resize((w, height), Image.BOX)


def augment_strokes(ink: dict, rnd: random.Random, slant: float = 0.25, width: tuple[float, float] = (0.6, 1.6),
                    jitter: float = 0.02, stretch: tuple[float, float] = (0.85, 1.15)) -> dict:
    """A varied copy of the ink: a shear (slant, tan of the angle up to `slant`), a horizontal stretch, a stroke width
    factor and a smooth jitter of the points (a share of the line's height)."""
    strokes = [s for s in ink.get("strokes", []) if s.get("points")]
    x0, y0, x1, y1 = _box(strokes)
    h = max(1.0, y1 - y0)
    sh = rnd.uniform(-slant, slant)
    sx = rnd.uniform(*stretch)
    wf = rnd.uniform(*width)
    amp = jitter * h
    out = []
    for s in strokes:
        ph1, ph2 = rnd.uniform(0, 6.3), rnd.uniform(0, 6.3)
        f = rnd.uniform(0.02, 0.08)
        pts = []
        for i, p in enumerate(s["points"]):
            y = p[1]
            x = (p[0] - x0) * sx + (y1 - y) * sh
            dx = amp * math.sin(ph1 + f * i)
            dy = amp * math.sin(ph2 + f * i)
            pts.append([x + dx, y + dy] + list(p[2:3]))
        out.append({"points": pts, "width": float(s.get("width", 1.0)) * wf})
    return {"width": ink.get("width"), "height": ink.get("height"), "strokes": out}
