#include "MdLayout.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include <pango/pangocairo.h>

#include "util/StringUtils.h"

#include "MdHighlight.h"
#include "MdText.h"

namespace xqt::md {

namespace {

constexpr Color LINK_COLOR(0x1a, 0x5f, 0xd8);  // (as the links in upstream's TextView)
constexpr Color CODE_BACKGROUND(0xf3, 0xf4, 0xf6);
constexpr Color INLINE_CODE_BACKGROUND(0xeb, 0xed, 0xf0);
constexpr Color RULE_COLOR(0xd0, 0xd7, 0xde);
constexpr Color MUTED(0x57, 0x60, 0x6a);
constexpr Color TABLE_HEADER_BACKGROUND(0xf6, 0xf8, 0xfa);
constexpr Color MARKER(0x9a, 0xa0, 0xa6);

constexpr double LINE_SPACING = 1.25;
constexpr double CODE_LINE_SPACING = 1.15;

guint16 u16(uint8_t c) { return static_cast<guint16>(c * 257); }

/// The Pango context of this thread. The font options make the layout independent of the zoom it is drawn at.
PangoContext* context() {
    static thread_local xoj::util::GObjectSPtr<PangoContext> ctx;
    if (!ctx) {
        ctx.reset(pango_font_map_create_context(pango_cairo_font_map_get_default()), xoj::util::adopt);
        cairo_font_options_t* options = cairo_font_options_create();
        cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
        cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
        pango_cairo_context_set_font_options(ctx.get(), options);
        cairo_font_options_destroy(options);
        pango_context_set_round_glyph_positions(ctx.get(), false);
    }
    return ctx.get();
}

void insert(PangoAttrList* list, PangoAttribute* a, size_t from, size_t to) {
    a->start_index = static_cast<guint>(from);
    a->end_index = static_cast<guint>(to);
    pango_attr_list_insert(list, a);
}

struct TextOptions {
    double size = 12;
    bool bold = false;
    bool mono = false;
    double width = -1;  ///< wrap width (points); -1: no wrapping
    double lineSpacing = LINE_SPACING;
    PangoAlignment align = PANGO_ALIGN_LEFT;
};

struct Ctx {
    Color color;
    bool tight = false;  ///< in a tight list: no space between paragraphs
    int depth = 0;       ///< list nesting
};

double pangoWidth(PangoLayout* l) {
    PangoRectangle logical;
    pango_layout_get_extents(l, nullptr, &logical);
    return logical.width / static_cast<double>(PANGO_SCALE);
}
double pangoHeight(PangoLayout* l) {
    PangoRectangle logical;
    pango_layout_get_extents(l, nullptr, &logical);
    return logical.height / static_cast<double>(PANGO_SCALE);
}
double baseline(PangoLayout* l) { return pango_layout_get_baseline(l) / static_cast<double>(PANGO_SCALE); }

/// A laid out text and where its links are.
struct Laid {
    xoj::util::GObjectSPtr<PangoLayout> layout;
    std::vector<LinkSpan> links;
    std::vector<SourceMap> sources;
    PangoLayout* get() const { return layout.get(); }
};

class Layouter {
public:
    Layouter(const Style& style, std::string_view source, size_t active): st(style), source(source), active(active) {}

    Layout run(const Document& doc) {
        out.links = doc.links;
        const auto& blocks = doc.root.children;
        out.blocks.resize(blocks.size());
        continuedListStart = continuationStart(doc);
        Ctx c;
        c.color = st.color;
        // Editing: the block with the cursor (or the blank lines after it) is laid out as its source
        size_t rawBlockIndex = blocks.size();
        size_t rawEnd = source.size();
        if (active != NO_SOURCE) {
            const auto spans = topLevelSpans(source, doc);
            for (size_t i = 0; i < spans.size(); ++i) {
                if (spans[i].begin <= active || i == 0) {
                    rawBlockIndex = i;
                    rawEnd = i + 1 < spans.size() ? spans[i + 1].begin : source.size();
                }
            }
            if (blocks.empty()) {
                rawBlock(nullptr, 0, source.size(), c);  // (an empty text: a line for the cursor)
            } else {
                rawSpan = spans[rawBlockIndex];
                if (rawBlockIndex == 0) {
                    rawSpan.begin = 0;  // (blank lines before the first block are its)
                }
            }
        }
        for (size_t i = 0; i < blocks.size(); ++i) {
            top = i;
            current = {};
            const size_t first = out.items.size();
            if (i == rawBlockIndex) {
                rawBlock(&blocks[i], rawSpan.begin, rawEnd, c);
            } else {
                block(blocks[i], 0, st.width, c);
            }
            Layout::Extent e{y, y};
            if (out.items.size() > first) {
                e = {out.items[first].y, out.items[first].y};
                for (size_t k = first; k < out.items.size(); ++k) {
                    const Item& it = out.items[k];
                    e.top = std::min({e.top, it.y, it.y + (it.kind == Item::Kind::Line ? it.height : 0)});
                    e.bottom = std::max(e.bottom, it.y + std::max(it.height, 0.0));
                }
            }
            e.item = current.item;
            e.parts = std::move(current.parts);
            out.blocks[i] = std::move(e);
        }
        out.height = y;
        return std::move(out);
    }

private:
    // --- vertical spacing: the space between two blocks is the larger of their margins (as in CSS) -------------
    void margin(double m) { pending = std::max(pending, m); }
    /// Content goes at y now: after the pending space, unless it is the first thing (of the box or a container).
    void open() {
        if (!atTop) {
            y += pending;
        }
        pending = 0;
        atTop = false;
    }

    Laid text(const std::vector<Run>& runs, const TextOptions& o, const std::vector<CodeSpan>& code = {}) {
        xoj::util::GObjectSPtr<PangoLayout> l(pango_layout_new(context()), xoj::util::adopt);
        PangoFontDescription* d = pango_font_description_new();
        pango_font_description_set_family(d, (o.mono ? st.monoFamily : st.family).c_str());
        pango_font_description_set_absolute_size(d, o.size * PANGO_SCALE);
        if (o.bold) {
            pango_font_description_set_weight(d, PANGO_WEIGHT_BOLD);
        }
        pango_layout_set_font_description(l.get(), d);
        pango_font_description_free(d);
        pango_layout_set_width(l.get(), o.width > 0 ? static_cast<int>(o.width * PANGO_SCALE) : -1);
        pango_layout_set_wrap(l.get(), PANGO_WRAP_WORD_CHAR);
        pango_layout_set_line_spacing(l.get(), static_cast<float>(o.lineSpacing));
        pango_layout_set_alignment(l.get(), o.align);

        std::string s;
        std::vector<LinkSpan> links;
        std::vector<SourceMap> sources;
        PangoAttrList* attrs = pango_attr_list_new();
        for (const Run& r: runs) {
            const size_t from = s.size();
            s += r.text;
            const size_t to = s.size();
            sources.push_back({static_cast<int>(from), static_cast<int>(to - from), r.source, r.sourceLength, r.flags});
            if ((r.flags & Link) && r.link >= 0) {
                if (!links.empty() && links.back().link == r.link && links.back().end == static_cast<int>(from)) {
                    links.back().end = static_cast<int>(to);  // (a link with formatting inside: several runs)
                } else {
                    links.push_back({static_cast<int>(from), static_cast<int>(to), r.link});
                }
            }
            if (r.flags & Strong) {
                insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD), from, to);
            }
            if (r.flags & Emphasis) {
                insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC), from, to);
            }
            if (r.flags & Strike) {
                insert(attrs, pango_attr_strikethrough_new(true), from, to);
            }
            if (r.flags & (Underline | Link)) {
                insert(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), from, to);
            }
            if (r.flags & Link) {
                insert(attrs,
                       pango_attr_foreground_new(u16(LINK_COLOR.red), u16(LINK_COLOR.green), u16(LINK_COLOR.blue)),
                       from, to);
            }
            if (r.flags & Code) {
                insert(attrs, pango_attr_family_new(st.monoFamily.c_str()), from, to);
                insert(attrs, pango_attr_size_new_absolute(static_cast<int>(o.size * 0.9 * PANGO_SCALE)), from, to);
                insert(attrs,
                       pango_attr_background_new(u16(INLINE_CODE_BACKGROUND.red), u16(INLINE_CODE_BACKGROUND.green),
                                                 u16(INLINE_CODE_BACKGROUND.blue)),
                       from, to);
            }
            if (r.flags & (Image | Html)) {
                insert(attrs, pango_attr_foreground_new(u16(MUTED.red), u16(MUTED.green), u16(MUTED.blue)), from, to);
            }
            if (r.flags & Image) {
                insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC), from, to);
            }
            if (r.flags & Math) {
                insert(attrs, pango_attr_family_new("Serif"), from, to);
                insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC), from, to);
            }
            if (r.flags & Marker) {
                insert(attrs, pango_attr_foreground_new(u16(MARKER.red), u16(MARKER.green), u16(MARKER.blue)), from,
                       to);
            }
        }
        for (const CodeSpan& c: code) {  // syntax highlighting
            const auto from = static_cast<size_t>(c.start);
            const auto to = static_cast<size_t>(c.start + c.length);
            insert(attrs, pango_attr_foreground_new(u16(c.color.red), u16(c.color.green), u16(c.color.blue)), from, to);
            if (c.bold) {
                insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD), from, to);
            }
            if (c.italic) {
                insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC), from, to);
            }
        }
        pango_layout_set_text(l.get(), s.c_str(), static_cast<int>(s.size()));
        pango_layout_set_attributes(l.get(), attrs);
        pango_attr_list_unref(attrs);
        return {std::move(l), std::move(links), std::move(sources)};
    }

    /// Returns its index.
    size_t addText(Laid l, double x, double atY, Color color) {
        Item it;
        it.kind = Item::Kind::Text;
        it.x = x;
        it.y = atY;
        it.width = pangoWidth(l.get());
        it.height = pangoHeight(l.get());
        it.layout = std::move(l.layout);
        it.links = std::move(l.links);
        it.sources = std::move(l.sources);
        it.color = color;
        it.block = top;
        out.items.push_back(std::move(it));
        return out.items.size() - 1;
    }
    void addFill(double x, double atY, double w, double h, Color color) {
        Item it;
        it.kind = Item::Kind::Fill;
        it.x = x;
        it.y = atY;
        it.width = w;
        it.height = h;
        it.color = color;
        it.block = top;
        out.items.push_back(std::move(it));
    }
    void addLine(double x, double atY, double dx, double dy, Color color, double lineWidth) {
        Item it;
        it.kind = Item::Kind::Line;
        it.x = x;
        it.y = atY;
        it.width = dx;
        it.height = dy;
        it.color = color;
        it.lineWidth = lineWidth;
        it.block = top;
        out.items.push_back(std::move(it));
    }

    // --- blocks -------------------------------------------------------------------------------------------------
    void block(const Block& b, double x, double w, const Ctx& c) {
        switch (b.kind) {
            case BlockKind::Paragraph:
                paragraph(b, x, w, c);
                break;
            case BlockKind::Heading:
                heading(b, x, w, c);
                break;
            case BlockKind::CodeBlock:
                code(b, x, w);
                break;
            case BlockKind::Quote:
                quote(b, x, w, c);
                break;
            case BlockKind::BulletList:
            case BlockKind::OrderedList:
                list(b, x, w, c);
                break;
            case BlockKind::Rule:
                margin(st.size);
                open();
                addLine(x, y + 0.5, w, 0, RULE_COLOR, 1);
                y += 1;
                margin(st.size);
                break;
            case BlockKind::Html:
                html(b, x, w);
                break;
            case BlockKind::Table:
                table(b, x, w, c);
                break;
            default:
                for (const Block& child: b.children) {
                    block(child, x, w, c);
                }
        }
    }

    void paragraph(const Block& b, double x, double w, const Ctx& c) {
        margin(c.tight ? 0 : 0.75 * st.size);
        open();
        auto l = text(b.runs, {st.size, false, false, w});
        const double h = pangoHeight(l.get());
        mainItem(addText(std::move(l), x, y, c.color));
        y += h;
        margin(c.tight ? 0.15 * st.size : 0.75 * st.size);
    }

    void heading(const Block& b, double x, double w, const Ctx& c) {
        const double s = st.size * headingScale(b.level);
        margin(s * (b.level <= 2 ? 1.0 : 0.8));
        open();
        auto l = text(b.runs, {s, true, false, w, 1.15});
        const double h = pangoHeight(l.get());
        mainItem(addText(std::move(l), x, y, c.color));
        y += h;
        if (b.level <= 2) {
            y += 0.25 * s;
            addLine(x, y, w, 0, RULE_COLOR, 0.75);
            y += 0.75;
        }
        margin(s * 0.45);
    }

    void code(const Block& b, double x, double w) {
        margin(0.75 * st.size);
        open();
        std::vector<Run> runs;
        for (const Run& r: b.runs) {
            Run plain = r;
            plain.flags = 0;
            runs.push_back(std::move(plain));
        }
        while (!runs.empty() && !runs.back().text.empty() && runs.back().text.back() == '\n') {
            runs.back().text.pop_back();
            if (runs.back().text.empty()) {
                runs.pop_back();
            }
        }
        const double pad = 0.6 * st.size;
        std::string source;
        for (const Run& r: runs) {
            source += r.text;
        }
        auto l = text(runs, {st.size * 0.88, false, true, w - 2 * pad, CODE_LINE_SPACING},
                      highlight(source, b.language.empty() ? b.info : b.language));
        const double h = pangoHeight(l.get());
        addFill(x, y, w, h + 2 * pad, CODE_BACKGROUND);
        mainItem(addText(std::move(l), x + pad, y + pad, st.color));
        y += h + 2 * pad;
        margin(0.75 * st.size);
    }

    void quote(const Block& b, double x, double w, const Ctx& c) {
        margin(0.75 * st.size);
        open();
        atTop = true;
        const double start = y;
        Ctx inner = c;
        inner.color = MUTED;
        inner.tight = false;
        const double indent = 1.1 * st.size;
        ++nesting;
        for (const Block& child: b.children) {
            block(child, x + indent, w - indent, inner);
        }
        --nesting;
        atTop = false;
        addLine(x + 0.3 * st.size, start, 0, y - start, RULE_COLOR, 2.5);
        margin(0.75 * st.size);
    }

    /// A check box (vector, not a font's glyph: some fonts have colored emoji for ☐ ☑).
    void checkbox(double x, double atY, double size, bool checked) {
        if (checked) {
            addFill(x, atY, size, size, LINK_COLOR);
            const Color white(0xff, 0xff, 0xff);
            addLine(x + 0.2 * size, atY + 0.52 * size, 0.22 * size, 0.22 * size, white, 0.13 * size);
            addLine(x + 0.42 * size, atY + 0.74 * size, 0.38 * size, -0.46 * size, white, 0.13 * size);
        } else {
            const double w = 0.08 * size;
            addLine(x, atY, size, 0, MUTED, w);
            addLine(x + size, atY, 0, size, MUTED, w);
            addLine(x + size, atY + size, -size, 0, MUTED, w);
            addLine(x, atY + size, 0, -size, MUTED, w);
        }
    }

    std::string bullet(int depth) const {
        static const char* bullets[] = {"\xe2\x80\xa2", "\xe2\x97\xa6", "\xe2\x96\xaa"};  // • ◦ ▪
        return bullets[depth % 3];
    }

    void list(const Block& b, double x, double w, const Ctx& c) {
        const bool ordered = b.kind == BlockKind::OrderedList;
        margin(c.tight ? 0.15 * st.size : 0.75 * st.size);
        open();
        atTop = true;
        Ctx inner = c;
        inner.tight = b.tight;
        inner.depth = c.depth + 1;

        double gutter = 1.4 * st.size;
        if (ordered) {
            const std::string widest = std::to_string(b.start + b.children.size()) + b.mark;
            auto l = text({Run{widest}}, {st.size});
            gutter = std::max(gutter, pangoWidth(l.get()) + 0.5 * st.size);
        }
        unsigned n = ordered && nesting == 0 && continuedListStart > 0 && top == 1 ? continuedListStart : b.start;
        const bool parts = nesting == 0;
        ++nesting;
        for (const Block& item: b.children) {
            const size_t first = out.items.size();
            const double itemTop = y;
            for (const Block& child: item.children) {
                block(child, x + gutter, w - gutter, inner);
            }
            if (item.children.empty()) {  // "-" alone: an empty item
                margin(b.tight ? 0.15 * st.size : 0.75 * st.size);
                open();
                y += st.size * LINE_SPACING;
            }
            // On the baseline of the item's first line
            const Item* firstText = nullptr;
            for (size_t k = first; k < out.items.size(); ++k) {
                if (out.items[k].kind == Item::Kind::Text) {
                    firstText = &out.items[k];
                    break;
                }
            }
            const double base = firstText ? firstText->y + baseline(firstText->layout.get()) : y - 0.25 * st.size;
            const double right = x + gutter - 0.45 * st.size;  // markers end here
            if (item.task) {
                const double size = 0.8 * st.size;
                checkbox(right - size, base + 0.1 * st.size - size, size, item.checked);
                out.checkBoxes.push_back({right - size, base + 0.1 * st.size - size, size, item.taskMark, item.checked});
            } else {
                const std::string marker = ordered ? std::to_string(n) + b.mark : bullet(c.depth);
                auto l = text({Run{marker}}, {st.size});
                const double mw = pangoWidth(l.get());
                const double my = base - baseline(l.get());
                addText(std::move(l), right - mw, my, c.color);
            }
            ++n;
            if (parts) {
                Layout::Extent e{y, itemTop};
                for (size_t k = first; k < out.items.size(); ++k) {
                    e.top = std::min(e.top, out.items[k].y);
                    e.bottom = std::max(e.bottom, out.items[k].y + std::max(out.items[k].height, 0.0));
                }
                current.parts.push_back(e);
            }
        }
        --nesting;
        atTop = false;
        margin(c.tight ? 0.15 * st.size : 0.75 * st.size);
    }

    void html(const Block& b, double x, double w) {
        std::string s = plainText(b);
        const auto first = s.find_first_not_of(" \t\r\n");
        const auto last = s.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return;
        }
        s = s.substr(first, last - first + 1);
        if (s.rfind("<!--", 0) == 0 && s.size() >= 7 && s.compare(s.size() - 3, 3, "-->") == 0) {
            return;  // a comment: not shown (also the page markers of the boxes)
        }
        margin(0.75 * st.size);
        open();
        Run r{s};
        r.flags = Html;
        auto l = text({r}, {st.size * 0.88, false, true, w, CODE_LINE_SPACING});
        const double h = pangoHeight(l.get());
        addText(std::move(l), x, y, st.color);
        y += h;
        margin(0.75 * st.size);
    }

    void table(const Block& b, double x, double w, const Ctx& c) {
        struct Row {
            const Block* block;
            bool header;
        };
        std::vector<Row> rows;
        for (const Block& part: b.children) {
            for (const Block& row: part.children) {
                rows.push_back({&row, part.kind == BlockKind::TableHead});
            }
        }
        size_t columns = 0;
        for (const Row& r: rows) {
            columns = std::max(columns, r.block->children.size());
        }
        if (columns == 0) {
            return;
        }
        margin(0.75 * st.size);
        open();
        const double pad = 0.4 * st.size;
        // Column widths: as wide as their widest cell, narrowed to the box together if needed
        std::vector<double> widths(columns, 2 * st.size);
        for (const Row& r: rows) {
            for (size_t i = 0; i < r.block->children.size(); ++i) {
                auto l = text(r.block->children[i].runs, {st.size, r.header});
                widths[i] = std::max(widths[i], pangoWidth(l.get()) + 2 * pad + 1);
            }
        }
        double total = 0;
        for (double cw: widths) {
            total += cw;
        }
        if (total > w) {
            for (double& cw: widths) {
                cw *= w / total;
            }
            total = w;
        }
        const double start = y;
        for (const Row& r: rows) {
            std::vector<Laid> cells;
            double h = 0;
            for (size_t i = 0; i < columns; ++i) {
                static const std::vector<Run> none;
                const Block* cell = i < r.block->children.size() ? &r.block->children[i] : nullptr;
                PangoAlignment align = PANGO_ALIGN_LEFT;
                if (cell && cell->align == Align::Center) {
                    align = PANGO_ALIGN_CENTER;
                } else if (cell && cell->align == Align::Right) {
                    align = PANGO_ALIGN_RIGHT;
                }
                cells.push_back(text(cell ? cell->runs : none, {st.size, r.header, false, widths[i] - 2 * pad,
                                                                 LINE_SPACING, align}));
                h = std::max(h, pangoHeight(cells.back().get()));
            }
            if (r.header) {
                addFill(x, y, total, h + 2 * pad, TABLE_HEADER_BACKGROUND);
            }
            double cx = x;
            for (size_t i = 0; i < columns; ++i) {
                addText(std::move(cells[i]), cx + pad, y + pad, c.color);
                cx += widths[i];
            }
            addLine(x, y, total, 0, RULE_COLOR, 0.75);
            if (nesting == 0) {
                current.parts.push_back({y, y + h + 2 * pad});
            }
            y += h + 2 * pad;
        }
        addLine(x, y, total, 0, RULE_COLOR, 0.75);
        double cx = x;
        for (size_t i = 0; i <= columns; ++i) {
            addLine(cx, start, 0, y - start, RULE_COLOR, 0.75);
            if (i < columns) {
                cx += widths[i];
            }
        }
        margin(0.75 * st.size);
    }

    void mainItem(size_t index) {
        if (nesting == 0) {
            current.item = static_cast<int>(index);
        }
    }

    /// A page's slice that continues a numbered list: "<!-- xqt:cont list start=4 -->" (see MdPaginate).
    static unsigned continuationStart(const Document& doc) {
        if (doc.root.children.empty() || doc.root.children[0].kind != BlockKind::Html) {
            return 0;
        }
        const std::string s = plainText(doc.root.children[0]);
        const auto at = s.find("start=");
        if (s.rfind("<!-- xqt:cont", 0) != 0 || at == std::string::npos) {
            return 0;
        }
        return static_cast<unsigned>(std::strtoul(s.c_str() + at + 6, nullptr, 10));
    }

    /// The block with the cursor, as its source: the text of its runs keeps its formatting, the marks between them
    /// are dimmed. Its item's text is exactly source[begin, end) (without a last line break, unless the cursor is
    /// after it), so a place in it is a place in the source.
    void rawBlock(const Block* b, size_t begin, size_t end, const Ctx& c) {
        using namespace text;
        std::string raw(source.substr(begin, end - begin));
        if (!raw.empty() && raw.back() == '\n' && active < end) {
            raw.pop_back();
        }
        const size_t rawEnd = begin + raw.size();
        std::vector<const Run*> content;
        if (b) {
            collectRuns(*b, begin, rawEnd, content);
        }
        std::sort(content.begin(), content.end(), [](const Run* a, const Run* z) { return a->source < z->source; });
        std::vector<Run> runs;
        size_t pos = begin;
        const auto marks = [&](size_t to) {
            if (to > pos) {
                Run m;
                m.text = raw.substr(pos - begin, to - pos);
                m.flags = Marker;
                m.source = pos;
                m.sourceLength = to - pos;
                runs.push_back(std::move(m));
                pos = to;
            }
        };
        for (const Run* r: content) {
            if (r->source < pos) {
                continue;
            }
            marks(r->source);
            runs.push_back(*r);
            pos = r->source + r->sourceLength;
        }
        marks(rawEnd);
        if (runs.empty()) {
            Run empty;
            empty.source = begin;
            runs.push_back(empty);
        }

        const BlockKind kind = b ? b->kind : BlockKind::Paragraph;
        TextOptions o{st.size, false, false, st.width};
        std::vector<CodeSpan> code;
        double pad = 0;
        if (kind == BlockKind::Heading) {
            o = {st.size * headingScale(b->level), true, false, st.width, 1.15};
        } else if (kind == BlockKind::CodeBlock || kind == BlockKind::Table || kind == BlockKind::Html) {
            pad = kind == BlockKind::CodeBlock ? 0.6 * st.size : 0;
            o = {st.size * 0.88, false, true, st.width - 2 * pad, CODE_LINE_SPACING};
            if (kind == BlockKind::CodeBlock && b->fenced) {
                // Highlighted as when drawn: the lines between the fences
                const size_t from = nextLine(source, begin);
                const std::string fence = fenceOf(lineAt(source, begin));
                size_t to = from;
                while (to < rawEnd && !closesFence(lineAt(source, to), fence)) {
                    to = nextLine(source, to);
                }
                to = std::min(to, rawEnd);
                if (from < to) {
                    for (CodeSpan s: highlight(std::string(source.substr(from, to - from)),
                                               b->language.empty() ? b->info : b->language)) {
                        s.start += static_cast<int>(from - begin);
                        code.push_back(s);
                    }
                }
            }
        }
        margin(kind == BlockKind::Heading ? o.size * 0.8 : 0.75 * st.size);
        open();
        auto l = text(runs, o, code);
        const double h = pangoHeight(l.get());
        size_t index = 0;
        if (pad > 0) {
            addFill(0, y, st.width, h + 2 * pad, CODE_BACKGROUND);
            index = addText(std::move(l), pad, y + pad, c.color);
        } else {
            index = addText(std::move(l), 0, y, c.color);
        }
        y += h + 2 * pad;
        mainItem(index);
        out.rawItem = static_cast<int>(index);
        out.rawBegin = begin;
        out.rawEnd = rawEnd;
        margin(kind == BlockKind::Heading ? o.size * 0.45 : 0.75 * st.size);
    }

    static void collectRuns(const Block& b, size_t from, size_t to, std::vector<const Run*>& out) {
        for (const Run& r: b.runs) {
            // (only text that is the source as it is: not made-up breaks, not entities)
            if (r.source != NO_SOURCE && r.sourceLength == r.text.size() && r.source >= from &&
                r.source + r.sourceLength <= to && r.sourceLength > 0) {
                out.push_back(&r);
            }
        }
        for (const Block& child: b.children) {
            collectRuns(child, from, to, out);
        }
    }

    const Style& st;
    std::string_view source;
    size_t active;
    BlockSpan rawSpan;
    Layout out;
    Layout::Extent current;  ///< item and parts of the top-level block being laid out
    int nesting = 0;         ///< in a list or quote
    unsigned continuedListStart = 0;
    size_t top = 0;
    double y = 0;
    double pending = 0;
    bool atTop = true;
};

}  // namespace

double headingScale(int level) {
    static const double scales[] = {2.0, 1.5, 1.25, 1.1, 1.0, 0.9};
    return scales[std::clamp(level, 1, 6) - 1];
}

Layout layout(const Document& doc, const Style& style, std::string_view source, size_t active) {
    return Layouter(style, source, active).run(doc);
}

std::optional<LinkHit> linkAt(const Layout& layout, double x, double y) {
    for (const Item& it: layout.items) {
        if (it.kind != Item::Kind::Text || it.links.empty() || x < it.x || y < it.y || x > it.x + it.width ||
            y > it.y + it.height) {
            continue;
        }
        int index = 0;
        int trailing = 0;
        if (!pango_layout_xy_to_index(it.layout.get(), static_cast<int>((x - it.x) * PANGO_SCALE),
                                      static_cast<int>((y - it.y) * PANGO_SCALE), &index, &trailing)) {
            continue;  // (beside the text of the line)
        }
        for (const LinkSpan& span: it.links) {
            if (index >= span.start && index < span.end && span.link < static_cast<int>(layout.links.size())) {
                PangoRectangle a;
                PangoRectangle b;
                pango_layout_index_to_pos(it.layout.get(), span.start, &a);
                pango_layout_index_to_pos(it.layout.get(), std::max(span.start, span.end - 1), &b);
                if (a.y != b.y) {  // (over several lines: the part on the tapped line)
                    pango_layout_index_to_pos(it.layout.get(), index, &a);
                    b = a;
                }
                LinkHit hit;
                hit.target = layout.links[static_cast<size_t>(span.link)];
                hit.x = it.x + a.x / static_cast<double>(PANGO_SCALE);
                hit.y = it.y + a.y / static_cast<double>(PANGO_SCALE);
                hit.width = (b.x + b.width - a.x) / static_cast<double>(PANGO_SCALE);
                hit.height = a.height / static_cast<double>(PANGO_SCALE);
                return hit;
            }
        }
    }
    return std::nullopt;
}

std::optional<Layout::CheckBox> checkBoxAt(const Layout& layout, double x, double y) {
    for (const Layout::CheckBox& box: layout.checkBoxes) {
        const double around = box.size * 0.4;  // (a finger is not a pin)
        if (box.mark != NO_SOURCE && x >= box.x - around && x <= box.x + box.size + around && y >= box.y - around &&
            y <= box.y + box.size + around) {
            return box;
        }
    }
    return std::nullopt;
}

std::string toggledTask(const std::string& source, size_t mark) {
    std::string out = source;
    if (mark < out.size()) {
        out[mark] = out[mark] == ' ' ? 'x' : ' ';
    }
    return out;
}

std::vector<Rect> findText(const Layout& layout, const std::string& search) {
    std::vector<Rect> found;
    if (search.empty()) {
        return found;
    }
    const std::string pattern = StringUtils::toLowerCase(search);
    for (const Item& it: layout.items) {
        if (it.kind != Item::Kind::Text) {
            continue;
        }
        const std::string text = StringUtils::toLowerCase(pango_layout_get_text(it.layout.get()));
        for (size_t pos = text.find(pattern); pos != std::string::npos; pos = text.find(pattern, pos + 1)) {
            PangoRectangle a;
            PangoRectangle b;
            pango_layout_index_to_pos(it.layout.get(), static_cast<int>(pos), &a);
            pango_layout_index_to_pos(it.layout.get(), static_cast<int>(pos + pattern.size() - 1), &b);
            const double x1 = it.x + a.x / static_cast<double>(PANGO_SCALE);
            const double y1 = it.y + a.y / static_cast<double>(PANGO_SCALE);
            const double x2 = it.x + (b.x + b.width) / static_cast<double>(PANGO_SCALE);
            const double y2 = it.y + (b.y + b.height) / static_cast<double>(PANGO_SCALE);
            found.push_back({std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1), std::abs(y2 - y1)});
        }
    }
    return found;
}

void draw(cairo_t* cr, const Layout& layout) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (const Item& it: layout.items) {
        Util::cairo_set_source_rgbi(cr, it.color);
        switch (it.kind) {
            case Item::Kind::Fill:
                cairo_rectangle(cr, it.x, it.y, it.width, it.height);
                cairo_fill(cr);
                break;
            case Item::Kind::Line:
                cairo_set_line_width(cr, it.lineWidth);
                cairo_move_to(cr, it.x, it.y);
                cairo_line_to(cr, it.x + it.width, it.y + it.height);
                cairo_stroke(cr);
                break;
            case Item::Kind::Text:
                cairo_move_to(cr, it.x, it.y);
                pango_cairo_show_layout(cr, it.layout.get());
                break;
        }
    }
    cairo_restore(cr);
}

}  // namespace xqt::md
