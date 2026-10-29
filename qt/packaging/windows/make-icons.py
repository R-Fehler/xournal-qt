#!/usr/bin/env python3
"""The Windows icons, made from the SVG icons (run it again after changing one of them):

    python3 qt/packaging/windows/make-icons.py

  xournal-qt.ico           the program (qt/packaging/xournal-qt.svg): the .exe, the Start menu, the uninstall entry
  xournal-qt-document.ico  .xopp and .xoj files in Explorer (upstream's ui/pixmaps/application-x-xopp.svg)

Each .ico holds the sizes Windows asks for (16 to 256 pixels) as PNG images, which Windows reads since Vista. The
SVGs are drawn with librsvg (PyGObject: python3-gi, gir1.2-rsvg-2.0, python3-cairo); the files are committed, so the
Windows build needs none of this.
"""
import io
import pathlib
import struct

import cairo
import gi

gi.require_version("Rsvg", "2.0")
from gi.repository import Rsvg  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
ICONS = {
    "xournal-qt.ico": ROOT / "qt/packaging/xournal-qt.svg",
    "xournal-qt-document.ico": ROOT / "ui/pixmaps/application-x-xopp.svg",
}


def png(svg: Rsvg.Handle, size: int) -> bytes:
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    context = cairo.Context(surface)
    viewport = Rsvg.Rectangle()
    viewport.x, viewport.y, viewport.width, viewport.height = 0, 0, size, size
    svg.render_document(context, viewport)
    out = io.BytesIO()
    surface.write_to_png(out)
    return out.getvalue()


def ico(images: list[tuple[int, bytes]]) -> bytes:
    # ICONDIR, then one ICONDIRENTRY per image, then the images (a width or height of 0 means 256)
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = len(head) + 16 * len(images)
    entries, data = b"", b""
    for size, image in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset + len(data))
        data += image
    return head + entries + data


def main() -> None:
    for name, source in ICONS.items():
        svg = Rsvg.Handle.new_from_file(str(source))
        (HERE / name).write_bytes(ico([(size, png(svg, size)) for size in SIZES]))
        print(f"{name}: {source.relative_to(ROOT)}, {len(SIZES)} sizes")


if __name__ == "__main__":
    main()
