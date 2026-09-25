# Handwriting recognition trials (throwaway)

Throwaway scripts behind the measured numbers in
[`qt/docs/research/handwriting-recognition.md`](../../docs/research/handwriting-recognition.md). They are not
app code; nothing in `qt/` or `src/` uses them. Nothing but these scripts is committed.

Data and models:
- in `~/.cache/huggingface`: TrOCR small and base as the Xenova ONNX exports, the IAM-line validation split
  (25 MB), and one 19 MB shard of `fhswf/german_handwriting`;
- in a scratch folder: kraken's PP-OCRv6 tiny, small and medium (Zenodo 21788403, 21788405 and 21788410).

| Script | What it checks |
|---|---|
| `xopp.py` | reads the pen strokes of a `.xopp` and renders them with PIL: `python3 xopp.py file.xopp out 2.0` |
| `sample_line.py` | crops one line of `test/files/benchmark/handwritten-text.xopp` (by the mean y of each stroke) |
| `segment.py` | lines and words from stroke geometry alone (adaptive gap threshold), then TrOCR top-k per word with its box: `python3 segment.py ../../../test/files/benchmark/handwritten-text.xopp 550 575` |
| `trocr_onnx.py` | TrOCR (ONNX) with onnxruntime, greedy and beam search, no torch |
| `eval_lines.py` | TrOCR small or base, fp32 or int8, on 40 IAM and 40 German lines. Prints CER, WER, time per line, how many of the ground-truth words search finds (exact or with the app's fuzzy rules; top-1, top-5, lexicon-snapped, top-5 plus lexicon candidates), and false hits: `python3 eval_lines.py 40 2 small small-q base-q` |
| `kraken_eval.py` | the same for kraken 7 PP-OCRv6 tiny, small and medium (Apache-2.0); needs the venv below |
| `footprint.py` | disk size, load time, peak RSS and time per line of one model in its own process: `footprint.py trocr-small-q 2 10`, `footprint.py kraken:medium.safetensors 2 10` |
| `kraken_finetune_time.py` | seconds per training step (CTC, AdamW, the model's line height, lines cut to 1,200 px) on a CPU, i.e. the cost of personalisation: `kraken_finetune_time.py threads batch model…`. Batch 4 or more exceeds a 3 GB cap. Also tries a dynamic-width ONNX export, which fails. |
| `kraken_onnx_fixed.py` | fixed-width ONNX export of a PP-OCRv6 model with two monkey-patches, compared with torch on a real line: `kraken_onnx_fixed.py 2 768 medium.safetensors` |
| `wordmatch.py`, `eval_lines_common.py` | a Python port of the app's fuzzy word rules (`qt/src/session/WordMatch.h`); CER and WER |

**Setup** (2026-09-26): Python 3.10 and onnxruntime 1.23.2, plus
`pip install --user huggingface_hub tokenizers pyarrow pandas pillow`. For kraken, a venv (1.6 GB):
`pip install torch --index-url https://download.pytorch.org/whl/cpu; pip install "kraken>=7.1"` (torch 2.14,
kraken 7.1.1).

**How they were run:** every run went through the workspace's limiter with a 3 GB memory cap and 2 threads
(`XQT_SLOT_MEM=3G OMP_NUM_THREADS=2 build-slot.sh python3 …`), on an i7-1165G7.
- The first `eval_lines.py` and `kraken_eval.py` runs (2026-09-25) shared the machine with builds (load 4–11),
  so their times are pessimistic.
- `footprint.py` and the second `eval_lines.py` run (2026-09-26) ran on a quiet machine.
- No training run was made: `kraken_finetune_time.py` times 3 steps and extrapolates.
