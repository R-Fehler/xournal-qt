"""Shared fixtures: tiny datasets made here (no network): synthetic lines in system fonts, an ink dataset with
strokes. Everything goes to pytest's temporary folders."""
import os
import random
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.environ.setdefault("OMP_NUM_THREADS", "2")
os.environ.setdefault("HF_HUB_OFFLINE", "1")


def make_ink_dataset(root: Path, n: int = 12, name: str = "ink-user"):
    """An ink dataset as the app exports it: pictures rendered from strokes, the strokes alongside."""
    from xqt_hwr.data import ink
    from xqt_hwr.data.dataset import DatasetInfo, DatasetWriter

    rnd = random.Random(3)
    words = ["Kalman", "Größe", "Messung", "matrix", "Übung", "the", "filter", "Straße"]
    info = DatasetInfo(name=name, languages=["de"], licence="own handwriting", kind="ink")
    with DatasetWriter(root / name, info) as w:
        for i in range(n):
            strokes = []
            x = 0.0
            for _ in range(rnd.randint(3, 6)):
                pts = []
                for k in range(12):
                    pts.append([x + k * 1.5, 10 + 6 * rnd.random(), 0.5 + 0.5 * rnd.random()])
                strokes.append({"points": pts, "width": 1.2})
                x += 22
            data = {"width": x, "height": 20, "strokes": strokes}
            text = " ".join(rnd.sample(words, 2))
            w.add(f"u{i:03d}", ink.render(data), text, "de", "me", ["train", "train", "val", "test"][i % 4], data)
    return root / name


@pytest.fixture(scope="session")
def data_root(tmp_path_factory):
    """Synthetic de and en (system fonts: each font a writer) and an ink dataset."""
    from xqt_hwr.data import synthetic

    root = tmp_path_factory.mktemp("hwr-data")
    cfg = synthetic.SynthConfig(size=40, max_words=4)
    for lang in ("de", "en"):
        synthetic.generate(root / f"synthetic-{lang}", f"synthetic-{lang}", lang, [{"source": "builtin"}], None, 40,
                           cfg, seed=1, workers=1, cache=root / ".cache", allow_system_fonts=True)
    make_ink_dataset(root)
    return root


TINY = ROOT / "configs" / "tiny"


def run_train(cfg, data_root, out, *sets, resume=False):
    from xqt_hwr import train

    args = ["--config", str(TINY / cfg), "--set", f"data_root={data_root}", "--set", f"out_dir={out}"]
    for s in sets:
        args += ["--set", s]
    if resume:
        args.append("--resume")
    train.main(args)


@pytest.fixture(scope="session")
def trained(data_root, tmp_path_factory):
    """Tiny TrOCR and CTC trained for three steps (shared by the evaluation and export tests)."""
    out = tmp_path_factory.mktemp("runs")
    run_train("tiny-trocr.yaml", data_root, out / "trocr")
    run_train("tiny-ctc.yaml", data_root, out / "ctc")
    return out


