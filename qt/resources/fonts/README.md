# Symbol font for Android

`XqtSymbols.ttf` ("Xournal Qt Symbols") is a subset of DejaVu Sans 2.37 (Bitstream Vera license, see
`LICENSE-DejaVu.txt`; renamed as that license asks of modified fonts) with arrows, geometric shapes and a few
dingbats (✓ ✕ ✎ ☐ ● ⋮ ↵ ← →). Android's UI font has none of them, and the phones' own symbol font is split over two
files with the same family name, of which Qt picks the wrong one, so these symbols showed as empty boxes.
`AndroidSetup.cpp` registers it as the fallback font on Android only (`qt/cmake/XqtAndroid.cmake`).

Made with fontTools from `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`: U+2190–21FF, U+25A0–25FF, and
U+2022 2026 2212 22EE 22EF 2318 2325 23CE 2605 2606 2610–2612 2699 270E–2710 2713–2718.

# Colour emoji font

`XqtEmoji.ttf` ("Xournal Qt Emoji") is Noto Color Emoji 2.051 (noto-emoji 20250818, Unicode 17) from
<https://github.com/googlefonts/noto-emoji> (`fonts/NotoColorEmoji.ttf`), SIL Open Font License 1.1 (see
`LICENSE-NotoColorEmoji.txt`), 10.7 MB. Only its names are changed (fontTools: name IDs 1, 3, 4, 6; the glyph
tables are byte for byte the original), as the license asks of modified fonts and so that no system font of the
same family takes its place: Android 15's own "Noto Color Emoji" has no flags (they are in a second file), and
fontconfig picked it over the app's copy, so 🇩🇪 showed as two letters in boxes.

Markdown, text boxes, the PDF exports, print and the previews draw emoji with it (`qt/src/markdown/EmojiFont.h`):
it is added to fontconfig for the app's process only (Linux: at start, `AppContext`; Windows and Android: in the
fonts.conf the app writes) with a rule that takes emoji from it first, and to Qt's font database. Nothing is
installed.

Why this format: it is the CBDT version (PNG bitmaps, 136 × 128 pixels). The COLRv1 version is 4 MB, but only
Cairo 1.18 draws it, and Ubuntu 22.04 / KDE neon have Cairo 1.16, where COLRv1 emoji are empty. Debian 13 (Cairo
1.18.4), MSYS2 (1.18.4) and the Android vcpkg baseline (1.18.6) could draw either; one font for all keeps them alike.
