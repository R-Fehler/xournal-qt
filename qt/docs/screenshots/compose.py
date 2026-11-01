#!/usr/bin/env python3
"""xournal-qt: the README's pictures from the shots of the UI tests (make-shots.sh; README.md here).

    python3 compose.py <the shots> <this folder>

Copies slides, search, markdown and plot as they are, crops the library to its cards, and puts the three sizes of
the lab journal (screen-desktop, -tablet, -phone) side by side as adaptive.png. Needs Pillow.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BACKGROUND = (240, 242, 246)


def save(image, path):
    image.convert("RGB").save(path, optimize=True)
    print(f"compose.py: {path.name} {image.width}x{image.height}")


def cropped_to_content(image, background, margin=24):
    """The window down to where its content ends (the rows below are the window's background)."""
    rgb = image.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    last = 0
    for y in range(h):
        if any(sum(abs(a - b) for a, b in zip(px[x, y], background)) > 24 for x in range(0, w, 4)):
            last = y
    return rgb.crop((0, 0, w, min(h, last + margin)))


def side_by_side(images, height=760, gap=32, pad=24):
    scaled = [im.resize((round(im.width * height / im.height), height), Image.LANCZOS) for im in images]
    width = sum(im.width for im in scaled) + gap * (len(scaled) - 1) + 2 * pad
    out = Image.new("RGB", (width, height + 2 * pad), BACKGROUND)
    draw = ImageDraw.Draw(out)
    x = pad
    for im in scaled:
        draw.rectangle((x - 1, pad - 1, x + im.width, pad + height), outline=(200, 204, 214))
        out.paste(im, (x, pad))
        x += im.width + gap
    return out


def main():
    shots, dest = Path(sys.argv[1]), Path(sys.argv[2])
    for name in ("slides", "search", "markdown", "plot"):
        save(Image.open(shots / f"{name}.png"), dest / f"{name}.png")
    library = Image.open(shots / "library.png")
    save(cropped_to_content(library, library.convert("RGB").getpixel((library.width // 2, library.height - 2))),
         dest / "library.png")
    save(side_by_side([Image.open(shots / f"screen-{n}.png") for n in ("desktop", "tablet", "phone")]),
         dest / "adaptive.png")


if __name__ == "__main__":
    main()
