/*
 * xournal-qt: a parsed Markdown text laid out for a box of a given width, and drawn with Cairo.
 *
 * Text is laid out with Pango (the text stack of upstream's text elements), independently of the zoom: no hinted
 * metrics, no rounded glyph positions. Lines therefore break at the same places on the canvas at any zoom, in the
 * thumbnails and in the PDF export, where the text stays text (vector, selectable).
 *
 * A Layout holds Pango layouts: use it on one thread only (Pango objects are not thread-safe).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <cairo.h>
#include <pango/pango.h>

#include "util/Color.h"
#include "util/raii/GObjectSPtr.h"

#include "MdDocument.h"

namespace xqt::md {

struct Style {
    std::string family = "Sans";
    std::string monoFamily = "Monospace";
    double size = 12;          ///< body text (points)
    Color color = Color(0, 0, 0);
    double width = 480;        ///< width of the box (points)
};

/// One thing to draw, in box coordinates (top left of the box at 0, 0).
struct Item {
    enum class Kind { Text, Fill, Line };
    Kind kind = Kind::Text;
    double x = 0;
    double y = 0;
    double width = 0;   ///< Line: the line goes from (x, y) to (x + width, y + height)
    double height = 0;
    xoj::util::GObjectSPtr<PangoLayout> layout;  ///< Text
    Color color;
    double lineWidth = 1;
    size_t block = 0;   ///< the top-level block it belongs to
};

struct Layout {
    std::vector<Item> items;
    /// Per top-level block of the document: where it is drawn (top of its first to bottom of its last item).
    struct Extent {
        double top = 0;
        double bottom = 0;
    };
    std::vector<Extent> blocks;
    double height = 0;  ///< of everything
};

/// Heading size factor (relative to the body text), level 1–6.
double headingScale(int level);

Layout layout(const Document& doc, const Style& style);

/// Draw at the current origin (the box's top left). The current source color is not kept.
void draw(cairo_t* cr, const Layout& layout);

}  // namespace xqt::md
