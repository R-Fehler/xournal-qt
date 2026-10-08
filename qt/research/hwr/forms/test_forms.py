"""Checks of the handwriting forms: python -m pytest (in this folder). Builds every form once (lualatex, ~40 s)."""
import json
import os
import shutil
import subprocess

import pytest

import build
import checks
import pdfread
from geometry import box_polygon, contains, polygons_overlap

HERE = os.path.dirname(os.path.abspath(__file__))


@pytest.fixture(scope="session")
def built(tmp_path_factory):
    out = tmp_path_factory.mktemp("forms")
    return {name: build.build(name, str(out)) for name in build.FORMS}


# --- geometry


def test_turned_boxes_overlap_by_their_polygons():
    a = box_polygon(0, 0, 100, 10, 45)
    b = box_polygon(60, 60, 10, 10, 0)     # inside a's bounding box, outside the turned box
    assert not polygons_overlap(a, b)
    assert polygons_overlap(a, box_polygon(45, 0, 10, 10, 0))
    assert not polygons_overlap(box_polygon(0, 0, 10, 10), box_polygon(10, 0, 10, 10))   # touching


def test_angles_turn_clockwise():
    # 90 degrees: the box's writing direction points down the page
    (x0, y0), (x1, y1) = box_polygon(0, 0, 40, 10, 90)[:2]
    assert abs(x1 - x0) < 1e-9 and y1 > y0
    assert contains(box_polygon(0, 0, 50, 50), box_polygon(10, 10, 10, 10, 30), 1.0)


# --- the forms


@pytest.mark.parametrize("name", build.FORMS)
def test_geometry(built, name):
    b = built[name]
    assert checks.check_geometry(b.form) == []
    assert checks.check_bottom(b.form) == []


@pytest.mark.parametrize("name", build.FORMS)
def test_manifest(built, name):
    b = built[name]
    assert checks.check_manifest(b.manifest, build.pdf_pages(b.pdf)) == []
    kinds = {it["kind"] for it in b.manifest["items"]}
    assert {"line", "word", "chars"} <= kinds


@pytest.mark.parametrize("name", build.FORMS)
def test_every_angle_class(built, name):
    """At least 6 boxes of text at each angle in each direction (English), 4 at 0, ±45, ±90, 180 (German)."""
    assert build.angle_problems(name, built[name].manifest) == []
    assert checks.check_angles({"items": []}) != []   # (the check does count)


def test_english_sections_and_kinds(built):
    m = built["xqt-hwr-en"].manifest
    assert {it["section"] for it in m["items"]} == {"0", "A", "B", "C", "D", "E", "F", "G"}
    kinds = {it["kind"] for it in m["items"]}
    assert kinds == {"line", "word", "chars", "number", "label", "math", "drawing", "mark", "free"}
    assert sum(1 for it in m["items"] if it["section"] == "B") >= 24
    assert sum(1 for it in m["items"] if it["section"] == "C") == 40
    assert sum(1 for it in m["items"] if it["section"] == "G") == 12


def test_german_chapter(built):
    de = built["xqt-hwr-de"].manifest
    assert {it["section"] for it in de["items"]} == {"0", "A", "B", "C", "D"}
    assert all(it["lang"] == "de" for it in de["items"])
    text = " ".join(it["text"] for it in de["items"])
    for ch in "äöüÄÖÜß„“€":
        assert ch in text
    both = built["xqt-hwr-en-de"].manifest
    h = [it for it in both["items"] if it["section"] == "H"]
    assert h and all(it["lang"] == "de" for it in h)
    assert {t for it in h for t in it["tags"] if t.startswith("de-")} == {"de-A", "de-B", "de-C", "de-D"}
    assert all(it["lang"] == "en" for it in both["items"] if it["section"] != "H")


@pytest.mark.parametrize("name", build.FORMS)
def test_printed_texts_fit(built, name):
    b = built[name]
    assert checks.check_measures(b.form, b.measures) == []
    assert checks.check_log(b.log) == []


@pytest.mark.parametrize("name", build.FORMS)
def test_manifest_read_back_from_pdf(built, name, tmp_path):
    b = built[name]
    assert pdfread.read_manifest(b.pdf) == b.manifest
    if shutil.which("pdfdetach"):
        subprocess.run(["pdfdetach", "-saveall", "-o", str(tmp_path), b.pdf], check=True)
        with open(tmp_path / ("%s.manifest.json" % name), encoding="utf-8") as f:
            assert json.load(f) == b.manifest


@pytest.mark.parametrize("name", build.FORMS)
def test_committed_files_are_current(built, name):
    """pdf/ holds what build.py makes now: rebuild (python3 build.py) and commit after changing the content."""
    with open(os.path.join(HERE, "pdf", name + ".manifest.json"), encoding="utf-8") as f:
        assert json.load(f) == built[name].manifest
    assert pdfread.read_manifest(os.path.join(HERE, "pdf", name + ".pdf")) == built[name].manifest
