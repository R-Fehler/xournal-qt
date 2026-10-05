"""prepare.py: fetch and convert datasets into the FORMATS.md layout under the data root.

  prepare.py all --config configs/data.yaml        everything the configs train on (optional sources may fail)
  prepare.py fonts [--list configs/fonts.yaml]     handwriting fonts from Google Fonts
  prepare.py synthetic --lang de --lines 200000 --corpus tatoeba --corpus builtin
  prepare.py fhswf | iam --source DIR | iam --hub | cvl --source DIR | cvl --download
  prepare.py hf --repo OWNER/NAME --name NAME --lang de --licence ...   any parquet line dataset on the hub
  prepare.py check DIR                              validate a dataset (also an ink dataset exported by the app)
"""
from __future__ import annotations

import argparse
import os
import sys
import traceback
from collections import Counter
from pathlib import Path

import yaml

from .data import corpora, fonts as fontlib, hfparquet, sources, synthetic
from .data.dataset import DatasetInfo, read_dataset

HERE = Path(__file__).resolve().parent.parent


def data_root(arg: str | None) -> Path:
    return Path(os.path.expanduser(arg or os.environ.get("XQT_HWR_DATA", "~/hwr-data")))


def check(path: Path) -> int:
    from PIL import Image

    from .data import ink

    info, lines = read_dataset(path)
    by_split = Counter(l.split for l in lines)
    writers = {s: len({l.writer for l in lines if l.split == s}) for s in by_split}
    chars = Counter(c for l in lines for c in l.text)
    problems = 0
    for l in lines[:2000]:
        if not l.image.exists():
            print(f"missing picture {l.image}")
            problems += 1
        if l.strokes is not None:
            if not l.strokes.exists():
                print(f"missing strokes {l.strokes}")
                problems += 1
            else:
                ink.render(ink.load_strokes(l.strokes))
    leak = {l.writer for l in lines if l.split == "train"} & {l.writer for l in lines if l.split != "train"}
    if leak and len({l.writer for l in lines}) > 1:
        print(f"writers in train and val/test: {sorted(leak)[:10]}")
        problems += 1
    elif leak:
        print("one writer, split by line (a user's own data: the held-out lines test the same hand)")
    print(f"{info.name} ({info.kind}, {info.languages}, licence {info.licence!r}"
          f"{', non-commercial' if info.noncommercial else ''}): {len(lines)} lines; "
          f"{dict(by_split)}; writers {writers}; {len(chars)} characters: {''.join(sorted(chars))}")
    if lines:
        with Image.open(lines[0].image) as im:
            print(f"first picture {im.size} {im.mode}")
    return problems


def run_one(spec: dict, root: Path, cache: Path, fonts_dir: Path, workers: int) -> int:
    src = spec["source"]
    if src == "fhswf":
        return sources.fhswf(root / spec.get("name", "fhswf-german"), cache, spec.get("path"),
                             spec.get("writer_column"), spec.get("writer_regex"), spec.get("pseudo_writers", 15),
                             spec.get("limit"))
    if src == "iam":
        return sources.iam(root / spec.get("name", "iam-lines"), spec.get("path"), cache, bool(spec.get("hub")),
                           spec.get("splits_dir"), spec.get("limit"))
    if src == "cvl":
        return sources.cvl(root / spec.get("name", "cvl-lines"), spec.get("path"), cache,
                           bool(spec.get("download")), spec.get("limit"))
    if src == "hf":
        info = DatasetInfo(name=spec["name"], languages=[spec["lang"]], licence=spec["licence"],
                           source=f"https://huggingface.co/datasets/{spec['repo']}", kind=spec.get("kind", "scan"),
                           noncommercial=bool(spec.get("noncommercial")))
        files = hfparquet.download(spec["repo"], cache, revision=spec.get("revision"))
        return hfparquet.convert(files, root / spec["name"], info, spec["lang"], spec.get("image_column"),
                                 spec.get("text_column"), spec.get("writer_column"), spec.get("writer_regex"),
                                 bool(spec.get("keep_splits", True)), int(spec.get("pseudo_writers", 0)),
                                 limit=spec.get("limit"))
    if src == "synthetic":
        lang = spec["lang"]
        return synthetic.generate(root / spec.get("name", f"synthetic-{lang}"), spec.get("name", f"synthetic-{lang}"),
                                  lang, spec.get("corpora", [{"source": "builtin"}]), fonts_dir,
                                  int(spec.get("lines", 1000)), synthetic.SynthConfig.from_dict(spec.get("render")),
                                  int(spec.get("seed", 0)), workers, cache,
                                  allow_system_fonts=bool(spec.get("allow_system_fonts")))
    raise SystemExit(f"unknown source {src!r}")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="prepare.py", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--data-root", help="where datasets go (default $XQT_HWR_DATA or ~/hwr-data)")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("all")
    a.add_argument("--config", default=str(HERE / "configs/data.yaml"))
    a.add_argument("--only", action="append", help="only these dataset names")
    f = sub.add_parser("fonts")
    f.add_argument("--list", default=str(HERE / "configs/fonts.yaml"))
    s = sub.add_parser("synthetic")
    s.add_argument("--lang", required=True, choices=sorted(corpora.ISO3))
    s.add_argument("--name")
    s.add_argument("--lines", type=int, default=1000)
    s.add_argument("--corpus", action="append", default=None,
                   help="builtin | tatoeba | wikipedia | textfile:PATH (repeatable)")
    s.add_argument("--seed", type=int, default=0)
    s.add_argument("--system-fonts", action="store_true", help="use system fonts if none were fetched (tests)")
    for name in ("fhswf", "iam", "cvl"):
        p = sub.add_parser(name)
        p.add_argument("--source", help="a local copy")
        p.add_argument("--limit", type=int)
        if name == "iam":
            p.add_argument("--hub", action="store_true", help=f"from {sources.IAM_HUB_REPO}")
            p.add_argument("--splits-dir", help="folder with the Aachen split files (trainset.txt, ...)")
        if name == "cvl":
            p.add_argument("--download", action="store_true")
        if name == "fhswf":
            p.add_argument("--writer-column")
            p.add_argument("--writer-regex")
            p.add_argument("--pseudo-writers", type=int, default=15)
    h = sub.add_parser("hf")
    for k in ("repo", "name", "lang", "licence"):
        h.add_argument(f"--{k}", required=True)
    for k in ("image-column", "text-column", "writer-column", "writer-regex", "revision"):
        h.add_argument(f"--{k}")
    h.add_argument("--pseudo-writers", type=int, default=0)
    h.add_argument("--noncommercial", action="store_true")
    c = sub.add_parser("check")
    c.add_argument("path")
    args = ap.parse_args(argv)

    root = data_root(args.data_root)
    cache = root / ".cache"
    fonts_dir = root / "fonts"
    if args.cmd == "check":
        sys.exit(1 if check(Path(args.path)) else 0)
    if args.cmd == "fonts":
        fams = yaml.safe_load(open(args.list, encoding="utf-8"))["families"]
        got = fontlib.download(fams, fonts_dir)
        print(f"{len(got)} of {len(fams)} fonts in {fonts_dir}")
        return
    if args.cmd == "all":
        cfg = yaml.safe_load(open(args.config, encoding="utf-8"))
        if cfg.get("data_root") and not args.data_root:
            root = data_root(cfg["data_root"])
            cache, fonts_dir = root / ".cache", root / "fonts"
        if cfg.get("fonts", True) and not (fonts_dir / "fonts.json").exists():
            fams = yaml.safe_load(open(HERE / "configs/fonts.yaml", encoding="utf-8"))["families"]
            fontlib.download(fams, fonts_dir)
        failed = []
        for spec in cfg["datasets"]:
            name = spec.get("name", spec["source"])
            if args.only and name not in args.only:
                continue
            try:
                n = run_one(spec, root, cache, fonts_dir, args.workers)
                print(f"{name}: {n} lines")
            except (Exception, SystemExit) as e:
                if not spec.get("optional"):
                    raise
                failed.append(name)
                print(f"optional dataset {name} not prepared: {e}", file=sys.stderr)
                traceback.print_exc()
        if failed:
            print(f"not prepared (optional): {failed}")
        return
    if args.cmd == "synthetic":
        specs = []
        for c in args.corpus or ["builtin"]:
            if c.startswith("textfile:"):
                specs.append({"source": "textfile", "paths": [c.split(":", 1)[1]]})
            else:
                specs.append({"source": c})
        spec = {"source": "synthetic", "lang": args.lang, "name": args.name or f"synthetic-{args.lang}",
                "lines": args.lines, "corpora": specs, "seed": args.seed, "allow_system_fonts": args.system_fonts}
    elif args.cmd == "hf":
        spec = {"source": "hf", "repo": args.repo, "name": args.name, "lang": args.lang, "licence": args.licence,
                "image_column": args.image_column, "text_column": args.text_column,
                "writer_column": args.writer_column, "writer_regex": args.writer_regex, "revision": args.revision,
                "pseudo_writers": args.pseudo_writers, "noncommercial": args.noncommercial}
    else:
        spec = {"source": args.cmd, "path": args.source, "limit": args.limit}
        for k in ("hub", "splits_dir", "download", "writer_column", "writer_regex", "pseudo_writers"):
            if hasattr(args, k):
                spec[k] = getattr(args, k)
    n = run_one(spec, root, cache, fonts_dir, args.workers)
    print(f"{n} lines")


if __name__ == "__main__":
    main()
