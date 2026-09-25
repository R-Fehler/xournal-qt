# Handwriting recognition (research, 2026-09)

Research for recognising handwriting in xournal-qt: first to **search** handwritten notes, later maybe to convert
ink to text. Nothing here is implemented. The throwaway trials behind the measured numbers are in
[`research/hwr/`](../../../research/hwr/README.md).

- **Measured** means run on the author's laptop (Intel i7-1165G7, 16 GB) with 2 threads under a 3 GB memory cap,
  on 40 random lines each of IAM validation (English) and of the German `fhswf/german_handwriting` set, and on the
  repo's `test/files/benchmark/handwritten-text.xopp`.
- **Estimated** means worked out from those numbers or from the sources, not run.
- Legal remarks are my reading, not legal advice.

## Summary

| Platform | Recommendation |
|---|---|
| **Linux** (and the fallback everywhere) | A bundled line recogniser in **ONNX Runtime** (MIT), **TrOCR-small handwritten, int8** (64 MB), used for search only: top-5 candidates per word with boxes, matched by the existing fuzzy search. It is good on English (97 % of words found, measured) and weak on German (41–50 %, measured). German needs a model that the project fine-tunes once on German data (see "German"), not per-user training. |
| **Windows** | The system recogniser (`Windows.UI.Input.Inking.InkRecognizerContainer`): it takes strokes, has German and English recognisers with a lexicon, and returns alternates. From MinGW, go through [mingw-w64-cppwinrt](https://github.com/alvinhochun/mingw-w64-cppwinrt) or the COM API. The ONNX backend is the fallback. |
| **Android** | The ONNX backend by default. ML Kit Digital Ink is the most accurate option and takes strokes, but it is a closed binary (a GPL problem) and downloads its models from Google. It could be an opt-in extra in a separate build flavour, if at all. |
| **iOS/macOS** | Apple Vision (`VNRecognizeTextRequest`) on rendered ink. It is a system framework, so there is no licence problem. Later. |
| **Personalisation** | Don't fine-tune in the app for v1. Fine-tuning is possible on a laptop (about 6–23 min for 64 lines, measured and extrapolated) and the literature shows real gains from 16–50 lines per writer, but it needs a training runtime (libtorch) in the app, it can make the model worse, and the German gap is a problem of the base model. Do the cheap parts instead: the user's corrections, the library's vocabulary as extra candidates, and the training text as a **test** that shows how well recognition works for this user. Keep adaptation as a later experiment with adapters (see "Personalisation"). |

## 1. Options per platform

### Android: ML Kit Digital Ink Recognition

- It recognises **strokes** (points x, y and an optional t in ms) on the device: text in 300+ languages including
  German, plus shapes, emoji and AutoDraw. It returns ranked candidates, and it takes a `WritingArea` and a
  **pre-context** of up to 20 characters ([Android guide](https://developers.google.com/ml-kit/vision/digital-ink-recognition/android),
  [overview](https://developers.google.com/ml-kit/vision/digital-ink-recognition)).
- Size: about **20 MB per language**, downloaded on demand from Google through `RemoteModelManager`. The docs
  describe no way to bundle a model. The library is `com.google.mlkit:digital-ink-recognition:19.0.0` and needs
  API 23+. It assumes a single line of text per request, so we segment lines first. Candidate scores exist only
  for the shape classifiers, so text candidates come ranked but without scores.
- There is **no math model**.
- **Terms:** it is proprietary. It ships in the APK, so the GPL's system-library exception does not cover it.
  `qt/docs/platform-research.md` already flags this. The first download also needs Google's servers, which breaks
  "self-contained". Quality: Google's online recogniser is the state of the art for strokes
  ([Carbune et al. 2020](https://doi.org/10.1007/s10032-020-00350-4)). Not measured here.
- **Verdict:** an opt-in extra at most, in a separate flavour (for example a Play build), after a licence decision
  by the author. It is not the default.

### Windows: the system ink recogniser

- WinRT `Windows.UI.Input.Inking.InkRecognizerContainer` (Windows 10+). `RecognizeAsync` takes an
  `InkStrokeContainer` (strokes built with `InkStrokeBuilder` from our points). Each result has
  `GetTextCandidates()` (alternates) and a `BoundingRect`. The container is null when no recogniser is installed.
  Recognisers come with the language features, including German and English
  ([docs](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.inking.inkrecognizercontainer)).
  `Windows.UI.Input.Inking.Analysis.InkAnalyzer` also separates writing from drawing and finds lines and words.
- Microsoft's recognisers are lexicon-backed (a TDNN plus the spell-checker's word list), and Windows offers
  **personalisation**: a wizard where the user writes set sentences, plus learning from use. This is the author's
  idea, and it has shipped since Vista ([Windows 7 blog](https://learn.microsoft.com/en-us/archive/blogs/e7/recognizing-improvements-in-windows-7-handwriting)).
- **From Qt/MinGW:** C++/WinRT works with mingw-w64 through
  [mingw-w64-cppwinrt](https://github.com/alvinhochun/mingw-w64-cppwinrt) (GCC 12+ or Clang 15+, an MSYS2
  package). The older COM "Tablet PC" API (`IInkRecognizerContext` in `msinkaut.h`) is another way. mingw-w64's
  `msinkaut.idl` has the result and alternate interfaces but **not** `IInkRecognizerContext`, so we would declare
  it ourselves. **Not verified:** whether the WinRT recogniser works in an unpackaged Win32 app. Check this on the
  Surface.
- **Verdict:** the preferred backend on Windows. It is part of the OS, so the licence is fine, and it covers
  German.

### Apple: PencilKit and Vision

- PencilKit has **no public recognition API**. Scribble only fills text fields, and the Notes app's
  recognition is private.
- Vision's `VNRecognizeTextRequest` reads handwriting in **images**, for a subset of languages. Check which
  languages the revision in use supports with `supportedRecognitionLanguages`
  ([docs](https://developer.apple.com/documentation/vision/vnrecognizetextrequest/supportedrecognitionlanguages(for:revision:))).
  The backend would render each ink line and use Vision's word boxes.
- **Verdict:** use Vision when iOS or macOS starts. It is a system framework, so the licence is fine.

### Linux, in depth

There is no platform recogniser, so we bundle one. The candidates:

| Option | Input | Size | Quality (measured unless marked) | Speed per line (measured, 2 threads) | Code / weights licence | Notes |
|---|---|---|---|---|---|---|
| **TrOCR-small handwritten**, int8 ONNX ([Xenova export](https://huggingface.co/Xenova/trocr-small-handwritten), [paper](https://arxiv.org/abs/2109.10282)) | image of a line | **64 MB** (encoder 23 + decoder 41) | IAM: CER 5.1 %, words found 97 % (top-5, fuzzy). German: CER 41 %, words found 41 % | greedy **0.16–0.20 s**, beam 5 **0.21–0.26 s** | MIT ([unilm](https://github.com/microsoft/unilm)) / MIT. Trained on IAM, see "Licences" | **Recommended start.** Peak RSS 474 MB in Python (the model is about 165 MB of it). |
| TrOCR-small handwritten, fp32 ONNX | image of a line | 247 MB | IAM: CER 3.1 %, 100 % found. German: CER 33 %, 50 % found | greedy 0.27–0.51 s, beam 5 0.33–0.65 s | as above | better but four times bigger; peak RSS 647 MB |
| TrOCR-base handwritten, int8 ONNX | image of a line | 338 MB | IAM: CER 32 % (!), German 87 % | 1.5 s, beam 2.3 s | MIT | the int8 export is broken (the paper reports about 3.4 % CER in fp32); fp32 (about 1.3 GB, estimated) was not downloaded |
| **kraken 7 PP-OCRv6 medium** ([Zenodo 21788410](https://doi.org/10.5281/zenodo.21788410)) | image of a line | 64 MB | IAM: CER 8.2 %, 90 % found (top-1). **German: CER 17.6 %, 62 % found**, the best on German | 0.73–1.05 s (torch) | Apache-2.0 / Apache-2.0 | multilingual, including historical German. Its training set includes IAM, so the IAM number may be optimistic. Python/torch only; ONNX export works only with patches and doesn't match yet (see below) |
| kraken PP-OCRv6 small | image of a line | 13 MB | IAM: CER 10.4 %, 82 %. German: CER 29.9 %, 48 % | 0.22–0.47 s | Apache-2.0 | |
| kraken PP-OCRv6 tiny | image of a line | 3 MB | IAM: CER 15.5 %, 68 %. German: 32 %, 35 % | 0.08–0.10 s | Apache-2.0 | |
| CTC-BiLSTM on IAM-OnDB ([OnlineHTR](https://github.com/PellelNitram/OnlineHTR), a re-implementation of Carbune et al.) | **strokes** | a few MB (estimated) | not measured; English only, from one IAM-OnDB model | fast (estimated) | MIT / weights on the author's blog, trained on IAM-OnDB (non-commercial) | the only open **stroke** model found. It is research-grade, English only, and needs point times. Upstream `Point` has x, y and pressure only (no time), so it would run without dt. |
| Tesseract on rendered ink | image | ~15 MB per language | not measured (not installed). Tesseract is built for print; handwriting results are known to be poor | – | Apache-2.0 | not a real option for cursive |
| [Calamari](https://github.com/Calamari-OCR/calamari) | image of a line | – | not measured; its public models are print or historical | – | Apache-2.0, TensorFlow | no advantage over kraken |
| [PyLaia](https://github.com/jpuigcerver/PyLaia) and Teklia models ([paper](https://arxiv.org/abs/2404.18722)) | image of a line | small CRNNs | not measured; strong with an n-gram LM | – | MIT code; per-model weights | an alternative to kraken for a German fine-tune |

Speeds: the lower value comes from a quiet machine (`footprint.py`), the higher one from `eval_lines.py` while
other jobs ran. IAM lines are about 1,700 px wide; the German lines are full-width scans.

**What the measurements say:**
- English on IAM-like writing is solved well enough for search. TrOCR-small int8 finds 97 % of the words of 3 or
  more letters in its top-5 candidates, with the app's fuzzy rules (a port of `WordMatch.h`). **False hits** (a
  random word of another line matching fuzzily) are at 1–2 %.
- On the repo's own ink (`handwritten-text.xopp`, a fast messy hand: "This is a dumb test, written many times…"),
  word segmentation from stroke geometry alone found all 16 words of two sentences in 7 ms. TrOCR-small int8 (beam
  4, word by word, 133 ms per word) got "This, is, a, dumb, test, times" at top-1, "many" in the top-4 of one
  sentence, and "written" never ("visits", "visitors"). On the full benchmark page, where lines are written over
  each other, the simple line grouping fails (11 lines, 46 words), so line finding must also use stroke order.
- **German is the gap.** TrOCR is trained on English IAM, with an English BPE vocabulary. The best open German
  result was kraken medium at 62 % of words found (top-1 only; its CER is slightly pessimistic because it outputs
  NFD umlauts).
- A lexicon from the rest of the dataset (the stand-in for "the library's own vocabulary") **hurts when it replaces
  words**: 96 → 92 % of words found on IAM. It helps only a little when it adds candidates: 41 → 43 % and
  50 → 53 % on German, with no extra false hits.

**What others do:**
- **Xournal++:** no HWR. An external project, [xournalpp_htr](https://github.com/PellelNitram/xournalpp_htr)
  (GPL-2.0, Python), does word detection plus recognition and writes a searchable PDF.
  [Issue #4932](https://github.com/xournalpp/xournalpp/issues/4932) asks how HWR should fit into Xournal++.
- **rnote:** no HWR yet. [Issue #327](https://github.com/flxzt/rnote/issues/327) is open, and
  [WIP PR #1828](https://github.com/flxzt/rnote/pull/1828) (Aug 2026, closed) tried a custom CNN + Conformer with
  greedy decoding and asked how search should use it.
- **Saber:** no HWR ([issue #711](https://github.com/saber-notes/saber/issues/711), open since 2023).
- **Joplin:** Tesseract OCR for printed text in images ([help](https://joplinapp.org/help/apps/ocr/)). HTR
  for handwritten scans is planned **on a server** (Joplin Cloud/Server), not on the device
  ([announcement](https://joplinapp.org/news/20241217-project-4-htr/)).
- **OneNote, Nebo/MyScript, GoodNotes, Samsung Notes** use proprietary engines (MyScript iink is a commercial SDK).

So no open desktop note app ships on-device HWR yet. We would be early, and search-only is the realistic scope.

### Datasets

| Dataset | What | Terms |
|---|---|---|
| [IAM](https://fki.tic.heia-fr.ch/databases/iam-handwriting-database) | 13k English lines, 657 writers, images | free for **non-commercial research** only (registration) |
| [IAM-OnDB](https://fki.tic.heia-fr.ch/databases/iam-on-line-handwriting-database) | English strokes (whiteboard), 221 writers | same as IAM |
| [fhswf/german_handwriting](https://huggingface.co/datasets/fhswf/german_handwriting) | 10.8k German lines, 15 writers, school and university texts | **AFL-3.0** (permissive), 1.8 GB |
| kraken's corpus ([card](https://doi.org/10.5281/zenodo.21788410)) | 44 languages; much historical German (Fraktur, Kurrent, 16th–20th c.) plus modern sets | open, per dataset |
| CROHME ([2019](https://www.cs.rit.edu/~crohme2019/)) | online math (InkML), about 10k expressions | research |
| [MathWriting](https://arxiv.org/abs/2404.10690) (Google 2024) | 230k human + 400k synthetic online math expressions | CC BY-NC-SA 4.0 |

German handwriting data is scarce. A German model would combine fhswf, modern German lines from kraken's sources,
and synthetic lines (handwriting fonts, or ink synthesised from strokes; synthetic-to-real adaptation:
[Kang et al. 2019](https://arxiv.org/abs/1909.08473)).

### Licences for a GPL app

- **Runtime:** ONNX Runtime is MIT. Its C++ library is about 20–25 MB per platform (the Python wheel's native
  library is 23 MB, measured), with an AAR for Android. It is compatible with GPL.
- **kraken code and weights** are Apache-2.0. Apache-2.0 is compatible with GPLv3 but not GPLv2-only.
  xournal-qt is GPLv2-or-later, so a binary that includes Apache code is GPLv3. Weights are data files next to the
  program, and I read that as aggregation.
- **TrOCR** weights are published under MIT by Microsoft (the base model card says MIT; the small card has no
  licence, the repo is MIT). They are fine-tuned on IAM, whose terms are "non-commercial research". Whether
  weights inherit dataset terms is unsettled. Microsoft publishes them under MIT, so the risk is low but not zero.
  The same applies to any IAM or IAM-OnDB model (OnlineHTR). A model we fine-tune on **fhswf (AFL-3.0)** plus
  permissive data would have a clean chain.
- **ML Kit:** proprietary. Linking it into a GPL APK is the problem noted above. Keep it out of the default build.
- **OS recognisers** (Windows, Apple) are system components, so there is no problem.

## 2. Search-only design

The goal is that searching for "Kalman" finds the handwritten word "Kalman", even when the recogniser's first guess
was "Kalmar". The text doesn't have to be perfect.

1. **Layout, no model.** Each page's pen strokes are split into **lines**, then **words**. Lines: strokes grouped
   by vertical overlap with the median stroke height, plus stroke order (strokes written one after another belong
   together; the time-free file still has order). Words: gaps above an adaptive threshold (two-class Otsu split
   of the line's gaps, clamped to 0.6–2 × stroke height). Dots, umlaut marks and i-dots join the nearest word
   (`segment.py`, 7 ms for 292 strokes in Python, measured). Drawings are left out: strokes much taller than the
   line, long straight strokes, and shapes from the shape recogniser.
2. **Recognise per line.** Render the line to a grey image, about 64–128 px high, and run the recogniser. A line
   is cheaper than word by word (0.21 s per line with beam 5, against 0.13 s per word, measured). TrOCR squeezes
   its input to 384×384, so a long line is cut at a word gap into pieces of at most ~8 words.
3. **Top-k per word with boxes.** Take the union of the words from the 5 beams, map the words of the best beam
   onto the geometric word boxes (in order; if the counts differ, by x position), and attach the other beams'
   variants to the same box. Store per ink word: its box, the ids of its strokes, and 3–5 candidates with a
   confidence (the beam's length-normalised log-probability).
4. **Index.** The words go into `DocumentTextIndex` as a third text source per page, next to the PDF text and the
   text elements, with a box per word, like `PdfPageLayout` (text plus a box per character). Fuzzy search and
   hit highlighting then work unchanged. Candidates also go into the page's **vocabulary**, so fuzzy terms match
   them. A match on a lower candidate ranks below exact PDF or text hits, the way the library already puts
   fuzzy-only documents after exact ones.
5. **Background and incremental.** Recognition runs on the single existing background worker, only while the
   canvas has no page to render (`visiblePagesBusy`), and pauses on battery. The key is a **hash of each line's
   strokes**, so an edit re-recognises only the changed lines, after the edits pause (as the text index already
   waits). For the whole library: about 20 lines per page × 0.2 s ≈ **4–5 s per page** (estimated from the
   measured speed), so 1,000 pages take about 1.3 h at low priority. Pages are done once and kept in the pack.
6. **Storage in the library.** A new pack `ink-text.pack` per folder (CBOR plus zlib like the others), per
   document per page: line hashes, word boxes and candidates. About 150 words per page × ~50 bytes ≈ 7 KB per page
   before compression (estimated). It is written when a `.xopp` is saved, like `notes.pack`, and it is kept apart
   so the big `pdf-text.pack` isn't rewritten.
7. **Hit highlighting on ink:** a rounded box (or an underline) around the ink word, the same mark as PDF hits.
   Hovering or tapping it shows the recognised text and the other candidates, which is also where the user
   corrects it (see Personalisation).
8. **Consent:** the author's rule for OCR applies. Ask before the first run over a library, with "remember my
   choice" and a "forget" button in Settings.

Ink-to-text conversion (optional, later) uses the same pipeline. It shows the top-1 with the alternates for the
user to confirm, then replaces the strokes with a text element.

## 3. Personalisation

**The idea:** the user writes a set text shown in the reference view, plus a margin-writing task, and the app
adapts the recogniser.

**What the literature says:**
- Adaptation to one writer from a few labelled lines works in research, and the gains are largest for writers the
  model handles badly:
  - [MetaHTR](https://arxiv.org/abs/2104.01876) (CVPR 2021): **16 samples**, one gradient step, word accuracy on
    IAM 81.3 → 89.2 %. It needs a model meta-trained for this.
  - [Aradillas et al. 2020](https://arxiv.org/abs/2012.02544): fine-tuning on a new hand; the error falls by
    about **1 % CER every 4 lines up to ~50 lines**, then by 1 % per 100 lines. With 150–350 lines, CER went from
    20 % to about 5 % (Washington), but on a model trained for a different domain.
  - Without labels: test-time adaptation from the page itself, up to 8 % absolute CER
    ([2308.15037](https://arxiv.org/abs/2308.15037)), and [DocTTT](https://arxiv.org/abs/2501.12898).
  - Prompt or adapter tuning of under 1 % of the weights ([MetaWriter 2025](https://arxiv.org/abs/2505.20513)).
  - In-context adaptation with no weight changes: an 8 M-parameter model reads a few samples of the writer
    ([2603.29450](https://arxiv.org/abs/2603.29450), CER 3.9 % on IAM).
- [On the generalisation of HTR models (2024)](https://arxiv.org/abs/2411.17332): out-of-domain error is driven
  first by **textual** divergence (language, vocabulary), then by visual divergence. For us that means the German
  vocabulary matters more than the individual hand.
- Microsoft shipped exactly the author's idea: a sentence-writing wizard plus learning from corrections.

**Does it help us?**
- **Yes, for the hand. No, for the language.** 30–60 lines from a training text are in the range where the
  literature sees large gains for one writer. But an English-trained TrOCR won't learn German from 50 lines of
  one person.
- **Risk to generality:** full fine-tuning on 50 lines overfits and forgets. Aradillas et al. saw data
  augmentation *hurt* after transfer on small sets. The safe way is to train only a small part (an adapter, LoRA,
  or the last layer), keep the base model untouched, test on held-out lines of the user (the margin task is a
  good held-out set: different position, size and slant), and use the adapted model only if it is better. A
  predefined text also has a narrow vocabulary: it teaches letter shapes, not words.
- **Cost on a laptop (measured, kraken PP-OCRv6, CPU, 2 threads, batch 1):**
  - small (3.2 M parameters): 0.27 s per line step, peak RSS 1.0 GB, so 64 lines × 20 epochs ≈ **6 min**.
  - medium (15.9 M): 1.08 s per line, 1.7 GB, so about **23 min**.
  - Batch 2 of wider lines needed 2.2 GB. Batch 4 (and the first try with batch 8) **was killed at the 3 GB cap**.
    Fine-tuning is **too heavy to run casually** next to the app on a 16 GB laptop, and out of the question on a
    phone.
  - TrOCR-small (62 M parameters) would cost several times more (estimated).
  - It would also need a training runtime in the app: libtorch is hundreds of MB; ONNX Runtime training is
    another dependency.
- **The cheap parts that need no retraining:**
  1. **Corrections.** A word the user confirms or corrects becomes candidate #1 with confidence 1 and is never
     overwritten by re-recognition, as long as its strokes stay the same. This fixes exactly the words the user
     searches for. Store corrections with the document if possible, not only in the cache, so they survive cache
     deletion. See "Open questions".
  2. **The library's vocabulary as extra candidates.** For each ink word, add the nearest words from the
     library's vocabulary (PDF text, typed text, earlier corrections) within edit distance 1–2 as low-confidence
     candidates, instead of *replacing* the word (replacing hurt, measured above). Better still, re-rank the
     beam with it. It costs nothing and slightly helped German.
  3. **The training text as a check:** the user writes the reference text, the app recognises it and shows the
     result ("82 % of words recognised; these letters are confused: u/n, a/o"). This sets expectations, chooses
     between backends (for example Windows' recogniser against ONNX), and later provides the data for an
     adaptation experiment.

**Recommendation:** v1 has no per-user fine-tuning. It has corrections, lexicon candidates, and the training text
as a test. Put the effort into a **German base model** (Stage 5 below), which helps every German user more than
adaptation would help one. Revisit adapter tuning (LoRA or last layer, at most a few minutes, opt-in, "use only if
better on your held-out lines") once there is a German model and users report hand-specific failures.

## 4. Math (handwriting to LaTeX): options and sizes only

| Option | Input | Size | Licence | Note |
|---|---|---|---|---|
| [Pix2Text MFR](https://huggingface.co/breezedeus/pix2text-mfr) | image | ONNX 87 + 30 MB | MIT | a TrOCR-style formula recogniser, printed and handwritten; fits the same ONNX runtime |
| [UniMERNet tiny](https://huggingface.co/wanderkid/unimernet_tiny) | image | 430 MB (pth) | Apache-2.0 | trained on printed and handwritten formulas |
| [TexTeller](https://huggingface.co/OleehyO/TexTeller) | image | 298 M parameters, 1.2 GB | Apache-2.0 | too big |
| CROHME models (BTTR, CoMER, [PosFormer](https://github.com/SJTU-DeepVisionLab/PosFormer)) | rendered ink | ~6–8 M parameters (estimated) | research code, CROHME data | best fit for handwriting; the weights' terms need checking |
| [MathWriting](https://arxiv.org/abs/2404.10690) | strokes (dataset) | – | CC BY-NC-SA | to train a stroke math model; the NC terms are a problem |
| MyScript Math | strokes | – | commercial | – |
| ML Kit, Windows ink, Apple | – | – | – | no text-to-LaTeX math model in the public APIs (Windows 7's Math Input Panel was a separate COM control; its status on Windows 11 isn't verified) |

For search, math ink should be **excluded** from the text index (it produces garbage words and false hits). A later
math feature is a separate "convert selection to LaTeX" command using Pix2Text MFR on the same ONNX runtime.

## 5. Sizes, RAM and latency (top Linux candidates)

| Model | Disk | Peak RSS (measured, Python process) | Per line, laptop (measured) | Per line, Galaxy Fold 7 (estimated) |
|---|---|---|---|---|
| TrOCR-small int8, ONNX Runtime | 64 MB | 474 MB, of which ~165 MB model after load; a C++ process would need ~250–300 MB (estimated) | greedy 0.16 s, beam 5 0.21 s | 0.15–0.4 s on 2 big cores; slower on the efficiency cores and when throttled |
| TrOCR-small fp32 | 247 MB | 647 MB | 0.27 s / 0.33 s | 0.3–0.7 s |
| kraken PP-OCRv6 medium (torch) | 64 MB | 999 MB (torch alone is ~350 MB of it) | 0.76 s | not portable yet (see below) |
| kraken PP-OCRv6 small (torch) | 13 MB | 683 MB | 0.22 s | – |
| kraken small / medium, ONNX, fixed width 768 px | 13 / 64 MB | – | **82 / 323 ms** for a 768 px line (torch: 89 / 359 ms), but the output doesn't match yet | – |

- The phone estimate assumes the Fold 7's Snapdragon 8 Elite is at least as fast per core as the i7-1165G7 in
  single-thread benchmarks, and that background work gets 2 cores. Measure this in the first Android build.
- **kraken to ONNX:** the dynamic-width export fails. With two small patches (plain-int padding and pooling
  sizes, `kraken_onnx_fixed.py`), a fixed-width export works and runs about 10 % faster in ONNX Runtime than in torch on a real line. But its
  output differs from torch (per-frame argmax agrees on only 88–92 % of a real line), so it needs debugging before
  kraken models can run in the app. Until then, the German-strong kraken medium is Python-only.

## 6. Proposed architecture

```
          strokes of a page (x, y, pressure; stroke order)
                         │
                InkLayout (core, Qt-free)        lines → words, boxes, stroke ids, line hashes
                         │
             HandwritingRecognizer (interface)   recognizeLines(lines, context) → per word: box, top-k (text, score)
       ┌─────────────┬───────────────┬──────────────┬────────────────┐
  OnnxLineRecognizer  WinInkRecognizer  MlKitRecognizer  VisionRecognizer
  (TrOCR-small int8;  (InkRecognizer-   (opt-in flavour)  (iOS/macOS,
   later a German      Container)                          later)
   model)
                         │
  InkTextIndexer (background worker, idle-only, incremental by line hash, consent)
                         │
  DocumentTextIndex (open document: 3rd text source + word boxes)
  ink-text.pack (library: per folder)  ──►  fuzzy search, vocabularies, hit marks on ink
```

- **Interface** (earlier called `InkRecognizer` in `platform-research.md`):
  - `capabilities()`: strokes or image, languages, top-k.
  - `recognizeLines(const std::vector<InkLine>&, const RecognitionContext&)`: the context holds the language(s),
    a pre-context, and the lexicon hint.
  - It returns `InkWord{QRectF box; std::vector<int> strokes; std::vector<Candidate{QString text; float score}>}`.
  - It is synchronous and runs on the worker. Backends that take images get a rendered line from `InkLayout`;
    stroke backends get points (we have no point times, so t is synthesised or left out).
- **Backend choice:** per platform by default (Windows: system, if installed for the document's language;
  otherwise ONNX), switchable in Settings → Search → "Handwriting".
- **The model** is shipped in the package on Linux and Windows (+64 MB). On Android, either in the APK or fetched
  once from our own release page (open question). It is never fetched from a third party without asking.
- **UI:**
  - a Settings switch "Search handwriting" (off until the user agrees);
  - a progress line in the library ("Reading handwriting: 120 of 800 pages");
  - hits boxed on the ink;
  - tapping a boxed word shows the candidates and a correction field;
  - an optional "Handwriting check" in the reference view (the training text as a test).

### Staged plan

1. **InkLayout:** lines and words from strokes, with tests on the benchmark and sample files (no ML). This also
   serves lasso "select word/line".
2. **ONNX backend + open documents:** ONNX Runtime in the build (Linux, Windows/MinGW), TrOCR-small int8, beam 5,
   words in `DocumentTextIndex`, hits marked on ink. English first.
3. **Library:** `ink-text.pack`, background indexing across the library, consent, and the progress UI.
4. **Platform backends:** Windows `InkRecognizerContainer` (check on the Surface first; this gives German on
   Windows). Decide about ML Kit. Apple Vision when iOS or macOS start.
5. **German model:** the project fine-tunes a small line model once, on a GPU outside the app (TrOCR-small or
   PP-OCRv6-small; fhswf, synthetic German, permissive modern lines), with a clean licence chain, and ships it as
   a second ONNX model chosen by the document's language. Or fix the kraken ONNX export and ship PP-OCRv6 medium
   (Apache-2.0, 64 MB).
6. **Corrections and lexicon;** then optionally the training-text check, and the adapter experiment if data shows
   a need.
7. Later: ink-to-text conversion, and math to LaTeX (Pix2Text MFR).

### Risks

- **German quality:** the open English-trained models are weak on German (measured). Without Stage 5, German
  search finds about half the words.
- **Segmentation:** dense or overlapping writing, drawings and math mixed with text. Geometry alone failed on the
  benchmark page. The fix is stroke order plus excluding non-text strokes.
- **False hits** from garbage candidates on drawings or math: measured at 1–2 % for words of 3 or more letters,
  but short terms will match more often. Weight lower candidates below exact hits and don't index 1–2-letter
  candidates.
- **Size and CPU:** +64 MB per model and minutes to hours of background CPU for a big library. On Android,
  battery: index only while charging, or opt-in.
- **Licences:** IAM-derived weights (TrOCR, OnlineHTR) have an unclear chain. ML Kit is proprietary.
  Apache-2.0 parts make the binary GPLv3.
- **The ONNX export of kraken** doesn't match torch yet.
- **Windows:** whether WinRT ink recognition works unpackaged from MinGW is not verified.

## Open questions for the author

1. **Scope:** is search-only enough for v1, with ink-to-text later?
2. **German:** fund Stage 5 (a German base model trained by the project, on a GPU, outside the app), or ship
   English-first and rely on Windows' recogniser for German at first?
3. **Android:** allow ML Kit as an opt-in in a separate flavour (proprietary, downloads from Google), or keep
   Android on the bundled ONNX model?
4. **Package size:** is +64 MB per model fine for the .deb, the Windows installer and the APK, or should models
   be fetched once from our release page?
5. **Corrections:** where should they live? In the `.xopp` (they survive and travel; needs a format extension
   that upstream ignores) or only in the cache packs?
6. **Personalisation:** agree to keep it as the "handwriting check" plus corrections for now, and treat
   fine-tuning as a later opt-in experiment?
