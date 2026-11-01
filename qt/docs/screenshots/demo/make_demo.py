#!/usr/bin/env python3
"""xournal-qt: the demo documents of the README's pictures (make-demo.sh runs this; see README.md here).

Writes into the folder given (a library):
  Lectures/Fourier series.xopp (+ .xopp.bg.pdf): the lecture slides with handwritten notes, highlights and arrows
  Papers/Sampling and aliasing.pdf: a paper (copied as it is)
  Lab journal.xopp: two pages of handwritten notes with a sketch, on squared paper
  Exercise 7.1.xopp: a worked exercise, by hand on lined paper
  Seminar notes.md, Reading list.md: Markdown notes

The handwriting is Hershey's single-stroke script font made to look written (hand.py), so that the pictures need no
one's real notes. Coordinates are PDF points (the slides are 960 x 540 pt).
"""
import gzip
import math
import shutil
import sys
from pathlib import Path

from hand import write, width_of

BLUE = "#2547a8ff"
RED = "#c8322bff"
GREEN = "#2e7d32ff"
YELLOW = "#ffe14d7f"
ORANGE = "#ffa1547f"


class Page:
    def __init__(self, width, height, background):
        self.width, self.height, self.background = width, height, background
        self.strokes = []

    def ink(self, strokes, color=BLUE, width=1.6):
        for pts, pressures in strokes:
            self.strokes.append(("pen", color, width, pts, pressures))

    def text(self, text, x, y, size=17, color=BLUE, seed=1, width=1.6, **kw):
        self.ink(write(text, x, y, size, seed=seed, **kw), color, width)
        return x + width_of(text, size)

    def line(self, pts, color=BLUE, width=1.6, tool="pen"):
        pts = _smooth(pts)
        self.strokes.append((tool, color, width, pts, [1.0] * len(pts)))

    def highlight(self, x0, x1, y, height=16, color=YELLOW):
        # a highlighter stroke as a hand draws it: a little uphill, not quite straight
        n = 24
        pts = [(x0 + (x1 - x0) * i / n, y - 1.2 * i / n + 0.6 * math.sin(i * 0.7)) for i in range(n + 1)]
        self.strokes.append(("highlighter", color, height, pts, None))

    def arrow(self, a, b, color=BLUE, bend=0.18, width=1.6):
        (ax, ay), (bx, by) = a, b
        mx, my = (ax + bx) / 2 - (by - ay) * bend, (ay + by) / 2 + (bx - ax) * bend
        pts = [((1 - t) ** 2 * ax + 2 * (1 - t) * t * mx + t * t * bx, (1 - t) ** 2 * ay + 2 * (1 - t) * t * my + t * t * by)
               for t in (i / 30 for i in range(31))]
        self.line(pts, color, width)
        ang = math.atan2(by - pts[-4][1], bx - pts[-4][0])
        for s in (-1, 1):
            self.line([(bx - 11 * math.cos(ang + s * 0.45), by - 11 * math.sin(ang + s * 0.45)), (bx, by)], color, width)

    def ellipse(self, cx, cy, rx, ry, color=RED, width=1.8):
        # a circle drawn by hand: a little more than once around, the end overlapping the start
        pts = []
        for i in range(56):
            t = -0.3 + 2 * math.pi * 1.08 * i / 55
            r = 1 + 0.035 * math.sin(3 * t + 1)
            pts.append((cx + rx * r * math.cos(t), cy + ry * r * math.sin(t) + 2 * t / 6))
        self.line(pts, color, width)

    def check(self, x, y, color=GREEN, width=2.0):
        self.line([(x, y), (x + 4, y + 5), (x + 7, y + 9), (x + 12, y + 1), (x + 18, y - 9)], color, width)

    def xml(self):
        out = [f'<page width="{self.width:g}" height="{self.height:g}">', self.background, "<layer>"]
        for tool, color, width, pts, pressures in self.strokes:
            coords = " ".join(f"{x:.2f} {y:.2f}" for x, y in pts)
            if pressures and tool == "pen":
                widths = " ".join(f"{width * (p + q) / 2:.3f}" for p, q in zip(pressures, pressures[1:]))
                w = f"{width} {widths}"
            else:
                w = f"{width}"
            out.append(f'<stroke tool="{tool}" color="{color}" width="{w}" capStyle="round">{coords}</stroke>')
        out += ["</layer>", "</page>"]
        return "\n".join(out)


def _smooth(pts, n=2):
    for _ in range(n):
        if len(pts) < 3:
            return pts
        out = [pts[0]]
        for a, b in zip(pts, pts[1:]):
            out += [(0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1]), (0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1])]
        out.append(pts[-1])
        pts = out
    return pts


def save(path, pages):
    body = "\n".join(p.xml() for p in pages)
    doc = ('<?xml version="1.0" standalone="no"?>\n<xournal creator="xournal-qt demo" fileversion="4">\n'
           "<title>Xournal++ document - see https://xournalpp.github.io/</title>\n" + body + "\n</xournal>\n")
    with gzip.open(path, "wt", encoding="utf-8") as f:
        f.write(doc)


def slides(out: Path, pdf: Path):
    """The lecture with notes: the slides are 960 x 540 pt."""
    pages = []
    for n in range(1, 6):
        bg = (f'<background type="pdf" domain="attach" filename="bg.pdf" pageno="{n}"/>' if n == 1
              else f'<background type="pdf" pageno="{n}"/>')
        pages.append(Page(960, 540, bg))
    p1, p2, p3, p4, p5 = pages

    # 1: the date, as one writes it at the start of a lecture
    p1.text("Tue, 14 Oct", 640, 118, 26, seed=11)

    # 2: what the harmonics are, beside the series
    p2.ellipse(402, 181, 44, 24)
    p2.text("k-th harmonic:", 590, 160, 24, seed=21)
    p2.text("frequency k w0", 605, 196, 24, seed=22)
    p2.arrow((585, 172), (452, 178), bend=0.2)
    p2.text("how much of each", 590, 330, 22, color=RED, seed=23)
    p2.text("harmonic is in x", 600, 362, 22, color=RED, seed=24)
    p2.arrow((585, 338), (540, 338), color=RED, bend=0.1)

    # 3: the jump and its overshoot
    p3.highlight(568, 866, 309, 15)
    p3.highlight(568, 614, 336, 15)
    p3.text("overshoot at", 215, 318, 22, color=RED, seed=31)
    p3.text("every jump!", 225, 346, 22, color=RED, seed=32)
    p3.ellipse(199, 288, 17, 11, width=1.5)
    p3.text("sin terms only: x is odd", 560, 400, 22, seed=33)

    # 4: in the mean, and what Parseval says
    p4.highlight(641, 772, 103, 15)
    p4.line([(214, 152), (446, 150)], RED, 1.5)
    p4.text("Gibbs: about 9 %", 548, 190, 24, color=RED, seed=41)
    p4.arrow((556, 170), (452, 154), color=RED, bend=0.15)
    p4.text("energy in time = energy in the coefficients", 70, 380, 22, seed=42)
    p4.arrow((230, 356), (230, 292), bend=0.1)

    # 5: what is done
    p5.check(16, 102)
    p5.check(16, 139)
    p5.text("due Thursday, 10:00", 560, 300, 28, color=RED, seed=51)
    p5.line([(556, 312), (842, 309)], RED, 1.8)
    p5.text("ask about 7.3 in the tutorial", 560, 350, 20, seed=52)

    save(out, pages)
    shutil.copyfile(pdf, out.with_name(out.name + ".bg.pdf"))


def lab_journal(out: Path):
    """Two pages of A4 on squared paper: notes, a sketch of a filter's response, a little table."""
    bg = '<background type="solid" color="#ffffffff" style="graph"/>'
    a, b = Page(595.28, 841.89, bg), Page(595.28, 841.89, bg)
    a.text("Lab journal", 60, 80, 30, seed=101)
    a.line([(58, 92), (250, 90)], BLUE, 1.8)
    a.text("14 Oct, measuring the anti-aliasing filter", 60, 128, 17, seed=102)
    lines = ["fourth order, cut-off at 1 kHz", "sampled at 2.6 kHz and at 8 kHz",
             "the aliasing error stays below the noise", "at 2.6 kHz: just about enough"]
    for i, t in enumerate(lines):
        a.line([(64, 161 + i * 30), (73, 160 + i * 30)], BLUE, 1.8)
        a.text(t, 80, 166 + i * 30, 16, seed=110 + i)
    # the sketch: an amplitude response falling off
    ox, oy = 90, 520
    a.line([(ox, oy), (ox + 400, oy)], BLUE, 1.4)
    a.line([(ox, oy), (ox, oy - 230)], BLUE, 1.4)
    curve = [(ox + x, oy - 190 / (1 + (x / 180.0) ** 8) ** 0.5 - 6) for x in range(0, 390, 6)]
    a.line(curve, RED, 2.0)
    a.line([(ox + 180, oy - 6), (ox + 180, oy + 8)], BLUE, 1.2)
    a.text("1 kHz", ox + 162, oy + 28, 14, seed=120)
    a.text("f", ox + 395, oy + 20, 15, seed=121)
    a.text("|H|", ox - 40, oy - 210, 15, seed=122)
    a.text("-24 dB per octave", ox + 230, oy - 120, 15, color=RED, seed=123)
    a.arrow((ox + 260, oy - 112), (ox + 238, oy - 70), color=RED)
    a.text("Next: repeat with the 12-bit logger", 60, 620, 17, seed=124)
    a.highlight(60, 380, 614, 18, ORANGE)

    b.text("Results", 60, 80, 26, seed=201)
    rows = [("rate", "error", "noise"), ("2.6 kHz", "-71 dB", "-72 dB"), ("4 kHz", "-80 dB", "-72 dB"),
            ("8 kHz", "-93 dB", "-72 dB")]
    for i, row in enumerate(rows):
        for j, cell in enumerate(row):
            b.text(cell, 70 + j * 140, 135 + i * 34, 16, seed=210 + i * 3 + j)
    b.line([(62, 144), (470, 142)], BLUE, 1.2)
    b.text("filter of order 4 is enough above 2.6 times the cut-off", 60, 300, 16, seed=230)
    b.ellipse(150, 170, 52, 16)
    save(out, [a, b])


def exercise_sheet(out: Path):
    """A worked exercise on lined paper: the coefficients of the triangle wave, by hand."""
    a = Page(595.28, 841.89, '<background type="solid" color="#ffffffff" style="lined"/>')
    a.text("Exercise 7.1", 90, 80, 26, seed=301)
    a.text("triangle wave, period T, odd harmonics only", 90, 128, 17, seed=302)
    steps = ["integrate by parts twice:", "c_k = 2 / (pi k)^2 for odd k", "falls off as 1 / k^2",
             "faster than the square wave (1 / k)", "no jumps, so no Gibbs overshoot"]
    for i, t in enumerate(steps):
        a.text(t, 110, 182 + i * 36, 18, seed=310 + i)
    a.ellipse(205, 214, 112, 20)
    a.check(60, 360)
    a.text("checked with the plot", 90, 372, 17, color=GREEN, seed=320)
    ox, oy = 110, 520  # the triangle wave, sketched
    a.line([(ox - 10, oy), (ox + 380, oy)], BLUE, 1.2)
    tri = [(ox + x, oy - 60 * (1 - abs(((x / 90.0) % 2) - 1) * 2)) for x in range(0, 365, 15)]
    a.line(tri, RED, 1.8)
    save(out, [a])


READING = """# Reading list

- [ ] Oppenheim, Willsky: *Signals and Systems*, chapter 3
- [ ] Gibbs, *Fourier's series* (Nature, 1899)
- [x] Keller, Duarte: *Sampling and aliasing in practice*

Tags: #signals #exam
"""

SEMINAR = """# Seminar, week 7

Fourier series of the **square wave**: only odd harmonics, falling off as $1/k$.

## To do

- [x] read chapter 4
- [ ] exercise 7.2 (real signals: $c_{-k} = c_k^*$)
- [ ] plot the partial sums for 1, 3 and 15 terms

> The overshoot at a jump does not go away, it only gets narrower.

| terms | overshoot |
| --- | --- |
| 1 | 27 % |
| 15 | 9 % |
"""


def main():
    here = Path(__file__).resolve().parent
    library, slides_pdf, paper_pdf = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    (library / "Lectures").mkdir(parents=True, exist_ok=True)
    (library / "Papers").mkdir(parents=True, exist_ok=True)
    slides(library / "Lectures" / "Fourier series.xopp", slides_pdf)
    shutil.copyfile(paper_pdf, library / "Papers" / "Sampling and aliasing.pdf")
    lab_journal(library / "Lab journal.xopp")
    exercise_sheet(library / "Exercise 7.1.xopp")
    (library / "Seminar notes.md").write_text(SEMINAR, encoding="utf-8")
    (library / "Reading list.md").write_text(READING, encoding="utf-8")
    del here


if __name__ == "__main__":
    main()
