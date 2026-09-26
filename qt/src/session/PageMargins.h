/*
 * xournal-qt: the margins of the page's own text (the page's Markdown text, the text mode, a chapter heading).
 *
 * 2 cm on A5 and bigger pages, as before. A smaller page (A6, A7 flashcards) has margins in proportion to its short
 * side, the proportion A5 has (2 cm of 148 mm, 13.5 %), at least 5 mm: A6 about 14 mm, A7 10 mm. A ruled page with a
 * margin line (upstream's "lined") starts its text after the line, as upstream's line is at a fixed distance.
 *
 * The page's text is found by where its box is (at the top-left margin, md::pageBoxOf). A page smaller than A5 whose
 * text was written before the margins scaled has its box at 2 cm: that one counts as well (pageBox).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "model/PageRef.h"

class Layer;
class Text;

namespace xqt::PageMargins {

/// 2 cm (as the page's text had it: 56.7 pt)
inline constexpr double FULL = 56.7;
/// 5 mm
inline constexpr double MIN = 5.0 * 72.0 / 25.4;
/// A5's short side (148 mm): up to here the margin is FULL
inline constexpr double A5_SHORT_SIDE = 148.0 * 72.0 / 25.4;

/// The margin of a page of this size (points), the same on all sides.
double forSize(double width, double height);

struct Margins {
    double left = FULL, top = FULL, right = FULL, bottom = FULL;
};
/// The margins of the page's text on this page (forSize, and after the margin line of a lined page). Read under the
/// document lock.
Margins of(const PageRef& page);
/// The margins as they were before they scaled (2 cm, after a margin line): where older text of a small page is.
Margins unscaled(const PageRef& page);

/// The page's own text box in its Markdown layer: at the top-left margin, or at the unscaled one (older text on a
/// small page). nullptr: none.
Text* pageBox(const Layer& layer, const PageRef& page);

}  // namespace xqt::PageMargins
