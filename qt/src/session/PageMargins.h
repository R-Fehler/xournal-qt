/*
 * xournal-qt: the margins of the page's own text (the page's Markdown text, the text mode, a chapter heading).
 *
 * 2 cm on A5 and bigger pages, as before. A smaller page (A6, A7 flashcards) has margins in proportion to its short
 * side, the proportion A5 has (2 cm of 148 mm, 13.5 %), at least 5 mm: A6 about 14 mm, A7 10 mm. A ruled page with a
 * margin line (upstream's "lined") starts its text after the line.
 *
 * The ruling of such a small page is drawn to the same scale (rulingScale, through upstream's xoj::view::ruledScale):
 * its margin line (1 inch on A5 and bigger, upstream's), and the space above and below the lines. A7: the line at
 * half an inch.
 *
 * The page's text is found by where its box is (at the top-left margin, md::pageBoxOf). A page smaller than A5 whose
 * text was written before the margins scaled has its box at 2 cm, and a small lined page's text written before its
 * line scaled is after the line at 1 inch: those count as well (pageBox).
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
/// The scale of the ruling of a page of this size (forSize / FULL): 1 on A5 and bigger pages.
double rulingScale(double width, double height);
/// Draw the ruling of small pages to scale (sets upstream's xoj::view::ruledScale; AppContext does it).
void installRuling();

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
