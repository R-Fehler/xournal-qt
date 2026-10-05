# Handwriting search

Handwritten notes become searchable: Ctrl+F in a document, the library's search and other PDF apps find words you
wrote by hand. The handwriting is **never turned into text** — the ink stays as it is; a recogniser reads it in the
background and the search looks at what it read.

## Using it

1. **Settings → Search → "Search handwriting"** (off until you switch it on).
2. The recogniser needs its model (TrOCR-small, about 64 MB). Settings shows where it comes from and how big it is;
   **Download the model** fetches it once into `~/.local/share/xournal-qt/models/trocr-small-hw-int8/` (with a
   progress bar; Cancel and a later Download go on where it stopped). **Remove the model** frees the space again.
   Nothing is downloaded unless you press the button.
3. Open a document with handwriting: after a few seconds (the page you look at first) its words are found by
   Ctrl+F. The box around the ink word is marked; a word the recogniser was unsure of is marked lighter.
4. The library's search finds documents by their handwriting too. A document found only through readings the
   recogniser was unsure of is listed after the others.

What to expect:
- **English only** for now (German handwriting is found about half as often; a German model is planned).
- The search tolerates the recogniser's mistakes: it looks at several readings of each word ("Kalmar" and "Kalman"),
  and a word of 5 or more letters is found with a typo even with Fuzzy off. With Fuzzy on, `klmn` finds "Kalman" as
  in typed text.
- Words of 1–2 letters are only found where the recogniser was sure (else "a" or "to" would be everywhere).
- Drawings, arrows, underlines and filled shapes are left out; highlighter strokes never count.
- Search terms of several words ("dumb test") find consecutive handwritten words, also across a line end.

## Where the results are kept

- **Open documents**: in memory, per page; an edit reads only the line it touched, about two seconds after you stop
  writing. Moving a line with the lasso reads nothing again.
- **The library's cache**: `.xournal_library/ink-text.pack` in each folder (or the app's cache folder, wherever the
  library keeps its cache), next to the search index. It is written when you save a document and by the background
  reading; opening a document takes what was read before. Deleting the cache only means reading again.
- **The `.xopp`**: nothing. The file stays as Xournal++ writes it.
- **PDFs with notes and archive PDFs**: an invisible text layer with the best reading of each word the recogniser
  was sure enough of, so Okular, Evince, Firefox, MuPDF and others find and select the words. The other readings
  are only searched in xournal-qt. Plain "Export as PDF" has no text layer yet.

## Work and power

- Reading runs on one thread at the lowest priority (Linux: `SCHED_IDLE`): it only uses a core nothing else wants.
  It waits while pages in view are drawn and while you write.
- The open documents are always read (the one in front first). The rest of the library is read only on **mains
  power**, one document at a time; unplug and it stops within a minute.
- About 0.2 s per line, 4–5 s per page of dense handwriting on a laptop; a page is read once.
- The model takes about 250 MB of memory while it reads and is unloaded after a minute without work.

## Your handwriting as a dataset

To train a model on your handwriting, or to measure how well a model reads it, the command line tool writes a
document's handwriting as a **line dataset** in the format the training reads
([qt/research/hwr/train/FORMATS.md](../research/hwr/train/FORMATS.md), §1, `"kind": "ink"`):

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

- Code: `qt/src/hwr` (layout of ink into lines and words, the recognisers, the worker, the indexer of an open
  document), `qt/src/session/InkText.h` (what was read and how the search matches it),
  `qt/src/session/InkTextLayer.h` (the PDF text layer), `qt/src/shell/InkTextStore.h` (the library's pack),
  `LibraryInkJob.h` (background reading), `ModelDownload.h`, `HandwritingSettings.h`. Tests: `xqt-hwr-tests`
  (label `hwr`), with a scripted recogniser.
- **The model** comes from a folder: the setting `handwritingModel` (in the `xournalQt` part of settings.xml), else
  `XQT_HWR_MODEL`, else `~/.local/share/xournal-qt/models/trocr-small-hw-int8/`. `qt/scripts/hwr-model.sh` puts it
  there (from the Hugging Face cache, else downloaded) and writes its manifest `model.json` (files with sha256 and
  size; the recogniser's id is derived from it, and the library reads its handwriting again when it changes).
- **ONNX Runtime** is not linked: only its C API's headers are vendored (`qt/3rdparty/onnxruntime`), and
  `libonnxruntime.so.1` is opened when the search is switched on (`XQT_ONNXRUNTIME`, else next to the program, else
  the system's). Without it, Settings says so and nothing else changes. Packages will bundle it later.
- Tests that need the runtime or the model are skipped unless `XQT_ONNXRUNTIME` (a tiny model in
  `qt/tests/hwr/data`) or `XQT_HWR_MODEL` is set; `XQT_BENCH_HWR=1` with the model prints the time per line.

## Decisions (2026-10-04)

- Off until switched on in Settings; no learning from the user's handwriting; English only.
- Typo tolerance applies to handwriting even with Fuzzy off.
- The library is read in the background only on mains power; open documents always.
- Nothing is stored in the `.xopp`; the PDF text layer has the best reading only (no file of all readings in PDFs).
- **The model is downloaded on demand** (the author, 2026-10-04): the app ships without it, and Settings offers the
  download once the search is switched on, with the address and size shown first (as the arXiv search does: opt-in,
  the address in view), into the app's data folder, checked against sha256s pinned in the app
  (`qt/src/shell/ModelDownload.cpp`), retryable and cancellable, with "Remove the model". The source is a pinned
  revision of `huggingface.co/Xenova/trocr-small-handwritten`. **Until the revision and the sha256s are pinned** (run
  `qt/scripts/hwr-model.sh`, which prints them), the button says the model must be installed with the script.
- The runtime is opened at run time (dlopen); bundling it in the packages is a later step.
