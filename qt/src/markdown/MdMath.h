/*
 * xournal-qt: math formulas of the Markdown text ($…$ and $$…$$), laid out by MicroTeX (vendored,
 * qt/3rdparty/microtex) and drawn with Cairo.
 *
 * MicroTeX lays a formula out with its own math font (Latin Modern Math, compiled into the program) and gives the
 * outlines of its glyphs: a formula is recorded once as filled and stroked paths, in em units, and drawn from that
 * at any size and in any color. It stays vector: in the PDF export a formula is paths, as sharp as the text.
 *
 * Qt-free and thread-safe: formulas are laid out under one lock (MicroTeX has global state) and are immutable
 * afterwards, so every render thread draws the same ones. The cache of formulas is owned here and has a limit.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cairo.h>

namespace xqt::md::math {

/// A formula laid out: its size and what to draw, in em (1 is the font size it is drawn at), from its baseline's
/// left end, y down.
struct Formula {
    bool ok = false;
    std::string error;  ///< why it could not be laid out (when not ok)
    double width = 0;
    double ascent = 0;   ///< above the baseline
    double descent = 0;  ///< below it

    /// A path, filled or stroked. Coordinates in em; `ops` has one entry per command, `points` 1 (move, line) or
    /// 3 (curve) points of 2 coordinates per command.
    struct Shape {
        enum Op : uint8_t { Move, Line, Curve, Close };
        std::vector<Op> ops;
        std::vector<float> points;
        bool stroke = false;
        float lineWidth = 0;  ///< stroke (em)
        uint8_t cap = 0;      ///< cairo_line_cap_t
        uint8_t join = 0;     ///< cairo_line_join_t
        std::vector<double> dash;  ///< stroke (em)
        /// ARGB; 0: the color of the text around it (the current source when drawn)
        uint32_t color = 0;
    };
    std::vector<Shape> shapes;

    /// Memory it takes (about), for the cache's limit.
    size_t bytes() const;
};

/// The formula of a TeX source (UTF-8, without the $ marks). `display`: a $$…$$ block (display style: bigger sums
/// and fractions). Never throws: a source MicroTeX cannot read gives a formula that is not ok, with the reason.
/// Cached (see cacheStats); any thread.
std::shared_ptr<const Formula> formula(std::string_view tex, bool display);

/// Draw a formula with its baseline's left end at (x, y), `size` points per em. Its parts without a color of
/// their own (\color) take the current source. `pathOnly`: add its outlines to the current path, without filling
/// (Pango's path mode, pango_cairo_layout_path); strokes are left out then.
void draw(cairo_t* cr, const Formula& f, double x, double y, double size, bool pathOnly = false);

struct CacheStats {
    size_t entries = 0;
    size_t bytes = 0;
    size_t hits = 0;
    size_t misses = 0;  ///< formulas laid out
};
CacheStats cacheStats();
/// Forget every formula (tests).
void clearCache();

/// The cache's limit (bytes of the formulas kept): the least recently used ones go beyond it.
constexpr size_t CACHE_LIMIT = 16 * 1024 * 1024;

}  // namespace xqt::md::math
