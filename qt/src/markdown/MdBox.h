/*
 * xournal-qt: Markdown boxes on pages.
 *
 * A box is an ordinary Xournal++ text element in a layer named "Markdown" (see src/core/model/MarkdownText.h). Its text
 * is the Markdown source, its font gives the family and size of the body text, its color the text color, its
 * wrap width the width of the box, and its position the top left of the box. Xournal++ shows the source as it is
 * (nothing is lost when the file goes back and forth); xournal-qt draws it formatted: drawText() is the renderer
 * that upstream's LayerView calls for these texts (installRenderer()).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include <cairo.h>

#include "model/MarkdownText.h"
#include "model/PageRef.h"
#include "util/Rectangle.h"

#include "MdLayout.h"

class Layer;
class Text;

namespace xqt::md {

/// Width of a box whose text has no wrap width (e.g. made in Xournal++).
constexpr double DEFAULT_WIDTH = 480;

/// The style a box's text element gives (font family and size, color, width).
Style styleOf(const Text& text);

/// The layout of a source (`active`: while editing, the source offset of the cursor, see layout()). Cached per thread
/// (the last boxes drawn there): valid until the next call on this thread.
const Layout& cachedLayout(const std::string& source, const Style& style, size_t active = NO_SOURCE);

/// Draw a box, in page coordinates (the renderer of upstream's TextView, see model/MarkdownText.h).
void drawText(const Text& text, cairo_t* cr);
/// How big a box is drawn (its bounding box, see model/MarkdownText.h).
xoj::markdown::Size drawnSize(const Text& text);

/// Markdown texts are drawn formatted, and are as big as they are drawn, from now on (idempotent).
void installRenderer();

/// The height of a box's content (points).
double contentHeight(const Text& text);
/// Where a box's content is on its page (page coordinates).
xoj::util::Rectangle<double> boxRect(const Text& text);
/// The link drawn at a point of the page, if any (page coordinates).
std::optional<LinkHit> linkAt(const Text& text, double x, double y);
/// The check box of a task drawn at a point of the page (page coordinates): its mark's offset in the box's text.
std::optional<size_t> checkBoxAt(const Text& text, double x, double y);
/// Where a text is shown in a box (case-insensitive; page coordinates): as the box is drawn, also while it is written
/// on the page (see setWritingCursor).
std::vector<Rect> findText(const Text& text, const std::string& search);
/// A box written on the page is drawn with the block of the cursor as its source (layout() with `active`): the
/// cursor's offset in the box's text, NO_SOURCE when it is not written any more. Any thread may read it.
void setWritingCursor(const Text& text, size_t active);
/// Whether a text is a box: in a Markdown layer.
bool isMarkdownLayer(const Layer& layer);

/// The page's Markdown layer (nullptr if none).
Layer* markdownLayer(const PageRef& page);
/// The box of a Markdown layer: its first text (nullptr if none).
Text* boxOf(const Layer& layer);
/// The page's Markdown text: the box whose top left is at (x, y), the page's margins (nullptr if none).
Text* pageBoxOf(const Layer& layer, double x, double y);
/// The box drawn at a point of the page (the topmost; nullptr if none).
Text* boxAt(const Layer& layer, double x, double y);

/// The size of new Markdown text, from the size of the text font: smaller, as Markdown has headings.
double defaultFontSize(double textFontSize);

}  // namespace xqt::md
