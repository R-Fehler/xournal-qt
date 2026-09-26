import tempfile
"""Throwaway: can a kraken PP-OCRv6 network be exported to ONNX (legacy exporter) with a FIXED input width, so a C++
app could run it with onnxruntime, padding each line to a width bucket? (The dynamic-width export in
kraken_finetune_time.py fails in the backbone.) Usage: kraken_onnx_fixed.py threads width model.safetensors"""
import os, sys, time
import numpy as np
import torch
from kraken.models import load_models
import torch.nn.functional as F
import kraken.lib.ppocr.backbone as bb

def _same_pad(x, kernel_size, stride=1, value=0.0):
    # kraken's version does max() on traced shapes, which the legacy exporter cannot export; with a fixed input
    # width the shapes are constants, so plain ints are fine.
    ih, iw = int(x.shape[-2]), int(x.shape[-1])
    kh, kw = bb._pair(kernel_size); sh, sw = bb._pair(stride)
    pad_h = max((-(-ih // sh) - 1) * sh + kh - ih, 0); pad_w = max((-(-iw // sw) - 1) * sw + kw - iw, 0)
    if pad_h == 0 and pad_w == 0:
        return x
    return F.pad(x, [pad_w // 2, pad_w - pad_w // 2, pad_h // 2, pad_h - pad_h // 2], value=value)
bb._same_pad = _same_pad

def _backbone_forward(self, x):  # the same, with the pooling height as a plain int (fixed at export)
    for b in (self.conv1, self.blocks2, self.blocks3, self.blocks4, self.blocks5, self.blocks6):
        x = b(x)
    return F.avg_pool2d(x, kernel_size=(int(x.shape[2]), 2))
bb.PPLCNetV4.forward = _backbone_forward

threads, W, path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
torch.set_num_threads(threads)
m = [x for x in load_models(path) if 'recognition' in x.model_type][0]
net = m.nn.eval()
H = m.input[2]
# a real line (IAM validation), scaled to the model height, padded with white / cropped to W
import io, pandas as pd
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from eval_lines_common import SETS
im = Image.open(io.BytesIO(pd.read_parquet(SETS['IAM-val (en)']).iloc[3]['image']['bytes'])).convert('RGB')
im = im.resize((max(1, im.size[0] * H // im.size[1]), H))
canvas = Image.new('RGB', (W, H), 'white'); canvas.paste(im, (0, 0))
x = torch.from_numpy(np.asarray(canvas, dtype=np.float32).transpose(2, 0, 1)[None] / 255.0)

class Wrap(torch.nn.Module):
    def __init__(s, n): super().__init__(); s.n = n
    def forward(s, x): return s.n(x)[0]

out = f"{tempfile.gettempdir()}/{os.path.basename(path)}-{W}.onnx"
torch.onnx.export(Wrap(net), (x,), out, input_names=['x'], output_names=['logits'], opset_version=17, dynamo=False)
import onnxruntime as ort
so = ort.SessionOptions(); so.intra_op_num_threads = threads
s = ort.InferenceSession(out, so, providers=['CPUExecutionProvider'])
with torch.no_grad():
    ref = net(x)[0].numpy()
s.run(None, {'x': x.numpy()})
t = time.perf_counter(); o = s.run(None, {'x': x.numpy()})[0]; tt = time.perf_counter() - t
t = time.perf_counter()
with torch.no_grad(): net(x)
tp = time.perf_counter() - t
print(f"{os.path.basename(path)} {H}x{W}: ONNX {os.path.getsize(out)/1e6:.1f} MB, argmax agreement "
      f"{(o.argmax(1) == ref.argmax(1)).mean():.3f}, max |diff| {np.abs(o - ref).max():.2e}, "
      f"onnxruntime {tt*1000:.0f} ms vs torch {tp*1000:.0f} ms", flush=True)
