#include "MdLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

#include <pango/pangocairo.h>

#include "util/StringUtils.h"

#include "EmojiFont.h"
#include "MdHighlight.h"
#include "MdMath.h"
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
constexpr Color MATH_ERROR(0xc6, 0x28, 0x28);  // (a formula that cannot be laid out: its source)

constexpr double LINE_SPACING = 1.25;
constexpr double CODE_LINE_SPACING = 1.15;
constexpr double PLAIN_LINE_SPACING = 1.2;

guint16 u16(uint8_t c) { return static_cast<guint16>(c * 257); }

/// The character a formula takes in the text (the Unicode object replacement character), drawn as the formula.
constexpr std::string_view FORMULA_CHAR = "\xef\xbf\xbc";
/// Space above and below a display formula (em of the text).
constexpr double DISPLAY_PAD = 0.3;
/// Formulas are drawn this much bigger than the text around: Latin Modern's letters are smaller than a sans text's of
/// the same size (its x-height is 0.43 em, DejaVu Sans' 0.55 em). KaTeX does the same (1.21).
constexpr double MATH_SCALE = 1.2;

/// What a formula's shape attribute carries: the formula, its size and where in its room it is drawn.
struct FormulaShape {
    std::shared_ptr<const math::Formula> formula;
    double size = 12;
    double dx = 0;
};
/// Pango draws a shape: the formula at the current point (its baseline's left end). `doPath`: the outlines only.
void drawFormulaShape(cairo_t* cr, PangoAttrShape* attr, gboolean doPath, gpointer) {
    const auto* shape = static_cast<const FormulaShape*>(attr->data);
    if (!shape || !shape->formula) {
        return;
    }
    double x = 0;
    double y = 0;
    cairo_get_current_point(cr, &x, &y);
    math::draw(cr, *shape->formula, x + shape->dx, y, shape->size, doPath);
}

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
        pango_cairo_context_set_shape_renderer(ctx.get(), drawFormulaShape, nullptr, nullptr);
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
    std::vector<MathSpan> maths;
    bool cached = false;  ///< taken from LayoutCache
    PangoLayout* get() const { return layout.get(); }
};

/// The Pango layouts made last on this thread, by their text, formatting and options (never changed once made:
/// items share them). Two generations: when the newer one is full, the older one goes (the layouts still used by
/// laid out texts stay alive with them).
class LayoutCache {
public:
    static constexpr size_t GENERATION = 1500;
    xoj::util::GObjectSPtr<PangoLayout> find(const std::string& key) {
        if (auto it = now.find(key); it != now.end()) {
            return it->second;
        }
        if (auto it = before.find(key); it != before.end()) {
            auto l = it->second;
            put(key, l);  // (still used: into the newer generation)
            return l;
        }
        return {};
    }
    void put(std::string key, xoj::util::GObjectSPtr<PangoLayout> l) {
        if (now.size() >= GENERATION) {
            before = std::move(now);
            now.clear();
        }
        now.emplace(std::move(key), std::move(l));
    }

private:
    std::unordered_map<std::string, xoj::util::GObjectSPtr<PangoLayout>> now, before;
};
LayoutCache& layoutCache() {
    static thread_local LayoutCache cache;
    return cache;
}

class Layouter {
public:
    Layouter(const Style& style, std::string_view source, size_t active): st(style), source(source), active(active) {}

    Layout run(const Document& doc) {
        if (doc.plain) {
            return runPlain(doc);
        }
        out.links = doc.links;
        out.wikiLinks = doc.wikiLinks;
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
            if (i == rawBlockIndex && afterClosedCode(blocks[i])) {
                // The cursor on the lines after a closed code block: the code is done (drawn as code), the cursor's
                // line is where the next paragraph goes
                block(blocks[i], 0, st.width, c);
                rawBlock(nullptr, text::lineStart(source, active), rawEnd, c);
            } else if (i == rawBlockIndex) {
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
    /// A plain text (Document::plain): its lines one below the other, as they are (wrapped at the box's width), in
    /// the box's font. The line with the cursor is the raw item (its text is exactly its source).
    Layout runPlain(const Document& doc) {
        const auto& blocks = doc.root.children;
        out.blocks.resize(blocks.size());
        for (size_t i = 0; i < blocks.size(); ++i) {
            top = i;
            const Block& b = blocks[i];
            if (b.kind != BlockKind::Paragraph) {
                out.blocks[i] = {y, y};  // (the marker line: not shown)
                continue;
            }
            std::vector<Run> runs = b.runs;
            for (Run& r: runs) {
                r.flags = 0;
            }
            auto l = text(runs, {st.size, false, false, st.width, PLAIN_LINE_SPACING});
            const double h = pangoHeight(l.get());
            const size_t index = addText(std::move(l), 0, y, st.color);
            out.blocks[i] = {y, y + h, static_cast<int>(index), {}};
            const size_t next = i + 1 < blocks.size() ? blocks[i + 1].textBegin : NO_SOURCE;
            const bool withCursor = active != NO_SOURCE && active >= b.textBegin && active < next;
            if (withCursor) {
                out.rawItem = static_cast<int>(index);
                out.rawBegin = b.textBegin;
                out.rawEnd = b.textEnd;
            }
            // The empty line after the last line break takes no room unless the cursor is on it (a page that ends
            // with a line break is not a line longer)
            const bool emptyLast = i + 1 == blocks.size() && i > 1 && b.textBegin == b.textEnd;
            if (emptyLast && !withCursor) {
                out.items[index].height = 0;
                out.blocks[i] = {y, y, static_cast<int>(index), {}};
                continue;
            }
            y += h;
        }
        out.height = y;
        return std::move(out);
    }

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

    Laid text(const std::vector<Run>& given, const TextOptions& o, const std::vector<CodeSpan>& code = {}) {
        // A formula's source can come in several runs (a line break in it): one run each, drawn as one
        std::vector<Run> merged;
        if (!mathAsSource && std::any_of(given.begin(), given.end(), [](const Run& r) { return r.flags & Math; })) {
            merged = mergedFormulas(given);
        }
        const std::vector<Run>& runs = merged.empty() ? given : merged;
        // The Pango layout of the same text with the same formatting is taken again (LayoutCache): while typing,
        // only the block that changed is shaped anew (a long text on one continuous page)
        std::string key;
        key.reserve(64);
        const auto add = [&key](const void* p, size_t n) { key.append(static_cast<const char*>(p), n); };
        add(&o.size, sizeof o.size);
        add(&o.width, sizeof o.width);
        add(&o.lineSpacing, sizeof o.lineSpacing);
        const char flags[] = {static_cast<char>(o.bold), static_cast<char>(o.mono), static_cast<char>(o.align),
                              static_cast<char>(mathAsSource)};
        add(flags, sizeof flags);
        key += st.family;
        key += '\0';
        key += st.monoFamily;
        key += '\0';
        for (const Run& r: runs) {
            const uint32_t n = static_cast<uint32_t>(r.text.size());
            add(&n, sizeof n);
            add(&r.flags, sizeof r.flags);
            key += r.text;
        }
        for (const CodeSpan& c: code) {
            add(&c.start, sizeof c.start);
            add(&c.length, sizeof c.length);
            add(&c.color, sizeof c.color);
            const char b[] = {static_cast<char>(c.bold), static_cast<char>(c.italic)};
            add(b, sizeof b);
        }
        Laid laid = makeText(runs, o, code, layoutCache().find(key));
        if (!laid.cached) {
            layoutCache().put(std::move(key), laid.layout);
        }
        return laid;
    }

    /// Consecutive runs of a formula as one run: its whole source.
    static std::vector<Run> mergedFormulas(const std::vector<Run>& runs) {
        std::vector<Run> out;
        out.reserve(runs.size());
        for (const Run& r: runs) {
            const uint16_t kind = Math | DisplayMath;
            if (!out.empty() && (r.flags & Math) && (out.back().flags & kind) == (r.flags & kind) &&
                out.back().link == r.link) {
                Run& m = out.back();
                m.text += r.text;
                // (from its first to its last text in the source: the line breaks md4c gives are made up)
                if (r.source != NO_SOURCE && m.source == NO_SOURCE) {
                    m.source = r.source;
                    m.sourceLength = r.sourceLength;
                } else if (r.source != NO_SOURCE && r.source >= m.source) {
                    m.sourceLength = r.source + r.sourceLength - m.source;
                }
                continue;
            }
            out.push_back(r);
        }
        return out;
    }

    /// The layout of a text (`cached`: the same one made before; only the links, sources and formulas are made here).
    Laid makeText(const std::vector<Run>& runs, const TextOptions& o, const std::vector<CodeSpan>& code,
                  xoj::util::GObjectSPtr<PangoLayout> cached) {
        // The text, where each run is in it, its links and formulas
        std::string s;
        std::vector<LinkSpan> links;
        std::vector<SourceMap> sources;
        std::vector<MathSpan> maths;
        struct Shape {
            size_t from = 0;
            FormulaShape shape;
            double width = 0;
            double ascent = 0;
            double descent = 0;
        };
        std::vector<Shape> shapes;
        for (const Run& r: runs) {
            const size_t from = s.size();
            std::shared_ptr<const math::Formula> formula;
            if ((r.flags & Math) && !mathAsSource) {
                formula = math::formula(r.text, r.flags & DisplayMath);
            }
            s += formula && formula->ok ? std::string(FORMULA_CHAR) : r.text;
            const size_t to = s.size();
            sources.push_back({static_cast<int>(from), static_cast<int>(to - from), r.source, r.sourceLength, r.flags});
            if ((r.flags & Link) && r.link >= 0) {
                if (!links.empty() && links.back().link == r.link && links.back().end == static_cast<int>(from)) {
                    links.back().end = static_cast<int>(to);  // (a link with formatting inside: several runs)
                } else {
                    links.push_back({static_cast<int>(from), static_cast<int>(to), r.link});
                }
            }
            if (!formula) {
                continue;
            }
            MathSpan m;
            m.start = static_cast<int>(from);
            m.length = static_cast<int>(to - from);
            m.tex = r.text;
            m.display = r.flags & DisplayMath;
            if (!formula->ok) {
                m.error = formula->error;
                maths.push_back(std::move(m));
                continue;
            }
            // Its size: the text's, smaller if it is wider than the box. A display formula takes a whole line
            // (Pango breaks the lines around it) and is centered in it, with some room above and below.
            Shape sh;
            sh.from = from;
            sh.shape.formula = formula;
            sh.shape.size = o.size * MATH_SCALE;
            if (o.width > 0 && formula->width * sh.shape.size > o.width) {
                sh.shape.size = o.width / formula->width;
            }
            const double inkWidth = formula->width * sh.shape.size;
            const double pad = m.display ? DISPLAY_PAD * o.size : 0;
            sh.width = m.display && o.width > 0 ? o.width - 0.01 : inkWidth;
            sh.shape.dx = (sh.width - inkWidth) / 2;
            sh.ascent = formula->ascent * sh.shape.size + pad;
            sh.descent = formula->descent * sh.shape.size + pad;
            m.inkX = sh.shape.dx;
            m.inkWidth = inkWidth;
            maths.push_back(std::move(m));
            shapes.push_back(std::move(sh));
        }
        if (cached) {
            return {std::move(cached), std::move(links), std::move(sources), std::move(maths), true};
        }
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

        PangoAttrList* attrs = pango_attr_list_new();
        for (size_t k = 0; k < runs.size(); ++k) {
            const Run& r = runs[k];
            const auto from = static_cast<size_t>(sources[k].start);
            const auto to = from + static_cast<size_t>(sources[k].length);
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
            if ((r.flags & Math) && s.compare(from, to - from, FORMULA_CHAR) != 0) {
                // A formula's source: while it is written (the block with the cursor), or one that cannot be laid
                // out (in red)
                insert(attrs, pango_attr_family_new(st.monoFamily.c_str()), from, to);
                insert(attrs, pango_attr_size_new_absolute(static_cast<int>(o.size * 0.9 * PANGO_SCALE)), from, to);
                if (!mathAsSource) {
                    insert(attrs,
                           pango_attr_foreground_new(u16(MATH_ERROR.red), u16(MATH_ERROR.green), u16(MATH_ERROR.blue)),
                           from, to);
                }
            }
            if (r.flags & Marker) {
                insert(attrs, pango_attr_foreground_new(u16(MARKER.red), u16(MARKER.green), u16(MARKER.blue)), from,
                       to);
            }
        }
        for (const Shape& sh: shapes) {  // the formulas drawn
            constexpr double S = PANGO_SCALE;
            PangoRectangle logical{0, static_cast<int>(-sh.ascent * S), static_cast<int>(sh.width * S),
                                   static_cast<int>((sh.ascent + sh.descent) * S)};
            PangoAttribute* a = pango_attr_shape_new_with_data(
                    &logical, &logical, new FormulaShape(sh.shape),
                    [](gconstpointer p) -> gpointer { return new FormulaShape(*static_cast<const FormulaShape*>(p)); },
                    [](gpointer p) { delete static_cast<FormulaShape*>(p); });
            insert(attrs, a, sh.from, sh.from + FORMULA_CHAR.size());
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
        return {std::move(l), std::move(links), std::move(sources), std::move(maths)};
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
        it.maths = std::move(l.maths);
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
        mathAsSource = true;  // (its formulas as their source: they are being written)
        auto l = text(runs, o, code);
        mathAsSource = false;
        const double h = pangoHeight(l.get());
        size_t index = 0;
        if (pad > 0) {
            addFill(0, y, st.width, h + 2 * pad, CODE_BACKGROUND);
            index = addText(std::move(l), pad, y + pad, c.color);
        } else {
            index = addText(std::move(l), 0, y, c.color);
        }
        y += h + 2 * pad;
        if (b) {
            mainItem(index);
        }
        out.rawItem = static_cast<int>(index);
        out.rawBegin = begin;
        out.rawEnd = rawEnd;
        if (b && kind == BlockKind::Paragraph) {
            displayPreview(*b, c);
        }
        margin(kind == BlockKind::Heading ? o.size * 0.45 : 0.75 * st.size);
    }

    /// Below a paragraph being written that has $$…$$ formulas: how they look (as Obsidian shows a formula block
    /// being written). Only drawn: no place of the source is in it.
    void displayPreview(const Block& b, const Ctx& c) {
        for (const Run& r: mergedFormulas(b.runs)) {
            if (!(r.flags & DisplayMath)) {
                continue;
            }
            Run shown = r;
            shown.flags = Math | DisplayMath;
            auto l = text({shown}, {st.size, false, false, st.width});
            l.sources.clear();
            l.links.clear();
            const double h = pangoHeight(l.get());
            addText(std::move(l), 0, y, c.color);
            y += h;
        }
    }

    /// Whether the cursor (`active`) is on the lines after a fenced code block that is closed (not on its lines).
    bool afterClosedCode(const Block& b) const {
        using namespace text;
        if (b.kind != BlockKind::CodeBlock || !b.fenced || active < rawSpan.end || rawSpan.end == 0) {
            return false;
        }
        const std::string fence = fenceOf(lineAt(source, rawSpan.begin));
        const size_t last = lineStart(source, rawSpan.end - 1);  // (its last line)
        return !fence.empty() && last > rawSpan.begin && closesFence(lineAt(source, last), fence);
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
    bool mathAsSource = false;  ///< formulas as their source (the block being written)
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
                hit.wiki = static_cast<size_t>(span.link) < layout.wikiLinks.size() &&
                           layout.wikiLinks[static_cast<size_t>(span.link)];
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

std::vector<LinkHit> linkBoxes(const Layout& layout) {
    std::vector<LinkHit> out;
    for (const Item& it: layout.items) {
        if (it.kind != Item::Kind::Text || it.links.empty()) {
            continue;
        }
        const char* text = pango_layout_get_text(it.layout.get());
        for (const LinkSpan& span: it.links) {
            if (span.link < 0 || span.link >= static_cast<int>(layout.links.size()) || span.end <= span.start) {
                continue;
            }
            // Character by character: one box per line the link is on
            std::vector<LinkHit> lines;
            for (int i = span.start; i < span.end; i = static_cast<int>(g_utf8_next_char(text + i) - text)) {
                PangoRectangle r;
                pango_layout_index_to_pos(it.layout.get(), i, &r);
                const double x0 = it.x + std::min(r.x, r.x + r.width) / static_cast<double>(PANGO_SCALE);
                const double x1 = it.x + std::max(r.x, r.x + r.width) / static_cast<double>(PANGO_SCALE);
                const double y = it.y + r.y / static_cast<double>(PANGO_SCALE);
                const double h = r.height / static_cast<double>(PANGO_SCALE);
                if (!lines.empty() && std::abs(lines.back().y - y) < 0.01) {
                    LinkHit& l = lines.back();
                    const double right = std::max(l.x + l.width, x1);
                    l.x = std::min(l.x, x0);
                    l.width = right - l.x;
                    l.height = std::max(l.height, h);
                    continue;
                }
                LinkHit l;
                l.target = layout.links[static_cast<size_t>(span.link)];
                l.wiki = static_cast<size_t>(span.link) < layout.wikiLinks.size() &&
                         layout.wikiLinks[static_cast<size_t>(span.link)];
                l.x = x0;
                l.y = y;
                l.width = x1 - x0;
                l.height = h;
                lines.push_back(std::move(l));
            }
            out.insert(out.end(), lines.begin(), lines.end());
        }
    }
    return out;
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

std::vector<Rect> textRects(const Item& it, int from, int to) {
    std::vector<Rect> out;
    if (it.kind != Item::Kind::Text || !it.layout || from >= to) {
        return out;
    }
    constexpr double S = PANGO_SCALE;
    PangoLayout* l = it.layout.get();
    PangoLayoutIter* iter = pango_layout_get_iter(l);
    do {
        PangoLayoutLine* line = pango_layout_iter_get_line_readonly(iter);
        const int start = line->start_index;
        const int end = start + line->length;
        if (end <= from || start >= to) {
            if (start >= to) {
                break;
            }
            continue;
        }
        // Left and right: the drawn range on this line; top and height: its first character's (as a caret's)
        int* ranges = nullptr;
        int n = 0;
        pango_layout_line_get_x_ranges(line, std::max(from, start), std::min(to, end), &ranges, &n);
        if (n > 0) {
            int left = ranges[0];
            int right = ranges[1];
            for (int k = 1; k < n; ++k) {
                left = std::min(left, ranges[2 * k]);
                right = std::max(right, ranges[2 * k + 1]);
            }
            PangoRectangle first;
            pango_layout_index_to_pos(l, std::max(from, start), &first);
            Rect r{it.x + left / S, it.y + first.y / S, (right - left) / S, first.height / S};
            for (const MathSpan& m: it.maths) {
                // A display formula takes its whole line: where it is drawn in it
                if (m.display && m.error.empty() && std::max(from, start) == m.start &&
                    std::min(to, end) == m.start + m.length) {
                    r.x += m.inkX;
                    r.width = m.inkWidth;
                }
            }
            out.push_back(r);
        }
        g_free(ranges);
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
    return out;
}

std::vector<Rect> sourceRects(const Layout& layout, size_t begin, size_t end) {
    std::vector<Rect> out;
    for (const Item& it: layout.items) {
        if (it.kind != Item::Kind::Text) {
            continue;
        }
        // The bytes of the item's text from that source, joined where they touch
        int from = -1;
        int to = -1;
        const auto flush = [&] {
            if (from >= 0 && to > from) {
                const auto rects = textRects(it, from, to);
                out.insert(out.end(), rects.begin(), rects.end());
            }
            from = to = -1;
        };
        for (const SourceMap& m: it.sources) {
            if (m.source == NO_SOURCE || m.length == 0 || m.source + std::max<size_t>(m.sourceLength, 1) <= begin ||
                m.source >= end) {
                continue;
            }
            int a = m.start;
            int b = m.start + m.length;
            if (m.sourceLength == static_cast<size_t>(m.length)) {  // (the text is the source: byte for byte)
                a = m.start + static_cast<int>(std::max(begin, m.source) - m.source);
                b = m.start + static_cast<int>(std::min(end, m.source + m.sourceLength) - m.source);
            }
            if (a != to) {
                flush();
                from = a;
            }
            to = b;
        }
        flush();
    }
    return out;
}

std::string searchText(const Item& it) {
    if (it.kind != Item::Kind::Text || !it.layout) {
        return {};
    }
    const std::string_view laid = pango_layout_get_text(it.layout.get());
    if (it.maths.empty()) {
        return std::string(laid);
    }
    std::string out;
    size_t at = 0;
    for (const MathSpan& m: it.maths) {
        if (!m.error.empty() || static_cast<size_t>(m.start) < at) {
            continue;  // (not drawn: its source is in the text)
        }
        out.append(laid.substr(at, static_cast<size_t>(m.start) - at));
        out += m.tex;
        at = static_cast<size_t>(m.start + m.length);
    }
    out.append(laid.substr(std::min(at, laid.size())));
    return out;
}

std::pair<int, int> layoutRange(const Item& it, int from, int to) {
    // Walk the formulas drawn: before each, the texts are the same, shifted by what the sources before added
    int shift = 0;  // (searched text - Pango text)
    int laidFrom = -1;
    int laidTo = -1;
    for (const MathSpan& m: it.maths) {
        if (!m.error.empty()) {
            continue;
        }
        const int texStart = m.start + shift;
        const int texEnd = texStart + static_cast<int>(m.tex.size());
        if (laidFrom < 0 && from < texEnd) {
            laidFrom = from < texStart ? from - shift : m.start;
        }
        if (laidTo < 0 && to <= texEnd) {
            laidTo = to <= texStart ? to - shift : m.start + m.length;
        }
        shift += static_cast<int>(m.tex.size()) - m.length;
    }
    if (laidFrom < 0) {
        laidFrom = from - shift;
    }
    if (laidTo < 0) {
        laidTo = to - shift;
    }
    return {laidFrom, std::max(laidFrom, laidTo)};
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
        // (the source of a formula is searched; a match in it marks the formula)
        const std::string shown = searchText(it);
        const std::string text = StringUtils::toLowerCase(shown);
        for (size_t pos = text.find(pattern); pos != std::string::npos; pos = text.find(pattern, pos + 1)) {
            // (a lower case of another length moves the places a little; they stay in the text)
            const auto from = static_cast<int>(std::min(pos, shown.size()));
            const auto to = static_cast<int>(std::min(pos + pattern.size(), shown.size()));
            const auto [a, b] = layoutRange(it, from, to);
            const auto rects = textRects(it, a, b);
            found.insert(found.end(), rects.begin(), rects.end());
        }
    }
    return found;
}

std::optional<MathHit> mathAt(const Layout& layout, double x, double y) {
    for (const Item& it: layout.items) {
        for (const MathSpan& m: it.maths) {
            for (const Rect& r: textRects(it, m.start, m.start + m.length)) {
                if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
                    return MathHit{m, r};
                }
            }
        }
    }
    return std::nullopt;
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
                emoji::showLayout(cr, it.layout.get());  // (colour emoji sharp in PDFs)
                break;
        }
    }
    cairo_restore(cr);
}

}  // namespace xqt::md
