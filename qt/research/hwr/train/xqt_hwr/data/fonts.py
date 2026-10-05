"""Handwriting fonts for synthetic lines, fetched at run time from Google Fonts (configs/fonts.yaml lists the families).

The CSS API (fonts.googleapis.com/css2?family=...) answers a client that is not a browser with one TrueType file per
family; that file is saved as <dir>/<Family>.ttf. Each font's licence is read from the font itself (OpenType name
records 13 and 14) and must be the SIL Open Font License or Apache 2.0; other fonts are left out unless allowed.
`fonts.json` in the folder lists what was fetched, with the licences (the synthetic dataset's licence names them).
"""
from __future__ import annotations

import json
import re
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

CSS = "https://fonts.googleapis.com/css2?family={family}"
_URL = re.compile(r"url\((https://[^)]+\.ttf)\)")
OK_LICENCES = (("OFL", ("ofl", "open font license")), ("Apache-2.0", ("apache",)))


@dataclass
class Font:
    family: str
    path: Path
    licence: str
    chars: frozenset

    def covers(self, text: str) -> bool:
        return all(c in self.chars or c.isspace() for c in text)


def licence_of(path: Path) -> str:
    from fontTools.ttLib import TTFont

    f = TTFont(str(path), lazy=True)
    name = f["name"]
    text = " ".join(filter(None, [name.getDebugName(13), name.getDebugName(14)])).lower()
    for short, keys in OK_LICENCES:
        if any(k in text for k in keys):
            return short
    return text[:120] or "unknown"


def chars_of(path: Path) -> frozenset:
    from fontTools.ttLib import TTFont

    cmap = TTFont(str(path), lazy=True).getBestCmap() or {}
    return frozenset(chr(c) for c in cmap)


def download(families: list[str], dst: Path, allow_other_licences: bool = False) -> list[dict]:
    dst.mkdir(parents=True, exist_ok=True)
    out = []
    for fam in families:
        path = dst / (fam.replace(" ", "") + ".ttf")
        try:
            if not path.exists():
                url = CSS.format(family=urllib.parse.quote_plus(fam))
                req = urllib.request.Request(url, headers={"User-Agent": "xqt-hwr-train"})
                css = urllib.request.urlopen(req, timeout=30).read().decode()
                m = _URL.search(css)
                if not m:
                    raise RuntimeError("no TrueType file in the CSS")
                req = urllib.request.Request(m.group(1), headers={"User-Agent": "xqt-hwr-train"})
                path.write_bytes(urllib.request.urlopen(req, timeout=60).read())
            lic = licence_of(path)
            if lic not in ("OFL", "Apache-2.0") and not allow_other_licences:
                print(f"skipping {fam}: licence {lic!r}", file=sys.stderr)
                path.unlink()
                continue
            out.append({"family": fam, "file": path.name, "licence": lic})
            print(f"font {fam}: {lic}", file=sys.stderr)
        except Exception as e:  # one font failing doesn't stop the others
            print(f"font {fam}: {e}", file=sys.stderr)
    with open(dst / "fonts.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    return out


def system_fallback() -> list[Path]:
    """Fonts for tests when no handwriting font was fetched (no network)."""
    cands = [Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
             Path("/usr/share/fonts/truetype/freefont/FreeSans.ttf"),
             Path("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"),
             Path("/usr/share/fonts/truetype/freefont/FreeSerifItalic.ttf")]
    try:
        import matplotlib  # noqa: F401  (ships DejaVu)
        cands.append(Path(matplotlib.__file__).parent / "mpl-data/fonts/ttf/DejaVuSans.ttf")
    except Exception:
        pass
    return [p for p in cands if p.exists()]


def load(dir: Path | None, allow_system: bool = False) -> list[Font]:
    fonts = []
    if dir is not None and (dir / "fonts.json").exists():
        for e in json.loads((dir / "fonts.json").read_text(encoding="utf-8")):
            p = dir / e["file"]
            if p.exists():
                fonts.append(Font(e["family"], p, e["licence"], chars_of(p)))
    if not fonts and allow_system:
        for p in system_fallback():
            fonts.append(Font(p.stem, p, "system font (tests only)", chars_of(p)))
    return fonts
