#include "InkText.h"

#include <algorithm>
#include <cmath>

#include "FuzzyQuery.h"
#include "WordMatch.h"

namespace xqt::ink {

size_t Word::bytes() const {
    return sizeof(Word) + static_cast<size_t>(text.capacity()) * 2 + candidates.capacity() * sizeof(Candidate);
}

QString folded(QStringView text) {
    QString out;
    textmatch::words(text, [&](qsizetype, qsizetype, QStringView w) { out += w; });
    return out;
}

Candidate candidate(QStringView text, float p) { return {words::idOf(folded(text)), p}; }

QPointF turned(QPointF p, double degrees) {
    double c = 0, s = 0;
    if (degrees == 0) {
        return p;
    } else if (degrees == 90) {
        s = 1;
    } else if (degrees == -90 || degrees == 270) {
        s = -1;
    } else if (degrees == 180 || degrees == -180) {
        c = -1;
    } else {
        const double r = degrees * M_PI / 180;
        c = std::cos(r);
        s = std::sin(r);
    }
    return {c * p.x() - s * p.y(), s * p.x() + c * p.y()};
}

QRectF Quad::boundingRect() const {
    double l = corners[0].x(), r = l, t = corners[0].y(), b = t;
    for (const QPointF& p: corners) {
        l = std::min(l, p.x());
        r = std::max(r, p.x());
        t = std::min(t, p.y());
        b = std::max(b, p.y());
    }
    return QRectF(QPointF(l, t), QPointF(r, b));
}

Quad quadOf(const QRectF& box, double angle) {
    Quad q{{box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()}};
    if (angle != 0) {
        const QPointF c = box.center();
        for (QPointF& p: q) {
            p = c + turned(p - c, angle);
        }
    }
    return q;
}

Quad quadOf(const Word& w) { return quadOf(w.box, w.angle); }

QRectF boundsOf(const Word& w) { return w.angle == 0 ? w.box : quadOf(w).boundingRect(); }

size_t LineResult::bytes() const {
    size_t b = sizeof(LineResult);
    for (const Word& w: words) {
        b += w.bytes();
    }
    return b;
}

std::shared_ptr<const PageText> PageText::assemble(const std::vector<PlacedLine>& lines) {
    auto page = std::make_shared<PageText>();
    size_t count = 0;
    for (const PlacedLine& l: lines) {
        count += l.result ? l.result->words.size() : 0;
    }
    page->words.reserve(count);
    for (const PlacedLine& l: lines) {
        if (!l.result || l.result->words.empty()) {
            continue;
        }
        page->lineStarts.push_back(static_cast<uint32_t>(page->words.size()));
        for (const Word& w: l.result->words) {
            page->words.push_back(w);
            Word& placed = page->words.back();
            if (l.angle == 0) {
                placed.box.translate(l.origin);
            } else {
                // (upright around its middle, which is turned with the line around its origin)
                placed.box.moveCenter(l.origin + turned(w.box.center(), l.angle));
                placed.angle = static_cast<float>(l.angle);
            }
        }
    }
    return page;
}

QString PageText::text() const {
    QString out;
    size_t line = 0;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            const bool newLine = line + 1 < lineStarts.size() && lineStarts[line + 1] == i;
            out += newLine ? u'\n' : u' ';
            line += newLine ? 1 : 0;
        }
        out += words[i].text;
    }
    return out;
}

size_t PageText::bytes() const {
    size_t b = sizeof(PageText) + lineStarts.capacity() * sizeof(uint32_t) +
               (words.capacity() - words.size()) * sizeof(Word);
    for (const Word& w: words) {
        b += w.bytes();
    }
    return b;
}

// --- matching ---------------------------------------------------------------------------------------------------

namespace {
/// One word of a term, matched against the readings of one ink word.
struct Piece {
    std::shared_ptr<const words::Matches> matches;
    bool sure = false;  ///< a term of 1-2 letters alone: only the whole best reading of a word the recogniser is sure of
};
using Plan = std::vector<Piece>;

Plan planOf(const textmatch::Term& term, int typos) {
    Plan plan;
    if (term.bounds & textmatch::Regex) {
        return plan;  // (a regular expression is matched in text, not in the words read from handwriting)
    }
    if (term.bounds & textmatch::Fuzzy) {
        plan.push_back({words::Matches::of(term), false});
        return plan;
    }
    std::vector<QString> list;
    textmatch::words(term.text, [&](qsizetype, qsizetype, QStringView w) { list.push_back(w.toString()); });
    const bool single = list.size() == 1;
    for (const QString& w: list) {
        const bool longWord = w.size() >= wordmatch::MIN_LETTERS;
        unsigned bounds = textmatch::Word;  // (a short word of a phrase: a whole reading)
        if (single && (term.bounds & textmatch::Word) != 0) {
            bounds = term.bounds & textmatch::Word;  // ^term, term$, 'term'
        } else if (longWord) {
            bounds = textmatch::Fuzzy | textmatch::TypoOnly | textmatch::typoBits(typos);
        }
        plan.push_back({words::Matches::of({w, bounds}), single && !longWord});
    }
    return plan;
}

struct WordHit {
    bool ok = false;
    bool exact = false;
    float p = 0;
};

WordHit matchWord(const Word& w, const Piece& piece) {
    WordHit h;
    if (w.candidates.empty()) {
        return h;
    }
    if (piece.sure) {
        const Candidate& best = w.candidates.front();
        if (w.conf >= SHORT_CONF && best.p >= EXACT_P && piece.matches->quality(best.word) != wordmatch::None) {
            h = {true, true, best.p};
        }
        return h;
    }
    for (size_t i = 0; i < w.candidates.size(); ++i) {
        const Candidate& c = w.candidates[i];
        if (i > 0 && c.p < MIN_P) {
            continue;
        }
        const wordmatch::Quality q = piece.matches->quality(c.word);
        if (q == wordmatch::None) {
            continue;
        }
        h.ok = true;
        h.p += c.p;
        if (i == 0 && q == wordmatch::Exact && c.p >= EXACT_P) {
            h.exact = true;
        }
    }
    h.p = std::min(h.p, 1.0f);
    return h;
}
}  // namespace

std::vector<Hit> find(const PageText& ink, const std::vector<textmatch::Term>& terms, int typos) {
    std::vector<Hit> out;
    if (ink.words.empty() || terms.empty()) {
        return out;
    }
    if (typos < 0) {
        typos = FuzzyQuery::typoTolerance();
    }
    std::vector<Plan> plans;
    for (const textmatch::Term& t: terms) {
        if (Plan p = planOf(t, typos); !p.empty()) {
            plans.push_back(std::move(p));
        }
    }
    const size_t n = ink.words.size();
    for (size_t i = 0; i < n;) {
        Hit best;
        size_t bestLength = 0;
        for (const Plan& plan: plans) {
            const size_t length = plan.size();
            if (length <= bestLength || i + length > n) {
                continue;
            }
            Hit h{static_cast<uint32_t>(i), static_cast<uint32_t>(i + length - 1), true, 1.0f};
            bool ok = true;
            for (size_t k = 0; k < length && ok; ++k) {
                const WordHit w = matchWord(ink.words[i + k], plan[k]);
                ok = w.ok;
                h.exact = h.exact && w.exact;
                h.p = std::min(h.p, w.p);
            }
            if (ok) {
                best = h;
                bestLength = length;
            }
        }
        if (bestLength > 0) {
            out.push_back(best);
            i += bestLength;
        } else {
            ++i;
        }
    }
    return out;
}

bool contains(const PageText& ink, const textmatch::Term& term, int typos) {
    return !find(ink, {term}, typos).empty();
}

namespace {
/// The marked parts of a hit, one per line: the box of its words, MARK_MARGIN bigger, in the frame of the line (the
/// page turned by -angle around (0, 0)), and that angle.
std::vector<std::pair<QRectF, double>> partsOf(const PageText& ink, const Hit& hit) {
    std::vector<std::pair<QRectF, double>> out;
    QRectF box;
    double angle = 0;
    bool open = false;
    auto close = [&] {
        out.emplace_back(box.adjusted(-MARK_MARGIN, -MARK_MARGIN, MARK_MARGIN, MARK_MARGIN), angle);
        open = false;
    };
    for (uint32_t i = hit.first; i <= hit.last && i < ink.words.size(); ++i) {
        const Word& w = ink.words[i];
        if (open && (std::binary_search(ink.lineStarts.begin(), ink.lineStarts.end(), i) || w.angle != angle)) {
            close();
        }
        QRectF framed = w.box;
        if (w.angle != 0) {
            framed.moveCenter(turned(w.box.center(), -w.angle));
        }
        box = open ? box.united(framed) : framed;
        angle = w.angle;
        open = true;
    }
    if (open) {
        close();
    }
    return out;
}

Quad quadOfPart(const QRectF& framed, double angle) {
    Quad q{{framed.topLeft(), framed.topRight(), framed.bottomRight(), framed.bottomLeft()}};
    for (QPointF& p: q) {
        p = turned(p, angle);
    }
    return q;
}
}  // namespace

std::vector<QRectF> rectsOf(const PageText& ink, const Hit& hit) {
    std::vector<QRectF> out;
    for (const auto& [box, angle]: partsOf(ink, hit)) {
        out.push_back(angle == 0 ? box : quadOfPart(box, angle).boundingRect());
    }
    return out;
}

std::vector<Quad> quadsOf(const PageText& ink, const Hit& hit) {
    std::vector<Quad> out;
    for (const auto& [box, angle]: partsOf(ink, hit)) {
        out.push_back(quadOfPart(box, angle));
    }
    return out;
}

bool atAnAngle(const PageText& ink, const Hit& hit) {
    for (uint32_t i = hit.first; i <= hit.last && i < ink.words.size(); ++i) {
        if (ink.words[i].angle != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace xqt::ink
