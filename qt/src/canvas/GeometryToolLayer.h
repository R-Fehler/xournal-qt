/*
 * xournal-qt: the setsquare and the compass on the canvas.
 *
 * The tool itself (its geometry) and how it is drawn come from upstream (model/Setsquare, model/Compass and their
 * views). Moving, turning and sizing it is done here with Qt input instead of upstream's GTK handlers: two fingers
 * on it (or the right mouse button) carry it along, turn it and size it, while one finger scrolls the page as
 * usual. Pen and left button always draw: on the tool and just around it the line follows the nearest edge - the
 * three edges of the setsquare, the circle of the compass - so it works like a real ruler.
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

class GeometryToolLayer {
public:
    explicit GeometryToolLayer(CanvasView& view);
    ~GeometryToolLayer();

    /// Put a setsquare or a compass on the current page (the same type again takes it away).
    void toggle(GeometryToolType type);
    /// Take it away altogether.
    void hide();
    /// It is out and to be seen: drawn on its page, guiding the pen, moved by fingers.
    bool visible() const { return tool != nullptr && !isMinimized; }
    /// It is out, maybe only minimized (put aside for a moment: not shown, not guiding, but where it was).
    bool active() const { return tool != nullptr; }
    bool minimized() const { return tool != nullptr && isMinimized; }
    /// Put it aside / bring it back on the current page, where it lay and as it was turned.
    void setMinimized(bool minimized);
    std::optional<GeometryToolType> type() const;
    /// The page it lies on (none if it is not out).
    CanvasPage* page() const { return onPage; }

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

    CanvasView& view;
    std::unique_ptr<GeometryTool> tool;
    CanvasPage* onPage = nullptr;
    bool isMinimized = false;
    /// The stroke it slides along (a copy of its points, page coordinates), and the page it is on
    std::vector<QPointF> path;
    const XojPage* pathPage = nullptr;
    bool steps = false;
    /// Where the fingers have turned it to; with steps the tool shows the nearest step of it
    double freeRotation = 0;
};

}  // namespace xqt
