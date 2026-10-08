"""The layout of a form: content (content/<lang>.yaml) placed on A4 pages, every box at a known position in mm.

The content says what to write (sections, blocks, items: texts, kinds, sizes, angles, tags); this module decides
where. The result is a list of pages, each with its writing boxes (`Item`) and its printed things (`Printed`: texts,
lines, frames), all as polygons in page millimetres, from which tex.py writes the LaTeX and manifest() the manifest.
"""
import math
import re

from geometry import Frame, aabb

PAGE_W, PAGE_H = 210.0, 297.0
MARGIN = 10.0                     # the printable margin: nothing is drawn closer to the edge
X0, X1 = 12.0, 198.0              # the content column
BOTTOM = 286.0                    # the content ends here
HEAD_GAP = 1.0                    # between a box's head row (prompt, id) and the box
ID_W = 8.0                        # room for a box id ("10.24") at the end of the head row
ARROW_W = 8.0                     # room for the writing-direction arrow at the start of the head row
ROW_GAP = 3.5                     # between rows of boxes
COL_GAP = 6.0                     # between boxes side by side
PT = 25.4 / 72.27                 # one TeX point in mm

TEXT_KINDS = ("line", "word", "chars", "number", "label", "math")
NO_TEXT_KINDS = ("drawing", "mark", "free")
KINDS = TEXT_KINDS + NO_TEXT_KINDS
SIZE_TAGS = {2: "small", 3: "normal", 4: "medium", 6: "large", 10: "headline"}


def box_height(x_height):
    """The box height (mm) for writing of a given x-height: room for ascenders, descenders and a little air."""
    return float(max(7, math.ceil(x_height * 3.3)))


def baseline_at(h):
    """Where a box's faint baseline is, from its top (mm)."""
    return round(h * 0.65, 2)


def head_height(prompt_pt):
    return {None: 3.2, 7: 3.6, 8: 4.0, 9: 4.5, 10: 5.0, 11: 5.4, 12: 6.0}[prompt_pt]


def hand_width(text, x_height):
    """A generous estimate of how wide `text` comes out in handwriting of the given x-height (mm)."""
    w = 0.0
    for ch in text:
        if ch == " ":
            w += 0.7
        elif ch in "il.,;:'!|`’":
            w += 0.6
        elif ch.isupper() or ch in "mwMW@%&€§":
            w += 1.35
        else:
            w += 1.0
    return w * x_height + 1.5 * x_height + 4


def tex_width(text, pt, mono=False):
    """An estimate (on the wide side) of a printed text's width (mm) in DejaVu Sans; TeX measures the real one."""
    if mono:
        return len(text) * 0.61 * pt * PT
    w = 0.0
    for ch in text:
        if ch == " ":
            w += 0.32
        elif ch in "il.,;:'!|ftrj()[]{}":
            w += 0.40
        elif ch.isupper() or ch in "mwMW@%&€§":
            w += 0.80
        else:
            w += 0.62
    return w * pt * PT


def words_of(text, minimum=3):
    """The default search words of a box: its words of 3 or more letters (DESIGN.md)."""
    out = []
    for tok in text.split():
        tok = tok.strip(".,;:!?\"'()[]{}„“”‘’«»…")
        if sum(ch.isalpha() for ch in tok) >= minimum:
            out.append(tok)
    return out


def round_up(v, step=5.0):
    return math.ceil(v / step - 1e-9) * step


class Item:
    """One writing box. Placed by setting its frame (centre and angle); `page` and `id` are set by the layout."""

    def __init__(self, spec, section, lang, kind=None, **over):
        spec = dict(spec or {}, **over)
        self.kind = spec.get("kind", kind or "line")
        if self.kind not in KINDS:
            raise ValueError("unknown kind %r" % self.kind)
        self.text = spec.get("text", "") if self.kind in TEXT_KINDS else ""
        self.prompt = spec.get("prompt", spec.get("text", ""))
        self.prompt_tex = spec.get("prompt_tex")   # a TeX prompt (formulas), printed as is
        self.latex = spec.get("latex")
        self.lang = spec.get("lang", lang)
        self.section = section
        xh = spec.get("x_height", 3 if self.kind in TEXT_KINDS else None)
        self.x_height = xh
        self.guides = bool(spec.get("guides", self.kind in ("line", "word", "number", "chars")))
        self.mono = bool(spec.get("mono", self.kind == "chars"))
        tags = list(spec.get("tags", []))
        if xh in SIZE_TAGS and self.kind in TEXT_KINDS and SIZE_TAGS[xh] not in tags:
            tags.append(SIZE_TAGS[xh])
        self.tags = tags
        if "search" in spec:
            self.search = list(spec["search"])
        elif self.kind in ("line", "word", "number", "label"):
            self.search = words_of(self.text)
        else:
            self.search = []
        self.context = spec.get("context")
        self.parent = None
        self.angle = float(spec.get("angle", 0))
        self.arrow = bool(spec.get("arrow", self.angle != 0))
        self.prompt_mode = spec.get("prompt_mode", "above")   # above, left, right, none
        self.prompt_pt = spec.get("prompt_pt", 8 if (xh or 3) <= 2 else 10)
        if self.prompt_mode == "none":
            self.prompt_pt = None
        self.side_w = float(spec.get("side_w", 0))            # the width of a left or right prompt
        self.h = float(spec.get("height", box_height(xh) if xh else 10))
        if "width" in spec:
            self.w = float(spec["width"])
        elif self.kind in TEXT_KINDS:
            self.w = max(float(spec.get("min_width", 20)), round_up(hand_width(self.text, xh)))
        else:
            self.w = float(spec.get("min_width", 20))
        if "width" not in spec and self.kind in TEXT_KINDS and self.prompt_mode == "above":
            self.w = max(self.w, round_up(self.head_need()))   # as wide as its prompt: more room is fine
        self.head_w = max(self.w, self.head_need())
        self.frame = None
        self.page = None
        self.id = None

    # The head row: [arrow] prompt ... id, above the box (in the box's frame).
    def head_h(self):
        if self.prompt_mode in ("left", "right"):
            return -HEAD_GAP   # no head row: the id goes beside the box, with the prompt
        return head_height(self.prompt_pt if self.prompt_mode == "above" else None)

    def head_need(self):
        need = ID_W + (ARROW_W if self.arrow else 0)
        if self.prompt_mode == "above" and self.prompt:
            need += tex_width(self.prompt, self.prompt_pt, self.mono) + 2
        return need

    def depth(self):
        """The footprint's height in its own frame: head row, gap, box."""
        return self.head_h() + HEAD_GAP + self.h

    def place(self, x, y, angle=None):
        """Places the footprint (head row and box) upright with its top-left corner at (x, y), or turned by
        `angle` around the box's centre when given (x, y then is the box's centre)."""
        if angle is None and self.angle == 0:
            self.frame = Frame(x + self.w / 2, y + self.head_h() + HEAD_GAP + self.h / 2, 0)
        else:
            self.frame = Frame(x, y, self.angle if angle is None else angle)
            self.angle = self.frame.angle
        return self

    # Polygons in page mm
    def box_poly(self):
        return self.frame.rect(-self.w / 2, -self.h / 2, self.w / 2, self.h / 2)

    def head_rect(self):
        v1 = -self.h / 2 - HEAD_GAP
        return (-self.w / 2, v1 - self.head_h(), -self.w / 2 + self.head_w, v1)

    def head_poly(self):
        return self.frame.rect(*self.head_rect())

    def footprint_poly(self):
        u0, v0, u1, _ = self.head_rect()
        return self.frame.rect(u0, v0, max(u1, self.w / 2), self.h / 2)

    def side_rect(self):
        """The left or right prompt's region (local), or None."""
        if self.prompt_mode == "left":
            return (-self.w / 2 - COL_GAP / 2 - self.side_w, -self.h / 2, -self.w / 2 - COL_GAP / 2, self.h / 2)
        if self.prompt_mode == "right":
            return (self.w / 2 + 2, -self.h / 2, self.w / 2 + 2 + self.side_w, self.h / 2)
        return None

    def upright_box(self):
        """box_mm of the manifest: x, y, w, h of the box upright around its centre."""
        f = self.frame
        return [round(f.cx - self.w / 2, 2), round(f.cy - self.h / 2, 2), round(self.w, 2), round(self.h, 2)]

    def manifest(self):
        d = {
            "id": self.id, "page": self.page, "section": self.section, "kind": self.kind,
            "text": self.text, "lang": self.lang, "box_mm": self.upright_box(),
            "angle": int(self.angle) if float(self.angle).is_integer() else self.angle,
            "x_height_mm": self.x_height if self.kind in TEXT_KINDS else None,
            "guides": self.guides, "tags": self.tags, "search": self.search,
        }
        if self.kind == "math":
            d["latex"] = self.latex or ""
        if self.context:
            d["context"] = self.context
        if self.parent is not None:
            d["in"] = self.parent.id
        return d


class Printed:
    """Something printed on the page: a text (measured by TeX against its allotted region) or a line or frame.

    `poly` is the region it may use; checks keep it off every box. `tex` draws it; a text's TeX holds the
    measuring macro with key `mkey`, so the build can compare TeX's size with the allotted one (`alloc`, mm)."""

    def __init__(self, poly, tex, kind="text", alloc=None, mkey=None, allow=()):
        self.poly, self.tex, self.kind, self.alloc, self.mkey = poly, tex, kind, alloc, mkey
        self.allow = set(allow)   # the boxes this may lie inside (a printed word to circle in a mark box)
        self.owner = None         # the box whose head row or side prompt this is


class Page:
    def __init__(self, number, section, title, instruction, x0=X0, x1=X1):
        self.number, self.section, self.title, self.instruction = number, section, title, instruction
        self.items, self.prints = [], []
        self.x0, self.x1 = x0, x1
        self.y = None   # the cursor of the flow, set after the header


# ---------------------------------------------------------------------------------------------------- TeX snippets

def tex_escape(s):
    rep = {"\\": r"\textbackslash{}", "{": r"\{", "}": r"\}", "$": r"\$", "&": r"\&", "#": r"\#",
           "^": r"\textasciicircum{}", "_": r"\_", "%": r"\%", "~": r"\textasciitilde{}"}
    return "".join(rep.get(ch, ch) for ch in s)


def fmt(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


class Measures:
    """Hands out keys for measured TeX boxes."""

    def __init__(self):
        self.n = 0

    def key(self, prefix):
        self.n += 1
        return "%s-%d" % (prefix, self.n)


MEASURES = Measures()


def text_node(page, x, y, text, pt, anchor="west", angle=0.0, alloc=None, poly=None, color="xqtprompt",
              mono=False, bold=False, raw=False, prefix="t", allow=()):
    """A one-line printed text with its anchor at page (x, y), turned with `angle` (clockwise)."""
    key = MEASURES.key(prefix)
    body = text if raw else tex_escape(text)
    font = r"\xqtmono" if mono else ""
    if bold:
        font += r"\bfseries"
    lead = int(round(pt * 1.2))
    tex = (r"\node[xqtt, text=%s, anchor=%s, rotate=%s] at (%s,%s) {\xqtm{%s}{\fontsize{%s}{%d}\selectfont%s %s}};"
           % (color, anchor, fmt(-angle), fmt(x), fmt(y), key, pt, lead, font, body))
    page.prints.append(Printed(poly, tex, "text", alloc, key, allow))


def para_node(page, x, y, w, h, text_tex, pt=10, color="xqtprompt", prefix="p"):
    """A printed paragraph (TeX already escaped) in the region x, y, w, h (mm); TeX measures its height."""
    key = MEASURES.key(prefix)
    lead = round(pt * 1.3, 1)
    tex = (r"\node[xqtt, text=%s, anchor=north west] at (%s,%s) {\xqtp{%s}{%smm}{\fontsize{%s}{%s}\selectfont %s}};"
           % (color, fmt(x), fmt(y), key, fmt(w), pt, lead, text_tex))
    poly = Frame(0, 0).rect(x, y, x + w, y + h)
    page.prints.append(Printed(poly, tex, "text", (w, h), key))


def para_lines(text, w, pt):
    """An estimate of the lines a paragraph needs (TeX checks it)."""
    per_line = w / (0.54 * pt * PT)
    lines = 0
    for p in text.split("\n"):
        lines += max(1, math.ceil(len(p) / per_line))
    return lines


def para_height(text, w, pt):
    return para_lines(text, w, pt) * pt * 1.3 * PT + 1.0


def line_print(page, pts, style="xqtline"):
    """A printed line (a frame edge, an axis); its region is a thin band around it."""
    (xa, ya), (xb, yb) = pts
    tex = r"\draw[%s] (%s,%s) -- (%s,%s);" % (style, fmt(xa), fmt(ya), fmt(xb), fmt(yb))
    ln = math.hypot(xb - xa, yb - ya)
    ang = math.degrees(math.atan2(yb - ya, xb - xa))
    f = Frame((xa + xb) / 2, (ya + yb) / 2, ang)
    poly = f.rect(-ln / 2, -0.2, ln / 2, 0.2)
    page.prints.append(Printed(poly, tex, "line"))


def rect_print(page, x, y, w, h, style="xqtline"):
    for a, b in (((x, y), (x + w, y)), ((x + w, y), (x + w, y + h)), ((x + w, y + h), (x, y + h)),
                 ((x, y + h), (x, y))):
        line_print(page, (a, b), style)


# ---------------------------------------------------------------------------------------------------- the flow

class Piece:
    """A part of a block that is placed as a whole: `height` mm tall, placed by `place(page, y)`."""

    def __init__(self, height, place, keep_with_next=False, gap=ROW_GAP):
        self.height, self.place, self.keep_with_next, self.gap = height, place, keep_with_next, gap


class Form:
    def __init__(self, content, sections=None, form_id=None, page_id=None, section_map=None):
        self.content = content
        self.form_id = form_id or content["form"]
        self.page_id = page_id or content["page_id"]
        self.lang = content["language"]
        self.pages = []
        self.section_map = section_map or {}

    # Pages
    def new_page(self, section, continued=False):
        sec = section
        cont = sec.get("continued", self.continued)
        title = sec["title"] + (cont if continued else "")
        x0 = X0 + sec.get("edge_width", 0)
        x1 = X1 - sec.get("edge_width", 0)
        p = Page(len(self.pages) + 1, self.section_map.get(sec["id"], sec["id"]), title, sec.get("instruction", ""),
                 x0, x1)
        self.pages.append(p)
        self.header(p, sec)
        return p

    def header(self, page, sec, y=None, title=None, instruction=None):
        """The title and instruction of a section, at the top of a page or, for a section that follows on the
        same page, at y. The id line ("XQT-HWR-EN v1 · page 3/8") is written by tex.py."""
        inline = y is not None
        if not inline:
            page.id_line_y = MARGIN + 1.6
            y = MARGIN + 4.6
        title = title or page.title
        text_node(page, X0, y + 3.2, title, 14, anchor="west", bold=True,
                  alloc=(X1 - X0, 6.4), poly=Frame(0, 0).rect(X0, y, X1, y + 6.4), prefix="title")
        y += 7.6
        instr = (instruction if instruction is not None else page.instruction).strip()
        if instr:
            h = para_height(instr, X1 - X0, 9)
            para_node(page, X0, y, X1 - X0, h, tex_escape(instr), pt=9, color="xqtnote", prefix="instr")
            y += h
        page.y = y + 4.0
        if not inline:
            page.top = page.y

    @staticmethod
    def header_height(sec):
        h = 7.6 + 4.0
        if sec.get("instruction", "").strip():
            h += para_height(sec["instruction"].strip(), X1 - X0, 9)
        return h

    def flow(self, section, pieces):
        page = None
        last = self.pages[-1] if self.pages else None
        if section.get("same_page") and last is not None and pieces and not section.get("edge_width"):
            # a short section follows on the page before when its header and first part fit there
            need = 6.0 + self.header_height(section) + pieces[0].height
            if pieces[0].keep_with_next and len(pieces) > 1:
                need += pieces[0].gap + pieces[1].height
            if last.y + need <= BOTTOM and last.x0 == X0 and last.x1 == X1:
                page = last
                self.header(page, section, y=page.y + 6.0, title=section["title"],
                            instruction=section.get("instruction", ""))
        if page is None:
            page = self.new_page(section)
        first = page
        pending = list(pieces)
        i = 0
        while i < len(pending):
            pc = pending[i]
            need = pc.height
            if pc.keep_with_next and i + 1 < len(pending):
                need += pc.gap + pending[i + 1].height
            if page.y + need > BOTTOM and page.y > self.content_top(page) + 0.5:
                page = self.new_page(section, continued=True)
            pc.place(page, page.y)
            page.y += pc.height + pc.gap
            i += 1
        return first, page

    def content_top(self, page):
        return page.top


# ---------------------------------------------------------------------------------------------------- blocks

def make_item(form, sec, spec, **over):
    it = Item(spec, form.section_map.get(sec["id"], sec["id"]), (spec or {}).get("lang", form.lang), **over)
    if sec["id"] in form.section_map:
        # a chapter mapped onto one section (the German chapter as H): keep where it came from as a tag
        tag = "%s-%s" % (form.lang, sec["id"])
        if tag not in it.tags:
            it.tags.append(tag)
    return it


def add_item(page, item, parent=None):
    item.page = page.number
    item.parent = parent
    page.items.append(item)
    return item


def block_heading(form, sec, block):
    text = block.get("heading")
    if not text:
        return []

    def place(page, y):
        text_node(page, page.x0, y + 2.6, text, 11, bold=True, alloc=(page.x1 - page.x0, 5.4),
                  poly=Frame(0, 0).rect(page.x0, y, page.x1, y + 5.4), prefix="heading")
        note = block.get("note")
        if note:
            h = para_height(note, page.x1 - page.x0, 9)
            para_node(page, page.x0, y + 6.0, page.x1 - page.x0, h, tex_escape(note), pt=9, color="xqtnote")

    h = 5.4
    if block.get("note"):
        h += 0.6 + para_height(block["note"], X1 - X0 - 2 * sec.get("edge_width", 0), 9)
    return [Piece(h, place, keep_with_next=True, gap=2.0)]


def block_para(form, sec, block):
    pt = block.get("size", 10)
    paras = block.get("paragraphs", [])
    bullets = block.get("bullets", [])
    w = X1 - X0 - 2 * sec.get("edge_width", 0)
    pieces = []
    for p in paras:
        h = para_height(p, w, pt)
        pieces.append(Piece(h, lambda page, y, p=p, h=h: para_node(page, page.x0, y, page.x1 - page.x0, h,
                                                                     tex_escape(p), pt=pt)))
    for b in bullets:
        h = para_height(b, w - 6, pt)

        def place(page, y, b=b, h=h):
            text_node(page, page.x0 + 1.5, y + pt * PT * 0.6, "•", pt, alloc=(4, h),
                      poly=Frame(0, 0).rect(page.x0, y, page.x0 + 5, y + h))
            para_node(page, page.x0 + 6, y, page.x1 - page.x0 - 6, h, tex_escape(b), pt=pt)
        pieces.append(Piece(h, place, gap=1.0))
    if pieces:
        pieces[-1].gap = ROW_GAP
    return pieces


def item_prints(page, it):
    """The head row (arrow, prompt, id) and side prompt of a placed item as printed things."""
    first = len(page.prints)
    _item_prints(page, it)
    for pr in page.prints[first:]:
        pr.owner = it


def _item_prints(page, it):
    f = it.frame
    u0, v0, u1, v1 = it.head_rect()
    vm = (v0 + v1) / 2
    hh = v1 - v0
    u = u0
    if it.arrow:
        a, b = f.point(u + 0.5, vm), f.point(u + ARROW_W - 2, vm)
        tex = r"\draw[xqtarrow] (%s,%s) -- (%s,%s);" % (fmt(a[0]), fmt(a[1]), fmt(b[0]), fmt(b[1]))
        page.prints.append(Printed(f.rect(u, v0, u + ARROW_W, v1), tex, "line"))
        u += ARROW_W
    if it.prompt_mode == "above" and (it.prompt or it.prompt_tex):
        x, y = f.point(u, vm)
        aw = u1 - ID_W - u
        text_node(page, x, y, it.prompt_tex or it.prompt, it.prompt_pt, angle=it.angle, alloc=(aw, hh),
                  poly=f.rect(u, v0, u1 - ID_W, v1), mono=it.mono, raw=bool(it.prompt_tex), prefix="prompt")
    side = it.side_rect()
    if not side:
        # the box id, at the end of the head row
        x, y = f.point(u1, vm)
        text_node(page, x, y, it.id or "?", 6, anchor="east", angle=it.angle, alloc=(ID_W, hh),
                  poly=f.rect(u1 - ID_W, v0, u1, v1), color="xqtid", prefix="id:%s" % (it.id or "?"))
        return
    # a prompt beside the box: the id at the top of the side region's right end (next to the box for a prompt on
    # the left, at the end of the line for one on the right), the prompt in the rest
    su0, sv0, su1, sv1 = side
    iu0, iu1, pu0, pu1 = su1 - ID_W, su1, su0, su1 - ID_W
    x, y = f.point(iu1, sv0 + 1.6)
    text_node(page, x, y, it.id or "?", 6, anchor="east", angle=it.angle, alloc=(ID_W, 3.2),
              poly=f.rect(iu0, sv0, iu1, sv0 + 3.2), color="xqtid", prefix="id:%s" % (it.id or "?"))
    x, y = f.point(pu0, (sv0 + sv1) / 2)
    text_node(page, x, y, it.prompt_tex or it.prompt, it.prompt_pt or 10, anchor="west", angle=it.angle,
              alloc=(pu1 - pu0, sv1 - sv0), poly=f.rect(pu0, sv0, pu1, sv1), raw=bool(it.prompt_tex),
              mono=it.mono, prefix="side")


def block_lines(form, sec, block):
    """Full-width lines, one per row (running text, pangrams, warm-up lines); optional bullets or an indent."""
    pieces = []
    bullet = block.get("bullet")
    indent = block.get("indent", 6 if bullet else 0)
    defaults = block.get("defaults", {})
    for spec in block["items"]:
        spec = dict(defaults, **(spec if isinstance(spec, dict) else {"text": spec}))
        spec.setdefault("kind", "line")
        probe = make_item(form, sec, spec, width=10)
        h = probe.depth()

        def place(page, y, spec=spec):
            w = spec.get("width", page.x1 - page.x0 - indent)
            it = make_item(form, sec, spec, width=w)
            it.place(page.x0 + indent, y)
            add_item(page, it)
            if bullet:
                cy = it.frame.cy
                text_node(page, page.x0 + 1, cy, "•", 12, alloc=(4, 5),
                          poly=Frame(0, 0).rect(page.x0, cy - 2.5, page.x0 + 4.5, cy + 2.5))
        pieces.append(Piece(h, place))
    return pieces


def block_rows(form, sec, block):
    """Boxes packed left to right in rows (characters, words, numbers), each as wide as its text needs."""
    defaults = block.get("defaults", {})
    specs = [dict(defaults, **(s if isinstance(s, dict) else {"text": s})) for s in block["items"]]
    width = X1 - X0 - 2 * sec.get("edge_width", 0)
    gap = block.get("gap", COL_GAP)
    rows, row, used = [], [], 0.0
    for s in specs:
        it = make_item(form, sec, s)
        if it.w > width:
            it.w = width
            it.head_w = max(it.w, it.head_need())
        fw = max(it.w, it.head_w)
        if row and used + gap + fw > width + 1e-6:
            rows.append(row)
            row, used = [], 0.0
        used += (gap if row else 0) + fw
        row.append((s, fw, it.depth()))
    if row:
        rows.append(row)
    pieces = []
    for row in rows:
        h = max(d for _, _, d in row)
        # spread the row's spare width over its gaps when asked (a full row looks tidier)
        def place(page, y, row=row):
            x = page.x0
            spare = (page.x1 - page.x0) - sum(fw for _, fw, _ in row) - gap * (len(row) - 1)
            full = spare < 0.25 * (page.x1 - page.x0)
            extra = spare / (len(row) - 1) if block.get("justify") and full and len(row) > 1 else 0
            for s, fw, d in row:
                it = make_item(form, sec, s)
                if it.w > page.x1 - page.x0:
                    it.w = page.x1 - page.x0
                    it.head_w = max(it.w, it.head_need())
                it.place(x, y)
                add_item(page, it)
                x += fw + gap + extra
        pieces.append(Piece(h, place))
    return pieces


def group_geometry(items, gap):
    """A group of parallel boxes (one angle): their width, the group's height, and each item's centre offset."""
    W = max(max(it.w, it.head_w) for it in items)
    H = sum(it.depth() for it in items) + gap * (len(items) - 1)
    offs, v = [], -H / 2
    for it in items:
        top = v
        cv = top + it.head_h() + HEAD_GAP + it.h / 2
        offs.append((-W / 2 + it.w / 2, cv))
        v += it.depth() + gap
    return W, H, offs


def block_angles(form, sec, block):
    """Groups of parallel lines at one angle each, the groups packed into shelves by their turned bounding boxes."""
    gap = block.get("line_gap", 3.0)
    width = X1 - X0 - 2 * sec.get("edge_width", 0)
    shelf_gap = block.get("gap", 6.0)
    groups = []
    for g in block["groups"]:
        a = float(g["angle"])
        specs = [dict(g.get("defaults", {}), **(s if isinstance(s, dict) else {"text": s})) for s in g["items"]]
        for s in specs:
            s.setdefault("kind", "line")
            s["angle"] = a
            s.setdefault("arrow", True)
        probe = [make_item(form, sec, s) for s in specs]
        # one width for the whole group, so its lines line up
        if g.get("same_width", True):
            same_w = max(it.w for it in probe)
            for s in specs:
                s.setdefault("width", same_w)
        probe = [make_item(form, sec, s) for s in specs]
        W, H, _ = group_geometry(probe, gap)
        poly = Frame(0, 0, a).rect(-W / 2, -H / 2, W / 2, H / 2)
        x0, y0, x1, y1 = aabb(poly)
        groups.append((a, specs, x1 - x0, y1 - y0))
    # shelves in the order given
    shelves, shelf, used = [], [], 0.0
    for g in groups:
        if shelf and used + shelf_gap + g[2] > width + 1e-6:
            shelves.append(shelf)
            shelf, used = [], 0.0
        used += (shelf_gap if shelf else 0) + g[2]
        shelf.append(g)
    if shelf:
        shelves.append(shelf)
    pieces = []
    for shelf in shelves:
        h = max(g[3] for g in shelf)

        def place(page, y, shelf=shelf, h=h):
            total = sum(g[2] for g in shelf)
            spare = (page.x1 - page.x0) - total
            sgap = spare / (len(shelf) + 1) if len(shelf) > 0 else 0
            x = page.x0 + sgap
            for a, specs, bw, bh in shelf:
                cx, cy = x + bw / 2, y + h / 2
                items = [make_item(form, sec, s) for s in specs]
                W, H, offs = group_geometry(items, gap)
                gf = Frame(cx, cy, a)
                for it, (u, v) in zip(items, offs):
                    px, py = gf.point(u, v)
                    it.place(px, py, a)
                    add_item(page, it)
                x += bw + sgap
        pieces.append(Piece(h, place))
    return pieces


def place_edge_notes(form, sec, pages):
    """Vertical notes along the page edges of a section's pages: -90 (upwards) on the left, 90 on the right."""
    notes = [dict(n) for n in sec.get("edge_notes", [])]
    left = [n for n in notes if float(n["angle"]) < 0]
    right = [n for n in notes if float(n["angle"]) > 0]
    for i, page in enumerate(pages):
        top = page.top
        for side, lst in ((-1, left), (1, right)):
            if i >= len(lst):
                continue
            spec = dict(lst[i])
            spec.setdefault("kind", "line")
            spec.setdefault("arrow", True)
            spec.setdefault("tags", []).append("edge")
            it = make_item(form, sec, spec)
            d = it.depth()
            band = BOTTOM - top
            it.w = min(it.w, band - 4)
            it.head_w = max(it.w, it.head_need())
            cy = top + band / 2
            # the box's centre: the head row lies outside it (towards the page edge)
            if side < 0:
                cx = X0 + d - it.h / 2
            else:
                cx = X1 - d + it.h / 2
            it.place(cx, cy, float(spec["angle"]))
            add_item(page, it)


def block_bars(form, sec, block):
    """A printed bar chart with a label written upwards inside each bar (a turned label inside a box)."""
    specs = block["items"]
    probe = [make_item(form, sec, dict(s, kind="label", angle=-90, arrow=True)) for s in specs]
    depth = max(p.depth() for p in probe)
    bar_w = depth + 6
    heights = block.get("heights", [70, 90, 80, 60])
    H = max(heights[: len(specs)]) + 6
    caption = block.get("caption", "")

    def place(page, y):
        n = len(specs)
        gap = 12.0
        total = n * bar_w + (n - 1) * gap
        x = page.x0 + (page.x1 - page.x0 - total) / 2
        base = y + H
        line_print(page, ((x - 6, base), (x + total + 6, base)), "xqtline")
        for i, s in enumerate(specs):
            bh = heights[i % len(heights)]
            rect_print(page, x, base - bh, bar_w, bh, "xqtline")
            it = make_item(form, sec, dict(s, kind="label", angle=-90, arrow=True, guides=False,
                                           width=bh - 8, context=caption))
            d = it.depth()
            # -90: the head row is to the left of the box; centre the footprint in the bar
            cx = x + (bar_w - d) / 2 + d - it.h / 2
            cy = base - bh / 2
            it.place(cx, cy, -90)
            add_item(page, it)
            x += bar_w + gap
    return [Piece(H, place)]


def block_annotate(form, sec, block):
    """A printed paragraph to annotate: margin notes left and right, an insertion between two printed lines,
    a note at the paragraph's end."""
    body_x0, body_x1 = 54.0, 156.0
    bw = body_x1 - body_x0
    pt = block.get("size", 10)
    p1, p2 = block["paragraph1"], block["paragraph2"]
    context = (p1 + " " + p2).strip()
    h1, h2 = para_height(p1, bw, pt), para_height(p2, bw, pt)
    ins = make_item(form, sec, dict(block["insertion"], kind="line", x_height=2, guides=False,
                                    tags=["insertion"], context=context))
    ins_gap = ins.depth() + 3
    end = make_item(form, sec, dict(block["end_note"], kind="line", tags=["end-note"], context=context,
                                    width=bw))
    body_h = h1 + ins_gap + h2 + 3 + end.depth()
    margin_specs = [(-1, s) for s in block.get("margin_left", [])] + [(1, s) for s in block.get("margin_right", [])]
    m_probe = [make_item(form, sec, dict(s, kind="line", x_height=2, guides=False, width=36)) for _, s in margin_specs]
    m_depth = max(p.depth() for p in m_probe) if m_probe else 0
    per_side = max(len(block.get("margin_left", [])), len(block.get("margin_right", [])), 1)
    H = max(body_h, per_side * (m_depth + 8))

    def place(page, y):
        para_node(page, body_x0, y, bw, h1, tex_escape(p1), pt=pt)
        yi = y + h1 + 1.5
        ins.place(body_x0 + 20, yi)
        add_item(page, ins)
        y2 = y + h1 + ins_gap
        para_node(page, body_x0, y2, bw, h2, tex_escape(p2), pt=pt)
        end.place(body_x0, y2 + h2 + 3)
        add_item(page, end)
        for side in (-1, 1):
            specs = [s for sd, s in margin_specs if sd == side]
            if not specs:
                continue
            step = H / len(specs)
            for k, s in enumerate(specs):
                x = 12.0 if side < 0 else body_x1 + 6
                it = make_item(form, sec, dict(s, kind="line", x_height=2, guides=False, width=36,
                                               tags=["margin", "margin-left" if side < 0 else "margin-right"],
                                               context=context))
                it.place(x, y + k * step + 2)
                add_item(page, it)
    return [Piece(H, place)]


def block_columns(form, sec, block):
    """A note in two columns."""
    cols = block["columns"]
    gap = 10.0
    n = max(len(c) for c in cols)
    probe = make_item(form, sec, {"text": "x", "kind": "line"}, width=10)
    pieces = []
    for r in range(n):
        def place(page, y, r=r):
            w = (page.x1 - page.x0 - gap) / 2
            for c, col in enumerate(cols):
                if r < len(col):
                    s = col[r] if isinstance(col[r], dict) else {"text": col[r]}
                    it = make_item(form, sec, dict(s, kind="line", width=w,
                                                   tags=list(s.get("tags", [])) + ["column-%d" % (c + 1)]))
                    it.place(page.x0 + c * (w + gap), y)
                    add_item(page, it)
        pieces.append(Piece(probe.depth(), place))
    return pieces


def block_table(form, sec, block):
    """A printed table to copy into an empty grid of the same shape, one box per cell."""
    rows = block["rows"]
    nr, nc = len(rows), len(rows[0])
    cw, rh = 52.0, 6.0
    ref_h = nr * rh
    cell_pad = 1.6
    probe = make_item(form, sec, {"text": "x", "kind": "word", "prompt_mode": "none"}, width=10)
    cell_h = probe.depth() + 2 * cell_pad
    grid_h = nr * cell_h
    H = ref_h + 5 + grid_h
    context = " | ".join(" ".join(r) for r in rows)

    def place(page, y):
        x0 = page.x0 + (page.x1 - page.x0 - nc * cw) / 2
        # the printed table
        for r, row in enumerate(rows):
            for c, cell in enumerate(row):
                cx = x0 + c * cw + 2
                cy = y + r * rh + rh / 2
                text_node(page, cx, cy, cell, 10, alloc=(cw - 4, rh - 1.2), bold=(r == 0),
                          poly=Frame(0, 0).rect(x0 + c * cw, y + r * rh + 0.6, x0 + (c + 1) * cw, y + (r + 1) * rh - 0.6))
        line_print(page, ((x0, y + rh), (x0 + nc * cw, y + rh)), "xqtrule")
        # the grid to fill
        gy = y + ref_h + 5
        for r in range(nr + 1):
            line_print(page, ((x0, gy + r * cell_h), (x0 + nc * cw, gy + r * cell_h)), "xqtline")
        for c in range(nc + 1):
            line_print(page, ((x0 + c * cw, gy), (x0 + c * cw, gy + grid_h)), "xqtline")
        for r, row in enumerate(rows):
            for c, cell in enumerate(row):
                kind = "number" if re.fullmatch(r"[\d.,:%+\- ]+( ?[a-zA-Zµ°]{1,2})?", cell) else "word"
                it = make_item(form, sec, {"text": cell, "kind": kind, "prompt_mode": "none",
                                           "width": cw - 2 * cell_pad, "guides": False,
                                           "tags": ["table", "table-r%dc%d" % (r + 1, c + 1)], "context": context})
                it.place(x0 + c * cw + cell_pad, gy + r * cell_h + cell_pad)
                add_item(page, it)
    return [Piece(H, place)]


def block_flowchart(form, sec, block):
    """A flow chart to draw: labels written in a row inside a drawing area; frames and arrows are drawn by hand."""
    labels = block["labels"]
    H = block.get("height", 46)
    dspec = {"kind": "drawing", "prompt": block.get("prompt", ""), "tags": ["flowchart"], "guides": False}
    probe = make_item(form, sec, dspec, width=10)

    def place(page, y):
        W = page.x1 - page.x0
        d = make_item(form, sec, dict(dspec, width=W, height=H - probe.head_h() - HEAD_GAP))
        d.place(page.x0, y)
        add_item(page, d)
        n = len(labels)
        lw = max(make_item(form, sec, dict(s if isinstance(s, dict) else {"text": s}, kind="label")).w
                 for s in labels)
        gap = (W - n * lw) / (n + 1)
        for k, s in enumerate(labels):
            s = s if isinstance(s, dict) else {"text": s}
            it = make_item(form, sec, dict(s, kind="label", width=lw, guides=False,
                                           tags=list(s.get("tags", [])) + ["flowchart"]))
            top = d.frame.cy - it.depth() / 2
            it.place(page.x0 + gap + k * (lw + gap), top)
            add_item(page, it, parent=d)
    return [Piece(H, place)]


def block_marks(form, sec, block):
    """Marks: printed words to circle, underline and strike out; a written word to circle; a written word to
    underline."""
    printed = block.get("printed", [])      # [{word, mark: circle|underline|strike, prompt}]
    written = block.get("written", [])      # [{text, mark: circle|underline, prompt}]
    pieces = []
    mh = 16.0
    mprobe = make_item(form, sec, {"kind": "mark", "prompt": "x"}, width=10, height=mh)
    if printed:
        def place(page, y):
            n = len(printed)
            W = page.x1 - page.x0
            mw = (W - (n - 1) * COL_GAP) / n
            for k, p in enumerate(printed):
                it = make_item(form, sec, {"kind": "mark", "prompt": p["prompt"], "width": mw, "height": mh,
                                           "guides": False, "tags": ["printed-word", p["mark"]],
                                           "context": p["word"]})
                it.place(page.x0 + k * (mw + COL_GAP), y)
                add_item(page, it)
                cx, cy = it.frame.cx, it.frame.cy
                tw = tex_width(p["word"], 14) + 2
                text_node(page, cx, cy, p["word"], 14, anchor="center", alloc=(tw, 7),
                          poly=Frame(0, 0).rect(cx - tw / 2, cy - 3.5, cx + tw / 2, cy + 3.5), allow=[it])
        pieces.append(Piece(mprobe.depth(), place))
    # the written words with their marks, side by side in one row
    parts = []   # (width, height, place(page, x, y))
    for w in written:
        if w["mark"] == "circle":
            word = dict(w, kind="word", tags=list(w.get("tags", [])) + ["circled"], prompt=w["prompt_word"])
            wi = make_item(form, sec, word)
            pad = 6.0
            mspec = {"kind": "mark", "prompt": w["prompt"], "width": max(wi.w + 2 * pad + 24, 70),
                     "height": wi.depth() + 2 * pad, "guides": False, "tags": ["circle"]}
            mp = make_item(form, sec, mspec)

            def place(page, x, y, word=word, mspec=mspec):
                m = make_item(form, sec, mspec)
                m.place(x, y)
                add_item(page, m)
                it = make_item(form, sec, word)
                it.place(m.frame.cx - it.w / 2, m.frame.cy - it.depth() / 2)
                add_item(page, it, parent=m)
            parts.append((max(mp.w, mp.head_w), mp.depth(), place))
        else:  # underline: the word, a strip under it for the line, and the instruction beside them
            word = dict(w, kind="word", tags=list(w.get("tags", [])) + ["underlined"], prompt=w["prompt_word"])
            wi = make_item(form, sec, word)
            strip = {"kind": "mark", "prompt_mode": "none", "width": wi.w, "height": 7.0, "guides": False,
                     "tags": ["underline"]}
            sp = make_item(form, sec, strip)
            note_w = tex_width(w["prompt"], 9) + 2
            h = wi.depth() + 0.3 + sp.depth()

            def place(page, x, y, word=word, strip=strip, w=w, note_w=note_w):
                it = make_item(form, sec, word)
                it.place(x, y)
                add_item(page, it)
                m = make_item(form, sec, strip)
                m.place(x, y + it.depth() + 0.3)   # right under the word: its id row sits in the gap
                add_item(page, m)
                nx = x + max(it.w, it.head_w) + 3
                text_node(page, nx, m.frame.cy, w["prompt"], 9, color="xqtnote", alloc=(note_w, 5),
                          poly=Frame(0, 0).rect(nx, m.frame.cy - 2.5, nx + note_w, m.frame.cy + 2.5))
            parts.append((max(wi.w, wi.head_w) + 3 + note_w, h, place))
    if parts:
        H = max(p[1] for p in parts)

        def place_row(page, y):
            x = page.x0
            for w_, h_, pl in parts:
                pl(page, x, y)
                x += w_ + 2 * COL_GAP
        pieces.append(Piece(H, place_row))
    return pieces


def block_drawings(form, sec, block):
    """Drawing areas side by side (a sketch, a hatched area): no text expected."""
    specs = block["items"]
    H = block.get("height", 50)
    probe = make_item(form, sec, dict(specs[0], kind="drawing"), height=H)

    def place(page, y):
        n = len(specs)
        W = page.x1 - page.x0
        w = (W - (n - 1) * COL_GAP) / n
        for k, s in enumerate(specs):
            it = make_item(form, sec, dict(s, kind="drawing", width=w, height=H, guides=False))
            it.place(page.x0 + k * (w + COL_GAP), y)
            add_item(page, it)
    return [Piece(probe.depth(), place)]


def block_plot(form, sec, block):
    """An axis plot to draw, with axis labels written beside it (the y label upwards)."""
    pw, ph = block.get("width", 110), block.get("height", 62)
    xl = block["x_label"]
    yl = block["y_label"]
    title = block.get("title")
    yprobe = make_item(form, sec, dict(yl, kind="label", angle=-90, guides=False))
    ydepth = yprobe.depth()
    xprobe = make_item(form, sec, dict(xl, kind="label", guides=False))
    dprobe = make_item(form, sec, {"kind": "drawing", "prompt": block["prompt"]}, width=pw, height=ph)
    tprobe = make_item(form, sec, dict(title, kind="label", guides=False)) if title else None
    H = (tprobe.depth() + ROW_GAP if tprobe else 0) + dprobe.depth() + 2 + xprobe.depth()

    def place(page, y):
        x0 = page.x0 + (page.x1 - page.x0 - (ydepth + 4 + pw)) / 2
        px = x0 + ydepth + 4
        yy = y
        if title:
            t = make_item(form, sec, dict(title, kind="label", guides=False, tags=["plot", "plot-title"]))
            t.place(px + (pw - t.w) / 2, yy)
            add_item(page, t)
            yy += t.depth() + ROW_GAP
        d = make_item(form, sec, {"kind": "drawing", "prompt": block["prompt"], "width": pw, "height": ph,
                                  "guides": False, "tags": ["plot"]})
        d.place(px, yy)
        add_item(page, d)
        box_top = yy + d.head_h() + HEAD_GAP
        ylab = make_item(form, sec, dict(yl, kind="label", guides=False, tags=["plot", "axis-label", "y-axis"]))
        ylab.w = min(ylab.w, ph)
        ylab.head_w = max(ylab.w, ylab.head_need())
        ylab.place(x0 + ydepth - ylab.h / 2, box_top + ph / 2, -90)
        add_item(page, ylab)
        xlab = make_item(form, sec, dict(xl, kind="label", guides=False, tags=["plot", "axis-label", "x-axis"]))
        xlab.place(px + (pw - xlab.w) / 2, box_top + ph + 2)
        add_item(page, xlab)
    return [Piece(H, place)]


def block_formulas(form, sec, block):
    """Formulas: typeset on the left, a box to write them on the right."""
    side_w = block.get("side_w", 72)
    pieces = []
    for s in block["items"]:
        spec = dict(s, kind="math", prompt_mode="left", side_w=side_w, prompt_tex="$\\displaystyle " + s["latex"] + "$",
                    guides=s.get("guides", True), prompt_pt=12)
        spec.setdefault("height", 12)
        spec.setdefault("search", [])
        probe = make_item(form, sec, spec, width=10)
        h = probe.depth()

        def place(page, y, spec=spec):
            x = page.x0 + side_w + COL_GAP / 2
            it = make_item(form, sec, dict(spec, width=page.x1 - x))
            it.place(x, y)
            add_item(page, it)
        pieces.append(Piece(h, place))
    return pieces


def block_ticks(form, sec, block):
    """Tick boxes with a printed choice to the right (kind free: not checked)."""
    pieces = []
    for s in block["items"]:
        spec = {"kind": "free", "prompt": s["text"], "prompt_mode": "right", "side_w": 170, "width": 6,
                "height": 6, "guides": False, "prompt_pt": 9, "tags": list(s.get("tags", [])) + ["tick"]}
        probe = make_item(form, sec, spec)

        def place(page, y, spec=spec):
            it = make_item(form, sec, dict(spec, side_w=page.x1 - page.x0 - 8 - 2))
            it.place(page.x0, y)
            add_item(page, it)
        pieces.append(Piece(probe.depth(), place))
    return pieces


BLOCKS = {
    "para": block_para, "lines": block_lines, "rows": block_rows, "angles": block_angles, "bars": block_bars,
    "annotate": block_annotate, "columns": block_columns, "table": block_table, "flowchart": block_flowchart,
    "marks": block_marks, "drawings": block_drawings, "plot": block_plot, "formulas": block_formulas,
    "ticks": block_ticks,
}


def build_form(content, form_id=None, page_id=None, section_map=None, extra_sections=()):
    """Lays out a form; `extra_sections` are (content, section_map) of further chapters (the German one)."""
    MEASURES.n = 0
    form = Form(content, form_id=form_id, page_id=page_id, section_map=section_map)
    chapters = [(content, section_map or {})] + list(extra_sections)
    for chap, smap in chapters:
        form.lang = chap["language"]
        form.section_map = smap
        form.continued = chap.get("continued", " (continued)")
        for sec in chap["sections"]:
            sec = dict(sec)
            pieces = []
            for block in sec.get("blocks", []):
                pieces += block_heading(form, sec, block)
                pieces += BLOCKS[block["type"]](form, sec, block)
            start = len(form.pages)
            first, last = form.flow(sec, pieces)
            sec_pages = form.pages[start:]
            for p in sec_pages:
                p.lang = chap["language"]
            if sec.get("edge_notes"):
                place_edge_notes(form, sec, sec_pages)
    # ids: page.n in the order the boxes were made (reading order); then their printed head rows
    for p in form.pages:
        for n, it in enumerate(p.items, 1):
            it.id = "%d.%d" % (p.number, n)
        for it in p.items:
            item_prints(p, it)
    return form


def manifest(form):
    items = [it.manifest() for p in form.pages for it in p.items]
    return {
        "form": form.form_id, "version": form.content.get("version", 1), "language": form.content["language"],
        "page_size_mm": [int(PAGE_W), int(PAGE_H)], "pages": len(form.pages), "items": items,
    }
