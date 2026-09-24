/*
 * xournal-qt: the pictures of the setsquare and the compass (GeometryToolPicture) look as upstream's views drew the
 * tool straight onto its page before.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>

#include <QImage>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/zoom/ZoomControl.h"
#include "model/Compass.h"
#include "model/Setsquare.h"
#include "util/Point.h"
#include "util/Range.h"
#include "util/Rectangle.h"
#include "view/CompassView.h"
#include "view/Repaintable.h"
#include "view/SetsquareView.h"

#include "GeometryToolPicture.h"

using namespace xqt;

namespace {
/// A page for upstream's view to draw on, at a zoom (what CanvasPage was for it)
class Page final: public xoj::view::Repaintable {
public:
    explicit Page(double zoom): zoom(zoom) {}
    Range getVisiblePart() const override { return Range(); }
    double getZoom() const override { return zoom; }
    ZoomControl* getZoomControl() const override { return const_cast<ZoomControl*>(&zoomControl); }
    double getWidth() const override { return 0; }
    double getHeight() const override { return 0; }
    xoj::util::Point<double> toWidgetCoordinates(const xoj::util::Point<double>& p) const override { return p; }
    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override {
        return r;
    }
    void flagDirtyRegion(const Range&) const override {}
    void drawAndDeleteToolView(xoj::view::ToolView*, const Range&) override {}
    void deleteOverlayView(xoj::view::OverlayView*, const Range&) override {}

    double zoom;
    ZoomControl zoomControl;
};

constexpr QSize SIZE(1100, 900);

cairo_t* onImage(QImage& image, cairo_surface_t*& surface) {
    image.fill(Qt::white);
    surface = cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, image.width(), image.height(),
                                                  static_cast<int>(image.bytesPerLine()));
    return cairo_create(surface);
}

/// Before: upstream's view drew the tool into its page's tiles (CanvasPage::composeTile), at `scale` pixels a point
QImage asBefore(GeometryToolType type, double height, double rotation, QPointF origin, double scale) {
    QImage image(SIZE, QImage::Format_ARGB32_Premultiplied);
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = onImage(image, surface);
    cairo_scale(cr, scale, scale);
    Page page(scale);
    if (type == GeometryToolType::SETSQUARE) {
        Setsquare tool(height, rotation, origin.x(), origin.y());
        xoj::view::SetsquareView view(&tool, &page, &page.zoomControl);
        tool.notify(true);
        view.draw(cr);
    } else {
        Compass tool(height, rotation, origin.x(), origin.y());
        xoj::view::CompassView view(&tool, &page, &page.zoomControl);
        tool.notify(true);
        view.draw(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return image;
}

void paint(cairo_t* cr, const GeometryToolPicture::Image& picture) {
    cairo_save(cr);
    cairo_translate(cr, picture.rect.x(), picture.rect.y());
    cairo_scale(cr, 1 / picture.scale, 1 / picture.scale);
    cairo_surface_t* s = cairo_image_surface_create_for_data(
            const_cast<uchar*>(picture.image.constBits()), CAIRO_FORMAT_ARGB32, picture.image.width(),
            picture.image.height(), static_cast<int>(picture.image.bytesPerLine()));
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);  // (as the GPU samples it)
    cairo_paint(cr);
    cairo_surface_destroy(s);
    cairo_restore(cr);
}

/// Now: its pictures placed as the canvas places them (the body turned around its middle, the display upright)
QImage asNow(GeometryToolPicture& pictures, double height, double rotation, QPointF origin, double scale) {
    QImage image(SIZE, QImage::Format_ARGB32_Premultiplied);
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = onImage(image, surface);
    cairo_scale(cr, scale, scale);
    cairo_save(cr);
    cairo_translate(cr, origin.x(), origin.y());
    cairo_rotate(cr, rotation);
    paint(cr, pictures.body(height, scale));
    cairo_restore(cr);
    // The display upright, its middle on a whole pixel (it is drawn on whole pixels around its middle)
    const QPointF c = GeometryToolPicture::displayCentre(pictures.type(), height);
    const QPointF middle(origin.x() + c.x() * std::cos(rotation) - c.y() * std::sin(rotation),
                         origin.y() + c.x() * std::sin(rotation) + c.y() * std::cos(rotation));
    cairo_translate(cr, std::round(middle.x() * scale) / scale, std::round(middle.y() * scale) / scale);
    paint(cr, pictures.display(height, rotation, scale));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return image;
}

struct Difference {
    int most = 0;          ///< the biggest difference of a colour channel
    int differing = 0;     ///< pixels that differ by more than a little
    int toolPixels = 0;    ///< pixels that are not white (in the picture before)
};
Difference compare(const QImage& a, const QImage& b, QRect leaveOut = QRect()) {
    Difference d;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (leaveOut.contains(x, y)) {
                continue;
            }
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            const int most = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                       std::abs(qBlue(p) - qBlue(q))});
            d.most = std::max(d.most, most);
            d.differing += most > 24;
            d.toolPixels += p != qRgb(255, 255, 255);
        }
    }
    return d;
}
}  // namespace

// At rest, unturned, the pictures show the tool exactly as upstream drew it on the page; only the angle display (whose
// middle the canvas puts on a whole pixel, for a sharp number) may lie a fraction of a pixel away.
TEST(GeometryToolPictureTest, anUnturnedToolLooksAsUpstreamDrewIt) {
    for (const auto type: {GeometryToolType::SETSQUARE, GeometryToolType::COMPASS}) {
        const double height = type == GeometryToolType::SETSQUARE ? 7 : 3.5;
        const double scale = 2.5;  // (a zoom of 125 % on a 200 % screen, say)
        const QPointF origin(220.4, 60.8);  // (whole pixels at this scale)
        GeometryToolPicture pictures(type);
        const QImage before = asBefore(type, height, 0, origin, scale);
        const QImage now = asNow(pictures, height, 0, origin, scale);
        const QPointF display = (origin + GeometryToolPicture::displayCentre(type, height)) * scale;
        const QRect displayBox = QRectF(display - QPointF(0.4, 0.4) * CM * scale, QSizeF(0.8, 0.8) * CM * scale)
                                         .toAlignedRect();
        const Difference body = compare(before, now, displayBox);
        EXPECT_GT(body.toolPixels, 10000) << "the tool is in the picture";
        EXPECT_LE(body.most, 1) << "the tool (but its angle display) is the same, pixel for pixel";
        const Difference all = compare(before, now);
        EXPECT_LT(all.differing, 10) << "the angle display differs only by its place on the pixel grid";
    }
}

// Turned, both draw the same picture of the tool turned (upstream: its mask, painted turned); the angle display is
// upright in both. Pixel for pixel, with the display's middle on a whole pixel as the canvas puts it.
TEST(GeometryToolPictureTest, aTurnedToolLooksAsUpstreamDrewIt) {
    for (const auto type: {GeometryToolType::SETSQUARE, GeometryToolType::COMPASS}) {
        const double height = type == GeometryToolType::SETSQUARE ? 7 : 3.5;
        const double scale = 2;
        const double rotation = 25 * M_PI / 180;
        // The middle of its display on a whole pixel (so that upstream draws the display where the canvas puts it)
        const QPointF c = GeometryToolPicture::displayCentre(type, height);
        const QPointF display(700, 420);
        const QPointF origin = display / scale - QPointF(c.x() * std::cos(rotation) - c.y() * std::sin(rotation),
                                                         c.x() * std::sin(rotation) + c.y() * std::cos(rotation));
        GeometryToolPicture pictures(type);
        const QImage before = asBefore(type, height, rotation, origin, scale);
        const QImage now = asNow(pictures, height, rotation, origin, scale);
        const QRect displayBox = QRectF(display - QPointF(0.4, 0.4) * CM * scale, QSizeF(0.8, 0.8) * CM * scale)
                                         .toAlignedRect();
        const Difference body = compare(before, now, displayBox);
        EXPECT_GT(body.toolPixels, 10000);
        EXPECT_LE(body.most, 2) << "the turned tool (but its angle display) is the same";
        const Difference all = compare(before, now);
        EXPECT_LE(all.most, 2) << "its angle display too, where it lies on the same pixel grid as the canvas puts it";
    }
}

// A part of the tool (the sharp part of a big one in view) is that part of the whole picture, pixel for pixel.
TEST(GeometryToolPictureTest, aPartIsThatPartOfTheWhole) {
    for (const auto type: {GeometryToolType::SETSQUARE, GeometryToolType::COMPASS}) {
        GeometryToolPicture pictures(type);
        const double scale = 1.5;
        const auto whole = pictures.body(6, scale);
        const auto part = pictures.body(6, scale, QRectF(-60.3, 10.2, 100, 70));
        ASSERT_FALSE(part.image.isNull());
        EXPECT_LT(part.image.width(), whole.image.width());
        const QPoint at = ((part.rect.topLeft() - whole.rect.topLeft()) * scale).toPoint();
        EXPECT_EQ(whole.image.copy(QRect(at, part.image.size())), part.image);
        EXPECT_EQ(pictures.bodiesDrawn(), 2);
    }
}

// The whole of it is upstream's extent of the tool; the display turns and shows the angle (as upstream writes it).
TEST(GeometryToolPictureTest, theBodyCoversTheToolAndTheDisplayItsAngle) {
    GeometryToolPicture pictures(GeometryToolType::SETSQUARE);
    const auto body = pictures.body(8, 1);
    EXPECT_TRUE(body.rect.contains(GeometryToolPicture::bounds(GeometryToolType::SETSQUARE, 8)));
    EXPECT_EQ(body.image.width(), static_cast<int>(std::ceil(body.rect.width())));
    EXPECT_EQ(GeometryToolPicture::displayText(0), "0.0");
    EXPECT_EQ(GeometryToolPicture::displayText(20 * M_PI / 180), "20.0");
    EXPECT_EQ(GeometryToolPicture::displayText(-30 * M_PI / 180), "30.0");
    const auto a = pictures.display(8, 0, 2);
    const auto b = pictures.display(8, 20 * M_PI / 180, 2);
    EXPECT_NE(a.image, b.image) << "another angle, another number";
    EXPECT_TRUE(a.rect.contains(QPointF(0, 0))) << "around the display's middle";
}
