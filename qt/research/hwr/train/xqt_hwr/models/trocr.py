"""TrOCR (a vision encoder-decoder, Hugging Face `VisionEncoderDecoderModel`): building, the tokenizer check, freezing,
LoRA, and an explicit key/value-cache decoder (`CachedDecoder`) used both for beam search in PyTorch and for the ONNX
export the app runs (FORMATS.md: encoder + merged decoder with past key values, as the Xenova export).

Base models (config `model.base`):
- `microsoft/trocr-small-handwritten` (default): fine-tuned on IAM by Microsoft (MIT). Best handwriting features.
- `microsoft/trocr-small-stage1`: pre-trained on printed and synthetic text only, before IAM.
- `tiny`: a randomly initialised miniature (tests; no download), with a tokenizer trained on the given texts.

Targets are `<s> tokens </s>` (as Microsoft's models were trained); the decoder starts from `decoder_start_token_id`
(</s>, id 2) and the app strips special tokens when it turns tokens into text.
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

import torch
from torch import nn

GERMAN_CHECK = ["ä", "ö", "ü", "Ä", "Ö", "Ü", "ß", "„", "“", "”", "‚", "‘", "’", "–", "€", "§", "°", "µ",
                "Größe über Maß", "„Zitat“", "Straße, Übung, Ärger, Öl"]


@dataclass
class TrocrBundle:
    model: nn.Module            # VisionEncoderDecoderModel (maybe wrapped by peft)
    tokenizer: object           # PreTrainedTokenizerFast
    image_size: int


# --- building ------------------------------------------------------------------------------------------------------


def tiny_tokenizer(texts: list[str], vocab_size: int = 300):
    """A small Unigram tokenizer (SentencePiece-like, as TrOCR-small's) with XLM-R's special ids:
    <s>=0, <pad>=1, </s>=2, <unk>=3."""
    from tokenizers import Tokenizer, decoders, models, normalizers, pre_tokenizers, processors, trainers
    from transformers import PreTrainedTokenizerFast

    tok = Tokenizer(models.Unigram())
    tok.normalizer = normalizers.NFC()
    tok.pre_tokenizer = pre_tokenizers.Metaspace()
    tok.decoder = decoders.Metaspace()
    trainer = trainers.UnigramTrainer(vocab_size=vocab_size, special_tokens=["<s>", "<pad>", "</s>", "<unk>"],
                                      unk_token="<unk>",
                                      initial_alphabet=sorted({c for s in GERMAN_CHECK for c in s if c != " "}))
    tok.train_from_iterator(texts, trainer)
    tok.post_processor = processors.TemplateProcessing(single="<s> $A </s>", special_tokens=[("<s>", 0), ("</s>", 2)])
    return PreTrainedTokenizerFast(tokenizer_object=tok, bos_token="<s>", eos_token="</s>", pad_token="<pad>",
                                   unk_token="<unk>")


def build_tiny(texts: list[str], image_size: int = 64, vocab_size: int = 300,
               learned_positions: bool = True) -> TrocrBundle:
    from transformers import (DeiTConfig, DeiTModel, TrOCRConfig, TrOCRForCausalLM, VisionEncoderDecoderConfig,
                              VisionEncoderDecoderModel)

    tok = tiny_tokenizer(texts, vocab_size)
    enc_cfg = DeiTConfig(image_size=image_size, patch_size=16, hidden_size=32, num_hidden_layers=2,
                         num_attention_heads=2, intermediate_size=64)
    dec_cfg = TrOCRConfig(vocab_size=len(tok), d_model=32, decoder_layers=2, decoder_attention_heads=2,
                          decoder_ffn_dim=64, cross_attention_hidden_size=32, max_position_embeddings=128,
                          layernorm_embedding=True, scale_embedding=True, use_learned_position_embeddings=learned_positions,
                          activation_function="relu", pad_token_id=1, bos_token_id=0, eos_token_id=2,
                          decoder_start_token_id=2)
    cfg = VisionEncoderDecoderConfig.from_encoder_decoder_configs(enc_cfg, dec_cfg)
    model = VisionEncoderDecoderModel(config=cfg, encoder=DeiTModel(enc_cfg, add_pooling_layer=False),
                                      decoder=TrOCRForCausalLM(dec_cfg))
    _set_ids(model, tok)
    return TrocrBundle(model, tok, image_size)


def _set_ids(model, tok):
    model.config.decoder_start_token_id = 2
    model.config.pad_token_id = tok.pad_token_id
    model.config.eos_token_id = tok.eos_token_id
    model.config.decoder.decoder_start_token_id = 2
    if getattr(model, "generation_config", None) is not None:
        model.generation_config.decoder_start_token_id = 2
        model.generation_config.eos_token_id = tok.eos_token_id
        model.generation_config.pad_token_id = tok.pad_token_id


def load(base: str, texts: list[str] | None = None, tiny: dict | None = None, attn: str = "eager") -> TrocrBundle:
    """A base model from the hub or a folder (save_pretrained: the model and its tokenizer), or `tiny`. `attn`: the
    encoder's attention ("sdpa" trains faster; the export and CachedDecoder do not depend on it)."""
    if base == "tiny":
        return build_tiny(texts or ["hello world"], **(tiny or {}))
    from transformers import AutoTokenizer, VisionEncoderDecoderModel

    try:
        model = VisionEncoderDecoderModel.from_pretrained(base, attn_implementation=attn)
    except (ValueError, ImportError):
        model = VisionEncoderDecoderModel.from_pretrained(base, attn_implementation="eager")
    if getattr(model.encoder, "pooler", None) is not None:
        model.encoder.pooler = None  # (unused by the decoder; DDP refuses parameters without gradients)
    model.decoder.model.decoder.layerdrop = 0.0  # (a skipped layer has no gradients either)
    tok = AutoTokenizer.from_pretrained(base, use_fast=True)
    if model.config.decoder_start_token_id is None:
        model.config.decoder_start_token_id = 2
    size = model.config.encoder.image_size
    size = size if isinstance(size, int) else size[0]
    return TrocrBundle(model, tok, int(size))


# --- the tokenizer's German characters -----------------------------------------------------------------------------


def app_decode(tokenizer_json: dict, ids: list[int]) -> str:
    """Tokens to text as the app does it (qt/src/hwr/BeamSearch.cpp, Tokenizer::decode): pieces by id, special added
    tokens left out, "▁" to spaces (or GPT-2 byte-level), whitespace runs collapsed."""
    model = tokenizer_json["model"]
    pieces: dict[int, str] = {}
    special: set[int] = set()
    if isinstance(model.get("vocab"), list):
        for i, v in enumerate(model["vocab"]):
            pieces[i] = v[0]
    elif isinstance(model.get("vocab"), dict):
        for k, i in model["vocab"].items():
            pieces[int(i)] = k
    for t in tokenizer_json.get("added_tokens", []):
        pieces[t["id"]] = t["content"]
        if t.get("special"):
            special.add(t["id"])
    dec = tokenizer_json.get("decoder") or {}
    byte_level = dec.get("type") == "ByteLevel" or (dec.get("type") == "Sequence" and "ByteLevel" in json.dumps(dec))
    s = "".join(pieces.get(i, "") for i in ids if i in pieces and i not in special)
    if byte_level:
        bs = list(range(ord("!"), ord("~") + 1)) + list(range(0xA1, 0xAD)) + list(range(0xAE, 0x100))
        cs = bs[:]
        n = 0
        for b in range(256):
            if b not in bs:
                bs.append(b)
                cs.append(256 + n)
                n += 1
        table = {chr(c): b for b, c in zip(bs, cs)}
        s = bytes(table[c] for c in s if c in table).decode("utf-8", errors="replace")
    else:
        s = s.replace("▁", " ")
    return " ".join(s.split())


def tokenizer_json(tok) -> dict:
    return json.loads(tok.backend_tokenizer.to_str())


def check_charset(tok, samples: list[str] | None = None) -> list[str]:
    """The samples (German characters by default) that do not come back unchanged through encode and the app's
    decode, or that need the unknown token."""
    tj = tokenizer_json(tok)
    bad = []
    for s in samples or GERMAN_CHECK:
        ids = tok(s, add_special_tokens=False).input_ids
        if tok.unk_token_id is not None and tok.unk_token_id in ids:
            bad.append(s)
        elif app_decode(tj, ids) != " ".join(s.split()):
            bad.append(s)
    return bad


def add_missing_chars(bundle: TrocrBundle, chars: list[str]) -> list[str]:
    """Adds single characters the tokenizer cannot write as new tokens (the decoder's embeddings grow)."""
    single = [c for c in chars if len(c) == 1]
    if not single:
        return []
    n = bundle.tokenizer.add_tokens(single)
    if n:
        bundle.model.decoder.resize_token_embeddings(len(bundle.tokenizer))
        bundle.model.config.decoder.vocab_size = len(bundle.tokenizer)
    return single


# --- training helpers ----------------------------------------------------------------------------------------------


def encode_targets(tok, texts: list[str], max_len: int) -> torch.Tensor:
    """Labels `<s> tokens </s>` padded with -100."""
    rows = []
    for t in texts:
        ids = tok(t, add_special_tokens=False).input_ids[: max_len - 2]
        rows.append([tok.bos_token_id] + ids + [tok.eos_token_id] if tok.bos_token_id is not None
                    else ids + [tok.eos_token_id])
    n = max(len(r) for r in rows)
    out = torch.full((len(rows), n), -100, dtype=torch.long)
    for i, r in enumerate(rows):
        out[i, : len(r)] = torch.tensor(r)
    return out


def freeze_encoder(model):
    for p in model.encoder.parameters():
        p.requires_grad = False


def apply_lora(model, r: int = 8, alpha: int = 16, dropout: float = 0.05, targets=None):
    from peft import LoraConfig, get_peft_model

    targets = targets or ["q_proj", "v_proj", "query", "value"]
    cfg = LoraConfig(r=r, lora_alpha=alpha, lora_dropout=dropout, target_modules=targets, bias="none")
    return get_peft_model(model, cfg)


def unwrap(model):
    """The plain VisionEncoderDecoderModel (LoRA merged in, DDP unwrapped)."""
    m = model.module if hasattr(model, "module") and not hasattr(model, "encoder") else model
    if hasattr(m, "merge_and_unload"):
        m = m.merge_and_unload()
    return m


# --- the decoder with an explicit cache ----------------------------------------------------------------------------


class CachedDecoder(nn.Module):
    """TrOCR's decoder written out step by step with its key/value cache as plain tensors (no transformers Cache
    classes), from the weights of a VisionEncoderDecoderModel. `past` is a list of 4 tensors per layer:
    self-attention key, value, cross-attention key, value, each [batch, heads, length, head_dim]."""

    def __init__(self, model):
        super().__init__()
        lm = model.decoder                       # TrOCRForCausalLM
        self.dec = lm.model.decoder              # TrOCRDecoder
        self.head = lm.output_projection
        cfg = lm.config
        self.heads = cfg.decoder_attention_heads
        self.dim = cfg.hidden_size
        self.head_dim = self.dim // self.heads
        self.layers = len(self.dec.layers)
        self.learned_pos = bool(cfg.use_learned_position_embeddings)
        self.padding_idx = cfg.pad_token_id
        et = self.dec.embed_tokens
        # transformers 5 scales inside the embedding module, 4.x in the decoder's forward
        self.extra_scale = 1.0 if hasattr(et, "embed_scale") else float(getattr(self.dec, "embed_scale", 1.0))
        self.act = self.dec.layers[0].activation_fn
        if not self.learned_pos:
            from transformers.models.trocr.modeling_trocr import TrOCRSinusoidalPositionalEmbedding as S
            self.register_buffer("sin_pos", S.get_embedding(cfg.max_position_embeddings + self.padding_idx + 2,
                                                            self.dim, self.padding_idx), persistent=False)

    def _split(self, x):  # [B, S, D] -> [B, H, S, hd]
        b, s, _ = x.shape
        return x.view(b, s, self.heads, self.head_dim).transpose(1, 2)

    def _attend(self, q, k, v, mask=None):
        w = torch.matmul(q, k.transpose(-1, -2))
        if mask is not None:
            w = w + mask
        w = torch.softmax(w, dim=-1)
        o = torch.matmul(w, v)  # [B, H, S, hd]
        b, h, s, d = o.shape
        return o.transpose(1, 2).reshape(b, s, h * d)

    def forward(self, input_ids, encoder_hidden_states, past: list | None = None):
        """Logits [B, S, V] and the new cache (4 per layer; the cross-attention's is computed from the encoder's
        states when `past` is None, else passed through)."""
        b, s = input_ids.shape
        past_len = past[0].shape[2] if past is not None else 0
        x = self.dec.embed_tokens(input_ids) * self.extra_scale
        pos = torch.arange(s, device=input_ids.device) + past_len
        if self.learned_pos:
            x = x + self.dec.embed_positions.weight[pos + 2].unsqueeze(0)
        else:
            x = x + self.sin_pos[pos + self.padding_idx + 1].unsqueeze(0)
        if self.dec.layernorm_embedding is not None:
            x = self.dec.layernorm_embedding(x)
        # causal mask over [past + new]
        total = s + past_len
        qi = torch.arange(s, device=x.device).unsqueeze(1) + past_len
        kj = torch.arange(total, device=x.device).unsqueeze(0)
        mask = torch.where(kj > qi, torch.tensor(torch.finfo(x.dtype).min, dtype=x.dtype, device=x.device),
                           torch.tensor(0.0, dtype=x.dtype, device=x.device))
        presents = []
        for i, layer in enumerate(self.dec.layers):
            sa = layer.self_attn
            q = self._split(sa.q_proj(x) * sa.scaling)
            k = self._split(sa.k_proj(x))
            v = self._split(sa.v_proj(x))
            if past is not None:
                k = torch.cat([past[4 * i], k], dim=2)
                v = torch.cat([past[4 * i + 1], v], dim=2)
            h = sa.out_proj(self._attend(q, k, v, mask))
            x = layer.self_attn_layer_norm(x + h)
            ca = layer.encoder_attn
            q = self._split(ca.q_proj(x) * ca.scaling)
            if past is None:
                ek = self._split(ca.k_proj(encoder_hidden_states))
                ev = self._split(ca.v_proj(encoder_hidden_states))
            else:
                ek, ev = past[4 * i + 2], past[4 * i + 3]
            h = ca.out_proj(self._attend(q, ek, ev))
            x = layer.encoder_attn_layer_norm(x + h)
            h = layer.fc2(self.act(layer.fc1(x)))
            x = layer.final_layer_norm(x + h)
            presents += [k, v, ek, ev]
        return self.head(x), presents


class EncoderForExport(nn.Module):
    """pixel_values [B, 3, S, S] -> the states the decoder attends to (with the projection, if the model has one)."""

    def __init__(self, model):
        super().__init__()
        self.encoder = model.encoder
        self.proj = getattr(model, "enc_to_dec_proj", None)

    def forward(self, pixel_values):
        h = self.encoder(pixel_values=pixel_values).last_hidden_state
        return self.proj(h) if self.proj is not None else h


def num_params(model) -> int:
    return sum(p.numel() for p in model.parameters())


def describe(bundle: TrocrBundle) -> str:
    m = bundle.model
    return (f"TrOCR {num_params(m) / 1e6:.1f} M parameters, image {bundle.image_size}px, "
            f"vocabulary {len(bundle.tokenizer)}")


def warn(msg: str):
    print(f"warning: {msg}", file=sys.stderr)

