#!/usr/bin/env python3
"""Checks the fork's documentation links (in CI: .github/workflows/xqt-build.yml, job "docs").

1. Every relative link in the fork's Markdown files resolves: the file or folder exists, and an anchor
   (`file.md#heading`) names a heading of that file (GitHub's rules for heading anchors).
2. Every `qt/docs/...` path cited in code (the fork's C++, QML, CMake, scripts and workflows, and the seams in
   upstream's files) and in the fork's own Markdown outside the dated records (review/, history/, release-notes/)
   exists.

The fork's Markdown: AGENTS.md, CLAUDE.md, FORK.md, README.md, TODO.md, VISION.md and everything under qt/ except
the vendored qt/3rdparty. Only files git tracks are read. Run from anywhere: `python3 qt/scripts/check-doc-links.py`.
Prints each broken link as `file:line: target` and exits with 1 when there is one.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ROOT_DOCS = {"AGENTS.md", "CLAUDE.md", "FORK.md", "README.md", "TODO.md", "VISION.md"}
CODE_SUFFIXES = {".h", ".hpp", ".cpp", ".c", ".qml", ".js", ".cmake", ".txt", ".sh", ".py", ".yml", ".yaml",
                 ".xml", ".in", ".java", ".kt", ".gradle", ".pro", ".frag", ".vert", ".json"}
# Dated records: they describe the tree as it was, so the paths they cite in text are not checked (their links are).
RECORDS = ("qt/docs/review/", "qt/docs/history/", "qt/docs/release-notes/")

INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_LINK = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s+\"[^\"]*\")?\s*$")
INLINE_CODE = re.compile(r"`[^`]*`")
CITED_PATH = re.compile(r"(?<![\w./-])qt/docs/[\w./-]*[\w/]")


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def is_fork_markdown(path):
    if not path.endswith(".md"):
        return False
    if "/" not in path:
        return path in ROOT_DOCS
    return path.startswith("qt/") and not path.startswith("qt/3rdparty/")


def is_code(path):
    """The fork's code, the xqt-* workflows, and upstream's files (whose seams, marked xournal-qt:, cite docs)."""
    if path.startswith(("qt/3rdparty/", "qt/docs/", "test/files/")):
        return False
    p = Path(path)
    return p.suffix in CODE_SUFFIXES or p.name == "CMakeLists.txt"


def slug(heading):
    """GitHub's anchor of a heading: lower case, punctuation dropped, spaces to hyphens."""
    text = re.sub(r"<[^>]+>", "", heading)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # a link in a heading: its text
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


_anchor_cache = {}


def anchors_of(md_path):
    if md_path not in _anchor_cache:
        seen = {}
        anchors = set()
        in_fence = False
        for line in md_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lstrip().startswith(("```", "~~~")):
                in_fence = not in_fence
                continue
            m = re.match(r"^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$", line)
            if in_fence or not m:
                for a in re.findall(r"<a\s+(?:name|id)=\"([^\"]+)\"", line):
                    anchors.add(a)
                continue
            base = slug(m.group(1))
            n = seen.get(base, 0)
            seen[base] = n + 1
            anchors.add(base if n == 0 else f"{base}-{n}")
        _anchor_cache[md_path] = anchors
    return _anchor_cache[md_path]


def check_link(source, target):
    """None when the link resolves, else the reason."""
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("//"):
        return None  # http:, https:, mailto:, …
    path_part, _, anchor = target.partition("#")
    if not path_part:
        dest = source
    else:
        if path_part.startswith("/"):
            dest = ROOT / path_part.lstrip("/")
        else:
            dest = (source.parent / path_part)
        dest = Path(*dest.parts)  # keep `..` for the check below
        try:
            dest = dest.resolve()
            dest.relative_to(ROOT)
        except ValueError:
            return "outside the repository"
        if not dest.exists():
            return "missing"
    if anchor and dest.suffix == ".md" and dest.is_file():
        if anchor.lower() not in anchors_of(dest):
            return f"no heading #{anchor}"
    return None


def check_markdown(rel, problems):
    path = ROOT / rel
    in_fence = False
    check_text = not rel.startswith(RECORDS)
    for no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if line.lstrip().startswith(("```", "~~~")):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for regex in (INLINE_LINK, REF_LINK):
            for target in regex.findall(INLINE_CODE.sub("", line)):
                reason = check_link(path, target)
                if reason:
                    problems.append(f"{rel}:{no}: {target} ({reason})")
        if check_text:
            for cited in CITED_PATH.findall(line):
                if not (ROOT / cited).exists():
                    problems.append(f"{rel}:{no}: {cited} (cited, missing)")


def check_code(rel, problems):
    try:
        text = (ROOT / rel).read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return
    for no, line in enumerate(text.splitlines(), 1):
        for cited in CITED_PATH.findall(line):
            cited = cited.rstrip(".")
            if not (ROOT / cited).exists():
                problems.append(f"{rel}:{no}: {cited} (cited, missing)")


def main():
    problems = []
    files = tracked_files()
    markdown = [f for f in files if is_fork_markdown(f)]
    code = [f for f in files if is_code(f)]
    for rel in markdown:
        check_markdown(rel, problems)
    for rel in code:
        check_code(rel, problems)
    for p in problems:
        print(p)
    print(f"check-doc-links: {len(markdown)} Markdown files, {len(code)} code files, {len(problems)} broken",
          file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
