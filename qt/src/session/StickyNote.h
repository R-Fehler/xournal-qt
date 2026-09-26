/*
 * xournal-qt: sticky notes (qt/docs/sticky-notes.md).
 *
 * A note is a layer of its own named "Sticky note" ("Sticky note (cover)" when it covers), whose first element is
 * its paper: a closed, filled rectangle stroke. The other elements of the layer are drawn on the note and clipped to
 * it. Xournal++ opens such a file as it is (a layer with an opaque rectangle and the ink on it); the grouping is
 * ours. This file: recognising notes, their look, making and changing them (with undo), drawing them (in place of
 * upstream's LayerView, see view/LayerView.h), peeking under covering notes on the screen, and a note on the
 * clipboard (its format, where it is pasted).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/Layer.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"
#include "util/Color.h"
#include "util/Point.h"
#include "util/Rectangle.h"

class LayerController;
class Stroke;
typedef struct _cairo cairo_t;
class Text;
class XojPage;
class Document;

namespace xoj::view {
class Context;
}

namespace xqt::sticky {

/// Names of a note's layer (saved; fixed English, like "Markdown")
inline constexpr const char* LAYER_NAME = "Sticky note";
inline constexpr const char* COVER_LAYER_NAME = "Sticky note (cover)";
/// The paper's outline as saved (points, in the paper's color: what upstream Xournal++ draws)
inline constexpr double PAPER_WIDTH = 0.5;
/// The paper's edge as we draw it (points): a darker shade of the paper's color (edgeColor)
inline constexpr double EDGE_WIDTH = 0.8;
/// The edge's color: the paper's color times this
inline constexpr double EDGE_SHADE = 0.78;
/// How far a note's picture (its edge and its shadow) reaches beyond its rectangle (points)
inline constexpr double DRAWN_MARGIN = 4;
/// The smallest side of a note (points)
inline constexpr double MIN_SIDE = 24;
/// A new note (points)
inline constexpr double DEFAULT_WIDTH = 200;
inline constexpr double DEFAULT_HEIGHT = 140;

/// Between the note's edge and its Markdown text (points, about 3.5 mm)
inline constexpr double TEXT_PADDING = 10;
/// The narrowest the note's text is laid out (points; a note is at least MIN_SIDE wide)
inline constexpr double MIN_TEXT_WIDTH = 8;

/// The pastel colors offered (the first is a new note's)
const std::vector<Color>& presetColors();

/// What a note looks like: where it is, its color, whether it covers.
struct Look {
    xoj::util::Rectangle<double> rect;
    Color color{};
    bool cover = false;
    bool operator==(const Look& o) const {
        return rect.x == o.rect.x && rect.y == o.rect.y && rect.width == o.rect.width &&
               rect.height == o.rect.height && color == o.color && cover == o.cover;
    }
    bool operator!=(const Look& o) const { return !(*this == o); }
};

/// The color of the edge of a note of this color
Color edgeColor(Color paper);
/// Where a note of this rectangle is drawn: the rectangle and its shadow (what is drawn again when it changes)
xoj::util::Rectangle<double> drawnRect(const xoj::util::Rectangle<double>& rect);

/// Whether a layer name is a note's
bool isNoteName(const std::string& name);
/// The paper of a note: its layer's first element if that is a filled stroke (nullptr: the layer is no note)
const Stroke* paperOf(const Layer& layer);
bool isNote(const Layer& layer);
/// The look of a note (nothing: the layer is no note)
std::optional<Look> lookOf(const Layer& layer);
/// A new note's layer (not on a page yet): its paper, nothing on it.
Layer* makeNote(const Look& look);
/// Give a note another look (the caller holds the document's lock): the paper goes to the new rectangle and color,
/// the layer's name follows `cover`, and the content moves by the change of the top left (a resize keeps it in place).
void applyLook(Layer& layer, const Look& from, const Look& to);

/// The topmost visible note of a page at a point (page coordinates; nullptr: none). Call under the document's lock.
Layer* noteAt(const XojPage& page, double x, double y);
/// The note that takes what is started at a point (the topmost visible note there, if it does not cover; nullptr:
/// the page takes it). Call under the document's lock.
Layer* openNoteAt(const XojPage& page, double x, double y);

// --- the note's Markdown text (qt/docs/sticky-notes.md, "Notes as containers") ------------------------------------
/// Where a note's Markdown text begins (its top left) and how wide it is laid out
xoj::util::Point<double> textOrigin(const Look& look);
double textWidth(const Look& look);
/// Whether a text of a layer is its note's Markdown text: the layer is a note, the text has a wrap width and lies
/// at the note's text origin. (The frontend's xoj::markdown::classifier: such a text is a Markdown text.)
bool isNoteText(const Layer& layer, const Text& text);
/// The note's Markdown text (nullptr: none yet). Call under the document's lock.
Text* textOf(const Layer& layer);
/// Whether a page has notes (`visibleOnly`: shown ones)
bool hasNotes(const XojPage& page, bool visibleOnly = false);
/// While a note's layer comes onto a page or leaves it (on this thread): only where the note is drawn changes, so a
/// view draws that part of the page again, not the whole page (CanvasView::layerChanged). Set around the layer
/// controller's insertLayer / removeLayer (and the undo of them); nothing if the layer is no note.
class NoteLayerChange {
public:
    explicit NoteLayerChange(const Layer& layer);
    ~NoteLayerChange();
    NoteLayerChange(const NoteLayerChange&) = delete;
    NoteLayerChange& operator=(const NoteLayerChange&) = delete;

private:
    std::optional<xoj::util::Rectangle<double>> before;
};
/// Where the note changing now (NoteLayerChange) is drawn; nothing if no note layer is changing on this thread
std::optional<xoj::util::Rectangle<double>> changingNoteArea();

/// While a selection of a note's elements lives, the note is its page's selected layer (they are dropped there):
/// leaveNoteLayer leaves it alone. Held and let go by the canvas (qt/docs/sticky-notes.md, "Selecting in a note").
void holdLayer(const Layer* layer, bool hold);

/// The layer id (1-based, as upstream counts) of a note layer, 0 if it is not on the page
Layer::Index layerIdOf(const XojPage& page, const Layer* layer);

/// The rule of the selected layer: never a note, except while something is done on one (qt/docs/sticky-notes.md).
/// If the page's selected layer is a note, the topmost layer that is not one is selected (a new empty layer below
/// the notes if there is none). Takes the document's lock itself. True when it changed the selection.
bool leaveNoteLayer(Document& doc, const PageRef& page);

// --- peeking (the screen only) --------------------------------------------------------------------------------
/// A covering note peeks (drawn see-through on the screen) or covers again. Any thread may ask.
void setPeeking(const Layer* layer, bool peeking);
bool isPeeking(const Layer* layer);

// --- drawing ---------------------------------------------------------------------------------------------------
/// Draw a note (in page coordinates): a soft shadow, the paper, its content clipped to it, and the paper's darker
/// edge (a look of our renderer: nothing of it is in the file). False if the layer is no note (the drawer of
/// upstream's LayerView, see view/LayerView.h).
bool draw(const Layer& layer, const xoj::view::Context& ctx);
/// The small triangle at a note's bottom right that says its Markdown text goes on below its bottom (the screen only;
/// drawn by `draw`, and by the editor while the text is written)
void drawMoreBelow(cairo_t* cr, const xoj::util::Rectangle<double>& rect, Color paper);
/// What is drawn around a note's paper: `Full` (the edge and the shadow) in the app; the others for measuring what
/// they cost (the benchmark in StickyNoteTest). `Flat` is the paper as upstream draws it. Any thread may ask.
enum class Finish { Flat, Edge, Full };
void setFinish(Finish finish);
/// Notes are drawn as notes from now on, everywhere a page is drawn (idempotent)
void installDrawer();

// --- undo ------------------------------------------------------------------------------------------------------
/// A note moved, resized, colored or switched to cover (or back): its look before and after.
class NoteUndoAction final: public UndoAction {
public:
    NoteUndoAction(const PageRef& page, Layer* layer, const Look& before, const Look& after, std::string text);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }

private:
    Layer* layer;
    Look before;
    Look after;
    std::string text;
};

/// Change a note's look now (applyLook under the document's lock) and have both places drawn again (the page's
/// listeners hear of the area). Not an undo step by itself: see NoteUndoAction.
void changeLook(Document& doc, const PageRef& page, Layer& layer, const Look& from, const Look& to);

/// A note went to another page (dragged there): it leaves one page and lies on top of the other with another look.
class NotePageUndoAction final: public UndoAction {
public:
    struct Place {
        PageRef page;
        Layer::Index position;  ///< 0-based, as LayerController::insertLayer takes it
        Look look;
    };
    NotePageUndoAction(LayerController* layers, Layer* layer, Place from, Place to, std::string text);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }
    /// Move the note now (no undo step by itself)
    static void move(LayerController* layers, Document& doc, Layer* layer, const Place& from, const Place& to);

private:
    LayerController* layers;
    Layer* layer;
    Place from;
    Place to;
    std::string text;
};

/// Elements moved into a note, out of it onto the page, or from one note to another on the same page, by a drag of a
/// selection: the move and the change of layer as one step. Undone, they are back at their places in the layer they
/// came from (their positions there, `indices`; Element::InvalidIndex: on top).
class ContentMoveUndoAction final: public UndoAction {
public:
    ContentMoveUndoAction(const PageRef& page, std::vector<Element*> elements, Layer* from,
                          std::vector<Element::Index> indices, Layer* to, double dx, double dy, std::string text);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }

private:
    void repaint(double dx, double dy) const;
    std::vector<Element*> elements;
    Layer* from;
    std::vector<Element::Index> indices;
    Layer* to;
    double dx;
    double dy;
    std::string text;
};

// --- the clipboard -------------------------------------------------------------------------------------------------
/// The clipboard's format of a whole note (the app's own: its layer's name and every element, the paper first, each
/// in upstream's element serialization, in a stream of its own). Upstream Xournal++ does not know it; the note is
/// saved like any other.
inline constexpr const char* CLIPBOARD_MIME = "application/x-xournal-qt-sticky-note";
/// A note for the clipboard (the caller holds the document's lock); empty if the layer is no note
std::string serialize(const Layer& layer);
/// A note from the clipboard (not on a page); nullptr if the data is not a note
std::unique_ptr<Layer> deserialize(const char* data, size_t size);
/// Where a pasted note goes on a page of this size: the same place when it fits, else moved inside the page (made
/// smaller only if it is larger than the page); moved on a little while it would lie exactly on one of `taken`.
xoj::util::Rectangle<double> pastePlace(xoj::util::Rectangle<double> rect, double pageWidth, double pageHeight,
                                        const std::vector<xoj::util::Rectangle<double>>& taken);

/// Where a copied selection of several notes and elements (qt/docs/sticky-notes.md, "Several notes at once") goes on
/// a page of this size: how far it is moved as a whole (its layout and size kept). The same place when it fits, else
/// moved inside the page (its top left at the page's when it is larger); moved on a little while one of its `notes`
/// would lie exactly on one of `taken`.
xoj::util::Point<double> groupPastePlace(const xoj::util::Rectangle<double>& bounds,
                                         const std::vector<xoj::util::Rectangle<double>>& notes, double pageWidth,
                                         double pageHeight, const std::vector<xoj::util::Rectangle<double>>& taken);

// --- several notes and elements together -----------------------------------------------------------------------
/// The clipboard's format of a selection of whole notes together with elements of the page (the app's own): where it
/// all was (its bounds), each note as CLIPBOARD_MIME has it, each element in a stream of its own (with whether it was
/// a Markdown box of the page).
inline constexpr const char* GROUP_CLIPBOARD_MIME = "application/x-xournal-qt-selection";
/// Notes and elements for the clipboard (the caller holds the document's lock)
std::string serializeGroup(const xoj::util::Rectangle<double>& bounds, const std::vector<const Layer*>& notes,
                           const std::vector<const Element*>& elements);
/// A selection from the clipboard (on no page)
struct Group {
    xoj::util::Rectangle<double> bounds;
    std::vector<std::unique_ptr<Layer>> notes;
    std::vector<ElementPtr> elements;
    std::vector<bool> markdown;  ///< per element: it was a Markdown box of its page (its page's Markdown layer's)
};
std::optional<Group> deserializeGroup(const char* data, size_t size);

/// Several undo actions as one step, named: undone last first (they may depend on each other, as layers taken from a
/// page and put on another do), redone in their order.
class UndoSteps final: public UndoAction {
public:
    explicit UndoSteps(std::string text);
    void add(std::unique_ptr<UndoAction> step);
    bool empty() const { return steps.empty(); }
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::vector<PageRef> getPages() override;
    std::string getText() override { return text; }

private:
    std::vector<std::unique_ptr<UndoAction>> steps;
    std::vector<PageRef> pages;
    std::string text;
};

}  // namespace xqt::sticky
