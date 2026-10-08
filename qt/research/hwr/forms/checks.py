"""Checks of a laid-out and built form: geometry, the manifest, the LaTeX build. Each returns a list of problems."""
from geometry import contains, inside, polygons_overlap
from layout import (BOTTOM, KINDS, MARGIN, NO_TEXT_KINDS, PAGE_H, PAGE_W, TEXT_KINDS, hand_width)

REQUIRED_FIELDS = ("id", "page", "section", "kind", "text", "lang", "box_mm", "angle", "x_height_mm", "guides",
                   "tags", "search")
ANGLES = (0, 15, -15, 30, -30, 45, -45, 60, -60, 90, -90, 135, -135, 180)


def _nested(child, parent_item):
    p = child.parent
    while p is not None:
        if p is parent_item:
            return True
        p = p.parent
    return False


def check_geometry(form):
    problems = []
    lo_x, lo_y, hi_x, hi_y = MARGIN, MARGIN, PAGE_W - MARGIN, PAGE_H - MARGIN
    for page in form.pages:
        pn = page.number
        for it in page.items:
            for name, poly in (("box", it.box_poly()), ("head row", it.head_poly())):
                if not inside(poly, lo_x, lo_y, hi_x, hi_y):
                    problems.append("page %d: %s of box %s leaves the printable area" % (pn, name, it.id))
            if it.parent is not None and not contains(it.parent.box_poly(), it.footprint_poly(), 0.5):
                problems.append("page %d: box %s is not inside its parent %s" % (pn, it.id, it.parent.id))
        for i, a in enumerate(page.items):
            for b in page.items[i + 1:]:
                if _nested(a, b) or _nested(b, a):
                    continue
                if polygons_overlap(a.box_poly(), b.box_poly()):
                    problems.append("page %d: boxes %s and %s overlap" % (pn, a.id, b.id))
        for pr in page.prints:
            if pr.poly is None:
                problems.append("page %d: a printed thing without a region: %s" % (pn, pr.tex[:60]))
                continue
            if not inside(pr.poly, lo_x, lo_y, hi_x, hi_y):
                problems.append("page %d: printed %s leaves the printable area" % (pn, pr.mkey or pr.tex[:40]))
            for it in page.items:
                if it in pr.allow:
                    continue
                if pr.owner is not None and (pr.owner is it or _nested(pr.owner, it)):
                    continue
                if polygons_overlap(pr.poly, it.box_poly()):
                    problems.append("page %d: printed %s overlaps box %s" % (pn, pr.mkey or pr.tex[:40], it.id))
        texts = [pr for pr in page.prints if pr.kind == "text" and pr.poly is not None]
        for i, a in enumerate(texts):
            for b in texts[i + 1:]:
                if polygons_overlap(a.poly, b.poly):
                    problems.append("page %d: printed %s and %s overlap" % (pn, a.mkey, b.mkey))
        lines = [pr for pr in page.prints if pr.kind == "line" and pr.poly is not None]
        for a in texts:
            for b in lines:
                if polygons_overlap(a.poly, b.poly):
                    problems.append("page %d: printed %s crosses a line" % (pn, a.mkey))
        for it in page.items:
            if it.kind in TEXT_KINDS and it.kind != "math" and it.x_height:
                need = hand_width(it.text, it.x_height) * 0.95
                if need > it.w + 0.5:
                    problems.append("page %d: box %s (%.0f mm) is too narrow for %r (about %.0f mm)"
                                    % (pn, it.id, it.w, it.text, need))
            if it.kind == "line" and (it.x_height or 0) >= 3 and it.h < 9:
                problems.append("page %d: line box %s is only %.1f mm high" % (pn, it.id, it.h))
    return problems


def check_manifest(m, pages=None):
    problems = []
    for key in ("form", "version", "language", "page_size_mm", "pages", "items"):
        if key not in m:
            problems.append("manifest: no %r" % key)
    if problems:
        return problems
    if m["page_size_mm"] != [210, 297]:
        problems.append("manifest: page size %r" % m["page_size_mm"])
    if pages is not None and m["pages"] != pages:
        problems.append("manifest: %d pages, the PDF has %d" % (m["pages"], pages))
    ids = set()
    for it in m["items"]:
        missing = [f for f in REQUIRED_FIELDS if f not in it]
        if missing:
            problems.append("item %s: missing %s" % (it.get("id"), ", ".join(missing)))
            continue
        iid = it["id"]
        if iid in ids:
            problems.append("item %s: id used twice" % iid)
        ids.add(iid)
        if not iid.startswith("%d." % it["page"]):
            problems.append("item %s: id does not start with its page %d" % (iid, it["page"]))
        if not 1 <= it["page"] <= m["pages"]:
            problems.append("item %s: page %r out of range" % (iid, it["page"]))
        if it["kind"] not in KINDS:
            problems.append("item %s: unknown kind %r" % (iid, it["kind"]))
        if it["kind"] in TEXT_KINDS and not it["text"].strip():
            problems.append("item %s: a %s without text" % (iid, it["kind"]))
        if it["kind"] in NO_TEXT_KINDS and (it["text"] or it["search"]):
            problems.append("item %s: a %s with text" % (iid, it["kind"]))
        if it["kind"] in TEXT_KINDS and not isinstance(it["x_height_mm"], (int, float)):
            problems.append("item %s: no x-height" % iid)
        if it["kind"] == "math" and not it.get("latex"):
            problems.append("item %s: a formula without latex" % iid)
        b = it["box_mm"]
        if len(b) != 4 or b[2] <= 0 or b[3] <= 0:
            problems.append("item %s: bad box %r" % (iid, b))
        if not -180 < it["angle"] <= 180:
            problems.append("item %s: angle %r out of (-180, 180]" % (iid, it["angle"]))
        if not isinstance(it["tags"], list) or not isinstance(it["search"], list):
            problems.append("item %s: tags and search must be lists" % iid)
        for w in it["search"]:
            if w not in it["text"]:
                problems.append("item %s: search word %r is not in its text" % (iid, w))
    for it in m["items"]:
        if "in" in it and it["in"] not in ids:
            problems.append("item %s: in %r, no such box" % (it["id"], it["in"]))
    return problems


def check_angles(m, minimum=4):
    """Every angle of DESIGN.md is there, and each angle class (15, 30, ... 180, both signs) has enough lines."""
    problems = []
    texts = [it for it in m["items"] if it["kind"] in ("line", "word", "label")]
    seen = {it["angle"] for it in texts}
    for a in ANGLES:
        if a not in seen:
            problems.append("no box at %d degrees" % a)
    for a in sorted({abs(a) for a in ANGLES}):
        n = sum(1 for it in texts if abs(it["angle"]) == a)
        if n < minimum:
            problems.append("only %d boxes at +-%d degrees" % (n, a))
    return problems


def check_measures(form, measures, tol=0.3):
    """Every printed text fits the region the layout allotted it (TeX's measured size)."""
    problems = []
    for page in form.pages:
        for pr in page.prints:
            if not pr.mkey:
                continue
            if pr.mkey not in measures:
                problems.append("page %d: TeX did not measure %s" % (page.number, pr.mkey))
                continue
            w, h = measures[pr.mkey]
            aw, ah = pr.alloc
            if w > aw + tol or h > ah + tol:
                problems.append("page %d: printed %s is %.1f x %.1f mm, room for %.1f x %.1f"
                                % (page.number, pr.mkey, w, h, aw, ah))
    return problems


def check_log(log_text):
    problems = []
    for line in log_text.splitlines():
        if line.startswith(("Overfull", "Underfull \\vbox")) or "Missing character" in line:
            problems.append("LaTeX: " + line.strip())
    return problems


def check_bottom(form):
    return ["page %d: content runs below %.0f mm" % (p.number, BOTTOM)
            for p in form.pages if p.y is not None and p.y - 4.0 > BOTTOM + 0.5]
