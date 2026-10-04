/*
 * xournal-qt: the curtain and the spotlight, for teaching and presenting (qt/docs/curtain.md).
 *
 * The curtain is a black sheet over part of the page, like a sheet of paper on an overhead projector: the audience
 * does not see what lies under it. It is put out over the lower half of the part of the page in view. The spotlight is
 * its inverse: everything of the canvas is black (the other pages and the space around them too) but a rectangle with
 * rounded corners, put out in the middle of the view. Both are moved, turned and sized like the setsquare (the
 * spotlight by its hole): two fingers on it carry, turn and size it, and while its handles are shown (a tap on the black shows
 * them, Esc or a press beside it hides them) a drag on it moves it, its corners and edges size it and the knob above
 * it turns it.
 *
 * It belongs to the view (a tab), not to the document: it is drawn only on the screen, by the canvas item, as a node of
 * its own over the pages (moving it changes only that node), and never saved, printed, exported or shown in the
 * thumbnails and previews. Nothing is drawn on it either: input that starts on the black writes nothing, erases
 * nothing and follows no link (only the handles act; a tap shows them).
 *
 * It lies on a page (page coordinates, so it scrolls and zooms with what it covers) and goes along to the page the view
 * is at, at the same place of that page, when the current page changes: flipping through the pages of a presentation
 * keeps the next one covered just as far.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QPointF>
#include <QSizeF>

class XojPage;

namespace xqt {

class CanvasPage;
class CanvasView;

class CurtainLayer {
public:
    enum class Shape { Curtain, Spotlight };
    /// What a press is on. `Body`: the black itself (moves it while the handles are shown).
    enum class Handle { None, Body, Rotate, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

    explicit CurtainLayer(CanvasView& view);
    ~CurtainLayer();

    /// Put it out (the same shape again takes it away).
    void toggle(Shape shape);
    void show(Shape shape);
    void hide();
    bool active() const { return shown.has_value(); }
    /// Out and on a page that is shown
    bool visible() const { return shown.has_value() && onPage != nullptr; }
    std::optional<Shape> shape() const { return shown; }
    /// The canvas page it lies on
    CanvasPage* page() const { return onPage; }

    /// Where it is: its middle (page points), its size (points) and how it is turned (radians, clockwise). For the
    /// spotlight that is its hole.
    QPointF centre() const { return middle; }
    QSizeF size() const { return extent; }
    double rotation() const { return turn; }
    /// Put it there (tests; the page stays)
    void place(QPointF centre, QSizeF size, double rotation);

    /// Its handles are shown: a drag on it moves it, its corners and edges size it, the knob turns it
    bool handlesShown() const { return visible() && withHandles; }
    void setHandlesShown(bool shown);

    // In the coordinates of the view (the canvas item)
    /// A point of its own (points from its middle, unturned) in the view, and back
    QPointF toView(QPointF own) const;
    QPointF fromView(QPointF viewPos) const;
    /// The black is at this place
    bool covers(QPointF viewPos) const;
    /// The handles (where they are in the view); none while they are hidden
    std::vector<std::pair<Handle, QPointF>> handles() const;
    /// What a press at this place takes (None: it is not on the curtain). `reach`: how far from a handle it may be.
    Handle handleAt(QPointF viewPos, double reach) const;

    /// A drag of a handle (or of the black, `Body`), from this place
    void beginDrag(Handle handle, QPointF viewPos);
    void dragTo(QPointF viewPos);
    void endDrag() { drag.handle = Handle::None; }
    bool dragging() const { return drag.handle != Handle::None; }

    /// Two fingers on it (view coordinates; as GeometryToolLayer: measured from where they came down, turning and
    /// sizing only once the fingers clearly turn or spread)
    void beginGesture(QPointF centre, double angle, double distance);
    void moveGesture(QPointF centre, double angle, double distance);
    void endGesture() { inGesture = false; }

    // The canvas pages change under it
    void pageGoing(const CanvasPage* page);
    void allPagesGoing();
    void pagesChanged();
    /// The view is at another page: it goes along, to the same place of that page
    void currentPageChanged();

    /// How small it can be made (points a side)
    static constexpr double MIN_SIDE = 24;
    /// The corners of the spotlight's hole are rounded with this radius (points; less on a small hole)
    static constexpr double RADIUS = 12;
    double cornerRadius() const { return std::min({RADIUS, extent.width() / 2, extent.height() / 2}); }
    /// The knob that turns it: this far above its top edge (view pixels)
    static constexpr double KNOB_DISTANCE = 36;
    /// How far a press may be from a handle: pen and mouse, finger (view pixels)
    static constexpr double REACH = 14;
    static constexpr double FINGER_REACH = 24;
    static constexpr double TURN_SLOP = 3 * M_PI / 180;
    static constexpr double SIZE_SLOP = 0.06;
    /// Turning it with the knob, it stays straight (a multiple of 90 degrees) within this much
    static constexpr double STRAIGHT_SNAP = 4 * M_PI / 180;

private:
    void put(CanvasPage& page);
    /// Where it goes when it is put out on this page: the curtain over the lower half of the part of the page in view,
    /// the spotlight's hole in its middle
    void placeDefault(CanvasPage& page);
    /// The page of the view (none: no pages)
    CanvasPage* currentPage() const;
    double zoom() const;
    QPointF pageOrigin() const;
    void changed();
    /// It came or went, or its handles did: the window's controls follow
    void stateChanged();

    CanvasView& view;
    std::optional<Shape> shown;
    CanvasPage* onPage = nullptr;
    std::weak_ptr<XojPage> onDocumentPage;
    QPointF middle;
    QSizeF extent;
    double turn = 0;
    bool withHandles = false;
    struct Drag {
        Handle handle = Handle::None;
        QPointF from;        ///< where the pointer was (page coordinates)
        QPointF middle;      ///< the curtain then
        QSizeF extent;
        double turn = 0;
        double angle = 0;    ///< of the pointer around its middle
    } drag;
    struct Gesture {
        QPointF centre;  ///< page coordinates
        QPointF middle;
        QSizeF extent;
        double turn = 0;
        double distance = 1;
        double lastAngle = 0;
        double twist = 0;
    } gesture;
    bool inGesture = false;
};

}  // namespace xqt
