#include "MdDocument.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "md4c.h"

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
    explicit Builder(std::string_view source): source(source) { stack.push_back(&doc.root); }

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
                flag = Math;
                break;
            case MD_SPAN_A:
                flag = Link;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_A_DETAIL*>(detail)->href));
                break;
            case MD_SPAN_IMG:
                flag = Image;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src));
                break;
            case MD_SPAN_WIKILINK:
                flag = Link;
                links.push_back(static_cast<int>(doc.links.size()));
                doc.links.push_back(attributeText(static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail)->target));
                break;
        }
        spans.push_back(flag);
        return 0;
    }

    int leaveSpan(MD_SPANTYPE type) {
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
        if (text >= source.data() && text + size <= source.data() + source.size()) {
            r.source = static_cast<size_t>(text - source.data());
            r.sourceLength = size;
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
                r.text = std::string(s);
        }
        textTarget().runs.push_back(std::move(r));
        return 0;
    }

private:
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
    Document doc;
    std::vector<Block*> stack;
    std::vector<uint16_t> spans;
    std::vector<int> links;
    size_t blockEvents = 0;
    size_t implicitAt = static_cast<size_t>(-1);
    const Block* implicitIn = nullptr;
};

}  // namespace

Document parse(std::string_view source) {
    Builder builder(source);
    MD_PARSER parser{};
    parser.abi_version = 0;
    // GitHub: tables, ~~strike~~, task lists, bare web addresses; [[wiki links]] as in Obsidian / Zettlr
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS;
    parser.enter_block = [](MD_BLOCKTYPE t, void* d, void* u) { return static_cast<Builder*>(u)->enterBlock(t, d); };
    parser.leave_block = [](MD_BLOCKTYPE t, void*, void* u) { return static_cast<Builder*>(u)->leaveBlock(t); };
    parser.enter_span = [](MD_SPANTYPE t, void* d, void* u) { return static_cast<Builder*>(u)->enterSpan(t, d); };
    parser.leave_span = [](MD_SPANTYPE t, void*, void* u) { return static_cast<Builder*>(u)->leaveSpan(t); };
    parser.text = [](MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* u) {
        return static_cast<Builder*>(u)->text(t, s, n);
    };
    md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &builder);
    return builder.take();
}

std::string plainText(const Block& block) {
    std::string out;
    for (const Run& r: block.runs) {
        out += r.text;
    }
    return out;
}

}  // namespace xqt::md
