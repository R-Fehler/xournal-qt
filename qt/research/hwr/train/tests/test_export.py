import hashlib
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
import pytest

from xqt_hwr import evaluate, export
from xqt_hwr.recognizers import OnnxCtc, OnnxTrocr, open_model, read_alphabet


@pytest.fixture(scope="module")
def exported(trained, data_root, tmp_path_factory):
    out = tmp_path_factory.mktemp("models")
    for kind in ("trocr", "ctc"):
        export.main(["--checkpoint", str(trained / kind / "checkpoints/best"), "--out", str(out / kind),
                     "--check-datasets", "synthetic-de", "synthetic-en", "--data-root", str(data_root)])
    return out


def _manifest(d):
    return json.loads((d / "model.json").read_text())


def test_trocr_folder_as_the_app_reads_it(exported):
    d = exported / "trocr"
    m = _manifest(d)
    assert m["kind"] == "trocr" and m["languages"] == ["de", "en"]
    assert m["encoder"] == "onnx/encoder_model_quantized.onnx"
    assert m["decoder"] == "onnx/decoder_model_merged_quantized.onnx"
    assert m["decoder_start_token_id"] == 2 and m["eos_token_id"] == 2 and m["image_size"] == 64
    assert m["trained_on"] == ["synthetic-de", "synthetic-en"] and "licence" in m
    for f, e in m["files"].items():
        assert hashlib.sha256((d / f).read_bytes()).hexdigest() == e["sha256"] and (d / f).stat().st_size == e["size"]
    assert set(m["files"]) == {m["encoder"], m["decoder"], m["tokenizer"]}
    enc = ort.InferenceSession(str(d / m["encoder"]))
    assert [i.name for i in enc.get_inputs()] == ["pixel_values"]
    dec = ort.InferenceSession(str(d / m["decoder"]))
    ins = {i.name: i for i in dec.get_inputs()}
    assert {"input_ids", "encoder_hidden_states", "use_cache_branch"} <= set(ins)
    assert ins["use_cache_branch"].type == "tensor(bool)"
    past = [i for n, i in ins.items() if n.startswith("past_key_values")]
    assert len(past) == 2 * 4  # 2 layers x decoder/encoder x key/value
    for i in past:  # heads and head size fixed: the app sizes the first step's empty caches by them
        assert isinstance(i.shape[1], int) and isinstance(i.shape[3], int)
    outs = [o.name for o in dec.get_outputs()]
    assert outs[0] == "logits" and all(o.startswith("present.") for o in outs[1:])
    assert {o.replace("present", "past_key_values") for o in outs[1:]} == {i.name for i in past}


def test_ctc_folder_as_the_app_reads_it(exported):
    d = exported / "ctc"
    m = _manifest(d)
    assert m["kind"] == "ctc" and m["model"] == "model_int8.onnx" and m["blank"] == 0
    assert m["input_height"] == 64 and m["max_width"] == 512
    al = read_alphabet(d / m["alphabet"])
    assert " " in al and "ß" in al
    s = ort.InferenceSession(str(d / m["model"]))
    assert [i.name for i in s.get_inputs()] == ["image"] and [o.name for o in s.get_outputs()] == ["logits"]
    for w in (40, 300, 512):
        out = s.run(None, {"image": np.random.rand(1, 1, 64, w).astype(np.float32)})[0]
        assert out.shape == (w // 4, 1, len(al) + 1)
        assert np.allclose(np.exp(out).sum(-1), 1, atol=1e-3)  # log-softmax


def test_parity_fp32_exact_int8_close(trained, exported, data_root):
    pics = export.sample_pictures(["synthetic-de"], str(data_root), 3)
    for kind in ("trocr", "ctc"):
        ck = trained / kind / "checkpoints/best"
        fp32 = exported / f"{kind}-fp32"
        (export.export_trocr if kind == "trocr" else export.export_ctc)(ck, fp32, quant=False)
        rep = export.parity(ck, exported / kind, pics, fp32)
        assert rep["fp32"]["same_top1"] == 3 and rep["fp32"]["max_abs_diff"] < 1e-3, rep
        assert rep["shipped"]["max_abs_diff"] < 1.0, rep


def test_exported_folders_evaluate_through_onnxruntime(exported, data_root, tmp_path, monkeypatch):
    r = open_model(exported / "trocr")
    assert isinstance(r, OnnxTrocr) and isinstance(open_model(exported / "ctc"), OnnxCtc)
    monkeypatch.setenv("XQT_HWR_MODEL", str(exported / "trocr"))  # stands in for the app's English model
    evaluate.main(["run", "--model", str(exported / "ctc"), "--app-model", "--datasets", "synthetic-en",
                   "--data-root", str(data_root), "--max-lines", "3", "--out", str(tmp_path / "rep")])
    d = json.loads((tmp_path / "rep.json").read_text())
    assert [m["kind"] for m in d["models"]] == ["ctc", "trocr"]
