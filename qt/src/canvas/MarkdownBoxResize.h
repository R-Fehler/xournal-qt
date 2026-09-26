/*
 * xournal-qt: the width of a Markdown text box (qt/docs/markdown-boxes.md, "Size").
 *
 * A text box (not the page's own Markdown text, which goes from margin to margin) has a handle in the middle of its
 * right edge: while it is written on the page (MarkdownEditor), and while it is selected alone (the selection's
 * right knob, marked with a double arrow). Dragging it with the pen, a finger or the mouse sets the box's wrap width
 * (upstream's `wrap` attribute of the text, so the file keeps it and Xournal++ reads it): the text flows anew while
 * it is dragged, and the box is as high as its text. At least 2 cm wide, at most to the page's right edge.
 *
 * Undo: written on the page, the drag is an undo step of the text being written (and part of the edit's one undo
 * step when it is done); selected, the drag is one undo step. A selected box is taken out of the selection while it
 * is dragged (drawn by this class, over its page) and selected again at the end: the selection's other knobs
 * (moving, scaling, turning, deleting) stay as they are.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <QPointF>
#include <QRectF>
#include <cairo.h>

#include "model/OverlayBase.h"
#include "model/PageRef.h"
#include "util/Color.h"

class EditSelection;
class Text;

namespace xqt {

class CanvasPage;
class CanvasView;

class MarkdownBoxResize final: public OverlayBase {
public:
    explicit MarkdownBoxResize(CanvasView& view);
    ~MarkdownBoxResize() override;

    /// The narrowest a box gets (2 cm, in points)
    static constexpr double MIN_WIDTH = 2 / 2.54 * 72;
    /// The handle's radius (screen pixels; the selection's knobs)
    static constexpr double HANDLE_RADIUS_PX = 12;

    /// Whether a place of the view is on the handle of the box written or selected (a finger: a larger area)
    bool onHandle(QPointF viewPos, bool touch = false) const;
    /// A press (view coordinates): on the handle it starts a drag (true); elsewhere nothing happens (false)
    bool press(QPointF viewPos, bool touch = false);
    bool dragging() const { return drag.has_value(); }
    /// The pointer moved while dragging (view coordinates, also beyond the page): the text flows anew
    void dragTo(QPointF viewPos);
    /// The drag ended: an undo step if the width changed (a selected box is selected again)
    void endDrag();
    /// The pages of the view go (rebuilt): a drag ends where it is
    void pagesGoing();

    /// The selected Markdown text box that has a handle: one text box alone, not turned, not the page's text
    /// (nullptr: none)
    const Text* selectedBox() const;
    /// Mark the handle over the selection's right knob (the selection's picture, as EditSelection::paint draws it:
    /// page pixels at `zoom`)
    void paintOverSelection(cairo_t* cr, double zoom) const;
    /// Draw the handle: a white knob with a ring in `color` and a double arrow. `px`: a screen pixel in cr's units.
    static void drawHandle(cairo_t* cr, double x, double y, double px, Color color);
    /// How far the handle reaches beyond the box's edge (page units at this zoom), for repainting
    static double handleReach(double zoom) { return (2 * HANDLE_RADIUS_PX + 4) / zoom; }
    /// Whether a text is the page's own Markdown text: its top left at the page's margins
    static bool isPageText(const PageRef& page, const Text& text);

    /// Draws the box while it is dragged (the renderer leaves it out: Text::setInEditing)
    void paint(cairo_t* cr, const CanvasPage* on) const;

private:
    struct Drag {
        bool selected = false;  ///< a selected box (else: the box being written)
        CanvasPage* page = nullptr;
        PageRef pageRef;
        Text* box = nullptr;  ///< a selected box, in its layer while it is dragged
        double scale = 1;     ///< page points per point of its width (a box scaled with the selection)
        double left = 0;      ///< its left edge on the page
        double pressX = 0;    ///< where the drag began (page coordinates)
        double startWidth = 0;
        double width = 0;
        QRectF area;  ///< where it was drawn last (page coordinates)
    };
    /// Where a place of the view is on a page (page coordinates)
    QPointF onPage(const CanvasPage& page, QPointF viewPos) const;
    /// The selected box's area (page coordinates) and the part a repaint covers
    QRectF dragArea() const;
    void repaint(const QRectF& area) const;
    /// Select the box again after a drag (as a tap with the object select tool does)
    void selectAgain(const Drag& d);

    CanvasView& view;
    std::optional<Drag> drag;
};

}  // namespace xqt
