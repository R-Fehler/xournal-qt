import json

from xqt_hwr import evaluate, metrics


def test_score_line_counts():
    truth = "Die Kalman Verstärkung ist gut"
    readings = [("Die Kalmar Verstarkung ist gat", -1.0), ("Die Kalman Verstärkung ist gut", -1.4)]
    c = metrics.score_line(truth, readings)
    assert c["terms"] == 5                         # die kalman verstärkung ist gut (3+ letters)
    assert metrics.rates(c)["words_found"] == 1.0  # the second reading has a share above MIN_P
    assert c["found_top1"] == 4                    # kalmar, verstarkung: a typo away; "gat" is not "gut"
    assert c["found_exact"] == 5 and c["char_edits"] == 3 and c["word_edits"] == 3
    # a reading with a tiny share does not count
    c = metrics.score_line(truth, [("Die Kalmar Verstarkung ist gat", -1.0), ("gut", -9.0)])
    assert c["found"] == 4


def test_score_line_terms_and_false_hits():
    c = metrics.score_line("a to be or the", [("a to be or the", 0.0)], others=["the zebra sleeps"])
    assert c["terms"] == 1  # only "the" has 3 letters
    assert c["false_tries"] == 2 and c["false_hits"] == 0


def test_evaluate_checkpoints_and_compare(trained, data_root, tmp_path, capsys):
    out = tmp_path / "rep"
    evaluate.main(["compare", "--model", str(trained / "trocr/checkpoints/best"),
                   "--model", str(trained / "ctc/checkpoints/best"), "--datasets", "synthetic-de", "synthetic-en",
                   "--data-root", str(data_root), "--max-lines", "4", "--out", str(out)])
    d = json.loads(out.with_suffix(".json").read_text())
    assert len(d["models"]) == 2
    for r in d["models"]:
        g = r["groups"]
        assert {"all", "dataset:synthetic-de", "lang:de", "lang:en"} <= set(g)
        assert any(k.startswith("writer:synthetic-de/font:") for k in g)
        assert g["all"]["lines"] == 8 and r["seconds_per_line"] > 0
    md = out.with_suffix(".md").read_text()
    assert "| all | tiny-trocr |" in md and "words_found" in md
    # from saved reports
    evaluate.main(["compare", "--reports", str(out.with_suffix(".json")), "--out", str(tmp_path / "again")])
    assert (tmp_path / "again.md").read_text() == md
