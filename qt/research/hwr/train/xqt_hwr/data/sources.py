"""Preparers of the real handwriting datasets (each writes the FORMATS.md layout):

| name   | what                                                      | licence                         | how                    |
|--------|-----------------------------------------------------------|---------------------------------|------------------------|
| fhswf  | fhswf/german_handwriting: ~10.8k German lines, 15 writers | AFL-3.0                         | Hugging Face hub       |
| iam    | IAM lines: ~13k English lines, 657 writers                | non-commercial research (free registration) | your copy (official layout), or Teklia/IAM-line on the hub |
| cvl    | CVL database: 310 writers, 1 German and 6 English texts   | research use (see its record)   | your copy, or Zenodo   |

All line pictures are framed as the app frames a line (images.frame_line).
"""
from __future__ import annotations

import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image

from .dataset import DatasetInfo, DatasetWriter
from .images import frame_line
from . import hfparquet

FHSWF_REPO = "fhswf/german_handwriting"
IAM_HUB_REPO = "Teklia/IAM-line"
CVL_URL = "https://zenodo.org/records/1492267/files/cvl-database-1-1.zip?download=1"


def iam_text(text: str) -> str:
    """IAM's transcriptions are tokenised: "this one , but it 's a good start ." and quotes as separate tokens
    (" Lady of Spain "), in the official lines.txt (with | between words) and in the hub copy alike. Written text has
    none of those spaces, and the app reads ink as written, so they are taken out."""
    text = re.sub(r"\s+", " ", text.replace("|", " ")).strip()
    out, quote_open = [], False
    for tok in text.split(" "):
        if tok == '"':
            if quote_open and out:
                out[-1] += '"'
            else:
                out.append('"')
            quote_open = not quote_open
            continue
        if out and out[-1] == '"' and quote_open:
            out[-1] += tok
        else:
            out.append(tok)
    text = " ".join(out)
    text = re.sub(r" ([.,;:!?)\]])", r"\1", text).replace("( ", "(").replace("[ ", "[")
    return re.sub(r" (['’](s|t|re|ve|ll|d|m)\b|n['’]t\b)", r"\1", text)


def fhswf(out: Path, cache: Path, source: Path | None = None, writer_column: str | None = None,
          writer_regex: str | None = None, pseudo_writers: int = 15, limit: int | None = None) -> int:
    files = sorted(Path(source).rglob("*.parquet")) if source else hfparquet.download(FHSWF_REPO, cache)
    info = DatasetInfo(name="fhswf-german", languages=["de"], licence="AFL-3.0",
                       source=f"https://huggingface.co/datasets/{FHSWF_REPO}", kind="scan")
    # Split by writer (never by the dataset's line splits)
    return hfparquet.convert(files, out, info, "de", writer_column=writer_column, writer_regex=writer_regex,
                             keep_splits=False, pseudo_writers=pseudo_writers, limit=limit)


def _iam_writers(forms: Path) -> dict[str, str]:
    out = {}
    for s in forms.read_text(encoding="latin-1").splitlines():
        if s.startswith("#") or not s.strip():
            continue
        p = s.split()
        out[p[0]] = p[1]
    return out


def _iam_split_files(d: Path | None) -> dict[str, str]:
    """The Aachen writer-independent split files (trainset.txt, validationset1/2.txt, testset.txt), if given."""
    out = {}
    if not d:
        return out
    for fn, split in (("trainset.txt", "train"), ("validationset1.txt", "val"), ("validationset2.txt", "val"),
                      ("testset.txt", "test")):
        p = Path(d) / fn
        if p.exists():
            for s in p.read_text().split():
                out[s.strip()] = split
    return out


def iam(out: Path, source: Path | None, cache: Path, hub: bool = False, splits_dir: Path | None = None,
        limit: int | None = None) -> int:
    info = DatasetInfo(name="iam-lines", languages=["en"], kind="scan", noncommercial=True,
                       licence="IAM Handwriting Database terms: non-commercial research use",
                       source="https://fki.tic.heia-fr.ch/databases/iam-handwriting-database")
    if hub:
        files = hfparquet.download(IAM_HUB_REPO, cache)
        info.source = f"https://huggingface.co/datasets/{IAM_HUB_REPO} (IAM lines, Aachen splits)"
        # No writer ids there; the files' splits are the writer-independent Aachen splits
        return hfparquet.convert(files, out, info, "en", keep_splits=True, limit=limit, text_fn=iam_text)
    if source is None:
        raise SystemExit("iam: give --source <your IAM folder> (with ascii/lines.txt and lines/) or --hub")
    source = Path(source)
    lines_txt = next(iter(source.rglob("lines.txt")), None)
    forms_txt = next(iter(source.rglob("forms.txt")), None)
    if lines_txt is None:
        raise SystemExit(f"no lines.txt under {source}")
    writers = _iam_writers(forms_txt) if forms_txt else {}
    if not writers:
        print("warning: no forms.txt: writers taken from the form id's prefix", file=sys.stderr)
    split_of = _iam_split_files(splits_dir)
    images = {p.stem: p for p in source.rglob("*.png")}
    with DatasetWriter(out, info, overwrite=True) as w:
        for s in lines_txt.read_text(encoding="latin-1").splitlines():
            if s.startswith("#") or not s.strip():
                continue
            p = s.split(" ")
            lid = p[0]
            text = iam_text(" ".join(p[8:]))
            form = "-".join(lid.split("-")[:2])
            img = images.get(lid)
            if img is None:
                continue
            split = split_of.get(lid) or split_of.get(form)
            # (status "err": IAM flags the line's segmentation as possibly wrong; kept, most are fine)
            w.add(lid, frame_line(Image.open(img)), text, "en", writers.get(form, form.split("-")[0]), split)
            if limit and w.count >= limit:
                break
    return w.count


_GERMAN = re.compile(r"[äöüßÄÖÜ]|\b(der|die|das|und|nicht|ist|ein|eine|ich|sie|mit|auf)\b", re.I)


def cvl(out: Path, source: Path | None, cache: Path, download: bool = False, limit: int | None = None) -> int:
    """CVL database: line pictures `<writer>-<text>-<line>.tif` under lines/, their text joined from the word
    pictures `<writer>-<text>-<line>-<word>-<label>.tif` under words/ (the label is in the file name). The language of
    each of the 7 texts is detected from its words. Splits by writer (CVL's own train/test split is not used: its test
    set holds 283 of the 310 writers)."""
    if download and source is None:
        import urllib.request
        import zipfile
        z = cache / "cvl-database-1-1.zip"
        if not z.exists():
            cache.mkdir(parents=True, exist_ok=True)
            print(f"downloading {CVL_URL} (about 1.6 GB)", file=sys.stderr)
            urllib.request.urlretrieve(CVL_URL, z)
        source = cache / "cvl"
        if not source.exists():
            zipfile.ZipFile(z).extractall(source)
    if source is None:
        raise SystemExit("cvl: give --source <your CVL folder> or --download")
    source = Path(source)
    words = defaultdict(list)
    for p in source.rglob("*"):
        if p.suffix.lower() not in (".tif", ".tiff", ".png") or "word" not in str(p.parent).lower():
            continue
        m = re.match(r"(\d+)-(\d+)-(\d+)-(\d+)-(.+)$", p.stem)
        if m:
            words[(m.group(1), m.group(2), m.group(3))].append((int(m.group(4)), m.group(5)))
    lines = {}
    for p in source.rglob("*"):
        if p.suffix.lower() in (".tif", ".tiff", ".png") and "line" in str(p.parent).lower():
            m = re.match(r"(\d+)-(\d+)-(\d+)$", p.stem)
            if m:
                lines[(m.group(1), m.group(2), m.group(3))] = p
    if not lines or not words:
        raise SystemExit(f"cvl: found {len(lines)} line and {len(words)} word groups under {source}")
    texts = {k: " ".join(lbl for _, lbl in sorted(v)) for k, v in words.items()}
    votes = defaultdict(Counter)
    for (wr, tx, _), t in texts.items():
        votes[tx]["de" if _GERMAN.search(t) else "en"] += 1
    lang_of = {tx: c.most_common(1)[0][0] for tx, c in votes.items()}
    print(f"cvl: languages per text {dict(sorted(lang_of.items()))}", file=sys.stderr)
    info = DatasetInfo(name="cvl-lines", languages=sorted(set(lang_of.values())), kind="scan", noncommercial=True,
                       licence="CVL Database terms (research use; see https://doi.org/10.5281/zenodo.1492267)",
                       source="https://doi.org/10.5281/zenodo.1492267")
    with DatasetWriter(out, info, overwrite=True) as w:
        for key in sorted(lines):
            t = texts.get(key)
            if not t:
                continue
            wr, tx, ln = key
            w.add(f"cvl-{wr}-{tx}-{ln}", frame_line(Image.open(lines[key])), t, lang_of[tx], wr)
            if limit and w.count >= limit:
                break
    return w.count
