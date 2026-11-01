#!/usr/bin/env bash
# xournal-qt: the demo library of the README's pictures (README.md in the folder above).
#
#   qt/docs/screenshots/demo/make-demo.sh <folder>    # writes the library into <folder> (made; must not exist yet)
#
# Needs node with Playwright (its Chromium prints the slides and the paper as PDFs: print.js) and python3 with the
# package Hershey-Fonts (`pip install Hershey-Fonts`: the font of hand.py).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$1"
[ ! -e "$out" ] || { echo "make-demo.sh: $out exists already" >&2; exit 1; }
pdfs="$(mktemp -d)"
trap 'rm -rf -- "$pdfs"' EXIT
node "$here/print.js" "$here/slides.html" "$pdfs/slides.pdf"
node "$here/print.js" "$here/paper.html" "$pdfs/paper.pdf"
python3 "$here/make_demo.py" "$out" "$pdfs/slides.pdf" "$pdfs/paper.pdf"
echo "make-demo.sh: the demo library is in $out"
