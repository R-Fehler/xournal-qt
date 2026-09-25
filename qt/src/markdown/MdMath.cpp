#include "MdMath.h"

#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <pango/pangocairo.h>

#include "core/formula.h"
#include "graphic/graphic.h"
#include "microtex.h"
#include "render/builder.h"
#include "render/render.h"
#include "unimath/font_src.h"

extern "C" {
// The math font's data (qt/3rdparty/microtex/res/lm-math, compiled in by XqtEmbedFile.cmake)
extern const unsigned char xqt_math_font[];
extern const unsigned long xqt_math_font_size;
}

namespace xqt::md::math {

namespace {

/// MicroTeX lays formulas out at this size (pixels per em; its own fixed size, so no scaling inside). The paths are
/// recorded at it (Cairo's fixed point has 1/256 of a unit: 1/256000 em) and kept in em.
constexpr float TEXT_SIZE = 1000;
/// The foreground given to MicroTeX: what it draws in it is drawn in the color of the text around the formula.
constexpr microtex::color TEXT_COLOR = 0xff010203;
/// Sources longer or nested deeper than this are not given to MicroTeX (its parser recurses).
constexpr size_t MAX_SOURCE = 8000;
constexpr int MAX_NESTING = 64;

/// Fonts are not used: glyphs are drawn as paths (GLYPH_RENDER_TYPE_PATH).
class NoFont: public microtex::Font {
public:
    bool operator==(const microtex::Font& f) const override { return this == &f; }
};

class Recorder;

/// Characters the math font does not have (in \text{…}: accented letters, other scripts): Pango, as the text
/// around. Made and used while a formula is laid out (under the lock, on one thread).
class PangoText: public microtex::TextLayout {
public:
    PangoText(const std::string& text, microtex::FontStyle style, float size) {
        layout = pango_layout_new(context());
        PangoFontDescription* d = pango_font_description_new();
        pango_font_description_set_family(d, microtex::isMono(style)        ? "Monospace" :
                                             microtex::isSansSerif(style) ? "Sans" :
                                                                            "Serif");
        pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
        if (microtex::isBold(style)) {
            pango_font_description_set_weight(d, PANGO_WEIGHT_BOLD);
        }
        if (microtex::isItalic(style)) {
            pango_font_description_set_style(d, PANGO_STYLE_ITALIC);
        }
        pango_layout_set_font_description(layout, d);
        pango_font_description_free(d);
        pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
        ascent = static_cast<float>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
    }
    ~PangoText() override { g_object_unref(layout); }
    PangoText(const PangoText&) = delete;
    PangoText& operator=(const PangoText&) = delete;

    void getBounds(microtex::Rect& bounds) override {
        PangoRectangle logical;
        pango_layout_get_extents(layout, nullptr, &logical);
        bounds.x = 0;
        bounds.y = -ascent;
        bounds.w = static_cast<float>(logical.width) / PANGO_SCALE;
        bounds.h = static_cast<float>(logical.height) / PANGO_SCALE;
    }
    void draw(microtex::Graphics2D& g2, float x, float y) override;

private:
    /// This thread's Pango context (as the text's in MdLayout: no hinting, the same at any zoom).
    static PangoContext* context() {
        static thread_local std::unique_ptr<PangoContext, decltype(&g_object_unref)> ctx(nullptr, g_object_unref);
        if (!ctx) {
            ctx.reset(pango_font_map_create_context(pango_cairo_font_map_get_default()));
            cairo_font_options_t* options = cairo_font_options_create();
            cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
            cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
            pango_cairo_context_set_font_options(ctx.get(), options);
            cairo_font_options_destroy(options);
        }
        return ctx.get();
    }

    PangoLayout* layout = nullptr;
    float ascent = 0;
};

class Factory: public microtex::PlatformFactory {
public:
    microtex::sptr<microtex::Font> createFont(const std::string&) override {
        return std::make_shared<NoFont>();
    }
    microtex::sptr<microtex::TextLayout> createTextLayout(const std::string& src, microtex::FontStyle style,
                                                          float size) override {
        return std::make_shared<PangoText>(src, style, size);
    }
};

/// A Graphics2D that records what MicroTeX draws as paths: the path is built in a Cairo context (so transformations
/// and arcs are Cairo's), then copied in device space, which is MicroTeX's pixels, and kept in em.
class Recorder: public microtex::Graphics2D {
public:
    Recorder(Formula& out, double ascentPx): out(out), ascent(ascentPx) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cr = cairo_create(surface);
    }
    ~Recorder() override {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void setColor(microtex::color c) override { color = c; }
    microtex::color getColor() const override { return color; }
    void setStroke(const microtex::Stroke& s) override { stroke = s; }
    const microtex::Stroke& getStroke() const override { return stroke; }
    void setStrokeWidth(float w) override { stroke.lineWidth = w; }
    void setDash(const std::vector<float>& d) override { dash = d; }
    std::vector<float> getDash() override { return dash; }
    microtex::sptr<microtex::Font> getFont() const override { return font; }
    void setFont(const microtex::sptr<microtex::Font>& f) override { font = f; }
    float getFontSize() const override { return fontSize; }
    void setFontSize(float size) override { fontSize = size; }

    void translate(float dx, float dy) override { cairo_translate(cr, dx, dy); }
    void scale(float sx, float sy) override { cairo_scale(cr, sx, sy); }
    void rotate(float angle) override { cairo_rotate(cr, angle); }
    void rotate(float angle, float px, float py) override {
        cairo_translate(cr, px, py);
        cairo_rotate(cr, angle);
        cairo_translate(cr, -px, -py);
    }
    void reset() override { cairo_identity_matrix(cr); }
    float sx() const override {
        cairo_matrix_t m;
        cairo_get_matrix(cr, &m);
        return static_cast<float>(m.xx);
    }
    float sy() const override {
        cairo_matrix_t m;
        cairo_get_matrix(cr, &m);
        return static_cast<float>(m.yy);
    }

    void drawGlyph(microtex::u16, float, float) override {}  // (glyphs come as paths)

    bool beginPath(microtex::i32) override {
        cairo_new_path(cr);
        return false;  // (no path is kept by id: it is drawn again)
    }
    void moveTo(float x, float y) override { cairo_move_to(cr, x, y); }
    void lineTo(float x, float y) override { cairo_line_to(cr, x, y); }
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) override {
        cairo_curve_to(cr, x1, y1, x2, y2, x3, y3);
    }
    void quadTo(float x1, float y1, float x2, float y2) override {
        double x0 = 0;
        double y0 = 0;
        cairo_get_current_point(cr, &x0, &y0);
        cairo_curve_to(cr, x0 + 2.0 / 3 * (x1 - x0), y0 + 2.0 / 3 * (y1 - y0), x2 + 2.0 / 3 * (x1 - x2),
                       y2 + 2.0 / 3 * (y1 - y2), x2, y2);
    }
    void closePath() override { cairo_close_path(cr); }
    void fillPath(microtex::i32) override { keep(false); }

    void drawLine(float x1, float y1, float x2, float y2) override {
        cairo_new_path(cr);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        keep(true);
    }
    void drawRect(float x, float y, float w, float h) override {
        cairo_new_path(cr);
        cairo_rectangle(cr, x, y, w, h);
        keep(true);
    }
    void fillRect(float x, float y, float w, float h) override {
        cairo_new_path(cr);
        cairo_rectangle(cr, x, y, w, h);
        keep(false);
    }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        roundRect(x, y, w, h, rx, ry);
        keep(true);
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        roundRect(x, y, w, h, rx, ry);
        keep(false);
    }

    /// A text laid out by Pango, its top left at (x, y): as filled outlines.
    void text(PangoLayout* layout, double x, double y) {
        cairo_new_path(cr);
        cairo_move_to(cr, x, y);
        pango_cairo_layout_path(cr, layout);
        keep(false);
    }

private:
    void roundRect(double x, double y, double w, double h, double rx, double ry) {
        cairo_new_path(cr);
        if (rx <= 0 || ry <= 0) {
            cairo_rectangle(cr, x, y, w, h);
            return;
        }
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, rx, ry);
        const double W = w / rx;
        const double H = h / ry;
        cairo_new_sub_path(cr);
        cairo_arc(cr, W - 1, 1, 1, -M_PI / 2, 0);
        cairo_arc(cr, W - 1, H - 1, 1, 0, M_PI / 2);
        cairo_arc(cr, 1, H - 1, 1, M_PI / 2, M_PI);
        cairo_arc(cr, 1, 1, 1, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
        cairo_restore(cr);
    }

    /// The current path as a shape of the formula (filled or stroked with the current stroke).
    void keep(bool stroked) {
        if (microtex::isTransparent(color)) {
            cairo_new_path(cr);
            return;
        }
        Formula::Shape s;
        s.stroke = stroked;
        s.color = color == TEXT_COLOR ? 0 : color;
        if (stroked) {
            // The width in device space (MicroTeX scales uniformly)
            double dx = stroke.lineWidth;
            double dy = 0;
            cairo_user_to_device_distance(cr, &dx, &dy);
            const double factor = std::hypot(dx, dy) / std::max(1e-9, static_cast<double>(stroke.lineWidth));
            s.lineWidth = static_cast<float>(stroke.lineWidth * factor / TEXT_SIZE);
            s.cap = static_cast<uint8_t>(stroke.cap == microtex::CAP_BUTT    ? CAIRO_LINE_CAP_BUTT :
                                         stroke.cap == microtex::CAP_SQUARE ? CAIRO_LINE_CAP_SQUARE :
                                                                              CAIRO_LINE_CAP_ROUND);
            s.join = static_cast<uint8_t>(stroke.join == microtex::JOIN_BEVEL ? CAIRO_LINE_JOIN_BEVEL :
                                          stroke.join == microtex::JOIN_MITER ? CAIRO_LINE_JOIN_MITER :
                                                                                CAIRO_LINE_JOIN_ROUND);
            for (float d: dash) {
                s.dash.push_back(d * factor / TEXT_SIZE);
            }
        }
        cairo_save(cr);
        cairo_identity_matrix(cr);
        cairo_path_t* path = cairo_copy_path(cr);
        cairo_restore(cr);
        const auto point = [&](const cairo_path_data_t& p) {
            s.points.push_back(static_cast<float>(p.point.x / TEXT_SIZE));
            s.points.push_back(static_cast<float>((p.point.y - ascent) / TEXT_SIZE));
        };
        for (int i = 0; i < path->num_data; i += path->data[i].header.length) {
            const cairo_path_data_t* d = &path->data[i];
            switch (d->header.type) {
                case CAIRO_PATH_MOVE_TO:
                    s.ops.push_back(Formula::Shape::Move);
                    point(d[1]);
                    break;
                case CAIRO_PATH_LINE_TO:
                    s.ops.push_back(Formula::Shape::Line);
                    point(d[1]);
                    break;
                case CAIRO_PATH_CURVE_TO:
                    s.ops.push_back(Formula::Shape::Curve);
                    point(d[1]);
                    point(d[2]);
                    point(d[3]);
                    break;
                case CAIRO_PATH_CLOSE_PATH:
                    s.ops.push_back(Formula::Shape::Close);
                    break;
            }
        }
        cairo_path_destroy(path);
        cairo_new_path(cr);
        if (!s.ops.empty()) {
            out.shapes.push_back(std::move(s));
        }
    }

    Formula& out;
    double ascent;  ///< px: the baseline is here in MicroTeX's drawing (its top is 0)
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;
    microtex::color color = TEXT_COLOR;
    microtex::Stroke stroke;
    std::vector<float> dash;
    microtex::sptr<microtex::Font> font;
    float fontSize = TEXT_SIZE;
};

void PangoText::draw(microtex::Graphics2D& g2, float x, float y) {
    static_cast<Recorder&>(g2).text(layout, x, y - ascent);
}

/// MicroTeX's state is global (its macros, the fonts, the platform factory): one formula at a time.
std::mutex& microtexMutex() {
    static std::mutex m;
    return m;
}

std::string mathFontName;  ///< (under microtexMutex)

bool initialise() {
    if (microtex::MicroTeX::isInited()) {
        return !mathFontName.empty();
    }
    microtex::PlatformFactory::registerFactory("xournal-qt", std::make_unique<Factory>());
    microtex::PlatformFactory::activate("xournal-qt");
    const microtex::FontSrcData src(static_cast<size_t>(xqt_math_font_size), xqt_math_font);
    mathFontName = microtex::MicroTeX::init(src).name;
    microtex::MicroTeX::setRenderGlyphUsePath(true);
    return !mathFontName.empty();
}

/// Why a source is not given to MicroTeX, if it is not.
std::string refused(std::string_view tex) {
    if (tex.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return "An empty formula";
    }
    if (tex.size() > MAX_SOURCE) {
        return "The formula is too long";
    }
    int depth = 0;
    int deepest = 0;
    for (size_t i = 0; i < tex.size(); ++i) {
        if (tex[i] == '\\') {
            ++i;  // (\{ and \} are characters)
        } else if (tex[i] == '{') {
            deepest = std::max(deepest, ++depth);
        } else if (tex[i] == '}') {
            --depth;
        }
    }
    if (deepest > MAX_NESTING) {
        return "The formula is nested too deeply";
    }
    return {};
}

std::shared_ptr<const Formula> layOut(std::string_view tex, bool display) {
    auto f = std::make_shared<Formula>();
    if (f->error = refused(tex); !f->error.empty()) {
        return f;
    }
    std::lock_guard lock(microtexMutex());
    try {
        if (!initialise()) {
            f->error = "The math font could not be loaded";
            return f;
        }
        microtex::Formula parsed{std::string(tex)};
        std::unique_ptr<microtex::Render> render(microtex::RenderBuilder()
                                                         .setStyle(display ? microtex::TexStyle::display :
                                                                             microtex::TexStyle::text)
                                                         .setTextSize(TEXT_SIZE)
                                                         .setMathFontName(mathFontName)
                                                         .setForeground(TEXT_COLOR)
                                                         .build(parsed));
        const double height = render->getHeight();  // (above and below the baseline)
        const double ascentPx = height > 0 ? height * render->getBaseline() : 0;
        if (render->getWidth() <= 0 || !std::isfinite(ascentPx)) {
            f->error = "An empty formula";  // (e.g. "{}": nothing to draw, nothing to tap)
            return f;
        }
        f->width = render->getWidth() / TEXT_SIZE;
        f->ascent = ascentPx / TEXT_SIZE;
        f->descent = (height - ascentPx) / TEXT_SIZE;
        Recorder recorder(*f, ascentPx);
        render->draw(recorder, 0, 0);
        f->ok = true;
    } catch (const std::exception& e) {
        f->ok = false;
        f->shapes.clear();
        f->error = e.what();
        // (MicroTeX says "Problem with command: x\n caused by: <the reason>": the reason)
        if (const size_t at = f->error.rfind("caused by: "); at != std::string::npos) {
            f->error = f->error.substr(at + 11);
        }
        if (f->error.empty()) {
            f->error = "The formula cannot be read";
        }
    } catch (...) {
        f->ok = false;
        f->shapes.clear();
        f->error = "The formula cannot be read";
    }
    return f;
}

/// The formulas laid out, by source: the least recently used go beyond CACHE_LIMIT.
struct Cache {
    std::mutex mtx;
    using Entry = std::pair<std::string, std::shared_ptr<const Formula>>;
    std::list<Entry> order;  ///< the most recently used first
    std::unordered_map<std::string, std::list<Entry>::iterator> index;
    size_t bytes = 0;
    size_t hits = 0;
    size_t misses = 0;
};
Cache& cache() {
    static Cache c;
    return c;
}

}  // namespace

size_t Formula::bytes() const {
    size_t n = sizeof(Formula) + error.size();
    for (const Shape& s: shapes) {
        n += sizeof(Shape) + s.ops.size() + s.points.size() * sizeof(float) + s.dash.size() * sizeof(double);
    }
    return n;
}

std::shared_ptr<const Formula> formula(std::string_view tex, bool display) {
    std::string key;
    key.reserve(tex.size() + 1);
    key += display ? 'D' : 'T';
    key += tex;
    Cache& c = cache();
    {
        std::lock_guard lock(c.mtx);
        if (auto it = c.index.find(key); it != c.index.end()) {
            c.order.splice(c.order.begin(), c.order, it->second);
            ++c.hits;
            return it->second->second;
        }
    }
    auto f = layOut(tex, display);  // (not under the cache's lock: other threads find theirs meanwhile)
    std::lock_guard lock(c.mtx);
    ++c.misses;
    if (auto it = c.index.find(key); it != c.index.end()) {
        return it->second->second;  // (laid out by another thread meanwhile)
    }
    c.order.emplace_front(key, f);
    c.index.emplace(std::move(key), c.order.begin());
    c.bytes += f->bytes();
    while (c.bytes > CACHE_LIMIT && c.order.size() > 1) {
        c.bytes -= c.order.back().second->bytes();
        c.index.erase(c.order.back().first);
        c.order.pop_back();
    }
    return f;
}

void draw(cairo_t* cr, const Formula& f, double x, double y, double size, bool pathOnly) {
    if (!f.ok) {
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size, size);
    for (const Formula::Shape& s: f.shapes) {
        if (pathOnly && s.stroke) {
            continue;
        }
        if (!pathOnly) {
            cairo_new_path(cr);
        }
        const float* p = s.points.data();
        for (Formula::Shape::Op op: s.ops) {
            switch (op) {
                case Formula::Shape::Move:
                    cairo_move_to(cr, p[0], p[1]);
                    p += 2;
                    break;
                case Formula::Shape::Line:
                    cairo_line_to(cr, p[0], p[1]);
                    p += 2;
                    break;
                case Formula::Shape::Curve:
                    cairo_curve_to(cr, p[0], p[1], p[2], p[3], p[4], p[5]);
                    p += 6;
                    break;
                case Formula::Shape::Close:
                    cairo_close_path(cr);
                    break;
            }
        }
        if (pathOnly) {
            continue;
        }
        cairo_save(cr);
        if (s.color != 0) {
            cairo_set_source_rgba(cr, ((s.color >> 16) & 0xff) / 255.0, ((s.color >> 8) & 0xff) / 255.0,
                                  (s.color & 0xff) / 255.0, ((s.color >> 24) & 0xff) / 255.0);
        }
        if (s.stroke) {
            cairo_set_line_width(cr, s.lineWidth);
            cairo_set_line_cap(cr, static_cast<cairo_line_cap_t>(s.cap));
            cairo_set_line_join(cr, static_cast<cairo_line_join_t>(s.join));
            cairo_set_dash(cr, s.dash.empty() ? nullptr : s.dash.data(), static_cast<int>(s.dash.size()), 0);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

CacheStats cacheStats() {
    Cache& c = cache();
    std::lock_guard lock(c.mtx);
    return {c.order.size(), c.bytes, c.hits, c.misses};
}

void clearCache() {
    Cache& c = cache();
    std::lock_guard lock(c.mtx);
    c.order.clear();
    c.index.clear();
    c.bytes = 0;
    c.hits = 0;
    c.misses = 0;
}

}  // namespace xqt::md::math
