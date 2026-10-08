# Handwriting models: the formats shared by the app and the training (2026-10-05)

Three blocks meet here: the training (`qt/research/hwr/train`, PyTorch), the app (`qt/src/hwr`), and later the user's
own data (`qt/hwr-userdata`). This file fixes what they exchange, so they can be built in parallel. Change it only
together with both sides.

## 1. A line dataset (what the training reads, what the app exports)

A dataset is a folder:

```
<dataset>/
  dataset.json          # what it is
  lines.jsonl           # one line of handwriting per JSON line
  images/<id>.png       # the line as a picture (grayscale, ink dark on white)
  strokes/<id>.json     # optional: the ink itself (from xournal-qt)
```

`dataset.json`:

```json
{ "name": "fhswf-german", "version": 1, "languages": ["de"], "licence": "AFL-3.0",
  "source": "https://huggingface.co/datasets/fhswf/german_handwriting", "kind": "scan" }
```

`kind` is `scan` (photographed or scanned paper), `ink` (rendered from pen strokes, as the app does) or `synthetic`
(rendered from fonts). `licence` is carried into the model's manifest for transparency (`"noncommercial": true` where
a dataset says so). It does not restrict anything: the app is free and non-commercial, and the author chose
(2026-10-05) to pick datasets and base models by quality alone.

`lines.jsonl`, one object per line:

```json
{ "id": "w03-l0042", "image": "images/w03-l0042.png", "text": "Die Kalman-Verstärkung …", "lang": "de",
  "writer": "w03", "split": "train", "strokes": "strokes/w03-l0042.json" }
```

- `text` is NFC-normalised Unicode (umlauts as one character).
- `writer` is required: splits are by writer, never by line, so a test never sees a training writer's hand.
- `split` is `train`, `val` or `test`; when missing, the training assigns splits by writer with a fixed seed.
- Lines from a handwriting form (`xournal-qt-cli hwr-form`, [forms/DESIGN.md](../forms/DESIGN.md)) carry more
  fields: `angle` (the box's writing direction in degrees, clockwise; the picture and strokes are upright), `kind`
  (`line`, `word`, `chars`, `number`, `label`, `math`), `box_id`, `form` (the manifest's form id) and `page` (of the
  form); formulas have `"math": true` (their `text` is the formula in plain characters: the training may skip them).
  Readers ignore fields they do not know.
- `strokes` (optional) is xournal-qt's ink of the line: `{ "width": w, "height": h, "strokes": [ { "points":
  [[x, y, pressure], …], "width": pt } ] }` in points, relative to the line's top-left.

**Line pictures from ink** are drawn exactly as the app draws them for its recogniser (`qt/src/hwr/LineImage.*`:
128 px line height, stroke width normalised to the height, round caps), so a model trained on ink-rendered lines sees
what the app will give it. The app exports such datasets (`xournal-qt-cli hwr-lines`, block `qt/hwr-multilang`).

## 2. A model (what the training exports, what the app loads)

A model is a folder with a manifest `model.json` and the files it names, each with its sha256 and size (as the app's
`TrocrRecognizer` reads today, `qt/src/hwr/TrocrRecognizer.h`). Two kinds:

**`"kind": "trocr"`** (a vision encoder-decoder; the existing format, compatible with the Xenova export):

```json
{ "kind": "trocr", "name": "trocr-small-de", "languages": ["de"], "version": "2026.10.1",
  "encoder": "onnx/encoder_model_quantized.onnx", "decoder": "onnx/decoder_model_merged_quantized.onnx",
  "tokenizer": "tokenizer.json", "decoder_start_token_id": 2, "eos_token_id": 2, "image_size": 384,
  "licence": "…", "trained_on": ["fhswf-german", "synthetic-de"],
  "files": { "onnx/encoder_model_quantized.onnx": { "sha256": "…", "size": 0 }, "…": {} } }
```

The decoder is the "merged" export (with and without past key values, `use_cache_branch`), as the Xenova export the
app runs today; the training's exporter must produce the same inputs and outputs.

**`"kind": "ctc"`** (a small convolutional + recurrent model with CTC; new, read by the app's `CtcRecognizer`):

```json
{ "kind": "ctc", "name": "crnn-de-en", "languages": ["de", "en"], "version": "2026.10.1",
  "model": "model_int8.onnx", "alphabet": "alphabet.txt", "blank": 0, "input_height": 64,
  "max_width": 2048, "licence": "…", "trained_on": ["…"], "files": { "…": {} } }
```

- Input `image`: float32 `[1, 1, input_height, W]`, W ≤ `max_width`, values in [0, 1] with ink = 1 (white = 0), the
  line scaled to `input_height` keeping its aspect ratio.
- Output `logits`: float32 `[T, 1, C]` (log-softmax over C = alphabet size + 1, class `blank` is the CTC blank).
  The frames are equal slices of the input's width, left to right (the CRNN: T = W / 4): the app takes frame t of T
  as t / T to (t + 1) / T of the picture to find where a word was read.
- `alphabet.txt`: one character per line (UTF-8, NFC), class i+1 is line i (class 0 is the blank). A line holding
  only a space is the space.
- The app decodes with a CTC beam search (top-k line readings with their probabilities, and per character the frames
  it was read on) and splits them into words at spaces, then puts them on the ink's word boxes: one to one when the
  numbers match, else each word on the box it overlaps most where it was read.

`languages` tells the app which documents a model is for (`qt/hwr-multilang`: English, German, both).

## 3. Language codes

ISO 639-1: `en`, `de`. A combined model lists both.
