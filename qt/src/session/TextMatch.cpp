#include "TextMatch.h"

namespace xqt::textmatch {

namespace {
inline char16_t fold(char16_t c) {
    if (c < 0x80) {
        return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c;
    }
    return static_cast<char16_t>(QChar::toCaseFolded(static_cast<char32_t>(c)));
}

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

/// Calls f(start, end) for each match
template <typename F>
void scan(QStringView t, QStringView q, F&& f) {
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
            if (const qsizetype end = matchAt(t, j, q); end > j) {
                f(j, end);
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

std::vector<Span> find(QStringView text, QStringView query) {
    std::vector<Span> out;
    scan(text, query, [&](qsizetype a, qsizetype b) { out.push_back({a, b}); });
    return out;
}

int count(QStringView text, QStringView query) {
    int n = 0;
    scan(text, query, [&](qsizetype, qsizetype) { ++n; });
    return n;
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
