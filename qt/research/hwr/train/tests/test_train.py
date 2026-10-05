import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from xqt_hwr import train

ROOT = Path(__file__).resolve().parent.parent
TINY = ROOT / "configs" / "tiny"


def run(cfg, data_root, out, *sets, resume=False):
    args = ["--config", str(TINY / cfg), "--set", f"data_root={data_root}", "--set", f"out_dir={out}"]
    for s in sets:
        args += ["--set", s]
    if resume:
        args.append("--resume")
    train.main(args)


def meta(ck):
    return json.loads((ck / "xqt.json").read_text())


@pytest.fixture(scope="session")
def trained(data_root, tmp_path_factory):
    """Tiny TrOCR and CTC trained for three steps (shared by the evaluation and export tests)."""
    out = tmp_path_factory.mktemp("runs")
    run("tiny-trocr.yaml", data_root, out / "trocr")
    run("tiny-ctc.yaml", data_root, out / "ctc")
    return out


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


@pytest.mark.parametrize("cfg", ["tiny-ctc.yaml", "tiny-trocr.yaml"])
def test_ddp_two_cpu_processes(cfg, data_root, tmp_path):
    env = dict(os.environ, OMP_NUM_THREADS="1", PYTHONPATH=str(ROOT))
    cmd = [sys.executable, "-m", "torch.distributed.run", "--standalone", "--nproc_per_node=2",
           str(ROOT / "train.py"), "--config", str(TINY / cfg), "--set", f"data_root={data_root}",
           "--set", f"out_dir={tmp_path}"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    assert "2 process(es) on cpu" in r.stdout
    assert "batch 4 x" in r.stdout and "x 2 =" in r.stdout
    m = meta(tmp_path / "checkpoints/best")
    assert m["step"] == 3
