# Handwriting search

Handwritten notes become searchable: Ctrl+F in a document, the library's search and other PDF apps find words you
wrote by hand. The handwriting is **never turned into text** in the document — the ink stays as it is; a recogniser
reads it in the background and the search looks at what it read. What it read can be **copied as text** to the
clipboard (below).

## Using it

1. **Settings → Search → "Search handwriting"** (off until you switch it on).
2. **Handwriting languages**: English, German, or English and German (the default). Each language needs its model;
   Settings shows, per language, whether it is there, where it comes from and how big it is. **Download** fetches
   it once into `~/.local/share/xournal-qt/models/<name>/` (with a progress bar; Cancel and a later Download go on
   where it stopped), **Remove** frees the space again, **Choose a folder…** uses a model of your own. Nothing is
   downloaded unless you press the button. English is TrOCR-small (about 64 MB); the German model is the project's
   own and says "not published yet" until it is (meanwhile a folder holding one can be chosen).
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

## Languages and models

- A model is a folder with a manifest `model.json` (its `kind`: `trocr` or `ctc`, its `languages`, its files with
  sha256 and size; [FORMATS.md](../../research/hwr/train/FORMATS.md) §2). Per language the app takes the folder chosen
  in Settings, else `XQT_HWR_MODEL` (English) / `XQT_HWR_MODEL_DE` (German), else its own in
  `~/.local/share/xournal-qt/models/` (`trocr-small-hw-int8`, `crnn-de`); without one there, any model in that folder
  that reads the language. A model that reads both languages serves both.
- With English and German, **both models read the handwriting** and their readings of each ink word go into one list:
  a word is found through either. Nothing is transcribed. A reading both models gave is there once, with the
  probability that one of them is right, 1 − (1 − p₁)(1 − p₂) (two models agreeing make a word surer than either
  alone; the search's ranking and its marking of unsure words take that probability). Each reading keeps the model, and so
  the language, that gave it.
- The results are named by the set of models: another set (a model added, another language) reads the library again.

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
  (the ink itself, in points relative to the line) and a line of `lines.jsonl` (`id`, `image`, `text`, `lang`,
  `writer`, `strokes`, and the `page` and `line` it came from). `dataset.json` names the set (`kind` `ink`).
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
- **ONNX Runtime** is not linked: only its C API's headers are vendored (`qt/3rdparty/onnxruntime`), and
  `libonnxruntime.so.1` is opened when the search is switched on (`XQT_ONNXRUNTIME`, else next to the program, else
  the system's). Without it, Settings says so and nothing else changes. Packages will bundle it later.
- Tests that need the runtime or the model are skipped unless `XQT_ONNXRUNTIME` (tiny models in
  `qt/tests/hwr/data`: a TrOCR stand-in and a CTC model, made by `tinytrocr.py` and `tinyctc.py`) or
  `XQT_HWR_MODEL` is set; `XQT_BENCH_HWR=1` with the model prints the time per line. English and German are tested
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
- **The model is downloaded on demand** (the author's choice): the app ships without it, and Settings offers the
  download once the search is switched on, with the address and size shown first (as the arXiv search does: opt-in,
  the address in view), into the app's data folder, checked against sha256s pinned in the app
  (`qt/src/shell/ModelDownload.cpp`), retryable and cancellable, with "Remove the model". The source is a pinned
  revision of `huggingface.co/Xenova/trocr-small-handwritten`. **Until the revision and the sha256s are pinned** (run
  `qt/scripts/hwr-model.sh`, which prints them), the button says the model must be installed with the script.
- The runtime is opened at run time (dlopen); bundling it in the packages is a later step.

### English and German

- Two models from the start (the author: "for the app we start with two models"), both run, their readings merged
  for the search: one list per ink word, readings both gave combined by noisy-OR (not the maximum: two models
  agreeing is evidence; not a sum: it goes over 1).
- "English and German" is the default; a language whose model is missing is read by the other model meanwhile, and
  Settings says so.
- The German model is pinned in the app once it is published (`ModelDownload.cpp`, `builtIn()`); until then its
  download says "not published yet" and a folder can be chosen.
- A document's language: both models on its first 6 lines, then the clearly surer one, the other on unsure lines;
  revisited when the confidence drops; the user's choice per document wins. Kept in the library's cache, not in the
  `.xopp` (there is no clean place in the file: upstream would drop an unknown attribute on its next save).
- The dataset export marks the writer's own lines `private` and noncommercial unless a licence is given.
