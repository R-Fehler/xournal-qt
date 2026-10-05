#include "InkText.h"

#include <algorithm>

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
            page->words.back().box.translate(l.origin);
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

std::vector<QRectF> rectsOf(const PageText& ink, const Hit& hit) {
    std::vector<QRectF> out;
    QRectF box;
    bool open = false;
    for (uint32_t i = hit.first; i <= hit.last && i < ink.words.size(); ++i) {
        if (open && std::binary_search(ink.lineStarts.begin(), ink.lineStarts.end(), i)) {
            out.push_back(box.adjusted(-MARK_MARGIN, -MARK_MARGIN, MARK_MARGIN, MARK_MARGIN));
            open = false;
        }
        box = open ? box.united(ink.words[i].box) : ink.words[i].box;
        open = true;
    }
    if (open) {
        out.push_back(box.adjusted(-MARK_MARGIN, -MARK_MARGIN, MARK_MARGIN, MARK_MARGIN));
    }
    return out;
}

}  // namespace xqt::ink
