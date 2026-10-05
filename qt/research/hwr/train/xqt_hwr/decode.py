"""Decoding as the app decodes: TrOCR beam search (a port of qt/src/hwr/BeamSearch.cpp), a CTC prefix beam search with
top-k readings, and which words of the readings the search sees (qt/src/hwr/WordAlignment.cpp, session/InkText.h)."""
from __future__ import annotations

import math
from typing import Callable

import numpy as np

from .text import words

MIN_P = 0.05  # InkText.h: a reading below this share takes part only when it is the best


def beam_search(step: Callable, start: int, end: int, k: int, max_length: int) -> list[tuple[list[int], float, float]]:
    """BeamSearch.cpp's beamSearch. `step(tokens, parents)` gives the next-token scores [rows, V] (numpy) for the
    rows' last tokens (`parents[r]`: the row of the step before that row r continues; empty at the first step), or
    None (failed). Returns (tokens without the start, with the end if it ended; log-probability; per token), best
    first by log-probability per token, without duplicates."""
    k = max(1, k)
    beams = [([start], 0.0)]
    done = []
    last = [start] * k
    parents: list[int] = []
    for _ in range(max_length):
        logits = step(last, parents)
        if logits is None or len(logits) < len(beams):
            return []
        cands = []
        for b, (seq, score) in enumerate(beams):
            row = np.asarray(logits[b], dtype=np.float64)
            top = row.max()
            norm = top + math.log(np.exp(row - top).sum())
            take = min(k, row.size)
            idx = np.argpartition(-row, take - 1)[:take]
            idx = idx[np.argsort(-row[idx], kind="stable")]
            for t in idx:
                cands.append((score + (row[t] - norm), b, int(t)))
        cands.sort(key=lambda c: -c[0])  # (stable, as std::stable_sort)
        nxt, rows = [], []
        for score, b, t in cands:
            nb = (beams[b][0] + [t], score)
            if t == end:
                done.append(nb)
            else:
                nxt.append(nb)
                rows.append(b)
            if len(nxt) == k:
                break
        if len(done) >= k or not nxt:
            beams = []
            break
        live = len(nxt)
        while len(nxt) < k:
            nxt.append(nxt[-1])
            rows.append(rows[-1])
        beams = nxt[:live]
        parents = rows
        last = [(beams[r] if r < live else beams[live - 1])[0][-1] for r in range(len(rows))]
    done += beams
    done.sort(key=lambda b: -b[1] / len(b[0]))
    out, seen = [], set()
    for seq, score in done:
        tokens = seq[1:]
        key = tuple(tokens[:-1] if tokens and tokens[-1] == end else tokens)
        if key in seen:
            continue
        seen.add(key)
        n = max(1, len(tokens))
        out.append((tokens, score, score / n))
        if len(out) == k:
            break
    return out


def _lse(a: float, b: float) -> float:
    if a == -math.inf:
        return b
    if b == -math.inf:
        return a
    m = max(a, b)
    return m + math.log(math.exp(a - m) + math.exp(b - m))


def ctc_beams(logp: np.ndarray, beam: int = 16, topk: int = 5, prune: float = -12.0,
              blank: int = 0) -> list[tuple[tuple[int, ...], float]]:
    """CTC prefix beam search over log-probabilities [T, C]: the `topk` likeliest label sequences with their
    log-probabilities (summed over their alignments), best first."""
    NEG = -math.inf
    beams = {(): (0.0, NEG)}  # prefix -> (log p ending in blank, log p ending in a label)
    for t in range(logp.shape[0]):
        row = logp[t]
        cand = np.where(row > prune)[0]
        if cand.size == 0:
            cand = np.array([int(row.argmax())])
        nb: dict = {}

        def add(prefix, pb, pnb):
            ob, onb = nb.get(prefix, (NEG, NEG))
            nb[prefix] = (_lse(ob, pb), _lse(onb, pnb))

        for prefix, (pb, pnb) in beams.items():
            total = _lse(pb, pnb)
            for c in cand:
                p = float(row[c])
                if c == blank:
                    add(prefix, total + p, NEG)
                    continue
                c = int(c)
                last = prefix[-1] if prefix else None
                ext = prefix + (c,)
                if c == last:
                    add(ext, NEG, pb + p)        # a repeat needs a blank in between
                    add(prefix, NEG, pnb + p)    # or it is the same label continued
                else:
                    add(ext, NEG, total + p)
        beams = dict(sorted(nb.items(), key=lambda kv: -_lse(*kv[1]))[:beam])
    out = sorted(((p, _lse(*s)) for p, s in beams.items()), key=lambda x: -x[1])
    return out[:topk]


def shares(log_probs: list[float]) -> list[float]:
    """Each reading's share of the guesses: the softmax of the readings' log-probabilities (WordAlignment.cpp)."""
    if not log_probs:
        return []
    best = max(log_probs)
    e = [math.exp(p - best) for p in log_probs]
    s = sum(e)
    return [x / s for x in e]


def candidate_words(readings: list[tuple[str, float]], min_p: float = MIN_P) -> set[str]:
    """The words the search would match for a line: the words of the best reading, and every other word whose share
    (summed over the readings that have it) is at least `min_p`. The app counts shares per word box; scanned lines
    have no word boxes, so here they are counted per line (see the README)."""
    if not readings:
        return set()
    sh = shares([lp for _, lp in readings])
    share: dict[str, float] = {}
    for (text, _), p in zip(readings, sh):
        for w in set(words(text)):
            share[w] = share.get(w, 0.0) + p
    best = set(words(readings[0][0]))
    return best | {w for w, p in share.items() if p >= min_p}
