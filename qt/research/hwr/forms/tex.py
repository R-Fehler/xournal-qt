"""The LaTeX of a laid-out form: one TikZ overlay per page, placed on `current page` in millimetres.

Every printed text goes through \\xqtm or \\xqtp, which write its measured size into the log
("XQTM key width height depth"), so the build can check it against the region the layout allotted it.
"""
import re

from layout import baseline_at, fmt, tex_escape, X0, PT

PREAMBLE = r"""\documentclass{article}
\usepackage[a4paper, margin=0mm]{geometry}
\usepackage{fontspec}
\setmainfont{DejaVuSans.ttf}[Ligatures=TeXOff, BoldFont=DejaVuSans-Bold.ttf, ItalicFont=DejaVuSans-Oblique.ttf]
\newfontfamily\xqtmono{DejaVuSansMono.ttf}[Ligatures=TeXOff, BoldFont=DejaVuSansMono-Bold.ttf]
\usepackage{amsmath, amssymb}
\usepackage{tikz}
\usetikzlibrary{arrows.meta}
\usepackage{embedfile}
\usepackage[hidelinks, pdfcreator={xournal-qt handwriting forms}]{hyperref}
\pagestyle{empty}
\parindent=0pt
\pdfvariable suppressoptionalinfo \numexpr 32 + 64 + 512\relax
\definecolor{xqtprompt}{gray}{0.08}
\definecolor{xqtnote}{gray}{0.25}
\definecolor{xqtid}{gray}{0.45}
\definecolor{xqtboxc}{gray}{0.55}
\definecolor{xqtguidec}{gray}{0.72}
\tikzset{
  xqtt/.style={inner sep=0pt, outer sep=0pt},
  xqtbox/.style={draw=xqtboxc, line width=0.35pt},
  xqtdrawbox/.style={draw=xqtboxc, line width=0.35pt, dash pattern=on 2pt off 1.5pt},
  xqtguide/.style={draw=xqtguidec, line width=0.3pt},
  xqtline/.style={draw=black!60, line width=0.45pt},
  xqtrule/.style={draw=black!40, line width=0.3pt},
  xqtarrow/.style={draw=black!70, line width=0.6pt, -{Stealth[length=2mm, width=1.5mm]}},
}
\newsavebox\xqtb
% a measured one-line text, and a measured paragraph of a given width
\newcommand\xqtm[2]{\sbox\xqtb{#2}\typeout{XQTM #1 \the\wd\xqtb\space\the\ht\xqtb\space\the\dp\xqtb}\usebox\xqtb}
\newcommand\xqtp[3]{\sbox\xqtb{\parbox[t]{#2}{\raggedright\hyphenpenalty=10000 #3}}%
  \typeout{XQTM #1 \the\wd\xqtb\space\the\ht\xqtb\space\the\dp\xqtb}\usebox\xqtb}
"""


def page_tex(form, page):
    out = [r"\begin{tikzpicture}[remember picture, overlay]",
           r"\begin{scope}[shift={(current page.north west)}, x=1mm, y=-1mm]"]
    # the id line
    idl = "%s v%s · page %d/%d" % (form.page_id, form.content.get("version", 1), page.number, len(form.pages))
    out.append(r"\node[xqtt, text=xqtid, anchor=west] at (%s,%s) {\xqtm{idline-%d}{\fontsize{7}{8}\selectfont %s}};"
               % (fmt(X0), fmt(page.id_line_y), page.number, tex_escape(idl)))
    for it in page.items:
        poly = it.box_poly()
        style = "xqtdrawbox" if it.kind in ("drawing", "mark") else "xqtbox"
        out.append(r"\draw[%s] %s -- cycle;" % (style, " -- ".join("(%s,%s)" % (fmt(x), fmt(y)) for x, y in poly)))
        if it.guides:
            v = -it.h / 2 + baseline_at(it.h)
            a = it.frame.point(-it.w / 2 + 1.5, v)
            b = it.frame.point(it.w / 2 - 1.5, v)
            out.append(r"\draw[xqtguide] (%s,%s) -- (%s,%s);" % (fmt(a[0]), fmt(a[1]), fmt(b[0]), fmt(b[1])))
    for pr in page.prints:
        out.append(pr.tex)
    out += [r"\end{scope}", r"\end{tikzpicture}"]
    return "\n".join(out)


def document(form, manifest_name, title):
    parts = [PREAMBLE, r"\hypersetup{pdftitle={%s}}" % tex_escape(title), r"\begin{document}",
             r"\embedfile[desc={The boxes and texts of this form (DESIGN.md)}, mimetype=application/json]{%s}"
             % manifest_name]
    for p in form.pages:
        parts.append(r"\null" + page_tex(form, p) + r"\clearpage")
    parts.append(r"\end{document}")
    return "\n".join(parts) + "\n"


def parse_measures(log_text):
    """{key: (width, height+depth)} in mm from the XQTM lines of a LaTeX log (the last run's)."""
    out = {}
    # build.py sets max_print_line so lines are not wrapped; joining them keeps this working when they are
    text = log_text.replace("\n", "")
    for m in re.finditer(r"XQTM (\S+) ([\d.]+)pt ([\d.]+)pt ([\d.]+)pt", text):
        key, w, h, d = m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4))
        out[key] = (w * PT, (h + d) * PT)
    return out
