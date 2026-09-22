#include "MdPaginate.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include <pango/pango.h>

namespace xqt::md {

namespace {

constexpr size_t MAX_PAGES = 1000;
constexpr uint16_t INLINE_SPANS = Strong | Emphasis | Strike | Code | Link | Underline | Image | Math;

// --- lines of a text ---------------------------------------------------------------------------------------------
size_t lineStart(std::string_view s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    const size_t i = s.rfind('\n', std::min(pos, s.size()) - 1);
    return i == std::string_view::npos ? 0 : i + 1;
}
/// The start of the line after the one `pos` is in.
size_t nextLine(std::string_view s, size_t pos) {
    const size_t i = s.find('\n', pos);
    return i == std::string_view::npos ? s.size() : i + 1;
}
std::string_view lineAt(std::string_view s, size_t start) {
    const size_t end = s.find('\n', start);
    return s.substr(start, (end == std::string_view::npos ? s.size() : end) - start);
}
bool blank(std::string_view line) { return line.find_first_not_of(" \t\r") == std::string_view::npos; }
std::string_view afterIndent(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    return line.substr(i);
}
/// The fence of a fenced code block's opening line ("```", "~~~~"); empty if it is none.
std::string fenceOf(std::string_view line) {
    line = afterIndent(line);
    if (line.empty() || (line[0] != '`' && line[0] != '~')) {
        return {};
    }
    const size_t n = line.find_first_not_of(line[0]);
    const size_t count = n == std::string_view::npos ? line.size() : n;
    return count >= 3 ? std::string(count, line[0]) : std::string();
}
bool closesFence(std::string_view line, const std::string& fence) {
    line = afterIndent(line);
    if (fence.empty() || line.size() < fence.size() || line[0] != fence[0]) {
        return false;
    }
    const size_t n = line.find_first_not_of(fence[0]);
    return n == std::string_view::npos ? line.size() >= fence.size() : n >= fence.size() && blank(line.substr(n));
}
bool setextUnderline(std::string_view line) {
    line = afterIndent(line);
    if (line.empty() || (line[0] != '=' && line[0] != '-')) {
        return false;
    }
    const size_t n = line.find_first_not_of(line[0]);
    return n == std::string_view::npos || blank(line.substr(n));
}
/// Whether a line would start a block other than a paragraph (a paragraph must not continue with it on a page).
bool startsBlock(std::string_view line) {
    const std::string_view s = afterIndent(line);
    if (s.empty()) {
        return true;
    }
    const char c = s[0];
    if (c == '#' || c == '>' || c == '<' || c == '|' || c == '`' || c == '~' || c == '=' || c == '\t') {
        return true;
    }
    if ((c == '-' || c == '*' || c == '+') && (s.size() == 1 || s[1] == ' ' || s[1] == c)) {
        return true;
    }
    if (c >= '0' && c <= '9') {
        const size_t n = s.find_first_not_of("0123456789");
        return n != std::string_view::npos && n <= 9 && (s[n] == '.' || s[n] == ')');
    }
    return line.size() >= 4 && line.substr(0, 4) == "    ";  // (indented code)
}

// --- where the top-level blocks are in the source ----------------------------------------------------------------
struct Span {
    size_t begin = 0;  ///< the start of its first line
    size_t end = 0;    ///< the start of the line after its last one
};

/// Source ranges (whole lines) of the top-level blocks. What matters for splitting is where each block begins; the
/// lines between two blocks (blank lines) stay with the block before.
std::vector<Span> topLevelSpans(std::string_view src, const Document& doc) {
    std::vector<Span> spans;
    size_t prevEnd = 0;
    for (const Block& b: doc.root.children) {
        size_t first = 0;
        if (b.textBegin != NO_SOURCE && b.textBegin >= prevEnd) {
            first = lineStart(src, b.textBegin);
            if (b.kind == BlockKind::CodeBlock && b.fenced && first > prevEnd) {
                first = lineStart(src, first - 1);  // the fence before the code
            }
        } else {
            first = prevEnd;  // (no text, e.g. a rule: its first line that is not blank)
            while (first < src.size() && blank(lineAt(src, first))) {
                first = nextLine(src, first);
            }
        }
        first = std::max(first, prevEnd);
        size_t end = 0;
        if (b.kind == BlockKind::CodeBlock && b.fenced) {
            const std::string fence = fenceOf(lineAt(src, first));
            end = src.size();
            for (size_t l = nextLine(src, first); l < src.size(); l = nextLine(src, l)) {
                if (closesFence(lineAt(src, l), fence)) {
                    end = nextLine(src, l);
                    break;
                }
            }
        } else if (b.textEnd != NO_SOURCE && b.textEnd > first) {
            end = nextLine(src, b.textEnd - 1);
            if (b.kind == BlockKind::Heading && end < src.size() && setextUnderline(lineAt(src, end))) {
                end = nextLine(src, end);
            }
        } else {
            end = nextLine(src, first);
        }
        spans.push_back({first, std::max(end, first)});
        prevEnd = spans.back().end;
    }
    return spans;
}

std::string marker(const std::string& kind) { return std::string(CONTINUATION) + " " + kind + " -->\n"; }

/// Where a page ends in the text being split (`rest`: the continuation lines of the page, then the rest).
struct Split {
    size_t at = 0;       ///< the next page starts here
    std::string close;   ///< lines this page ends with (a closing fence)
    std::string next;    ///< lines the next page starts with (the marker, an opening fence, a table header)
    double overflow = 0;
};

class Splitter {
public:
    Splitter(const std::string& rest, size_t contentStart, const Document& doc, const Layout& lay, double height,
             unsigned listStart, double bodySize):
            rest(rest), contentStart(contentStart), doc(doc), lay(lay), height(height), listStart(listStart),
            codePadding(0.6 * bodySize), spans(topLevelSpans(rest, doc)) {}

    Split find() const {
        const auto& blocks = doc.root.children;
        size_t k = 0;
        while (k < blocks.size() && lay.blocks[k].bottom <= height + 0.01) {
            ++k;
        }
        if (k >= blocks.size()) {  // (everything fits: the caller does not ask)
            return {rest.size(), "", marker("block"), 0};
        }
        if (auto s = inside(k)) {
            return *s;
        }
        // Before the block; a heading goes with it
        size_t j = k;
        while (j > 0 && blocks[j - 1].kind == BlockKind::Heading && spans[j - 1].begin > contentStart) {
            --j;
        }
        if (spans[j].begin > contentStart) {
            return {spans[j].begin, "", marker("block"), 0};
        }
        if (spans[k].begin > contentStart) {
            return {spans[k].begin, "", marker("block"), 0};
        }
        // Not even the first block fits: it stays on the page (below its bottom margin)
        size_t after = k + 1;
        while (after < blocks.size() && spans[after].begin <= contentStart) {
            ++after;
        }
        const double overflow = lay.blocks[k].bottom - height;
        if (after >= blocks.size()) {
            return {rest.size(), "", marker("block"), overflow};
        }
        return {spans[after].begin, "", marker("block"), overflow};
    }

private:
    std::optional<Split> inside(size_t k) const {
        const Block& b = doc.root.children[k];
        switch (b.kind) {
            case BlockKind::Paragraph:
                return inParagraph(k);
            case BlockKind::CodeBlock:
                return b.fenced ? inCode(k) : std::nullopt;
            case BlockKind::BulletList:
            case BlockKind::OrderedList:
                return inList(k);
            case BlockKind::Table:
                return inTable(k);
            default:
                return std::nullopt;
        }
    }

    /// The Pango lines of a block's text item: their start (byte in the layout's text) and bottom (box y).
    struct Line {
        int start = 0;
        double bottom = 0;
    };
    std::vector<Line> linesOf(const Item& it) const {
        std::vector<Line> lines;
        PangoLayoutIter* iter = pango_layout_get_iter(it.layout.get());
        do {
            PangoRectangle logical;
            pango_layout_iter_get_line_extents(iter, nullptr, &logical);
            lines.push_back({pango_layout_iter_get_index(iter),
                             it.y + (logical.y + logical.height) / static_cast<double>(PANGO_SCALE)});
        } while (pango_layout_iter_next_line(iter));
        pango_layout_iter_free(iter);
        return lines;
    }

    std::optional<Split> inParagraph(size_t k) const {
        const int index = lay.blocks[k].item;
        if (index < 0) {
            return std::nullopt;
        }
        const Item& it = lay.items[static_cast<size_t>(index)];
        const auto lines = linesOf(it);
        const int n = static_cast<int>(lines.size());
        int fits = -1;  // the last line that fits
        while (fits + 1 < n && lines[static_cast<size_t>(fits + 1)].bottom <= height + 0.01) {
            ++fits;
        }
        // At least two lines on each page where there are enough (no lone first or last line)
        fits = std::min(fits, n >= 4 ? n - 3 : n - 2);
        for (int l = fits; l >= (n >= 4 ? 1 : 0); --l) {
            const int byte = lines[static_cast<size_t>(l + 1)].start;
            const auto at = sourceOf(it, byte);
            if (at && *at > contentStart && *at < rest.size() && !startsBlock(lineAt(rest, *at))) {
                return Split{*at, "", marker("p"), 0};
            }
        }
        return std::nullopt;
    }

    /// The source offset of a byte of an item's text; nothing inside inline formatting (**, links, ...) or where
    /// the text is not the source's (entities).
    std::optional<size_t> sourceOf(const Item& it, int byte) const {
        const SourceMap* at = nullptr;
        const SourceMap* before = nullptr;
        for (const SourceMap& m: it.sources) {
            if (byte >= m.start && byte < m.start + m.length) {
                at = &m;
                break;
            }
            before = &m;
        }
        if (!at) {
            return std::nullopt;
        }
        if (byte > at->start) {  // inside a run
            if ((at->flags & INLINE_SPANS) || at->source == NO_SOURCE || at->sourceLength != size_t(at->length)) {
                return std::nullopt;
            }
            return at->source + static_cast<size_t>(byte - at->start);
        }
        if (before && (before->flags & at->flags & INLINE_SPANS)) {
            return std::nullopt;  // between two runs of the same formatting
        }
        if (at->source != NO_SOURCE) {
            return at->source;
        }
        return std::nullopt;
    }

    std::optional<Split> inCode(size_t k) const {
        const int index = lay.blocks[k].item;
        const std::string fence = fenceOf(lineAt(rest, spans[k].begin));
        if (index < 0 || fence.empty()) {
            return std::nullopt;
        }
        const Item& it = lay.items[static_cast<size_t>(index)];
        const std::string text = pango_layout_get_text(it.layout.get());
        const auto lines = linesOf(it);
        // The last line that fits and starts a line of the code (not a wrapped part of one), after the first line
        std::optional<size_t> at;
        // (the code's background goes on below its last line)
        for (size_t l = 1; l < lines.size() && lines[l - 1].bottom + codePadding <= height + 0.01; ++l) {
            const auto byte = static_cast<size_t>(lines[l].start);
            if (byte == 0 || text[byte - 1] != '\n') {
                continue;
            }
            // That code line in the source: the n-th line after the opening fence
            const auto n = static_cast<size_t>(std::count(text.begin(), text.begin() + static_cast<long>(byte), '\n'));
            size_t src = nextLine(rest, spans[k].begin);
            for (size_t i = 0; i < n && src < rest.size(); ++i) {
                src = nextLine(rest, src);
            }
            if (src > contentStart && src < spans[k].end) {
                at = src;
            }
        }
        if (!at) {
            return std::nullopt;
        }
        const std::string open(lineAt(rest, spans[k].begin));
        return Split{*at, fence + "\n", marker("code") + open + "\n", 0};
    }

    std::optional<Split> inList(size_t k) const {
        const Block& list = doc.root.children[k];
        const auto& parts = lay.blocks[k].parts;
        std::optional<Split> best;
        for (size_t i = 0; i + 1 < parts.size() && i + 1 < list.children.size() && parts[i].bottom <= height + 0.01;
             ++i) {
            const Block& next = list.children[i + 1];
            if (next.textBegin == NO_SOURCE) {
                continue;
            }
            const size_t at = lineStart(rest, next.textBegin);
            if (at <= contentStart) {
                continue;
            }
            std::string kind = "list";
            if (list.kind == BlockKind::OrderedList) {
                const unsigned first = listStart > 0 && isFirstContent(k) ? listStart : list.start;
                kind += " start=" + std::to_string(first + i + 1);
            }
            best = Split{at, "", marker(kind), 0};
        }
        return best;
    }

    std::optional<Split> inTable(size_t k) const {
        const auto& rows = lay.blocks[k].parts;  // header, then the body rows
        const size_t header = spans[k].begin;
        const size_t delimiter = nextLine(rest, header);
        std::optional<Split> best;
        for (size_t r = 1; r + 1 < rows.size() && rows[r].bottom <= height + 0.01; ++r) {
            // Row r + 1 is the (r + 2)-th line of the table
            size_t at = nextLine(rest, delimiter);
            for (size_t i = 1; i <= r && at < rest.size(); ++i) {
                at = nextLine(rest, at);
            }
            if (at > contentStart && at < spans[k].end) {
                best = Split{at, "",
                             marker("table") + std::string(lineAt(rest, header)) + "\n" +
                                     std::string(lineAt(rest, delimiter)) + "\n",
                             0};
            }
        }
        return best;
    }

    /// Whether block k is the first block after the continuation lines.
    bool isFirstContent(size_t k) const {
        for (size_t i = 0; i < k; ++i) {
            if (spans[i].begin >= contentStart) {
                return false;
            }
        }
        return true;
    }

    const std::string& rest;
    size_t contentStart;
    const Document& doc;
    const Layout& lay;
    double height;
    unsigned listStart;
    double codePadding;
    std::vector<Span> spans;
};

/// "start=N" of a continuation marker (0: none).
unsigned startOf(std::string_view markerLine) {
    const auto at = markerLine.find("start=");
    return at == std::string_view::npos ? 0
                                        : static_cast<unsigned>(std::strtoul(markerLine.data() + at + 6, nullptr, 10));
}

bool blankText(std::string_view s) { return s.find_first_not_of(" \t\r\n") == std::string_view::npos; }

}  // namespace

bool continues(std::string_view slice) { return slice.substr(0, CONTINUATION.size()) == CONTINUATION; }

Pagination paginate(const std::string& source, Style style, const std::function<Frame(size_t)>& frame) {
    Pagination out;
    size_t pos = 0;      // where the rest of the text starts in the source
    std::string prefix;  // the lines the next page starts with (marker, fence, table header)
    for (size_t page = 0;; ++page) {
        const Frame f = frame(page);
        style.width = f.width;
        const std::string rest = prefix + source.substr(pos);
        const Document doc = parse(rest);
        const Layout lay = layout(doc, style);
        if (lay.height <= f.height + 0.01 || page + 1 >= MAX_PAGES) {
            out.slices.push_back(rest);
            out.overflow = std::max(out.overflow, lay.height - f.height);
            break;
        }
        const Split split =
                Splitter(rest, prefix.size(), doc, lay, f.height, startOf(lineAt(prefix, 0)), style.size).find();
        out.overflow = std::max(out.overflow, split.overflow);
        const size_t next = pos + (split.at - prefix.size());
        if (next >= source.size() || blankText(std::string_view(source).substr(next))) {
            out.slices.push_back(rest);  // (only blank lines would be left for the next page)
            break;
        }
        out.slices.push_back(rest.substr(0, split.at) + split.close);
        pos = next;
        prefix = split.next;
    }
    if (out.overflow < 0.01) {
        out.overflow = 0;
    }
    return out;
}

std::string join(const std::vector<std::string>& slices) {
    std::string out;
    for (size_t i = 0; i < slices.size(); ++i) {
        std::string_view s = slices[i];
        if (i > 0 && continues(s)) {
            const std::string_view first = lineAt(s, 0);
            s.remove_prefix(nextLine(s, 0));
            const bool code = first.find(" code") != std::string_view::npos;
            const bool table = first.find(" table") != std::string_view::npos;
            if (code) {
                // The fence that closed the page before and the fence that opened this one were added
                const size_t last = out.empty() ? 0 : lineStart(out, out.size() - 1);
                if (!out.empty() && out.back() == '\n' && !fenceOf(lineAt(out, last)).empty()) {
                    out.erase(last);
                }
                s.remove_prefix(nextLine(s, 0));
            } else if (table) {
                s.remove_prefix(nextLine(s, 0));  // the header and its delimiter row
                s.remove_prefix(nextLine(s, 0));
            }
        }
        out += s;
    }
    return out;
}

}  // namespace xqt::md
