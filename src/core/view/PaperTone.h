/*
 * xournal-qt: the paper of the page being drawn on this thread, for the highlighter (qt/docs/dark-pages.md).
 *
 * Upstream draws a highlighter by multiplying it with what is under it (CAIRO_OPERATOR_MULTIPLY, opacity 0.47): on
 * white paper a translucent marker, on dark paper (black, dark grey) nothing at all. On dark paper it lightens instead
 * (CAIRO_OPERATOR_SCREEN, the same thing turned around: the paper takes the marker's color, light ink stays light),
 * at the opacity the color palettes give dark paper (0.8). The file does not change.
 *
 * Who draws a page says which paper it has (PaperToneScope: DocumentView::drawPage, the frontend's canvas); without a
 * scope the paper is light, as upstream.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cmath>

#include <cairo.h>

#include "util/Color.h"

namespace xoj::view {

namespace paper_tone {
inline thread_local bool dark = false;
}

/// Paper this dark wants light ink: its relative luminance (WCAG) is below about 0.18, where white text has more
/// contrast on it than black text (the fork's ColorPalettes::isDarkPaper)
inline bool isDarkPaper(Color c) {
    const auto lin = [](uint8_t v) {
        const double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.red) + 0.7152 * lin(c.green) + 0.0722 * lin(c.blue) < 0.18;
}

/// While it lives, this thread draws on paper of this color (a page of a PDF or an image: light)
class PaperToneScope {
public:
    explicit PaperToneScope(bool darkPaper): before(paper_tone::dark) { paper_tone::dark = darkPaper; }
    ~PaperToneScope() { paper_tone::dark = before; }
    PaperToneScope(const PaperToneScope&) = delete;
    PaperToneScope& operator=(const PaperToneScope&) = delete;

private:
    bool before;
};

/// How a highlighter is put onto the paper of this thread's page
inline cairo_operator_t highlighterOperator() {
    return paper_tone::dark ? CAIRO_OPERATOR_SCREEN : CAIRO_OPERATOR_MULTIPLY;
}
/// The opacity of a highlighter without a fill of its own (`light`: upstream's, on light paper)
inline double highlighterOpacity(double light) { return paper_tone::dark ? 0.8 : light; }

}  // namespace xoj::view
