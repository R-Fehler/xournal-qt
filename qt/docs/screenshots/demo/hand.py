"""Handwriting-like ink from Hershey's single-stroke script font: a gentle slant, a baseline that drifts, letters a
little bigger or smaller, smoothed points and a pen pressure that swells in the middle of a stroke."""
import math, random
from HersheyFonts import HersheyFonts

def _font(name, size):
    f = HersheyFonts(); f.load_default_font(name); f.normalize_rendering(size); return f

def _chaikin(pts, n=1):
    for _ in range(n):
        if len(pts) < 3: return pts
        out = [pts[0]]
        for a, b in zip(pts, pts[1:]):
            out += [(0.75*a[0]+0.25*b[0], 0.75*a[1]+0.25*b[1]), (0.25*a[0]+0.75*b[0], 0.25*a[1]+0.75*b[1])]
        out.append(pts[-1]); pts = out
    return pts

def write(text, x, y, size, seed=1, fontname="scripts", slant=0.12, angle=0.0):
    """[(points, pressures)] of `text`, baseline from (x, y) (page coordinates, y down), capitals about `size` high."""
    rng = random.Random(seed)
    f = _font(fontname, size)
    ph1, ph2 = rng.uniform(0, 6.3), rng.uniform(0, 6.3)
    k1, k2 = rng.uniform(0.004, 0.008), rng.uniform(0.02, 0.035)
    ca, sa = math.cos(angle), math.sin(angle)
    out = []
    for stroke in f.strokes_for_text(text):
        pts = list(stroke)
        cx = sum(p[0] for p in pts) / len(pts)
        g = random.Random(int(cx // (size * 0.45)) * 7919 + seed)  # (about one letter)
        sc = 1 + g.uniform(-0.06, 0.06)
        dy = g.uniform(-0.035, 0.035) * size
        q = []
        for px, py in pts:
            base = math.sin(px * k1 + ph1) * 0.10 * size + math.sin(px * k2 + ph2) * 0.03 * size
            X = px + slant * py
            Y = -(py * sc) + base + dy
            q.append((x + X * ca - Y * sa, y + X * sa + Y * ca))
        q = _chaikin(q, 1)
        n = len(q)
        pr = [min(1.0, 0.62 + 0.38 * math.sin(math.pi * (i + 0.5) / n) ** 0.5) for i in range(n)]
        out.append((q, pr))
    return out

def width_of(text, size, fontname="scripts"):
    xs = [p[0] for s in _font(fontname, size).strokes_for_text(text) for p in s]
    return max(xs) - min(xs) if xs else 0
