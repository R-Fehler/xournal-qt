"""Line pictures as the app gives them to a recogniser (qt/src/hwr/LineImage.*): ink dark on white, the ink's box with a
margin of a quarter of its height on every side, LINE_PX high. Scans and synthetic lines are brought to the same
framing (`frame_line`), so every source looks alike to the model."""
from __future__ import annotations

import numpy as np
from PIL import Image, ImageOps

LINE_PX = 128      # LineImage.h
PAD = 0.25         # of the ink box's height, every side
TROCR_PX = 384     # MODEL_PX


def otsu(a: np.ndarray) -> float:
    hist = np.bincount(a.ravel(), minlength=256).astype(np.float64)
    total = hist.sum()
    if total == 0:
        return 128.0
    omega = np.cumsum(hist) / total
    mu = np.cumsum(hist * np.arange(256)) / total
    mu_t = mu[-1]
    with np.errstate(divide="ignore", invalid="ignore"):
        sigma = (mu_t * omega - mu) ** 2 / (omega * (1 - omega))
    sigma = np.nan_to_num(sigma)
    return float(np.argmax(sigma))


def ink_box(img: Image.Image, min_share: float = 0.002) -> tuple[int, int, int, int] | None:
    """The box (x0, y0, x1, y1) of the dark ink: Otsu threshold, rows and columns with at least `min_share` ink
    (ignores specks). None for an empty picture."""
    a = np.asarray(img.convert("L"), dtype=np.uint8)
    if a.size == 0 or int(a.max()) - int(a.min()) < 16:
        return None
    t = otsu(a)
    ink = a <= t  # (Otsu: class 0 is the values up to t)
    rows = ink.sum(1) >= max(1, min_share * a.shape[1])
    cols = ink.sum(0) >= max(1, min_share * a.shape[0])
    if not rows.any() or not cols.any():
        return None
    ys, xs = np.where(rows)[0], np.where(cols)[0]
    return int(xs[0]), int(ys[0]), int(xs[-1]) + 1, int(ys[-1]) + 1


def frame_line(img: Image.Image, height: int = LINE_PX, pad: float = PAD, max_width: int = 16 * LINE_PX) -> Image.Image:
    """Crop to the ink, add the app's margin, scale to `height` (keeps the aspect ratio, width capped)."""
    img = img.convert("L")
    box = ink_box(img)
    if box is not None:
        img = img.crop(box)
    w, h = img.size
    p = max(2, int(round(pad * h)))
    img = ImageOps.expand(img, border=p, fill=255)
    w, h = img.size
    nw = max(1, min(max_width, int(round(w * height / h))))
    return img.resize((nw, height), Image.BILINEAR)


def to_trocr_pixels(img: Image.Image, size: int = TROCR_PX) -> np.ndarray:
    """The TrOCR input of a framed line: squeezed to size x size, three equal channels, (v / 255 - 0.5) / 0.5."""
    g = img.convert("L").resize((size, size), Image.BILINEAR)
    a = np.asarray(g, dtype=np.float32) / 255.0
    a = (a - 0.5) / 0.5
    return np.repeat(a[None], 3, axis=0)


def to_ctc_pixels(img: Image.Image, height: int = 64, max_width: int = 2048) -> np.ndarray:
    """The CTC input of a framed line (FORMATS.md): [1, height, W], ink = 1, paper = 0, aspect ratio kept."""
    g = img.convert("L")
    w, h = g.size
    nw = max(8, min(max_width, int(round(w * height / h))))
    g = g.resize((nw, height), Image.BILINEAR)
    a = 1.0 - np.asarray(g, dtype=np.float32) / 255.0
    return a[None]
