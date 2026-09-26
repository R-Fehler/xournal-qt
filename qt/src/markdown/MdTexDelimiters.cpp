#include "MdTexDelimiters.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

#include "MdDocument.h"
#include "MdText.h"

namespace xqt::md::tex {

namespace {

// --- the characters around a "$", as md4c sees them -----------------------------------------------------------------

/// The code point that starts at `i` (a broken one: U+FFFD).
uint32_t codePointAt(std::string_view s, size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        return c;
    }
    const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (n == 0 || i + n > s.size()) {
        return 0xFFFD;
    }
    uint32_t cp = c & (0x7F >> n);
    for (size_t k = 1; k < n; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    return cp;
}
/// The code point that ends before `i`.
uint32_t codePointBefore(std::string_view s, size_t i) {
    size_t b = i - 1;
    while (b > 0 && i - b < 4 && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) {
        --b;
    }
    return codePointAt(s, b);
}

/// White space or punctuation: next to it a "$" can open (before it) or close (after it) a formula. md4c takes
/// Unicode's white space and its P and S categories; here the ASCII ones and the common others. Another character
/// counts as a letter, so a pair next to it stays as it is (never a "$" that md4c then shows as text).
bool spaceOrPunct(uint32_t cp) {
    if (cp < 0x80) {
        return cp == ' ' || (cp >= '\t' && cp <= '\r') || (cp >= 33 && cp <= 47) || (cp >= 58 && cp <= 64) ||
               (cp >= 91 && cp <= 96) || (cp >= 123 && cp <= 126);
    }
    static const uint32_t ranges[][2] = {
            {0xA0, 0xA9},     {0xAB, 0xAC},     {0xAE, 0xB1},     {0xB4, 0xB4},     {0xB6, 0xB8},
            {0xBB, 0xBB},     {0xBF, 0xBF},     {0xD7, 0xD7},     {0xF7, 0xF7},     {0x1680, 0x1680},
            {0x2000, 0x200A}, {0x2010, 0x2027}, {0x202F, 0x205F}, {0x20A0, 0x20C0}, {0x2190, 0x2426},
            {0x2500, 0x2775}, {0x3000, 0x3004}, {0x3008, 0x3020}, {0xFF01, 0xFF0F}, {0xFF1A, 0xFF20},
    };
    return std::any_of(std::begin(ranges), std::end(ranges),
                       [cp](const uint32_t* r) { return cp >= r[0] && cp <= r[1]; });
}

// --- the lines: code, paragraphs --------------------------------------------------------------------------------------

/// A line without its indentation and quote marks ("> ").
std::string_view containerless(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '>')) {
        ++i;
    }
    return line.substr(i);
}
/// The indentation in columns (a tab to the next multiple of 4).
size_t indentOf(std::string_view line) {
    size_t col = 0;
    for (const char c: line) {
        if (c == ' ') {
            ++col;
        } else if (c == '\t') {
            col += 4 - col % 4;
        } else {
            break;
        }
    }
    return col;
}
bool listItem(std::string_view s) {
    const auto markEnd = [&](size_t n) { return n == s.size() || s[n] == ' ' || s[n] == '\t'; };
    if (!s.empty() && (s[0] == '-' || s[0] == '*' || s[0] == '+')) {
        return markEnd(1);
    }
    size_t n = 0;
    while (n < s.size() && n < 10 && s[n] >= '0' && s[n] <= '9') {
        ++n;
    }
    return n > 0 && n < 10 && n < s.size() && (s[n] == '.' || s[n] == ')') && markEnd(n + 1);
}
/// Where the text of a line begins: after its indentation, quote marks and list mark.
size_t contentStart(std::string_view line) {
    std::string_view inner = containerless(line);
    if (listItem(inner)) {
        size_t n = inner.find_first_of(" \t");
        n = n == std::string_view::npos ? inner.size() : n;
        while (n < inner.size() && (inner[n] == ' ' || inner[n] == '\t')) {
            ++n;
        }
        inner = inner.substr(n);
    }
    return line.size() - inner.size();
}
/// Whether nothing but blanks (and a punctuation mark: "\]." ends a sentence) follows.
bool endsLine(std::string_view rest) {
    size_t i = 0;
    if (!rest.empty() && (rest[0] == '.' || rest[0] == ',' || rest[0] == ';' || rest[0] == ':' || rest[0] == '!' ||
                          rest[0] == '?')) {
        i = 1;
    }
    return text::blank(rest.substr(i));
}
bool atxHeading(std::string_view s) {
    size_t n = 0;
    while (n < s.size() && s[n] == '#') {
        ++n;
    }
    return n >= 1 && n <= 6 && (n == s.size() || s[n] == ' ' || s[n] == '\t');
}

struct Delimiter {
    size_t at = 0;  ///< its backslash
    bool open = false;
    bool display = false;
};
struct Ticks {  ///< a run of backticks
    size_t at = 0;
    size_t length = 0;
    bool escaped = false;  ///< after a backslash: its first one is no backtick of a code span's opening
};
struct Dollars {  ///< a run of "$" (not escaped)
    size_t at = 0;
    size_t length = 0;
};
struct Pair {
    size_t open = 0;   ///< the backslash of "\(" / "\["
    size_t close = 0;  ///< the backslash of "\)" / "\]"
    bool display = false;
};

/// A "$" or "$$" of the text md4c reads.
struct Mark {
    size_t at = 0;      ///< in the source
    size_t length = 0;  ///< 1 or 2 "$"
    bool canOpen = false;
    bool canClose = false;
    int pair = -1;  ///< a pair's delimiter (its index), or -1: a "$" of the source
};

/// md4c's pairing of "$" marks (md_analyze_dollar): a mark that can close takes the last one that can open if that
/// is as long (and then none before it is open any more); else it is one that can open.
std::vector<std::pair<size_t, size_t>> formulas(const std::vector<Mark>& marks) {
    std::vector<std::pair<size_t, size_t>> out;
    std::vector<size_t> openers;
    for (size_t i = 0; i < marks.size(); ++i) {
        const Mark& m = marks[i];
        if (m.canClose && !openers.empty() && marks[openers.back()].length == m.length) {
            out.emplace_back(openers.back(), i);
            openers.clear();
            continue;
        }
        if (m.canOpen) {
            openers.push_back(i);
        }
    }
    return out;
}

/// The pairs of a text, in order: one pass over its lines, each paragraph resolved at its end.
class Finder {
public:
    explicit Finder(std::string_view s): s(s) {}

    std::vector<Pair> run() {
        using namespace text;
        std::string fence;
        bool afterBlank = true;  // (the start: as after a blank line)
        bool indentedCode = false;
        bool inList = false;
        for (size_t ls = 0; ls < s.size(); ls = nextLine(s, ls)) {
            const std::string_view line = lineAt(s, ls);
            const std::string_view inner = containerless(line);
            if (!fence.empty()) {  // (in a fenced code block)
                if (closesFence(inner, fence)) {
                    fence.clear();
                }
                continue;
            }
            if (blank(inner)) {
                paragraph();
                afterBlank = true;
                continue;
            }
            // Indented code: after a blank line (not in a list) or after code; else it continues a paragraph
            if (indentOf(line) >= 4 && (indentedCode || (afterBlank && !inList))) {
                paragraph();
                indentedCode = true;
                afterBlank = false;
                continue;
            }
            indentedCode = false;
            if (const std::string f = fenceOf(inner);
                !f.empty() && (f[0] == '~' || inner.find('`', f.size()) == std::string_view::npos)) {
                paragraph();
                fence = f;
                afterBlank = false;
                continue;
            }
            if (listItem(inner)) {
                paragraph();  // (an item: a paragraph of its own)
                inList = true;
            } else if (afterBlank && indentOf(line) == 0 && line[0] != '>') {
                inList = false;
            }
            const bool oneLine = atxHeading(inner) || inner[0] == '|';  // (a heading, a table row)
            if (oneLine) {
                paragraph();
            }
            scan(ls, ls + line.size(), ls + contentStart(line));
            if (oneLine) {
                paragraph();
            }
            afterBlank = false;
        }
        paragraph();
        return std::move(out);
    }

private:
    /// A line of a paragraph: its delimiters, backticks and "$". A "\[" counts where it begins the line's text
    /// (`content`), a "\]" where it ends it: "\[" and "\]" inside a line are brackets, often escaped ones (pandoc's
    /// Markdown writes "\[1\]").
    void scan(size_t i, size_t end, size_t content) {
        while (i < end) {
            const char c = s[i];
            if (c == '\\') {
                size_t j = i;
                while (j < end && s[j] == '\\') {
                    ++j;
                }
                if ((j - i) % 2 == 0 || j == end) {  // (backslashes escaping each other)
                    i = j;
                    continue;
                }
                const char d = s[j];
                if (d == '(' || d == ')' || (d == '[' && j - 1 == content) ||
                    (d == ']' && endsLine(s.substr(j + 1, end - j - 1)))) {
                    delimiters.push_back({j - 1, d == '(' || d == '[', d == '[' || d == ']'});
                    i = j + 1;
                } else if (d == '`') {
                    size_t k = j;
                    while (k < end && s[k] == '`') {
                        ++k;
                    }
                    ticks.push_back({j, k - j, true});
                    i = k;
                } else {
                    i = j + 1;  // (an escaped character: "\$" is no mark)
                }
                continue;
            }
            if (c == '`' || c == '$') {
                size_t k = i;
                while (k < end && s[k] == c) {
                    ++k;
                }
                if (c == '`') {
                    ticks.push_back({i, k - i, false});
                } else {
                    dollars.push_back({i, k - i});
                }
                i = k;
                continue;
            }
            ++i;
        }
    }

    /// The code spans of the paragraph, [begin, end): a run of backticks and the next one as long (CommonMark).
    std::vector<std::pair<size_t, size_t>> codeSpans() const {
        std::vector<std::pair<size_t, size_t>> spans;
        std::map<size_t, std::vector<size_t>> byLength;  // the runs of each length, in order
        for (size_t i = 0; i < ticks.size(); ++i) {
            byLength[ticks[i].length].push_back(i);
        }
        for (size_t i = 0; i < ticks.size();) {
            const Ticks& t = ticks[i];
            const size_t length = t.escaped ? t.length - 1 : t.length;
            const auto same = byLength.find(length);
            if (length == 0 || same == byLength.end()) {
                ++i;
                continue;
            }
            const auto next = std::upper_bound(same->second.begin(), same->second.end(), i);
            if (next == same->second.end()) {
                ++i;  // (no closing run: its backticks are text)
                continue;
            }
            spans.emplace_back(t.at + t.length - length, ticks[*next].at + length);
            i = *next + 1;
        }
        return spans;
    }

    /// Keep what is not in one of the (sorted, disjoint) ranges.
    template <typename T>
    static void dropInside(std::vector<T>& items, const std::vector<std::pair<size_t, size_t>>& ranges) {
        size_t r = 0;
        size_t kept = 0;
        for (const T& x: items) {
            while (r < ranges.size() && ranges[r].second <= x.at) {
                ++r;
            }
            if (r == ranges.size() || x.at < ranges[r].first) {
                items[kept++] = x;
            }
        }
        items.resize(kept);
    }

    Mark mark(size_t at, size_t sourceLength, size_t length, int pair) const {
        Mark m;
        m.at = at;
        m.length = length;
        m.pair = pair;
        m.canOpen = at == 0 || spaceOrPunct(codePointBefore(s, at));
        m.canClose = at + sourceLength >= s.size() || spaceOrPunct(codePointAt(s, at + sourceLength));
        return m;
    }

    /// The end of a paragraph: its pairs that md4c reads as formulas once rewritten.
    void paragraph() {
        if (!delimiters.empty()) {
            resolve();
        }
        delimiters.clear();
        ticks.clear();
        dollars.clear();
    }

    void resolve() {
        // Nothing in code; md4c's formulas of the source ("$…\(…$") stay as they are
        const auto code = codeSpans();
        dropInside(delimiters, code);
        dropInside(dollars, code);
        std::vector<Mark> sourceMarks;
        for (const Dollars& d: dollars) {
            if (d.length <= 2) {  // (a longer run is no mark)
                sourceMarks.push_back(mark(d.at, d.length, d.length, -1));
            }
        }
        std::vector<std::pair<size_t, size_t>> sourceFormulas;
        for (const auto& [a, b]: formulas(sourceMarks)) {
            sourceFormulas.emplace_back(sourceMarks[a].at, sourceMarks[b].at + sourceMarks[b].length);
        }
        dropInside(delimiters, sourceFormulas);

        // Pairs in order: an opening one waits for its closing one; a later opening one takes its place
        std::vector<Pair> pairs;
        const Delimiter* pending = nullptr;
        for (const Delimiter& d: delimiters) {
            if (d.open) {
                pending = &d;
            } else if (pending && pending->display == d.display) {
                pairs.push_back({pending->at, d.at, d.display});
                pending = nullptr;
            }
        }
        // No "$" of the rewritten text next to another one (md4c would read "$$" or "$$$")
        const auto dollarAt = [&](size_t i) { return i < s.size() && s[i] == '$'; };
        std::vector<bool> keep(pairs.size(), true);
        for (size_t i = 0; i < pairs.size(); ++i) {
            const Pair& p = pairs[i];
            if ((p.open > 0 && dollarAt(p.open - 1)) || dollarAt(p.open + 2) || dollarAt(p.close - 1) ||
                dollarAt(p.close + 2) || p.close == p.open + 2) {
                keep[i] = false;
            }
            if (i + 1 < pairs.size() && pairs[i + 1].open == p.close + 2) {
                keep[i] = keep[i + 1] = false;
            }
        }
        // As md4c reads the rewritten text: each pair is a formula, and those of the source stay as they are. The
        // first pair that is not (next to a letter, ...) stays as written, and the rest is read again without it:
        // its "$" may have taken a later one's. A few rounds at most, then none of the paragraph's pairs.
        constexpr int ROUNDS = 8;
        for (int round = 0; round < ROUNDS; ++round) {
            std::vector<Mark> marks = sourceMarks;
            for (size_t i = 0; i < pairs.size(); ++i) {
                if (keep[i]) {
                    const size_t length = pairs[i].display ? 2 : 1;
                    marks.push_back(mark(pairs[i].open, 2, length, static_cast<int>(i)));
                    marks.push_back(mark(pairs[i].close, 2, length, static_cast<int>(i)));
                }
            }
            std::sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.at < b.at; });
            std::vector<bool> read(pairs.size(), false);
            std::vector<std::pair<size_t, size_t>> fromSource;
            for (const auto& [a, b]: formulas(marks)) {
                const Mark& o = marks[a];
                const Mark& c = marks[b];
                if (o.pair < 0 && c.pair < 0) {
                    fromSource.emplace_back(o.at, c.at + c.length);
                } else if (o.pair == c.pair && o.at == pairs[static_cast<size_t>(o.pair)].open) {
                    read[static_cast<size_t>(o.pair)] = true;
                }
            }
            bool all = fromSource == sourceFormulas;
            for (size_t i = 0; i < pairs.size(); ++i) {
                all = all && (!keep[i] || read[i]);
            }
            if (all) {
                break;
            }
            size_t first = 0;
            while (first < pairs.size() && (!keep[first] || read[first])) {
                ++first;
            }
            if (first < pairs.size()) {
                keep[first] = false;
            }
            if (first == pairs.size() || round + 1 == ROUNDS) {  // (or the source's formulas changed, by none of them)
                std::fill(keep.begin(), keep.end(), false);
                break;
            }
        }
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (keep[i]) {
                out.push_back(pairs[i]);
            }
        }
    }

    std::string_view s;
    std::vector<Pair> out;
    // the paragraph being read
    std::vector<Delimiter> delimiters;
    std::vector<Ticks> ticks;
    std::vector<Dollars> dollars;
};

bool hasDelimiters(std::string_view s) {
    return s.find("\\(") != std::string_view::npos || s.find("\\[") != std::string_view::npos;
}

}  // namespace

size_t Rewritten::toSource(size_t offset) const {
    const auto it = std::upper_bound(changes.begin(), changes.end(), offset,
                                     [](size_t o, const Change& c) { return o < c.at; });
    return it == changes.begin() ? offset : offset + std::prev(it)->shift;
}

Rewritten rewrite(std::string_view src) {
    Rewritten r;
    if (!hasDelimiters(src)) {
        return r;
    }
    const std::vector<Pair> pairs = Finder(src).run();
    if (pairs.empty()) {
        return r;
    }
    r.changed = true;
    r.text.reserve(src.size());
    size_t pos = 0;
    size_t shift = 0;
    const auto put = [&](size_t at, bool display) {
        r.text.append(src.substr(pos, at - pos));
        if (display) {
            r.text += "$$";
        } else {
            r.text += '$';
            r.changes.push_back({r.text.size(), ++shift});
        }
        pos = at + 2;
    };
    for (const Pair& p: pairs) {
        put(p.open, p.display);
        put(p.close, p.display);
    }
    r.text.append(src.substr(pos));
    return r;
}

std::string convertPasted(std::string_view text, size_t from, size_t to, std::string_view pasted) {
    if (!hasDelimiters(pasted)) {
        return std::string(pasted);
    }
    from = std::min(from, text.size());
    to = std::clamp(to, from, text.size());
    std::string whole;
    whole.reserve(text.size() + pasted.size());
    whole.append(text.substr(0, from)).append(pasted).append(text.substr(to));
    if (isPlain(whole)) {
        return std::string(pasted);
    }
    const size_t end = from + pasted.size();
    std::string out;
    out.reserve(pasted.size());
    size_t pos = from;
    for (const Pair& p: Finder(whole).run()) {
        if (p.open < from || p.close + 2 > end) {
            continue;  // (only pairs that are both in the pasted text)
        }
        const char* mark = p.display ? "$$" : "$";
        out.append(whole, pos, p.open - pos).append(mark);
        out.append(whole, p.open + 2, p.close - p.open - 2).append(mark);
        pos = p.close + 2;
    }
    out.append(whole, pos, end - pos);
    return out;
}

}  // namespace xqt::md::tex
