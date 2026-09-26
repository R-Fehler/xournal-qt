import tempfile
"""Throwaway: how long would fine-tuning a PP-OCRv6 kraken model on a user's lines take on a laptop CPU?
Times forward+backward+Adam steps (CTC loss) on batches of real line images (height 128), then exports the
network to ONNX with a dynamic width and compares it with PyTorch. Usage: kraken_finetune_time.py threads batch model...
With batch 8 the medium model was killed at the 3 GB cap; batch 2 fits."""
import io, os, random, sys, time
import numpy as np
import pandas as pd
import torch
from PIL import Image
from kraken.models import load_models
sys.path.insert(0, os.path.dirname(__file__))
from eval_lines_common import SETS

threads, B = int(sys.argv[1]), int(sys.argv[2]); torch.set_num_threads(threads)
import resource
d = pd.read_parquet(SETS['fhswf German'])
sample = random.Random(3).sample(range(len(d)), B)

def make_batch(Ht, maxW=1200):
    """Lines scaled to the model's input height, cropped to maxW px (a user's line in the app is shorter than
    these full-width scans)."""
    imgs = []
    for i in sample:
        im = Image.open(io.BytesIO(d.iloc[i]['image']['bytes'])).convert('RGB')
        W, H = im.size
        im = im.resize((max(32, int(W * Ht / H)), Ht)).crop((0, 0, min(maxW, max(32, int(W * Ht / H))), Ht))
        imgs.append(np.asarray(im, dtype=np.float32).transpose(2, 0, 1) / 255.0)
    batch = torch.ones(len(imgs), 3, Ht, max(a.shape[2] for a in imgs))
    for k, a in enumerate(imgs):
        batch[k, :, :, :a.shape[2]] = torch.from_numpy(a)
    return batch, torch.tensor([a.shape[2] for a in imgs]), imgs

for path in sys.argv[3:]:
    m = [x for x in load_models(path) if 'recognition' in x.model_type][0]
    batch, lens, imgs = make_batch(m.input[2])
    print(f"batch of {len(imgs)} lines, height {m.input[2]}, mean width {int(lens.float().mean())} px, threads {threads}", flush=True)
    net = m.nn
    net.train()
    opt = torch.optim.AdamW(net.parameters(), lr=5e-5)
    ctc = torch.nn.CTCLoss(blank=0, zero_infinity=True)
    targets = torch.randint(1, net.num_classes, (len(imgs), 40))
    times = []
    for step in range(4):
        t = time.perf_counter()
        logits, out_lens = net(batch, lens)
        lp = logits.squeeze(2).permute(2, 0, 1).log_softmax(-1)  # (T, N, C)
        loss = ctc(lp, targets, out_lens, torch.full((len(imgs),), 40))
        opt.zero_grad(); loss.backward(); opt.step()
        times.append(time.perf_counter() - t)
    per_line = min(times[1:]) / len(imgs)
    params = sum(p.numel() for p in net.parameters())
    print(f"{os.path.basename(path)} {params/1e6:.2f}M params: train step {min(times[1:]):.2f}s/batch = {per_line:.3f}s/line "
          f"-> 64 lines x 20 epochs ~ {64*20*per_line/60:.1f} min, 256 x 20 ~ {256*20*per_line/60:.1f} min; peak RSS {resource.getrusage(resource.RUSAGE_SELF).ru_maxrss/1024:.0f} MB", flush=True)
    # ONNX export (inference graph)
    net.eval()
    onnx_path = f"{tempfile.gettempdir()}/{os.path.basename(path)}.onnx"
    x1 = batch[:1, :, :, :int(lens[0])]
    try:
        class Wrap(torch.nn.Module):
            def __init__(s, n): super().__init__(); s.n = n
            def forward(s, x): return s.n(x)[0]
        torch.onnx.export(Wrap(net), (x1,), onnx_path, input_names=['x'], output_names=['logits'],
                          dynamic_axes={'x': {3: 'w'}, 'logits': {3: 'wout'}}, opset_version=17, dynamo=False)
        import onnxruntime as ort
        so = ort.SessionOptions(); so.intra_op_num_threads = threads
        s = ort.InferenceSession(onnx_path, so, providers=['CPUExecutionProvider'])
        x2 = batch[-1:, :, :, :int(lens[-1])]
        with torch.no_grad():
            ref = net(x2)[0].numpy()
        t = time.perf_counter(); out = s.run(None, {'x': x2.numpy()})[0]; tt = time.perf_counter() - t
        t = time.perf_counter()
        with torch.no_grad(): net(x2)
        tp = time.perf_counter() - t
        same = (out.argmax(1) == ref.argmax(1)).mean()
        print(f"   ONNX export ok: {os.path.getsize(onnx_path)/1e6:.1f} MB, argmax agreement {same:.3f}, "
              f"onnxruntime {tt*1000:.0f} ms vs torch {tp*1000:.0f} ms for a {int(lens[-1])} px line", flush=True)
    except Exception as e:
        print("   ONNX export failed:", type(e).__name__, str(e).splitlines()[0][:300] if str(e) else "")
