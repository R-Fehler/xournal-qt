"""train.py: train a TrOCR or a CTC model (config `kind`), on 1..N GPUs or the CPU.

  python train.py --config configs/de-ctc.yaml                       one GPU (or the CPU)
  torchrun --nproc_per_node=4 train.py --config configs/de-trocr.yaml   DDP on 4 GPUs
  ... --resume                                                         go on from <out_dir>/checkpoints/last
  ... --set train.batch_size=16 --set out_dir=runs/x                   override config keys

Rank 0 logs (stdout and <out_dir>/log.jsonl) and writes the checkpoints: `last` at every evaluation (with the
optimizer's state, for --resume) and `best` when the validation's words found improve. Training stops after
`train.max_steps` or when `train.early_stopping` evaluations in a row brought no improvement.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import os
import random
import shutil
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.distributed as dist
import torch.nn.functional as F
from torch.nn.parallel import DistributedDataParallel as DDP
from torch.utils.data import DataLoader

from . import config as configlib
from . import metrics
from .data.augment import LineAugment
from .data.images import to_ctc_pixels, to_trocr_pixels
from .data.mix import DistributedWeightedSampler, Lines, licences, load_sources
from .models import ctc, trocr

DEFAULTS = {
    "seed": 1234,
    "languages": ["de"],
    "version": "0.0.1",
    "val": {"every_steps": 2000, "max_lines": 600, "topk": None},
    "train": {"batch_size": 16, "grad_accum": 1, "lr": 5e-5, "weight_decay": 0.01, "warmup_steps": 500,
              "max_steps": 20000, "steps_per_epoch": 2000, "num_workers": 4, "precision": "auto",
              "clip_grad": 1.0, "early_stopping": 6, "log_every": 50, "max_target_tokens": 64},
    "model": {},
    "augment": {},
    "finetune": {},
}


def log(msg: str, rank: int = 0):
    if rank == 0:
        print(msg, flush=True)



def auto_precision(capability, bf16_native):
    """The precision "auto" means on a GPU: bf16 where the GPU has it (Ampere or newer), fp16 with a gradient scaler
    where it has tensor cores for it (Volta, Turing), else fp32. torch's is_bf16_supported() also counts emulated
    bf16, which on a GTX 1080 Ti (Pascal) runs a matmul at 60 % of fp32's speed; fp16 there is no faster than fp32."""
    if bf16_native:
        return "bf16"
    return "fp16" if tuple(capability) >= (7, 0) else "fp32"

class Run:
    """The state of one training run (one per process)."""

    def __init__(self, cfg: dict, resume: bool = False):
        self.cfg = configlib.merge(DEFAULTS, cfg)
        self.kind = self.cfg["kind"]
        if self.kind not in ("trocr", "ctc"):
            raise SystemExit("config kind: trocr or ctc")
        self.world = int(os.environ.get("WORLD_SIZE", "1"))
        self.rank = int(os.environ.get("RANK", "0"))
        self.local = int(os.environ.get("LOCAL_RANK", "0"))
        if torch.cuda.is_available() and not self.cfg.get("cpu"):
            torch.cuda.set_device(self.local)
            self.device = torch.device("cuda", self.local)
        else:
            self.device = torch.device("cpu")
        if self.world > 1 and not dist.is_initialized():
            dist.init_process_group("nccl" if self.device.type == "cuda" else "gloo")
        self.out = Path(os.path.expanduser(self.cfg.get("out_dir", f"runs/{self.cfg.get('name', self.kind)}")))
        self.resume = resume
        seed = int(self.cfg["seed"])
        random.seed(seed + self.rank)
        np.random.seed(seed + self.rank)
        torch.manual_seed(seed)

    # --- data ---------------------------------------------------------------------------------------------------

    def setup_data(self):
        c = self.cfg
        self.sources = load_sources(c["datasets"], c.get("data_root"), seed=int(c["seed"]))
        self.train_texts = [l.text for s in self.sources for l in s.lines if l.split == "train"]
        if not self.train_texts:
            raise SystemExit("no training lines")

    def transform(self):
        if self.kind == "trocr":
            size = self.image_size
            return lambda img, text: (torch.from_numpy(to_trocr_pixels(img, size)), text)
        h = int(self.cfg["model"].get("input_height", 64))
        mw = int(self.cfg["model"].get("max_width", 2048))
        return lambda img, text: (torch.from_numpy(to_ctc_pixels(img, h, mw)), text)

    def collate(self, batch):
        items = [b[0] for b in batch]
        texts = [t for _, t in items]
        if self.kind == "trocr":
            x = torch.stack([p for p, _ in items])
            y = trocr.encode_targets(self.tokenizer, texts, int(self.cfg["train"]["max_target_tokens"]))
            return x, y
        widths = torch.tensor([p.shape[-1] for p, _ in items])
        W = int(widths.max())
        W += (-W) % 4
        x = torch.zeros(len(items), 1, items[0][0].shape[1], W)
        for i, (p, _) in enumerate(items):
            x[i, :, :, : p.shape[-1]] = p
        enc = [self.codec.encode(t) for t in texts]
        targets = torch.tensor([k for e in enc for k in e], dtype=torch.long)
        lengths = torch.tensor([len(e) for e in enc], dtype=torch.long)
        return x, (widths, targets, lengths)

    # --- model --------------------------------------------------------------------------------------------------

    def setup_model(self):
        c, m, ft = self.cfg, self.cfg["model"], self.cfg["finetune"]
        last = self.out / "checkpoints" / "last"
        from_last = self.resume and (last / "train_state.pt").exists()
        init = str(last) if from_last else (ft.get("init_from") or None)
        self.base_name = m.get("base", "tiny") if self.kind == "trocr" else "crnn"
        if self.kind == "trocr":
            bundle = trocr.load(init or m.get("base", "microsoft/trocr-small-handwritten"), texts=self.train_texts,
                                tiny=m.get("tiny"), attn=m.get("attn_implementation", "sdpa"))
            self.tokenizer, self.image_size = bundle.tokenizer, bundle.image_size
            model = bundle.model
            if not init:
                chars = sorted({ch for t in self.train_texts for ch in t if not ch.isspace()})
                bad = trocr.check_charset(self.tokenizer)
                bad_data = trocr.check_charset(self.tokenizer, chars)
                if bad or bad_data:
                    log(f"tokenizer: these do not round-trip: {bad} {bad_data}", self.rank)
                    if m.get("add_missing_chars", True):
                        added = trocr.add_missing_chars(bundle, sorted(set(bad_data) | {b for b in bad if len(b) == 1}))
                        log(f"tokenizer: added {added}", self.rank)
                else:
                    log("tokenizer: German characters and every character of the data round-trip", self.rank)
            if m.get("gradient_checkpointing"):
                model.config.use_cache = False
                model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant": False})
            freeze = ft.get("freeze", "none")
            if freeze in ("encoder", "lora"):
                trocr.freeze_encoder(model)
            if freeze == "lora":
                lc = ft.get("lora", {})
                model = trocr.apply_lora(model, int(lc.get("r", 8)), int(lc.get("alpha", 16)),
                                         float(lc.get("dropout", 0.05)), lc.get("targets"))
            log(trocr.describe(bundle), self.rank)
        else:
            if init:
                model, self.alphabet, _ = ctc.load(Path(init) / "model.pt")
            else:
                self.alphabet = ctc.build_alphabet(self.train_texts, int(m.get("alphabet_min_count", 1)))
                model = ctc.CRNN(len(self.alphabet) + 1, ctc.CtcConfig.from_dict(m.get("arch")))
            self.codec = ctc.Codec(self.alphabet)
            unknown = set().union(*(self.codec.unknown(t) for t in self.train_texts))
            if unknown:
                log(f"note: characters outside the alphabet are left out of the targets: {sorted(unknown)}", self.rank)
            if ft.get("freeze") == "last_layers":
                ctc.freeze_but_last(model, int(ft.get("last_lstm_layers", 1)))
            log(f"CRNN {sum(p.numel() for p in model.parameters()) / 1e6:.1f} M parameters, "
                f"{len(self.alphabet)} characters", self.rank)
        self.model = model.to(self.device)
        trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
        log(f"trainable parameters: {trainable / 1e6:.2f} M", self.rank)
        self.state = {"step": 0, "epoch": 0, "best": -1.0, "bad_evals": 0}
        self.from_last = from_last

    def train_mode(self):
        self.model.train()
        if self.kind == "ctc" and self.cfg["finetune"].get("freeze") == "last_layers":
            self.model.cnn.eval()  # (frozen batch norms keep their statistics)

    def plain_model(self):
        """The model as the export and the evaluation want it (no DDP, LoRA kept as is)."""
        m = self.model
        if hasattr(m, "get_base_model"):
            return m.get_base_model()
        return m

    # --- optimisation -------------------------------------------------------------------------------------------

    def setup_optim(self):
        t = self.cfg["train"]
        params = [p for p in self.model.parameters() if p.requires_grad]
        decay = [p for p in params if p.ndim >= 2]
        no_decay = [p for p in params if p.ndim < 2]
        self.opt = torch.optim.AdamW([{"params": decay, "weight_decay": float(t["weight_decay"])},
                                      {"params": no_decay, "weight_decay": 0.0}], lr=float(t["lr"]))
        warm, total = int(t["warmup_steps"]), int(t["max_steps"])
        floor = float(t.get("min_lr_ratio", 0.05))

        def lr_at(step):
            if step < warm:
                return (step + 1) / max(1, warm)
            p = min(1.0, (step - warm) / max(1, total - warm))
            return floor + (1 - floor) * 0.5 * (1 + math.cos(math.pi * p))

        self.sched = torch.optim.lr_scheduler.LambdaLR(self.opt, lr_at)
        prec = t["precision"]
        if self.device.type != "cuda":
            prec = "fp32" if prec == "auto" else prec
        elif prec == "auto":
            prec = auto_precision(torch.cuda.get_device_capability(self.device),
                                  torch.cuda.is_bf16_supported(including_emulation=False))
        self.precision = prec
        self.amp_dtype = {"bf16": torch.bfloat16, "fp16": torch.float16}.get(prec)
        self.scaler = torch.amp.GradScaler(self.device.type, enabled=(prec == "fp16"))
        log(f"precision {prec}, {self.world} process(es) on {self.device.type}", self.rank)
        if self.from_last:
            st = torch.load(self.out / "checkpoints/last/train_state.pt", map_location=self.device,
                            weights_only=False)
            self.model.load_state_dict(st["model"])
            self.opt.load_state_dict(st["optimizer"])
            self.sched.load_state_dict(st["scheduler"])
            if st.get("scaler"):
                self.scaler.load_state_dict(st["scaler"])
            self.state = st["state"]
            log(f"resumed at step {self.state['step']}", self.rank)
        if self.world > 1:
            self.ddp = DDP(self.model, device_ids=[self.local] if self.device.type == "cuda" else None)
        else:
            self.ddp = self.model

    def loss(self, x, y):
        if self.kind == "trocr":
            return self.ddp(pixel_values=x, labels=y).loss
        widths, targets, lengths = y
        logp = self.ddp(x, widths)
        frames = torch.clamp(widths // 4, min=1, max=logp.shape[0])
        return F.ctc_loss(logp.float(), targets, frames, lengths, blank=0, zero_infinity=True)

    # --- validation ---------------------------------------------------------------------------------------------

    def recognizer(self):
        from .recognizers import TorchCtc, TorchTrocr

        k = self.cfg["val"].get("topk")
        if self.kind == "trocr":
            return TorchTrocr(self.plain_model(), self.tokenizer, self.image_size, k or 4, device=self.device)
        return TorchCtc(self.plain_model(), self.alphabet, k or 5, device=self.device,
                        height=int(self.cfg["model"].get("input_height", 64)),
                        max_width=int(self.cfg["model"].get("max_width", 2048)))

    @torch.no_grad()
    def validate(self) -> dict:
        val = self.val
        rec = self.recognizer()
        texts = [l.text for l, _ in val.items]
        rnd = random.Random(self.rank)
        keys = metrics.COUNTS
        groups = sorted({s.info.name for _, s in val.items})
        tot = torch.zeros(len(groups) + 1, len(keys), dtype=torch.float64)
        for i in range(self.rank, len(val), self.world):
            line, src = val.items[i]
            readings = rec.read(val.picture(i, train=False))
            c = metrics.score_line(line.text, readings, texts, rnd)
            row = torch.tensor([c[k] for k in keys], dtype=torch.float64)
            tot[0] += row
            tot[1 + groups.index(src.info.name)] += row
        if self.world > 1:
            t = tot.to(self.device) if self.device.type == "cuda" else tot
            dist.all_reduce(t)
            tot = t.cpu()
        self.train_mode()
        out = metrics.rates(dict(zip(keys, tot[0].tolist())))
        out["per_dataset"] = {g: metrics.rates(dict(zip(keys, tot[1 + j].tolist()))) for j, g in enumerate(groups)}
        return out

    # --- checkpoints --------------------------------------------------------------------------------------------

    def meta(self, val: dict | None) -> dict:
        c = self.cfg
        m = {"kind": self.kind, "name": c.get("name", self.kind), "languages": c["languages"],
             "version": str(c["version"]), "base": self.base_name if not c["finetune"].get("init_from")
             else c["finetune"]["init_from"], "trained_on": [s.info.name for s in self.sources],
             "datasets": licences(self.sources), "step": self.state["step"], "val": val,
             "config": {k: v for k, v in c.items() if not k.startswith("_")}}
        if self.kind == "trocr":
            m["image_size"] = self.image_size
        else:
            m["input_height"] = int(c["model"].get("input_height", 64))
            m["max_width"] = int(c["model"].get("max_width", 2048))
        return m

    def save(self, name: str, val: dict | None, with_state: bool):
        if self.rank != 0:
            return
        final = self.out / "checkpoints" / name
        tmp = final.with_name(name + ".tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir(parents=True)
        if self.kind == "trocr":
            m = self.model
            plain = copy.deepcopy(m).merge_and_unload() if hasattr(m, "merge_and_unload") else m
            plain.save_pretrained(tmp)
            self.tokenizer.save_pretrained(tmp)
        else:
            ctc.save(tmp / "model.pt", self.model, self.alphabet)
        (tmp / "xqt.json").write_text(json.dumps(self.meta(val), indent=2, ensure_ascii=False), encoding="utf-8")
        if with_state:
            torch.save({"model": self.model.state_dict(), "optimizer": self.opt.state_dict(),
                        "scheduler": self.sched.state_dict(),
                        "scaler": self.scaler.state_dict() if self.scaler.is_enabled() else None,
                        "state": self.state}, tmp / "train_state.pt")
        old = final.with_name(name + ".old")
        if final.exists():
            final.rename(old)
        tmp.rename(final)
        shutil.rmtree(old, ignore_errors=True)

    # --- the loop -----------------------------------------------------------------------------------------------

    def run(self):
        c, t = self.cfg, self.cfg["train"]
        self.setup_data()
        self.setup_model()
        self.setup_optim()
        if self.rank == 0:
            self.out.mkdir(parents=True, exist_ok=True)
            (self.out / "config.yaml").write_text(
                __import__("yaml").safe_dump({k: v for k, v in c.items() if not k.startswith("_")},
                                             allow_unicode=True, sort_keys=False), encoding="utf-8")
        tf = self.transform()
        train = Lines(self.sources, "train", tf, augment=LineAugment(c["augment"]))
        vsrc = [s for s in self.sources if not c["val"].get("datasets") or s.info.name in c["val"]["datasets"]]
        self.val = Lines(vsrc, "val", tf, max_lines=int(c["val"]["max_lines"]), seed=int(c["seed"]))
        if len(self.val) == 0:
            raise SystemExit("no validation lines (each dataset needs writers in its val split)")
        bs, accum = int(t["batch_size"]), int(t["grad_accum"])
        per_epoch = int(t["steps_per_epoch"]) * bs * accum * self.world
        sampler = DistributedWeightedSampler(train.weights(), per_epoch, self.rank, self.world, int(c["seed"]))
        loader = DataLoader(train, batch_size=bs, sampler=sampler, num_workers=int(t["num_workers"]),
                            collate_fn=self.collate, drop_last=True, pin_memory=self.device.type == "cuda",
                            persistent_workers=int(t["num_workers"]) > 0)
        log(f"{len(train)} training lines in {len(self.sources)} datasets, {len(self.val)} validation lines; "
            f"batch {bs} x {accum} x {self.world} = {bs * accum * self.world} lines per step", self.rank)
        st = self.state
        max_steps, every = int(t["max_steps"]), int(c["val"]["every_steps"])
        patience = int(t["early_stopping"])
        logf = open(self.out / "log.jsonl", "a", encoding="utf-8") if self.rank == 0 else None
        self.train_mode()
        stop = st["step"] >= max_steps
        t0, seen, running = time.time(), 0, 0.0
        while not stop:
            sampler.set_epoch(st["epoch"])
            micro = 0
            for x, y in loader:
                x = x.to(self.device, non_blocking=True)
                y = y.to(self.device) if torch.is_tensor(y) else tuple(v.to(self.device) for v in y)
                sync = (micro + 1) % accum == 0
                ctxt = self.ddp.no_sync() if (self.world > 1 and not sync) else _null()
                with ctxt:
                    with torch.autocast(self.device.type, dtype=self.amp_dtype or torch.float32,
                                        enabled=self.amp_dtype is not None):
                        loss = self.loss(x, y) / accum
                    self.scaler.scale(loss).backward()
                running += float(loss.detach()) * accum
                seen += x.shape[0]
                micro += 1
                if not sync:
                    continue
                self.scaler.unscale_(self.opt)
                if t["clip_grad"]:
                    torch.nn.utils.clip_grad_norm_([p for p in self.model.parameters() if p.requires_grad],
                                                   float(t["clip_grad"]))
                self.scaler.step(self.opt)
                self.scaler.update()
                self.opt.zero_grad(set_to_none=True)
                self.sched.step()
                st["step"] += 1
                if st["step"] % int(t["log_every"]) == 0 or st["step"] == 1:
                    dt = time.time() - t0
                    rec = {"step": st["step"], "loss": round(running / max(1, micro), 4),
                           "lr": self.sched.get_last_lr()[0], "lines_per_s": round(seen * self.world / dt, 1)}
                    log(json.dumps(rec), self.rank)
                    if logf:
                        logf.write(json.dumps(rec) + "\n")
                        logf.flush()
                    t0, seen, running, micro = time.time(), 0, 0.0, 0
                if st["step"] % every == 0 or st["step"] >= max_steps:
                    v = self.validate()
                    score = v["words_found"] or 0.0
                    better = score > st["best"]
                    if better:
                        st["best"], st["bad_evals"] = score, 0
                    else:
                        st["bad_evals"] += 1
                    rec = {"step": st["step"], "val": v, "best": st["best"]}
                    log(json.dumps(rec, ensure_ascii=False), self.rank)
                    if logf:
                        logf.write(json.dumps(rec, ensure_ascii=False) + "\n")
                        logf.flush()
                    if better:
                        self.save("best", v, with_state=False)
                    self.save("last", v, with_state=True)
                    if self.world > 1:
                        dist.barrier()
                    if st["bad_evals"] >= patience:
                        log(f"early stop: {patience} evaluations without improvement", self.rank)
                        stop = True
                    t0, seen = time.time(), 0
                if st["step"] >= max_steps:
                    stop = True
                if stop:
                    break
            st["epoch"] += 1
        if logf:
            logf.close()
        log(f"done at step {st['step']}; best words found {st['best']:.4f}; "
            f"checkpoints in {self.out / 'checkpoints'}", self.rank)
        if self.world > 1:
            dist.barrier()
            dist.destroy_process_group()


class _null:
    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def main(argv=None):
    ap = argparse.ArgumentParser(prog="train.py", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--config", required=True)
    ap.add_argument("--set", action="append", default=[], help="override a config key: a.b=value")
    ap.add_argument("--resume", action="store_true")
    args = ap.parse_args(argv)
    cfg = configlib.load(args.config, args.set)
    Run(cfg, resume=args.resume).run()


if __name__ == "__main__":
    sys.exit(main())
