# Training xournal-qt's handwriting models

This project trains the models behind xournal-qt's handwriting search ([handwriting-search.md](../../../docs/features/handwriting-search.md)).
It takes you from a fresh GPU machine to a model folder that the app loads. The goal is **search**, not
transcription: a model is good when the search finds the words you wrote among its top-k readings, even when its
first guess is wrong. Every measure here is built around that.

What it builds, all in PyTorch:

| Model | What | Config |
|---|---|---|
| **TrOCR-small, German** | Microsoft's TrOCR-small (62 M parameters), fine-tuned on German lines, with the app's current loader and format | `configs/de-trocr.yaml` |
| **CTC, German** | a CNN and a BiLSTM (5.7 M parameters, 64 px lines), trained from scratch. Small and fast, read by the app's `CtcRecognizer` | `configs/de-ctc.yaml` |
| **Combined German + English** | one model of either kind, trained on the merged datasets | `configs/en-de-combined-{trocr,ctc}.yaml` |
| **Personal** | a trained model fine-tuned on one person's ink, exported by the app | `configs/user-finetune{,-ctc}.yaml` |

The contract with the app is [FORMATS.md](FORMATS.md): the line-dataset layout read here and exported by the app, and
the two model-folder kinds (`trocr`, `ctc`).

## Hardware

- One or more NVIDIA GPUs with **at least 11 GB** each (for example RTX 2080 Ti, 3060 12 GB, or better), and a driver
  for CUDA 12.8. Several GPUs are used through DDP (`torchrun`).
- CPU: about 6–8 cores per GPU. Pictures are augmented on the CPU, and the CTC model is usually limited by that.
- 32 GB RAM. Disk: about 10 GB for the data and 1–3 GB per run of checkpoints (estimated).

## Setup (fresh Linux GPU machine)

```sh
git clone <xournal-qt> && cd xournal-qt/qt/research/hwr/train
python3 -m venv ~/xqt-hwr-venv && . ~/xqt-hwr-venv/bin/activate
pip install -r requirements-gpu.txt && pip install -e .
python -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.device_count())"
python -m pytest -q          # the CPU smoke tests below, about a minute: the pipeline works before you spend GPU time
```

`requirements-gpu.txt` pulls the CUDA 12.8 build of PyTorch. For another CUDA version, change its index URL
(`cu126`, `cu130`, …). Models and datasets come from the Hugging Face hub without a login. If a dataset asks for
one, run `huggingface-cli login`.

## The steps, one command each

All data goes to `~/hwr-data` (or set `XQT_HWR_DATA`, or `--data-root`). Runs go to `runs/<name>/`.

```sh
# 1. Data: fonts, fhswf (German), synthetic German and English, IAM (from the hub), CVL (German + English).
#    Optional sources that fail (no network, a moved file) are reported and skipped.
python prepare.py all
python prepare.py check ~/hwr-data/fhswf-german      # lines, splits, writers, characters: look at the writers!

# 2. German TrOCR (N = number of GPUs; with one GPU: python train.py --config ...)
torchrun --standalone --nproc_per_node=N train.py --config configs/de-trocr.yaml

# 3. German CTC
torchrun --standalone --nproc_per_node=N train.py --config configs/de-ctc.yaml

# 4. Combined German + English (either kind or both)
torchrun --standalone --nproc_per_node=N train.py --config configs/en-de-combined-trocr.yaml
torchrun --standalone --nproc_per_node=N train.py --config configs/en-de-combined-ctc.yaml

# 5. Evaluate a checkpoint (PyTorch, on the GPU)
python evaluate.py run --model runs/de-trocr/checkpoints/best --datasets fhswf-german synthetic-de \
    --device cuda --out reports/de-trocr-checkpoint

# 6. Export to the app's format (int8 ONNX), with the parity check against PyTorch
python export.py --checkpoint runs/de-trocr/checkpoints/best --out models/trocr-small-de --check-datasets fhswf-german
python export.py --checkpoint runs/de-ctc/checkpoints/best --out models/crnn-de --check-datasets fhswf-german

# 7. Compare what ships (ONNX Runtime on the CPU, 2 threads, decoded as the app decodes), with the app's current
#    English model if it is installed
python evaluate.py compare --model models/trocr-small-de --model models/crnn-de --app-model \
    --datasets fhswf-german iam-lines cvl-lines --max-lines 500 --out reports/compare

# 8. Install for the app
cp -r models/trocr-small-de ~/.local/share/xournal-qt/models/trocr-small-de/   # or: export.py ... --install
```

Then choose the model in the app's settings (block `qt/hwr-multilang`: a model per language), or start the app with
`XQT_HWR_MODEL=~/.local/share/xournal-qt/models/trocr-small-de`.

More options:
- `--resume` continues a run from `runs/<name>/checkpoints/last`, after a crash or to raise `max_steps`.
- `--set key=value` overrides one config key, for example `--set train.batch_size=16 --set out_dir=runs/try2`.
- `python prepare.py synthetic --lang de --lines 50000 --corpus tatoeba --corpus textfile:my-notes.txt` adds
  synthetic lines from your own text, which helps with your subject's vocabulary.
- `python prepare.py hf --repo OWNER/NAME --name NAME --lang de --licence ...` converts any parquet line dataset on the
  hub. `prepare.py iam --source /data/IAM --splits-dir /data/IAM/splits` reads your own IAM copy.

### What a run prints and writes

Every `log_every` steps you get a JSON line with the step, loss, learning rate and lines per second. Every
`val.every_steps` it prints the validation: CER, WER, words found and the other measures, also per dataset. The same
lines go to `runs/<name>/log.jsonl`. Checkpoints:
- `runs/<name>/checkpoints/best`: best words found. Export this one.
- `runs/<name>/checkpoints/last`: with the optimizer's state, for `--resume`.

Each is a plain model folder (Hugging Face format for TrOCR, `model.pt` for CTC) with `xqt.json`. That file names the
datasets with their licences, the base model and the scores. Training stops at `max_steps`, or after
`train.early_stopping` evaluations without better words found.

## Memory, batch sizes, several GPUs

| Model | Per-GPU batch (config) | Memory at that batch | If it does not fit |
|---|---|---|---|
| TrOCR-small, 384 × 384 | 24, gradient accumulation 2 | ~8–9 GB in bf16/fp16 with SDPA attention (estimated) | `batch_size: 16`, or `gradient_checkpointing: true` (about 3× less activation memory, ~25 % slower; then 48–64 fit) |
| CTC, 64 px, lines up to 2048 px | 48 | ~3–5 GB (estimated) | `batch_size: 32` or `max_width: 1536` |

- Precision is automatic: bf16 on GPUs that have it (Ampere or newer), fp16 with a gradient scaler on Volta and
  Turing (for example the 2080 Ti), fp32 on older GPUs. On a GTX 1080 Ti (Pascal) a matmul runs at 8.9 TFLOPS in
  fp32, 8.4 in fp16 and 5.5 in emulated bf16 (measured).
- PyTorch's CUDA 12.8 wheels are fine for Volta and newer. For Pascal, install from the `cu126` index instead (its
  `sm_60` kernels run on `sm_61`): change the index URL in `requirements-gpu.txt`.
- With N GPUs, one step sees `batch_size × grad_accum × N` lines. The learning rate is not scaled for you. With 4 or
  more GPUs, set `grad_accum: 1` for TrOCR to keep the step size the configs were written for.
- Datasets are mixed by **weight** (`datasets:` in each config). A weight is a dataset's share of the samples,
  whatever its size. A missing optional dataset is skipped, and the other weights then share its part.

## Expected times (estimates, not measured on a GPU)

| Step | One 11–12 GB GPU (RTX 3060 / 2080 Ti class) | 4 GPUs |
|---|---|---|
| `prepare.py all` | 30–60 min (downloads: fhswf 1.8 GB, CVL 1.6 GB; rendering 350k synthetic lines on 8 cores) | – |
| German TrOCR (40k steps × 48 lines) | 6–10 h | 2–3 h |
| German CTC (80k steps × 48 lines) | 2–4 h (often limited by the CPU's augmentation) | 1 h |
| Combined TrOCR / CTC | 9–15 h / 3–5 h | 3–4 h / 1–1.5 h |
| Personal fine-tune (a few hundred lines) | 5–15 min | – |
| Export with parity check | 1–3 min | – |
| Evaluating an exported TrOCR on the CPU | ~0.2 s per line (measured for the Xenova model on a laptop) | – |

The training times come from the throughput expected of TrOCR-small and a 6 M-parameter CRNN on such GPUs. Use
early stopping: the German TrOCR may well level off before 40k steps.

### Measured here (CPU, 2026-10-05): the pipeline learns, and the export keeps what was learned

To show the whole chain on real fonts without a GPU, a small CRNN (1.0 M parameters) was trained on the CPU for
1,500 steps of 16 lines. The data was 3,000 synthetic German lines in five Google fonts, with the built-in sample text.
Validation used a font held out from training.

| step | 300 | 600 | 900 | 1200 | 1500 |
|---|---|---|---|---|---|
| CER | 83 % | 44 % | 30 % | 18 % | 18 % |
| words found | 0 % | 22 % | 40 % | 66 % | 62 % |

The best checkpoint (step 1200) was then exported to int8 ONNX (1.9 MB). On 200 test lines of a fifth font, never
seen in training:

| | words found | exact | top-1 | CER | false hits | s/line |
|---|---|---|---|---|---|---|
| checkpoint (PyTorch) | 75.6 % | 59.7 % | 67.1 % | 14.2 % | 0.6 % | 0.039 |
| exported int8 (ONNX Runtime, as the app reads it) | 75.3 % | 60.1 % | 67.1 % | 14.3 % | 0.6 % | 0.023 |

The parity check found the fp32 export identical (largest logit difference 2.4e-5). int8 gave the same best reading
on 17 of 20 lines. With a tiny vocabulary and a few fonts these numbers say nothing about real handwriting. They show
that training, validation, early stopping, export and evaluation work together.

## How it is measured: words found

The app reads a line, keeps a few readings per word and lets the search match all likely readings
([InkText.h](../../../src/session/InkText.h)). The evaluation does the same:

- **Terms**: the words of the true line with 3 or more letters, case folded as the app folds text.
- **Candidates**: the words of the best reading, plus every word whose share of the readings (the softmax of their
  log-probabilities, summed over the readings that have it) is at least 5 %. These are the app's `MIN_P` and
  `WordAlignment` rules, counted per line here because scanned lines have no word boxes.
- **Found**: a candidate contains the term, or is the term with one typo when the term has 5 or more letters. This is
  the app's plain search over handwriting (a port of `WordMatch.h`: Damerau-Levenshtein, optimal string alignment).
- Also reported: `words_found_exact` (containment only), `words_found_fuzzy` (the fuzzy search: the term's letters
  in order), `words_found_top1` (best reading only), `false_hits` (words of other lines that would match: noise),
  CER and WER of the best reading, and seconds per line.

Decoding is the app's: TrOCR beam search with 4 beams and at most 48 tokens (a port of `BeamSearch.cpp`), and CTC
prefix beam search with 8 prefixes and the top 5 readings. The research baselines from
[handwriting-recognition.md](../research.md), measured with the same rules:
TrOCR-small int8 finds 97 % of IAM's words and 41 % of fhswf's German words; kraken PP-OCRv6 medium finds 62 % of
the German words at top-1.

## How pictures are made (and why)

The app draws a line of ink 128 px high, with a margin of a quarter of the ink's height on every side
(`qt/src/hwr/LineImage.*`). TrOCR gets pieces of at most 8 words, squeezed to 384 × 384. Every source here is
brought to the same framing: scans are cropped to their ink, given that margin and scaled to 128 px. Ink is drawn
from its strokes by a port of `LineImage`. Synthetic lines have 1–8 words. So a model trains on what the app will
show it.

## Data and licences

The app is free and non-commercial. The author decided (2026-10-05) that dataset and base-model licences should not
restrict the choice of data, so non-commercial sets are used by default. Each dataset's licence is carried into
`dataset.json`, `xqt.json` and the exported `model.json` (`licence`, `datasets`, and `noncommercial: true` when such
data was used), so a model always says what it learned from.

| Dataset | Content | Licence | Source |
|---|---|---|---|
| `fhswf-german` | ~10.8k German lines, 15 writers | AFL-3.0 | [fhswf/german_handwriting](https://huggingface.co/datasets/fhswf/german_handwriting) |
| `iam-lines` | ~13k English lines, 657 writers | IAM terms: non-commercial research, free registration | [IAM](https://fki.tic.heia-fr.ch/databases/iam-handwriting-database): your copy (`--source`), or [Teklia/IAM-line](https://huggingface.co/datasets/Teklia/IAM-line) (`--hub`, the default in `data.yaml`) |
| `cvl-lines` | 310 writers, 1 German and 6 English texts | CVL database terms (research) | [Zenodo 1492267](https://doi.org/10.5281/zenodo.1492267) (`--download`) or your copy |
| `synthetic-de`, `synthetic-en` | text in handwriting fonts | text: Tatoeba CC BY 2.0 FR, Wikipedia CC BY-SA 4.0 / GFDL, the built-in sample GPL-2.0-or-later; fonts: OFL or Apache-2.0 (read from each font file) | `prepare.py fonts`, `prepare.py synthetic` |
| your ink | your lines, exported by the app | yours | `xournal-qt-cli hwr-lines` (block `qt/hwr-multilang`) |

| Base model | Licence | Note |
|---|---|---|
| `microsoft/trocr-small-handwritten` (default) | MIT | Fine-tuned by Microsoft on IAM: the best handwriting features to start from |
| `microsoft/trocr-small-stage1` | MIT | Pre-trained on printed and synthetic text only, before IAM. Set `model.base` to use it |
| CRNN | – | Trained from scratch here |

Things to check on the first real run:
- **fhswf's writers.** If its parquet files carry no writer column, the preparer cuts the lines into 15 blocks of
  consecutive lines and says so. `prepare.py check` shows the writers. If the files name writers (a column or the
  file names), pass `--writer-column` or `--writer-regex` so that the split is truly by writer.
- **CVL's layout** is read from the file names (`<writer>-<text>-<line>.tif` under `lines/`, the word pictures
  `…-<word>-<label>.tif` give the text). The download address may have moved; then download it by hand and use
  `--source`.
- **The tokenizer check.** At the start of a TrOCR run, German characters, sample phrases and every character of the
  data are sent through the tokenizer and the app's own token decoding. Characters that do not come back are added
  as tokens (`add_missing_chars`), and the log says which.

## Your own handwriting

The app exports lines of your ink as an ink dataset (FORMATS.md: `kind: ink`, with the strokes). Put it at
`~/hwr-data/my-handwriting`, check it, and fine-tune:

```sh
python prepare.py check ~/hwr-data/my-handwriting
python train.py --config configs/user-finetune.yaml --set finetune.init_from=runs/de-trocr/checkpoints/best
python evaluate.py compare --model runs/de-trocr/checkpoints/best --model runs/finetune-personal/checkpoints/best \
    --datasets my-handwriting --split test --device cuda
python export.py --checkpoint runs/finetune-personal/checkpoints/best --out models/trocr-small-de-personal
```

- **TrOCR** fine-tunes with LoRA adapters by default (`finetune.freeze: lora`, merged into the weights at export), or
  with the encoder frozen (`encoder`), or fully (`none`, with a learning rate of about 1e-5).
- **CTC** (`user-finetune-ctc.yaml`) trains only the last LSTM layer and the head. It draws the lines again from your
  strokes with varied slant, width and jitter.
- 30 % of each batch comes from the base data, so the model does not forget other hands. Validation and early stopping
  use your held-out lines only (`val.datasets`). Use the personal model only if `compare` shows it is better on your
  test lines.
- To include your lines in a base or combined model instead, add `{name: my-handwriting, weight: 0.05}` to its
  `datasets`.

## Layout

```
prepare.py train.py evaluate.py export.py     entry points (also installed as xqt-hwr-prepare, -train, ...)
configs/                                      the run configs, data.yaml, fonts.yaml; tiny/ for the CPU tests
xqt_hwr/data/        dataset.py (the layout), sources.py (fhswf, IAM, CVL), hfparquet.py, synthetic.py,
                     corpora.py, fonts.py, ink.py (strokes as LineImage draws them), images.py (framing),
                     augment.py, mix.py (weights, distributed sampler)
xqt_hwr/models/      trocr.py (building, tokenizer check, LoRA, the explicit-cache decoder), ctc.py (CRNN, alphabet)
xqt_hwr/decode.py    the app's beam search, CTC prefix beams, candidate words
xqt_hwr/recognizers.py  one read() for checkpoints and exported folders (ONNX Runtime)
xqt_hwr/metrics.py   CER, WER, words found; wordmatch.py and text.py port the app's word rules
xqt_hwr/onnx_merge.py   the merged decoder (an If over the decoders without and with past key values)
tests/               the CPU smoke tests
```

The TrOCR decoder is written out step by step with its key/value cache as plain tensors (`CachedDecoder`, checked
against transformers' own forward in the tests). Beam search in PyTorch and the ONNX export both use it, so what is
evaluated in PyTorch is what gets exported. `export.py`'s parity check confirms it on real lines.

## The CPU test environment

The tests (43) run the whole pipeline on a CPU in about a minute (4 cores), with no downloads: tiny synthetic datasets in system
fonts, a fake fhswf/IAM/CVL, an ink dataset, a randomly initialised tiny TrOCR and a tiny CRNN. They cover one
training step of each kind, a two-process DDP launch (gloo), resume, LoRA and last-layer fine-tuning, evaluation,
export and the ONNX parity check.

```sh
micromamba create -y -p /opt/xqt-train -f environment-cpu.yml     # about 2.4 GB
/opt/xqt-train/bin/python -m pytest -q
```

In the development container, `MAMBA_ROOT_PREFIX=/opt/xqt-env/mamba` and the micromamba of
`/opt/xqt-env/bootstrap/bin` were used. The test env ran torch 2.13 (CPU), transformers 5.18, onnx 1.23,
onnxruntime 1.30 and peft 0.21. The GPU requirements accept transformers 4.46 or newer. The code avoids version-specific
transformers internals (its own cache decoder, the TorchScript ONNX exporter).
