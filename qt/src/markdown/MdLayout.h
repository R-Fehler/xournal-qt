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
#include <optional>
#include <string>
#include <utility>
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
    /// A plain text (MdDocument.h, isPlain): paginate() starts each page's slice with the plain marker. (Drawing
    /// knows it from the source.)
    bool plain = false;
};

/// A link in the text of an item: its bytes in the Pango layout's text, and the index into Layout::links.
struct LinkSpan {
    int start = 0;
    int end = 0;
    int link = -1;
};

/// Where the text of an item came from: bytes [start, start + length) of its Pango layout's text are a run with this
/// source (NO_SOURCE: made up, e.g. a soft break) and formatting.
struct SourceMap {
    int start = 0;
    int length = 0;
    size_t source = NO_SOURCE;
    size_t sourceLength = 0;
    uint16_t flags = 0;
};

/// A formula ($…$, $$…$$) in the text of an item: bytes [start, start + length) of its Pango layout's text. A formula
/// is drawn in the text (MdMath) in place of one character, U+FFFC, so lines break around it and a tap on it is a
/// place in the text; one that cannot be laid out is its source, in red, and `error` says why.
struct MathSpan {
    int start = 0;
    int length = 0;
    std::string tex;      ///< its source (searched instead of the U+FFFC)
    bool display = false; ///< $$…$$: a centered line of its own
    std::string error;    ///< empty when it is drawn
    double inkX = 0;      ///< where it is drawn in its line's room (a display formula is centered in a whole line)
    double inkWidth = 0;
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
    std::vector<LinkSpan> links;                  ///< Text
    std::vector<SourceMap> sources;               ///< Text
    std::vector<MathSpan> maths;                  ///< Text: its formulas
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
        int item = -1;              ///< the item with the text of a paragraph, heading or code block
        std::vector<Extent> parts;  ///< the items of a list, the rows of a table (header included)
    };
    std::vector<Extent> blocks;
    double height = 0;  ///< of everything
    std::vector<std::string> links;  ///< link targets (Document::links)
    std::vector<bool> wikiLinks;     ///< per link: a [[wiki link]] (Document::wikiLinks)
    /// Editing (layout with `active`): the block being edited is laid out as its source, source[rawBegin, rawEnd),
    /// in this item (its text is exactly that source).
    int rawItem = -1;
    size_t rawBegin = NO_SOURCE;
    size_t rawEnd = NO_SOURCE;
    /// The check boxes of task lists: where they are drawn (box coordinates) and their mark in the source.
    struct CheckBox {
        double x = 0;
        double y = 0;
        double size = 0;
        size_t mark = NO_SOURCE;
        bool checked = false;
    };
    std::vector<CheckBox> checkBoxes;
};

/// A link at a point (box coordinates): its target and where it is (box coordinates).
struct LinkHit {
    std::string target;
    bool wiki = false;  ///< a [[wiki link]]: the target is a name to look for
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};
std::optional<LinkHit> linkAt(const Layout& layout, double x, double y);
/// Every link of a layout where it is drawn: a box per line it is on (box coordinates). For the links of a PDF.
std::vector<LinkHit> linkBoxes(const Layout& layout);
/// The check box at a point (box coordinates; a little around it counts), if any.
std::optional<Layout::CheckBox> checkBoxAt(const Layout& layout, double x, double y);
/// The text with a task's mark switched: "[ ]" <-> "[x]" (`mark`: the offset of the " " / "x").
std::string toggledTask(const std::string& source, size_t mark);

/// A rectangle in box coordinates.
struct Rect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};
/// Where a text is shown (case-insensitive; box coordinates): per match, a rectangle on each line it is drawn on
/// (from its first to its last character there, as upstream's Text::findText does for a line).
std::vector<Rect> findText(const Layout& layout, const std::string& search);
/// Where bytes [from, to) of a Text item's Pango text are drawn (box coordinates): a rectangle per line. A range that
/// is a display formula is where the formula is drawn (not its whole line).
std::vector<Rect> textRects(const Item& item, int from, int to);
/// The text of a Text item as it is searched: its Pango text, with the source of each formula drawn in it in place
/// of the formula's U+FFFC (the TeX stays searchable).
std::string searchText(const Item& item);
/// Bytes [from, to) of searchText() as bytes of the Pango text: a part of a formula's source is the whole formula.
std::pair<int, int> layoutRange(const Item& item, int from, int to);
/// The formula drawn at a point (box coordinates), if any: its span in its item (for the error of one that is shown
/// as its source) and where it is.
struct MathHit {
    MathSpan span;
    Rect rect;
};
std::optional<MathHit> mathAt(const Layout& layout, double x, double y);
/// Where a range of the source, [begin, end), is drawn (box coordinates): a rectangle per line of each text it is in.
/// Marks that are not drawn (a "**", a fence) have no place; text that stands for source it does not show (an
/// entity) is marked whole.
std::vector<Rect> sourceRects(const Layout& layout, size_t begin, size_t end);

/// Heading size factor (relative to the body text), level 1–6.
double headingScale(int level);

/// `source` / `active` (editing): the top-level block with the source offset `active` (or the blank lines after it)
/// is laid out as its source, its Markdown marks shown dimmed (as Typora and Obsidian's live preview do).
Layout layout(const Document& doc, const Style& style, std::string_view source = {}, size_t active = NO_SOURCE);

/// Draw at the current origin (the box's top left). The current source color is not kept.
void draw(cairo_t* cr, const Layout& layout);

}  // namespace xqt::md
