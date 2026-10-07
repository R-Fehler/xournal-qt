"""Line datasets in the layout of FORMATS.md section 1: dataset.json, lines.jsonl, images/<id>.png, strokes/<id>.json.

`DatasetWriter` writes one (the preparers use it), `read_dataset` reads one and fills in missing splits by writer.
"""
from __future__ import annotations

import json
import os
import random
from collections import Counter
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from ..text import clean_line

SPLITS = ("train", "val", "test")
KINDS = ("scan", "ink", "synthetic")


@dataclass
class DatasetInfo:
    name: str
    languages: list[str]
    licence: str
    source: str = ""
    kind: str = "scan"
    version: int = 1
    noncommercial: bool = False
    extra: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        d = {"name": self.name, "version": self.version, "languages": self.languages, "licence": self.licence,
             "source": self.source, "kind": self.kind}
        if self.noncommercial:
            d["noncommercial"] = True
        d.update(self.extra)
        return d

    @staticmethod
    def from_json(d: dict) -> "DatasetInfo":
        known = {"name", "version", "languages", "licence", "source", "kind", "noncommercial"}
        return DatasetInfo(name=d["name"], languages=list(d.get("languages", [])), licence=d.get("licence", ""),
                           source=d.get("source", ""), kind=d.get("kind", "scan"), version=int(d.get("version", 1)),
                           noncommercial=bool(d.get("noncommercial", False)),
                           extra={k: v for k, v in d.items() if k not in known})


@dataclass
class Line:
    id: str
    image: Path          # absolute
    text: str            # NFC
    lang: str
    writer: str
    split: str
    strokes: Path | None = None
    dataset: str = ""


_SAFE = re.compile(r"[^A-Za-z0-9._-]+")


def safe_id(s: str) -> str:
    return _SAFE.sub("_", s).strip("_") or "line"


class DatasetWriter:
    """Writes a dataset folder. Lines are appended; `close()` writes dataset.json (and the split summary)."""

    def __init__(self, root: str | os.PathLike, info: DatasetInfo, overwrite: bool = False):
        self.root = Path(root)
        self.info = info
        if overwrite and (self.root / "lines.jsonl").exists():
            (self.root / "lines.jsonl").unlink()
        (self.root / "images").mkdir(parents=True, exist_ok=True)
        self._lines = open(self.root / "lines.jsonl", "a", encoding="utf-8")
        self._ids: set[str] = set()
        self.count = 0

    def add(self, id: str, image: Image.Image | bytes | str | os.PathLike, text: str, lang: str, writer: str,
            split: str | None = None, strokes: dict | None = None) -> str:
        id = safe_id(id)
        base, n = id, 1
        while id in self._ids:
            n += 1
            id = f"{base}-{n}"
        self._ids.add(id)
        rel = f"images/{id}.png"
        dst = self.root / rel
        if isinstance(image, Image.Image):
            image.convert("L").save(dst)
        elif isinstance(image, (bytes, bytearray)):
            import io
            Image.open(io.BytesIO(image)).convert("L").save(dst)
        else:
            Image.open(image).convert("L").save(dst)
        rec = {"id": id, "image": rel, "text": clean_line(text), "lang": lang, "writer": str(writer)}
        if split:
            if split not in SPLITS:
                raise ValueError(f"split {split!r}")
            rec["split"] = split
        if strokes is not None:
            (self.root / "strokes").mkdir(exist_ok=True)
            srel = f"strokes/{id}.json"
            with open(self.root / srel, "w", encoding="utf-8") as f:
                json.dump(strokes, f)
            rec["strokes"] = srel
        self._lines.write(json.dumps(rec, ensure_ascii=False) + "\n")
        self.count += 1
        return id

    def close(self):
        self._lines.close()
        with open(self.root / "dataset.json", "w", encoding="utf-8") as f:
            json.dump(self.info.to_json(), f, ensure_ascii=False, indent=2)
            f.write("\n")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def assign_writer_splits(writers: list[str], seed: int = 1234, val: float = 0.1, test: float = 0.1) -> dict[str, str]:
    """Splits by writer with a fixed seed (a test never sees a training writer's hand). `writers` has one entry per
    line, so test and validation get about their share of the LINES, not of the writers: groups of very different
    sizes (fhswf's capture days: 7 to 898 lines) would otherwise make a test set of half or twice its share. Writers
    are taken in a shuffled order; one that would overshoot a share by more than it fills is left for the next.
    Fewer than 3 writers: all train (the caller warns)."""
    count = Counter(writers)
    ws = sorted(count)
    if len(ws) < 3:
        return {w: "train" for w in ws}
    rnd = random.Random(seed)
    rnd.shuffle(ws)
    total = sum(count.values())
    want = {"test": test * total, "val": val * total}
    have = {"test": 0, "val": 0}
    out = {}
    for w in ws:
        n = count[w]
        out[w] = "train"
        for split in ("test", "val"):
            if have[split] < want[split] and (have[split] == 0 or have[split] + n - want[split] <= want[split] - have[split]):
                out[w] = split
                have[split] += n
                break
    if len(ws) - sum(1 for v in out.values() if v != "train") < 1:
        out[ws[-1]] = "train"
    return out

def read_info(root: str | os.PathLike) -> DatasetInfo:
    with open(Path(root) / "dataset.json", encoding="utf-8") as f:
        return DatasetInfo.from_json(json.load(f))


def read_dataset(root: str | os.PathLike, seed: int = 1234) -> tuple[DatasetInfo, list[Line]]:
    root = Path(root)
    info = read_info(root)
    raw = []
    with open(root / "lines.jsonl", encoding="utf-8") as f:
        for n, s in enumerate(f, 1):
            s = s.strip()
            if not s:
                continue
            d = json.loads(s)
            if "writer" not in d or "text" not in d or "image" not in d:
                raise ValueError(f"{root}/lines.jsonl:{n}: id, image, text and writer are required")
            raw.append(d)
    missing = [d for d in raw if d.get("split") not in SPLITS]
    splits = {}
    if missing:
        splits = assign_writer_splits([d["writer"] for d in missing], seed)
        if len(set(splits.values())) == 1 and len(missing) > 0:
            print(f"warning: {info.name}: fewer than 3 writers without a split; all are train", file=sys.stderr)
    lines = []
    for d in raw:
        split = d.get("split") if d.get("split") in SPLITS else splits[d["writer"]]
        lines.append(Line(id=d["id"], image=root / d["image"], text=clean_line(d["text"]),
                          lang=d.get("lang", info.languages[0] if info.languages else ""), writer=str(d["writer"]),
                          split=split, strokes=(root / d["strokes"]) if d.get("strokes") else None,
                          dataset=info.name))
    return info, lines


def resolve(name_or_path: str, data_root: str | os.PathLike | None) -> Path:
    """A dataset given by path, or by name under the data root (config `data_root`, env XQT_HWR_DATA)."""
    p = Path(os.path.expanduser(name_or_path))
    if p.is_absolute() or (p / "dataset.json").exists():
        return p
    root = Path(os.path.expanduser(str(data_root or os.environ.get("XQT_HWR_DATA", "~/hwr-data"))))
    return root / name_or_path
