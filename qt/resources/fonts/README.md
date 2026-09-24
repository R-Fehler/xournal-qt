# Symbol font for Android

`XqtSymbols.ttf` ("Xournal Qt Symbols") is a subset of DejaVu Sans 2.37 (Bitstream Vera license, see
`LICENSE-DejaVu.txt`; renamed as that license asks of modified fonts) with arrows, geometric shapes and a few
dingbats (✓ ✕ ✎ ☐ ● ⋮ ↵ ← →). Android's UI font has none of them, and the phones' own symbol font is split over two
files with the same family name, of which Qt picks the wrong one, so these symbols showed as empty boxes.
`AndroidSetup.cpp` registers it as the fallback font on Android only (`qt/cmake/XqtAndroid.cmake`).

Made with fontTools from `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`: U+2190–21FF, U+25A0–25FF, and
U+2022 2026 2212 22EE 22EF 2318 2325 23CE 2605 2606 2610–2612 2699 270E–2710 2713–2718.
