/*
 * xournal-qt: the setsquare or the compass drawn into pictures, for the scene graph to move and turn on the GPU.
 *
 * The tool used to be an overlay of its page, drawn with cairo into the page tiles: every step of moving or turning
 * it composed and uploaded the tiles under its old and new place, with a cost that grows with (size x zoom x screen
 * scale)^2 - a big setsquare lagged on a 200 % screen. Now the canvas shows it as pictures of its own under a
 * transform: moving, turning and sizing it only changes the transform.
 *
 * - The body (outline, scales, numbers) is drawn in the tool's own coordinates - unturned, its middle at the origin -
 *   so turning it needs no new picture; only a new size or zoom does (the canvas waits until they are stable).
 * - The angle display (the angle in a small circle) changes while it turns, and its number stays upright on the page:
 *   it is a small picture of its own, drawn upright.
 *
 * Both are drawn by upstream's own views (SetsquareView, CompassView), unchanged, on a private copy of the tool that
 * lies at the origin; the part of the body to draw is the extent of that copy (upstream draws the tool into a mask of
 * that extent first), so a part of a big tool costs no more than its size.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>

#include <QImage>
#include <QPointF>
#include <QRectF>

#include "model/GeometryTool.h"

namespace xoj::view {
class GeometryToolView;
}

class ZoomControl;

namespace xqt {

class GeometryToolPicture {
public:
    explicit GeometryToolPicture(GeometryToolType type);
    ~GeometryToolPicture();
    GeometryToolPicture(const GeometryToolPicture&) = delete;
    GeometryToolPicture& operator=(const GeometryToolPicture&) = delete;

    struct Image {
        QImage image;  ///< premultiplied ARGB, transparent around the tool
        QRectF rect;   ///< where the image lies, in points: of the tool's own coordinates (body), or around the centre
                       ///< of the display (display)
        double scale = 0;   ///< pixels per point
        double height = 0;  ///< of the tool it shows (centimetres)
    };

    /// The whole tool in its own coordinates (points, unturned, its middle at the origin) - upstream's extent of it.
    static QRectF bounds(GeometryToolType type, double height);
    /// The middle of the angle display, in the tool's own coordinates (points).
    static QPointF displayCentre(GeometryToolType type, double height);

    /// The body of a tool of this height, at `scale` pixels per point: `part` of it (points, own coordinates), or all.
    Image body(double height, double scale, QRectF part = QRectF());
    /// The angle display for this height and rotation (radians), upright, around its middle (see displayCentre). It
    /// shows the angle, as displayText() does.
    Image display(double height, double rotation, double scale);

    /// The angle the display shows for this rotation: the display is drawn anew when this changes.
    static std::string displayText(double rotation);

    GeometryToolType type() const { return toolType; }
    /// Different for every picture made (a tool put out anew may get the address of the one before)
    quint64 serial() const { return number; }
    /// Pictures drawn so far (tests, XQT_PERF)
    int bodiesDrawn() const { return bodies; }
    int displaysDrawn() const { return displays; }

private:
    class Surface;
    GeometryToolType toolType;
    quint64 number;
    std::unique_ptr<ZoomControl> zoomControl;  ///< a zoom of its own: the canvas' zoom must not throw its masks away
    std::unique_ptr<Surface> bodySurface;
    std::unique_ptr<Surface> displaySurface;
    std::unique_ptr<GeometryTool> bodyTool;     ///< the copy at the origin whose extent is the part to draw
    std::unique_ptr<GeometryTool> displayTool;  ///< a copy of a tiny extent: its view draws the display only
    std::unique_ptr<xoj::view::GeometryToolView> bodyView;
    std::unique_ptr<xoj::view::GeometryToolView> displayView;
    int bodies = 0;
    int displays = 0;
};

}  // namespace xqt
