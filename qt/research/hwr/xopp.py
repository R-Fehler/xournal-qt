"""Throwaway: read strokes from a .xopp (gzip XML) and render them with PIL. Not app code."""
import gzip, re, sys
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

def load(path):
    raw = open(path, 'rb').read()
    try:
        raw = gzip.decompress(raw)
    except OSError:
        pass
    root = ET.fromstring(raw)
    pages = []
    for page in root.iter('page'):
        w, h = float(page.get('width')), float(page.get('height'))
        strokes = []
        for s in page.iter('stroke'):
            if s.get('tool') not in ('pen', None):
                continue
            v = [float(x) for x in s.text.split()]
            pts = list(zip(v[0::2], v[1::2]))
            widths = [float(x) for x in s.get('width').split()]
            strokes.append({'pts': pts, 'w': widths[0]})
        pages.append({'w': w, 'h': h, 'strokes': strokes})
    return pages

def bbox(strokes):
    xs = [x for s in strokes for x, _ in s['pts']]
    ys = [y for s in strokes for _, y in s['pts']]
    return min(xs), min(ys), max(xs), max(ys)

def render(strokes, box=None, scale=3.0, pad=4, width_px=None):
    """Black ink on white; box in page points."""
    x0, y0, x1, y1 = box or bbox(strokes)
    x0 -= pad; y0 -= pad; x1 += pad; y1 += pad
    W, H = int((x1 - x0) * scale) + 1, int((y1 - y0) * scale) + 1
    img = Image.new('L', (W, H), 255)
    d = ImageDraw.Draw(img)
    for s in strokes:
        p = [((x - x0) * scale, (y - y0) * scale) for x, y in s['pts']]
        lw = max(1, int(round((width_px or s['w'] * scale))))
        if len(p) == 1:
            d.ellipse([p[0][0]-lw/2, p[0][1]-lw/2, p[0][0]+lw/2, p[0][1]+lw/2], fill=0)
        else:
            d.line(p, fill=0, width=lw, joint='curve')
    return img

if __name__ == '__main__':
    pages = load(sys.argv[1])
    for i, pg in enumerate(pages):
        print(i, len(pg['strokes']), 'strokes', bbox(pg['strokes']) if pg['strokes'] else '')
        img = render(pg['strokes'], (0, 0, pg['w'], pg['h']), scale=float(sys.argv[3]) if len(sys.argv) > 3 else 2.0)
        img.save(f"{sys.argv[2]}-p{i}.png")
