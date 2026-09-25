"""Throwaway: disk size, load time, peak RSS and time per line of one recogniser, in its own process.
Usage: footprint.py trocr-small|trocr-small-q|kraken:<model.safetensors> threads [N lines]"""
import io, os, random, resource, sys, time
import pandas as pd
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from eval_lines_common import SETS

def rss_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024

kind, threads = sys.argv[1], int(sys.argv[2])
N = int(sys.argv[3]) if len(sys.argv) > 3 else 10
base = rss_mb()
t = time.perf_counter()
if kind.startswith("trocr"):
    from trocr_onnx import TrOCR
    m = TrOCR("Xenova/trocr-small-handwritten", quantized=kind.endswith("-q"), threads=threads)
    size = m.bytes
    run = lambda im: m.greedy(im)
    runk = lambda im: m.beam(im, k=5)
else:
    import torch
    torch.set_num_threads(threads)
    from kraken.tasks import RecognitionTaskModel
    from kraken.containers import Segmentation, BaselineLine
    from kraken.configs import RecognitionInferenceConfig

    def recognise(model, cfg, im):  # one line = the whole image (as in kraken_eval.py)
        im = im.convert('L'); W, H = im.size
        line = BaselineLine(id='l0', baseline=[(0, int(H * 0.75)), (W - 1, int(H * 0.75))],
                            boundary=[(0, 0), (W - 1, 0), (W - 1, H - 1), (0, H - 1)])
        seg = Segmentation(type='baselines', imagename='x', text_direction='horizontal-lr',
                           script_detection=False, lines=[line])
        return ' '.join(r.prediction for r in model.predict(im, seg, cfg)).strip()
    path = kind.split(":", 1)[1]
    model = RecognitionTaskModel.load_model(path)
    cfg = RecognitionInferenceConfig(num_line_workers=0)
    size = os.path.getsize(path)
    run = lambda im: recognise(model, cfg, im)
    runk = None
load = time.perf_counter() - t
after_load = rss_mb()
run(Image.new("L", (400, 64), 255))
d = pd.read_parquet(SETS["IAM-val (en)"])
idx = random.Random(11).sample(range(len(d)), N)
ims = [Image.open(io.BytesIO(d.iloc[i]["image"]["bytes"])) for i in idx]
t = time.perf_counter()
for im in ims:
    run(im)
per = (time.perf_counter() - t) / N
perk = None
if runk:
    t = time.perf_counter()
    for im in ims:
        runk(im)
    perk = (time.perf_counter() - t) / N
print(f"{kind}: file {size/1e6:.0f} MB, load {load:.2f}s, peak RSS {rss_mb():.0f} MB "
      f"(python+pandas baseline {base:.0f} MB, after load {after_load:.0f} MB), "
      f"{per:.2f}s/line greedy" + (f", {perk:.2f}s/line beam5" if perk else "") + f", {threads} threads", flush=True)
