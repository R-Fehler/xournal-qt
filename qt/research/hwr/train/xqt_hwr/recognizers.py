"""One interface for every model the evaluation reads with: `read(picture) -> [(text, log-probability)]`, best first.

- a training checkpoint (a folder with xqt.json: PyTorch, TrOCR or CTC),
- an exported model folder (model.json, FORMATS.md section 2: ONNX Runtime, decoded as the app decodes),
- the app's current English model (the Xenova TrOCR-small export with its model.json, kind missing = trocr).

The picture is a framed line (data.images.frame_line): grey, ink dark on white.
"""
from __future__ import annotations

import json
import os
from pathlib import Path

import numpy as np
import torch
from PIL import Image

from .data.images import to_ctc_pixels, to_trocr_pixels
from .decode import beam_search, ctc_beams

TROCR_BEAMS = 4         # TrocrRecognizer::BEAMS
TROCR_MAX_TOKENS = 48   # TrocrRecognizer::MAX_TOKENS
CTC_TOPK = 5


class Recognizer:
    kind = ""
    name = ""
    languages: list[str] = []

    def read(self, img: Image.Image) -> list[tuple[str, float]]:
        raise NotImplementedError


# --- PyTorch ---------------------------------------------------------------------------------------------------------


class TorchTrocr(Recognizer):
    kind = "trocr"

    def __init__(self, model, tokenizer, image_size: int, k: int = TROCR_BEAMS, max_tokens: int = TROCR_MAX_TOKENS,
                 device="cpu", name="trocr"):
        from .models.trocr import CachedDecoder, EncoderForExport, tokenizer_json

        self.model = model.eval().to(device)
        self.enc = EncoderForExport(self.model)
        self.dec = CachedDecoder(self.model)
        self.tj = tokenizer_json(tokenizer)
        self.size, self.k, self.max_tokens, self.device, self.name = image_size, k, max_tokens, device, name
        self.start = int(self.model.config.decoder_start_token_id)
        self.end = int(tokenizer.eos_token_id)

    @torch.no_grad()
    def read_pixels(self, pixels: np.ndarray) -> list[tuple[list[int], float, float]]:
        x = torch.from_numpy(pixels[None]).to(self.device)
        states = self.enc(x).repeat(self.k, 1, 1)
        past = None

        def step(tokens, parents):
            nonlocal past
            if parents and past is not None:
                idx = torch.tensor(parents, device=self.device)
                past = [p.index_select(0, idx) if j % 4 < 2 else p for j, p in enumerate(past)]
            ids = torch.tensor(tokens, device=self.device).view(-1, 1)
            logits, past = self.dec(ids, states, past)
            return logits[:, -1].float().cpu().numpy()

        return beam_search(step, self.start, self.end, self.k, self.max_tokens)

    def read(self, img):
        from .models.trocr import app_decode

        out = []
        for tokens, lp, _ in self.read_pixels(to_trocr_pixels(img, self.size)):
            out.append((app_decode(self.tj, [t for t in tokens if t != self.end]), lp))
        return out


class TorchCtc(Recognizer):
    kind = "ctc"

    def __init__(self, model, alphabet, k: int = CTC_TOPK, device="cpu", height: int = 64, max_width: int = 2048,
                 name="ctc"):
        from .models.ctc import Codec

        self.model = model.eval().to(device)
        self.codec = Codec(alphabet)
        self.k, self.device, self.height, self.max_width, self.name = k, device, height, max_width, name

    @torch.no_grad()
    def logits(self, img) -> np.ndarray:
        x = torch.from_numpy(to_ctc_pixels(img, self.height, self.max_width)[None]).to(self.device)
        return self.model(x)[:, 0].float().cpu().numpy()

    def read(self, img):
        return [(self.codec.decode(p), lp) for p, lp in ctc_beams(self.logits(img), topk=self.k)]


# --- ONNX Runtime (what ships) ---------------------------------------------------------------------------------------


def _session(path: Path, threads: int):
    import onnxruntime as ort

    so = ort.SessionOptions()
    so.intra_op_num_threads = threads
    so.inter_op_num_threads = 1
    return ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])


class OnnxTrocr(Recognizer):
    """As TrocrRecognizer.cpp runs the encoder and the merged decoder."""
    kind = "trocr"

    def __init__(self, folder: str | Path, k: int = TROCR_BEAMS, max_tokens: int = TROCR_MAX_TOKENS, threads: int = 2):
        self.folder = Path(folder)
        m = json.loads((self.folder / "model.json").read_text(encoding="utf-8"))
        self.manifest = m
        self.name = m.get("name", self.folder.name)
        self.languages = m.get("languages", ["en"])
        self.enc = _session(self.folder / m["encoder"], threads)
        self.dec = _session(self.folder / m["decoder"], threads)
        self.tj = json.loads((self.folder / m["tokenizer"]).read_text(encoding="utf-8"))
        self.start = int(m.get("decoder_start_token_id", 2))
        self.end = int(m.get("eos_token_id", 2))
        self.size = int(m.get("image_size", 384))
        self.k, self.max_tokens = k, max_tokens
        self.inputs = self.dec.get_inputs()
        self.out_names = [o.name for o in self.dec.get_outputs()]

    def read_pixels(self, pixels: np.ndarray):
        enc = self.enc.run(None, {self.enc.get_inputs()[0].name: pixels[None].astype(np.float32)})[0]
        states = np.repeat(enc, self.k, axis=0)
        past: dict[str, np.ndarray] = {}
        first = True

        def step(tokens, parents):
            nonlocal first
            rows = len(tokens)
            if parents:
                for n, t in past.items():
                    if ".encoder." not in n and t.shape[0] == rows:
                        past[n] = t[np.asarray(parents)]
            feed = {"input_ids": np.asarray(tokens, np.int64).reshape(rows, 1), "encoder_hidden_states": states}
            for i in self.inputs:
                if i.name == "use_cache_branch":
                    feed[i.name] = np.array([not first])
                elif i.name.startswith("past_key_values"):
                    if not first and i.name in past:
                        feed[i.name] = past[i.name]
                    else:
                        heads = i.shape[1] if isinstance(i.shape[1], int) else 8
                        dim = i.shape[3] if isinstance(i.shape[3], int) else 32
                        feed[i.name] = np.zeros((rows, heads, 0, dim), np.float32)
            res = self.dec.run(self.out_names, feed)
            for n, v in zip(self.out_names[1:], res[1:]):
                n = "past_key_values" + n[len("present"):] if n.startswith("present") else n
                if not first and ".encoder." in n:
                    continue
                past[n] = v
            first = False
            return res[0][:, -1, :]

        return beam_search(step, self.start, self.end, self.k, self.max_tokens)

    def read(self, img):
        from .models.trocr import app_decode

        return [(app_decode(self.tj, [t for t in tokens if t != self.end]), lp)
                for tokens, lp, _ in self.read_pixels(to_trocr_pixels(img, self.size))]


class OnnxCtc(Recognizer):
    kind = "ctc"

    def __init__(self, folder: str | Path, k: int = CTC_TOPK, threads: int = 2):
        from .models.ctc import Codec

        self.folder = Path(folder)
        m = json.loads((self.folder / "model.json").read_text(encoding="utf-8"))
        self.manifest = m
        self.name = m.get("name", self.folder.name)
        self.languages = m.get("languages", [])
        self.sess = _session(self.folder / m["model"], threads)
        alphabet = read_alphabet(self.folder / m["alphabet"])
        self.codec = Codec(alphabet)
        self.blank = int(m.get("blank", 0))
        self.height = int(m.get("input_height", 64))
        self.max_width = int(m.get("max_width", 2048))
        self.k = k

    def logits(self, img) -> np.ndarray:
        x = to_ctc_pixels(img, self.height, self.max_width)[None].astype(np.float32)
        return self.sess.run(["logits"], {"image": x})[0][:, 0]

    def read(self, img):
        return [(self.codec.decode(p), lp) for p, lp in ctc_beams(self.logits(img), topk=self.k, blank=self.blank)]


def read_alphabet(path: Path) -> list[str]:
    """alphabet.txt: one character per line; a line holding only a space is the space."""
    out = []
    for line in path.read_text(encoding="utf-8").split("\n"):
        if line == "":
            continue
        out.append(line if line == " " else line.strip("\r"))
    return out


def write_alphabet(path: Path, alphabet: list[str]):
    path.write_text("".join(c + "\n" for c in alphabet), encoding="utf-8")


# --- by path ---------------------------------------------------------------------------------------------------------


def open_model(path: str | Path, device: str = "cpu", k: int | None = None, threads: int = 2) -> Recognizer:
    path = Path(os.path.expanduser(str(path)))
    if (path / "model.json").exists():
        kind = json.loads((path / "model.json").read_text(encoding="utf-8")).get("kind", "trocr")
        if kind == "ctc":
            return OnnxCtc(path, k or CTC_TOPK, threads)
        return OnnxTrocr(path, k or TROCR_BEAMS, threads=threads)
    if (path / "xqt.json").exists():
        meta = json.loads((path / "xqt.json").read_text(encoding="utf-8"))
        if meta["kind"] == "ctc":
            from .models import ctc

            model, alphabet, d = ctc.load(path / "model.pt")
            r = TorchCtc(model, alphabet, k or CTC_TOPK, device, name=meta.get("name", path.name))
        else:
            from transformers import AutoTokenizer, VisionEncoderDecoderModel

            model = VisionEncoderDecoderModel.from_pretrained(path, attn_implementation="eager")
            tok = AutoTokenizer.from_pretrained(path)
            r = TorchTrocr(model, tok, int(meta["image_size"]), k or TROCR_BEAMS, device=device,
                           name=meta.get("name", path.name))
        r.languages = meta.get("languages", [])
        return r
    raise SystemExit(f"{path}: neither a model folder (model.json) nor a checkpoint (xqt.json)")
