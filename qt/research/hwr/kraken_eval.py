"""Throwaway: kraken 7 PP-OCRv6 line recognisers (Apache-2.0, Zenodo) on the same lines as eval_lines.py.
Needs torch (CPU) and kraken>=7.1 (a venv). Usage: kraken_eval.py N threads model.safetensors..."""
import glob, io, os, random, sys, time
import pandas as pd
import torch
from PIL import Image, ImageOps
from kraken.tasks import RecognitionTaskModel
from kraken.containers import Segmentation, BaselineLine
from kraken.configs import RecognitionInferenceConfig
sys.path.insert(0, os.path.dirname(__file__))
from wordmatch import words, match
from eval_lines_common import cer, wer, found, SETS

N, threads = int(sys.argv[1]), int(sys.argv[2])
torch.set_num_threads(threads)

def seg_for(im):
    W, H = im.size
    line = BaselineLine(id='l0', baseline=[(0, int(H * 0.75)), (W - 1, int(H * 0.75))],
                        boundary=[(0, 0), (W - 1, 0), (W - 1, H - 1), (0, H - 1)])
    return Segmentation(type='baselines', imagename='x', text_direction='horizontal-lr',
                        script_detection=False, lines=[line])

def recognise(model, cfg, im):
    im = im.convert('L')
    return ' '.join(r.prediction for r in model.predict(im, seg_for(im), cfg)).strip()

for path in sys.argv[3:]:
    t = time.perf_counter()
    model = RecognitionTaskModel.load_model(path)
    load = time.perf_counter() - t
    cfg = RecognitionInferenceConfig(num_line_workers=0)
    params = sum(p.numel() for p in model.parameters())
    recognise(model, cfg, Image.new('L', (400, 64), 255))
    for sname, ppath in SETS.items():
        d = pd.read_parquet(ppath)
        rnd = random.Random(7)
        idx = rnd.sample(range(len(d)), N)
        S = dict(cer=0, wer=0, t=0, n=0, r1e=0, r1f=0, w=0)
        for i in idx:
            img = Image.open(io.BytesIO(d.iloc[i]['image']['bytes']))
            gt = d.iloc[i]['text']
            t = time.perf_counter(); hyp = recognise(model, cfg, img); S['t'] += time.perf_counter() - t
            S['cer'] += cer(gt, hyp); S['wer'] += wer(gt, hyp); S['n'] += 1
            top1 = words(hyp)
            for w in (w for w in words(gt) if len(w) >= 3):
                S['w'] += 1; S['r1e'] += found(w, top1, False); S['r1f'] += found(w, top1, True)
        n, W = S['n'], S['w']
        print(f"{os.path.basename(path):18s} {params/1e6:.2f}M load={load:.2f}s {sname:13s} n={n} CER={S['cer']/n:.3f} "
              f"WER={S['wer']/n:.3f} {S['t']/n:.3f}s/line | recall top1 exact={S['r1e']/W:.2f} fuzzy={S['r1f']/W:.2f}", flush=True)
