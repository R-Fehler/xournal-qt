/*
 * xournal-qt: the setsquare and the compass on the canvas.
 *
 * The tool itself (its geometry) and how it is drawn come from upstream (model/Setsquare, model/Compass and their
 * views). Moving, turning and sizing it is done here with Qt input instead of upstream's GTK handlers: two fingers
 * on it (or the right mouse button) carry it along, turn it and size it, while one finger scrolls the page as
 * usual. Pen and left button always draw: on the tool and just around it the line follows the nearest edge - the
 * three edges of the setsquare, the circle of the compass - so it works like a real ruler.
 *
 * The canvas shows it over its page as pictures of its own (GeometryToolPicture): moving, turning and sizing it move
 * those on the GPU, and the page itself is not drawn again for it.
 *
 * It lies on one page of the document. The canvas makes its pages anew now and then (pages inserted, deleted or
 * moved, a document read again, the view closed): it tells the layer before a page of its goes and after the pages
 * changed, and the tool is put back onto the page it lies on - or aside, when that page is gone.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <QPointF>

#include "model/GeometryTool.h"

class Compass;
class Setsquare;
class XojPage;

namespace xqt {

class CanvasPage;
class CanvasView;
class GeometryToolPicture;

class GeometryToolLayer {
public:
    explicit GeometryToolLayer(CanvasView& view);
    ~GeometryToolLayer();

    /// Put a setsquare or a compass on the current page (the same type again takes it away).
    void toggle(GeometryToolType type);
    /// Take it away altogether.
    void hide();
    /// It is out and to be seen: drawn on its page, guiding the pen, moved by fingers.
    bool visible() const { return tool != nullptr && !isMinimized && onPage != nullptr; }
    /// It is out, maybe only minimized (put aside for a moment: not shown, not guiding, but where it was).
    bool active() const { return tool != nullptr; }
    bool minimized() const { return tool != nullptr && isMinimized; }
    /// Put it aside / bring it back on the current page, where it lay and as it was turned.
    void setMinimized(bool minimized);
    std::optional<GeometryToolType> type() const;
    /// The canvas page it lies on (none if it is not out, or put aside).
    CanvasPage* page() const { return onPage; }
    /// Draws it for the canvas (none if it is not out).
    GeometryToolPicture* picture() const { return drawing.get(); }

    // The canvas pages change under it (see above)
    /// This canvas page goes (with the views on it).
    void pageGoing(const CanvasPage* page);
    /// All canvas pages go.
    void allPagesGoing();
    /// The canvas pages changed: onto its page again, or aside if the page is gone.
    void pagesChanged();

    /// Two fingers on it. `centre` is the middle between them (in the coordinates of the tool's page), `angle` and
    /// `distance` those of the line from the one to the other (screen). Every step is measured from where the
    /// fingers came down, so nothing adds up or drifts: the tool is carried with the fingers exactly, turns around
    /// them and grows or shrinks around them - the turning and sizing only once the fingers clearly turn or spread
    /// (TURN_SLOP, SIZE_SLOP), so that carrying it does not turn or size it a little. Held to a stroke, it slides
    /// along that and turns around its middle.
    void beginGesture(QPointF centre, double angle, double distance);
    void moveGesture(QPointF centre, double angle, double distance);
    void endGesture() { inGesture = false; }

    /// A point (page coordinates) is on the tool.
    bool contains(QPointF pagePoint) const;
    /// Move it by this much (page coordinates).
    void moveBy(QPointF delta);
    /// Turn it around its middle by this angle, and size it by this factor.
    void turnAndSize(double angle, double factor);
    /// Hold its middle (the 0 of the scale) on the ink stroke nearest to it: it jumps there, and from then on moving
    /// it slides it along that stroke, so distances can be measured along it. False when the page has no stroke.
    bool holdToStroke();
    void releaseStroke();
    bool heldToStroke() const { return !path.empty(); }

    /// Turning goes in steps of 15 degrees (the fingers still turn freely underneath, the tool follows in steps).
    void setAngleSteps(bool steps);
    bool angleSteps() const { return steps; }
    static constexpr double ANGLE_STEP = M_PI / 12;
    /// How it stands and how big it is (radians / centimetres); 0 when it is not out.
    double rotation() const;
    double height() const;
    /// Where its middle is (the 0 of the setsquare's scale, the centre of the compass), page coordinates.
    QPointF middle() const;
    /// A point of the page in the tool's own coordinates (centimetres from its middle), and back.
    QPointF pageToTool(QPointF pagePoint) const;
    QPointF toolToPage(QPointF toolPoint) const;
    /// Two fingers have to turn this far (radians) / spread this much (share) before the tool turns / grows, so that
    /// carrying it does not also turn or size it a little.
    static constexpr double TURN_SLOP = 3 * M_PI / 180;
    static constexpr double SIZE_SLOP = 0.06;
    /// How small and how big it can be made (centimetres)
    static constexpr double MIN_HEIGHT_CM = 2;
    static constexpr double MAX_HEIGHT_CM = 30;

    /// Short lines at the marks of the setsquare's scale, every `spacingCm` along its long edge and on the outer side
    /// of it (whole centimetres a little longer), as page coordinates. None for the compass or when it is not out.
    std::vector<std::pair<QPointF, QPointF>> marks(double spacingCm) const;
    static constexpr double MARK_CM = 0.25;
    static constexpr double WHOLE_MARK_CM = 0.4;

    /// Where a stroke point goes: along the edge of the setsquare, or on the circle of the compass. Returns the
    /// point itself when it is too far away from the tool to snap to it.
    QPointF snap(QPointF pagePoint) const;
    /// How far a point may be from the edge to be drawn along it, in centimetres (the tool measures in those).
    static constexpr double SNAP_CM = 0.5;

private:
    void place(CanvasPage& page);
    void remove();
    /// It moved, turned or changed its size: the canvas shows it anew.
    void changed();

    CanvasView& view;
    std::unique_ptr<GeometryTool> tool;
    std::unique_ptr<GeometryToolPicture> drawing;
    CanvasPage* onPage = nullptr;
    /// The page of the document it lies on (outlives the canvas pages)
    std::weak_ptr<XojPage> onDocumentPage;
    struct Gesture {
        QPointF centre;       ///< where the fingers' middle began
        QPointF middle;       ///< where the tool's middle was then
        double rotation = 0;  ///< how it was turned then (and freely, under the steps)
        double freeRotation = 0;
        double height = 0;
        double distance = 1;  ///< of the fingers then
        double lastAngle = 0;
        double twist = 0;  ///< how far the fingers turned since (added up in small steps: no jump at +-180 degrees)
    } gesture;
    bool inGesture = false;
    bool isMinimized = false;
    /// The stroke it slides along (a copy of its points, page coordinates), and the page it is on
    std::vector<QPointF> path;
    const XojPage* pathPage = nullptr;
    bool steps = false;
    /// Where the fingers have turned it to; with steps the tool shows the nearest step of it
    double freeRotation = 0;
};

}  // namespace xqt
