#include "MdPaginate.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include <pango/pango.h>

#include "MdText.h"

namespace xqt::md {

namespace {
using namespace text;

constexpr size_t MAX_PAGES = 1000;
constexpr uint16_t INLINE_SPANS = Strong | Emphasis | Strike | Code | Link | Underline | Image | Math;

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
        if (!at || (at->flags & Math)) {
            return std::nullopt;  // (not at a formula: its "$" or "$$" is before it)
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
    std::vector<BlockSpan> spans;
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

Pagination paginate(const std::string& source, Style style, const std::function<Frame(size_t)>& frame,
                    const Pagination* before, const std::string* beforeSource) {
    Pagination out;
    size_t pos = 0;      // where the rest of the text starts in the source
    // the lines the next page starts with (marker, fence, table header); a plain text: its marker on every page
    std::string prefix = style.plain ? std::string(PLAIN_MARKER) + "\n" : std::string();
    const std::string plainNext = std::string(PLAIN_CONTINUATION) + "\n";
    size_t page = 0;
    // After a change: the pages before the one with the change are as they were (its first block could go back onto
    // the page before: from that one on); the text after the change is the text from before, moved by `delta`
    const bool incremental = before && beforeSource && !before->parts.empty();
    size_t changeEnd = 0;  // (in the new text)
    std::ptrdiff_t delta = 0;
    if (incremental) {
        const std::string& old = *beforeSource;
        size_t common = 0;
        while (common < old.size() && common < source.size() && old[common] == source[common]) {
            ++common;
        }
        size_t suffix = 0;
        while (suffix < old.size() - common && suffix < source.size() - common &&
               old[old.size() - 1 - suffix] == source[source.size() - 1 - suffix]) {
            ++suffix;
        }
        changeEnd = source.size() - suffix;
        delta = static_cast<std::ptrdiff_t>(source.size()) - static_cast<std::ptrdiff_t>(old.size());
        size_t start = 0;
        while (start + 1 < before->parts.size() && before->parts[start + 1].begin <= common) {
            ++start;
        }
        start = start > 0 ? start - 1 : 0;
        for (size_t i = 0; i < start; ++i) {
            out.slices.push_back(before->slices[i]);
            out.parts.push_back(before->parts[i]);
            out.overflow = std::max(out.overflow, before->parts[i].overflow);
        }
        page = start;
        pos = before->parts[start].begin;
        prefix = before->slices[start].substr(0, before->parts[start].prefix);
    }
    for (;; ++page) {
        // After the change, a page that starts where one did before (with the same lines): the rest is as it was
        if (incremental && page < before->parts.size() && page > 0 && pos >= changeEnd && pos > 0 &&
            before->parts[page].begin == static_cast<size_t>(static_cast<std::ptrdiff_t>(pos) - delta) &&
            before->parts[page].prefix == prefix.size() &&
            before->slices[page].compare(0, prefix.size(), prefix) == 0) {
            for (size_t i = page; i < before->parts.size(); ++i) {
                Part p = before->parts[i];
                p.begin = static_cast<size_t>(static_cast<std::ptrdiff_t>(p.begin) + delta);
                p.end = static_cast<size_t>(static_cast<std::ptrdiff_t>(p.end) + delta);
                out.slices.push_back(before->slices[i]);
                out.parts.push_back(p);
                out.overflow = std::max(out.overflow, p.overflow);
            }
            break;
        }
        const Frame f = frame(page);
        style.width = f.width;
        const std::string rest = prefix + source.substr(pos);
        // Only the blocks that can be on the page (and a few more): as many as the first ones say, more if needed
        const Document all = parse(rest);
        const auto spans = topLevelSpans(rest, all);
        // A page break (`<div style="page-break-after: always"></div>`): the page ends after it, when what is before
        // it fits (one at the top of a page, before anything else, is already where a page starts)
        size_t limit = spans.size();
        bool content = false;
        for (size_t i = 0; i < spans.size(); ++i) {
            if (spans[i].begin < prefix.size()) {
                continue;  // (the lines added for the page)
            }
            if (all.root.children[i].kind == BlockKind::Html &&
                pageBreak(std::string_view(rest).substr(spans[i].begin, spans[i].end - spans[i].begin))) {
                if (content) {
                    limit = i + 1;
                    break;
                }
                continue;
            }
            content = true;
        }
        size_t blocks = std::min<size_t>(limit, 12);
        std::string laidOut;
        Document some;
        Layout lay;
        bool whole = false;
        for (;;) {
            whole = blocks >= spans.size();
            if (whole) {
                laidOut = rest;
                lay = layout(all, style);
            } else {
                laidOut = rest.substr(0, spans[blocks].begin);
                some = parse(laidOut);
                lay = layout(some, style);
            }
            if (whole || lay.height > f.height + 0.01 || blocks >= limit) {
                break;
            }
            blocks = std::min(limit, std::max(blocks + 4, static_cast<size_t>(static_cast<double>(blocks) * f.height /
                                                                              std::max(lay.height, 1.0) * 1.3)));
        }
        const Document& doc = whole ? all : some;
        if ((whole && lay.height <= f.height + 0.01) || page + 1 >= MAX_PAGES) {
            const double overflow = std::max(0.0, lay.height - f.height);
            out.slices.push_back(rest);
            out.parts.push_back({pos, source.size(), prefix.size(), overflow});
            out.overflow = std::max(out.overflow, overflow);
            break;
        }
        Split split = !whole && blocks >= limit && lay.height <= f.height + 0.01
                              ? Split{spans[limit].begin, "", marker("block"), 0}  // (after the page break)
                              : Splitter(laidOut, prefix.size(), doc, lay, f.height, startOf(lineAt(prefix, 0)),
                                         style.size).find();
        if (style.plain) {
            split.close.clear();
            split.next = plainNext;  // (lines as they are: nothing added, only the marker)
        }
        out.overflow = std::max(out.overflow, split.overflow);
        const size_t next = pos + (split.at - prefix.size());
        if (next >= source.size() || blankText(std::string_view(source).substr(next))) {
            out.slices.push_back(rest);  // (only blank lines would be left for the next page)
            out.parts.push_back({pos, source.size(), prefix.size(), split.overflow});
            break;
        }
        out.slices.push_back(rest.substr(0, split.at) + split.close);
        out.parts.push_back({pos, next, prefix.size(), split.overflow});
        pos = next;
        prefix = split.next;
    }
    if (out.overflow < 0.01) {
        out.overflow = 0;
    }
    return out;
}

Pagination onePage(const std::string& source, const Style& style) {
    const std::string prefix = style.plain ? std::string(PLAIN_MARKER) + "\n" : std::string();
    Pagination out;
    out.slices.push_back(prefix + source);
    out.parts.push_back({0, source.size(), prefix.size(), 0});
    return out;
}

std::string join(const std::vector<std::string>& slices, std::vector<Part>* parts) {
    std::string out;
    if (parts) {
        parts->clear();
    }
    for (size_t i = 0; i < slices.size(); ++i) {
        std::string_view s = slices[i];
        const size_t sliceSize = s.size();
        size_t removedAtEnd = 0;  // (a closing fence of the slice before)
        if (i == 0 && isPlain(s)) {
            s.remove_prefix(nextLine(s, 0));  // (a plain text's marker)
        } else if (i > 0 && continues(s)) {
            const std::string_view first = lineAt(s, 0);
            s.remove_prefix(nextLine(s, 0));
            const bool code = first.find(" code") != std::string_view::npos;
            const bool table = first.find(" table") != std::string_view::npos;
            if (code) {
                // The fence that closed the page before and the fence that opened this one were added
                const size_t last = out.empty() ? 0 : lineStart(out, out.size() - 1);
                if (!out.empty() && out.back() == '\n' && !fenceOf(lineAt(out, last)).empty()) {
                    removedAtEnd = out.size() - last;
                    out.erase(last);
                }
                s.remove_prefix(nextLine(s, 0));
            } else if (table) {
                s.remove_prefix(nextLine(s, 0));  // the header and its delimiter row
                s.remove_prefix(nextLine(s, 0));
            }
        }
        if (parts) {
            if (!parts->empty()) {
                parts->back().end -= removedAtEnd;
            }
            parts->push_back({out.size(), out.size() + s.size(), sliceSize - s.size()});
        }
        out += s;
    }
    return out;
}

}  // namespace xqt::md
