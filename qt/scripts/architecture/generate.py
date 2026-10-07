#!/usr/bin/env python3
"""Generates the architecture overview of xournal-qt from qt/docs/architecture/architecture.yaml.

Writes, next to the YAML file:
  architecture.svg   the layered diagram (GitHub shows it in the README; opened on its own, its blocks link to GitHub)
  README.md          the overview for humans: the diagram, every layer and block, how the Qt frontend uses the
                     Xournal++ core, three paths through the code, where to change what
  site/index.html    one self-contained interactive page (inline SVG, vanilla JS, no network), for GitHub Pages

    python3 qt/scripts/architecture/generate.py           # regenerate the three files
    python3 qt/scripts/architecture/generate.py --check   # CI: fail when they are out of date or the YAML is wrong

Both modes check the YAML: its schema, that every path in it exists in the repository, and that the CMake links
between the targets it names (target_link_libraries in qt/cmake/*.cmake) and its `links` edges agree. Python 3's
standard library only; no network.
"""

import argparse
import html
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ARCH_DIR = ROOT / "qt" / "docs" / "architecture"
YAML_FILE = ARCH_DIR / "architecture.yaml"
SVG_FILE = ARCH_DIR / "architecture.svg"
README_FILE = ARCH_DIR / "README.md"
HTML_FILE = ARCH_DIR / "site" / "index.html"
GENERATOR = "qt/scripts/architecture/generate.py"


# ---------------------------------------------------------------------------------------------------------------------
# A small YAML subset: block mappings and lists, one-line plain/quoted scalars, flow lists, folded scalars (>-).
# Strict, so that the file stays valid YAML for every other tool: a plain scalar may not contain ": " or " #" and may
# not start with an indicator character.

class YamlError(Exception):
    pass


KEY_RE = re.compile(r"^([A-Za-z_][\w-]*):(?:[ ]+(.*))?$")
PLAIN_FORBIDDEN_START = set("`@&*!|>%{}[],#'\"?:-")


class _Parser:
    def __init__(self, text, name):
        self.lines = text.split("\n")
        self.i = 0
        self.name = name

    def fail(self, msg, line=None):
        raise YamlError(f"{self.name}:{(self.i if line is None else line) + 1}: {msg}")

    def peek(self):
        """(indent, content) of the next line that is not blank or a comment, or None at the end."""
        while self.i < len(self.lines):
            line = self.lines[self.i]
            stripped = line.strip()
            if stripped and not stripped.startswith("#"):
                lead = line[:len(line) - len(line.lstrip())]
                if "\t" in lead:
                    self.fail("a tab in the indentation")
                return len(lead), stripped
            self.i += 1
        return None

    def document(self):
        p = self.peek()
        if p is None:
            return {}
        if p[0] != 0:
            self.fail("the document must start at column 0")
        result = self.mapping(0)
        if self.peek() is not None:
            self.fail("unexpected indentation")
        return result

    def block(self, indent):
        p = self.peek()
        if p[1] == "-" or p[1].startswith("- "):
            return self.sequence(indent)
        return self.mapping(indent)

    def mapping(self, indent):
        result = {}
        while True:
            p = self.peek()
            if p is None or p[0] < indent:
                return result
            if p[0] > indent:
                self.fail("unexpected indentation")
            if p[1] == "-" or p[1].startswith("- "):
                self.fail("a list item where a key was expected")
            m = KEY_RE.match(p[1])
            if not m:
                self.fail(f"expected 'key: value', got {p[1]!r}")
            key, rest = m.group(1), (m.group(2) or "")
            if key in result:
                self.fail(f"duplicate key {key!r}")
            line = self.i
            self.i += 1
            result[key] = self.value(rest, indent, line)

    def sequence(self, indent):
        items = []
        while True:
            p = self.peek()
            if p is None or p[0] < indent:
                return items
            if p[0] > indent:
                self.fail("unexpected indentation")
            if not (p[1] == "-" or p[1].startswith("- ")):
                self.fail("a key where a list item was expected")
            rest = p[1][1:].lstrip()
            line = self.i
            if not rest:
                self.i += 1
                nxt = self.peek()
                if nxt is None or nxt[0] <= indent:
                    self.fail("an empty list item", line)
                items.append(self.block(nxt[0]))
            elif KEY_RE.match(rest):
                column = indent + (len(p[1]) - len(rest))
                self.lines[self.i] = " " * column + rest  # the item's first key, as if on its own line
                items.append(self.mapping(column))
            else:
                self.i += 1
                items.append(self.inline(rest, line))

    def value(self, rest, indent, line):
        rest = rest.strip()
        if rest == "" or rest.startswith("#"):
            p = self.peek()
            if p is None or p[0] <= indent:
                return None
            return self.block(p[0])
        if rest in (">-", ">"):
            return self.folded(indent, line)
        return self.inline(rest, line)

    def folded(self, indent, line):
        body = []
        block_indent = None
        while self.i < len(self.lines):
            raw = self.lines[self.i]
            if raw.strip() == "":
                body.append("")
                self.i += 1
                continue
            ind = len(raw) - len(raw.lstrip(" "))
            if block_indent is None:
                if ind <= indent:
                    break
                block_indent = ind
            if ind < block_indent:
                break
            if ind > block_indent:
                self.fail("more-indented lines in a folded scalar are not supported")
            body.append(raw.strip())
            self.i += 1
        while body and body[-1] == "":
            body.pop()
            self.i -= 1
        if not body:
            self.fail("an empty folded scalar", line)
        out, blanks = "", 0
        for text in body:
            if text == "":
                blanks += 1
                continue
            if out:
                out += "\n" * blanks if blanks else " "
            out += text
            blanks = 0
        return out

    def inline(self, text, line):
        if text.startswith("["):
            close = _find_flow_end(text)
            if close is None:
                self.fail("a flow list must close on its line", line)
            tail = text[close + 1:].strip()
            if tail and not tail.startswith("#"):
                self.fail("text after a flow list", line)
            inner = text[1:close].strip()
            if not inner:
                return []
            return [self.scalar(part.strip(), line, flow=True) for part in _split_flow(inner)]
        return self.scalar(text, line)

    def scalar(self, text, line, flow=False):
        if text.startswith('"'):
            out, j = [], 1
            while j < len(text):
                c = text[j]
                if c == "\\":
                    nxt = text[j + 1:j + 2]
                    out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\", "/": "/"}.get(nxt))
                    if out[-1] is None:
                        self.fail(f"unknown escape \\{nxt}", line)
                    j += 2
                    continue
                if c == '"':
                    tail = text[j + 1:].strip()
                    if tail and not tail.startswith("#"):
                        self.fail("text after a quoted scalar", line)
                    return "".join(out)
                out.append(c)
                j += 1
            self.fail("an unclosed quote", line)
        if text.startswith("'"):
            m = re.match(r"^'((?:[^']|'')*)'\s*(#.*)?$", text)
            if not m:
                self.fail("a bad single-quoted scalar", line)
            return m.group(1).replace("''", "'")
        cut = text.find(" #")
        if cut >= 0:
            text = text[:cut]
        text = text.strip()
        if not text:
            self.fail("an empty value", line)
        if text[0] in PLAIN_FORBIDDEN_START:
            self.fail(f"a plain scalar may not start with {text[0]!r}: quote it ({text!r})", line)
        if ": " in text or text.endswith(":"):
            self.fail(f"a plain scalar may not contain ': ': quote it ({text!r})", line)
        if flow and any(c in text for c in "[]{}"):
            self.fail(f"brackets in a flow list item: quote it ({text!r})", line)
        return text


def _find_flow_end(text):
    quote = None
    for j, c in enumerate(text):
        if quote:
            if c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "]":
            return j
    return None


def _split_flow(inner):
    parts, current, quote = [], "", None
    for c in inner:
        if quote:
            current += c
            if c == quote:
                quote = None
        elif c in "\"'":
            quote = c
            current += c
        elif c == ",":
            parts.append(current)
            current = ""
        else:
            current += c
    parts.append(current)
    return parts


def parse_yaml(text, name="architecture.yaml"):
    return _Parser(text, name).document()


# ---------------------------------------------------------------------------------------------------------------------
# The model and its checks

BLOCK_KINDS = {
    "qml": "QML UI",
    "app": "app (C++)",
    "qt": "xournal-qt module",
    "compat": "qt/compat",
    "upstream": "Xournal++ core",
    "unbuilt": "Xournal++, not built",
    "data": "shared data",
    "tests": "tests",
    "tools": "tools",
    "platform": "platform",
    "ci": "CI",
}
EDGE_KINDS = {
    "links": "a CMake target links another",
    "calls": "direct use: includes and calls",
    "shadows": "through qt/compat's shadow headers",
    "seam": "a hook or edit in an upstream file (ADR 0002)",
    "data": "reads or writes shared files",
    "qml": "QML to C++: `app`, registered types, image providers, models",
}
TOP_KEYS = {"github", "branch", "pages", "intro", "core_usage", "layers", "blocks", "edges", "flows", "tasks"}
LAYER_KEYS = ({"id", "name", "about"}, {"side"})
BLOCK_KEYS = ({"id", "name", "layer", "kind", "label", "paths", "responsibility", "key"}, {"target", "docs", "w", "row"})
KEY_KEYS = ({"name", "path", "what"}, set())
EDGE_KEYS = ({"from", "to", "kind", "what"}, {"hide"})
FLOW_KEYS = ({"id", "name", "steps"}, set())
STEP_KEYS = ({"block", "file", "text"}, set())
TASK_KEYS = ({"task", "qt", "upstream"}, {"note"})


class Model:
    def __init__(self, data):
        self.data = data
        self.github = data["github"].rstrip("/")
        self.branch = data["branch"]
        self.pages = data["pages"]
        self.layers = data["layers"]
        self.layer_by_id = {l["id"]: l for l in self.layers}
        self.blocks = data["blocks"]
        self.block_by_id = {b["id"]: b for b in self.blocks}
        self.edges = data["edges"]
        self.flows = data.get("flows") or []
        self.tasks = data.get("tasks") or []
        for b in self.blocks:
            b.setdefault("docs", [])
            b["row"] = int(b.get("row", 1))
            b["w"] = float(b.get("w", 1))
        for e in self.edges:
            e["hide"] = str(e.get("hide", "false")).lower() == "true"

    def out_edges(self, bid):
        return [e for e in self.edges if e["from"] == bid]

    def in_edges(self, bid):
        return [e for e in self.edges if e["to"] == bid]

    def github_url(self, path):
        kind = "tree" if (ROOT / path).is_dir() else "blob"
        return f"{self.github}/{kind}/{self.branch}/{path.rstrip('/')}"


def _check_keys(obj, keys, where, problems):
    required, optional = keys
    if not isinstance(obj, dict):
        problems.append(f"{where}: expected a mapping")
        return False
    missing = required - obj.keys()
    unknown = obj.keys() - required - optional
    for k in sorted(missing):
        problems.append(f"{where}: missing '{k}'")
    for k in sorted(unknown):
        problems.append(f"{where}: unknown key '{k}'")
    return not missing


def _check_path(path, where, problems):
    if not isinstance(path, str) or not path or path.startswith("/") or ".." in path.split("/"):
        problems.append(f"{where}: bad path {path!r}")
        return
    p = ROOT / path
    if path.endswith("/"):
        if not p.is_dir():
            problems.append(f"{where}: no folder {path}")
    elif not p.is_file():
        problems.append(f"{where}: no file {path}" + (" (a folder: end it with '/')" if p.is_dir() else ""))


def validate(data):
    """The problems of the YAML (schema, references, paths), as messages."""
    problems = []
    for k in sorted(data.keys() - TOP_KEYS):
        problems.append(f"unknown top-level key '{k}'")
    for k in ("github", "branch", "pages", "intro", "core_usage", "layers", "blocks", "edges"):
        if k not in data:
            problems.append(f"missing top-level key '{k}'")
    if problems:
        return problems
    ids = set()
    for i, layer in enumerate(data["layers"]):
        if _check_keys(layer, LAYER_KEYS, f"layers[{i}]", problems):
            if layer["id"] in ids:
                problems.append(f"layers[{i}]: duplicate id {layer['id']}")
            ids.add(layer["id"])
    layer_ids = {l.get("id") for l in data["layers"]}
    block_ids = set()
    for i, b in enumerate(data["blocks"]):
        where = f"block {b.get('id', i)}"
        if not _check_keys(b, BLOCK_KEYS, where, problems):
            continue
        if b["id"] in block_ids:
            problems.append(f"{where}: duplicate id")
        block_ids.add(b["id"])
        if not re.fullmatch(r"[a-z0-9-]+", b["id"]):
            problems.append(f"{where}: an id is lower case letters, digits and '-'")
        if b["layer"] not in layer_ids:
            problems.append(f"{where}: unknown layer {b['layer']}")
        if b["kind"] not in BLOCK_KINDS:
            problems.append(f"{where}: unknown kind {b['kind']} (one of {', '.join(BLOCK_KINDS)})")
        if not isinstance(b["paths"], list) or not b["paths"]:
            problems.append(f"{where}: 'paths' is a non-empty list")
        else:
            for p in b["paths"]:
                _check_path(p, where, problems)
        for p in b.get("docs") or []:
            _check_path(p, f"{where} docs", problems)
        if not isinstance(b["key"], list) or not b["key"]:
            problems.append(f"{where}: 'key' is a non-empty list")
        else:
            for j, k in enumerate(b["key"]):
                if _check_keys(k, KEY_KEYS, f"{where} key[{j}]", problems):
                    _check_path(k["path"], f"{where} key {k['name']}", problems)
        for num in ("w", "row"):
            if num in b:
                try:
                    float(b[num])
                except ValueError:
                    problems.append(f"{where}: '{num}' is a number")
    for i, e in enumerate(data["edges"]):
        where = f"edge {e.get('from')} -> {e.get('to')}"
        if not _check_keys(e, EDGE_KEYS, where, problems):
            continue
        for end in ("from", "to"):
            if e[end] not in block_ids:
                problems.append(f"{where}: unknown block {e[end]}")
        if e["from"] == e["to"]:
            problems.append(f"{where}: an edge to itself")
        if e["kind"] not in EDGE_KINDS:
            problems.append(f"{where}: unknown kind {e['kind']} (one of {', '.join(EDGE_KINDS)})")
        if str(e.get("hide", "false")).lower() not in ("true", "false"):
            problems.append(f"{where}: 'hide' is true or false")
    seen = set()
    for e in data["edges"]:
        key = (e.get("from"), e.get("to"), e.get("kind"))
        if key in seen:
            problems.append(f"edge {key[0]} -> {key[1]}: two edges of kind {key[2]} (merge them)")
        seen.add(key)
    for i, f in enumerate(data.get("flows") or []):
        if _check_keys(f, FLOW_KEYS, f"flows[{i}]", problems):
            for j, s in enumerate(f["steps"]):
                if _check_keys(s, STEP_KEYS, f"flow {f['id']} step {j + 1}", problems):
                    if s["block"] not in block_ids:
                        problems.append(f"flow {f['id']} step {j + 1}: unknown block {s['block']}")
                    _check_path(s["file"], f"flow {f['id']} step {j + 1}", problems)
    for i, t in enumerate(data.get("tasks") or []):
        if _check_keys(t, TASK_KEYS, f"tasks[{i}]", problems):
            for side in ("qt", "upstream"):
                if not isinstance(t[side], list):
                    problems.append(f"task {t['task']!r}: '{side}' is a list")
                    continue
                for p in t[side]:
                    _check_path(p, f"task {t['task']!r}", problems)
    for b in data["blocks"]:
        if isinstance(b, dict) and b.get("id") in block_ids and not any(
                e.get("from") == b["id"] or e.get("to") == b["id"] for e in data["edges"]):
            problems.append(f"block {b['id']}: no edge")
    return problems


def cmake_links():
    """{target: set of targets it links} from target_link_libraries in the fork's CMake files."""
    links = {}
    files = sorted((ROOT / "qt" / "cmake").glob("*.cmake")) + [ROOT / "qt" / "CMakeLists.txt"]
    for f in files:
        text = "\n".join(line.split("#", 1)[0] for line in f.read_text(encoding="utf-8").splitlines())
        for m in re.finditer(r"target_link_libraries\(\s*([\w.:-]+)([^)]*)\)", text):
            deps = [t for t in m.group(2).split() if t not in ("PUBLIC", "PRIVATE", "INTERFACE")]
            links.setdefault(m.group(1), set()).update(deps)
    return links


def check_cmake(model):
    problems = []
    links = cmake_links()
    targets = {}
    for b in model.blocks:
        if b.get("target"):
            targets.setdefault(b["target"], []).append(b["id"])
    for e in model.edges:
        if e["kind"] != "links":
            continue
        src, dst = model.block_by_id[e["from"]], model.block_by_id[e["to"]]
        if not src.get("target"):
            problems.append(f"edge {e['from']} -> {e['to']}: a 'links' edge from a block without a target")
            continue
        if not dst.get("target"):
            continue  # vendored code without a target of its own here
        if dst["target"] not in links.get(src["target"], set()):
            problems.append(f"edge {e['from']} -> {e['to']}: CMake has no target_link_libraries({src['target']} … "
                            f"{dst['target']})")
    for src_t, deps in sorted(links.items()):
        if src_t not in targets:
            continue
        for dst_t in sorted(deps):
            if dst_t not in targets or dst_t == src_t:
                continue
            if not any(e["from"] in targets[src_t] and e["to"] in targets[dst_t] for e in model.edges):
                problems.append(f"CMake: {src_t} links {dst_t}, but no edge goes from a block of {src_t} "
                                f"({', '.join(targets[src_t])}) to one of {dst_t} ({', '.join(targets[dst_t])})")
    return problems


# ---------------------------------------------------------------------------------------------------------------------
# Layout

W = 1200
PAD = 16
BRACKET_W = 30
DATA_W = 190
COL_GAP = 24
MAIN_X = PAD + BRACKET_W
MAIN_W = W - MAIN_X - COL_GAP - DATA_W - PAD
TITLE_H = 70
BAND_LABEL_H = 22
BAND_PAD = 10
BLOCK_H = 58
ROW_GAP = 12
BLOCK_GAP = 12
BAND_GAP = 16

FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, 'SF Mono', Menlo, Consolas, 'Liberation Mono', monospace"

BLOCK_STYLE = {  # fill, stroke, text accent
    "qml": ("#fdf0f7", "#bf3989", "#99286e"),
    "app": ("#e6efff", "#2f5fc4", "#1f4596"),
    "qt": ("#eef5ff", "#0969da", "#0550ae"),
    "compat": ("#f4efff", "#8250df", "#6639ba"),
    "upstream": ("#fff3e3", "#bc4c00", "#953800"),
    "unbuilt": ("#f6f8fa", "#8c959f", "#57606a"),
    "data": ("#eaf7ed", "#1a7f37", "#116329"),
    "tests": ("#f6f8fa", "#6e7781", "#424a53"),
    "tools": ("#f6f8fa", "#6e7781", "#424a53"),
    "platform": ("#f6f8fa", "#6e7781", "#424a53"),
    "ci": ("#f6f8fa", "#6e7781", "#424a53"),
}
BAND_FILL = {"ui": "#fdf7fb", "app": "#f5f8ff", "shell": "#f5f8ff", "view": "#f5f9ff", "document": "#f5f9ff",
             "compat": "#f9f6ff", "core": "#fffaf2", "around": "#f8f9fa", "data": "#f3faf4"}
BAND_TEXT = {"ui": "#99286e", "app": "#1f4596", "shell": "#1f4596", "view": "#0550ae", "document": "#0550ae",
             "compat": "#6639ba", "core": "#953800", "around": "#424a53", "data": "#116329"}
EDGE_STYLE = {  # color, dash
    "links": ("#57606a", ""),
    "calls": ("#0969da", ""),
    "shadows": ("#8250df", "6 3"),
    "seam": ("#d1570b", "2 3"),
    "data": ("#1a7f37", "8 3 2 3"),
    "qml": ("#bf3989", ""),
}


def text_width(s, size, bold=False):
    w = 0.0
    for c in s:
        if c in "il.,:;'|!()[]`ijtfI/ ":
            w += 0.30
        elif c in "mwMW@":
            w += 0.86
        elif c.isupper() or c in "&%#":
            w += 0.66
        else:
            w += 0.54
    return w * size * (1.07 if bold else 1.0)


def fit(s, max_w, size, bold=False, mono=False):
    width = (lambda t: len(t) * 0.61 * size) if mono else (lambda t: text_width(t, size, bold))
    if width(s) <= max_w:
        return s
    while s and width(s + "…") > max_w:
        s = s[:-1]
    return s.rstrip(" ,") + "…"


def layout(model):
    """Positions: {block id: (x, y, w, h)}, the bands [(layer, x, y, w, h)], and the total height."""
    pos, bands = {}, []
    y = TITLE_H
    main_layers = [l for l in model.layers if not l.get("side")]
    for layer in main_layers:
        blocks = [b for b in model.blocks if b["layer"] == layer["id"]]
        rows = sorted({b["row"] for b in blocks})
        band_y = y
        y += BAND_LABEL_H
        for r in rows:
            row = [b for b in blocks if b["row"] == r]
            avail = MAIN_W - 2 * BAND_PAD - BLOCK_GAP * (len(row) - 1)
            total = sum(b["w"] for b in row)
            x = MAIN_X + BAND_PAD
            for b in row:
                bw = avail * b["w"] / total
                pos[b["id"]] = (round(x, 1), y, round(bw, 1), BLOCK_H)
                x += bw + BLOCK_GAP
            y += BLOCK_H + ROW_GAP
        y += BAND_PAD - ROW_GAP
        bands.append((layer, MAIN_X, band_y, MAIN_W, y - band_y))
        y += BAND_GAP
    height = y - BAND_GAP
    band_of = {b[0]["id"]: b for b in bands}
    for layer in model.layers:
        if not layer.get("side"):
            continue
        # beside the bands of the layers whose blocks use it; each block level with its users where it can be
        into = [e for e in model.edges if model.block_by_id[e["to"]]["layer"] == layer["id"]]
        users = {model.block_by_id[e["from"]]["layer"] for e in into}
        spans = [band_of[u] for u in users if u in band_of] or bands
        top = min(s[2] for s in spans)
        bottom = max(s[2] + s[4] for s in spans)
        x = MAIN_X + MAIN_W + COL_GAP
        bands.append((layer, x, top, DATA_W, bottom - top))
        blocks = [b for b in model.blocks if b["layer"] == layer["id"]]
        lo, hi = top + BAND_LABEL_H, bottom - BAND_PAD - BLOCK_H
        wanted = []
        for i, b in enumerate(blocks):
            src = [pos[e["from"]] for e in into if e["to"] == b["id"] and e["from"] in pos]
            visible = [pos[e["from"]] for e in into if e["to"] == b["id"] and e["from"] in pos and not e["hide"]]
            src = visible or src
            y_want = sum(p[1] for p in src) / len(src) if src else lo
            wanted.append((y_want, i, b))
        wanted.sort(key=lambda t: (t[0], t[1]))
        ys, gap = [], BLOCK_H + 10
        for y_want, _, _ in wanted:
            ys.append(max(lo, y_want, ys[-1] + gap if ys else lo))
        for k in range(len(ys) - 1, -1, -1):  # pushed past the bottom: back up
            ys[k] = min(ys[k], hi - (len(ys) - 1 - k) * gap)
        for (_, _, b), y_b in zip(wanted, ys):
            pos[b["id"]] = (x + BAND_PAD, round(y_b, 1), DATA_W - 2 * BAND_PAD, BLOCK_H)
    return pos, bands, height


def route(model, pos, edges):
    """The path of each edge: it leaves and enters a block on the side facing the other one (below: bottom to top;
    in the same row: side to side when adjacent, else an arc below the row; to the side column: right to left), at
    a port of its own along that side, sorted by where the other end is so that edges do not cross there."""
    ends = []  # (edge index, which end, block id, side, sort key)
    sides = {}
    for i, e in enumerate(edges):
        a, b = pos[e["from"]], pos[e["to"]]
        a_side_col = model.layer_by_id[model.block_by_id[e["from"]]["layer"]].get("side")
        b_side_col = model.layer_by_id[model.block_by_id[e["to"]]["layer"]].get("side")
        ax, ay, aw, ah = a
        bx, by, bw, bh = b
        if b_side_col and not a_side_col:
            sa, sb = "right", "left"
        elif a_side_col and not b_side_col:
            sa, sb = "left", "right"
        elif abs(ay - by) < 1:
            adjacent = not any(abs(p[1] - ay) < 1 and min(ax, bx) < p[0] < max(ax, bx)
                               for bid, p in pos.items() if bid not in (e["from"], e["to"]))
            if adjacent:
                sa, sb = ("right", "left") if bx > ax else ("left", "right")
            else:
                sa, sb = "bottom", "bottom"
        elif by > ay:
            sa, sb = "bottom", "top"
        else:
            sa, sb = "top", "bottom"
        sides[i] = (sa, sb)
        ends.append((i, 0, e["from"], sa, (bx + bw / 2, by + bh / 2)))
        ends.append((i, 1, e["to"], sb, (ax + aw / 2, ay + ah / 2)))
    groups = {}
    for end in ends:
        groups.setdefault((end[2], end[3]), []).append(end)
    points = {}
    for (bid, side), group in groups.items():
        x, y, w, h = pos[bid]
        horizontal = side in ("top", "bottom")
        group.sort(key=lambda t: (t[4][0], t[4][1]) if horizontal else (t[4][1], t[4][0]))
        n = len(group)
        for k, (i, which, _, _, _) in enumerate(group):
            frac = 0.5 if n == 1 else 0.15 + 0.7 * k / (n - 1)
            if side == "top":
                pt = (x + w * frac, y)
            elif side == "bottom":
                pt = (x + w * frac, y + h)
            elif side == "left":
                pt = (x, y + h * frac)
            else:
                pt = (x + w, y + h * frac)
            points[(i, which)] = (round(pt[0], 1), round(pt[1], 1))
    paths = []
    for i, e in enumerate(edges):
        (sx, sy), (tx, ty) = points[(i, 0)], points[(i, 1)]
        sa, sb = sides[i]
        dist = ((tx - sx) ** 2 + (ty - sy) ** 2) ** 0.5
        d = max(18.0, min(90.0, dist * 0.42))
        if sa == sb:
            d = 26.0
        c1 = _offset(sx, sy, sa, d)
        c2 = _offset(tx, ty, sb, d)
        paths.append(f"M{sx:g},{sy:g} C{c1[0]:g},{c1[1]:g} {c2[0]:g},{c2[1]:g} {tx:g},{ty:g}")
    return paths


def _offset(x, y, side, d):
    return {"top": (x, round(y - d, 1)), "bottom": (x, round(y + d, 1)),
            "left": (round(x - d, 1), y), "right": (round(x + d, 1), y)}[side]


# ---------------------------------------------------------------------------------------------------------------------
# SVG

def esc(s):
    return html.escape(str(s), quote=True)


def plain(s):
    """Text without Markdown code ticks (for the SVG and titles)."""
    return str(s).replace("`", "")


def render_svg(model, interactive=False):
    pos, bands, height = layout(model)
    legend_y = height + 22
    total_h = legend_y + 64
    edges = model.edges if interactive else [e for e in model.edges if not e["hide"]]
    paths = route(model, pos, edges)
    out = []
    a = out.append
    a(f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
      f'viewBox="0 0 {W} {total_h:g}" width="{W}" height="{total_h:g}" font-family="{esc(FONT)}" '
      f'role="img" aria-labelledby="arch-title arch-desc"' + (' id="arch"' if interactive else '') + '>')
    a('<title id="arch-title">xournal-qt architecture</title>')
    a('<desc id="arch-desc">The layers of xournal-qt, from the QML UI down to the Xournal++ core it builds on, '
      'and the edges between their blocks. Generated from qt/docs/architecture/architecture.yaml.</desc>')
    a("<defs>")
    for kind, (color, _) in EDGE_STYLE.items():
        a(f'<marker id="arr-{kind}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
          f'orient="auto-start-reverse"><path d="M0,1 L10,5 L0,9 z" fill="{color}"/></marker>')
    a('<pattern id="hatch" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
      '<rect width="8" height="8" fill="#f6f8fa"/><line x1="0" y1="0" x2="0" y2="8" stroke="#e1e4e8" '
      'stroke-width="3"/></pattern>')
    a("</defs>")
    a(f'<rect x="0" y="0" width="{W}" height="{total_h:g}" fill="#ffffff"/>')
    # title
    a(f'<text x="{PAD}" y="30" font-size="21" font-weight="700" fill="#1f2328">xournal-qt: architecture</text>')
    sub = ("Click a block for its responsibility, key files and edges. Dependencies point down."
           if interactive else
           "Dependencies point down. Opened on its own, each block links to its folder on GitHub. "
           "Generated from architecture.yaml.")
    a(f'<text x="{PAD}" y="52" font-size="13" fill="#57606a">{esc(sub)}</text>')
    # brackets: which part of the tree
    _brackets(a, model, bands)
    # bands (their labels go over the edges)
    labels = []
    for layer, x, y, w, h in bands:
        fill = BAND_FILL.get(layer["id"], "#f6f8fa")
        color = BAND_TEXT.get(layer["id"], "#424a53")
        a(f'<g class="band" data-layer="{esc(layer["id"])}"><title>{esc(plain(layer["about"]))}</title>'
          f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="10" fill="{fill}" stroke="#d0d7de" '
          f'stroke-width="0.8"/></g>')
        labels.append(f'<text x="{x + BAND_PAD:g}" y="{y + 15:g}" font-size="11.5" font-weight="700" '
                      f'fill="{color}" stroke="{fill}" stroke-width="4" stroke-linejoin="round" '
                      f'paint-order="stroke" letter-spacing="0.3">{esc(layer["name"].upper())}</text>')
    # edges (under the blocks, so a block's text stays readable)
    a('<g class="edges" fill="none">')
    for i, (e, d) in enumerate(zip(edges, paths)):
        color, dash = EDGE_STYLE[e["kind"]]
        cls = f'edge k-{e["kind"]}' + (" minor" if e["hide"] else "")
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        title = f'{model.block_by_id[e["from"]]["name"]} → {model.block_by_id[e["to"]]["name"]} ({e["kind"]}): ' \
                f'{plain(e["what"])}'
        extra = (f' data-from="{esc(e["from"])}" data-to="{esc(e["to"])}" data-kind="{e["kind"]}" data-i="{i}"'
                 if interactive else "")
        a(f'<path class="{cls}" d="{d}" stroke="{color}" stroke-width="1.5"{dash_attr} '
          f'marker-end="url(#arr-{e["kind"]})"{extra}><title>{esc(title)}</title></path>')
    a("</g>")
    a('<g class="band-labels">' + "".join(labels) + "</g>")
    # blocks
    a('<g class="blocks">')
    for b in model.blocks:
        x, y, w, h = pos[b["id"]]
        fill, stroke, accent = BLOCK_STYLE[b["kind"]]
        unbuilt = b["kind"] == "unbuilt"
        rect_fill = "url(#hatch)" if unbuilt else fill
        dash = ' stroke-dasharray="5 3"' if unbuilt else ""
        name = fit(b["name"], w - 16, 13.5, bold=True)
        label = fit(plain(b["label"]), w - 16, 11)
        third = b.get("target") or b["paths"][0]
        third = fit(third, w - 16, 10.5, mono=True)
        title = f'{b["name"]}: {plain(b["responsibility"])}'
        inner = (f'<title>{esc(title)}</title>'
                 f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="7" fill="{rect_fill}" '
                 f'stroke="{stroke}" stroke-width="1.3"{dash}/>'
                 f'<text x="{x + 8:g}" y="{y + 20:g}" font-size="13.5" font-weight="700" '
                 f'fill="{"#57606a" if unbuilt else "#1f2328"}">{esc(name)}</text>'
                 f'<text x="{x + 8:g}" y="{y + 36:g}" font-size="11" fill="#424a53">{esc(label)}</text>'
                 f'<text x="{x + 8:g}" y="{y + 50:g}" font-size="10.5" font-family="{esc(MONO)}" '
                 f'fill="{accent}">{esc(third)}</text>')
        if interactive:
            a(f'<g class="block" data-id="{esc(b["id"])}" tabindex="0" role="button" '
              f'aria-label="{esc(b["name"])}">{inner}</g>')
        else:
            url = esc(model.github_url(b["paths"][0]))
            a(f'<a href="{url}" xlink:href="{url}" target="_top"><g class="block">{inner}</g></a>')
    a("</g>")
    _legend(a, legend_y)
    a("</svg>")
    return "\n".join(out) + "\n"


def _brackets(a, model, bands):
    by_id = {b[0]["id"]: b for b in bands}
    groups = [
        (["ui", "app", "shell", "view", "document"], "xournal-qt · qt/", "#0550ae"),
        (["compat"], "qt/compat", "#6639ba"),
        (["core"], "Xournal++ · src/", "#953800"),
    ]
    for ids, text, color in groups:
        spans = [by_id[i] for i in ids if i in by_id]
        if not spans:
            continue
        top = min(s[2] for s in spans) + 4
        bottom = max(s[2] + s[4] for s in spans) - 4
        x = PAD + 10
        a(f'<path d="M{x + 8},{top:g} H{x} V{bottom:g} H{x + 8}" fill="none" stroke="{color}" stroke-width="1.6"/>')
        cy = (top + bottom) / 2
        a(f'<text x="{x - 4}" y="{cy:g}" font-size="11" font-weight="700" fill="{color}" text-anchor="middle" '
          f'transform="rotate(-90 {x - 4} {cy:g})" dy="-2">{esc(text)}</text>')


def _legend(a, y):
    a(f'<g class="legend" font-size="11.5" fill="#424a53">')
    x = PAD
    a(f'<text x="{x}" y="{y:g}" font-weight="700" fill="#1f2328">Edges</text>')
    x += 50
    for kind, (color, dash) in EDGE_STYLE.items():
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        a(f'<path d="M{x},{y - 4:g} H{x + 30}" stroke="{color}" stroke-width="1.8"{dash_attr} '
          f'marker-end="url(#arr-{kind})"/>')
        a(f'<text x="{x + 38}" y="{y:g}">{esc(kind)}</text>')
        x += 46 + text_width(kind, 11.5) + 18
    y2 = y + 26
    x = PAD
    a(f'<text x="{x}" y="{y2:g}" font-weight="700" fill="#1f2328">Blocks</text>')
    x += 50
    shown = [("qml", "QML UI"), ("app", "app C++"), ("qt", "xournal-qt module"), ("compat", "qt/compat"),
             ("upstream", "Xournal++ core"), ("unbuilt", "Xournal++, not built"), ("data", "shared data"),
             ("tools", "tools, tests, platforms, CI")]
    for kind, text in shown:
        fill, stroke, _ = BLOCK_STYLE[kind]
        f = "url(#hatch)" if kind == "unbuilt" else fill
        dash = ' stroke-dasharray="4 2"' if kind == "unbuilt" else ""
        a(f'<rect x="{x}" y="{y2 - 11:g}" width="22" height="14" rx="3" fill="{f}" stroke="{stroke}"{dash}/>')
        a(f'<text x="{x + 28}" y="{y2:g}">{esc(text)}</text>')
        x += 28 + text_width(text, 11.5) + 18
    a("</g>")


# ---------------------------------------------------------------------------------------------------------------------
# README.md

class Headings:
    """GitHub's anchors for the headings of one Markdown file (the rules of qt/scripts/check-doc-links.py)."""

    def __init__(self):
        self.seen = {}

    def add(self, text):
        slug = re.sub(r"<[^>]+>", "", text)
        slug = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", slug).strip().lower()
        slug = re.sub(r"[^\w\- ]", "", slug).replace(" ", "-")
        n = self.seen.get(slug, 0)
        self.seen[slug] = n + 1
        return slug if n == 0 else f"{slug}-{n}"


def md(s, table=False):
    """YAML text as Markdown: code spans kept, characters Markdown or HTML would read escaped outside them."""
    parts = re.split(r"(`[^`]*`)", str(s))
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append(p.replace("|", "\\|") if table else p)
            continue
        p = p.replace("\\", "\\\\")
        for c in "*_~[]":
            p = p.replace(c, "\\" + c)
        p = p.replace("<", "&lt;").replace(">", "&gt;")
        if table:
            p = p.replace("|", "\\|")
        out.append(p)
    return "".join(out).replace("\n", "\n\n")


def rel(path):
    """A relative link from qt/docs/architecture/README.md to a repository path (works on any branch)."""
    link = os.path.relpath(ROOT / path, ARCH_DIR).replace(os.sep, "/")
    return link + "/" if path.endswith("/") else link


def path_link(path):
    return f"[`{path}`]({rel(path)})"


def render_readme(model):
    h = Headings()
    anchors = {}
    lines = []
    a = lines.append

    def heading(level, text):
        slug = h.add(text)
        a(f'{"#" * level} {text}')
        a("")
        return slug

    heading(1, "Architecture")
    a(f"<!-- Generated by {GENERATOR} from architecture.yaml. Edit the YAML, then run the script. -->")
    a("")
    a(md(model.data["intro"]))
    a("")
    a("![The layers of xournal-qt and the Xournal++ core, and the edges between their blocks](architecture.svg)")
    a("")
    a(f"**The interactive page**: [{model.pages}]({model.pages}) (click a block for its files and edges; filter the "
      f"edges by kind), published from [`site/index.html`](site/index.html) by `.github/workflows/xqt-pages.yml`; "
      f"it works offline from a checkout too. On GitHub the diagram above is a picture: the links are in the tables "
      f"below. Opened on its own ([architecture.svg](architecture.svg)), its blocks link to their folders.")
    a("")
    a("Edges, by kind:")
    a("")
    a("| Kind | Means |")
    a("| --- | --- |")
    for kind, text in EDGE_KINDS.items():
        a(f"| `{kind}` | {md(text, table=True)} |")
    a("")
    a("Sub-pages: [image caches](image-caches.md) (the pictures the app draws ahead and keeps, their owners and "
      "limits). Related: [ADR 0002](../decisions/0002-upstream-seams.md) (every seam in upstream files, the ports), "
      "[ADR 0003](../decisions/0003-app-services.md) (what the windows share), "
      "[AGENTS.md](../../../AGENTS.md) (\"What the moving parts assume\": threads, page revisions, memory owners).")
    a("")

    # pre-compute the anchors of the blocks: they are referenced before their headings
    pre = Headings()
    pre.add("Architecture")
    pre.add("The layers")
    for layer in model.layers:
        pre.add(layer["name"])
        for b in [b for b in model.blocks if b["layer"] == layer["id"]]:
            anchors[b["id"]] = pre.add(b["name"])

    def blink(bid):
        return f'[{model.block_by_id[bid]["name"]}](#{anchors[bid]})'

    heading(2, "The layers")
    a("From the top of the diagram down; the shared data last. Each block: what it does, where it is, its key "
      "classes, what it depends on and what uses it.")
    a("")
    for layer in model.layers:
        heading(3, layer["name"])
        a(md(layer["about"]))
        a("")
        blocks = [b for b in model.blocks if b["layer"] == layer["id"]]
        a("| Block | Responsibility | Target |")
        a("| --- | --- | --- |")
        for b in blocks:
            target = f'`{b["target"]}`' if b.get("target") else ""
            a(f'| {blink(b["id"])} | {md(_first_sentence(b["responsibility"]), table=True)} | {target} |')
        a("")
        for b in blocks:
            slug = heading(4, b["name"])
            assert slug == anchors[b["id"]], f"anchor {slug} != {anchors[b['id']]}"
            a(md(b["responsibility"]))
            a("")
            where = ", ".join(path_link(p) for p in b["paths"])
            facts = [f"**Where**: {where}"]
            if b.get("target"):
                facts.append(f'**Target**: `{b["target"]}`')
            facts.append(f'**Kind**: {BLOCK_KINDS[b["kind"]]}')
            a(". ".join(facts) + ".")
            if b["docs"]:
                a("")
                a("**Docs**: " + ", ".join(f"[{_doc_title(p)}]({rel(p)})" for p in b["docs"]) + ".")
            a("")
            a("| Key class or file | What |")
            a("| --- | --- |")
            for k in b["key"]:
                a(f'| [`{k["name"]}`]({rel(k["path"])}) | {md(k["what"], table=True)} |')
            a("")
            outs, ins = model.out_edges(b["id"]), model.in_edges(b["id"])
            if outs:
                a("Depends on:")
                a("")
                for e in outs:
                    a(f'- {blink(e["to"])} (`{e["kind"]}`): {md(e["what"])}')
                a("")
            if ins:
                a("Used by:")
                a("")
                for e in ins:
                    a(f'- {blink(e["from"])} (`{e["kind"]}`): {md(e["what"])}')
                a("")

    core_ids = {b["id"] for b in model.blocks if b["layer"] == "core"}
    compat_ids = {b["id"] for b in model.blocks if b["layer"] == "compat"}
    data_ids = {b["id"] for b in model.blocks if b["kind"] == "data"}
    frontend = [b for b in model.blocks if b["layer"] not in ("core", "compat") and b["kind"] != "data"]

    heading(2, "How the Qt frontend uses the Xournal++ core")
    a(md(model.data["core_usage"]))
    a("")
    heading(3, "What of src/ the Qt build compiles")
    a(f"The list is {path_link('qt/cmake/XojSources.cmake')}, built by "
      f"{path_link('qt/cmake/XojCore.cmake')} with `XOJ_NO_GTK=1` and `qt/compat` before "
      "upstream's include paths. What is not built: [ADR 0002, \"Not compiled in the Qt build\"]"
      "(../decisions/0002-upstream-seams.md#not-compiled-in-the-qt-build).")
    a("")
    a("| Upstream part | Folder | Built into |")
    a("| --- | --- | --- |")
    for b in model.blocks:
        if b["id"] in core_ids:
            built = f'`{b["target"]}`' if b.get("target") else "**not built**"
            a(f'| {blink(b["id"])} | {", ".join(path_link(p) for p in b["paths"])} | {built} |')
    a("")
    heading(3, "Direct calls")
    a("Upstream classes the fork's modules use as they are (`calls`), and the CMake links that carry them "
      "(`links`).")
    a("")
    _edge_table(a, model, blink, [e for e in model.edges if e["kind"] in ("calls", "links")
                                  and e["to"] in core_ids and e["from"] not in core_ids])
    heading(3, "Through qt/compat")
    a("Upstream code calls the shadow headers; fork classes implement them; the shadows stand in for the GTK "
      "classes that are not built. The full list: [ADR 0002, section 1]"
      "(../decisions/0002-upstream-seams.md#1-compatibility-headers-no-upstream-edit).")
    a("")
    _edge_table(a, model, blink, [e for e in model.edges if e["kind"] == "shadows"])
    heading(3, "Seams in upstream files")
    a("Hooks a frontend may set and small edits, each marked `xournal-qt:` in the upstream file and listed in "
      "[ADR 0002](../decisions/0002-upstream-seams.md) ([guarded seams]"
      "(../decisions/0002-upstream-seams.md#2-guarded-seams-in-upstream-files-ifdef-xoj_no_gtk-tagged-xournal-qt), "
      "[small upstreamable refactorings]"
      "(../decisions/0002-upstream-seams.md#3-small-upstreamable-refactorings-valid-for-both-builds)). "
      "Without the fork's hooks set, upstream behaves as before.")
    a("")
    _edge_table(a, model, blink, [e for e in model.edges if e["kind"] == "seam"])
    heading(3, "Ported, not reused")
    a("Where upstream's class is GTK-bound through and through, the fork ported it and records the origin in a "
      "comment: `PageRaster` and `RenderService` (from `RenderJob`, the scheduler), `CanvasPage` (from "
      "`XojPageView`), `CanvasInput` (from the input handlers), `CanvasView`, `DocumentLayout`, `ViewController` "
      "(from `XournalView`, `Layout`, `ZoomControl`), `DocumentSession` (from parts of `Control` and the jobs), "
      "`TextEditor`, `SessionRecovery`, `Thumbnails`. The table, to re-check after upstream merges: "
      "[ADR 0002, \"Ported, not reused\"]"
      "(../decisions/0002-upstream-seams.md#ported-not-reused-the-upstream-original-is-gtk-bound).")
    a("")
    heading(3, "Shared data")
    a("What both programs read and write, and who does it here. xournal-qt keeps its configuration, caches and "
      "autosaves in folders of its own (`~/.config/xournal-qt`, `~/.cache/xournal-qt`), so it never touches "
      "Xournal++'s.")
    a("")
    a("| Data | What | Read and written by |")
    a("| --- | --- | --- |")
    for b in model.blocks:
        if b["id"] in data_ids:
            users = ", ".join(sorted({blink(e["from"]) for e in model.in_edges(b["id"])}))
            a(f'| {blink(b["id"])} | {md(b["responsibility"], table=True)} | {users} |')
    a("")
    heading(3, "Which module uses which upstream part")
    a("Every edge from a frontend block into the core or `qt/compat`, by block.")
    a("")
    a("| xournal-qt block | Upstream part (kind) |")
    a("| --- | --- |")
    for b in frontend:
        es = [e for e in model.out_edges(b["id"]) if e["to"] in core_ids | compat_ids]
        if es:
            a(f'| {blink(b["id"])} | ' + "; ".join(f'{blink(e["to"])} (`{e["kind"]}`)' for e in es) + " |")
    a("")

    heading(2, "Paths through the code")
    a("How a document becomes a tab, how a pen stroke travels, how a page gets on the screen.")
    a("")
    for f in model.flows:
        heading(3, f["name"])
        for n, s in enumerate(f["steps"], 1):
            a(f'{n}. {blink(s["block"])}, [`{Path(s["file"]).name}`]({rel(s["file"])}): {md(s["text"])}')
        a("")

    heading(2, "Where to change what")
    a("The files to start from on each side. A change to an upstream file is a seam: tiny, marked `xournal-qt:`, "
      "listed in [ADR 0002](../decisions/0002-upstream-seams.md).")
    a("")
    a("| Task | xournal-qt (`qt/`) | Xournal++ (`src/`) |")
    a("| --- | --- | --- |")
    for t in model.tasks:
        qt = "<br>".join(path_link(p) for p in t["qt"]) or "–"
        up = "<br>".join(path_link(p) for p in t["upstream"]) or "– (none)"
        task = md(t["task"], table=True)
        if t.get("note"):
            task += f'<br>*{md(t["note"], table=True)}*'
        a(f"| {task} | {qt} | {up} |")
    a("")

    heading(2, "Keeping this page up to date")
    a(f"This page, [architecture.svg](architecture.svg) and [site/index.html](site/index.html) are generated from "
      f"[architecture.yaml](architecture.yaml) by [`{GENERATOR}`](../../scripts/architecture/generate.py) "
      f"(Python 3, standard library only). A change that adds, moves or removes a module, a target or a key class "
      f"updates the YAML and runs the script in the same commit:")
    a("")
    a("```sh")
    a(f"python3 {GENERATOR}           # writes README.md, architecture.svg, site/index.html")
    a(f"python3 {GENERATOR} --check   # what CI runs")
    a("```")
    a("")
    a("The check (CI: the job \"Doc links\" of `xqt-build.yml`) fails when the generated files are out of date, a "
      "path in the YAML does not exist, or the CMake links between the targets it names and its `links` edges "
      "disagree.")
    a("")
    text = "\n".join(lines)
    return re.sub(r"\n{3,}", "\n\n", text).rstrip("\n") + "\n"


def _first_sentence(s):
    m = re.match(r"^(.+?\.)\s", s)
    return m.group(1) if m else s


def _doc_title(path):
    p = Path(path)
    if p.name == "README.md":
        return f"{p.parent.name}/README.md"
    return p.name


def _edge_table(a, model, blink, edges):
    a("| From | To | Kind | What |")
    a("| --- | --- | --- | --- |")
    for e in edges:
        a(f'| {blink(e["from"])} | {blink(e["to"])} | `{e["kind"]}` | {md(e["what"], table=True)} |')
    a("")


# ---------------------------------------------------------------------------------------------------------------------
# site/index.html

def render_html(model):
    svg = render_svg(model, interactive=True)
    data = {
        "github": model.github, "branch": model.branch,
        "intro": model.data["intro"],
        "layers": [{"id": l["id"], "name": l["name"], "about": l["about"]} for l in model.layers],
        "blocks": [{
            "id": b["id"], "name": b["name"], "layer": b["layer"], "kind": b["kind"],
            "kindName": BLOCK_KINDS[b["kind"]], "target": b.get("target") or "",
            "responsibility": b["responsibility"],
            "paths": [{"path": p, "url": model.github_url(p)} for p in b["paths"]],
            "key": [{"name": k["name"], "what": k["what"], "path": k["path"], "url": model.github_url(k["path"])}
                    for k in b["key"]],
            "docs": [{"path": p, "url": model.github_url(p)} for p in b["docs"]],
        } for b in model.blocks],
        "edges": [{"from": e["from"], "to": e["to"], "kind": e["kind"], "what": e["what"], "minor": e["hide"]}
                  for e in model.edges],
        "edgeKinds": [{"id": k, "about": v, "color": EDGE_STYLE[k][0]} for k, v in EDGE_KINDS.items()],
        "flows": [{"id": f["id"], "name": f["name"],
                   "steps": [{"block": s["block"], "text": s["text"], "file": s["file"],
                              "url": model.github_url(s["file"])} for s in f["steps"]]} for f in model.flows],
        "readme": f'{model.github}/blob/{model.branch}/qt/docs/architecture/README.md',
        "yaml": f'{model.github}/blob/{model.branch}/qt/docs/architecture/architecture.yaml',
    }
    payload = json.dumps(data, ensure_ascii=False, indent=None, separators=(",", ":"), sort_keys=False)
    payload = payload.replace("</", "<\\/")
    return HTML_TEMPLATE.replace("@@SVG@@", svg.rstrip("\n")).replace("@@DATA@@", payload)


HTML_TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>xournal-qt architecture</title>
<meta name="description" content="The architecture of xournal-qt, a Qt frontend on the Xournal++ core: layers, blocks, and how they connect.">
<!-- Generated by qt/scripts/architecture/generate.py from qt/docs/architecture/architecture.yaml. Do not edit. -->
<style>
:root {
  --bg: #f6f8fa; --panel: #ffffff; --text: #1f2328; --muted: #57606a; --line: #d0d7de; --link: #0969da;
  --chip: #eaeef2; --hl: #fff8c5;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #0d1117; --panel: #161b22; --text: #e6edf3; --muted: #8d96a0; --line: #30363d; --link: #4493f8;
    --chip: #21262d; --hl: #3b2e00;
  }
}
:root[data-theme="dark"] {
  --bg: #0d1117; --panel: #161b22; --text: #e6edf3; --muted: #8d96a0; --line: #30363d; --link: #4493f8;
  --chip: #21262d; --hl: #3b2e00;
}
* { box-sizing: border-box; }
html, body { margin: 0; }
body { background: var(--bg); color: var(--text); font: 14px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI",
  "Noto Sans", Helvetica, Arial, sans-serif; }
a { color: var(--link); text-decoration: none; }
a:hover { text-decoration: underline; }
code { font: 12.5px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; background: var(--chip);
  padding: 1px 4px; border-radius: 4px; }
header { display: flex; flex-wrap: wrap; gap: 8px 18px; align-items: center; padding: 10px 16px;
  border-bottom: 1px solid var(--line); background: var(--panel); }
header h1 { font-size: 17px; margin: 0; }
header .links { color: var(--muted); font-size: 13px; }
.filters { display: flex; flex-wrap: wrap; gap: 6px 12px; align-items: center; font-size: 13px; }
.filters label { display: inline-flex; align-items: center; gap: 5px; cursor: pointer; white-space: nowrap; }
.swatch { display: inline-block; width: 18px; height: 3px; border-radius: 2px; }
main { display: grid; grid-template-columns: minmax(0, 1fr) 380px; gap: 0; min-height: calc(100vh - 54px); }
#diagram { overflow: auto; padding: 12px 16px; }
#diagram svg { width: 100%; height: auto; max-width: 1400px; display: block; margin: 0 auto;
  border-radius: 10px; box-shadow: 0 0 0 1px var(--line); }
aside { border-left: 1px solid var(--line); background: var(--panel); padding: 16px 18px; overflow: auto;
  max-height: calc(100vh - 54px); position: sticky; top: 0; }
aside h2 { font-size: 18px; margin: 4px 0 2px; }
aside h3 { font-size: 12px; text-transform: uppercase; letter-spacing: .04em; color: var(--muted);
  margin: 18px 0 6px; }
aside ul { margin: 0; padding-left: 18px; }
aside li { margin: 3px 0; }
.chip { display: inline-block; font-size: 11.5px; padding: 1px 7px; border-radius: 10px; background: var(--chip);
  color: var(--muted); margin-right: 4px; }
.chip.kind { font-weight: 600; color: var(--text); border-radius: 3px; border-left: 4px solid; }
.muted { color: var(--muted); }
.edge-list li { list-style: none; margin-left: -18px; padding: 4px 0; border-bottom: 1px solid var(--line); }
.edge-list .who { font-weight: 600; cursor: pointer; color: var(--link); }
button.linklike { background: none; border: 0; padding: 0; color: var(--link); cursor: pointer; font: inherit; }
#arch .block { cursor: pointer; }
#arch .block:focus { outline: none; }
#arch .block:focus rect, #arch .block.sel rect { stroke-width: 3; }
#arch .edge { transition: opacity .15s; }
#arch .edge.minor { display: none; }
#arch.show-minor .edge.minor { display: inline; }
#arch .edge.off { display: none !important; }
#arch.has-sel .edge { opacity: .12; }
#arch.has-sel .edge.on { opacity: 1; stroke-width: 2.4; display: inline; }
#arch.has-sel .block { opacity: .35; }
#arch.has-sel .block.sel, #arch.has-sel .block.nb { opacity: 1; }
#arch .block.step rect { stroke-width: 3; }
#arch.has-flow .block { opacity: .35; }
#arch.has-flow .block.step { opacity: 1; }
#arch.has-flow .edge { opacity: .15; }
@media (max-width: 900px) {
  main { grid-template-columns: 1fr; }
  aside { border-left: 0; border-top: 1px solid var(--line); max-height: none; position: static; }
  #diagram { padding: 8px; }
  #diagram svg { min-width: 880px; }
}
</style>
</head>
<body>
<header>
  <h1>xournal-qt architecture</h1>
  <span class="links"><a id="readme-link" href="#">Overview (README)</a> · <a id="yaml-link" href="#">architecture.yaml</a></span>
  <div class="filters" id="filters" aria-label="Edge kinds"></div>
</header>
<main>
  <div id="diagram">
@@SVG@@
  </div>
  <aside id="panel" aria-live="polite"></aside>
</main>
<script type="application/json" id="arch-data">@@DATA@@</script>
<script>
(function () {
  "use strict";
  var DATA = JSON.parse(document.getElementById("arch-data").textContent);
  var svg = document.getElementById("arch");
  var panel = document.getElementById("panel");
  var byId = {};
  DATA.blocks.forEach(function (b) { byId[b.id] = b; });
  var layerName = {};
  DATA.layers.forEach(function (l) { layerName[l.id] = l.name; });
  var kindsOn = {};
  DATA.edgeKinds.forEach(function (k) { kindsOn[k.id] = true; });
  var showMinor = false, selected = null, flow = null;
  document.getElementById("readme-link").href = DATA.readme;
  document.getElementById("yaml-link").href = DATA.yaml;

  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }
  function text(s) {  // YAML text: `code` spans, paragraphs
    return esc(s).replace(/`([^`]*)`/g, "<code>$1</code>").replace(/\n/g, "</p><p>");
  }
  function link(url, label) {
    return '<a href="' + esc(url) + '" target="_blank" rel="noopener">' + label + "</a>";
  }

  // the filters
  var filters = document.getElementById("filters");
  DATA.edgeKinds.forEach(function (k) {
    var l = document.createElement("label");
    l.title = k.about;
    l.innerHTML = '<input type="checkbox" checked data-kind="' + k.id + '"><span class="swatch" style="background:' +
      k.color + '"></span>' + esc(k.id);
    filters.appendChild(l);
  });
  var minor = document.createElement("label");
  minor.title = "Edges the static diagram leaves out (always shown for the selected block)";
  minor.innerHTML = '<input type="checkbox" id="minor-toggle"> all edges';
  filters.appendChild(minor);
  filters.addEventListener("change", function (ev) {
    var t = ev.target;
    if (t.id === "minor-toggle") { showMinor = t.checked; }
    else if (t.dataset.kind) { kindsOn[t.dataset.kind] = t.checked; }
    update();
  });

  function update() {
    svg.classList.toggle("show-minor", showMinor);
    svg.classList.toggle("has-sel", !!selected);
    svg.classList.toggle("has-flow", !!flow);
    var near = {};
    svg.querySelectorAll(".edge").forEach(function (p) {
      var on = selected && (p.dataset.from === selected || p.dataset.to === selected);
      p.classList.toggle("off", !kindsOn[p.dataset.kind]);
      p.classList.toggle("on", !!on && kindsOn[p.dataset.kind]);
      if (on && kindsOn[p.dataset.kind]) { near[p.dataset.from] = near[p.dataset.to] = true; }
    });
    var steps = {};
    if (flow) { flow.steps.forEach(function (s) { steps[s.block] = true; }); }
    svg.querySelectorAll(".block").forEach(function (g) {
      g.classList.toggle("sel", g.dataset.id === selected);
      g.classList.toggle("nb", !!near[g.dataset.id]);
      g.classList.toggle("step", !!steps[g.dataset.id]);
    });
  }

  function edgeItem(e, other) {
    var b = byId[other];
    var kind = DATA.edgeKinds.filter(function (k) { return k.id === e.kind; })[0];
    return '<li><span class="chip kind" style="border-color:' + kind.color + '">' + esc(e.kind) + "</span>" +
      '<button class="linklike who" data-go="' + esc(other) + '">' + esc(b.name) + "</button>" +
      '<div class="muted">' + text(e.what) + "</div></li>";
  }

  function showBlock(id) {
    var b = byId[id];
    selected = id; flow = null;
    var outs = DATA.edges.filter(function (e) { return e.from === id; });
    var ins = DATA.edges.filter(function (e) { return e.to === id; });
    var h = '<span class="chip">' + esc(layerName[b.layer]) + '</span><span class="chip">' + esc(b.kindName) +
      "</span>" + "<h2>" + esc(b.name) + "</h2>";
    if (b.target) { h += '<div class="muted">CMake target <code>' + esc(b.target) + "</code></div>"; }
    h += "<p>" + text(b.responsibility) + "</p>";
    h += "<h3>Where</h3><ul>" + b.paths.map(function (p) {
      return "<li>" + link(p.url, "<code>" + esc(p.path) + "</code>") + "</li>";
    }).join("") + "</ul>";
    h += "<h3>Key classes and files</h3><ul>" + b.key.map(function (k) {
      return "<li>" + link(k.url, "<code>" + esc(k.name) + "</code>") + ' <span class="muted">' + text(k.what) +
        "</span></li>";
    }).join("") + "</ul>";
    if (b.docs.length) {
      h += "<h3>Docs</h3><ul>" + b.docs.map(function (d) {
        return "<li>" + link(d.url, esc(d.path.replace(/^qt\/docs\//, ""))) + "</li>";
      }).join("") + "</ul>";
    }
    if (outs.length) {
      h += '<h3>Depends on (' + outs.length + ')</h3><ul class="edge-list">' +
        outs.map(function (e) { return edgeItem(e, e.to); }).join("") + "</ul>";
    }
    if (ins.length) {
      h += '<h3>Used by (' + ins.length + ')</h3><ul class="edge-list">' +
        ins.map(function (e) { return edgeItem(e, e.from); }).join("") + "</ul>";
    }
    h += '<p style="margin-top:18px"><button class="linklike" data-home="1">← Overview</button></p>';
    panel.innerHTML = h;
    panel.scrollTop = 0;
    if (history.replaceState) { history.replaceState(null, "", "#" + id); }
    update();
  }

  function showFlow(f) {
    selected = null; flow = f;
    var h = '<span class="chip">Path through the code</span><h2>' + esc(f.name) + "</h2><ol>" +
      f.steps.map(function (s) {
        return '<li><button class="linklike who" data-go="' + esc(s.block) + '">' + esc(byId[s.block].name) +
          "</button>, " + link(s.url, "<code>" + esc(s.file.split("/").pop()) + "</code>") + ": " + text(s.text) +
          "</li>";
      }).join("") + "</ol>" + '<p><button class="linklike" data-home="1">← Overview</button></p>';
    panel.innerHTML = h;
    if (history.replaceState) { history.replaceState(null, "", "#flow-" + f.id); }
    update();
  }

  function showHome() {
    selected = null; flow = null;
    var h = "<h2>Overview</h2><p>" + text(DATA.intro) + "</p>" +
      '<p class="muted">Click a block for its responsibility, key files (on GitHub) and its edges; click an edge ' +
      "partner to follow it. The filters above hide kinds of edges; “all edges” also shows the ones the static " +
      "diagram leaves out.</p>";
    h += "<h3>Paths through the code</h3><ul>" + DATA.flows.map(function (f) {
      return '<li><button class="linklike" data-flow="' + esc(f.id) + '">' + esc(f.name) + "</button></li>";
    }).join("") + "</ul>";
    h += "<h3>Edges</h3><ul>" + DATA.edgeKinds.map(function (k) {
      return '<li><span class="chip kind" style="border-color:' + k.color + '">' + esc(k.id) + "</span>" + text(k.about) +
        "</li>";
    }).join("") + "</ul>";
    h += "<h3>Layers</h3><ul>" + DATA.layers.map(function (l) {
      return "<li><strong>" + esc(l.name) + "</strong>: " + text(l.about) + "</li>";
    }).join("") + "</ul>";
    panel.innerHTML = h;
    if (history.replaceState) { history.replaceState(null, "", location.pathname + location.search); }
    update();
  }

  svg.addEventListener("click", function (ev) {
    var g = ev.target.closest ? ev.target.closest(".block") : null;
    if (g) { showBlock(g.dataset.id); } else if (selected || flow) { showHome(); }
  });
  svg.addEventListener("keydown", function (ev) {
    var g = ev.target.closest ? ev.target.closest(".block") : null;
    if (g && (ev.key === "Enter" || ev.key === " ")) { ev.preventDefault(); showBlock(g.dataset.id); }
  });
  panel.addEventListener("click", function (ev) {
    var t = ev.target.closest ? ev.target.closest("[data-go],[data-home],[data-flow]") : null;
    if (!t) { return; }
    if (t.dataset.go) { showBlock(t.dataset.go); }
    else if (t.dataset.flow) { showFlow(DATA.flows.filter(function (f) { return f.id === t.dataset.flow; })[0]); }
    else { showHome(); }
  });
  document.addEventListener("keydown", function (ev) { if (ev.key === "Escape") { showHome(); } });

  var hash = decodeURIComponent(location.hash.slice(1));
  var startFlow = DATA.flows.filter(function (f) { return "flow-" + f.id === hash; })[0];
  if (byId[hash]) { showBlock(hash); } else if (startFlow) { showFlow(startFlow); } else { showHome(); }
})();
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="write nothing; fail when the generated files are out of date or the YAML is wrong")
    args = ap.parse_args()
    try:
        data = parse_yaml(YAML_FILE.read_text(encoding="utf-8"), str(YAML_FILE.relative_to(ROOT)))
    except YamlError as e:
        print(f"architecture: {e}", file=sys.stderr)
        return 1
    problems = validate(data)
    if problems:
        for p in problems:
            print(f"architecture.yaml: {p}", file=sys.stderr)
        return 1
    model = Model(data)
    problems = check_cmake(model)
    if problems:
        for p in problems:
            print(f"architecture.yaml: {p}", file=sys.stderr)
        return 1
    outputs = {SVG_FILE: render_svg(model), README_FILE: render_readme(model), HTML_FILE: render_html(model)}
    stale = []
    for path, content in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != content:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
    names = ", ".join(str(p.relative_to(ROOT)) for p in stale)
    if args.check:
        if stale:
            print(f"architecture: out of date: {names}. Run: python3 {GENERATOR}", file=sys.stderr)
            return 1
        print(f"architecture: up to date ({len(model.blocks)} blocks, {len(model.edges)} edges, every path exists)",
              file=sys.stderr)
        return 0
    print(f"architecture: wrote {names}" if stale else "architecture: nothing changed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
