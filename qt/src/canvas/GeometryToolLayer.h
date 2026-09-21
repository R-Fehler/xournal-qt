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

#include <memory>
#include <optional>

#include <QPointF>

#include "model/GeometryTool.h"

class Compass;
class Setsquare;

namespace xqt {

class CanvasPage;
class CanvasView;

class GeometryToolLayer {
public:
    explicit GeometryToolLayer(CanvasView& view);
    ~GeometryToolLayer();

    /// Put a setsquare or a compass on the current page (the same type again takes it away).
    void toggle(GeometryToolType type);
    void hide();
    bool visible() const { return tool != nullptr; }
    std::optional<GeometryToolType> type() const;
    /// The page it lies on (none if it is not out).
    CanvasPage* page() const { return onPage; }

    /// A point (page coordinates) is on the tool.
    bool contains(QPointF pagePoint) const;
    /// Move it by this much (page coordinates).
    void moveBy(QPointF delta);
    /// Turn it around its middle by this angle, and size it by this factor.
    void turnAndSize(double angle, double factor);
    /// How it stands and how big it is (radians / centimetres); 0 when it is not out.
    double rotation() const;
    double height() const;

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
};

}  // namespace xqt
