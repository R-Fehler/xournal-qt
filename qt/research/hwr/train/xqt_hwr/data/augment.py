"""Training augmentation of line pictures (grey, ink dark on white), in torchvision: slant, small rotation, elastic
distortion, erosion/dilation (thinner or thicker ink), contrast and brightness, blur, noise. Each is applied with its
own probability (config `augment:`); all draw from torch's random generator (seeded per DataLoader worker)."""
from __future__ import annotations

from dataclasses import dataclass, fields

import torch
from PIL import Image, ImageFilter
from torchvision.transforms import v2 as T
from torchvision.transforms.v2 import functional as F


def elastic(img: Image.Image, alpha: float, sigma: float, coarse: int = 4) -> Image.Image:
    """torchvision's ElasticTransform, with its random displacement field made at 1/`coarse` of the size and scaled up.
    The field is a blur of noise with sigma of several pixels, so it is smooth at that scale anyway; torchvision blurs
    it at full size with a kernel of 8 sigma, which took 590 ms for a 1600 px line, most of a CTC worker's time (the
    GPUs waited). This takes 6 ms, with the same strength and smoothness (the blur at 1/coarse leaves noise `coarse`
    times stronger, hence the division). Scaled as torchvision does: dx by alpha / width, dy by alpha / height."""
    w, h = img.size
    hc, wc = -(-h // coarse) + 1, -(-w // coarse) + 1
    sc = sigma / coarse
    k = int(8 * sc + 1)
    k += k % 2 == 0
    d = torch.rand(1, 2, hc, wc) * 2 - 1
    if sc > 0:
        d = F.gaussian_blur(d, [k, k], [sc, sc])
    d = torch.nn.functional.interpolate(d, size=(h, w), mode="bilinear", align_corners=False) / coarse
    d = torch.cat([d[:, 0:1] * alpha / w, d[:, 1:2] * alpha / h], 1).permute(0, 2, 3, 1)
    return F.elastic(img, d, fill=255)

@dataclass
class AugmentConfig:
    p: float = 0.9            # any augmentation at all
    slant: float = 0.5        # probability of a shear
    slant_deg: float = 15.0
    rotate: float = 0.2
    rotate_deg: float = 2.0
    elastic: float = 0.3
    elastic_alpha: float = 25.0
    elastic_sigma: float = 6.0
    morph: float = 0.3        # erosion or dilation
    contrast: float = 0.5
    blur: float = 0.2
    noise: float = 0.2
    noise_std: float = 0.06
    scale: float = 0.3        # squeeze or stretch horizontally
    scale_range: tuple = (0.8, 1.2)

    @staticmethod
    def from_dict(d: dict | None) -> "AugmentConfig":
        d = dict(d or {})
        names = {f.name for f in fields(AugmentConfig)}
        unknown = set(d) - names
        if unknown:
            raise ValueError(f"unknown augment keys: {sorted(unknown)}")
        if "scale_range" in d:
            d["scale_range"] = tuple(d["scale_range"])
        return AugmentConfig(**d)


def _u(a: float, b: float) -> float:
    return float(torch.empty(1).uniform_(a, b))


def _p(p: float) -> bool:
    return bool(torch.rand(1) < p)


class LineAugment:
    def __init__(self, cfg: AugmentConfig | dict | None = None):
        self.cfg = cfg if isinstance(cfg, AugmentConfig) else AugmentConfig.from_dict(cfg)

    def __call__(self, img: Image.Image) -> Image.Image:
        c = self.cfg
        img = img.convert("L")
        if not _p(c.p):
            return img
        if _p(c.scale):
            w, h = img.size
            img = img.resize((max(8, int(w * _u(*c.scale_range))), h), Image.BILINEAR)
        if _p(c.slant) or _p(c.rotate):
            w, h = img.size
            shear = _u(-c.slant_deg, c.slant_deg) if _p(c.slant) else 0.0
            angle = _u(-c.rotate_deg, c.rotate_deg) if _p(c.rotate) else 0.0
            # room for the sheared ink
            extra = int(abs(torch.tan(torch.tensor(shear * 3.14159 / 180))) * h) + 2
            canvas = Image.new("L", (w + 2 * extra, h), 255)
            canvas.paste(img, (extra, 0))
            img = F.affine(canvas, angle=angle, translate=[0, 0], scale=1.0, shear=[shear, 0.0], fill=255,
                           interpolation=T.InterpolationMode.BILINEAR)
        if _p(c.elastic):
            img = elastic(img, c.elastic_alpha, c.elastic_sigma)
        if _p(c.morph):
            img = img.filter(ImageFilter.MinFilter(3) if _p(0.5) else ImageFilter.MaxFilter(3))
        if _p(c.contrast):
            img = F.adjust_contrast(img, _u(0.5, 1.5))
            img = F.adjust_brightness(img, _u(0.8, 1.2))
        if _p(c.blur):
            img = img.filter(ImageFilter.GaussianBlur(_u(0.3, 1.2)))
        if _p(c.noise):
            t = F.pil_to_tensor(img).float() / 255.0
            t = (t + torch.randn_like(t) * c.noise_std).clamp(0, 1)
            img = F.to_pil_image((t * 255).to(torch.uint8))
        return img
