#include "GeometryToolPicture.h"

#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <cairo.h>

#include "control/zoom/ZoomControl.h"
#include "model/Compass.h"
#include "model/Setsquare.h"
#include "util/Point.h"
#include "util/Range.h"
#include "util/Rectangle.h"
#include "view/CompassView.h"
#include "view/Repaintable.h"
#include "view/SetsquareView.h"

#include "Perf.h"

namespace xqt {

/// What upstream's views draw on: here only their zoom (the scale of the picture); nothing is repainted.
class GeometryToolPicture::Surface final: public xoj::view::Repaintable {
public:
    explicit Surface(ZoomControl* zoomControl): zoomControl(zoomControl) {}
    double zoom = 1;

    Range getVisiblePart() const override { return Range(); }
    double getZoom() const override { return zoom; }
    ZoomControl* getZoomControl() const override { return zoomControl; }
    double getWidth() const override { return 0; }
    double getHeight() const override { return 0; }
    xoj::util::Point<double> toWidgetCoordinates(const xoj::util::Point<double>& p) const override { return p; }
    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override {
        return r;
    }
    void flagDirtyRegion(const Range&) const override {}
    void drawAndDeleteToolView(xoj::view::ToolView*, const Range&) override {}  // (the views belong to the picture)
    void deleteOverlayView(xoj::view::OverlayView*, const Range&) override {}

private:
    ZoomControl* zoomControl;
};

namespace {
/// The tool at the origin, of an extent that is set: upstream draws a tool into a mask of its extent and places that,
/// so the extent is the part that is drawn.
template <class Tool>
class PartOf final: public Tool {
public:
    PartOf(): Tool(Tool::INITIAL_HEIGHT, 0, 0, 0) {}
    Range extent{0, 0, 1, 1};
    Range getToolRange(bool) const override { return extent; }
};

/// Draw a view into a new image covering `rect` (points) at `scale`. Returns the image and the rect it really covers
/// (whole pixels).
std::pair<QImage, QRectF> draw(const xoj::view::GeometryToolView& view, QRectF rect, double scale) {
    const int x0 = static_cast<int>(std::floor(rect.left() * scale));
    const int y0 = static_cast<int>(std::floor(rect.top() * scale));
    const int w = std::max(1, static_cast<int>(std::ceil(rect.right() * scale)) - x0);
    const int h = std::max(1, static_cast<int>(std::ceil(rect.bottom() * scale)) - y0);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // CAIRO_FORMAT_ARGB32 and QImage::Format_ARGB32_Premultiplied share the memory layout.
    cairo_surface_t* surface = cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, w, h,
                                                                   static_cast<int>(image.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    // Whole pixels, as upstream's mask of the same extent and zoom: the mask is copied pixel for pixel
    cairo_translate(cr, -x0, -y0);
    cairo_scale(cr, scale, scale);
    view.draw(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return {std::move(image), QRectF(x0 / scale, y0 / scale, w / scale, h / scale)};
}

cairo_matrix_t placed(QPointF origin, double rotation) {
    cairo_matrix_t m;
    cairo_matrix_init_translate(&m, origin.x(), origin.y());
    cairo_matrix_rotate(&m, rotation);
    cairo_matrix_scale(&m, CM, CM);
    return m;
}

/// Where the display is drawn: far from the (tiny) extent of the display's tool copy, and from the body
const QPointF DISPLAY_AT(1000, 1000);
/// Half the size of the display's picture (points): its circle has a radius of 0.3 cm, the number fits in it
constexpr double DISPLAY_HALF = 0.5 * CM;
}  // namespace

GeometryToolPicture::GeometryToolPicture(GeometryToolType type):
        toolType(type),
        number([] {
            static std::atomic<quint64> made{0};
            return ++made;
        }()),
        zoomControl(std::make_unique<ZoomControl>()),
        bodySurface(std::make_unique<Surface>(zoomControl.get())),
        displaySurface(std::make_unique<Surface>(zoomControl.get())) {
    if (type == GeometryToolType::SETSQUARE) {
        auto body = std::make_unique<PartOf<Setsquare>>();
        auto display = std::make_unique<PartOf<Setsquare>>();
        bodyView = std::make_unique<xoj::view::SetsquareView>(body.get(), bodySurface.get(), zoomControl.get());
        displayView =
                std::make_unique<xoj::view::SetsquareView>(display.get(), displaySurface.get(), zoomControl.get());
        bodyTool = std::move(body);
        displayTool = std::move(display);
    } else {
        auto body = std::make_unique<PartOf<Compass>>();
        auto display = std::make_unique<PartOf<Compass>>();
        bodyView = std::make_unique<xoj::view::CompassView>(body.get(), bodySurface.get(), zoomControl.get());
        displayView = std::make_unique<xoj::view::CompassView>(display.get(), displaySurface.get(), zoomControl.get());
        bodyTool = std::move(body);
        displayTool = std::move(display);
    }
}

GeometryToolPicture::~GeometryToolPicture() {
    // The views before their tools (a tool tells its views when it goes)
    bodyView.reset();
    displayView.reset();
}

QRectF GeometryToolPicture::bounds(GeometryToolType type, double height) {
    Range r;
    if (type == GeometryToolType::SETSQUARE) {
        r = Setsquare(height, 0, 0, 0).getToolRange(false);
    } else {
        r = Compass(height, 0, 0, 0).getToolRange(false);
    }
    return QRectF(QPointF(r.minX, r.minY), QPointF(r.maxX, r.maxY));
}

QPointF GeometryToolPicture::displayCentre(GeometryToolType type, double height) {
    // Upstream's SetsquareView / CompassView (RELATIVE_CIRCLE_POS, OFFSET_CIRCLE_POS): the display's circle lies on
    // the axis of the tool, in the setsquare towards its apex, in the compass towards its top.
    if (type == GeometryToolType::SETSQUARE) {
        return QPointF(0, 0.75 * height * CM);
    }
    return QPointF(0, -(0.8 * height - 0.4) * CM);
}

std::string GeometryToolPicture::displayText(double rotation) {
    // As upstream's SetsquareView::drawRotation and CompassView::drawRotation write it
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << std::abs(std::remainder(rotation * 180.0 / M_PI, 180.));
    return ss.str();
}

auto GeometryToolPicture::body(double height, double scale, QRectF part) -> Image {
    const QRectF all = bounds(toolType, height);
    const QRectF r = part.isNull() ? all : part.intersected(all);
    if (r.isEmpty() || scale <= 0) {
        return {};
    }
    bodyTool->setHeight(height);
    auto* tool = bodyTool.get();
    const Range extent(r.left(), r.top(), r.right(), r.bottom());
    if (toolType == GeometryToolType::SETSQUARE) {
        static_cast<PartOf<Setsquare>*>(tool)->extent = extent;
    } else {
        static_cast<PartOf<Compass>*>(tool)->extent = extent;
    }
    bodySurface->zoom = scale;
    bodyView->on(xoj::view::GeometryToolView::RESET_MASK);
    // Its view draws the display too: far away from the body, where it is not in the picture
    bodyView->on(xoj::view::GeometryToolView::UPDATE_VALUES, height, 0,
                 placed(all.bottomRight() + QPointF(1000, 1000), 0));
    auto [image, rect] = draw(*bodyView, r, scale);
    bodyView->on(xoj::view::GeometryToolView::RESET_MASK);  // (the picture is the cache now)
    ++bodies;
    Perf::add(Perf::GeometryPictures);
    return {std::move(image), rect, scale, height};
}

auto GeometryToolPicture::display(double height, double rotation, double scale) -> Image {
    if (scale <= 0) {
        return {};
    }
    // Its view draws the tool's body into a mask of its tool's tiny extent (at the origin, away from the display)
    // once, and keeps that: only the display is drawn each time.
    displaySurface->zoom = scale;
    // The tool turned and placed so that the middle of its display is at DISPLAY_AT
    const QPointF c = displayCentre(toolType, height);
    const QPointF turned(c.x() * std::cos(rotation) - c.y() * std::sin(rotation),
                         c.x() * std::sin(rotation) + c.y() * std::cos(rotation));
    displayView->on(xoj::view::GeometryToolView::UPDATE_VALUES, height, rotation,
                    placed(DISPLAY_AT - turned, rotation));
    const QRectF around(DISPLAY_AT - QPointF(DISPLAY_HALF, DISPLAY_HALF), QSizeF(2 * DISPLAY_HALF, 2 * DISPLAY_HALF));
    auto [image, rect] = draw(*displayView, around, scale);
    ++displays;
    Perf::add(Perf::GeometryDisplays);
    return {std::move(image), rect.translated(-DISPLAY_AT), scale, height};
}

}  // namespace xqt
