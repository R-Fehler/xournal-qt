# Makes the "Handwriting sample" pages (qt/docs/handwriting-search.md, "Your handwriting as a dataset"): a .xopp per
# language with the sentences of sentences-<lang>.txt as numbered grey prompts (text, not ink) and room to write each
# one under it, ten to a page. Write every sentence once, on one line, under its prompt; then
#   xournal-qt-cli hwr-lines handwriting-sample-de.xopp --text sentences-de.txt --lang de --writer me --out <dir>
# gives the line dataset with the sentences as the lines' texts (matched in reading order).
# Run: python3 make_sample.py (writes handwriting-sample-en.xopp and handwriting-sample-de.xopp next to it).
import gzip, os
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 595.27559, 841.88976
PER_PAGE = 10
TOP, STEP = 96.0, 72.0  # the first prompt, the room per sentence (prompt + about 50 pt to write in)
TITLE = {"en": "Handwriting sample (English): write each sentence once, on one line, below it.",
         "de": "Handschriftprobe (Deutsch): jeden Satz einmal, in einer Zeile, darunter schreiben."}


def page(lang, sentences, first, number, pages):
    out = ['<page width="%.5f" height="%.5f">' % (W, H),
           '<background type="solid" color="#ffffffff" style="plain"/>', '<layer>',
           '<text font="Sans" size="11" x="40" y="40" color="#5f6368ff">%s (%d/%d)</text>'
           % (escape(TITLE[lang]), number, pages)]
    for i, s in enumerate(sentences):
        y = TOP + i * STEP
        out.append('<text font="Sans" size="10" x="40" y="%.1f" color="#9aa0a6ff">%d. %s</text>'
                   % (y, first + i + 1, escape(s)))
    out += ['</layer>', '</page>']
    return "\n".join(out)


for lang in ("en", "de"):
    with open(os.path.join(HERE, "sentences-%s.txt" % lang), encoding="utf-8") as f:
        sentences = [l.strip() for l in f if l.strip() and not l.startswith("#")]
    chunks = [sentences[i:i + PER_PAGE] for i in range(0, len(sentences), PER_PAGE)]
    body = "\n".join(page(lang, c, k * PER_PAGE, k + 1, len(chunks)) for k, c in enumerate(chunks))
    xml = ('<?xml version="1.0" standalone="no"?>\n<xournal creator="xournal-qt" fileversion="4">\n'
           '<title>Handwriting sample</title>\n%s\n</xournal>\n' % body)
    with open(os.path.join(HERE, "handwriting-sample-%s.xopp" % lang), "wb") as f:
        f.write(gzip.compress(xml.encode("utf-8"), mtime=0))  # (the same bytes every run)
