import numpy as np
import pytest
import torch

from xqt_hwr.data.corpora import builtin
from xqt_hwr.decode import beam_search, candidate_words, ctc_beams
from xqt_hwr.models import ctc, trocr

TEXTS = builtin("de").sentences + builtin("en").sentences


@pytest.fixture(scope="module")
def tiny():
    torch.manual_seed(0)
    return trocr.build_tiny(TEXTS)


def test_tiny_tokenizer_writes_german(tiny):
    assert trocr.check_charset(tiny.tokenizer) == []
    ids = tiny.tokenizer("Größe „Maß“").input_ids
    assert ids[0] == 0 and ids[-1] == 2  # <s> ... </s>, as TrOCR-small's XLM-R vocabulary


def test_missing_characters_are_added(tiny):
    b = trocr.build_tiny(TEXTS)
    assert trocr.check_charset(b.tokenizer, ["Ω"]) == ["Ω"]
    n = len(b.tokenizer)
    trocr.add_missing_chars(b, ["Ω"])
    assert len(b.tokenizer) == n + 1
    assert b.model.decoder.get_output_embeddings().weight.shape[0] == n + 1
    assert trocr.check_charset(b.tokenizer, ["Ω", "ΩΩ"]) == []


@pytest.mark.parametrize("learned", [True, False])
def test_cached_decoder_matches_transformers(learned):
    torch.manual_seed(1)
    b = trocr.build_tiny(TEXTS, learned_positions=learned)
    m = b.model.eval()
    px = torch.randn(2, 3, b.image_size, b.image_size)
    ids = torch.tensor([[2, 0, 17, 25, 40, 9], [2, 0, 33, 12, 7, 80]])
    with torch.no_grad():
        ref = m(pixel_values=px, decoder_input_ids=ids).logits
        enc = trocr.EncoderForExport(m)(px)
        dec = trocr.CachedDecoder(m)
        full, _ = dec(ids, enc)
        assert torch.allclose(full, ref, atol=1e-4)
        # step by step with the cache
        past = None
        steps = []
        for t in range(ids.shape[1]):
            lg, past = dec(ids[:, t:t + 1], enc, past)
            steps.append(lg)
        assert torch.allclose(torch.cat(steps, 1), ref, atol=1e-4)


def test_lora_and_freezing(tiny):
    b = trocr.build_tiny(TEXTS)
    trocr.freeze_encoder(b.model)
    assert not any(p.requires_grad for p in b.model.encoder.parameters())
    m = trocr.apply_lora(trocr.build_tiny(TEXTS).model, r=2, alpha=4)
    trainable = sum(p.numel() for p in m.parameters() if p.requires_grad)
    assert 0 < trainable < 0.2 * sum(p.numel() for p in m.parameters())
    plain = trocr.unwrap(m)
    assert not any("lora" in n for n, _ in plain.named_parameters())


def test_targets(tiny):
    y = trocr.encode_targets(tiny.tokenizer, ["Maß", "eine längere Zeile"], 64)
    assert y[0, 0] == 0 and (y[0] == 2).sum() == 1 and y[0, -1] == -100


def test_crnn_shapes_and_size():
    m = ctc.CRNN(120)
    n = sum(p.numel() for p in m.parameters())
    assert 5e6 < n < 15e6, n
    m.eval()
    x = torch.rand(2, 1, 64, 400)
    out = m(x)
    assert out.shape == (100, 2, 120)
    assert torch.allclose(out.exp().sum(-1), torch.ones(100, 2), atol=1e-4)
    # packed (training) and plain (export) agree for an unpadded line
    with torch.no_grad():
        a = m(x[:1])
        b = m(x[:1], torch.tensor([400]))
    assert torch.allclose(a, b, atol=1e-5)


def test_codec_and_alphabet():
    al = ctc.build_alphabet(["Größe ∑"])
    assert "ß" in al and "∑" in al and " " in al and al == sorted(al)
    c = ctc.Codec(al)
    ids = c.encode("Maß")
    assert c.decode(ids) == "Maß"
    a, s = c.index["a"], c.index["s"]
    assert c.greedy([0, a, a, 0, s, s, 0, s]) == "ass"


def test_ctc_beams_finds_the_readings():
    # frames: "ab" likely, "aa" second
    C = 4  # blank, a, b, c
    lp = np.log(np.array([
        [0.05, 0.9, 0.03, 0.02],
        [0.6, 0.1, 0.25, 0.05],
        [0.05, 0.3, 0.6, 0.05],
    ]))
    out = ctc_beams(lp, topk=3)
    assert out[0][0] == (1, 2)
    assert {p for p, _ in out} >= {(1, 2), (1,)}
    probs = [np.exp(l) for _, l in out]
    assert probs == sorted(probs, reverse=True)


def test_beam_search_port():
    # vocabulary 0..3, start 2 = end 2 (as TrOCR): the decoder prefers 1, then ends
    def step(tokens, parents):
        rows = []
        for t in tokens:
            r = np.full(4, -5.0)
            if t == 2:
                r[1], r[3] = 2.0, 1.0
            else:
                r[2], r[1] = 3.0, 0.0
            rows.append(r)
        return np.array(rows)

    out = beam_search(step, 2, 2, 3, 10)
    assert out[0][0] == [1, 2]
    assert len(out) == 3 and len({tuple(o[0]) for o in out}) == 3


def test_candidate_words_shares():
    r = [("the Kalmar filter", -1.0), ("the Kalman filter", -1.5), ("tho Kalmon fitter", -9.0)]
    w = candidate_words(r)
    assert {"kalmar", "kalman", "filter"} <= w and "kalmon" not in w
