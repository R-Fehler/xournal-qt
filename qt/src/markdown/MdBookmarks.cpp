#include "MdBookmarks.h"

#include "MdText.h"

namespace xqt::md::bookmarks {

namespace {
using namespace text;

constexpr std::string_view SPACE = " \t\r\n";

std::string_view trimmed(std::string_view s) {
    const size_t a = s.find_first_not_of(SPACE);
    if (a == std::string_view::npos) {
        return {};
    }
    const size_t b = s.find_last_not_of(SPACE);
    return s.substr(a, b - a + 1);
}

/// Whether a top-level block is a comment (not drawn: MdLayout), or a page break (not drawn either).
bool hidden(const Block& b) {
    if (b.kind != BlockKind::Html) {
        return false;
    }
    const std::string s = plainText(b);
    const std::string_view t = trimmed(s);
    return (t.substr(0, 4) == "<!--" && t.size() >= 7 && t.substr(t.size() - 3) == "-->") || pageBreak(t);
}

/// White space (also the Unicode line and paragraph separators) as one space, trimmed.
std::string simplified(std::string_view s) {
    std::string out;
    bool space = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        bool ws = c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        size_t skip = 0;
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(s[i + 2]) == 0xA8 || static_cast<unsigned char>(s[i + 2]) == 0xA9)) {
            ws = true;  // U+2028, U+2029 (a hard line break of a paragraph)
            skip = 2;
        }
        if (ws) {
            space = !out.empty();
            i += skip;
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += s[i];
    }
    return out;
}

std::string headingText(const Block& b) { return simplified(plainText(b)); }
}  // namespace

std::optional<std::string> labelOf(std::string_view html) {
    const std::string_view t = trimmed(html);
    if (t.find('\n') != std::string_view::npos || t.substr(0, 4) != "<!--" || t.size() < 7 ||
        t.substr(t.size() - 3) != "-->") {
        return std::nullopt;
    }
    std::string_view inner = t.substr(4, t.size() - 7);
    const size_t key = inner.find_first_not_of(" \t");
    if (key == std::string_view::npos || inner.substr(key, KEY.size()) != KEY) {
        return std::nullopt;
    }
    inner.remove_prefix(key + KEY.size());
    if (!inner.empty() && inner.front() != ' ' && inner.front() != '\t') {
        return std::nullopt;  // ("xqt:bookmarks", …)
    }
    if (inner.find("-->") != std::string_view::npos) {
        return std::nullopt;  // (the comment ends before: text after it)
    }
    return std::string(trimmed(inner));
}

bool isMark(const Block& block) { return block.kind == BlockKind::Html && labelOf(plainText(block)).has_value(); }

std::vector<Mark> find(std::string_view source, const Document& doc) {
    std::vector<Mark> marks;
    if (doc.plain || !mayContain(source)) {
        return marks;
    }
    const auto& blocks = doc.root.children;
    std::vector<BlockSpan> spans;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].kind != BlockKind::Html) {
            continue;
        }
        auto label = labelOf(plainText(blocks[i]));
        if (!label) {
            continue;
        }
        if (spans.empty()) {
            spans = topLevelSpans(source, doc);
        }
        Mark m;
        m.begin = spans[i].begin;
        m.lineEnd = lineEnd(source, m.begin);
        m.end = nextLine(source, m.begin);
        m.block = i;
        m.label = std::move(*label);
        marks.push_back(std::move(m));
    }
    return marks;
}

std::vector<Mark> find(std::string_view source) {
    if (!mayContain(source)) {
        return {};
    }
    return find(source, parse(source));
}

std::string cleanLabel(std::string_view label) {
    std::string s = simplified(label);
    for (size_t at = s.find("--"); at != std::string::npos; at = s.find("--", at)) {
        s.replace(at, 2, "\xe2\x80\x93");  // "–": "--" may not be in a comment, "-->" would end it
    }
    return s;
}

std::string comment(std::string_view label) {
    const std::string l = cleanLabel(label);
    return l.empty() ? std::string("<!-- ") + std::string(KEY) + " -->"
                     : std::string("<!-- ") + std::string(KEY) + " " + l + " -->";
}

std::optional<PageMark> ofPage(std::string_view slice) {
    if (!mayContain(slice)) {
        return std::nullopt;
    }
    const Document doc = parse(slice);
    const std::vector<Mark> marks = find(slice, doc);
    if (marks.empty()) {
        return std::nullopt;
    }
    const Mark& m = marks.front();
    const auto& blocks = doc.root.children;
    PageMark p;
    // The automatic label: the heading it marks, else the first heading of the page
    size_t next = m.block + 1;
    while (next < blocks.size() && hidden(blocks[next])) {
        ++next;
    }
    if (next < blocks.size() && blocks[next].kind == BlockKind::Heading) {
        p.automatic = headingText(blocks[next]);
    }
    for (size_t i = 0; i < blocks.size() && p.automatic.empty(); ++i) {
        if (blocks[i].kind == BlockKind::Heading) {
            p.automatic = headingText(blocks[i]);
        }
    }
    p.isAutomatic = m.label.empty();
    p.label = p.isAutomatic ? p.automatic : m.label;
    return p;
}

std::optional<Edit> add(std::string_view source, size_t begin, size_t end, std::string_view label,
                        bool* onEarlierPage) {
    if (onEarlierPage) {
        *onEarlierPage = false;
    }
    const Document doc = parse(source);
    if (doc.plain) {
        return std::nullopt;
    }
    const auto& blocks = doc.root.children;
    const std::vector<BlockSpan> spans = topLevelSpans(source, doc);
    // The first block that starts on the page, else the one that goes on over it
    size_t target = blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (spans[i].begin >= begin && spans[i].begin < end && !hidden(blocks[i])) {
            target = i;
            break;
        }
    }
    bool earlier = false;
    if (target == blocks.size()) {
        for (size_t i = 0; i < blocks.size() && spans[i].begin < begin; ++i) {
            if (!hidden(blocks[i])) {
                target = i;
            }
        }
        earlier = target < blocks.size();
    }
    const std::string line = comment(label) + "\n";
    if (target == blocks.size()) {
        // Nothing but blank lines and comments: at the end of the page's part (on a line of its own)
        const size_t at = std::min(end, source.size());
        const bool needsBreak = at > 0 && source[at - 1] != '\n';
        return Edit{at, at, (needsBreak ? "\n" : "") + line};
    }
    if (onEarlierPage) {
        *onEarlierPage = earlier;
    }
    // Already marked: a bookmark among the comments right before it
    for (size_t i = target; i > 0 && hidden(blocks[i - 1]); --i) {
        if (isMark(blocks[i - 1])) {
            return std::nullopt;
        }
    }
    return Edit{spans[target].begin, spans[target].begin, line};
}

std::optional<Edit> remove(std::string_view source, size_t begin, size_t end) {
    std::vector<Mark> on;
    for (Mark& m: find(source)) {
        if (m.begin >= begin && m.begin < end) {
            on.push_back(std::move(m));
        }
    }
    if (on.empty()) {
        return std::nullopt;
    }
    Edit e{on.front().begin, on.back().end, {}};
    size_t at = e.from;
    for (const Mark& m: on) {
        e.with += source.substr(at, m.begin - at);
        at = m.end;
    }
    return e;
}

std::optional<Edit> rename(std::string_view source, size_t begin, size_t end, std::string_view label) {
    for (const Mark& m: find(source)) {
        if (m.begin >= begin && m.begin < end) {
            return Edit{m.begin, m.lineEnd, comment(label)};
        }
    }
    return std::nullopt;
}

}  // namespace xqt::md::bookmarks
