import os
"""Shared bits of the throwaway line evaluations."""
import glob
from wordmatch import match

SETS = {
    "IAM-val (en)": (glob.glob(os.path.expanduser('~/.cache/huggingface/hub/datasets--Teklia--IAM-line/snapshots/*/data/validation.parquet')) or [None])[0],
    "fhswf German": (glob.glob(os.path.expanduser('~/.cache/huggingface/hub/datasets--fhswf--german_handwriting/snapshots/*/data/train-00001-of-00008.parquet')) or [None])[0],
}

def cer(ref, hyp):
    r, h = ref, hyp
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = min(d[j] + 1, d[j-1] + 1, prev + (r[i-1] != h[j-1]))
            prev, d[j] = d[j], cur
    return d[len(h)] / max(1, len(r))

def wer(ref, hyp):
    return cer(ref.split(), hyp.split())

def found(term, cands, fuzzy):
    best = max((match(term, w) for w in cands), default=0)
    return best == 2 or (fuzzy and best == 1)
