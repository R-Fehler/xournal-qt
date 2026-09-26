#include "MdFormat.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <regex>

#include "MdText.h"

namespace xqt::md::format {

const std::string_view PAGE_BREAK = R"(<div style="page-break-after: always"></div>)";

namespace {
using namespace md::text;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; }
bool isPunct(char c) { return std::ispunct(static_cast<unsigned char>(c)); }

// --- several changes as one ------------------------------------------------------------------------------------------

/// A change of text[at, at + len) to `with`. The changes of one tool are sorted and do not overlap.
struct Op {
    size_t at = 0;
    size_t len = 0;
    std::string with;
};

/// Where a place of the text is after the changes. `after`: a change inserted right at the place goes before it
/// (the place moves behind the insertion), and a place inside a replaced range goes to the end of the replacement.
size_t mapThrough(const std::vector<Op>& ops, size_t p, bool after) {
    std::ptrdiff_t shift = 0;
    for (const Op& op: ops) {
        const bool before = op.len == 0 ? (op.at < p || (op.at == p && after)) : op.at + op.len <= p;
        if (before) {
            shift += static_cast<std::ptrdiff_t>(op.with.size()) - static_cast<std::ptrdiff_t>(op.len);
        } else if (op.at < p && p < op.at + op.len) {
            return static_cast<size_t>(static_cast<std::ptrdiff_t>(op.at) + shift) + (after ? op.with.size() : 0);
        } else {
            break;
        }
    }
    return static_cast<size_t>(static_cast<std::ptrdiff_t>(p) + shift);
}

/// The changes as one Edit; the selection follows them (its start after insertions there, its end before them).
Edit combine(std::string_view t, std::vector<Op> ops, size_t anchor, size_t caret, bool startAfter = true,
             bool endAfter = false) {
    if (ops.empty()) {
        return {caret, caret, "", anchor, caret};
    }
    std::sort(ops.begin(), ops.end(), [](const Op& a, const Op& b) { return a.at < b.at; });
    Edit e;
    e.from = ops.front().at;
    e.to = ops.back().at + ops.back().len;
    size_t pos = e.from;
    for (const Op& op: ops) {
        e.with.append(t.substr(pos, op.at - pos));
        e.with += op.with;
        pos = op.at + op.len;
    }
    const bool forward = anchor <= caret;
    const size_t lo = mapThrough(ops, std::min(anchor, caret), startAfter);
    const size_t hi = std::max(lo, mapThrough(ops, std::max(anchor, caret), anchor == caret ? startAfter : endAfter));
    e.anchor = forward ? lo : hi;
    e.caret = forward ? hi : lo;
    return e;
}

// --- inline marks ----------------------------------------------------------------------------------------------------

enum class Mark { Bold, Italic, Strike, Code, Math };

std::string markText(Mark m) {
    switch (m) {
        case Mark::Bold:
            return "**";
        case Mark::Italic:
            return "*";
        case Mark::Strike:
            return "~~";
        case Mark::Code:
            return "`";
        case Mark::Math:
            return "$";
    }
    return {};
}

/// A pair of marks: [openBegin, openEnd) ... [closeBegin, closeEnd).
struct Span {
    size_t openBegin = 0;
    size_t openEnd = 0;
    size_t closeBegin = 0;
    size_t closeEnd = 0;
};

struct LineSpans {
    std::vector<Span> bold, italic, strike, code, math;
    const std::vector<Span>& of(Mark m) const {
        switch (m) {
            case Mark::Bold:
                return bold;
            case Mark::Italic:
                return italic;
            case Mark::Strike:
                return strike;
            case Mark::Code:
                return code;
            case Mark::Math:
                break;
        }
        return math;
    }
};

size_t runOf(std::string_view t, size_t i, size_t end) {
    size_t j = i;
    while (j < end && t[j] == t[i]) {
        ++j;
    }
    return j - i;
}

/// The inline marks of the line [ls, le): code spans first (nothing is a mark inside them), then formulas, then
/// emphasis (as CommonMark pairs delimiter runs, simplified) and strikethrough.
LineSpans scanLine(std::string_view t, size_t ls, size_t le) {
    LineSpans out;
    std::vector<bool> masked(le - ls, false);
    const auto mask = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            masked[i - ls] = true;
        }
    };
    // Code spans: backtick runs of the same length
    for (size_t i = ls; i < le;) {
        if (t[i] == '\\') {
            i = std::min(le, i + 2);
            continue;
        }
        if (t[i] != '`') {
            ++i;
            continue;
        }
        const size_t n = runOf(t, i, le);
        size_t j = i + n;
        bool closed = false;
        while (j < le) {
            if (t[j] != '`') {
                ++j;
                continue;
            }
            const size_t m = runOf(t, j, le);
            if (m == n) {
                out.code.push_back({i, i + n, j, j + n});
                mask(i, j + n);
                i = j + n;
                closed = true;
                break;
            }
            j += m;
        }
        if (!closed) {
            i += n;
        }
    }
    // Formulas "$...$" (not "$$", not "\$"; as md4c: no letter or digit before the opening one or after the
    // closing one, no space inside next to them)
    for (size_t i = ls; i < le; ++i) {
        if (masked[i - ls] || t[i] == '\\') {
            i += t[i] == '\\' ? 1 : 0;
            continue;
        }
        if (t[i] != '$') {
            continue;
        }
        if (i + 1 < le && t[i + 1] == '$') {
            ++i;  // (a display formula's "$$")
            continue;
        }
        if ((i > ls && isAlnum(t[i - 1])) || i + 1 >= le || isSpace(t[i + 1])) {
            continue;
        }
        for (size_t j = i + 1; j < le; ++j) {
            if (masked[j - ls]) {
                break;
            }
            if (t[j] == '\\') {
                ++j;
                continue;
            }
            if (t[j] == '$' && !isSpace(t[j - 1]) && (j + 1 >= le || (!isAlnum(t[j + 1]) && t[j + 1] != '$'))) {
                out.math.push_back({i, i + 1, j, j + 1});
                mask(i, j + 1);
                i = j;
                break;
            }
        }
    }
    // Emphasis and strikethrough: delimiter runs that can open or close
    struct Opener {
        char c;
        size_t pos;
        size_t n;
    };
    std::vector<Opener> openers;
    for (size_t i = ls; i < le;) {
        const char c = t[i];
        if (masked[i - ls]) {
            ++i;
            continue;
        }
        if (c == '\\') {
            i = std::min(le, i + 2);
            continue;
        }
        if (c != '*' && c != '_' && c != '~') {
            ++i;
            continue;
        }
        const size_t n = runOf(t, i, le);
        const char prev = i > ls ? t[i - 1] : ' ';
        const char next = i + n < le ? t[i + n] : ' ';
        const bool left = !isSpace(next) && (!isPunct(next) || isSpace(prev) || isPunct(prev));
        const bool right = !isSpace(prev) && (!isPunct(prev) || isSpace(next) || isPunct(next));
        bool canOpen = left;
        bool canClose = right;
        if (c == '_') {
            canOpen = left && (!right || isPunct(prev));
            canClose = right && (!left || isPunct(next));
        }
        size_t start = i;
        size_t remaining = n;
        if (c == '~' && n > 2) {
            i += n;  // (not a strikethrough mark)
            continue;
        }
        while (canClose && remaining > 0) {
            auto k = openers.size();
            while (k > 0 && openers[k - 1].c != c) {
                --k;
            }
            if (k == 0) {
                break;
            }
            Opener& o = openers[k - 1];
            if (c == '~') {
                if (o.n != remaining) {
                    break;
                }
                out.strike.push_back({o.pos, o.pos + o.n, start, start + remaining});
                start += remaining;
                remaining = 0;
                openers.resize(k - 1);
                break;
            }
            const size_t use = remaining >= 2 && o.n >= 2 ? 2 : 1;
            (use == 2 ? out.bold : out.italic).push_back({o.pos + o.n - use, o.pos + o.n, start, start + use});
            o.n -= use;
            start += use;
            remaining -= use;
            openers.resize(o.n == 0 ? k - 1 : k);  // (the ones opened inside and not closed are not marks)
        }
        if (remaining > 0 && canOpen) {
            openers.push_back({c, start, remaining});
        }
        i += n;
    }
    return out;
}

LineSpans spansAround(std::string_view t, size_t pos) { return scanLine(t, lineStart(t, pos), lineEnd(t, pos)); }

/// The span whose text holds the cursor, or that holds the selection [a, b] (with its marks or not).
const Span* spanOver(const std::vector<Span>& spans, size_t a, size_t b) {
    for (const Span& s: spans) {
        if (a == b ? s.openEnd <= a && a <= s.closeBegin : s.openBegin <= a && b <= s.closeEnd) {
            return &s;
        }
    }
    return nullptr;
}

/// The mark right before and right after the cursor, and nothing between them: marks inserted empty.
bool emptyMarksAt(std::string_view t, size_t c, Mark m) {
    if (m == Mark::Code || m == Mark::Math || m == Mark::Strike) {
        const std::string mark = markText(m);
        return c >= mark.size() && t.substr(c - mark.size(), mark.size()) == mark && t.substr(c, mark.size()) == mark;
    }
    size_t l = 0;
    while (l < c && t[c - 1 - l] == '*') {
        ++l;
    }
    const size_t r = runOf(t, c, t.size()) * (c < t.size() && t[c] == '*');
    return m == Mark::Bold ? l >= 2 && r >= 2 && l == r : l == r && l % 2 == 1;
}

/// The start of a line's text after its marks (indentation, quote, list or heading mark).
size_t contentStart(std::string_view t, size_t ls);

Edit toggleInline(std::string_view t, size_t anchor, size_t caret, Mark m) {
    const std::string mark = markText(m);
    const size_t a = std::min(anchor, caret);
    const size_t b = std::max(anchor, caret);
    if (a == b) {
        if (emptyMarksAt(t, a, m)) {
            return {a - mark.size(), a + mark.size(), "", a - mark.size(), a - mark.size()};
        }
        const LineSpans spans = spansAround(t, a);
        if (const Span* s = spanOver(spans.of(m), a, a)) {
            return combine(t, {{s->openBegin, s->openEnd - s->openBegin, ""}, {s->closeBegin, s->closeEnd - s->closeBegin, ""}},
                           anchor, caret);
        }
        return {a, a, mark + mark, a + mark.size(), a + mark.size()};
    }
    // The parts of the selection on each of its lines (without the lines' marks and the spaces around them)
    struct Segment {
        size_t from;
        size_t to;
        const Span* span;  // the span that has the mark already
    };
    std::vector<LineSpans> lines;
    std::vector<std::pair<size_t, size_t>> ranges;
    for (size_t ls = lineStart(t, a); ls <= b && ls <= t.size();) {
        const size_t le = lineEnd(t, ls);
        size_t from = std::max(a, contentStart(t, ls));
        size_t to = std::min(b, le);
        while (from < to && isSpace(t[from])) {
            ++from;
        }
        while (to > from && isSpace(t[to - 1])) {
            --to;
        }
        if (from < to) {
            ranges.emplace_back(from, to);
            lines.push_back(scanLine(t, ls, le));
        }
        if (le >= t.size()) {
            break;
        }
        ls = le + 1;
    }
    if (ranges.empty()) {
        return {a, a, mark + mark, a + mark.size(), a + mark.size()};
    }
    std::vector<Segment> segments;
    bool all = true;
    for (size_t i = 0; i < ranges.size(); ++i) {
        // (a selection with its marks: the marks are outside the trimmed range only if selected with them)
        const Span* s = spanOver(lines[i].of(m), ranges[i].first, ranges[i].second);
        segments.push_back({ranges[i].first, ranges[i].second, s});
        all = all && s;
    }
    std::vector<Op> ops;
    if (all) {
        for (const Segment& s: segments) {
            const bool seen = std::any_of(ops.begin(), ops.end(), [&](const Op& o) { return o.at == s.span->openBegin; });
            if (!seen) {
                ops.push_back({s.span->openBegin, s.span->openEnd - s.span->openBegin, ""});
                ops.push_back({s.span->closeBegin, s.span->closeEnd - s.span->closeBegin, ""});
            }
        }
        return combine(t, std::move(ops), anchor, caret);
    }
    for (const Segment& s: segments) {
        if (!s.span) {
            ops.push_back({s.from, 0, mark});
            ops.push_back({s.to, 0, mark});
        }
    }
    return combine(t, std::move(ops), anchor, caret);
}

// --- links and images ------------------------------------------------------------------------------------------------

const std::regex& linkPattern() {
    static const std::regex r(R"((!?)\[([^\]]*)\]\(([^)]*)\))");
    return r;
}

/// The link on the cursor's line that holds [a, b]: its range, its text's range.
std::optional<std::pair<std::pair<size_t, size_t>, std::pair<size_t, size_t>>> linkAround(std::string_view t, size_t a,
                                                                                           size_t b) {
    const size_t ls = lineStart(t, a);
    const std::string line(lineAt(t, ls));
    for (auto it = std::sregex_iterator(line.begin(), line.end(), linkPattern()); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        if (m[1].length() > 0) {
            continue;  // (an image)
        }
        const size_t from = ls + static_cast<size_t>(m.position(0));
        const size_t to = from + static_cast<size_t>(m.length(0));
        if (from <= a && b <= to && !(a == to && b == to)) {
            const size_t textFrom = ls + static_cast<size_t>(m.position(2));
            return std::make_pair(std::make_pair(from, to),
                                  std::make_pair(textFrom, textFrom + static_cast<size_t>(m.length(2))));
        }
    }
    return std::nullopt;
}

bool looksLikeAddress(std::string_view s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("www.", 0) == 0 ||
           s.rfind("mailto:", 0) == 0;
}

Edit link(std::string_view t, size_t anchor, size_t caret) {
    const size_t a = std::min(anchor, caret);
    const size_t b = std::max(anchor, caret);
    if (auto l = linkAround(t, a, b)) {
        // On a link: it becomes its text again
        const auto [range, text] = *l;
        return combine(t, {{range.first, text.first - range.first, ""}, {text.second, range.second - text.second, ""}},
                       anchor, caret);
    }
    const std::string selected(t.substr(a, b - a));
    if (selected.find('\n') != std::string::npos) {
        return {caret, caret, "", anchor, caret};  // (a link is within a line)
    }
    if (looksLikeAddress(selected)) {
        // An address selected: the link to it, the cursor where its text goes
        return {a, b, "[](" + selected + ")", a + 1, a + 1};
    }
    if (a == b) {
        return {a, a, "[](https://)", a + 1, a + 1};
    }
    // The text selected: its link, the address to type selected
    const std::string with = "[" + selected + "](https://)";
    return {a, b, with, a + selected.size() + 3, a + with.size() - 1};
}

Edit image(std::string_view t, size_t anchor, size_t caret, std::string_view path) {
    const size_t a = std::min(anchor, caret);
    const size_t b = std::max(anchor, caret);
    std::string alt(t.substr(a, b - a));
    if (alt.find('\n') != std::string::npos) {
        alt.clear();
    }
    if (alt.empty() && path.empty()) {
        alt = "image";  // (a placeholder: "image.png" to be typed over; a picture's alt text stays empty, as Typora's)
    }
    const std::string target = path.empty() ? std::string("image.png") : std::string(path);
    const std::string with = "![" + alt + "](" + target + ")";
    if (!path.empty()) {
        return {a, b, with, a + with.size(), a + with.size()};
    }
    return {a, b, with, a + alt.size() + 4, a + with.size() - 1};  // (the placeholder selected, to be typed over)
}

// --- line marks ------------------------------------------------------------------------------------------------------

struct LineInfo {
    size_t start = 0;
    size_t end = 0;
    size_t indentEnd = 0;  ///< after the indentation
    size_t quoteEnd = 0;   ///< after the quote marks ("> ")
    size_t markBegin = 0;  ///< the list or heading mark
    size_t markEnd = 0;
    enum class Kind { None, Heading, Bullet, Numbered, Task } kind = Kind::None;
    int level = 0;
    unsigned long number = 0;
    bool blank = false;
};

LineInfo lineInfo(std::string_view t, size_t ls) {
    LineInfo l;
    l.start = ls;
    l.end = lineEnd(t, ls);
    const std::string_view s = t.substr(ls, l.end - ls);
    l.blank = blank(s);
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    l.indentEnd = ls + i;
    while (i < s.size() && s[i] == '>') {
        ++i;
        if (i < s.size() && s[i] == ' ') {
            ++i;
        }
    }
    l.quoteEnd = ls + i;
    while (i < s.size() && s[i] == ' ') {
        ++i;
    }
    l.markBegin = l.markEnd = ls + i;
    const std::string rest(s.substr(i));
    static const std::regex heading(R"(^(#{1,6})( |$))");
    static const std::regex task(R"(^[-*+] \[[ xX]\]( |$))");
    static const std::regex bullet(R"(^[-*+]( |$))");
    static const std::regex numbered(R"(^(\d{1,9})[.)]( |$))");
    std::smatch m;
    if (std::regex_search(rest, m, heading)) {
        l.kind = LineInfo::Kind::Heading;
        l.level = static_cast<int>(m[1].length());
    } else if (std::regex_search(rest, m, task)) {
        l.kind = LineInfo::Kind::Task;
    } else if (std::regex_search(rest, m, bullet) && !setextUnderline(rest)) {
        l.kind = LineInfo::Kind::Bullet;
    } else if (std::regex_search(rest, m, numbered)) {
        l.kind = LineInfo::Kind::Numbered;
        l.number = std::stoul(m[1].str());
    }
    if (l.kind != LineInfo::Kind::None) {
        l.markEnd = l.markBegin + static_cast<size_t>(m[0].length());
    }
    return l;
}

size_t contentStart(std::string_view t, size_t ls) { return lineInfo(t, ls).markEnd; }

/// The lines of the selection (a selection that ends at the start of a line does not take that line).
std::vector<LineInfo> selectedLines(std::string_view t, size_t a, size_t b) {
    if (b > a && b == lineStart(t, b)) {
        --b;
    }
    std::vector<LineInfo> lines;
    for (size_t ls = lineStart(t, a);;) {
        lines.push_back(lineInfo(t, ls));
        const size_t le = lines.back().end;
        if (le >= b || le >= t.size()) {
            break;
        }
        ls = le + 1;
    }
    return lines;
}

/// The lines a line mark is for: those with text, or the cursor's line when there are none.
std::vector<LineInfo> markedLines(const std::vector<LineInfo>& lines) {
    std::vector<LineInfo> out;
    std::copy_if(lines.begin(), lines.end(), std::back_inserter(out), [](const LineInfo& l) { return !l.blank; });
    if (out.empty()) {
        out.push_back(lines.front());
    }
    return out;
}

Edit heading(std::string_view t, size_t anchor, size_t caret, int level) {
    const auto lines = markedLines(selectedLines(t, std::min(anchor, caret), std::max(anchor, caret)));
    const bool all = level > 0 && std::all_of(lines.begin(), lines.end(), [&](const LineInfo& l) {
                         return l.kind == LineInfo::Kind::Heading && l.level == level;
                     });
    std::vector<Op> ops;
    for (const LineInfo& l: lines) {
        const std::string mark = all || level == 0 ? std::string() : std::string(static_cast<size_t>(level), '#') + " ";
        if (l.markEnd - l.markBegin > 0 || !mark.empty()) {
            ops.push_back({l.markBegin, l.markEnd - l.markBegin, mark});
        }
    }
    return combine(t, std::move(ops), anchor, caret, true, true);
}

Edit list(std::string_view t, size_t anchor, size_t caret, LineInfo::Kind kind) {
    const auto lines = markedLines(selectedLines(t, std::min(anchor, caret), std::max(anchor, caret)));
    const bool all = std::all_of(lines.begin(), lines.end(), [&](const LineInfo& l) { return l.kind == kind; });
    // A numbered list goes on from the item right before it
    unsigned long number = 1;
    if (kind == LineInfo::Kind::Numbered && lines.front().start > 0) {
        const LineInfo before = lineInfo(t, lineStart(t, lines.front().start - 1));
        if (before.kind == LineInfo::Kind::Numbered &&
            before.markBegin - before.start == lines.front().markBegin - lines.front().start) {
            number = before.number + 1;
        }
    }
    std::vector<Op> ops;
    for (const LineInfo& l: lines) {
        std::string mark;
        if (!all) {
            mark = kind == LineInfo::Kind::Bullet ? "- "
                   : kind == LineInfo::Kind::Task ? "- [ ] "
                                                  : std::to_string(number++) + ". ";
        }
        ops.push_back({l.markBegin, l.markEnd - l.markBegin, mark});
    }
    return combine(t, std::move(ops), anchor, caret, true, true);
}

Edit quote(std::string_view t, size_t anchor, size_t caret) {
    const auto lines = selectedLines(t, std::min(anchor, caret), std::max(anchor, caret));
    const auto marked = markedLines(lines);
    const bool all = std::all_of(marked.begin(), marked.end(), [](const LineInfo& l) { return l.quoteEnd > l.indentEnd; });
    std::vector<Op> ops;
    for (const LineInfo& l: lines) {
        if (all) {
            if (l.quoteEnd > l.indentEnd) {  // one level less
                const size_t n = l.indentEnd + 1 < l.end && t[l.indentEnd + 1] == ' ' ? 2 : 1;
                ops.push_back({l.indentEnd, n, ""});
            }
        } else if (l.quoteEnd == l.indentEnd) {
            // (a blank line between two: the quote goes on over it)
            ops.push_back({l.indentEnd, 0, l.blank && lines.size() > 1 ? ">" : "> "});
        }
    }
    return combine(t, std::move(ops), anchor, caret, true, true);
}

// --- blocks ----------------------------------------------------------------------------------------------------------

/// Where a block goes for the cursor: an empty line is taken; at the start of a line, before it; else after the line
/// (after the selection's last line).
std::pair<size_t, size_t> blockPlace(std::string_view t, size_t a, size_t b) {
    if (a == b) {
        const size_t ls = lineStart(t, a);
        const size_t le = lineEnd(t, a);
        if (blank(t.substr(ls, le - ls))) {
            return {ls, le};
        }
        if (a == ls) {
            return {ls, ls};
        }
    }
    const auto lines = selectedLines(t, a, b);
    return {lines.back().end, lines.back().end};
}

/// A block of lines in place of text[from, to), with blank lines around it. The cursor goes to `caretInBlock` in
/// it, or (npos) after it: where the text after it starts, or on an empty line at the end.
Edit placeBlock(std::string_view t, size_t from, size_t to, const std::string& block, size_t caretInBlock) {
    const std::string_view before = t.substr(0, from);
    const std::string_view after = t.substr(to);
    std::string sepBefore;
    if (!before.empty() && !(before.size() >= 2 && before.substr(before.size() - 2) == "\n\n")) {
        sepBefore = before.back() == '\n' ? "\n" : "\n\n";
    }
    std::string sepAfter;
    size_t skip = 0;  // (line breaks after the block, before the text after it)
    if (after.empty()) {
        sepAfter = caretInBlock == std::string::npos ? "\n\n" : "\n";
    } else if (after.substr(0, 2) == "\n\n") {
        skip = 2;
    } else if (after[0] == '\n') {
        sepAfter = "\n";
        skip = 1;
    } else {
        sepAfter = "\n\n";
    }
    Edit e;
    e.from = from;
    e.to = to;
    e.with = sepBefore + block + sepAfter;
    const size_t at = caretInBlock == std::string::npos ? from + e.with.size() + skip
                                                        : from + sepBefore.size() + caretInBlock;
    e.anchor = e.caret = at;
    return e;
}

/// A fenced block ("```lang", "$$") around the selected lines, or empty with the cursor inside.
Edit fenced(std::string_view t, size_t anchor, size_t caret, const std::string& open, const std::string& close) {
    const size_t a = std::min(anchor, caret);
    const size_t b = std::max(anchor, caret);
    if (a == b) {
        const auto [from, to] = blockPlace(t, a, b);
        return placeBlock(t, from, to, open + "\n\n" + close, open.size() + 1);
    }
    const auto lines = selectedLines(t, a, b);
    const size_t from = lines.front().start;
    const size_t to = lines.back().end;
    const std::string inner(t.substr(from, to - from));
    Edit e = placeBlock(t, from, to, open + "\n" + inner + "\n" + close, open.size() + 1);
    e.caret = e.anchor + inner.size();  // (the lines selected again)
    return e;
}

Edit block(std::string_view t, size_t anchor, size_t caret, const std::string& text) {
    const auto [from, to] = blockPlace(t, std::min(anchor, caret), std::max(anchor, caret));
    return placeBlock(t, from, to, text, std::string::npos);
}

}  // namespace

Edit insertBlock(std::string_view text, size_t anchor, size_t caret, const std::string& lines) {
    return block(text, std::min(anchor, text.size()), std::min(caret, text.size()), lines);
}

std::optional<Action> actionNamed(std::string_view name) {
    static const std::pair<std::string_view, Action> names[] = {
            {"paragraph", Action::Paragraph},   {"heading1", Action::Heading1},     {"heading2", Action::Heading2},
            {"heading3", Action::Heading3},     {"bold", Action::Bold},             {"italic", Action::Italic},
            {"strike", Action::Strike},         {"code", Action::Code},             {"link", Action::Link},
            {"inlineMath", Action::InlineMath}, {"bulletList", Action::BulletList}, {"numberedList", Action::NumberedList},
            {"taskList", Action::TaskList},     {"quote", Action::Quote},           {"codeBlock", Action::CodeBlock},
            {"mathBlock", Action::MathBlock},   {"rule", Action::Rule},             {"image", Action::Image},
            {"pageBreak", Action::PageBreak}};
    for (const auto& [n, a]: names) {
        if (n == name) {
            return a;
        }
    }
    return std::nullopt;
}

Edit apply(std::string_view t, size_t anchor, size_t caret, Action action, std::string_view arg) {
    anchor = std::min(anchor, t.size());
    caret = std::min(caret, t.size());
    switch (action) {
        case Action::Paragraph:
            return heading(t, anchor, caret, 0);
        case Action::Heading1:
            return heading(t, anchor, caret, 1);
        case Action::Heading2:
            return heading(t, anchor, caret, 2);
        case Action::Heading3:
            return heading(t, anchor, caret, 3);
        case Action::Bold:
            return toggleInline(t, anchor, caret, Mark::Bold);
        case Action::Italic:
            return toggleInline(t, anchor, caret, Mark::Italic);
        case Action::Strike:
            return toggleInline(t, anchor, caret, Mark::Strike);
        case Action::Code:
            return toggleInline(t, anchor, caret, Mark::Code);
        case Action::InlineMath:
            return toggleInline(t, anchor, caret, Mark::Math);
        case Action::Link:
            return link(t, anchor, caret);
        case Action::BulletList:
            return list(t, anchor, caret, LineInfo::Kind::Bullet);
        case Action::NumberedList:
            return list(t, anchor, caret, LineInfo::Kind::Numbered);
        case Action::TaskList:
            return list(t, anchor, caret, LineInfo::Kind::Task);
        case Action::Quote:
            return quote(t, anchor, caret);
        case Action::CodeBlock:
            return fenced(t, anchor, caret, "```" + std::string(arg), "```");
        case Action::MathBlock:
            return fenced(t, anchor, caret, "$$", "$$");
        case Action::Rule:
            return block(t, anchor, caret, "---");
        case Action::PageBreak:
            return block(t, anchor, caret, std::string(PAGE_BREAK));
        case Action::Image:
            return image(t, anchor, caret, arg);
    }
    return {caret, caret, "", anchor, caret};
}

State stateAt(std::string_view t, size_t anchor, size_t caret) {
    State s;
    anchor = std::min(anchor, t.size());
    caret = std::min(caret, t.size());
    const size_t ls = lineStart(t, caret);
    // In a fenced code block (its fences included)?
    std::string fence;
    for (size_t l = 0; l < ls; l = nextLine(t, l)) {
        const std::string_view line = lineAt(t, l);
        if (fence.empty()) {
            fence = fenceOf(line);
        } else if (closesFence(line, fence)) {
            fence.clear();
        }
    }
    if (!fence.empty() || !fenceOf(lineAt(t, ls)).empty()) {
        s.codeBlock = true;
        return s;
    }
    const LineInfo l = lineInfo(t, ls);
    s.quote = l.quoteEnd > l.indentEnd;
    switch (l.kind) {
        case LineInfo::Kind::Heading:
            s.heading = l.level;
            break;
        case LineInfo::Kind::Bullet:
            s.list = State::List::Bullet;
            break;
        case LineInfo::Kind::Numbered:
            s.list = State::List::Numbered;
            break;
        case LineInfo::Kind::Task:
            s.list = State::List::Task;
            break;
        case LineInfo::Kind::None:
            break;
    }
    // The inline marks around the selection (on the cursor's line)
    size_t a = std::min(anchor, caret);
    size_t b = std::max(anchor, caret);
    if (a < ls || b > l.end) {
        a = b = caret;
    }
    const LineSpans spans = scanLine(t, ls, l.end);
    s.bold = spanOver(spans.bold, a, b) != nullptr;
    s.italic = spanOver(spans.italic, a, b) != nullptr;
    s.strike = spanOver(spans.strike, a, b) != nullptr;
    s.code = spanOver(spans.code, a, b) != nullptr;
    s.math = spanOver(spans.math, a, b) != nullptr;
    s.link = linkAround(t, a, b).has_value();
    // In a formula block: an odd number of "$$" before the cursor in its paragraph
    size_t para = ls;
    while (para > 0 && !blank(lineAt(t, lineStart(t, para - 1)))) {
        para = lineStart(t, para - 1);
    }
    size_t marks = 0;
    for (size_t i = t.find("$$", para); i != std::string_view::npos && i + 2 <= caret; i = t.find("$$", i + 2)) {
        marks += i == 0 || t[i - 1] != '\\';
    }
    s.math = s.math || marks % 2 == 1;
    s.table = lineAt(t, ls).find('|') != std::string_view::npos && table::at(t, caret).has_value();
    return s;
}

}  // namespace xqt::md::format

namespace xqt::md::table {

namespace {
using namespace md::text;

std::string trim(std::string_view s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string_view::npos) {
        return {};
    }
    const size_t b = s.find_last_not_of(" \t\r");
    return std::string(s.substr(a, b - a + 1));
}

/// Where the cells of a row are split: its pipes that are not escaped (GFM: also inside code spans).
std::vector<size_t> pipesOf(std::string_view line) {
    std::vector<size_t> pipes;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
        } else if (line[i] == '|') {
            pipes.push_back(i);
        }
    }
    return pipes;
}

bool delimiterRow(std::string_view line) {
    if (line.find('|') == std::string_view::npos && line.find('-') == std::string_view::npos) {
        return false;
    }
    static const std::regex cell(R"(^:?-+:?$)");
    const auto cells = splitRow(line);
    return !cells.empty() && std::all_of(cells.begin(), cells.end(), [](const std::string& c) {
        return std::regex_match(c, cell);
    });
}

Align alignOf(const std::string& delimiter) {
    const bool left = !delimiter.empty() && delimiter.front() == ':';
    const bool right = delimiter.size() > 1 && delimiter.back() == ':';
    return left && right ? Align::Center : left ? Align::Left : right ? Align::Right : Align::None;
}

/// How many columns a text takes in a monospaced font (wide East Asian characters and emoji: two).
size_t displayWidth(std::string_view s) {
    size_t width = 0;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0) {
            n = 4;
            cp = c & 0x07;
        } else if (c >= 0xE0) {
            n = 3;
            cp = c & 0x0F;
        } else if (c >= 0xC0) {
            n = 2;
            cp = c & 0x1F;
        }
        for (size_t k = 1; k < n && i + k < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        i += n;
        if ((cp >= 0x300 && cp <= 0x36F) || cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F)) {
            continue;  // (combining marks, joiners, variation selectors)
        }
        const bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                          (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                          (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                          (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
                          (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
        width += wide ? 2 : 1;
    }
    return width;
}

std::string escapeCell(const std::string& cell) {
    std::string out;
    for (size_t i = 0; i < cell.size(); ++i) {
        if (cell[i] == '|') {
            out += "\\|";
        } else if (cell[i] == '\n') {
            out += "<br>";
        } else if (cell[i] != '\r') {
            out += cell[i];
        }
    }
    return trim(out);
}

std::string pad(const std::string& s, size_t width, Align align) {
    const size_t w = displayWidth(s);
    const size_t space = width > w ? width - w : 0;
    switch (align) {
        case Align::Right:
            return std::string(space, ' ') + s;
        case Align::Center:
            return std::string(space / 2, ' ') + s + std::string(space - space / 2, ' ');
        default:
            return s + std::string(space, ' ');
    }
}
}  // namespace

std::vector<std::string> splitRow(std::string_view line) {
    std::string_view s = line;
    const size_t a = s.find_first_not_of(" \t");
    s = a == std::string_view::npos ? std::string_view() : s.substr(a);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    std::vector<size_t> pipes = pipesOf(s);
    if (!pipes.empty() && pipes.front() == 0) {
        s.remove_prefix(1);
        pipes = pipesOf(s);
    }
    if (!pipes.empty() && pipes.back() + 1 == s.size()) {
        s.remove_suffix(1);
        pipes.pop_back();
    }
    std::vector<std::string> cells;
    size_t from = 0;
    pipes.push_back(s.size());
    for (size_t p: pipes) {
        std::string cell = trim(s.substr(from, p - from));
        // "\|" is a pipe of the cell's text
        for (size_t i = cell.find("\\|"); i != std::string::npos; i = cell.find("\\|", i + 1)) {
            cell.erase(i, 1);
        }
        cells.push_back(std::move(cell));
        from = p + 1;
    }
    return cells;
}

std::optional<Table> parse(std::string_view lines) {
    std::vector<std::string_view> rows;
    for (size_t l = 0; l < lines.size(); l = nextLine(lines, l)) {
        rows.push_back(lineAt(lines, l));
    }
    if (rows.size() < 2 || !delimiterRow(rows[1])) {
        return std::nullopt;
    }
    Table t;
    t.header = splitRow(rows[0]);
    const auto delimiters = splitRow(rows[1]);
    if (t.header.size() != delimiters.size()) {
        return std::nullopt;  // (GFM: not a table)
    }
    for (const std::string& d: delimiters) {
        t.align.push_back(alignOf(d));
    }
    for (size_t r = 2; r < rows.size(); ++r) {
        auto cells = splitRow(rows[r]);
        cells.resize(t.columns());
        t.rows.push_back(std::move(cells));
    }
    return t;
}

std::string write(const Table& table) {
    const size_t columns = std::max<size_t>(1, table.header.size());
    std::vector<std::vector<std::string>> cells;
    cells.push_back(table.header);
    for (const auto& r: table.rows) {
        cells.push_back(r);
    }
    for (auto& r: cells) {
        r.resize(columns);
        for (auto& c: r) {
            c = escapeCell(c);
        }
    }
    std::vector<size_t> widths(columns, 3);
    for (const auto& r: cells) {
        for (size_t c = 0; c < columns; ++c) {
            widths[c] = std::max(widths[c], displayWidth(r[c]));
        }
    }
    const auto alignOfColumn = [&](size_t c) { return c < table.align.size() ? table.align[c] : Align::None; };
    const auto row = [&](const std::vector<std::string>& r) {
        std::string line = "|";
        for (size_t c = 0; c < columns; ++c) {
            line += " " + pad(r[c], widths[c], alignOfColumn(c)) + " |";
        }
        return line;
    };
    std::string out = row(cells[0]) + "\n|";
    for (size_t c = 0; c < columns; ++c) {
        const Align a = alignOfColumn(c);
        std::string d(widths[c], '-');
        if (a == Align::Left || a == Align::Center) {
            d.front() = ':';
        }
        if (a == Align::Right || a == Align::Center) {
            d.back() = ':';
        }
        out += " " + d + " |";
    }
    for (size_t r = 1; r < cells.size(); ++r) {
        out += "\n" + row(cells[r]);
    }
    return out;
}

std::optional<Found> at(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    const size_t ls = lineStart(text, offset);
    const auto inTable = [&](size_t l) {
        const std::string_view line = lineAt(text, l);
        return !blank(line) && line.find('|') != std::string_view::npos;
    };
    if (!inTable(ls)) {
        return std::nullopt;
    }
    size_t first = ls;
    while (first > 0 && inTable(lineStart(text, first - 1))) {
        first = lineStart(text, first - 1);
    }
    size_t last = ls;
    while (lineEnd(text, last) < text.size() && inTable(nextLine(text, last))) {
        last = nextLine(text, last);
    }
    // The header: the line before the first delimiter row
    size_t header = std::string_view::npos;
    for (size_t l = first; l < last;) {
        const size_t next = nextLine(text, l);
        if (delimiterRow(lineAt(text, next))) {
            header = l;
            break;
        }
        l = next;
    }
    if (header == std::string_view::npos || ls < header) {
        return std::nullopt;
    }
    Found f;
    f.begin = header;
    f.end = lineEnd(text, last);
    auto t = parse(text.substr(f.begin, f.end - f.begin));
    if (!t) {
        return std::nullopt;
    }
    f.table = std::move(*t);
    // The row: the lines from the header to the cursor's (the delimiter row counts as the header)
    size_t n = 0;
    for (size_t l = header; l < ls; l = nextLine(text, l)) {
        ++n;
    }
    f.row = n <= 1 ? 0 : n - 1;
    // The column: the pipes before the cursor
    const std::string_view line = lineAt(text, ls);
    const auto pipes = pipesOf(line);
    const size_t lead = line.find_first_not_of(" \t");
    size_t column = 0;
    for (size_t p: pipes) {
        if (p < offset - ls && !(p == lead)) {
            ++column;
        }
    }
    f.column = std::min(column, f.table.columns() > 0 ? f.table.columns() - 1 : 0);
    return f;
}

format::Edit replaceOrInsert(std::string_view text, size_t anchor, size_t caret, const Table& table) {
    const std::string written = write(table);
    if (auto f = at(text, caret)) {
        format::Edit e{f->begin, f->end, written, f->begin + written.size(), f->begin + written.size()};
        return e;
    }
    return format::insertBlock(text, anchor, caret, written);
}

}  // namespace xqt::md::table
