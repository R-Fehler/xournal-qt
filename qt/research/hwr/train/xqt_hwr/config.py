"""YAML configs: `extends: other.yaml` (relative to the file) merges a base config under this one; `--set a.b=value`
on the command line overrides single keys (the value is read as YAML)."""
from __future__ import annotations

import copy
import os
from pathlib import Path

import yaml


def merge(base: dict, over: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def load(path: str | os.PathLike, overrides: list[str] | None = None) -> dict:
    path = Path(path)
    with open(path, encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    if "extends" in cfg:
        base = load(path.parent / cfg.pop("extends"))
        cfg = merge(base, cfg)
    for o in overrides or []:
        key, _, val = o.partition("=")
        d = cfg
        parts = key.split(".")
        for p in parts[:-1]:
            d = d.setdefault(p, {})
        d[parts[-1]] = yaml.safe_load(val)
    cfg["_file"] = str(path)
    return cfg
