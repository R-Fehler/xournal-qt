#include "MdDocument.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <utility>

#include "md4c.h"

#include "MdTexDelimiters.h"
#include "MdText.h"

namespace xqt::md {

namespace {

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
    }
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// "&amp;", "&#123;", "&#x1F600;" as text. md4c leaves entities to the application; the common named ones are
/// known here, others stay as written.
std::string decodeEntity(std::string_view e) {
    std::string out;
    if (e.size() >= 4 && e[1] == '#') {
        const bool hex = e[2] == 'x' || e[2] == 'X';
        const std::string digits(e.substr(hex ? 3 : 2, e.size() - (hex ? 4 : 3)));
        appendUtf8(out, static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10)));
        return out;
    }
    static const std::pair<std::string_view, uint32_t> named[] = {
            {"&amp;", '&'},      {"&lt;", '<'},        {"&gt;", '>'},        {"&quot;", '"'},    {"&apos;", '\''},
            {"&nbsp;", 0xA0},    {"&copy;", 0xA9},     {"&reg;", 0xAE},      {"&deg;", 0xB0},    {"&times;", 0xD7},
            {"&laquo;", 0xAB},   {"&raquo;", 0xBB},    {"&ndash;", 0x2013},  {"&mdash;", 0x2014}, {"&hellip;", 0x2026},
            {"&euro;", 0x20AC},  {"&larr;", 0x2190},   {"&rarr;", 0x2192},   {"&check;", 0x2713}, {"&middot;", 0xB7},
    };
    for (const auto& [name, cp]: named) {
        if (e == name) {
            appendUtf8(out, cp);
            return out;
        }
    }
    return std::string(e);
}

std::string attributeText(const MD_ATTRIBUTE& a) {
    std::string out;
    if (!a.text) {
        return out;
    }
    for (int i = 0; a.substr_offsets[i] < a.size; ++i) {
        const std::string_view part(a.text + a.substr_offsets[i], a.substr_offsets[i + 1] - a.substr_offsets[i]);
        switch (a.substr_types[i]) {
            case MD_TEXT_ENTITY:
                out += decodeEntity(part);
                break;
            case MD_TEXT_NULLCHAR:
                appendUtf8(out, 0xFFFD);
                break;
            default:
                out += part;
        }
    }
    return out;
}

class Builder {
public:
    /// `source`: the text md4c reads; `original`: the text as written, where `rewritten` (its \(…\) as $…$)
    /// maps the places in `source` to.
    Builder(std::string_view source, std::string_view original, const tex::Rewritten& rewritten):
            source(source), original(original), rewritten(rewritten) {
        stack.push_back(&doc.root);
    }

    Document take() {
        computeRanges(doc.root);
        return std::move(doc);
    }

    int enterBlock(MD_BLOCKTYPE type, void* detail) {
        if (type == MD_BLOCK_DOC) {
            return 0;
        }
        Block b;
        switch (type) {
            case MD_BLOCK_QUOTE:
                b.kind = BlockKind::Quote;
                break;
            case MD_BLOCK_UL: {
                auto* d = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
                b.kind = BlockKind::BulletList;
                b.tight = d->is_tight;
                b.mark = d->mark;
                break;
            }
            case MD_BLOCK_OL: {
                auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
                b.kind = BlockKind::OrderedList;
                b.tight = d->is_tight;
                b.mark = d->mark_delimiter;
                b.start = d->start;
                break;
            }
            case MD_BLOCK_LI: {
                auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
                b.kind = BlockKind::ListItem;
                b.task = d->is_task;
                b.checked = d->is_task && (d->task_mark == 'x' || d->task_mark == 'X');
                if (d->is_task) {
                    b.taskMark = toOriginal(d->task_mark_offset);
                }
                break;
            }
            case MD_BLOCK_HR:
                b.kind = BlockKind::Rule;
                break;
            case MD_BLOCK_H:
                b.kind = BlockKind::Heading;
                b.level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
                break;
            case MD_BLOCK_CODE: {
                auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
                b.kind = BlockKind::CodeBlock;
                b.info = attributeText(d->info);
                b.language = attributeText(d->lang);
                b.fenced = d->fence_char != 0;
                break;
            }
            case MD_BLOCK_HTML:
                b.kind = BlockKind::Html;
                break;
            case MD_BLOCK_P:
                b.kind = BlockKind::Paragraph;
                break;
            case MD_BLOCK_TABLE:
                b.kind = BlockKind::Table;
                break;
            case MD_BLOCK_THEAD:
                b.kind = BlockKind::TableHead;
                break;
            case MD_BLOCK_TBODY:
                b.kind = BlockKind::TableBody;
                break;
            case MD_BLOCK_TR:
                b.kind = BlockKind::TableRow;
                break;
            case MD_BLOCK_TH:
            case MD_BLOCK_TD: {
                b.kind = type == MD_BLOCK_TH ? BlockKind::TableHeaderCell : BlockKind::TableCell;
                switch (static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align) {
                    case MD_ALIGN_LEFT:
                        b.align = Align::Left;
                        break;
                    case MD_ALIGN_CENTER:
                        b.align = Align::Center;
                        break;
                    case MD_ALIGN_RIGHT:
                        b.align = Align::Right;
                        break;
                    default:
                        break;
                }
                break;
            }
            default:
                b.kind = BlockKind::Paragraph;
        }
        ++blockEvents;
        // (the parents on the stack stay where they are: only the innermost block gets children)
        auto& siblings = stack.back()->children;
        siblings.push_back(std::move(b));
        stack.push_back(&siblings.back());
        return 0;
    }

    int leaveBlock(MD_BLOCKTYPE type) {
        ++blockEvents;
        if (type != MD_BLOCK_DOC && stack.size() > 1) {
            stack.pop_back();
        }
        return 0;
    }

    int enterSpan(MD_SPANTYPE type, void* detail) {
        uint16_t flag = 0;
        switch (type) {
            case MD_SPAN_EM:
                flag = Emphasis;
                break;
            case MD_SPAN_STRONG:
                flag = Strong;
                break;
            case MD_SPAN_DEL:
                flag = Strike;
                break;
            case MD_SPAN_CODE:
                flag = Code;
                break;
            case MD_SPAN_U:
                flag = Underline;
                break;
            case MD_SPAN_LATEXMATH:
            case MD_SPAN_LATEXMATH_DISPLAY:
                flag = type == MD_SPAN_LATEXMATH ? Math : Math | DisplayMath;
                math.emplace();
                math->display = type == MD_SPAN_LATEXMATH_DISPLAY;
                math->scanFrom = parsedEnd;
                break;
            case MD_SPAN_A:
                flag = Link;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_A_DETAIL*>(detail)->href));
                doc.wikiLinks.push_back(false);
                break;
            case MD_SPAN_IMG:
                flag = Image;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src));
                doc.wikiLinks.push_back(false);
                if (image) {
                    ++image->nested;  // (an image in an image's alt text: its text is alt text)
                } else {
                    image.emplace();
                    image->scanFrom = parsedEnd;
                    image->link = links.back();
                }
                break;
            case MD_SPAN_WIKILINK:
                flag = Link;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail)->target));
                doc.wikiLinks.push_back(true);
                break;
        }
        spans.push_back(flag);
        return 0;
    }

    int leaveSpan(MD_SPANTYPE type) {
        if ((type == MD_SPAN_LATEXMATH || type == MD_SPAN_LATEXMATH_DISPLAY) && math) {
            endFormula();
        }
        if (type == MD_SPAN_IMG && image) {
            if (image->nested > 0) {
                --image->nested;
            } else {
                endImage();
            }
        }
        if (!spans.empty()) {
            spans.pop_back();
        }
        if ((type == MD_SPAN_A || type == MD_SPAN_IMG || type == MD_SPAN_WIKILINK) && !links.empty()) {
            links.pop_back();
        }
        return 0;
    }

    int text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
        Run r;
        for (uint16_t f: spans) {
            r.flags |= f;
        }
        r.link = links.empty() ? -1 : links.back();
        // md4c passes pointers into the source, except for made-up text (line breaks, indentation, U+0000)
        size_t parsedAt = NO_SOURCE;
        if (text >= source.data() && text + size <= source.data() + source.size()) {
            parsedAt = static_cast<size_t>(text - source.data());
            r.source = toOriginal(parsedAt);
            r.sourceLength = toOriginal(parsedAt + size) - r.source;
            parsedEnd = parsedAt + size;
        }
        const std::string_view s(text, size);
        switch (type) {
            case MD_TEXT_SOFTBR:
                r.text = " ";
                r.source = NO_SOURCE;
                break;
            case MD_TEXT_BR:
                r.text = "\xe2\x80\xa8";  // a line break inside the paragraph
                r.source = NO_SOURCE;
                break;
            case MD_TEXT_NULLCHAR:
                appendUtf8(r.text, 0xFFFD);
                break;
            case MD_TEXT_ENTITY:
                r.text = decodeEntity(s);
                break;
            case MD_TEXT_HTML:
                r.text = std::string(s);
                if (stack.back()->kind != BlockKind::Html) {
                    r.flags |= Html;
                }
                break;
            default:
                // (a "$" of a rewritten "\(" that md4c did not take as a formula's mark: the text as written)
                r.text = std::string(r.source != NO_SOURCE && r.sourceLength != size
                                             ? original.substr(r.source, r.sourceLength)
                                             : s);
        }
        if (math) {  // (a formula's text: kept until its end, see endFormula)
            if (math->runs.empty()) {
                math->firstAt = parsedAt;
            }
            math->runs.push_back(std::move(r));
            return 0;
        }
        runTarget().push_back(std::move(r));
        return 0;
    }

private:
    /// The end of a formula: its runs go into the text. A formula of nothing but blanks ("$ $", "$$ $$") is none:
    /// it is text, marks and all, as they are in the source (MicroTeX would draw nothing).
    void endFormula() {
        Formula f = std::move(*math);
        math.reset();
        const bool blankOnly = std::all_of(f.runs.begin(), f.runs.end(), [](const Run& r) {
            return r.text.find_first_not_of(" \t\r\n") == std::string::npos;
        });
        std::vector<Run>& target = runTarget();
        if (!blankOnly) {
            std::move(f.runs.begin(), f.runs.end(), std::back_inserter(target));
            return;
        }
        // Its marks: before its first text (else the first "$" after the text before it), and the next "$" after
        // them (only blanks are between)
        const size_t mark = f.display ? 2 : 1;
        const size_t open = f.firstAt != NO_SOURCE && f.firstAt >= mark ? f.firstAt - mark : source.find('$', f.scanFrom);
        const size_t close = open == std::string_view::npos ? open : source.find('$', open + mark);
        if (close == std::string_view::npos || close + mark > source.size()) {
            std::move(f.runs.begin(), f.runs.end(), std::back_inserter(target));
            return;
        }
        Run r;
        for (size_t i = 0; i + 1 < spans.size(); ++i) {  // (the formatting around it, not the formula's)
            r.flags |= spans[i];
        }
        r.link = links.empty() ? -1 : links.back();
        r.source = toOriginal(open);
        r.sourceLength = toOriginal(close + mark) - r.source;
        r.text = std::string(original.substr(r.source, r.sourceLength));
        std::replace(r.text.begin(), r.text.end(), '\n', ' ');  // (a line break in it: a space, as elsewhere)
        std::replace(r.text.begin(), r.text.end(), '\r', ' ');
        parsedEnd = std::max(parsedEnd, close + mark);
        target.push_back(std::move(r));
    }

    /// The end of an image: one run for it, whose text is its alt text and whose source is the whole "![…](…)" (as a
    /// formula's run is its whole "$…$"), so the layout draws the picture in its place. If its marks cannot be found,
    /// its alt text stays as it was (text).
    void endImage() {
        PendingImage img = std::move(*image);
        image.reset();
        std::vector<Run>& target = runTarget();
        const size_t open = source.find("![", img.scanFrom);
        const size_t end = open == std::string_view::npos ? open : imageEnd(source, open);
        if (end == std::string_view::npos) {
            std::move(img.runs.begin(), img.runs.end(), std::back_inserter(target));
            return;
        }
        Run r;
        for (size_t i = 0; i + 1 < spans.size(); ++i) {  // (the formatting around it)
            r.flags |= spans[i];
        }
        r.flags |= Image;
        r.link = img.link;
        for (const Run& alt: img.runs) {
            r.text += alt.text;
        }
        r.source = toOriginal(open);
        r.sourceLength = toOriginal(end) - r.source;
        parsedEnd = std::max(parsedEnd, end);
        target.push_back(std::move(r));
    }

    /// Where an image's Markdown that starts at `at` ("![") ends: after its ")" (inline), its "[label]" or its "]"
    /// (references). npos if its alt text is not closed.
    static size_t imageEnd(std::string_view s, size_t at) {
        const size_t n = s.size();
        size_t i = at + 2;
        int depth = 1;
        while (i < n) {
            const char c = s[i];
            if (c == '\\') {
                i += 2;
                continue;
            }
            if (c == '`') {  // (a code span in the alt text: brackets in it do not count)
                size_t ticks = 0;
                while (i + ticks < n && s[i + ticks] == '`') {
                    ++ticks;
                }
                const size_t close = s.find(std::string(ticks, '`'), i + ticks);
                i = close == std::string_view::npos ? i + ticks : close + ticks;
                continue;
            }
            if (c == '[') {
                ++depth;
            } else if (c == ']' && --depth == 0) {
                break;
            }
            ++i;
        }
        if (i >= n) {
            return std::string_view::npos;
        }
        const size_t afterAlt = ++i;
        const auto space = [&](size_t k) { return k < n && (s[k] == ' ' || s[k] == '\t' || s[k] == '\n' || s[k] == '\r'); };
        if (i < n && s[i] == '(') {
            ++i;
            while (space(i)) {
                ++i;
            }
            if (i < n && s[i] == '<') {  // <destination with spaces>
                while (i < n && s[i] != '>' && s[i] != '\n') {
                    i += s[i] == '\\' ? 2 : 1;
                }
                ++i;
            } else {
                int parens = 0;
                while (i < n && !space(i)) {
                    if (s[i] == '\\') {
                        i += 2;
                        continue;
                    }
                    if (s[i] == '(') {
                        ++parens;
                    } else if (s[i] == ')') {
                        if (parens == 0) {
                            break;
                        }
                        --parens;
                    }
                    ++i;
                }
            }
            while (space(i)) {
                ++i;
            }
            if (i < n && (s[i] == '"' || s[i] == '\'' || s[i] == '(')) {  // a title
                const char close = s[i] == '(' ? ')' : s[i];
                ++i;
                while (i < n && s[i] != close) {
                    i += s[i] == '\\' ? 2 : 1;
                }
                ++i;
                while (space(i)) {
                    ++i;
                }
            }
            if (i < n && s[i] == ')') {
                return i + 1;
            }
            return afterAlt;  // (not an inline image after all: a reference followed by a parenthesis)
        }
        if (i < n && s[i] == '[') {
            const size_t close = s.find(']', i);
            return close == std::string_view::npos ? afterAlt : close + 1;
        }
        return afterAlt;
    }

    /// Where the runs of text go: an image's alt text, or the block's text.
    std::vector<Run>& runTarget() { return image ? image->runs : textTarget().runs; }

    size_t toOriginal(size_t offset) const { return rewritten.changes.empty() ? offset : rewritten.toSource(offset); }

    /// Where text goes. The text of a tight list item comes without a paragraph: it gets one, so list items only
    /// have blocks (the text before and after a nested list are two paragraphs).
    Block& textTarget() {
        Block& b = *stack.back();
        if (b.kind != BlockKind::ListItem) {
            return b;
        }
        if (b.children.empty() || implicitAt != blockEvents || implicitIn != &b) {
            Block p;
            p.kind = BlockKind::Paragraph;
            b.children.push_back(std::move(p));
            implicitAt = blockEvents;
            implicitIn = &b;
        }
        return b.children.back();
    }

    static void computeRanges(Block& b) {
        size_t begin = NO_SOURCE;
        size_t end = 0;
        auto add = [&](size_t from, size_t to) {
            if (from != NO_SOURCE) {
                begin = std::min(begin, from);
                end = std::max(end, to);
            }
        };
        for (const Run& r: b.runs) {
            add(r.source, r.source + r.sourceLength);
        }
        for (Block& c: b.children) {
            computeRanges(c);
            add(c.textBegin, c.textEnd);
        }
        b.textBegin = begin;
        b.textEnd = begin == NO_SOURCE ? NO_SOURCE : end;
    }

    std::string_view source;
    std::string_view original;
    const tex::Rewritten& rewritten;
    Document doc;
    std::vector<Block*> stack;
    std::vector<uint16_t> spans;
    std::vector<int> links;
    /// The formula being read: its runs wait for its end (endFormula)
    struct Formula {
        bool display = false;
        size_t scanFrom = 0;         ///< the end of the text before it (in the parsed text)
        size_t firstAt = NO_SOURCE;  ///< where its first text is, if it is in the parsed text
        std::vector<Run> runs;
    };
    std::optional<Formula> math;
    /// The image being read: its alt text waits for its end (endImage)
    struct PendingImage {
        size_t scanFrom = 0;  ///< the end of the text before it (in the parsed text)
        int link = -1;
        int nested = 0;       ///< images inside its alt text
        std::vector<Run> runs;
    };
    std::optional<PendingImage> image;
    size_t parsedEnd = 0;  ///< the end of the last text that is in the parsed text
    size_t blockEvents = 0;
    size_t implicitAt = static_cast<size_t>(-1);
    const Block* implicitIn = nullptr;
};

}  // namespace

bool isPlain(std::string_view source) {
    const auto startsWithLine = [&](std::string_view marker) {
        return source.substr(0, marker.size()) == marker &&
               (source.size() == marker.size() || source[marker.size()] == '\n');
    };
    return startsWithLine(PLAIN_MARKER) || startsWithLine(PLAIN_CONTINUATION);
}

namespace {
/// A plain text: the marker line (a comment, not shown), then a paragraph of one run per line, the text as it is.
Document parsePlain(std::string_view source) {
    using namespace text;
    Document doc;
    doc.plain = true;
    Block marker;
    marker.kind = BlockKind::Html;
    const std::string_view first = lineAt(source, 0);
    marker.runs.push_back(Run{std::string(first), Html, -1, 0, first.size()});
    marker.textBegin = 0;
    marker.textEnd = first.size();
    doc.root.children.push_back(std::move(marker));
    size_t pos = nextLine(source, 0);
    if (pos == first.size()) {
        return doc;  // (no line break after the marker: no text)
    }
    for (;;) {
        const std::string_view line = lineAt(source, pos);
        Block b;
        b.kind = BlockKind::Paragraph;
        b.runs.push_back(Run{std::string(line), 0, -1, pos, line.size()});
        b.textBegin = pos;
        b.textEnd = pos + line.size();
        doc.root.children.push_back(std::move(b));
        if (pos + line.size() >= source.size()) {
            break;  // (the last line: after the last line break, maybe empty)
        }
        pos = nextLine(source, pos);
    }
    return doc;
}
}  // namespace

Document parse(std::string_view source) {
    if (isPlain(source)) {
        return parsePlain(source);
    }
    // \(…\) and \[…\] (as chat apps write formulas) as $…$ and $$…$$; the places md4c reports are mapped back
    const tex::Rewritten rewritten = tex::rewrite(source);
    const std::string_view parsed = rewritten.changed ? std::string_view(rewritten.text) : source;
    Builder builder(parsed, source, rewritten);
    MD_PARSER parser{};
    parser.abi_version = 0;
    // GitHub: tables, ~~strike~~, task lists, bare web addresses; [[wiki links]] as in Obsidian / Zettlr; formulas
    // $…$ and $$…$$ as in Obsidian, Zettlr and GitHub (MdMath)
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = [](MD_BLOCKTYPE t, void* d, void* u) { return static_cast<Builder*>(u)->enterBlock(t, d); };
    parser.leave_block = [](MD_BLOCKTYPE t, void*, void* u) { return static_cast<Builder*>(u)->leaveBlock(t); };
    parser.enter_span = [](MD_SPANTYPE t, void* d, void* u) { return static_cast<Builder*>(u)->enterSpan(t, d); };
    parser.leave_span = [](MD_SPANTYPE t, void*, void* u) { return static_cast<Builder*>(u)->leaveSpan(t); };
    parser.text = [](MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* u) {
        return static_cast<Builder*>(u)->text(t, s, n);
    };
    md_parse(parsed.data(), static_cast<MD_SIZE>(parsed.size()), &parser, &builder);
    return builder.take();
}

std::vector<BlockSpan> topLevelSpans(std::string_view src, const Document& doc) {
    using namespace text;
    std::vector<BlockSpan> spans;
    size_t prevEnd = 0;
    for (const Block& b: doc.root.children) {
        size_t first = 0;
        if (b.textBegin != NO_SOURCE && b.textBegin >= prevEnd) {
            first = lineStart(src, b.textBegin);
            if (b.kind == BlockKind::CodeBlock && b.fenced) {
                // The fence before the code: the code may start with blank lines (its text begins after them)
                size_t fence = first;
                while (fence > prevEnd) {
                    fence = lineStart(src, fence - 1);
                    if (!blank(lineAt(src, fence))) {
                        break;
                    }
                }
                if (fence < first && !fenceOf(lineAt(src, fence)).empty()) {
                    first = fence;
                }
            }
        } else {
            first = prevEnd;  // (no text, e.g. a rule: its first line that is not blank)
            while (first < src.size() && blank(lineAt(src, first))) {
                first = nextLine(src, first);
            }
        }
        first = std::max(first, prevEnd);
        if (b.kind == BlockKind::Paragraph && !b.runs.empty() && (b.runs.front().flags & Math)) {
            // A formula's "$$" on a line of its own before it: the paragraph begins there
            size_t l = prevEnd;
            while (l < first && blank(lineAt(src, l))) {
                l = nextLine(src, l);
            }
            first = std::min(first, l);
        }
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
            // (and the "$$" or "\]" after a formula at its end, on a line of its own)
            if (b.kind == BlockKind::Paragraph && !b.runs.empty() && (b.runs.back().flags & Math) && end < src.size()) {
                const std::string_view after = afterIndent(lineAt(src, end));
                if (after.substr(0, 1) == "$" || after.substr(0, 2) == "\\]") {
                    end = nextLine(src, end);
                }
            }
        } else {
            end = nextLine(src, first);
        }
        spans.push_back({first, std::max(end, first)});
        prevEnd = spans.back().end;
    }
    return spans;
}

std::string plainText(const Block& block) {
    std::string out;
    for (const Run& r: block.runs) {
        out += r.text;
    }
    return out;
}

}  // namespace xqt::md
