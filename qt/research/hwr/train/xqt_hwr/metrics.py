"""Scores of a model's readings of a line: CER and WER of the best reading, and **words found**, the measure the app
cares about (search, not transcription).

Words found: each word of the true text with MIN_LETTERS (3) or more letters (as the search folds and splits text) is a
search term. It is found when one of the line's candidate words (decode.candidate_words: the best reading's words and
the other readings' words with at least MIN_P of the guesses) matches it as the app's plain search matches handwriting:
the candidate contains the term, or is the term with a typo (terms of 5+ letters; WordMatch.h, InkText.h). Also counted:
`exact` (containment only), `fuzzy` (the fuzzy search: letters in order too), `top1` (best reading only), and false
hits: words of other lines (not in this line) that this line's candidates would match.
"""
from __future__ import annotations

import random
from collections import defaultdict

from . import wordmatch
from .decode import candidate_words
from .text import words

COUNTS = ("lines", "char_edits", "chars", "word_edits", "words", "terms", "found", "found_exact", "found_fuzzy",
          "found_top1", "false_tries", "false_hits")


def levenshtein(a, b) -> int:
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x != y)))
        prev = cur
    return prev[-1]


def _found(term: str, cands, in_order=False, exact_only=False) -> bool:
    for c in cands:
        q = wordmatch.match(term, c, in_order=in_order)
        if q == wordmatch.EXACT or (q == wordmatch.FUZZY and not exact_only):
            return True
    return False


def score_line(truth: str, readings: list[tuple[str, float]], others: list[str] | None = None,
               rnd: random.Random | None = None) -> dict:
    """Counts for one line (sums over lines give the rates; see `rates`). `others`: texts of other lines, for false
    hits."""
    best = readings[0][0] if readings else ""
    c = dict.fromkeys(COUNTS, 0)
    c["lines"] = 1
    c["char_edits"] = levenshtein(truth, best)
    c["chars"] = len(truth)
    c["word_edits"] = levenshtein(truth.split(), best.split())
    c["words"] = len(truth.split())
    cands = candidate_words(readings)
    top1 = set(words(best))
    truth_words = set(words(truth))
    for t in words(truth):
        if len(t) < wordmatch.MIN_LETTERS:
            continue
        c["terms"] += 1
        c["found"] += _found(t, cands)
        c["found_exact"] += _found(t, cands, exact_only=True)
        c["found_fuzzy"] += _found(t, cands, in_order=True)
        c["found_top1"] += _found(t, top1)
    if others:
        rnd = rnd or random.Random(0)
        pool = [w for o in rnd.sample(others, min(2, len(others))) for w in words(o)
                if len(w) >= wordmatch.MIN_LETTERS and w not in truth_words][:5]
        for w in pool:
            c["false_tries"] += 1
            c["false_hits"] += _found(w, cands)
    return c


def add(into: dict, c: dict):
    for k in COUNTS:
        into[k] = into.get(k, 0) + c.get(k, 0)


def rates(c: dict) -> dict:
    def r(a, b):
        return round(c.get(a, 0) / c[b], 4) if c.get(b) else None
    return {"lines": c.get("lines", 0), "terms": c.get("terms", 0), "cer": r("char_edits", "chars"),
            "wer": r("word_edits", "words"), "words_found": r("found", "terms"),
            "words_found_exact": r("found_exact", "terms"), "words_found_fuzzy": r("found_fuzzy", "terms"),
            "words_found_top1": r("found_top1", "terms"), "false_hits": r("false_hits", "false_tries")}


class Groups:
    """Counts per group (all, per dataset, per language, per writer)."""

    def __init__(self):
        self.c = defaultdict(dict)

    def add(self, keys: list[str], counts: dict):
        for k in keys:
            add(self.c[k], counts)

    def table(self) -> dict:
        return {k: rates(v) for k, v in sorted(self.c.items())}

