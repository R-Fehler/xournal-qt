# Handwriting forms

PDFs to fill in by hand in xournal-qt: every box asks for one known text (or a drawing or mark) at a given size and
angle, so a filled form is training data of one's own hand and an end-to-end benchmark of the handwriting search.
What a form contains and why, and the manifest format: [DESIGN.md](DESIGN.md).

| Form | Pages | Boxes | What |
| --- | --- | --- | --- |
| [`pdf/xqt-hwr-en.pdf`](pdf/xqt-hwr-en.pdf) | 13 | 196 | English: start, A characters, B running text, C search words, D angles, E notes on a page, F drawings and marks, G formulas |
| [`pdf/xqt-hwr-de.pdf`](pdf/xqt-hwr-de.pdf) | 5 | 79 | German: start, A Zeichen, B Fließtext, C Suchwörter, D Winkel |
| [`pdf/xqt-hwr-en-de.pdf`](pdf/xqt-hwr-en-de.pdf) | 17 | 266 | the English form, then the German one (without its start page) as chapter H |

Each PDF carries its manifest as an attached file (`<form>.manifest.json`, also next to it in `pdf/`):
`pdfdetach -list`, or `python3 pdfread.py pdf/xqt-hwr-en.pdf`.

## Filling a form in xournal-qt

1. Open the PDF in xournal-qt (it becomes the background of a new note).
2. Write with the pen: copy each printed text once into its box, in your normal hand, at the size of the box; follow
   the arrows of turned boxes (turn the page or the tablet if that helps). Draw, circle and underline where the
   form asks. A mistake: strike it out and write on, or erase it.
3. Save (`.xopp` beside the PDF, or the PDF with the notes).

The printed text never reaches the recogniser: only the pen's strokes are read, matched to the boxes by the
manifest. Turning a filled form into a dataset and a report is the app's side (`xournal-qt-cli hwr-form` and
`hwr-bench`, DESIGN.md "From a filled form to data and numbers").

## Rebuilding

```sh
python3 build.py                      # all forms into pdf/, then the checks below; non-zero on a problem
python3 build.py xqt-hwr-de --png /tmp/forms --dpi 80   # one form, every page also as PNG to look at
python3 -m pytest                     # the checks as tests (builds every form once, about 35 s)
```

Needs Python 3 with PyYAML (for the tests also pytest: `~/miniforge3/envs/xqt-hwr` has both) and TeX Live's
`lualatex` with fontspec, TikZ, embedfile and hyperref, and the DejaVu fonts. The build is reproducible: the same
content gives the same PDF bytes. **After changing the content or the layout, rebuild and commit `pdf/`**
(`test_committed_files_are_current` fails otherwise).

| File | What |
| --- | --- |
| `content/en.yaml`, `content/de.yaml` | the content: sections, blocks of items (text, kind, x-height, angle, guides, tags, search) |
| `layout.py` | where everything goes: the blocks (lines, rows, angle groups, the annotated paragraph, table, flow chart, marks, plot, formulas, …) placed on A4 pages in mm; every box and printed text a polygon |
| `tex.py` | the LaTeX: one TikZ overlay per page on `current page` (`remember picture, overlay`), the manifest embedded with `embedfile` |
| `build.py` | lays out, writes the manifest and the `.tex`, runs `lualatex` twice, copies to `pdf/`, checks |
| `checks.py`, `test_forms.py` | the checks: boxes and texts inside the printable margin (10 mm; turned boxes by all four corners), no two boxes overlapping (turned: polygons), no printed text on a box or on another text, every text in the room it was given (TeX measures each one and writes its size into the log), no overfull box or missing glyph, the manifest's fields and ids, every angle with enough lines, the manifest read back from the PDF |
| `geometry.py`, `pdfread.py` | turned boxes and polygon overlap; reading the embedded manifest back with the standard library |

Writing a new section: add a block to the content (`type:` one of the `BLOCKS` in `layout.py`), rebuild with
`--png` and look at the pages; the checks catch overlaps and texts that do not fit, not ugliness. Box sizes follow
the x-height: a box is 3.3 × the x-height high (normal lines 10 mm, small notes 7 mm, large 20 mm, headlines 33 mm)
and as wide as a generous estimate of the handwriting needs.

## Contributing (later)

A shared dataset of many hands is planned (DESIGN.md "Contributing"); the licence for contributions is the author's
decision, and the start page only asks the writer to tick one. Until then, the forms are for one's own data.
