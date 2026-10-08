# Handwriting search

Handwritten notes become searchable: Ctrl+F in a document, the library's search and other PDF apps find words you
wrote by hand. The handwriting is **never turned into text** in the document — the ink stays as it is; a recogniser
reads it in the background and the search looks at what it read. What it read can be **copied as text** to the
clipboard (below).

## Using it

1. **Settings → Search → "Search handwriting"** (off until you switch it on).
2. **Handwriting languages**: English, German, or English and German (the default). The app **comes with a model
   that reads both** (see "The built-in model"): Settings says "Built in, in use (<name>, <size>)" per language,
   with its **Licence** note one click away, and nothing needs to be downloaded. **Choose a folder…** uses a model of
   your own for a language instead. A language no built-in model reads offers its model's **Download** (once into
   `~/.local/share/xournal-qt/models/<name>/`, with a progress bar; Cancel and a later Download go on where it
   stopped) and **Remove**; nothing is downloaded unless you press the button. An English TrOCR-small model
   downloaded before (about 64 MB) keeps reading English, next to the built-in model for German, until it is
   removed.
3. Open a document with handwriting: after a few seconds (the page you look at first) its words are found by
   Ctrl+F. The box around the ink word is marked; a word the recogniser was unsure of is marked lighter.
4. The library's search finds documents by their handwriting too. A document found only through readings the
   recogniser was unsure of is listed after the others.

What to expect:
- With English only, German handwriting is found about half as often; with the German model too, both are found
  (see "Languages and models").
- The search tolerates the recogniser's mistakes: it looks at several readings of each word ("Kalmar" and "Kalman"),
  and a word of 5 or more letters is found with a typo even with Fuzzy off. With Fuzzy on, `klmn` finds "Kalman" as
  in typed text.
- Words of 1–2 letters are only found where the recogniser was sure (else "a" or "to" would be everywhere).
- Drawings, arrows, underlines and filled shapes are left out; highlighter strokes never count.
- Search terms of several words ("dumb test") find consecutive handwritten words, also across a line end.

## Handwriting at an angle

Text written at an angle or along the margin (a note written upwards along the edge of a PDF slide, a label along an
arrow, a line written steeply uphill) is found, marked, copied and put into the PDF text layer like level text.

- **What counts**: a run of strokes written one after the other close together (a few words: gaps up to 4 times the
  strokes' typical smaller side), long and narrow enough (at least 6 times that side and 3 times as long as wide; not
  two strokes alone, an i and its dot), whose strokes follow each other along it, has a direction. Letters written in
  pieces (stems, bars, a "Th" in one stroke) and crossings written afterwards still count; a list of words one below
  the other does not. Within 10° of left to right it is laid out as level text (slopes are fine), and within 20°
  unless it climbs by more than its own height (a long line written uphill, which the page's rules would cut into
  pieces: it keeps its angle). 70°–110° either way counts as exactly 90°: **written downwards** (90°) or **upwards**
  (270°, -90°), 160°–180° as exactly 180°: **upside down** (written leftwards, a note for the person across the table, a
  page turned while writing). Between 20° and 70° and between 110° and 160° either way the line keeps its own angle.
- **How it is read**: the strokes of each direction are turned upright and laid out by the same rules as a page (lines,
  words, drawings left out, in the frame's own units), so the recogniser gets the line upright. Pieces too short to
  have a direction (a word written after a pause, a "This" apart from the rest) join the line they lie on whole, and
  dots, accents and straight strokes (a T's bar) the line or piece they lie on; whether a straight stroke is a drawing
  is decided in the frame.
- **Level text is unchanged**: a page without writing at an angle comes out exactly as before (the same lines, words
  and hashes; `InkLayoutTest.theBenchmarkPageIsLaidOutAsBefore`). On a page with both, the level text's units leave the
  strokes at an angle out (they are as tall as they are long).
- **The results**: a line keeps its angle; its words are boxes in its upright frame, so a line moved with the lasso
  keeps what was read in it, and the same words written at another angle are the same line to the cache. Turned with
  the lasso, a line at a free angle keeps its result while it stays in its range; a level line or one at 90° or 180°
  turned by the lasso is read again. The angle is kept in the library's pack (older packs: level).
- **Marked** with the box turned like the ink; **copied as text** as one line, a paragraph of its own (empty lines
  around it), placed among the other lines by the top of the box around it; a sweep takes its words by their turned
  boxes. **The PDF text layer** turns each word with its text matrix: viewers select along the ink. Poppler (Okular,
  Evince) reads words written up or down as words; at a free angle it takes the letters one by one ("K a l m a n").
- **Limits**: a column of single letters or digits, one below the other, may be taken for a line written downwards
  (geometry alone cannot tell them apart; a list of words is not).
- Code: `InkLayout.h` (`framesOf`, `InkLine::angle` / `upright`), `Recognizer.cpp` (`LineInput::of` turns the line
  upright), `InkText.h` (`PlacedLine::angle`, `Word::angle`, `quadOf`, `quadsOf`), `DocumentSearch::Place::quads`,
  `DocumentCanvasItem::updateSearchHits`, `InkCopy.cpp`, `InkTextLayer.cpp`, `InkTextStore.cpp`. Tests:
  `InkRotationTest.*` (`-L hwr`: 0°, ±15°, ±35°, 90°, 270°, 180°, ±135°, a mixed page, a list, the benchmark's real
  lines at ±90°, ±45°, 30°, ±15°, 180° and ±135° and lists of their words, the hash, the scripted recogniser, marks, copying, poppler), `InkSearchTest.handwritingAtAnAngleIsMarkedTurned`,
  `InkLibraryTest.theAngleOfALineIsStored`, `CanvasItemRenderTest.aHitInHandwritingAtAnAngleIsMarkedTurned` (`-L quick`).
- **Measured**: `HwrRotationBenchmark` (`XQT_HWR_ROTATION_BENCH=<folder>` and `XQT_ONNXRUNTIME`, about 30 s) turns the
  benchmark's lines that stand alone (`qt/tests/hwr/BenchmarkInk.h`) to 90°, -90°, ±45°, 30°, 15°, ±135° and 180°, alone
  and as a page, reads them with the built-in model and compares with the level reading: lines found, character error
  rate, words the search finds; also lists of the same words and a margin note on a page. The table and every line's
  picture go to the folder.

## Copy handwriting as text

The author: "copy handwritten text but make it its own tool similar to select pdf text (could be a cycle)
to not interrupt the ink annotation flow. Or maybe when selecting inked words with the select tool offer to copy the
text in addition to normal copy and give the user a small popup with the text that is now in the clipboard."

- **The tool**: the mark-PDF-text button is a cycle, mark PDF text ↔ **copy handwriting as text** (a tap while one is
  in use takes the other; held: both, and how PDF text is marked); **Shift+T**; on a phone, "My tools". Then one sweep
  over the words (drawn like the lasso: along a line, a loop around a paragraph, or a tap on one word) copies them,
  and the tool in hand before comes back at once: the pen writes on while the words are read. A sweep takes the words
  whose box it touches (3 pt around them) or whose middle it encloses.
- **"Copy as text"** beside Copy on the selection's pill, when the selection holds handwriting (pen strokes): every
  word of it. The selection stays.
- **The text**: the best reading of each word, in reading order: lines from the words' heights (a line written uphill
  or with tall letters stays one), top to bottom, words left to right; words by spaces, lines by line breaks, and an
  empty line where two lines are much further apart than the others (a paragraph). Only the clipboard gets it: nothing
  is written into the document.
- **The card**: near the words (above them, or below where there is no room) "Copied as text" with the text, which
  can be selected (and copied again in part) but not edited: the clipboard has it as it is shown. Words the
  recogniser was unsure of (below 0.5, as the search's lighter marks) are grey, with a note. Long texts scroll. It
  hides after 5 s plus 40 ms per letter (at most 15 s), not while the pointer is on it or text in it is selected; ×
  closes it. While lines not read before are read: "Reading the handwriting…" (after 300 ms; words read before come at
  once).
- **Not read yet**: the lines under the sweep (or of the selection) are read on demand, by the search's worker as a
  job the user waits for: it goes before the pages queued for the search, a page being read gives way after its line
  (and goes on afterwards), it waits neither for the pages in view nor while the user writes, and it reads only the
  lines of the sweep's area. What it reads is kept in the line cache for the search as well.
- **The search off**: the tool is not armed; the note says "Copying handwriting as text needs the handwriting search
  (Settings → Search)" with **Settings**, which opens Settings at Search. **No model** (and nothing read before): "No
  handwriting model to read it with (Settings → Search)", the same button. Lines left out for want of a model: the
  card says so.
- Code: `hwr/InkCopy.h` (the words a sweep takes, reading order), `InkRecognitionService::Job::area` / `urgent`,
  `app/AppInkCopy.cpp`, `Snip.h` (`snip::Purpose::InkText`: armed like a snip, the lasso), `CanvasView::inkSwept`,
  `InkTextToast.qml`, `ToolGroups.qml` (`text`). Tests: `InkCopyTest.*` (`-L hwr`: lines and reading order, the
  sweep, the urgent jobs), `CopyToolsTest.*` (`-L ui`, with the scripted recogniser: the tool and the pen back, the
  card and the unsure word, the pill's entry, the search off and no model).

## The built-in model

The author (2026-10-08): "ship the small model with the next pre-release … bundle it, not download on demand … the
pipeline should be somewhat model agnostic: if the big model is better we just swap model and weights"; the licence:
"ship it with its own licence note".

- **What**: the project's own CTC model for German and English (`qt/resources/hwr/<name>/`: `model.json`,
  `model_int8.onnx`, `alphabet.txt`, `LICENCE.md`; about 9 MB). The build copies every folder there that holds a
  `model.json` into the resource dir as `hwr-models/<name>/` (`XqtHwr.cmake`; installed with `share/xournal-qt`;
  where that is per platform: [data-on-disk.md](../architecture/data-on-disk.md)). On Android the folder travels in
  the APK's resources and is copied to the app's data folder at start (`AndroidSetup.cpp`), where ONNX Runtime opens
  it by its path; a model whose `model.json` changed is copied anew.
- **Model agnostic**: no code names the model. The app scans `<resource dir>/hwr-models/*/model.json`
  (`HandwritingSearch::bundledModels`) and takes for each language a model that reads it. Another model (the bigger
  one, a third language) is a swap of the folder in `qt/resources/hwr/`; a model the manifest marks
  `"noncommercial": true` says so in Settings and About.
- **Checked as a downloaded one**: the recogniser checks the files against the manifest's sizes before it reads and
  against their sha256 when it loads the model (`CtcRecognizer`); a damaged file is said in Settings and read no
  further.
- **Licence**: the weights are for non-commercial use (trained on IAM and CVL, whose terms are non-commercial); the
  note `LICENCE.md` ships beside the model, shown from Settings → Search (Licence) and Settings → Help → About, which
  says the model is for non-commercial use and the app's code stays under the GPL.
- **ONNX Runtime** is looked for where each platform's package puts it (`ort::candidates`, `OrtRuntime.h`):
  `XQT_ONNXRUNTIME` when set; Linux `<prefix>/lib/xournal-qt/libonnxruntime.so.1`; Windows `bin\onnxruntime.dll`
  next to the program; macOS `Contents/Frameworks/libonnxruntime.1.dylib` in the bundle (then the Linux-style places);
  Android `libonnxruntime.so` by its name (the ONNX Runtime AAR's library among the APK's native libraries); last
  the system's by its name.
- **`xournal-qt --hwr-info`** (for the packages' smoke tests, like `--audio-info`) prints one fact per line and exits
  with 0 when the search can read, else 1:

  ```
  onnxruntime: found (/usr/lib/xournal-qt/libonnxruntime.so.1, version 1.20.1)
  models: /usr/share/xournal-qt/hwr-models
  model: <name> (ctc; de, en; version 2026.10.1; non-commercial) in /usr/share/xournal-qt/hwr-models/<name>
    reads the sample line: "Hallo" (126 ms)
  hwr: ready
  ```

  Without the runtime: `onnxruntime: not found (<why>)` and `  cannot read the sample line (<why>)`; without a
  model: `models: none in <folder>`. The sample line is the word "Hallo" written with eight strokes, built into the
  app (`hwr::sampleLine`, `HwrInfo.cpp`).
- Tests: `BundledModelTest.*`, `OrtRuntimeTest.*` (`-L hwr`; with `XQT_ONNXRUNTIME`: the sample line, a damaged
  file, the benchmark's real handwritten line read by the built-in model), `ModelDownloadTest.aBuiltInModelComesLastInTheLookup`,
  `ModelDownloadTest.theBuiltInModelReadsWhatHasNoModelOfItsOwn`, `MainWindowTest.settingsSearchTabSwitchesTheHandwritingSearch`,
  `MainWindowTest.aboutSaysTheHandwritingModelIsForNonCommercialUse` (`-L ui`).

## Languages and models

- A model is a folder with a manifest `model.json` (its `kind`: `trocr` or `ctc`, its `languages`, its files with
  sha256 and size; [FORMATS.md](../../research/hwr/train/FORMATS.md) §2). Per language the app takes the folder chosen
  in Settings, else `XQT_HWR_MODEL` (English) / `XQT_HWR_MODEL_DE` (German), else its own in
  `~/.local/share/xournal-qt/models/` (`trocr-small-hw-int8`, `crnn-de`); without one there, any model in that folder
  that reads the language; else a model that comes with the app and reads it (above). A model that reads both
  languages serves both (read once per line).
- With English and German, **both models read the handwriting** and their readings of each ink word go into one list:
  a word is found through either. Nothing is transcribed. A reading both models gave is there once, with the
  probability that one of them is right, 1 − (1 − p₁)(1 − p₂) (two models agreeing make a word surer than either
  alone; the search's ranking and its marking of unsure words take that probability). Each reading keeps the model, and so
  the language, that gave it.
- The results are named by the set of models: another set (a model added, another language) reads the library again.

### How readings get their word boxes

The layout finds the lines and the word boxes from the strokes alone (`InkLayout`); a model reads a whole line (or a
piece of it) as text, in a few readings (the beams of its search). Each reading's words are put on the boxes
(`WordAlignment`): one to one when it has as many words as the line has boxes, else each word to the box it overlaps
most in x (the words of one box joined with a space; a box may get none). Where a word is:
- **a CTC model** (the German model): where the model read it. Each of its output frames is a slice of the line's
  picture (a quarter of its width in pixels: frame t of T covers t / T to (t + 1) / T), the beam search keeps per
  character the first and last frame it was read on, and a word spans from its first character to its last, mapped
  back through the picture's scale and margin to the page;
- **TrOCR** (no positions): estimated, the words share the line's width by their letters and the boxes by their widths.

The letters' shares misplace a short word written wide ("a wonderful": the "a" gets a tenth of the width), a box the
model read nothing in (a drawn mark the layout kept), and a word the layout split in two. Measured on the handwriting in
`test/files` with the German model (2026-10-08): of 439 lines, the best reading had another number of words than boxes
in 14; the two ways differ in 11 of them. Looking at the 13 distinct lines where any reading differed: the frames put
the words on the right boxes in 9 ("This is a" all on the box of "This" by the letters, each on its own by the frames;
"Theis is" likewise), the letters in none, 4 could not be told (ink written over itself). The frames also give all
readings of a line the same boxes, so their shares add up, where the letters' shares move with the spelling. (The
German model's recogniser id ends in `ctc2` since: lines it read before are read once again.)

### Which language a document is in

Reading a line costs about 0.2 s per model, and most documents are in one language. So with both models in use:
- both read the **first 6 lines** of a document; the model whose words are clearly surer (its mean confidence 0.15 or
  more above the other's) decides the document's language;
- from then on only that model reads every line; the other reads only the lines it is **unsure** of (below 0.5), so
  German words in an English document are still found;
- when the preferred model becomes unsure of half of the last 12 lines (the document goes on in the other language,
  or someone else wrote), both read again and the language is decided anew. A document both models read alike (a
  mixed one) keeps both.
- **⋮ → Document → Handwriting language**: Automatic, English, German or Both, for this document. A new choice reads
  only what the chosen model did not read yet.
- The decision and the choice are kept in the library's cache next to the results (`ink-text.pack`), not in the
  `.xopp`. The library's vocabulary is not used for the decision: it is one dictionary for all languages.

## Where the results are kept

- **Open documents**: in memory, per page; an edit reads only the line it touched, about two seconds after you stop
  writing. Moving a line with the lasso reads nothing again.
- **The library's cache**: `.xournal_library/ink-text.pack` in each folder (or the app's cache folder, wherever the
  library keeps its cache), next to the search index. It is written when you save a document and by the background
  reading; opening a document takes what was read before. It also keeps each document's handwriting language (the
  one found and the one chosen). Deleting the cache only means reading again (and choosing the language again).
- **The `.xopp`**: nothing. The file stays as Xournal++ writes it.
- **PDFs with notes and archive PDFs**: an invisible text layer with the best reading of each word the recogniser
  was sure enough of, so Okular, Evince, Firefox, MuPDF and others find and select the words. The other readings
  are only searched in xournal-qt. Plain "Export as PDF" has no text layer yet.

## Work and power

- Reading runs on one thread at the lowest priority (Linux: `SCHED_IDLE`): it only uses a core nothing else wants.
  It waits while pages in view are drawn and while you write.
- The open documents are always read (the one in front first). The rest of the library is read only on **mains
  power**, one document at a time; unplug and it stops within a minute.
- About 0.2 s per line and model, 4–5 s per page of dense handwriting on a laptop; a page is read once. With two
  models a document's first lines cost twice that, then mostly one model reads (see above).
- A model takes about 250 MB (TrOCR) of memory while it reads and is unloaded after a minute without work.

## Your handwriting as a dataset

To train a model on your handwriting, or to measure how well a model reads it, the command line tool writes a
document's handwriting as a **line dataset** in the format the training reads
([qt/research/hwr/train/FORMATS.md](../../research/hwr/train/FORMATS.md), §1, `"kind": "ink"`):

```sh
xournal-qt-cli hwr-lines notes.xopp --out ~/hwr-data/notes [--text transcripts.txt] [--lang de] [--writer me]
```

- Every line of ink the search finds (the same layout of lines and words) becomes `images/<id>.png` (drawn exactly
  as the app draws a line for its recognisers: 128 px high, black ink with round caps on white), `strokes/<id>.json`
  (the ink itself, in points relative to the line; a line written at an angle turned upright) and a line of
  `lines.jsonl` (`id`, `image`, `text`, `lang`, `writer`, `strokes`, and the `page` and `line` it came from). `dataset.json` names the set (`kind` `ink`).
- The ids are `<writer>-<document>-p<page>-l<line>`. Lines are in reading order: pages in order, lines top to bottom.
- `--text`: a text file with one line of text per line of ink, in the same order (empty lines and lines starting
  with `#` are skipped; NFC). When the numbers differ, the texts are matched as far as they go and the tool warns:
  look at the `page` and `line` of the entries to find a sentence written on two lines. Without `--text` the texts
  are empty (a set to read, not to train on).
- `--licence`: the dataset's licence. Default `private`, marked `"noncommercial": true`, so the training does not mix
  your handwriting into a model it publishes unless asked.

**A handwriting sample** to start from: `qt/research/hwr/sample/handwriting-sample-en.xopp` and
`handwriting-sample-de.xopp` have 20 numbered sentences each (grey text, not ink) with room under each one. Open one in
xournal-qt, write every sentence once, on one line, under its prompt, save, and run

```sh
xournal-qt-cli hwr-lines handwriting-sample-de.xopp --text qt/research/hwr/sample/sentences-de.txt --lang de --out ~/hwr-data/sample-de
```

The sentences are the texts (`sentences-<lang>.txt`; `make_sample.py` makes the pages from them again after a change).

## For developers

- Code: `qt/src/hwr` (layout of ink into lines and words, the recognisers — `TrocrRecognizer`, `CtcRecognizer`
  with `CtcDecode`, `MultiRecognizer` for several models, `LanguagePlan` for a document's language —, `ModelInfo`
  (a model's manifest), the worker, the indexer of an open document, `LineDataset` (the export)), `qt/src/session/InkText.h` (what was read and how the search matches it),
  `qt/src/session/InkTextLayer.h` (the PDF text layer), `qt/src/shell/InkTextStore.h` (the library's pack),
  `LibraryInkJob.h` (background reading), `ModelDownload.h`, `HandwritingSettings.h`. Tests: `xqt-hwr-tests`
  (label `hwr`), with a scripted recogniser.
- **The models** come from folders (see "Languages and models"); the settings are `handwritingLanguages` (`en`,
  `de`, `en+de`), `handwritingModel` and `handwritingModelDe` (in the `xournalQt` part of settings.xml).
  `qt/scripts/hwr-model.sh` puts the English model in place and writes its manifest `model.json`; a CTC model of the
  training block brings its own. The recogniser's id is derived from the manifests (results of other models are read
  again); several models' ids are joined with `+`.
- **ONNX Runtime** is not linked: only its C API's headers are vendored (`qt/3rdparty/onnxruntime`), and the library
  is opened when the search is switched on (where: "The built-in model"). Without it, Settings says so and nothing
  else changes. The packages bring it (the packaging's job: `qt/packaging`, the CI).
- Tests that need the runtime or the model are skipped unless `XQT_ONNXRUNTIME` (tiny models in
  `qt/tests/hwr/data`: a TrOCR stand-in and a CTC model, made by `tinytrocr.py` and `tinyctc.py`) or
  `XQT_HWR_MODEL` is set; `XQT_BENCH_HWR=1` with the model prints the time per line; `XQT_HWR_BOXES_OUT=<folder>` with
  `XQT_HWR_CTC_MODEL` draws the lines whose word boxes the two ways above give differently
  (`CtcTest.wordBoxesOfRealInk`, more documents in `XQT_HWR_BOXES_FILES`). English and German are tested
  with scripted models (`MultiModelTest`: merging, the language of a document, the choice).

- **Training models** (German, German + English, a person's own hand): `qt/research/hwr/train` ([README](../../research/hwr/train/README.md)),
  PyTorch on GPUs, exporting the model folders of [FORMATS.md](../../research/hwr/train/FORMATS.md).
- Tests that need the runtime or the model are skipped unless `XQT_ONNXRUNTIME` (a tiny model in
  `qt/tests/hwr/data`) or `XQT_HWR_MODEL` is set; `XQT_BENCH_HWR=1` with the model prints the time per line.

## Decisions

- Off until switched on in Settings; no learning from the user's handwriting; English and German (below).
- Typo tolerance applies to handwriting even with Fuzzy off.
- The library is read in the background only on mains power; open documents always.
- Nothing is stored in the `.xopp`; the PDF text layer has the best reading only (no file of all readings in PDFs).
- **The model is bundled** (the author, 2026-10-08; before: downloaded on demand): the shared German and English
  model ships inside the app with its own licence note (non-commercial), found by its manifest, not by name ("The
  built-in model"). The download stays for a language no built-in model reads, with the address and size shown first
  (opt-in), checked against sha256s pinned in the app (`qt/src/shell/ModelDownload.cpp`), retryable and
  cancellable, with "Remove the model"; the English TrOCR (a pinned revision of
  `huggingface.co/Xenova/trocr-small-handwritten`, `2432e24d`, about 64 MB) is not offered while the built-in model
  reads English (kept simple; one downloaded before keeps reading English until removed). To move to another
  revision, `XQT_HWR_REVISION=<commit> qt/scripts/hwr-model.sh` prints the lines to pin.
- Bundled files are checked like downloaded ones (sizes before reading, sha256 when loading), not trusted blindly.
- The runtime is opened at run time (dlopen), from where the package put it.

### English and German

- Two models from the start (the author: "for the app we start with two models"), both run, their readings merged
  for the search: one list per ink word, readings both gave combined by noisy-OR (not the maximum: two models
  agreeing is evidence; not a sum: it goes over 1).
- "English and German" is the default; a language whose model is missing is read by the other model meanwhile, and
  Settings says so.
- German (and English) come with the app in the shared built-in model; the catalogue's German download entry
  (`ModelDownload.cpp`, `builtIn()`, "not published yet") is only offered where no built-in model reads German.
- A document's language: both models on its first 6 lines, then the clearly surer one, the other on unsure lines;
  revisited when the confidence drops; the user's choice per document wins. Kept in the library's cache, not in the
  `.xopp` (there is no clean place in the file: upstream would drop an unknown attribute on its next save).
- The dataset export marks the writer's own lines `private` and noncommercial unless a licence is given.

## On the device

What only a real device, screen or another app can show; walked before a release from the [device checklist](../testing/device-checklist.md).

- [ ] A margin note written upwards along a slide's edge and a label along an arrow are found and marked over the
      ink, copied as text, and selected along the ink in Okular and Firefox after saving as a PDF with notes.
- [ ] The models download once with consent; the author's own notes in English and German are found by words
      written in them, also in the library search and in other PDF viewers (the invisible text layer).
- [ ] Each package (`.deb`, AppImage, Windows zip, macOS bundle, APK): `--hwr-info` says "hwr: ready" (on Android:
      Settings → Search says "Built in, in use" after switching the search on), and a German and an English note are
      found without downloading anything; Settings → Help → About shows the model's licence note.
