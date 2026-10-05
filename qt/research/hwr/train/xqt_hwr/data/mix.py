"""Datasets mixed by weight for training (config `datasets:`), and their val/test lines for evaluation.

    datasets:
      - {name: fhswf-german, weight: 0.5}
      - {name: synthetic-de, weight: 0.3}
      - {name: cvl-lines, weight: 0.2, langs: [de], optional: true}

A dataset's weight is its share of the training samples, whatever its size (lines of small sets repeat, lines of big
sets are sampled). `langs` keeps only lines of those languages; `optional` skips a dataset that was not prepared;
`render_from_strokes` draws ink lines from their strokes with stroke augmentation (ink datasets with strokes).
"""
from __future__ import annotations

import math
import random
import sys
from dataclasses import dataclass
from pathlib import Path

import torch
from PIL import Image
from torch.utils.data import Dataset, Sampler

from . import ink
from .dataset import DatasetInfo, Line, read_dataset, resolve


@dataclass
class Source:
    info: DatasetInfo
    lines: list[Line]
    weight: float
    render_from_strokes: bool = False


def load_sources(specs: list[dict], data_root: str | None, seed: int = 1234) -> list[Source]:
    out = []
    for spec in specs:
        path = resolve(spec["name"], data_root)
        if not (path / "dataset.json").exists():
            if spec.get("optional"):
                print(f"note: optional dataset {spec['name']} not found at {path}; skipped", file=sys.stderr)
                continue
            raise SystemExit(f"dataset {spec['name']} not found at {path} (prepare it first)")
        info, lines = read_dataset(path, seed)
        langs = spec.get("langs")
        if langs:
            lines = [l for l in lines if l.lang in langs]
        if spec.get("limit"):
            lines = lines[: int(spec["limit"])]
        if info.noncommercial:
            print(f"note: {info.name} is licensed for non-commercial use ({info.licence}); "
                  "the model's manifest will say so", file=sys.stderr)
        out.append(Source(info, lines, float(spec.get("weight", 1.0)), bool(spec.get("render_from_strokes"))))
    if not out:
        raise SystemExit("no datasets")
    return out


class Lines(Dataset):
    """Lines of one split (several datasets), each read as a framed grey picture and passed through `transform`
    (a function of the picture and the text giving the model's sample)."""

    def __init__(self, sources: list[Source], split: str, transform, augment=None, max_lines: int | None = None,
                 seed: int = 0):
        self.items: list[tuple[Line, Source]] = [(l, s) for s in sources for l in s.lines if l.split == split]
        if max_lines and len(self.items) > max_lines:
            rnd = random.Random(seed)
            self.items = rnd.sample(self.items, max_lines)
        self.transform = transform
        self.augment = augment
        self.split = split

    def weights(self) -> torch.Tensor:
        """Per-line sampling weights: each dataset's weight shared among its lines of this split."""
        count: dict[int, int] = {}
        for _, s in self.items:
            count[id(s)] = count.get(id(s), 0) + 1
        w = [s.weight / count[id(s)] for _, s in self.items]
        return torch.tensor(w, dtype=torch.double)

    def __len__(self):
        return len(self.items)

    def picture(self, i: int, train: bool) -> Image.Image:
        line, src = self.items[i]
        if src.render_from_strokes and line.strokes is not None:
            strokes = ink.load_strokes(line.strokes)
            if train:
                strokes = ink.augment_strokes(strokes, random.Random(int(torch.randint(1 << 30, (1,)))))
            return ink.render(strokes)
        with Image.open(line.image) as im:
            return im.convert("L")

    def __getitem__(self, i: int):
        img = self.picture(i, self.augment is not None)
        if self.augment is not None:
            img = self.augment(img)
        line, _ = self.items[i]
        return self.transform(img, line.text), i


class DistributedWeightedSampler(Sampler):
    """Samples drawn with replacement by weight, `num_samples` per epoch in all, each rank its own share (as
    DistributedSampler shares a dataset). The same seed and epoch give every rank the same draw."""

    def __init__(self, weights: torch.Tensor, num_samples: int, rank: int = 0, world: int = 1, seed: int = 0):
        self.weights = weights
        self.rank, self.world, self.seed = rank, world, seed
        self.per_rank = int(math.ceil(num_samples / world))
        self.epoch = 0

    def set_epoch(self, epoch: int):
        self.epoch = epoch

    def __iter__(self):
        g = torch.Generator()
        g.manual_seed(self.seed + 7919 * self.epoch)
        idx = torch.multinomial(self.weights, self.per_rank * self.world, replacement=True, generator=g)
        return iter(idx[self.rank::self.world].tolist())

    def __len__(self):
        return self.per_rank


def licences(sources: list[Source]) -> list[dict]:
    return [{"name": s.info.name, "licence": s.info.licence, "noncommercial": s.info.noncommercial,
             "source": s.info.source, "languages": s.info.languages} for s in sources]
