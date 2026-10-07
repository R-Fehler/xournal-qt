"""Line datasets published as parquet files on the Hugging Face hub (an image column with PNG/JPEG bytes and a text
column), converted to the FORMATS.md layout. Used by the fhswf and IAM (Teklia/IAM-line) preparers and usable for any
other such dataset (`prepare.py hf --repo ...`).

Writers: a writer column if there is one (`writer_column`, else one of WRITER_COLUMNS); else a regular expression on
the image's file name (`writer_regex`, group 1); else the split the files come with is kept (when the dataset has
train/validation/test files) and every line gets the writer "<split>"; else the lines are cut into `pseudo_writers`
consecutive blocks (datasets are usually stored writer by writer) and the preparer says so loudly.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from .dataset import DatasetInfo, DatasetWriter
from .images import frame_line

WRITER_COLUMNS = ("writer", "writer_id", "author", "author_id", "scribe", "hand")
TEXT_COLUMNS = ("text", "transcription", "label", "ground_truth", "sentence")
IMAGE_COLUMNS = ("image", "img", "line_image")
SPLIT_NAMES = {"train": "train", "training": "train", "validation": "val", "valid": "val", "val": "val",
               "dev": "val", "test": "test", "testing": "test"}


def download(repo: str, cache: Path, patterns=("*.parquet",), revision: str | None = None) -> list[Path]:
    from huggingface_hub import snapshot_download

    d = snapshot_download(repo_id=repo, repo_type="dataset", allow_patterns=list(patterns), revision=revision,
                          cache_dir=str(cache / "hf"))
    return sorted(Path(d).rglob("*.parquet"))


def split_of_file(path: Path) -> str | None:
    for part in reversed(path.with_suffix("").parts):
        m = re.match(r"([a-z]+)", part.lower())
        if m and m.group(1) in SPLIT_NAMES:
            return SPLIT_NAMES[m.group(1)]
    return None


def _pick(names: list[str], wanted: str | None, options: tuple) -> str | None:
    if wanted:
        if wanted not in names:
            raise SystemExit(f"column {wanted!r} not in {names}")
        return wanted
    return next((n for n in options if n in names), None)


def convert(files: list[Path], out: Path, info: DatasetInfo, lang: str, image_column: str | None = None,
            text_column: str | None = None, writer_column: str | None = None, writer_regex: str | None = None,
            keep_splits: bool = True, pseudo_writers: int = 0, frame: bool = True, limit: int | None = None,
            text_fn=None) -> int:
    import io

    import pyarrow.parquet as pq
    from PIL import Image

    if not files:
        raise SystemExit("no parquet files")
    rx = re.compile(writer_regex) if writer_regex else None
    rows = []
    for fpath in files:
        pf = pq.ParquetFile(fpath)
        names = pf.schema_arrow.names
        ic = _pick(names, image_column, IMAGE_COLUMNS)
        tc = _pick(names, text_column, TEXT_COLUMNS)
        wc = _pick(names, writer_column, WRITER_COLUMNS)
        if not ic or not tc:
            raise SystemExit(f"{fpath}: no image/text column among {names} (use --image-column/--text-column)")
        print(f"{fpath.name}: columns {names}; image={ic} text={tc} writer={wc}", file=sys.stderr)
        fsplit = split_of_file(fpath) if keep_splits else None
        for batch in pf.iter_batches(batch_size=256, columns=[c for c in (ic, tc, wc) if c]):
            d = batch.to_pydict()
            for k in range(len(d[tc])):
                img = d[ic][k]
                b = img.get("bytes") if isinstance(img, dict) else img
                p = img.get("path") if isinstance(img, dict) else None
                rows.append((b, p, d[tc][k], d[wc][k] if wc else None, fsplit))
                if limit and len(rows) >= limit:
                    break
            if limit and len(rows) >= limit:
                break
    how = "column"
    if all(r[3] is None for r in rows):
        if rx:
            how = "file names"
            rows = [(b, p, t, (rx.search(p or "") or [None, None])[1], s) for b, p, t, _, s in rows]
        elif keep_splits and all(r[4] for r in rows):
            how = "the dataset's own splits (no writer ids)"
            rows = [(b, p, t, f"split-{s}", s) for b, p, t, _, s in rows]
        elif pseudo_writers > 0:
            how = f"{pseudo_writers} blocks of consecutive lines (NO writer ids: check the splits)"
            n = len(rows)
            rows = [(b, p, t, f"block{(i * pseudo_writers) // n:02d}", None) for i, (b, p, t, _, _) in enumerate(rows)]
        else:
            raise SystemExit("no writer ids: give --writer-column, --writer-regex or --pseudo-writers N")
    print(f"writers from {how}: {len({r[3] for r in rows})} writers, {len(rows)} lines", file=sys.stderr)
    with DatasetWriter(out, info, overwrite=True) as w:
        for i, (b, p, t, wr, s) in enumerate(rows):
            if not t or not str(t).strip() or b is None:
                continue
            im = Image.open(io.BytesIO(b)).convert("L")
            if frame:
                im = frame_line(im)
            stem = Path(p).stem if p else f"{i:07d}"
            text = text_fn(str(t)) if text_fn else str(t)
            w.add(f"{info.name}-{stem}", im, text, lang, str(wr), s if keep_splits else None)
    return w.count
