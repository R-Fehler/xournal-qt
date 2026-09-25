#include "EmojiFont.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-font.h>

namespace xqt::emoji {

namespace {

std::atomic<bool> registered{false};

/// Emoji (Pango asks for them with the language "und-zsye") in the app's font first, whatever the text's font and
/// the system's rules say: the same pictures on every system, and one that Cairo can draw.
std::string preferRule() {
    return std::string("  <match target=\"pattern\">\n"
                       "    <test name=\"lang\"><string>und-zsye</string></test>\n"
                       "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\"><string>") +
           FONT_FAMILY +
           "</string></edit>\n"
           "  </match>\n";
}

/// fontconfig's 10-scale-bitmap-fonts.conf, the part for colour bitmap fonts: their glyphs at the size asked.
constexpr const char* SCALE_BITMAPS = R"(  <match target="font">
    <test name="outline" compare="eq"><bool>false</bool></test>
    <edit name="pixelsizefixupfactor" mode="assign">
      <divide><name target="pattern">pixelsize</name><name target="font">pixelsize</name></divide>
    </edit>
  </match>
  <match target="font">
    <test name="outline" compare="eq"><bool>false</bool></test>
    <test name="pixelsizefixupfactor" compare="not_eq"><double>1.0</double></test>
    <edit name="matrix" mode="assign">
      <times><name>matrix</name>
        <matrix><name>pixelsizefixupfactor</name><double>0</double><double>0</double><name>pixelsizefixupfactor</name></matrix>
      </times>
    </edit>
    <edit name="size" mode="assign"><divide><name>size</name><name>pixelsizefixupfactor</name></divide></edit>
  </match>
)";

bool vectorTarget(cairo_t* cr) {
    switch (cairo_surface_get_type(cairo_get_target(cr))) {
        case CAIRO_SURFACE_TYPE_PDF:
        case CAIRO_SURFACE_TYPE_PS:
        case CAIRO_SURFACE_TYPE_SVG:
            return true;
        default:
            return false;
    }
}

/// A colour glyph drawn big, to be scaled down into a vector surface. Every picture has an id of its own (Cairo's
/// unique id), so a PDF holds each emoji once however often it is on its pages.
struct Picture {
    cairo_font_face_t* face = nullptr;  ///< (a reference: the key)
    unsigned glyph = 0;
    int pixels = 0;
    cairo_surface_t* image = nullptr;
    double scale = 1;       ///< pixels per unit of the text
    double x = 0, y = 0;    ///< its top left from the glyph's origin (units of the text)
};

/// The pictures of this thread (the last ones used; each is at most about 140 x 140 pixels).
class Pictures {
public:
    static constexpr size_t LIMIT = 64;
    ~Pictures() {
        for (Picture& p: list) {
            forget(p);
        }
    }
    const Picture* get(cairo_scaled_font_t* font, unsigned glyph) {
        cairo_matrix_t m;
        cairo_scaled_font_get_font_matrix(font, &m);
        const double em = std::hypot(m.yx, m.yy);  // the text's size (units of the layout)
        if (em <= 0) {
            return nullptr;
        }
        // About the font's own resolution (Noto's bitmaps are 136 x 128 pixels at 109 pixels per em), never less
        // than the text's size itself
        const double scale = std::max(1.0, 128.0 / em);
        const int pixels = static_cast<int>(std::lround(em * scale));
        cairo_font_face_t* face = cairo_scaled_font_get_font_face(font);
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (it->face == face && it->glyph == glyph && it->pixels == pixels) {
                list.splice(list.begin(), list, it);
                return &list.front();
            }
        }
        Picture p = draw(font, glyph, scale);
        if (!p.image) {
            return nullptr;
        }
        p.face = cairo_font_face_reference(face);
        p.glyph = glyph;
        p.pixels = pixels;
        list.push_front(p);
        if (list.size() > LIMIT) {
            forget(list.back());
            list.pop_back();
        }
        return &list.front();
    }

private:
    static void forget(Picture& p) {
        cairo_surface_destroy(p.image);
        cairo_font_face_destroy(p.face);
    }
    static Picture draw(cairo_scaled_font_t* font, unsigned glyph, double scale) {
        Picture p;
        cairo_glyph_t g{glyph, 0, 0};
        cairo_text_extents_t ext;
        cairo_scaled_font_glyph_extents(font, &g, 1, &ext);
        if (ext.width <= 0 || ext.height <= 0) {
            return p;
        }
        const double pad = 1 / scale;  // (a pixel around it: no edge cut by rounding)
        const int w = static_cast<int>(std::ceil((ext.width + 2 * pad) * scale));
        const int h = static_cast<int>(std::ceil((ext.height + 2 * pad) * scale));
        if (w > 4096 || h > 4096) {
            return p;
        }
        cairo_surface_t* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(image);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, pad - ext.x_bearing, pad - ext.y_bearing);
        // (the same face, matrix and options: Cairo makes it anew for this scale)
        cairo_set_scaled_font(cr, font);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_show_glyphs(cr, &g, 1);
        cairo_destroy(cr);
        static std::atomic<uint64_t> ids{0};
        const std::string id = "xqt-emoji-" + std::to_string(++ids);
        auto* data = static_cast<unsigned char*>(g_malloc(id.size()));
        std::copy(id.begin(), id.end(), data);
        cairo_surface_set_mime_data(image, CAIRO_MIME_TYPE_UNIQUE_ID, data, id.size(), g_free, data);
        p.image = image;
        p.scale = scale;
        p.x = ext.x_bearing - pad;
        p.y = ext.y_bearing - pad;
        return p;
    }

    std::list<Picture> list;
};

Pictures& pictures() {
    static thread_local Pictures p;
    return p;
}

}  // namespace

bool registerFont(const std::string& fontFile) {
    FcConfig* config = FcConfigGetCurrent();
    if (!config || !FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(fontFile.c_str()))) {
        return false;
    }
    const std::string rules = "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
                              "<fontconfig>\n" +
                              preferRule() + "</fontconfig>\n";
    FcConfigParseAndLoadFromMemory(config, reinterpret_cast<const FcChar8*>(rules.c_str()), FcTrue);
    registered = true;
    return true;
}

bool fontRegistered() { return registered; }

std::string fontconfigRules(bool scaleBitmaps) {
    std::string out = preferRule();
    if (scaleBitmaps) {
        out += SCALE_BITMAPS;
    }
    return out;
}

bool isColorFont(PangoFont* font) {
    if (!font || !PANGO_IS_FC_FONT(font)) {
        return false;
    }
    FcPattern* pattern = pango_fc_font_get_pattern(PANGO_FC_FONT(font));
    FcBool color = FcFalse;
    return pattern && FcPatternGetBool(pattern, FC_COLOR, 0, &color) == FcResultMatch && color;
}

void showLayout(cairo_t* cr, PangoLayout* layout) {
    if (!vectorTarget(cr)) {
        pango_cairo_show_layout(cr, layout);
        return;
    }
    // The colour glyphs: taken out of the layout while Pango draws the rest (underlines, backgrounds, and the
    // widths stay), then drawn as pictures, and put back
    struct Glyph {
        PangoGlyphInfo* info;
        PangoGlyph glyph;
        PangoFont* font;
        double x, y;  ///< its origin, in the layout (units)
    };
    std::vector<Glyph> glyphs;
    PangoLayoutIter* it = pango_layout_get_iter(layout);
    do {
        PangoLayoutRun* run = pango_layout_iter_get_run(it);
        if (!run || !isColorFont(run->item->analysis.font)) {
            continue;
        }
        PangoRectangle logical;
        pango_layout_iter_get_run_extents(it, nullptr, &logical);
        const int baseline = pango_layout_iter_get_baseline(it);
        int x = logical.x;
        for (int i = 0; i < run->glyphs->num_glyphs; ++i) {
            PangoGlyphInfo& g = run->glyphs->glyphs[i];
            if (g.glyph != PANGO_GLYPH_EMPTY && !(g.glyph & PANGO_GLYPH_UNKNOWN_FLAG)) {
                glyphs.push_back({&g, g.glyph, run->item->analysis.font,
                                  static_cast<double>(x + g.geometry.x_offset) / PANGO_SCALE,
                                  static_cast<double>(baseline + g.geometry.y_offset) / PANGO_SCALE});
            }
            x += g.geometry.width;
        }
    } while (pango_layout_iter_next_run(it));
    pango_layout_iter_free(it);
    if (glyphs.empty()) {
        pango_cairo_show_layout(cr, layout);
        return;
    }
    double x0 = 0;
    double y0 = 0;
    if (cairo_has_current_point(cr)) {
        cairo_get_current_point(cr, &x0, &y0);
    }
    for (Glyph& g: glyphs) {
        g.info->glyph = PANGO_GLYPH_EMPTY;
    }
    pango_cairo_show_layout(cr, layout);
    for (Glyph& g: glyphs) {
        g.info->glyph = g.glyph;
    }
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    for (const Glyph& g: glyphs) {
        const Picture* p = pictures().get(pango_cairo_font_get_scaled_font(PANGO_CAIRO_FONT(g.font)), g.glyph);
        if (!p) {
            continue;
        }
        cairo_save(cr);
        cairo_translate(cr, x0 + g.x + p->x, y0 + g.y + p->y);
        cairo_scale(cr, 1 / p->scale, 1 / p->scale);
        cairo_set_source_surface(cr, p->image, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

}  // namespace xqt::emoji
