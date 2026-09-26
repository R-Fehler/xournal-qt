import os
"""Throwaway: TrOCR on real handwriting lines (IAM-line validation; fhswf German handwriting shard 1).
Reports CER/WER (top-1), search recall of the ground-truth words (exact / app-style fuzzy, top-1 vs. top-k beam
union), false-hit rate, lexicon snapping, and seconds per line. Usage: eval_lines.py N threads model..."""
import glob, io, sys, time, random
import pandas as pd
from PIL import Image
from trocr_onnx import TrOCR
from wordmatch import words, match, edit_distance

def cer(ref, hyp):
    import numpy as np
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

def snap(ws, lex):
    """Replace each word not in the lexicon by the closest lexicon word (edit distance <= 1/2)."""
    out = []
    for w in ws:
        if w in lex or len(w) < 4:
            out.append(w); continue
        mx = 1 if len(w) < 8 else 2
        best = min(((edit_distance(w, l, mx), l) for l in lex if abs(len(l) - len(w)) <= mx), default=(99, w))
        out.append(best[1] if best[0] <= mx else w)
    return out

N, threads = int(sys.argv[1]), int(sys.argv[2])
models = sys.argv[3:] or ["small", "small-q", "base-q"]
sets = {
    "IAM-val (en)": glob.glob(os.path.expanduser('~/.cache/huggingface/hub/datasets--Teklia--IAM-line/snapshots/*/data/validation.parquet'))[0],
    "fhswf German": glob.glob(os.path.expanduser('~/.cache/huggingface/hub/datasets--fhswf--german_handwriting/snapshots/*/data/train-00001-of-00008.parquet'))[0],
}
for mname in models:
    repo = "Xenova/trocr-base-handwritten" if mname.startswith("base") else "Xenova/trocr-small-handwritten"
    m = TrOCR(repo, quantized=mname.endswith("-q"), threads=threads)
    m.greedy(Image.new("L", (400, 60), 255))  # warm-up
    for sname, path in sets.items():
        d = pd.read_parquet(path)
        allgt = [t for t in d.text]
        rnd = random.Random(7)
        idx = rnd.sample(range(len(d)), N)
        lex = set(w for i, t in enumerate(allgt) if i not in idx for w in words(t))  # "the library's vocabulary"
        S = dict(cer=0, wer=0, t1=0, tk=0, n=0, rec_top1_exact=0, rec_top1_fuzzy=0, rec_topk_exact=0, rec_topk_fuzzy=0,
                 rec_snap_exact=0, rec_union=0, fpu=0, fp1=0, fpk=0, fpn=0, words=0, cer_snap=0)
        for i in idx:
            img = Image.open(io.BytesIO(d.iloc[i]["image"]["bytes"]))
            gt = d.iloc[i]["text"]
            t = time.perf_counter(); g = m.greedy(img); S["t1"] += time.perf_counter() - t
            t = time.perf_counter(); b = m.beam(img, k=5); S["tk"] += time.perf_counter() - t
            S["cer"] += cer(gt, g); S["wer"] += wer(gt, g); S["n"] += 1
            top1 = words(g); topk = set(w for txt, _ in b for w in words(txt)) | set(top1)
            sn = snap(top1, lex)
            S["cer_snap"] += cer(" ".join(words(gt)), " ".join(sn))
            terms = [w for w in words(gt) if len(w) >= 3]
            for w in terms:
                S["words"] += 1
                S["rec_top1_exact"] += found(w, top1, False); S["rec_top1_fuzzy"] += found(w, top1, True)
                S["rec_topk_exact"] += found(w, topk, False); S["rec_topk_fuzzy"] += found(w, topk, True)
                S["rec_snap_exact"] += found(w, sn, False)
                S["rec_union"] += found(w, topk | set(sn), True)  # lexicon words added as extra candidates
            # false hits: 5 random words (3+ letters) of other lines that are not in this line
            gtw = set(words(gt))
            others = [w for w in words(allgt[rnd.randrange(len(allgt))] + " " + allgt[rnd.randrange(len(allgt))]) if len(w) >= 3 and w not in gtw][:5]
            for w in others:
                S["fpn"] += 1; S["fp1"] += found(w, top1, True); S["fpk"] += found(w, topk, True); S["fpu"] += found(w, topk | set(sn), True)
        n, W = S["n"], S["words"]
        print(f"{mname:8s} {sname:13s} n={n} CER={S['cer']/n:.3f} WER={S['wer']/n:.3f} "
              f"greedy={S['t1']/n:.2f}s/line beam5={S['tk']/n:.2f}s/line | recall of GT words ({W}): "
              f"top1 exact={S['rec_top1_exact']/W:.2f} fuzzy={S['rec_top1_fuzzy']/W:.2f} "
              f"top5 exact={S['rec_topk_exact']/W:.2f} fuzzy={S['rec_topk_fuzzy']/W:.2f} "
              f"lexicon-snapped exact={S['rec_snap_exact']/W:.2f} (word-CER after snap {S['cer_snap']/n:.3f}) "
              f"top5+lexicon fuzzy={S['rec_union']/W:.2f} | "
              f"false hits fuzzy top1={S['fp1']/S['fpn']:.3f} top5={S['fpk']/S['fpn']:.3f} top5+lexicon={S['fpu']/S['fpn']:.3f}", flush=True)
