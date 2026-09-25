/*
 * xournal-qt: sticky notes (qt/docs/sticky-notes.md).
 *
 * A note is a layer of its own named "Sticky note" ("Sticky note (cover)" when it covers), whose first element is
 * its paper: a closed, filled rectangle stroke. The other elements of the layer are drawn on the note and clipped to
 * it. Xournal++ opens such a file as it is (a layer with an opaque rectangle and the ink on it); the grouping is
 * ours. This file: recognising notes, their look, making and changing them (with undo), drawing them (in place of
 * upstream's LayerView, see view/LayerView.h) and peeking under covering notes on the screen.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/Layer.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"
#include "util/Color.h"
#include "util/Rectangle.h"

class Stroke;
class XojPage;
class Document;

namespace xoj::view {
class Context;
}

namespace xqt::sticky {

/// Names of a note's layer (saved; fixed English, like "Markdown")
inline constexpr const char* LAYER_NAME = "Sticky note";
inline constexpr const char* COVER_LAYER_NAME = "Sticky note (cover)";
/// The paper's outline (points; drawn in the paper's color)
inline constexpr double PAPER_WIDTH = 0.5;
/// The smallest side of a note (points)
inline constexpr double MIN_SIDE = 24;
/// A new note (points)
inline constexpr double DEFAULT_WIDTH = 200;
inline constexpr double DEFAULT_HEIGHT = 140;

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
/// Whether a page has notes (`visibleOnly`: shown ones)
bool hasNotes(const XojPage& page, bool visibleOnly = false);
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
/// Draw a note (in page coordinates): the paper, then its content clipped to it. False if the layer is no note (the
/// drawer of upstream's LayerView, see view/LayerView.h).
bool draw(const Layer& layer, const xoj::view::Context& ctx);
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

}  // namespace xqt::sticky
