"""The app's rules for when a recognised word matches a search term (qt/src/session/WordMatch.h, InkText.h).

A term of MIN_LETTERS or more letters matches a word (both case folded, text.words()) when
  1. the word contains the term: exact;
  2. (fuzzy search only, `in_order`) the word starts with the term's first letter and has its letters in order with at
     most len(term) // 2 other letters between them;
  3. the term has a typo (Damerau-Levenshtein, optimal string alignment): one for terms of 5+ letters, two for 8+ with
     tolerance 2. The app applies this to handwriting even with the fuzzy search off.
"""
from __future__ import annotations

MIN_LETTERS = 3
TYPO_LETTERS = 5
TWO_TYPOS_LETTERS = 8
DEFAULT_TYPOS = 1

NONE, FUZZY, EXACT = 0, 1, 2


def gaps_of(term: str, word: str) -> int:
    if not word or not term or word[0] != term[0]:
        return -1
    i, gaps, last = 0, 0, -1
    for j, c in enumerate(word):
        if i < len(term) and c == term[i]:
            if last >= 0:
                gaps += j - last - 1
            last, i = j, i + 1
    return gaps if i == len(term) else -1


def typos_allowed(letters: int, typos: int = DEFAULT_TYPOS) -> int:
    if typos <= 0 or letters < TYPO_LETTERS:
        return 0
    if typos >= 2 and letters >= TWO_TYPOS_LETTERS:
        return 2
    return 1


def edit_distance(a: str, b: str, mx: int) -> int:
    """Optimal string alignment distance if <= mx, else mx + 1."""
    if abs(len(a) - len(b)) > mx:
        return mx + 1
    d = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i in range(len(a) + 1):
        d[i][0] = i
    for j in range(len(b) + 1):
        d[0][j] = j
    for i in range(1, len(a) + 1):
        for j in range(1, len(b) + 1):
            c = 0 if a[i - 1] == b[j - 1] else 1
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + c)
            if i > 1 and j > 1 and a[i - 1] == b[j - 2] and a[i - 2] == b[j - 1]:
                d[i][j] = min(d[i][j], d[i - 2][j - 2] + 1)
    return min(d[-1][-1], mx + 1)


def match(term: str, word: str, typos: int = DEFAULT_TYPOS, in_order: bool = False) -> int:
    """EXACT, FUZZY or NONE (Rule::match). `in_order`: the fuzzy search's rule 2 too."""
    n = len(term)
    if n == 0 or not word:
        return NONE
    if len(word) >= n and term in word:
        return EXACT
    if in_order:
        g = gaps_of(term, word)
        if 0 <= g <= n // 2:
            return FUZZY
    t = typos_allowed(n, typos)
    if t and edit_distance(term, word, t) <= t:
        return FUZZY
    return NONE
