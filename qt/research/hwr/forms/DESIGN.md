# Handwriting forms: copy-out documents for training data and an end-to-end benchmark

A **form** is a PDF that asks the writer to copy printed text by hand into marked boxes, at given sizes and angles,
and to draw a few diagrams, marks and formulas. Opened in xournal-qt and written on with the pen, it becomes:

1. **training data** of that writer's hand, with exact transcriptions (every box has one expected text), and
2. **an end-to-end benchmark** of the whole handwriting search: layout (lines, words, angles), text against drawings,
   recognition, the search, the copy tool and the PDF text layer, per category and per angle.

Because the boxes and their texts are known in advance, nobody has to transcribe anything afterwards. The same form,
written by many people, can later become a community dataset (see "Contributing").

## Principles

- **One box, one expected text.** A box holds one line (or one word, or one formula). Its ground truth is in a
  manifest, not guessed from the PDF.
- **Copy, don't trace.** The prompt is printed beside or above the box, never inside it, in the writer's normal hand.
  The printed page never reaches the recogniser: xournal-qt reads only the pen's strokes, so guide lines and prompts
  cannot leak into the training data.
- **Light guides.** A box is a thin grey outline; normal boxes have a faint baseline. Some boxes have no guides at
  all, because real notes have none.
- **Real use, not only clean lines.** Margin notes beside a printed paragraph, a word squeezed between lines, a
  circled word, a labelled diagram, a formula: what people actually write onto PDFs.
- **Every axis the pipeline must cope with, measured separately:** size, angle, character class, layout context,
  text against non-text.
- **Self-contained.** The manifest is embedded in the PDF (an attached file), so a filled form carries its own
  ground truth. A visible id on every page (`XQT-HWR-EN v1 · page 3/8 · box 3.12`) ties boxes to the manifest.
- **Short enough to finish.** The core form takes about 30 minutes; everything else is optional.

## Size: how much is enough

- **Personal fine-tuning:** studies of writer adaptation show clear gains from 16 to 50 lines and little after
  about 100; the core form gives about 120 boxes of text.
- **Benchmark per writer:** a rate per category needs 20 or more items to be stable to a few points; each category
  below has 20 to 40 items, and each angle class 4 to 8 lines.
- **Effort:** about 4 pages of plain copying (about 20 minutes) plus 3 to 4 pages of angles, layout, drawings and
  formulas (about 15 minutes). The German chapter adds about 15 minutes.
- **Community dataset:** about 50 writers would roughly double the real handwriting the shared model is trained on
  (fhswf has 15 writers, CVL 310 who copied the same 7 texts).

## Structure

Every page: the id line, a title, a short instruction. A4. Sizes are letter heights (x-height, mm) of what the writer
is asked to write; box heights follow from them.

| Section | Pages | Content | Measures |
| --- | --- | --- | --- |
| 0. Start | 1 | what the form is for, how to write (own hand, no tracing, one box one line, mistakes: strike out and write on), the licence choice for contributions, three warm-up lines | — |
| A. Characters | 1 | the alphabet in lower and upper case, digits, punctuation and symbols (`.,;:!?'"()[]{}-–/\@#%&*+=<>€$§°`), pangrams; confusable groups (rn m, cl d, 0 O o, 1 l I \|, u v, a o, 5 S, 2 Z, g q 9) as single words | character coverage; CER per character class |
| B. Running text | 2 | 24 lines of ordinary sentences, normal size, on guides; the vocabulary of lecture and meeting notes; numbers, dates, times, units, abbreviations, an e-mail address, a URL | CER, WER, words found (as the training measures) |
| C. Search words | 1 | 40 single words and short phrases in boxes of 4 sizes (small 2 mm, normal 3 mm, large 6 mm, headline 10 mm): names, technical terms, acronyms, long words, mixed case, words with digits | words found by the search, per size |
| D. Angles | 2 | lines and short notes at 0°, ±15°, ±30°, ±45°, ±60°, ±90°, ±135° and 180°, each with a printed arrow for the writing direction; vertical notes along the page edge; 1-, 2- and 4-word notes at ±90° (short notes are the hard case); a rotated label inside a box | lines found at the right angle, CER and words found per angle |
| E. On a page | 1 | a printed paragraph to annotate: margin notes left and right (small), an insertion between two printed lines, a short note at the paragraph's end, a bullet list, a two-column note, a table of 3 × 3 cells to fill | layout in context: margin notes kept apart from the body, no merged lines |
| F. Drawings and marks | 1 | a flow chart of three labelled boxes with arrows (labels are text, the boxes and arrows are not), a circled word, an underlined word, a struck-out word, a sketch area ("draw a small house", no text), a hatched area, an axis plot with axis labels | text against drawing: labels found, no text read in drawings and marks |
| G. Formulas | 1 | 12 formulas of rising difficulty (`a^2 + b^2 = c^2`, a fraction, a sum, an integral, a matrix, Greek letters, `f(x) = ...`, a chemical formula, units like `3,5 kΩ`) printed typeset, to be written as one would | formulas kept out of text, or read where the alphabet allows (benchmark only, not training) |
| H. German chapter (optional) | 2–3 | sections A to D in German: umlauts and ß, German sentences, compounds (`Kalman-Verstärkung`, `Zwischenprüfung`), German quotes „…“, and a smaller angle set | the same, for German |

Texts must be free to use: the project's own sentences, or public domain. Words for section C are chosen to be
distinctive in a library (a search for them should find this form and nothing else).

## The manifest

`<form>.manifest.json`, also embedded in the PDF as an attached file of that name:

```json
{
  "form": "xqt-hwr-en", "version": 1, "language": "en", "page_size_mm": [210, 297],
  "pages": 8,
  "items": [
    {
      "id": "3.12", "page": 3, "section": "C", "kind": "word",
      "text": "Kalman", "lang": "en",
      "box_mm": [25.0, 140.0, 60.0, 14.0], "angle": 0, "x_height_mm": 6,
      "guides": true, "tags": ["name", "large"], "search": ["Kalman"]
    }
  ]
}
```

- `box_mm`: x, y, width, height of the box upright, in mm from the page's top-left; `angle`: the writing direction
  in degrees, clockwise on the page (0 left to right, 90 downwards, -90 upwards, 180 upside down), the box turned by
  it around its centre. This is the convention of `ink::Word::angle` in the app.
- `kind`: `line`, `word`, `chars` (a character set, read character by character), `number`, `label` (text inside a
  drawing), `math` (`text` holds the formula as written in plain characters, `latex` the typeset one), `drawing`
  (no text expected), `mark` (underline, circle, strike-out: no text), `free` (not checked).
- `search`: the words a search should find in that box (default: its words of 3 or more letters).
- A box may hold `context`: the printed text it belongs to (a margin note's paragraph), for the layout measures.

As built (`build.py`, see [README.md](README.md)), with these additions and details:

- `x_height_mm` is `null` for `drawing`, `mark` and `free` boxes; `latex` is there for `math` only.
- `in` (optional): the id of the box this one lies inside. Only a `drawing` or `mark` box holds others: the labels of
  the flow chart inside its drawing area, a written word inside the area it is to be circled in. Text read inside
  such a box's children is theirs and does not count as text read in the drawing or mark.
- `context` is also given for table cells (the printed table), the labels inside the bar chart (its caption) and the
  marks on printed words (the printed word).
- The combined form `xqt-hwr-en-de` has `"language": "en"` (its main language); its German chapter's boxes have
  `"section": "H"`, `"lang": "de"` and a tag naming their German section (`de-A` to `de-D`).
- Tags used: the size from the x-height (`small` 2, `normal` 3, `medium` 4, `large` 6, `headline` 10 mm), what a text
  is (`name`, `place`, `term`, `acronym`, `long`, `mixed-case`, `digits`, `date`, `time`, `units`, `email`, `url`,
  `quotes`, `compound`, `umlauts`, `pangram`, `confusable`, …), where it is (`edge`, `margin`, `margin-left`,
  `margin-right`, `insertion`, `end-note`, `column-1`, `column-2`, `table`, `flowchart`, `plot`, `axis-label`), the
  short notes at ±90° (`one-word`, `two-words`, `four-words`), and the marks (`circle`, `underline`, `strike`,
  `printed-word`, `circled`, `underlined`, `sketch`, `hatching`, `tick`, `licence`).
- The page id line reads `XQT-HWR-EN v1 · page 3/13`; the box id (`3.12`) is printed small at the end of each box's
  prompt row.
- The forms came out longer than planned above: English 15 pages (238 boxes, 211 with text), German 6 pages (87),
  combined 20. Section D has at least 6 boxes of text at each angle in each direction (lines, and notes of one, two
  and four words, some with digits; 3 pages), the German chapter at least 4 at 0, ±45, ±90 and 180. The effort
  estimate above is untested: time a real fill before relying on it.

## From a filled form to data and numbers

1. **Write:** open the PDF in xournal-qt, write with the pen, save (`.xopp` beside the PDF, or the PDF with notes).
2. **Export training data:** `xournal-qt-cli hwr-form filled.xopp --out <dataset>`: reads the manifest from the
   background PDF, takes the strokes of each box (most of a stroke's points inside the box, in the box's own turned
   frame), and writes an ink dataset (`FORMATS.md` §1, `kind: ink`) with each box's text, angle and kind. `drawing`,
   `mark` and `free` boxes are left out of the training data.
3. **Benchmark:** `xournal-qt-cli hwr-bench filled.xopp [--model <folder>]`: runs the app's own pipeline on the
   whole pages, without the boxes (layout, angles, the recogniser, the search), then compares with the manifest:
   lines found per box and their angle, CER and words found per box, search recall for each box's `search` words,
   text read inside `drawing` and `mark` boxes (should be none), and false hits. It writes a report (JSON and a
   Markdown table) per section, kind, size and angle, so models, versions and settings can be compared on the same
   ink.

## Contributing (later)

- A filled form plus a short `contribution.json` (writer id chosen by the writer, hand, device and pen, languages,
  and the licence) is enough. A menu entry could package it later ("Contribute this handwriting sample").
- **Licence of contributions: the author's decision.** CC0 or CC BY 4.0 would let future models train without
  non-commercial data and drop the non-commercial note of the current model.
- Splits stay by writer; a contributed writer is in train, val or test as a whole.
