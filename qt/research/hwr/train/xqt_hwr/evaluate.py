"""evaluate.py: how well a model serves the search, per dataset, language and writer.

  evaluate.py run --model runs/de-trocr/checkpoints/best --datasets fhswf-german synthetic-de --out reports/de-trocr
  evaluate.py run --model ~/models/trocr-small-de --datasets fhswf-german     an exported folder, through ONNX Runtime
  evaluate.py compare --model A --model B --app-model --datasets fhswf-german iam-lines --out reports/compare
  evaluate.py compare --reports reports/a.json reports/b.json --out reports/compare

A model is a training checkpoint (PyTorch), an exported model folder (model.json: ONNX Runtime, decoded as the app
decodes: what ships), or the app's current English model (`--app-model`: the Xenova TrOCR-small in
~/.local/share/xournal-qt/models/trocr-small-hw-int8, if present). Lines come from the datasets' test split by default.

Measures (metrics.py): CER and WER of the best reading; words found (terms of 3+ letters found among the readings by
the app's plain search, which tolerates a typo in terms of 5+ letters), also exact-only, with the fuzzy search, and
from the best reading alone; false hits; seconds per line. Reports are JSON plus a Markdown table.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import time
from pathlib import Path

from . import metrics
from .data.dataset import read_dataset, resolve
from .recognizers import open_model

APP_MODEL = "~/.local/share/xournal-qt/models/trocr-small-hw-int8"
SHOWN = ("words_found", "words_found_exact", "words_found_fuzzy", "words_found_top1", "cer", "wer", "false_hits")


def app_model_dir() -> Path | None:
    for p in (os.environ.get("XQT_HWR_MODEL"), APP_MODEL):
        if p and (Path(os.path.expanduser(p)) / "model.json").exists():
            return Path(os.path.expanduser(p))
    return None


def lines_of(datasets: list[str], data_root: str | None, split: str, max_lines: int | None, seed: int = 7):
    out = []
    for name in datasets:
        info, lines = read_dataset(resolve(name, data_root))
        ls = [l for l in lines if l.split == split]
        if max_lines and len(ls) > max_lines:
            ls = random.Random(seed).sample(ls, max_lines)
        out += ls
    return out


def evaluate(model_path: str | Path, lines, device: str = "cpu", topk: int | None = None, threads: int = 2,
             samples: int = 5, progress: bool = True) -> dict:
    from PIL import Image

    rec = open_model(model_path, device=device, k=topk, threads=threads)
    texts = [l.text for l in lines]
    rnd = random.Random(0)
    g = metrics.Groups()
    shown, spent = [], 0.0
    for n, l in enumerate(lines):
        with Image.open(l.image) as im:
            img = im.convert("L")
        t = time.perf_counter()
        readings = rec.read(img)
        spent += time.perf_counter() - t
        c = metrics.score_line(l.text, readings, texts, rnd)
        g.add(["all", f"dataset:{l.dataset}", f"lang:{l.lang}", f"writer:{l.dataset}/{l.writer}"], c)
        if len(shown) < samples:
            shown.append({"id": l.id, "truth": l.text, "readings": [[t, round(p, 3)] for t, p in readings]})
        if progress and (n + 1) % 100 == 0:
            print(f"  {n + 1}/{len(lines)}", flush=True)
    return {"model": getattr(rec, "name", str(model_path)), "path": str(model_path), "kind": rec.kind,
            "languages": getattr(rec, "languages", []), "lines": len(lines),
            "seconds_per_line": round(spent / max(1, len(lines)), 4), "groups": g.table(), "samples": shown}


def _pct(v):
    return "–" if v is None else f"{100 * v:.1f}"


def markdown(reports: list[dict], writers: bool = True) -> str:
    out = []
    keys = [k for k in reports[0]["groups"] if not k.startswith("writer:")]
    out.append("| group | model | lines | terms | " + " | ".join(SHOWN) + " | s/line |")
    out.append("|---|---|---:|---:|" + "---:|" * (len(SHOWN) + 1))
    for k in keys:
        for r in reports:
            g = r["groups"].get(k)
            if not g:
                continue
            out.append(f"| {k} | {r['model']} | {g['lines']} | {g['terms']} | "
                       + " | ".join(_pct(g[s]) for s in SHOWN) + f" | {r['seconds_per_line']:.3f} |")
    out.append("")
    out.append("Percentages. words_found: the app's plain search over the readings (typo tolerance for 5+ letters); "
               "_exact: containment only; _fuzzy: with the fuzzy search; _top1: best reading only; false_hits: words "
               "of other lines matched. CER/WER: best reading.")
    if writers:
        out.append("")
        out.append("Words found per writer:")
        out.append("")
        ws = sorted({k for r in reports for k in r["groups"] if k.startswith("writer:")})
        out.append("| writer | " + " | ".join(r["model"] for r in reports) + " |")
        out.append("|---|" + "---:|" * len(reports))
        for w in ws:
            out.append(f"| {w[7:]} | " + " | ".join(_pct((r["groups"].get(w) or {}).get("words_found"))
                                                      for r in reports) + " |")
    return "\n".join(out) + "\n"


def write(reports: list[dict], out: str | None):
    md = markdown(reports)
    print(md)
    if out:
        p = Path(out)
        p.parent.mkdir(parents=True, exist_ok=True)
        data = reports[0] if len(reports) == 1 else {"models": reports}
        p.with_suffix(".json").write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
        p.with_suffix(".md").write_text(md, encoding="utf-8")
        print(f"wrote {p.with_suffix('.json')} and {p.with_suffix('.md')}")


def load_reports(paths: list[str]) -> list[dict]:
    out = []
    for p in paths:
        d = json.loads(Path(p).read_text(encoding="utf-8"))
        out += d["models"] if "models" in d else [d]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(prog="evaluate.py", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("run", "compare"):
        p = sub.add_parser(name)
        p.add_argument("--model", action="append", default=[], help="checkpoint or model folder (repeatable)")
        p.add_argument("--app-model", action="store_true", help="also the app's current English model, if present")
        p.add_argument("--datasets", nargs="+", default=[])
        p.add_argument("--data-root")
        p.add_argument("--split", default="test", choices=("train", "val", "test"))
        p.add_argument("--max-lines", type=int, default=None, help="per dataset (a fixed random sample)")
        p.add_argument("--topk", type=int, help="beams (default: the app's: 4 TrOCR, 5 CTC)")
        p.add_argument("--device", default="cpu", help="for checkpoints: cpu or cuda")
        p.add_argument("--threads", type=int, default=2, help="ONNX Runtime threads (the app uses 2)")
        p.add_argument("--out", help="report path without suffix (.json and .md are written)")
        if name == "compare":
            p.add_argument("--reports", nargs="*", default=[], help="existing JSON reports instead of running")
    a = ap.parse_args(argv)
    reports = load_reports(a.reports) if a.cmd == "compare" and a.reports else []
    models = list(a.model)
    if a.app_model:
        d = app_model_dir()
        if d:
            models.append(str(d))
        else:
            print(f"note: no app model in {APP_MODEL} (or $XQT_HWR_MODEL); skipped")
    if models:
        if not a.datasets:
            raise SystemExit("--datasets")
        lines = lines_of(a.datasets, a.data_root, a.split, a.max_lines)
        print(f"{len(lines)} lines ({a.split}) from {', '.join(a.datasets)}")
        for m in models:
            print(f"evaluating {m}")
            reports.append(evaluate(m, lines, a.device, a.topk, a.threads))
    if not reports:
        raise SystemExit("nothing to evaluate (--model, --app-model or --reports)")
    write(reports, a.out)


if __name__ == "__main__":
    main()
