"""Throwaway: split ink into lines and words from stroke geometry alone (no model), then recognise each word
with TrOCR and keep top-k candidates with their boxes, as a search index would. Not app code."""
import statistics, sys, time
from xopp import load, render, bbox

def boxes(strokes):
    return [bbox([s]) for s in strokes]

def lines(strokes):
    """Greedy: strokes sorted by vertical centre join the line whose band they overlap most."""
    bx = boxes(strokes)
    h = statistics.median([b[3] - b[1] for b in bx]) or 1
    order = sorted(range(len(strokes)), key=lambda i: (bx[i][1] + bx[i][3]) / 2)
    out = []  # [y0, y1, [idx]]
    for i in order:
        c = (bx[i][1] + bx[i][3]) / 2
        for L in out:
            if L[0] - 0.3 * h <= c <= L[1] + 0.3 * h:
                L[2].append(i); L[0] = min(L[0], bx[i][1]) if bx[i][3]-bx[i][1] < 2.5*h else L[0]
                L[1] = max(L[1], bx[i][3]) if bx[i][3]-bx[i][1] < 2.5*h else L[1]
                break
        else:
            out.append([bx[i][1], bx[i][3], [i]])
    return [[strokes[i] for i in L[2]] for L in sorted(out, key=lambda L: L[0])], h

def otsu(gaps):
    """Threshold between intra-word and inter-word gaps (two classes, max between-class variance)."""
    g = sorted(gaps)
    best, thr = -1, None
    for i in range(1, len(g)):
        a, b = g[:i], g[i:]
        ma, mb = sum(a) / len(a), sum(b) / len(b)
        v = len(a) * len(b) * (ma - mb) ** 2
        if v > best:
            best, thr = v, (g[i - 1] + g[i]) / 2
    return thr

def words(line, h, lo=0.6, hi=2.0):
    """Words of a line: strokes sorted by left edge; tiny strokes (dots, accents, umlaut marks) are not used to
    split and join the nearest word; a new word starts at a gap above an adaptive threshold (Otsu over the line's
    gaps, clamped to [lo, hi] x median stroke height)."""
    big = [s for s in line if max(bbox([s])[2] - bbox([s])[0], bbox([s])[3] - bbox([s])[1]) >= 0.5 * h]
    small = [s for s in line if s not in big]
    bx = sorted(((bbox([s]), s) for s in big), key=lambda t: t[0][0])
    gaps, right = [], None
    for b, s in bx:
        if right is not None:
            gaps.append(b[0] - right)
        right = b[2] if right is None else max(right, b[2])
    pos = [g for g in gaps if g > 0]
    thr = otsu(pos) if len(pos) >= 4 else hi * h
    thr = min(max(thr, lo * h), hi * h)
    ws, boxes_, right = [], [], None
    for b, s in bx:
        if right is None or b[0] - right > thr:
            ws.append([s]); boxes_.append(list(b)); right = b[2]
        else:
            ws[-1].append(s); right = max(right, b[2])
            bb = boxes_[-1]; bb[0] = min(bb[0], b[0]); bb[1] = min(bb[1], b[1]); bb[2] = max(bb[2], b[2]); bb[3] = max(bb[3], b[3])
    for s in small:  # attach to the word whose box is nearest horizontally
        b = bbox([s]); cx = (b[0] + b[2]) / 2
        if ws:
            j = min(range(len(ws)), key=lambda k: 0 if boxes_[k][0] <= cx <= boxes_[k][2] else min(abs(cx - boxes_[k][0]), abs(cx - boxes_[k][2])))
            ws[j].append(s)
    return ws

if __name__ == "__main__":
    from trocr_onnx import TrOCR
    pg = load(sys.argv[1])[0]
    y0, y1 = float(sys.argv[2]), float(sys.argv[3])
    sel = [s for s in pg["strokes"] if y0 <= sum(y for _, y in s["pts"]) / len(s["pts"]) <= y1]
    t = time.perf_counter(); ls, h = lines(sel); ws = [w for L in ls for w in words(L, h)]
    print(f"{len(sel)} strokes -> {len(ls)} line(s), {len(ws)} words; segmentation {1000*(time.perf_counter()-t):.1f} ms")
    m = TrOCR(quantized=True, threads=2)
    t = time.perf_counter()
    for w in ws:
        img = render(w, scale=3, pad=6)
        cands = m.beam(img, k=4)
        x0, yy0, x1, yy1 = bbox(w)
        print(f"  box=({x0:.0f},{yy0:.0f},{x1:.0f},{yy1:.0f}) " + " | ".join(f"{c} ({s:.1f})" for c, s in cands))
    print(f"recognition: {(time.perf_counter()-t)/len(ws)*1000:.0f} ms/word (beam 4, int8 small, 2 threads)")
