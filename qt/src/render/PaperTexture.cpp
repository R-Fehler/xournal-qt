/*
 * xournal-qt: page colors and textured paper (see PaperTexture.h).
 *
 * @license GNU GPLv2 or later
 */
#include "PaperTexture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "model/PageType.h"
#include "model/XojPage.h"
#include "util/StringUtils.h"
#include "view/background/BackgroundView.h"

namespace xqt::paper {

const std::vector<Swatch>& swatches() {
    static const std::vector<Swatch> list{
            {"white", 0xffffff},         //
            {"illustration", 0xf2e6cb},  // warm cream of sketch and illustration paper
            {"kraft", 0xc9a77c},         // kraft brown
            {"sage", 0xe3ecd9},          // soft green
            {"mist", 0xdde7f0},          // soft blue
            {"grey", 0x5c5f66},          //
            {"darkgrey", 0x2b2d31},      //
            {"black", 0x161616},         // (not pure black: ink has room to be darker)
    };
    return list;
}

namespace {

std::vector<std::pair<std::string, std::string>> parse(const std::string& config) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& s: StringUtils::split(config, ',')) {
        if (s.empty()) {
            continue;
        }
        const size_t eq = s.find_last_of('=');  // (as upstream's BackgroundConfig)
        out.emplace_back(eq == std::string::npos ? s : s.substr(0, eq),
                         eq == std::string::npos ? std::string() : s.substr(eq + 1));
    }
    return out;
}
std::string join(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out;
    for (const auto& [k, v]: kv) {
        if (!out.empty()) {
            out += ',';
        }
        out += k + '=' + v;
    }
    return out;
}
std::string with(const std::string& config, const std::string& key, const std::string& value) {
    auto kv = parse(config);
    kv.erase(std::remove_if(kv.begin(), kv.end(), [&](const auto& p) { return p.first == key; }), kv.end());
    if (!value.empty()) {
        kv.emplace_back(key, value);
    }
    return join(kv);
}
std::string get(const std::string& config, const std::string& key) {
    for (const auto& [k, v]: parse(config)) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

/// The keys the fork sets for a paper (texture, ruling colors)
constexpr const char* PAPER_KEYS[] = {TEXTURE_KEY, "f1", "af1", "f2", "af2"};
/// The marker that the ruling colors are the fork's (so a type's own f1 is not taken for one)
constexpr const char* LINES_KEY = "xqt-lines";

// --- color math (OKLab, as canvas/DarkPages.cpp) ----------------------------------------------------------------
double lin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double enc(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
struct Lab {
    double L, a, b;
};
Lab oklab(Color c) {
    const double r = lin(c.red / 255.0), g = lin(c.green / 255.0), b = lin(c.blue / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
Color fromOklab(Lab lab) {
    const double l = std::pow(lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3);
    const double m = std::pow(lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3);
    const double s = std::pow(lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3);
    const auto byte = [](double v) {
        return static_cast<uint8_t>(std::clamp(std::lround(enc(std::clamp(v, 0.0, 1.0)) * 255), 0L, 255L));
    };
    return Color(byte(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
                 byte(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
                 byte(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s));
}
std::string hex(Color c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%02x%02x%02x", c.red, c.green, c.blue);
    return buf;
}

// --- the grain ----------------------------------------------------------------------------------------------------
constexpr int GRAIN = 256;              ///< pixels of the repeated tile
constexpr double GRAIN_POINTS = 120.0;  ///< ... and its size on the page (points): a grain of about half a point

uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
double random01(int x, int y, uint32_t seed) {
    return (hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed) & 0xffff) / 65535.0;
}
/// Smooth noise repeating every GRAIN pixels, of cells `cell` pixels wide
double valueNoise(int x, int y, int cell, uint32_t seed) {
    const int cells = GRAIN / cell;
    const double fx = static_cast<double>(x) / cell, fy = static_cast<double>(y) / cell;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - x0, ty = fy - y0;
    const double sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const auto at = [&](int i, int j) {
        return random01(((i % cells) + cells) % cells, ((j % cells) + cells) % cells, seed);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};

/// The grain as an alpha mask (one per thread: cairo's PDF backend attaches data to a source surface)
cairo_surface_t* grain() {
    thread_local std::unique_ptr<cairo_surface_t, SurfaceDeleter> tile;
    if (!tile) {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, GRAIN, GRAIN);
        cairo_surface_flush(s);
        unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        for (int y = 0; y < GRAIN; ++y) {
            for (int x = 0; x < GRAIN; ++x) {
                // fine tooth, a little mottling, and long fibres (stretched cells)
                const double fine = random01(x, y, 1);
                const double mottle = valueNoise(x, y, 32, 2);
                const double tooth = valueNoise(x, y, 4, 3);
                const double fibre = valueNoise(x, y / 4, 8, 4);
                double v = 0.35 * fine + 0.25 * tooth + 0.25 * mottle + 0.15 * fibre;
                v = std::clamp((v - 0.32) * 1.9, 0.0, 1.0);
                data[y * stride + x] = static_cast<unsigned char>(std::lround(v * 255));
            }
        }
        cairo_surface_mark_dirty(s);
        tile.reset(s);
    }
    return tile.get();
}

class TexturedBackground final: public xoj::view::BackgroundView {
public:
    TexturedBackground(std::unique_ptr<BackgroundView> inner, Color paper, double width, double height):
            BackgroundView(width, height), inner(std::move(inner)), paper(paper) {}
    void draw(cairo_t* cr) const override {
        inner->draw(cr);
        drawTexture(cr, paper, pageWidth, pageHeight);
    }

private:
    std::unique_ptr<BackgroundView> inner;
    Color paper;
};

std::unique_ptr<xoj::view::BackgroundView> decorate(std::unique_ptr<xoj::view::BackgroundView> view,
                                                    const XojPage& page) {
    const PageType type = page.getBackgroundType();
    if (!view || type.isSpecial() || !textured(type.config)) {
        return view;
    }
    return std::make_unique<TexturedBackground>(std::move(view), page.getBackgroundColor(), page.getWidth(),
                                                page.getHeight());
}

}  // namespace

bool textured(const std::string& config) { return get(config, TEXTURE_KEY) == TEXTURE_PAPER; }

std::string withTexture(const std::string& config, bool on) {
    return with(config, TEXTURE_KEY, on ? TEXTURE_PAPER : "");
}

bool isDark(Color c) {
    return 0.2126 * lin(c.red / 255.0) + 0.7152 * lin(c.green / 255.0) + 0.0722 * lin(c.blue / 255.0) < 0.18;
}

Color lineColor(Color paper) {
    Lab l = oklab(paper);
    const bool dark = isDark(paper);
    l.L += dark ? 0.22 : -0.16;
    l.a *= 1.3;
    l.b *= 1.3;
    return fromOklab(l);
}

std::string withLineColors(const std::string& config, Color paper) {
    std::string c = config;
    const bool ours = get(config, LINES_KEY) == "1";
    if (ours) {  // (the ruling colors chosen for another paper go)
        for (const char* k: {"f1", "af1", "f2", "af2", LINES_KEY}) {
            c = with(c, k, "");
        }
    }
    const bool white = paper.red == 255 && paper.green == 255 && paper.blue == 255;
    if (white || (!ours && (!get(c, "f1").empty() || !get(c, "af1").empty()))) {
        return c;  // (white: upstream's; a type's own colors stay)
    }
    const std::string line = hex(lineColor(paper));
    c = with(c, "f1", line);
    c = with(c, "af1", line);
    // The margin line of lined paper: a muted red that shows on this paper
    Lab red = oklab(Color(0xd0u, 0x5au, 0x6eu));
    red.L = std::clamp(oklab(paper).L + (isDark(paper) ? 0.28 : -0.22), 0.35, 0.8);
    const std::string margin = hex(fromOklab(red));
    c = with(c, "f2", margin);
    c = with(c, "af2", margin);
    return with(c, LINES_KEY, "1");
}

std::string baseConfig(const std::string& config) {
    auto kv = parse(config);
    const bool ours = get(config, LINES_KEY) == "1";
    kv.erase(std::remove_if(kv.begin(), kv.end(),
                            [&](const auto& p) {
                                if (p.first == TEXTURE_KEY || p.first == LINES_KEY) {
                                    return true;
                                }
                                return ours &&
                                       (p.first == "f1" || p.first == "af1" || p.first == "f2" || p.first == "af2");
                            }),
             kv.end());
    return join(kv);
}

void drawTexture(cairo_t* cr, Color paper, double width, double height) {
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    cairo_pattern_t* pattern = cairo_pattern_create_for_surface(grain());
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);
    cairo_matrix_t m;
    cairo_matrix_init_scale(&m, GRAIN / GRAIN_POINTS, GRAIN / GRAIN_POINTS);
    cairo_pattern_set_matrix(pattern, &m);
    // Light paper: darker grain; dark paper: lighter grain (subtle either way)
    if (isDark(paper)) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    } else {
        cairo_set_source_rgba(cr, 0.25, 0.2, 0.1, 0.08);
    }
    cairo_mask(cr, pattern);
    cairo_pattern_destroy(pattern);
    cairo_restore(cr);
}

void install() { xoj::view::backgroundDecorator.store(&decorate, std::memory_order_release); }

}  // namespace xqt::paper
