#!/usr/bin/env python3
"""Builds the handwriting forms (DESIGN.md): content/<lang>.yaml -> layout -> LaTeX (TikZ) -> PDF with the
manifest embedded, then checks them.

  python3 build.py                 # all forms into pdf/, checked
  python3 build.py xqt-hwr-de      # one form
  python3 build.py --png /tmp/png  # also render every page (pdftoppm or gs) to look at
  python3 build.py --out DIR       # somewhere else than pdf/

Needs Python 3 with PyYAML, and lualatex (TeX Live: fontspec, TikZ, embedfile, hyperref; the DejaVu fonts).
"""
import argparse
import copy
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import yaml  # noqa: E402

import checks  # noqa: E402
import layout  # noqa: E402
import pdfread  # noqa: E402
import tex  # noqa: E402

FORMS = ("xqt-hwr-en", "xqt-hwr-de", "xqt-hwr-en-de")
SOURCE_DATE_EPOCH = "1791158400"   # 2026-10-05: the PDFs come out the same on every build


def load(lang):
    with open(os.path.join(HERE, "content", "%s.yaml" % lang), encoding="utf-8") as f:
        return yaml.safe_load(f)


def lay_out(name):
    """The laid-out form (layout.Form) of one of FORMS."""
    if name == "xqt-hwr-en":
        return layout.build_form(load("en"))
    if name == "xqt-hwr-de":
        return layout.build_form(load("de"))
    if name == "xqt-hwr-en-de":
        en, de = load("en"), load("de")
        en = copy.deepcopy(en)
        en["form"], en["page_id"], en["title"] = name, "XQT-HWR-EN-DE", de["combined_title"]
        en["language"] = "en"
        chapter = copy.deepcopy(de)
        chapter["sections"] = [dict(s, title=de["chapter_prefix"] + s["title"]) for s in de["sections"]
                               if s["id"] != "0"]
        smap = {s["id"]: "H" for s in chapter["sections"]}
        return layout.build_form(en, form_id=name, page_id="XQT-HWR-EN-DE", extra_sections=[(chapter, smap)])
    raise SystemExit("unknown form %r (known: %s)" % (name, ", ".join(FORMS)))


class Built:
    def __init__(self, name, form, manifest, pdf, log, measures):
        self.name, self.form, self.manifest, self.pdf, self.log, self.measures = \
            name, form, manifest, pdf, log, measures

    def problems(self):
        p = []
        p += checks.check_geometry(self.form)
        p += checks.check_bottom(self.form)
        p += checks.check_manifest(self.manifest, pdf_pages(self.pdf))
        p += checks.check_measures(self.form, self.measures)
        p += checks.check_log(self.log)
        back = pdfread.read_manifest(self.pdf)
        if back != self.manifest:
            p.append("the manifest read back from the PDF differs from the one written")
        return p


def pdf_pages(path):
    with open(path, "rb") as f:
        data = f.read()
    m = re.findall(rb"/Type\s*/Pages\b.*?/Count\s+(\d+)", data, re.S)
    if m:
        return max(int(x) for x in m)
    return None


def manifest_json(m):
    """The manifest as JSON, one box per line (short diffs, still easy to read)."""
    head = {k: v for k, v in m.items() if k != "items"}
    lines = ["{"]
    for k, v in head.items():
        lines.append(" %s: %s," % (json.dumps(k), json.dumps(v, ensure_ascii=False)))
    items = [json.dumps(it, ensure_ascii=False) for it in m["items"]]
    lines.append(' "items": [')
    lines += ["  %s%s" % (it, "," if i + 1 < len(items) else "") for i, it in enumerate(items)]
    lines += [" ]", "}"]
    return "\n".join(lines) + "\n"


def build(name, out_dir, work_dir=None):
    """Lays out, writes and compiles one form into out_dir (<name>.pdf, <name>.manifest.json)."""
    form = lay_out(name)
    m = layout.manifest(form)
    own_work = work_dir is None
    work = work_dir or tempfile.mkdtemp(prefix="xqt-forms-")
    try:
        mname = "%s.manifest.json" % name
        with open(os.path.join(work, mname), "w", encoding="utf-8") as f:
            f.write(manifest_json(m))
        stamp = int(SOURCE_DATE_EPOCH)
        os.utime(os.path.join(work, mname), (stamp, stamp))   # the embedded file's date
        title = form.content.get("title", name)
        with open(os.path.join(work, name + ".tex"), "w", encoding="utf-8") as f:
            f.write(tex.document(form, mname, title))
        env = dict(os.environ, SOURCE_DATE_EPOCH=SOURCE_DATE_EPOCH, FORCE_SOURCE_DATE="1", max_print_line="100000",
                   TZ="UTC")
        for _ in range(2):   # remember picture: the second run places the pictures on the page
            r = subprocess.run(["lualatex", "-interaction=nonstopmode", "-halt-on-error", name + ".tex"],
                               cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if r.returncode != 0:
                tail = r.stdout.decode("utf-8", "replace")[-3000:]
                raise RuntimeError("lualatex failed on %s:\n%s" % (name, tail))
        with open(os.path.join(work, name + ".log"), encoding="utf-8", errors="replace") as f:
            log = f.read()
        os.makedirs(out_dir, exist_ok=True)
        pdf = os.path.join(out_dir, name + ".pdf")
        shutil.copyfile(os.path.join(work, name + ".pdf"), pdf)
        with open(os.path.join(out_dir, mname), "w", encoding="utf-8") as f:
            f.write(manifest_json(m))
        return Built(name, form, m, pdf, log, tex.parse_measures(log))
    finally:
        if own_work:
            shutil.rmtree(work, ignore_errors=True)


def render_png(pdf, out_dir, dpi=60):
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, os.path.splitext(os.path.basename(pdf))[0])
    if shutil.which("pdftoppm"):
        subprocess.run(["pdftoppm", "-r", str(dpi), "-png", pdf, base], check=True)
    else:
        subprocess.run(["gs", "-q", "-dNOPAUSE", "-dBATCH", "-sDEVICE=png16m", "-r%d" % dpi,
                        "-sOutputFile=%s-%%d.png" % base, pdf], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("forms", nargs="*", default=list(FORMS))
    ap.add_argument("--out", default=os.path.join(HERE, "pdf"))
    ap.add_argument("--png", help="render every page into this folder")
    ap.add_argument("--dpi", type=int, default=60)
    ap.add_argument("--keep", help="keep the LaTeX files in this folder")
    args = ap.parse_args()
    bad = 0
    for name in args.forms:
        work = None
        if args.keep:
            work = os.path.join(args.keep, name)
            os.makedirs(work, exist_ok=True)
        b = build(name, args.out, work)
        problems = b.problems()
        if name in ("xqt-hwr-en", "xqt-hwr-en-de"):
            problems += checks.check_angles(b.manifest)
        n = len(b.manifest["items"])
        print("%s: %d pages, %d boxes -> %s" % (name, b.manifest["pages"], n, os.path.relpath(b.pdf)))
        for p in problems:
            print("  " + p)
        bad += len(problems)
        if args.png:
            render_png(b.pdf, args.png, args.dpi)
    if bad:
        sys.exit("%d problems" % bad)


if __name__ == "__main__":
    main()
