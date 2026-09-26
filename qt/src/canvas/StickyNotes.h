/*
 * xournal-qt: sticky notes on the canvas (qt/docs/sticky-notes.md; the format and drawing: session/StickyNote.h).
 *
 * One per view. It places notes, keeps the selected note (an outline and a handle drawn over its page), moves and
 * resizes it, changes its color and cover mode (all undoable), lets covering notes peek when tapped, and hides or
 * shows the notes of a page. Writing on a note is done by the page's usual tools with the note's layer selected for
 * the stroke (CanvasPage).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <unordered_set>

#include <QPointF>
#include <QRectF>

#include "model/Layer.h"
#include "model/OverlayBase.h"
#include "model/PageRef.h"
#include "session/StickyNote.h"

namespace xqt {

class CanvasPage;
class CanvasView;

class StickyNotes final: public OverlayBase {
public:
    explicit StickyNotes(CanvasView& view);
    ~StickyNotes();

    // --- placing ------------------------------------------------------------------------------------------------
    /// A new note in the middle of the visible part of the current page, selected (one undo step). The color: the
    /// last one chosen. Returns false when there is no page.
    bool insert();

    // --- the selected note ------------------------------------------------------------------------------------
    bool hasSelection() const { return selected != nullptr; }
    Layer* selectedLayer() const { return selected; }
    /// Its page (nullptr: none)
    CanvasPage* selectedPage() const { return selectedOn; }
    std::optional<sticky::Look> selectedLook() const;
    /// Where the selected note is in the view (view coordinates; empty: none), for its pill
    QRectF selectedViewBox() const;
    void select(CanvasPage& page, Layer* note);
    void clearSelection();
    void setColor(Color color);
    void setCover(bool cover);
    /// Delete the selected note (one undo step)
    void deleteSelected();

    // --- input (page coordinates of the page pressed) ---------------------------------------------------------
    /// A press: on the selected note's handle (any tool) it starts resizing; with a select tool on the selected
    /// note, or on any other note, it selects it and starts moving it. A press elsewhere ends the selection.
    /// Returns true when the press was the note's (nothing else happens); `deselected`: it only ended a selection.
    bool press(CanvasPage& page, double x, double y, bool selectTool, bool& deselected);
    /// A finger on the selected note (view coordinates): it moves it or its handle resizes it. False: not on it.
    bool pressTouch(CanvasPage& page, double x, double y);
    bool dragging() const { return drag != Drag::None; }
    /// The pointer moved while dragging (page coordinates of the selected note's page, also beyond it)
    void dragTo(double x, double y);
    /// The drag ended: one undo step if the note changed
    void endDrag();
    /// Whether (x, y) is on the handle of the selected note of this page (touch: a larger area)
    bool onHandle(const CanvasPage& page, double x, double y, bool touch = false) const;

    /// A tap on a covering note: it peeks, or covers again. Returns false if there is no covering note there.
    bool tapCover(CanvasPage& page, double x, double y);

    // --- the notes of a page ----------------------------------------------------------------------------------
    bool pageHasNotes(size_t page) const;
    /// Whether the notes of the page are hidden (all of them)
    bool notesHidden(size_t page) const;
    /// Hide or show every note of a page (a view state, like the layer panel's eye: not an undo step)
    void setNotesHidden(size_t page, bool hidden);

    /// The layers of a page changed (undo, a layer removed ...): a selection or a peek of a note that is gone ends.
    void layersChanged();
    /// The selected note's page goes
    void pageGoing(const CanvasPage* page);

    /// Screen pixels of the handle's radius
    static constexpr double HANDLE_RADIUS_PX = 9;

private:
    enum class Drag { None, Move, Resize };
    /// Its look now, under the document's lock (nothing: no note any more)
    std::optional<sticky::Look> lookOf(const Layer* layer) const;
    /// Apply a new look to the selected note (not an undo step: see endDrag / change)
    void show(const sticky::Look& from, const sticky::Look& to);
    /// A new look for the selected note as one undo step
    void change(const sticky::Look& to, const char* what);
    /// The outline and handle need drawing again
    void repaintSelection(const std::optional<sticky::Look>& look);
    void startDrag(Drag how, double x, double y);

    CanvasView& view;
    Layer* selected = nullptr;
    CanvasPage* selectedOn = nullptr;
    PageRef selectedPageRef;
    Drag drag = Drag::None;
    QPointF dragFrom;
    sticky::Look dragStart;
    sticky::Look dragNow;
    Color lastColor;
    /// Covering notes of this view that peek (their pictures on the screen)
    std::unordered_set<const Layer*> peeking;
};

}  // namespace xqt
