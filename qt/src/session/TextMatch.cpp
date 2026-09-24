#include "TextMatch.h"

#include <algorithm>

#include <QList>

namespace xqt::textmatch {

namespace {

/// The letters of a ligature (nullptr: none)
inline QStringView ligature(char16_t c) {
    switch (c) {
        case 0xFB00: return u"ff";
        case 0xFB01: return u"fi";
        case 0xFB02: return u"fl";
        case 0xFB03: return u"ffi";
        case 0xFB04: return u"ffl";
        case 0xFB05:
        case 0xFB06: return u"st";
        default: return {};
    }
}

/// text[j] is the hyphen of a word broken at a line end: "hyphen- ated"
inline bool lineBreakHyphen(QStringView t, qsizetype j) {
    return j > 0 && j + 2 < t.size() && t[j] == u'-' && t[j + 1] == u' ' && t[j - 1].isLetter() && t[j + 2].isLower();
}

/// A letter or digit (of a word)
inline bool wordChar(QChar c) { return c.isLetterOrNumber(); }

/// The match [a, b) lies where `bounds` want it (a word broken at a line end is one word)
inline bool inBounds(QStringView t, qsizetype a, qsizetype b, unsigned bounds) {
    if ((bounds & WordStart) && a > 0 && (wordChar(t[a - 1]) || (a >= 2 && lineBreakHyphen(t, a - 2)))) {
        return false;
    }
    if ((bounds & WordEnd) && b < t.size() && (wordChar(t[b]) || lineBreakHyphen(t, b))) {
        return false;
    }
    return true;
}

/// The end of the match of `q` at `j`, or -1
qsizetype matchAt(QStringView t, qsizetype j, QStringView q) {
    const qsizetype n = t.size();
    const qsizetype m = q.size();
    qsizetype k = 0;
    bool afterBreak = false;  // the last character matched was the hyphen of a line break
    while (k < m) {
        if (j >= n) {
            return -1;
        }
        const char16_t c = t[j].unicode();
        if (fold(c) == q[k].unicode()) {
            afterBreak = c == u'-' && lineBreakHyphen(t, j);
            ++j;
            ++k;
            continue;
        }
        if (const QStringView letters = ligature(c); !letters.isEmpty()) {
            if (!q.sliced(k).startsWith(letters)) {
                return -1;
            }
            k += letters.size();
            ++j;
            afterBreak = false;
            continue;
        }
        if (k > 0 && c == u'-' && lineBreakHyphen(t, j)) {
            j += 2;  // "hyphen- ated" as "hyphenated"
            continue;
        }
        if (afterBreak && c == u' ') {
            ++j;  // "pre- processing" as "pre-processing"
            afterBreak = false;
            continue;
        }
        if (k > 0 && c == 0x00AD) {
            ++j;
            continue;
        }
        return -1;
    }
    return j;
}


/// Calls f(start, end) for each match; f returns false to stop
template <typename F>
void scan(QStringView t, QStringView q, unsigned bounds, F&& f) {
    if (q.isEmpty()) {
        return;
    }
    const char16_t q0 = q[0].unicode();
    const char16_t upper = q0 >= u'a' && q0 <= u'z' ? static_cast<char16_t>(q0 - 32) : q0;
    const bool ligatureStart = q0 == u'f' || q0 == u's';
    const char16_t* data = reinterpret_cast<const char16_t*>(t.data());
    const qsizetype n = t.size();
    qsizetype j = 0;
    while (j < n) {
        const char16_t c = data[j];
        if (c == q0 || c == upper || (c >= 0x80 && (fold(c) == q0 || (ligatureStart && !ligature(c).isEmpty())))) {
            if (const qsizetype end = matchAt(t, j, q); end > j && (bounds == Anywhere || inBounds(t, j, end, bounds))) {
                if (!f(j, end)) {
                    return;
                }
                j = end;
                continue;
            }
        }
        ++j;
    }
}
}  // namespace

QString prepare(const QString& query) {
    const QString s = query.simplified();
    QString out;
    out.reserve(s.size());
    for (const QChar c: s) {
        if (const QStringView letters = ligature(c.unicode()); !letters.isEmpty()) {
            out += letters;
        } else {
            out += QChar(fold(c.unicode()));
        }
    }
    return out;
}

std::vector<Span> find(QStringView text, QStringView query, unsigned bounds) {
    std::vector<Span> out;
    scan(text, query, bounds, [&](qsizetype a, qsizetype b) {
        out.push_back({a, b});
        return true;
    });
    return out;
}

int count(QStringView text, QStringView query, unsigned bounds) {
    int n = 0;
    scan(text, query, bounds, [&](qsizetype, qsizetype) {
        ++n;
        return true;
    });
    return n;
}

bool contains(QStringView text, QStringView query, unsigned bounds) {
    bool found = false;
    scan(text, query, bounds, [&](qsizetype, qsizetype) {
        found = true;
        return false;
    });
    return found;
}

std::vector<Span> merged(std::vector<Span> all) {
    std::sort(all.begin(), all.end(), [](const Span& a, const Span& b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    std::vector<Span> out;
    out.reserve(all.size());
    for (const Span& s: all) {
        if (out.empty() || s.start >= out.back().end) {
            out.push_back(s);
        }
    }
    return out;
}

namespace {
/// The matches of all terms, not merged; `found`: how many terms have matches
std::vector<Span> collect(QStringView text, const std::vector<Term>& terms, int& found) {
    std::vector<Span> all;
    found = 0;
    for (const Term& t: terms) {
        const size_t before = all.size();
        scan(text, t.text, t.bounds, [&](qsizetype a, qsizetype b) {
            all.push_back({a, b});
            return true;
        });
        found += all.size() > before ? 1 : 0;
    }
    return all;
}
}  // namespace

std::vector<Span> find(QStringView text, const std::vector<Term>& terms) {
    if (terms.size() == 1) {
        return find(text, terms.front().text, terms.front().bounds);
    }
    int found = 0;
    std::vector<Span> all = collect(text, terms, found);
    return found > 1 ? merged(std::move(all)) : all;
}

int count(QStringView text, const std::vector<Term>& terms) {
    if (terms.size() == 1) {
        return count(text, terms.front().text, terms.front().bounds);
    }
    // One scan per term; only matches of several terms need to be put in order (they may overlap)
    int found = 0;
    std::vector<Span> all = collect(text, terms, found);
    return found > 1 ? static_cast<int>(merged(std::move(all)).size()) : static_cast<int>(all.size());
}

QString encode(const std::vector<Term>& terms) {
    QString out;
    for (const Term& t: terms) {
        if (!out.isEmpty()) {
            out += QChar(0x1e);
        }
        out += QChar(u'0' + static_cast<char16_t>(t.bounds & Word));
        out += t.text;
    }
    return out;
}

std::vector<Term> decode(QStringView encoded) {
    std::vector<Term> out;
    for (const QStringView part: encoded.split(QChar(0x1e))) {
        if (part.size() >= 2 && part[0] >= u'0' && part[0] <= u'3') {
            out.push_back({part.sliced(1).toString(), static_cast<unsigned>(part[0].unicode() - u'0')});
        }
    }
    return out;
}

Simplified simplify(QStringView original) {
    Simplified s;
    s.text.reserve(original.size());
    s.origin.reserve(static_cast<size_t>(original.size()) + 1);
    bool space = false;
    for (qsizetype i = 0; i < original.size(); ++i) {
        const QChar c = original[i];
        if (c.isSpace()) {
            space = !s.text.isEmpty();
            continue;
        }
        if (space) {
            s.text += u' ';
            s.origin.push_back(i - 1);  // (the last character of the whitespace run)
            space = false;
        }
        s.text += c;
        s.origin.push_back(i);
    }
    s.origin.push_back(s.origin.empty() ? 0 : s.origin.back() + 1);
    return s;
}

}  // namespace xqt::textmatch
