/*
 * xournal-qt: a selection of several sticky notes, or of notes together with elements of the page
 * (qt/docs/sticky-notes.md, "Several notes at once").
 *
 * One per view. It holds whole notes (their layers) and elements of the page's own layers, all on one page, where they
 * are: nothing is taken out of its layer while it is selected (unlike upstream's EditSelection, which takes elements
 * out of their one layer and could not hold a note, a layer of its own). It is drawn over its page (an outline around
 * each note and a dashed box around everything), moved as one (a drag, also onto another page), copied, cut, pasted
 * and deleted as one, each change one undo step.
 *
 * What makes one: a rectangle or lasso started beside the notes that encloses a whole note (with the page's elements
 * it encloses), and Ctrl (or Shift) with a select tool on a note or an element while something is selected
 * (CanvasView::toggleSelected). A single note alone is the note's own selection (StickyNotes: its pill, its handle);
 * elements of one layer alone are an ordinary element selection (CanvasView::selectTogether decides).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/OverlayBase.h"
#include "model/PageRef.h"
#include "session/StickyNote.h"
#include "util/Rectangle.h"

namespace xqt {

class CanvasPage;
class CanvasView;

class MixedSelection final: public OverlayBase {
public:
    /// An element of the page and the layer it is in
    struct Item {
        Layer* layer = nullptr;
        Element* element = nullptr;
    };

    explicit MixedSelection(CanvasView& view);
    ~MixedSelection();

    bool active() const { return page != nullptr; }
    CanvasPage* selectedPage() const { return page; }
    /// The notes (bottom first, as on the page) and the elements
    const std::vector<Layer*>& notes() const { return noteLayers; }
    const std::vector<Item>& items() const { return elements; }
    bool holds(const Layer* note) const;
    bool holds(const Element* element) const;

    /// Select these (on this page; CanvasView::selectTogether ends any other selection first)
    void set(CanvasPage& page, std::vector<Layer*> notes, std::vector<Item> items);
    void clear();
    /// Around everything selected (page coordinates; nothing: no selection). Takes the document's lock.
    std::optional<xoj::util::Rectangle<double>> bounds() const;
    /// Where it is in the view (view coordinates; empty: none)
    QRectF viewBox() const;
    /// Whether a point of a page (page coordinates) is in it (a press there moves it)
    bool contains(const CanvasPage& page, double x, double y) const;

    // --- moving (page coordinates of its page, also beyond it) ---------------------------------------------------
    void startDrag(double x, double y);
    bool dragging() const { return isDragging; }
    void dragTo(double x, double y);
    /// One undo step; let go over another page, everything goes there (its layout kept)
    void endDrag();

    // --- the clipboard (sticky::GROUP_CLIPBOARD_MIME, and a picture of it all for other apps) ---------------------
    bool copy();
    /// Copy, then delete (one undo step)
    bool cut();
    /// Delete everything selected: one undo step (`what`: its name in the undo list)
    void deleteAll(const char* what = nullptr);
    static bool clipboardHas();
    /// The copied notes and elements onto a page of this view, in their layout: where they were when it fits, else
    /// moved inside the page, a little further while a note would lie exactly on one there. Selected; one undo step.
    bool paste(size_t page);

    /// Something changed (undo, a layer went, a page went): what is no longer where it was leaves the selection
    void validate();
    void pageGoing(const CanvasPage* page);

private:
    /// Under the document's lock
    std::optional<xoj::util::Rectangle<double>> boundsLocked() const;
    void repaint(const xoj::util::Rectangle<double>& area) const;
    bool dropOnOtherPage();
    /// A page's Markdown layer for Markdown boxes that come onto it: made (at the bottom) if the page has none, as a
    /// step of `steps`
    Layer* markdownLayerFor(const PageRef& page, sticky::UndoSteps& steps);

    CanvasView& view;
    CanvasPage* page = nullptr;
    PageRef ref;
    std::vector<Layer*> noteLayers;
    std::vector<Item> elements;

    bool isDragging = false;
    QPointF dragFrom;
    QPointF dragPointer;
    QPointF applied;  ///< how far everything is moved by the drag so far
    xoj::util::Rectangle<double> dragBounds;
    std::vector<sticky::Look> dragLooks;
};

}  // namespace xqt
