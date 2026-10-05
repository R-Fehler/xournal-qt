"""The CTC line model: a CNN over the line picture (64 px high) and a bidirectional LSTM over its columns, trained with
CTC (FORMATS.md, kind "ctc"). About 5.7 M parameters with the defaults.

Input [B, 1, 64, W] (ink 1, paper 0), output log-probabilities [T, B, C] with T = W / 4 and C = alphabet + 1 (class 0
is the blank). The alphabet is built from the training texts (NFC), plus a base set of German and English characters
so that a rare letter is never missing.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass, field

import torch
from torch import nn

BASE_CHARS = (" !\"#%&'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_abcdefghijklmnopqrstuvwxyz"
              "ÄÖÜäöüß„“”‚‘’–€§°")


@dataclass
class CtcConfig:
    height: int = 64
    channels: list = field(default_factory=lambda: [32, 64, 128, 128, 256, 256])
    proj: int = 256
    lstm_hidden: int = 256
    lstm_layers: int = 3
    dropout: float = 0.2

    @staticmethod
    def from_dict(d: dict | None) -> "CtcConfig":
        return CtcConfig(**(d or {}))


def build_alphabet(texts, min_count: int = 1, base: str = BASE_CHARS) -> list[str]:
    counts = Counter(c for t in texts for c in t)
    chars = set(base) | {c for c, n in counts.items() if n >= min_count and c.isprintable()}
    chars.discard("\n")
    return sorted(chars)


class Codec:
    """Text <-> class ids (class i + 1 is alphabet[i]; 0 is the blank)."""

    def __init__(self, alphabet: list[str]):
        self.alphabet = list(alphabet)
        self.index = {c: i + 1 for i, c in enumerate(self.alphabet)}

    @property
    def classes(self) -> int:
        return len(self.alphabet) + 1

    def encode(self, text: str) -> list[int]:
        return [self.index[c] for c in text if c in self.index]

    def unknown(self, text: str) -> set[str]:
        return {c for c in text if c not in self.index}

    def decode(self, ids) -> str:
        return "".join(self.alphabet[i - 1] for i in ids if 0 < i <= len(self.alphabet))

    def greedy(self, frames) -> str:
        """Best path: argmax per frame, repeats merged, blanks dropped."""
        out, prev = [], 0
        for k in frames:
            k = int(k)
            if k != prev and k != 0:
                out.append(k)
            prev = k
        return self.decode(out)


def _block(cin, cout):
    return nn.Sequential(nn.Conv2d(cin, cout, 3, padding=1, bias=False), nn.BatchNorm2d(cout), nn.ReLU(inplace=True))


class CRNN(nn.Module):
    def __init__(self, classes: int, cfg: CtcConfig | None = None):
        super().__init__()
        cfg = cfg or CtcConfig()
        self.cfg = cfg
        c = cfg.channels
        assert len(c) == 6, "six convolution widths"
        self.cnn = nn.Sequential(
            _block(1, c[0]), nn.MaxPool2d(2, 2),               # H/2, W/2
            _block(c[0], c[1]), nn.MaxPool2d(2, 2),            # H/4, W/4
            _block(c[1], c[2]), _block(c[2], c[3]), nn.MaxPool2d((2, 1), (2, 1)),   # H/8
            _block(c[3], c[4]), _block(c[4], c[5]), nn.MaxPool2d((2, 1), (2, 1)),   # H/16
        )
        rows = cfg.height // 16
        self.proj = nn.Sequential(nn.Linear(c[5] * rows, cfg.proj), nn.ReLU(inplace=True), nn.Dropout(cfg.dropout))
        self.lstm = nn.LSTM(cfg.proj, cfg.lstm_hidden, num_layers=cfg.lstm_layers, bidirectional=True,
                            dropout=cfg.dropout if cfg.lstm_layers > 1 else 0.0)
        self.head = nn.Sequential(nn.Dropout(cfg.dropout), nn.Linear(2 * cfg.lstm_hidden, classes))

    @staticmethod
    def frames(width: int) -> int:
        return width // 4

    def forward(self, x: torch.Tensor, widths: torch.Tensor | None = None) -> torch.Tensor:
        """x [B, 1, H, W] -> log-probabilities [T, B, C]. `widths` (training, padded batches): each line's own width,
        so that the LSTM does not read the padding (packed sequences)."""
        f = self.cnn(x)                                   # [B, C, H/16, W/4]
        b, c, h, t = f.shape
        f = f.permute(3, 0, 1, 2).reshape(t, b, c * h)    # [T, B, C*H']
        f = self.proj(f)
        if widths is not None:
            lengths = torch.clamp(widths // 4, min=1, max=t).cpu()
            packed = nn.utils.rnn.pack_padded_sequence(f, lengths, enforce_sorted=False)
            out, _ = self.lstm(packed)
            f, _ = nn.utils.rnn.pad_packed_sequence(out, total_length=t)
        else:
            f, _ = self.lstm(f)
        return torch.log_softmax(self.head(f).float(), dim=-1)


def save(path, model: CRNN, alphabet: list[str], extra: dict | None = None):
    torch.save({"kind": "ctc", "config": asdict(model.cfg), "alphabet": alphabet, "state_dict": model.state_dict(),
                **(extra or {})}, path)


def load(path, map_location="cpu") -> tuple[CRNN, list[str], dict]:
    d = torch.load(path, map_location=map_location, weights_only=False)
    model = CRNN(len(d["alphabet"]) + 1, CtcConfig.from_dict(d["config"]))
    model.load_state_dict(d["state_dict"])
    return model, d["alphabet"], d


def freeze_but_last(model: CRNN, last_lstm_layers: int = 1):
    """Fine-tuning: only the head and the last LSTM layers learn."""
    for p in model.parameters():
        p.requires_grad = False
    for p in model.head.parameters():
        p.requires_grad = True
    n = model.cfg.lstm_layers
    for name, p in model.lstm.named_parameters():
        layer = int(name.split("_l")[1].split("_")[0])
        if layer >= n - last_lstm_layers:
            p.requires_grad = True
