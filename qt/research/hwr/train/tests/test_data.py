import io
import json
import random
from collections import Counter
from pathlib import Path

import numpy as np
import pytest
import torch
from PIL import Image

from xqt_hwr import text, wordmatch
from xqt_hwr.data import ink, synthetic, sources, hfparquet
from xqt_hwr.data.augment import LineAugment
from xqt_hwr.data.dataset import DatasetInfo, read_dataset
from xqt_hwr.data.images import frame_line, ink_box, LINE_PX
from xqt_hwr.data.mix import DistributedWeightedSampler, Lines, load_sources
from xqt_hwr.prepare import check


def test_words_fold_like_the_app():
    assert text.words("Größe, STRASSE und ﬁlter-Bank 42") == ["größe", "strasse", "und", "filter", "bank", "42"]
    assert text.fold("ẞ") == "ß"
    assert text.clean_line("  äb   c ") == "äb c"


@pytest.mark.parametrize("term,word,in_order,want", [
    ("turbine", "turbines", False, wordmatch.EXACT),
    ("tbine", "turbine", True, wordmatch.FUZZY),
    ("tbine", "tambourine", True, wordmatch.NONE),
    ("klmn", "kalman", True, wordmatch.FUZZY),
    ("klmn", "kalman", False, wordmatch.NONE),
    ("turbnie", "turbine", False, wordmatch.FUZZY),   # a typo, also in the plain search
    ("kalmn", "kalman", False, wordmatch.FUZZY),
    ("abc", "abduct", True, wordmatch.NONE),
    ("abc", "abacus", True, wordmatch.FUZZY),
])
def test_wordmatch_rules(term, word, in_order, want):
    assert wordmatch.match(term, word, in_order=in_order) == want


def test_synthetic_dataset(data_root):
    info, lines = read_dataset(data_root / "synthetic-de")
    assert info.kind == "synthetic" and info.languages == ["de"]
    assert len(lines) >= 30
    assert check(data_root / "synthetic-de") == 0
    # fonts are writers, held out by split
    by = {}
    for l in lines:
        by.setdefault(l.writer, set()).add(l.split)
    assert all(len(s) == 1 for s in by.values())
    with Image.open(lines[0].image) as im:
        assert im.size[1] == LINE_PX and im.mode == "L"
    assert any(c in "äöüßÄÖÜ„“" for l in lines for c in l.text)


def test_frame_line_crops_and_pads():
    img = Image.new("L", (400, 200), 255)
    img.paste(0, (100, 80, 300, 120))
    out = frame_line(img)
    assert out.size[1] == LINE_PX
    # ink 40 px high + 10 px margins -> ink spans 2/3 of the height, the width scales with it
    box = ink_box(out)
    assert abs((box[3] - box[1]) / LINE_PX - 40 / 60) < 0.05
    assert abs(out.size[0] - 220 * LINE_PX / 60) < 3


def test_ink_render_like_line_image():
    data = {"strokes": [{"points": [[0, 0, 1], [30, 10, 1]], "width": 1.0}, {"points": [[40, 0]], "width": 2}]}
    img = ink.render(data)
    assert img.size[1] == LINE_PX
    # box 40 x 10 points; pad = max(2, 2.5) = 2.5 -> scale 128 / 15
    assert abs(img.size[0] - round((40 + 5) * 128 / 15)) <= 1
    a = np.asarray(img)
    assert a.min() < 50 and a[0, 0] == 255
    aug = ink.augment_strokes(data, random.Random(1))
    assert len(aug["strokes"]) == 2 and len(aug["strokes"][0]["points"]) == 2


def test_ink_dataset_with_strokes(data_root):
    info, lines = read_dataset(data_root / "ink-user")
    assert info.kind == "ink" and all(l.strokes is not None for l in lines)
    assert check(data_root / "ink-user") == 0
    srcs = load_sources([{"name": "ink-user", "render_from_strokes": True}], str(data_root))
    ds = Lines(srcs, "train", lambda img, t: (img.size, t), augment=None)
    (size, t), _ = ds[0]
    assert size[1] == LINE_PX and t


def test_augment_keeps_a_line():
    torch.manual_seed(0)
    img = frame_line(Image.new("L", (300, 60), 255))
    aug = LineAugment({"p": 1.0, "slant": 1.0, "elastic": 1.0, "morph": 1.0, "contrast": 1.0, "blur": 1.0,
                       "noise": 1.0, "rotate": 1.0, "scale": 1.0})
    for _ in range(5):
        out = aug(img)
        assert out.mode == "L" and out.size[1] == img.size[1]
    with pytest.raises(ValueError):
        LineAugment({"nonsense": 1})


def test_mixing_weights_and_sampler(data_root):
    srcs = load_sources([{"name": "synthetic-de", "weight": 3}, {"name": "synthetic-en", "weight": 1},
                         {"name": "not-there", "optional": True}], str(data_root))
    ds = Lines(srcs, "train", lambda img, t: t)
    w = ds.weights()
    share_de = sum(float(w[i]) for i, (_, s) in enumerate(ds.items) if s.info.name == "synthetic-de") / float(w.sum())
    assert abs(share_de - 0.75) < 1e-6
    a = list(DistributedWeightedSampler(w, 400, rank=0, world=2, seed=5))
    b = list(DistributedWeightedSampler(w, 400, rank=1, world=2, seed=5))
    assert len(a) == len(b) == 200
    drawn = Counter(ds.items[i][1].info.name for i in a + b)
    assert 0.65 < drawn["synthetic-de"] / 400 < 0.85


def _png(color=0):
    img = Image.new("L", (120, 30), 255)
    img.paste(color, (10, 8, 100, 22))
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def test_hf_parquet_and_fhswf_conversion(tmp_path):
    import pyarrow as pa
    import pyarrow.parquet as pq

    rows = {"image": [{"bytes": _png(), "path": f"w{i // 4:02d}_l{i}.png"} for i in range(20)],
            "text": [f"Zeile {i} mit Größe" for i in range(20)]}
    d = tmp_path / "src" / "data"
    d.mkdir(parents=True)
    pq.write_table(pa.table(rows), d / "train-00000-of-00001.parquet")
    n = sources.fhswf(tmp_path / "out", tmp_path / "cache", source=tmp_path / "src", writer_regex=r"^(w\d+)_")
    assert n == 20
    info, lines = read_dataset(tmp_path / "out")
    assert info.licence == "AFL-3.0" and {l.writer for l in lines} == {f"w{i:02d}" for i in range(5)}
    assert check(tmp_path / "out") == 0
    # without writer ids: blocks of consecutive lines
    n = sources.fhswf(tmp_path / "out2", tmp_path / "cache", source=tmp_path / "src", pseudo_writers=5)
    _, lines = read_dataset(tmp_path / "out2")
    assert len({l.writer for l in lines}) == 5
    # a hub dataset with its own split files (Teklia/IAM-line style) keeps them
    pq.write_table(pa.table(rows), d / "validation-00000-of-00001.parquet")
    info = DatasetInfo(name="x", languages=["en"], licence="test")
    hfparquet.convert(sorted(d.glob("*.parquet")), tmp_path / "out3", info, "en", keep_splits=True)
    _, lines = read_dataset(tmp_path / "out3")
    assert Counter(l.split for l in lines) == {"train": 20, "val": 20}


def test_iam_official_layout(tmp_path):
    src = tmp_path / "iam"
    (src / "ascii").mkdir(parents=True)
    (src / "lines/a01/a01-000u").mkdir(parents=True)
    (src / "lines/b02/b02-001").mkdir(parents=True)
    (src / "ascii/lines.txt").write_text(
        "# comment\n"
        "a01-000u-00 ok 154 19 408 746 1661 89 A|MOVE|to|stop|Mr.|Gaitskell|from\n"
        "a01-000u-01 ok 156 19 395 932 1850 105 nominating|any|more|Labour|life|Peers|,|he|'s\n"
        "b02-001-00 err 156 19 395 932 1850 105 Hello|world|.\n")
    (src / "ascii/forms.txt").write_text("a01-000u 000 2 prt 7 5 52 36\nb02-001 001 2 prt 7 5 52 36\n")
    for lid in ("a01-000u-00", "a01-000u-01"):
        (src / f"lines/a01/a01-000u/{lid}.png").write_bytes(_png())
    (src / "lines/b02/b02-001/b02-001-00.png").write_bytes(_png())
    assert sources.iam(tmp_path / "out", src, tmp_path / "cache") == 3
    info, lines = read_dataset(tmp_path / "out")
    assert info.noncommercial
    t = {l.id: l.text for l in lines}
    assert t["a01-000u-01"] == "nominating any more Labour life Peers, he's"
    assert {l.writer for l in lines} == {"000", "001"}


def test_cvl_layout(tmp_path):
    src = tmp_path / "cvl/trainset"
    (src / "lines/0001").mkdir(parents=True)
    (src / "words/0001").mkdir(parents=True)
    for tx, ws in (("1", ["Imagine", "a", "vast"]), ("6", ["Die", "Größe", "ist"])):
        Image.open(io.BytesIO(_png())).save(src / f"lines/0001/0001-{tx}-0.tif")
        for k, wd in enumerate(ws):
            Image.open(io.BytesIO(_png())).save(src / f"words/0001/0001-{tx}-0-{k}-{wd}.tif")
    assert sources.cvl(tmp_path / "out", tmp_path / "cvl", tmp_path / "cache") == 2
    _, lines = read_dataset(tmp_path / "out")
    t = {l.text: l.lang for l in lines}
    assert t == {"Imagine a vast": "en", "Die Größe ist": "de"}
