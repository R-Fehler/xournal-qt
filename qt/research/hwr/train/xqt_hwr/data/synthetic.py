"""Synthetic handwriting lines: corpus text drawn with handwriting fonts, word by word with jitter (size, baseline,
spacing, letter spacing), then varied as a pen and a scanner would (slant, stroke width, ink tone, blur, paper noise)
and framed as the app frames a line (images.frame_line). Each font is a "writer": val and test hold out whole fonts.
"""
from __future__ import annotations

import math
import random
from dataclasses import dataclass, fields
from multiprocessing import Pool
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from . import corpora, fonts as fontlib
from .dataset import DatasetInfo, DatasetWriter, assign_writer_splits
from .images import frame_line


@dataclass
class SynthConfig:
    size: int = 64                 # font size before framing (px)
    size_jitter: float = 0.12      # per word
    baseline_jitter: float = 0.06  # per word, share of the size
    word_gap: tuple = (0.25, 0.9)  # share of the size
    letter_spacing: float = 0.3    # probability of per-letter spacing jitter in a word
    slant_deg: float = 14.0
    stroke: float = 0.4            # probability of a thicker stroke (PIL stroke_width 1..2)
    thin: float = 0.15             # probability of thinner strokes (erosion)
    ink_tone: tuple = (0, 90)      # grey value of the ink
    blur: float = 0.25
    noise: float = 0.2             # paper noise and tint
    ruled: float = 0.0             # ruled lines behind the text (the app's line pictures have none)
    min_words: int = 1
    max_words: int = 8

    @staticmethod
    def from_dict(d: dict | None) -> "SynthConfig":
        d = dict(d or {})
        names = {f.name for f in fields(SynthConfig)}
        bad = set(d) - names
        if bad:
            raise ValueError(f"unknown synthetic keys: {sorted(bad)}")
        for k in ("word_gap", "ink_tone"):
            if k in d:
                d[k] = tuple(d[k])
        return SynthConfig(**d)


_font_cache: dict = {}


def _font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    key = (str(path), size)
    f = _font_cache.get(key)
    if f is None:
        f = ImageFont.truetype(str(path), size)
        if len(_font_cache) > 512:
            _font_cache.clear()
        _font_cache[key] = f
    return f


def render_line(text: str, font_path: Path, rnd: random.Random, cfg: SynthConfig) -> Image.Image:
    words = text.split()
    base = cfg.size
    W = int(base * (len(text) * 0.9 + 4))
    H = int(base * 3)
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    tone = rnd.randint(*cfg.ink_tone)
    stroke = rnd.randint(1, 2) if rnd.random() < cfg.stroke else 0
    x = base * 0.5
    y0 = base
    for w in words:
        size = max(8, int(base * (1 + rnd.uniform(-cfg.size_jitter, cfg.size_jitter))))
        f = _font(font_path, size)
        y = y0 + rnd.uniform(-cfg.baseline_jitter, cfg.baseline_jitter) * base
        if rnd.random() < cfg.letter_spacing and len(w) > 1:
            for c in w:
                d.text((x, y), c, font=f, fill=tone, anchor="ls", stroke_width=stroke, stroke_fill=tone)
                x += f.getlength(c) * rnd.uniform(0.92, 1.12)
        else:
            d.text((x, y), w, font=f, fill=tone, anchor="ls", stroke_width=stroke, stroke_fill=tone)
            x += f.getlength(w)
        x += base * rnd.uniform(*cfg.word_gap)
    img = img.crop((0, 0, min(W, int(x + base)), H))
    # slant
    sh = math.tan(math.radians(rnd.uniform(-cfg.slant_deg, cfg.slant_deg)))
    w, h = img.size
    extra = int(abs(sh) * h) + 2
    canvas = Image.new("L", (w + 2 * extra, h), 255)
    canvas.paste(img, (extra, 0))
    img = canvas.transform(canvas.size, Image.AFFINE, (1, sh, -sh * h / 2, 0, 1, 0), resample=Image.BILINEAR,
                           fillcolor=255)
    if rnd.random() < cfg.thin:
        img = img.filter(ImageFilter.MaxFilter(3))
    if rnd.random() < cfg.ruled:
        d = ImageDraw.Draw(img)
        g = rnd.randint(150, 220)
        d.line([(0, y0 + 2), (img.size[0], y0 + 2)], fill=g, width=1)
    if rnd.random() < cfg.blur:
        img = img.filter(ImageFilter.GaussianBlur(rnd.uniform(0.3, 1.0)))
    if rnd.random() < cfg.noise:
        a = np.asarray(img, dtype=np.float32)
        tint = rnd.uniform(0, 25)
        a = a - tint * (a > 128) + np.random.default_rng(rnd.randrange(1 << 30)).normal(0, rnd.uniform(2, 12), a.shape)
        img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    return frame_line(img)


def _work(args):
    i, text, font_path, seed, cfg = args
    rnd = random.Random(seed)
    try:
        return i, render_line(text, Path(font_path), rnd, cfg)
    except Exception:
        return i, None


def generate(out: Path, name: str, lang: str, corpus_specs: list[dict], font_dir: Path | None, n: int,
             cfg: SynthConfig | None = None, seed: int = 0, workers: int = 4, cache: Path | None = None,
             allow_system_fonts: bool = False) -> int:
    cfg = cfg or SynthConfig()
    rnd = random.Random(seed)
    cache = cache or out.parent / ".cache"
    cs = [corpora.load(spec, lang, cache) for spec in corpus_specs]
    sentences = [s for c in cs for s in c.sentences]
    texts = corpora.lines_from(sentences, n, rnd, cfg.min_words, cfg.max_words)
    fs = fontlib.load(font_dir, allow_system=allow_system_fonts)
    if not fs:
        raise SystemExit(f"no fonts in {font_dir} (run: prepare.py fonts)")
    splits = assign_writer_splits([f.family for f in fs], seed=seed)
    jobs, skipped = [], 0
    for i, t in enumerate(texts):
        ok = [f for f in fs if f.covers(t)]
        if not ok:
            skipped += 1
            continue
        f = rnd.choice(ok)
        jobs.append((i, t, str(f.path), rnd.randrange(1 << 30), cfg))
    font_of = {j[0]: j[2] for j in jobs}
    family = {str(f.path): f.family for f in fs}
    licences = sorted({c.licence for c in cs})
    flic = sorted({f.licence for f in fs})
    info = DatasetInfo(name=name, languages=[lang], kind="synthetic",
                       licence=f"text: {'; '.join(licences)}; fonts: {', '.join(flic)}",
                       source="xqt_hwr synthetic: " + ", ".join(c.name for c in cs),
                       extra={"fonts": sorted({f.family for f in fs}), "corpora": [c.name for c in cs]})
    with DatasetWriter(out, info, overwrite=True) as w:
        def take(results):
            for i, img in results:
                if img is not None:
                    fam = family[font_of[i]]
                    w.add(f"{name}-{i:07d}", img, texts[i], lang, f"font:{fam}", splits[fam])
        if workers > 1:
            with Pool(workers) as pool:
                take(pool.imap_unordered(_work, jobs, chunksize=64))
        else:
            take(map(_work, jobs))
    if skipped:
        print(f"{name}: {skipped} lines skipped (no font has all their characters)")
    return w.count
