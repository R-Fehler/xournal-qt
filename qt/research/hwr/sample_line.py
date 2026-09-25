"""Throwaway: crop the cleanest line of test/files/benchmark/handwritten-text.xopp (repo sample)."""
import sys
from xopp import load, render
pg = load(sys.argv[1])[0]
y0, y1 = float(sys.argv[3]), float(sys.argv[4])
sel = [s for s in pg["strokes"] if y0 <= sum(y for _, y in s["pts"]) / len(s["pts"]) <= y1]
print(len(sel), 'strokes')
render(sel, scale=3).save(sys.argv[2])
