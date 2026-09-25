"""Throwaway: TrOCR (Xenova ONNX export) on CPU with onnxruntime, no torch/transformers.
Greedy and beam search (top-k candidates with log-probabilities). Not app code."""
import glob, json, os, time
import numpy as np
import onnxruntime as ort
from PIL import Image
from tokenizers import Tokenizer

def snapshot(repo):
    d = os.path.expanduser(f"~/.cache/huggingface/hub/models--{repo.replace('/', '--')}/snapshots/*")
    return sorted(glob.glob(d))[-1]

class TrOCR:
    def __init__(self, repo="Xenova/trocr-small-handwritten", quantized=False, threads=2):
        d = snapshot(repo)
        q = "_quantized" if quantized else ""
        so = ort.SessionOptions()
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        t = time.perf_counter()
        self.enc = ort.InferenceSession(f"{d}/onnx/encoder_model{q}.onnx", so, providers=["CPUExecutionProvider"])
        self.dec = ort.InferenceSession(f"{d}/onnx/decoder_model_merged{q}.onnx", so, providers=["CPUExecutionProvider"])
        self.load_s = time.perf_counter() - t
        self.tok = Tokenizer.from_file(f"{d}/tokenizer.json")
        g = json.load(open(f"{d}/generation_config.json"))
        self.start, self.eos = g["decoder_start_token_id"], g["eos_token_id"]
        self.size = json.load(open(f"{d}/preprocessor_config.json"))["size"]["height"]
        self.past_names = [i.name for i in self.dec.get_inputs() if i.name.startswith("past_key_values")]
        self.bytes = sum(os.path.getsize(f"{d}/onnx/{n}{q}.onnx") for n in ("encoder_model", "decoder_model_merged"))

    def pixels(self, img):
        img = img.convert("RGB").resize((self.size, self.size), Image.BICUBIC)
        a = np.asarray(img, dtype=np.float32) / 255.0
        a = (a - 0.5) / 0.5
        return a.transpose(2, 0, 1)[None]

    def encode(self, img):
        return self.enc.run(None, {"pixel_values": self.pixels(img)})[0]

    def _step(self, ids, enc, past):
        feed = {"input_ids": ids, "encoder_hidden_states": enc}
        B = ids.shape[0]
        if past is None:
            for n in self.past_names:
                shp = [B, 8 if "small" in n or True else 0, 0, 0]
                i = [x for x in self.dec.get_inputs() if x.name == n][0]
                heads, dim = i.shape[1], i.shape[3]
                feed[n] = np.zeros((B, heads, 0, dim), np.float32)
            feed["use_cache_branch"] = np.array([False])
        else:
            feed.update(past)
            feed["use_cache_branch"] = np.array([True])
        outs = self.dec.run(None, feed)
        names = [o.name for o in self.dec.get_outputs()]
        logits = outs[0][:, -1, :]
        newpast = {n.replace("present", "past_key_values"): v for n, v in zip(names[1:], outs[1:])}
        if past is not None:  # encoder cross-attention cache stays as it was (merged model returns empty)
            for k in newpast:
                if ".encoder." in k:
                    newpast[k] = past[k]
        return logits, newpast

    def greedy(self, img, max_len=48):
        enc = self.encode(img)
        ids = [self.start]
        past = None
        cur = np.array([[self.start]], np.int64)
        for _ in range(max_len):
            logits, past = self._step(cur, enc, past)
            nxt = int(logits[0].argmax())
            if nxt == self.eos:
                break
            ids.append(nxt)
            cur = np.array([[nxt]], np.int64)
        return self.tok.decode(ids[1:], skip_special_tokens=True).strip()

    def beam(self, img, k=5, max_len=48):
        """Top-k transcriptions with total log-probability (length-normalised for ranking)."""
        enc = self.encode(img)
        encB = np.repeat(enc, k, axis=0)
        beams = [([self.start], 0.0)]
        done = []
        past = None
        cur = np.array([[self.start]] * k, np.int64)
        for step in range(max_len):
            logits, past = self._step(cur, encB, past)
            lp = logits - logits.max(-1, keepdims=True)
            lp = lp - np.log(np.exp(lp).sum(-1, keepdims=True))
            cand = []
            for b, (seq, s) in enumerate(beams):
                top = np.argpartition(-lp[b], k)[:k]
                for t in top:
                    cand.append((s + float(lp[b, t]), b, int(t)))
            cand.sort(reverse=True)
            nb, rows = [], []
            for s, b, t in cand:
                seq = beams[b][0] + [t]
                if t == self.eos:
                    done.append((seq, s))
                else:
                    nb.append((seq, s)); rows.append(b)
                if len(nb) == k:
                    break
            if len(done) >= k or not nb:
                break
            while len(nb) < k:  # keep batch shape
                nb.append(nb[-1]); rows.append(rows[-1])
            beams = nb
            idx = np.array(rows)
            past = {n: (v[idx] if ".decoder." in n else v) for n, v in past.items()}
            cur = np.array([[seq[-1]] for seq, _ in beams], np.int64)
        done += beams
        out, seen = [], set()
        for seq, s in sorted(done, key=lambda x: -x[1] / len(x[0])):
            txt = self.tok.decode([t for t in seq[1:] if t != self.eos], skip_special_tokens=True).strip()
            if txt not in seen:
                seen.add(txt); out.append((txt, s))
        return out[:k]
