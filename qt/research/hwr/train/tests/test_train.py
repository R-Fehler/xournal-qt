import json
import os
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parent.parent
TINY = ROOT / "configs" / "tiny"


from conftest import run_train as run


def meta(ck):
    return json.loads((ck / "xqt.json").read_text())


def test_one_step_each_kind(trained):
    for kind in ("trocr", "ctc"):
        ck = trained / kind / "checkpoints"
        assert (ck / "best" / "xqt.json").exists() and (ck / "last" / "train_state.pt").exists()
        m = meta(ck / "best")
        assert m["kind"] == kind and m["step"] == 3 and m["val"]["terms"] > 0
        assert m["trained_on"] == ["synthetic-de", "synthetic-en"]
        log = [json.loads(l) for l in (trained / kind / "log.jsonl").read_text().splitlines()]
        assert any("loss" in r for r in log) and any("val" in r for r in log)
    assert (trained / "trocr/checkpoints/best/tokenizer.json").exists()
    assert (trained / "trocr/checkpoints/best/config.json").exists()


def test_resume(data_root, tmp_path, capsys):
    run("tiny-ctc.yaml", data_root, tmp_path)
    run("tiny-ctc.yaml", data_root, tmp_path, "train.max_steps=5", "val.every_steps=5", resume=True)
    assert "resumed at step 3" in capsys.readouterr().out
    assert meta(tmp_path / "checkpoints/last")["step"] == 5


def test_finetune_lora_and_last_layers(trained, data_root, tmp_path):
    run("tiny-finetune-trocr.yaml", data_root, tmp_path / "t", f"finetune.init_from={trained / 'trocr/checkpoints/best'}")
    m = meta(tmp_path / "t/checkpoints/best")
    assert m["trained_on"] == ["ink-user", "synthetic-de"] and m["base"].endswith("trocr/checkpoints/best")
    # the checkpoint is a plain model (LoRA merged)
    from transformers import VisionEncoderDecoderModel
    plain = VisionEncoderDecoderModel.from_pretrained(tmp_path / "t/checkpoints/best")
    assert not any("lora" in n for n, _ in plain.named_parameters())
    run("tiny-finetune-ctc.yaml", data_root, tmp_path / "c", f"finetune.init_from={trained / 'ctc/checkpoints/best'}")
    assert meta(tmp_path / "c/checkpoints/best")["kind"] == "ctc"


@pytest.mark.parametrize("cfg", ["tiny-ctc.yaml", "tiny-trocr.yaml", "tiny-finetune-trocr.yaml"])
def test_ddp_two_cpu_processes(cfg, data_root, tmp_path, trained):
    env = dict(os.environ, OMP_NUM_THREADS="1", PYTHONPATH=str(ROOT))
    cmd = [sys.executable, "-m", "torch.distributed.run", "--standalone", "--nproc_per_node=2",
           str(ROOT / "train.py"), "--config", str(TINY / cfg), "--set", f"data_root={data_root}",
           "--set", f"out_dir={tmp_path}", "--set", "train.max_steps=3", "--set", "val.every_steps=3"]
    if "finetune" in cfg:  # a saved model through from_pretrained (DDP must see no unused parameters), with LoRA
        cmd += ["--set", f"finetune.init_from={trained / 'trocr/checkpoints/best'}", "--set", "finetune.freeze=none"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    assert "2 process(es) on cpu" in r.stdout
    assert "batch 4 x" in r.stdout and "x 2 =" in r.stdout
    m = meta(tmp_path / "checkpoints/best")
    assert m["step"] == 3


def test_auto_precision_uses_fp32_where_bf16_is_only_emulated():
    from xqt_hwr.train import auto_precision
    assert auto_precision((8, 6), True) == "bf16"     # Ampere
    assert auto_precision((7, 5), False) == "fp16"    # Turing
    assert auto_precision((6, 1), False) == "fp32"    # Pascal (GTX 1080 Ti): emulated bf16 is slower than fp32


def test_a_final_run_learns_from_train_and_val(data_root, tmp_path, capsys):
    import re
    run("tiny-ctc.yaml", data_root, tmp_path / "a")
    only_train = int(re.search(r"(\d+) training lines", capsys.readouterr().out).group(1))
    run("tiny-ctc.yaml", data_root, tmp_path / "b", "train.splits=[train, val]")
    with_val = int(re.search(r"(\d+) training lines", capsys.readouterr().out).group(1))
    assert with_val > only_train
