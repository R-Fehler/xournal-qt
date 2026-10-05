"""Text for synthetic lines, per language. Sources (config `corpora:`):

- `builtin`: a few dozen sentences per language shipped here (written for this project; for tests and as a fallback).
- `tatoeba`: Tatoeba's per-language sentence exports (CC BY 2.0 FR), https://tatoeba.org/en/downloads.
- `wikipedia`: sentences from one or more parquet shards of the Hugging Face dataset `wikimedia/wikipedia`
  (CC BY-SA 4.0 and GFDL; text only shapes the model, it is not redistributed: see the README's licence notes).
- `textfile`: your own UTF-8 text files (for example the typed text of your lecture notes), with a licence you name.

Every source gives sentences; `lines_from()` cuts them into pieces of 1..max_words words, as the app reads a line in
pieces of at most 8 words (qt/src/hwr/LineImage.h).
"""
from __future__ import annotations

import bz2
import os
import random
import re
import sys
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from ..text import clean_line

HERE = Path(__file__).parent
TATOEBA = "https://downloads.tatoeba.org/exports/per_language/{l3}/{l3}_sentences.tsv.bz2"
ISO3 = {"de": "deu", "en": "eng"}
_SENT = re.compile(r"(?<=[.!?])\s+(?=[A-ZÄÖÜ„\"'(])")


@dataclass
class Corpus:
    name: str
    lang: str
    licence: str
    sentences: list[str]


def _download(url: str, dst: Path) -> Path:
    if dst.exists() and dst.stat().st_size > 0:
        return dst
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".part")
    print(f"downloading {url}", file=sys.stderr)
    req = urllib.request.Request(url, headers={"User-Agent": "xqt-hwr-train"})
    with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.rename(dst)
    return dst


def good_sentence(s: str, min_words: int = 2, max_words: int = 30, max_chars: int = 220) -> bool:
    if not (3 <= len(s) <= max_chars):
        return False
    n = len(s.split())
    if not (min_words <= n <= max_words):
        return False
    letters = sum(c.isalpha() for c in s)
    return letters >= 0.6 * len(s.replace(" ", ""))


def builtin(lang: str) -> Corpus:
    p = HERE / "corpus" / f"{lang}.txt"
    lines = [clean_line(x) for x in p.read_text(encoding="utf-8").splitlines() if x.strip()]
    return Corpus("builtin", lang, "GPL-2.0-or-later (xournal-qt)", lines)


def tatoeba(lang: str, cache: Path, limit: int | None = None) -> Corpus:
    l3 = ISO3[lang]
    path = _download(TATOEBA.format(l3=l3), cache / f"tatoeba-{l3}.tsv.bz2")
    out = []
    with bz2.open(path, "rt", encoding="utf-8") as f:
        for row in f:
            parts = row.rstrip("\n").split("\t")
            if len(parts) >= 3:
                s = clean_line(parts[2])
                if good_sentence(s, min_words=1):
                    out.append(s)
    random.Random(0).shuffle(out)
    return Corpus("tatoeba", lang, "CC BY 2.0 FR (Tatoeba)", out[:limit] if limit else out)


def wikipedia(lang: str, cache: Path, shards: int = 1, dump: str = "20231101", limit: int | None = None) -> Corpus:
    from huggingface_hub import HfApi, hf_hub_download
    import pyarrow.parquet as pq

    prefix = f"{dump}.{lang}/"
    files = sorted(f for f in HfApi().list_repo_files("wikimedia/wikipedia", repo_type="dataset")
                   if f.startswith(prefix) and f.endswith(".parquet"))[:shards]
    out = []
    for fn in files:
        p = hf_hub_download("wikimedia/wikipedia", fn, repo_type="dataset", cache_dir=str(cache / "hf"))
        table = pq.read_table(p, columns=["text"])
        for text in table.column("text").to_pylist():
            for para in text.split("\n"):
                if len(para) < 40:
                    continue  # headings, list items
                for s in _SENT.split(para):
                    s = clean_line(s)
                    if good_sentence(s, min_words=4, max_words=25):
                        out.append(s)
            if limit and len(out) >= 2 * limit:
                break
    random.Random(0).shuffle(out)
    return Corpus("wikipedia", lang, "CC BY-SA 4.0 / GFDL (Wikipedia)", out[:limit] if limit else out)


def textfile(lang: str, paths: list[str], licence: str) -> Corpus:
    out = []
    for p in paths:
        for para in Path(os.path.expanduser(p)).read_text(encoding="utf-8").split("\n"):
            for s in _SENT.split(para):
                s = clean_line(s)
                if good_sentence(s, min_words=1):
                    out.append(s)
    return Corpus("textfile", lang, licence, out)


def load(spec: dict, lang: str, cache: Path) -> Corpus:
    kind = spec["source"]
    limit = spec.get("limit")
    if kind == "builtin":
        return builtin(lang)
    if kind == "tatoeba":
        return tatoeba(lang, cache, limit)
    if kind == "wikipedia":
        return wikipedia(lang, cache, int(spec.get("shards", 1)), spec.get("dump", "20231101"), limit)
    if kind == "textfile":
        return textfile(lang, list(spec["paths"]), spec.get("licence", "own text"))
    raise ValueError(f"unknown corpus source {kind!r}")


def lines_from(sentences: list[str], n: int, rnd: random.Random, min_words: int = 1, max_words: int = 8) -> list[str]:
    """`n` lines of min..max words, cut from the sentences in order (a line never spans two sentences)."""
    if not sentences:
        raise ValueError("empty corpus")
    out = []
    while len(out) < n:
        words = rnd.choice(sentences).split()
        i = 0
        while i < len(words) and len(out) < n:
            k = rnd.randint(min_words, max_words)
            out.append(" ".join(words[i:i + k]))
            i += k
    return out
