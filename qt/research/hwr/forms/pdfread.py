"""Reads the manifest back out of a form's PDF, with the standard library only.

The manifest is an embedded file (the `embedfile` package): a stream object of /Type /EmbeddedFile, Flate-compressed.
Usage: python3 pdfread.py form.pdf  (prints the manifest)
"""
import json
import re
import sys
import zlib

_STREAM = re.compile(rb"(\d+)\s+(\d+)\s+obj\s*<<(.*?)>>\s*stream\r?\n", re.S)


def embedded_files(pdf_bytes):
    """The contents of every /EmbeddedFile stream in a PDF, decompressed (bytes)."""
    out = []
    for m in _STREAM.finditer(pdf_bytes):
        head = m.group(3)
        if b"/EmbeddedFile" not in head:
            continue
        start = m.end()
        length = re.search(rb"/Length\s+(\d+)(\s+\d+\s+R)?", head)
        if length and not length.group(2):
            data = pdf_bytes[start:start + int(length.group(1))]
        else:
            data = pdf_bytes[start:pdf_bytes.index(b"endstream", start)]
        if b"/FlateDecode" in head:
            data = zlib.decompressobj().decompress(data)
        out.append(data)
    return out


def read_manifest(path):
    """The form manifest embedded in the PDF at `path` (a dict), or None."""
    with open(path, "rb") as f:
        pdf = f.read()
    for data in embedded_files(pdf):
        try:
            d = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            continue
        if isinstance(d, dict) and "form" in d and "items" in d:
            return d
    return None


if __name__ == "__main__":
    m = read_manifest(sys.argv[1])
    if m is None:
        sys.exit("no manifest in %s" % sys.argv[1])
    json.dump(m, sys.stdout, ensure_ascii=False, indent=1)
    print()
