"""export.py: a training checkpoint -> the model folder the app loads (FORMATS.md section 2), with a parity check.

  export.py --checkpoint runs/de-trocr/checkpoints/best --out models/trocr-small-de
  export.py --checkpoint runs/de-ctc/checkpoints/best --out models/crnn-de --check-datasets fhswf-german
  export.py ... --install          also copy it to ~/.local/share/xournal-qt/models/<name>/

TrOCR: onnx/encoder_model{,_quantized}.onnx (pixel_values -> last_hidden_state) and
onnx/decoder_model_merged{,_quantized}.onnx (input_ids, encoder_hidden_states, past_key_values.*, use_cache_branch ->
logits, present.*), as the Xenova export TrocrRecognizer runs; tokenizer.json. CTC: model_int8.onnx (image [1, 1, 64,
W] -> logits [T, 1, C], log-softmax) and alphabet.txt. int8 is ONNX Runtime's dynamic quantisation (weights int8,
activations quantised at run time). model.json names the files with their sha256 and size, the languages, the licence
and the datasets the model was trained on.

The parity check runs sample lines through PyTorch and through the exported files (fp32 and int8) and compares the
logits and the readings; it fails the export if fp32 does not match.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
import onnx
import torch

from .data.images import to_ctc_pixels, to_trocr_pixels
from .onnx_merge import merge

OPSET = 17
APP_MODELS = "~/.local/share/xournal-qt/models"
KNOWN_BASES = {"microsoft/trocr-small-handwritten": "MIT (Microsoft TrOCR-small, fine-tuned by Microsoft on IAM)",
               "microsoft/trocr-small-stage1": "MIT (Microsoft TrOCR-small, pre-trained, before IAM)",
               "microsoft/trocr-small-printed": "MIT (Microsoft TrOCR-small printed)",
               "tiny": "random initialisation (tests)", "crnn": "trained from scratch"}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def base_licence(meta: dict, depth: int = 0) -> str:
    base = str(meta.get("base", ""))
    if base in KNOWN_BASES:
        return KNOWN_BASES[base]
    p = Path(os.path.expanduser(base)) / "xqt.json"
    if p.exists() and depth < 5:
        inner = json.loads(p.read_text(encoding="utf-8"))
        return base_licence(inner, depth + 1) + f", fine-tuned as {inner.get('name')}"
    return f"see {base}"


def licence_text(meta: dict) -> str:
    data = "; ".join(f"{d['name']}: {d['licence']}" for d in meta.get("datasets", []))
    nc = any(d.get("noncommercial") for d in meta.get("datasets", []))
    return (f"weights: {base_licence(meta)}; training data: {data}"
            + ("; includes data licensed for non-commercial research only" if nc else ""))


def quantize(src: Path, dst: Path, op_types=None):
    from onnxruntime.quantization import QuantType, quantize_dynamic

    quantize_dynamic(str(src), str(dst), weight_type=QuantType.QInt8, op_types_to_quantize=op_types,
                     extra_options={"MatMulConstBOnly": True})


# --- TrOCR -------------------------------------------------------------------------------------------------------------


class _NoPast(torch.nn.Module):
    def __init__(self, dec):
        super().__init__()
        self.dec = dec

    def forward(self, input_ids, encoder_hidden_states):
        logits, presents = self.dec(input_ids, encoder_hidden_states, None)
        return (logits, *presents)


class _WithPast(torch.nn.Module):
    def __init__(self, dec):
        super().__init__()
        self.dec = dec

    def forward(self, input_ids, encoder_hidden_states, *past):
        logits, presents = self.dec(input_ids, encoder_hidden_states, list(past))
        out = [logits]
        for i in range(self.dec.layers):
            out += [presents[4 * i], presents[4 * i + 1]]
        return tuple(out)


def _names(layers: int, prefix: str, which=("decoder", "encoder")):
    out = []
    for i in range(layers):
        for w in which:
            out += [f"{prefix}.{i}.{w}.key", f"{prefix}.{i}.{w}.value"]
    return out


def _cache_order(layers: int, prefix: str):
    """In CachedDecoder's order: per layer decoder key, value, encoder key, value."""
    return _names(layers, prefix)


def export_trocr(ck: Path, out: Path, quant: bool = True, keep_fp32: bool = False) -> dict:
    from transformers import AutoTokenizer, VisionEncoderDecoderModel

    from .models.trocr import CachedDecoder, EncoderForExport

    meta = json.loads((ck / "xqt.json").read_text(encoding="utf-8"))
    model = VisionEncoderDecoderModel.from_pretrained(ck, attn_implementation="eager").eval()
    tok = AutoTokenizer.from_pretrained(ck)
    size = int(meta["image_size"])
    dec = CachedDecoder(model).eval()
    H, D, L = dec.heads, dec.head_dim, dec.layers
    onnx_dir = out / "onnx"
    onnx_dir.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="xqt-export-"))
    with torch.no_grad():
        px = torch.randn(1, 3, size, size)
        enc_mod = EncoderForExport(model).eval()
        torch.onnx.export(enc_mod, (px,), onnx_dir / "encoder_model.onnx", input_names=["pixel_values"],
                          output_names=["last_hidden_state"], opset_version=OPSET, dynamo=False,
                          dynamic_axes={"pixel_values": {0: "batch_size"},
                                        "last_hidden_state": {0: "batch_size", 1: "encoder_sequence_length"}})
        states = enc_mod(px)
        ids = torch.tensor([[2], [0]])
        st2 = states.repeat(2, 1, 1)
        cache = _cache_order(L, "present")
        dyn = {"input_ids": {0: "batch_size", 1: "decoder_sequence_length"},
               "encoder_hidden_states": {0: "batch_size", 1: "encoder_sequence_length"},
               "logits": {0: "batch_size", 1: "decoder_sequence_length"}}
        for n in cache:
            dyn[n] = {0: "batch_size", 2: ("encoder_sequence_length" if ".encoder." in n
                                           else "past_decoder_sequence_length + 1")}
        torch.onnx.export(_NoPast(dec), (ids, st2), tmp / "no_past.onnx", input_names=["input_ids",
                          "encoder_hidden_states"], output_names=["logits"] + cache, opset_version=OPSET,
                          dynamic_axes=dyn, dynamo=False)
        _, presents = dec(ids, st2, None)
        past_names = _cache_order(L, "past_key_values")
        dyn2 = {"input_ids": {0: "batch_size", 1: "decoder_sequence_length"},
                "encoder_hidden_states": {0: "batch_size", 1: "encoder_sequence_length"},
                "logits": {0: "batch_size", 1: "decoder_sequence_length"}}
        for n in past_names:
            dyn2[n] = {0: "batch_size", 2: ("encoder_sequence_length" if ".encoder." in n
                                            else "past_decoder_sequence_length")}
        dec_out = _names(L, "present", ("decoder",))
        for n in dec_out:
            dyn2[n] = {0: "batch_size", 2: "past_decoder_sequence_length + 1"}
        torch.onnx.export(_WithPast(dec), (ids, st2, *presents), tmp / "with_past.onnx",
                          input_names=["input_ids", "encoder_hidden_states"] + past_names,
                          output_names=["logits"] + dec_out, opset_version=OPSET, dynamic_axes=dyn2, dynamo=False)
    np_m, wp_m = onnx.load(tmp / "no_past.onnx"), onnx.load(tmp / "with_past.onnx")
    _static_cache_dims(wp_m, H, D)
    onnx.save(merge(np_m, wp_m), onnx_dir / "decoder_model_merged.onnx")
    files = ["onnx/encoder_model.onnx", "onnx/decoder_model_merged.onnx"]
    if quant:
        quantize(onnx_dir / "encoder_model.onnx", onnx_dir / "encoder_model_quantized.onnx")
        quantize(tmp / "no_past.onnx", tmp / "no_past_q.onnx")
        quantize(tmp / "with_past.onnx", tmp / "with_past_q.onnx")
        npq, wpq = onnx.load(tmp / "no_past_q.onnx"), onnx.load(tmp / "with_past_q.onnx")
        _static_cache_dims(wpq, H, D)
        onnx.save(merge(npq, wpq), onnx_dir / "decoder_model_merged_quantized.onnx")
        files = ["onnx/encoder_model_quantized.onnx", "onnx/decoder_model_merged_quantized.onnx"]
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copy(ck / "tokenizer.json", out / "tokenizer.json")
    manifest = {"kind": "trocr", "name": meta["name"], "languages": meta["languages"], "version": meta["version"],
                "encoder": files[0], "decoder": files[1], "tokenizer": "tokenizer.json",
                "decoder_start_token_id": int(model.config.decoder_start_token_id),
                "eos_token_id": int(tok.eos_token_id), "image_size": size}
    if quant and not keep_fp32:
        for f in ("encoder_model.onnx", "decoder_model_merged.onnx"):
            (onnx_dir / f).unlink()
    return finish(out, manifest, meta, files + ["tokenizer.json"])


def _static_cache_dims(m: onnx.ModelProto, heads: int, dim: int):
    """The past inputs' heads and head size as fixed numbers (the app sizes its empty first-step caches by them)."""
    for vi in m.graph.input:
        if vi.name.startswith("past_key_values"):
            d = vi.type.tensor_type.shape.dim
            d[1].Clear()
            d[1].dim_value = heads
            d[3].Clear()
            d[3].dim_value = dim


# --- CTC ---------------------------------------------------------------------------------------------------------------


def export_ctc(ck: Path, out: Path, quant: bool = True, keep_fp32: bool = False) -> dict:
    from .models import ctc
    from .recognizers import write_alphabet

    meta = json.loads((ck / "xqt.json").read_text(encoding="utf-8"))
    model, alphabet, _ = ctc.load(ck / "model.pt")
    model.eval()
    out.mkdir(parents=True, exist_ok=True)
    h = int(meta.get("input_height", 64))
    with torch.no_grad():
        torch.onnx.export(model, (torch.rand(1, 1, h, 256),), out / "model.onnx", input_names=["image"],
                          output_names=["logits"], opset_version=OPSET, dynamo=False,
                          dynamic_axes={"image": {3: "width"}, "logits": {0: "frames"}})  # (batch 1, FORMATS.md)
    write_alphabet(out / "alphabet.txt", alphabet)
    name = "model.onnx"
    if quant:
        quantize(out / "model.onnx", out / "model_int8.onnx", op_types=["MatMul", "Gemm", "LSTM"])
        name = "model_int8.onnx"
        if not keep_fp32:
            (out / "model.onnx").unlink()
    manifest = {"kind": "ctc", "name": meta["name"], "languages": meta["languages"], "version": meta["version"],
                "model": name, "alphabet": "alphabet.txt", "blank": 0, "input_height": h,
                "max_width": int(meta.get("max_width", 2048))}
    return finish(out, manifest, meta, [name, "alphabet.txt"])


def finish(out: Path, manifest: dict, meta: dict, files: list[str]) -> dict:
    manifest["licence"] = licence_text(meta)
    manifest["trained_on"] = meta.get("trained_on", [])
    manifest["datasets"] = meta.get("datasets", [])
    if any(d.get("noncommercial") for d in manifest["datasets"]):
        manifest["noncommercial"] = True
    manifest["base_model"] = meta.get("base")
    manifest["validation"] = {k: v for k, v in (meta.get("val") or {}).items() if k != "per_dataset"}
    manifest["files"] = {f: {"sha256": sha256(out / f), "size": (out / f).stat().st_size} for f in files}
    (out / "model.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


# --- parity ------------------------------------------------------------------------------------------------------------


def sample_pictures(datasets: list[str], data_root: str | None, n: int):
    from PIL import Image

    from .data.dataset import read_dataset, resolve

    pics = []
    for name in datasets:
        _, lines = read_dataset(resolve(name, data_root))
        ls = [l for l in lines if l.split == "test"] or lines
        for l in ls[: max(1, n // max(1, len(datasets)))]:
            with Image.open(l.image) as im:
                pics.append((l.text, im.convert("L")))
    if not pics:
        from .data import fonts, synthetic
        import random
        fs = fonts.load(None, allow_system=True)
        rnd = random.Random(0)
        for t in ["Größe über Maß", "the Kalman filter", "Übung 3"][:n]:
            pics.append((t, synthetic.render_line(t, fs[0].path, rnd, synthetic.SynthConfig(size=40))))
    return pics


def parity(ck: Path, out: Path, pictures, fp32_dir: Path | None = None) -> dict:
    """PyTorch against the exported files: `shipped` (the folder's files, int8 by default) and `fp32` (when given).
    Per target: lines whose best reading is the same, and the largest difference of the logits (encoder states and
    three decoder steps for TrOCR)."""
    from .recognizers import OnnxCtc, OnnxTrocr, open_model

    torch_rec = open_model(ck)
    report = {"lines": len(pictures)}
    targets = {"shipped": out}
    if fp32_dir is not None:
        targets["fp32"] = fp32_dir
    for label, folder in targets.items():
        rec = OnnxTrocr(folder) if torch_rec.kind == "trocr" else OnnxCtc(folder)
        same, maxdiff = 0, 0.0
        for _, img in pictures:
            if torch_rec.kind == "ctc":
                a, b = torch_rec.logits(img), rec.logits(img)
                maxdiff = max(maxdiff, float(np.abs(a - b).max()))
            else:
                px = to_trocr_pixels(img, torch_rec.size)
                with torch.no_grad():
                    a = torch_rec.enc(torch.from_numpy(px[None])).numpy()
                b = rec.enc.run(None, {"pixel_values": px[None]})[0]
                maxdiff = max(maxdiff, float(np.abs(a - b).max()))
                maxdiff = max(maxdiff, _decoder_diff(torch_rec, rec, a))
            ra, rb = torch_rec.read(img), rec.read(img)
            same += bool(ra and rb and ra[0][0] == rb[0][0])
        report[label] = {"same_top1": same, "max_abs_diff": round(maxdiff, 6)}
    return report


def _decoder_diff(torch_rec, onnx_rec, states: np.ndarray) -> float:
    """Logits of three decoder steps (no cache, then with it), PyTorch against the merged ONNX decoder."""
    ids = [[torch_rec.start], [0], [5]]
    past_t, feed_past, first, diff = None, {}, True, 0.0
    st = torch.from_numpy(states)
    for step in ids:
        with torch.no_grad():
            lt, past_t = torch_rec.dec(torch.tensor([step]), st, past_t)
        feed = {"input_ids": np.array([step], np.int64), "encoder_hidden_states": states}
        for i in onnx_rec.inputs:
            if i.name == "use_cache_branch":
                feed[i.name] = np.array([not first])
            elif i.name.startswith("past_key_values"):
                feed[i.name] = feed_past.get(i.name) if not first else np.zeros((1, i.shape[1], 0, i.shape[3]),
                                                                              np.float32)
        res = onnx_rec.dec.run(onnx_rec.out_names, feed)
        diff = max(diff, float(np.abs(res[0] - lt.numpy()).max()))
        for n, v in zip(onnx_rec.out_names[1:], res[1:]):
            n = "past_key_values" + n[len("present"):]
            if first or ".encoder." not in n:
                feed_past[n] = v
        first = False
    return diff


def main(argv=None):
    ap = argparse.ArgumentParser(prog="export.py", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--checkpoint", required=True, help="a checkpoint folder (xqt.json)")
    ap.add_argument("--out", required=True, help="the model folder to write")
    ap.add_argument("--no-quantize", action="store_true", help="fp32 only")
    ap.add_argument("--keep-fp32", action="store_true", help="also keep the fp32 files (not in model.json)")
    ap.add_argument("--check-datasets", nargs="*", default=[], help="datasets for the parity check's sample lines")
    ap.add_argument("--data-root")
    ap.add_argument("--check-lines", type=int, default=8)
    ap.add_argument("--no-check", action="store_true")
    ap.add_argument("--install", action="store_true", help=f"copy the folder to {APP_MODELS}/<name>/")
    a = ap.parse_args(argv)
    from .recognizers import quiet_transformers
    quiet_transformers()
    ck, out = Path(os.path.expanduser(a.checkpoint)), Path(os.path.expanduser(a.out))
    kind = json.loads((ck / "xqt.json").read_text(encoding="utf-8"))["kind"]
    if out.exists():
        shutil.rmtree(out)
    quant = not a.no_quantize
    fn = export_trocr if kind == "trocr" else export_ctc
    manifest = fn(ck, out, quant)
    print(f"exported {manifest['name']} ({kind}) to {out}:")
    for f, e in manifest["files"].items():
        print(f"  {f}  {e['size'] / 1e6:.1f} MB  sha256 {e['sha256'][:16]}…")
    if not a.no_check:
        fp32 = None
        if quant:
            fp32 = Path(tempfile.mkdtemp(prefix="xqt-fp32-"))
            fn(ck, fp32, quant=False)
        rep = parity(ck, out, sample_pictures(a.check_datasets, a.data_root, a.check_lines), fp32)
        print("parity:", json.dumps(rep))
        if fp32:
            shutil.rmtree(fp32, ignore_errors=True)
        ref = rep.get("fp32") or rep["shipped"]
        if ref["max_abs_diff"] > 1e-3 or ref["same_top1"] != rep["lines"]:
            print("PARITY FAILED: the fp32 export does not compute what PyTorch computes", file=sys.stderr)
            sys.exit(2)
    if a.install:
        dst = Path(os.path.expanduser(APP_MODELS)) / manifest["name"]
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(out, dst)
        print(f"installed to {dst}")


if __name__ == "__main__":
    main()
