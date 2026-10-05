/*
 * xournal-qt: dark pages on the canvas (qt/docs/dark-pages.md), pixel tests: a page with ink, a highlighter and a
 * picture, shown as it is and dark. The dark picture is the light one through the table (on the GPU a shader; on the
 * software renderer the same table on the CPU), the picture keeps its colors, a page with dark paper stays as it is,
 * and no page is drawn again for it.
 *
 * Plain ctest runs it on the software renderer; DarkPagesCanvas.quick@gl runs it on OpenGL (Mesa's llvmpipe under
 * Xvfb), with XQT_EXPECT_GPU set: then the shader must be what drew it.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include <cairo.h>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PageRaster.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "DarkPages.h"
#include "DevicePixels.h"
#include "DocumentCanvasItem.h"

using namespace xqt;

namespace {
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 800; height: 700; visible: true
    background: Rectangle { color: "#404040" }
    DocumentCanvas { id: canvas; objectName: "canvas"; anchors.fill: parent }
}
)";

constexpr QRgb CLASSIC_WARNINGS = 0xffd6342c, DARK_WARNINGS = 0xffff6b6b;
constexpr QRgb CLASSIC_KEYTERMS_HIGHLIGHT = 0xffffe066, DARK_KEYTERMS_HIGHLIGHT = 0xff7a5a12;
constexpr QRgb GREEN = 0xff00a050;

std::string png(int w, int h, QRgb color) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, qRed(color) / 255.0, qGreen(color) / 255.0, qBlue(color) / 255.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    std::string bytes;
    cairo_surface_write_to_png_stream(
            s,
            [](void* closure, const unsigned char* data, unsigned int length) {
                static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
                return CAIRO_STATUS_SUCCESS;
            },
            &bytes);
    cairo_surface_destroy(s);
    return bytes;
}

std::unique_ptr<Stroke> line(double x0, double x1, double y, double width, QRgb color, bool highlighter = false) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(width);
    s->setColor(Color(static_cast<uint32_t>(color)));
    if (highlighter) {
        s->setToolType(StrokeTool::HIGHLIGHTER);
    }
    s->addPoint(Point(x0, y, -1));
    s->addPoint(Point(x1, y, -1));
    return s;
}

int distance(QRgb a, QRgb b) {
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}
std::string hex(QRgb c) { return QColor(c).name().toStdString(); }
QRgb over(QRgb paper, QRgb ink, double a) {
    const auto mix = [a](int p, int i) { return static_cast<int>(std::lround(p + a * (i - p))); };
    return qRgb(mix(qRed(paper), qRed(ink)), mix(qGreen(paper), qGreen(ink)), mix(qBlue(paper), qBlue(ink)));
}

class DarkPagesCanvas: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        dark::setRoles({{CLASSIC_WARNINGS, DARK_WARNINGS, 1.0},
                        {CLASSIC_KEYTERMS_HIGHLIGHT, DARK_KEYTERMS_HIGHLIGHT, 0.8 / 0.47}});
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        Document* doc = session->getDocument();
        const PageRef page = doc->getPage(0);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        page->setBackgroundColor(Color(0xffffffffU));
        Layer* layer = page->getSelectedLayer();
        layer->addElement(line(60, 220, 100, 10, 0xff000000));
        layer->addElement(line(60, 220, 140, 10, CLASSIC_WARNINGS));
        layer->addElement(line(60, 220, 190, 24, CLASSIC_KEYTERMS_HIGHLIGHT, true));
        auto image = std::make_unique<Image>();
        image->setImage(png(80, 60, GREEN));
        image->setTransformation({1, 0, 0, 1, {280, 80}});
        picture = QRectF(image->getBoundingBox().x, image->getBoundingBox().y, image->getBoundingBox().width,
                         image->getBoundingBox().height);
        layer->addElement(std::move(image));

        view = std::make_unique<CanvasView>(*session);
        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        settle(500);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
        dark::setRoles({});
    }

    void settle(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// A point of the first page (page coordinates) in the window
    QPointF at(double x, double y) const {
        const QRectF r = view->pageViewRect(0);
        const double zoom = view->getViewController().zoom();
        return r.topLeft() + QPointF(x, y) * zoom;
    }
    QRgb pixel(const QImage& shot, double x, double y) const { return test::pixelAt(shot, window, at(x, y)); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
    QRectF picture;
};
}  // namespace

TEST_F(DarkPagesCanvas, thePageIsShownDarkWithoutBeingDrawnAgain) {
    const QImage light = window->grabWindow();
    const auto rendersBefore = PageRaster::stats().renders;

    canvas->setDarkPages(true);
    settle(400);
    const QImage darkShot = window->grabWindow();
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        light.save(qEnvironmentVariable("XQT_TEST_SHOT") + "-light.png");
        darkShot.save(qEnvironmentVariable("XQT_TEST_SHOT") + "-dark.png");
    }
    EXPECT_EQ(PageRaster::stats().renders, rendersBefore) << "turning the pages dark drew them again";
    if (qEnvironmentVariableIsSet("XQT_EXPECT_GPU")) {
        EXPECT_TRUE(canvas->darkOnGpuShown()) << "the shader did not draw the dark page";
    }

    // The paper, the black ink, the role's red and the highlighter in their dark equivalents
    EXPECT_LE(distance(pixel(darkShot, 40, 40), dark::darkPaper()), 3) << hex(pixel(darkShot, 40, 40));
    EXPECT_LE(distance(pixel(darkShot, 140, 100), dark::lightInk()), 6) << hex(pixel(darkShot, 140, 100));
    EXPECT_LE(distance(pixel(darkShot, 140, 140), DARK_WARNINGS), 8) << hex(pixel(darkShot, 140, 140));
    const QRgb marker = over(dark::darkPaper(), DARK_KEYTERMS_HIGHLIGHT, 0.8);
    EXPECT_LE(distance(pixel(darkShot, 140, 190), marker), 8)
            << hex(pixel(darkShot, 140, 190)) << " vs " << hex(marker);
    // The picture keeps its colors
    const QPointF middle = picture.center();
    EXPECT_LE(distance(pixel(darkShot, middle.x(), middle.y()), GREEN), 2)
            << hex(pixel(darkShot, middle.x(), middle.y()));
    EXPECT_LE(distance(pixel(darkShot, middle.x(), middle.y()), pixel(light, middle.x(), middle.y())), 2);

    // Every pixel of the page (outside the picture) is the light one through the table
    int checked = 0, off = 0;
    for (double y = 20; y < 260; y += 3.7) {
        for (double x = 20; x < 420; x += 4.3) {
            if (picture.adjusted(-2, -2, 2, 2).contains(x, y)) {
                continue;
            }
            const QRgb expected = dark::lookup(pixel(light, x, y));
            const QRgb shown = pixel(darkShot, x, y);
            ++checked;
            if (distance(expected, shown) > 4) {
                if (++off < 5) {
                    ADD_FAILURE() << "at " << x << ", " << y << ": " << hex(shown) << " instead of " << hex(expected)
                                  << " (light " << hex(pixel(light, x, y)) << ")";
                }
            }
        }
    }
    EXPECT_GT(checked, 5000);
    EXPECT_EQ(off, 0);

    // And back: as it was
    canvas->setDarkPages(false);
    settle(400);
    const QImage again = window->grabWindow();
    EXPECT_LE(distance(pixel(again, 40, 40), 0xffffffff), 1);
    EXPECT_LE(distance(pixel(again, 140, 140), pixel(light, 140, 140)), 1);
    EXPECT_EQ(PageRaster::stats().renders, rendersBefore);
}

TEST_F(DarkPagesCanvas, aPageWithDarkPaperStaysAsItIs) {
    {
        Document* doc = session->getDocument();
        doc->lock();
        doc->getPage(0)->setBackgroundColor(Color(0xff141414U));
        doc->unlock();
        session->firePageChanged(0);
    }
    settle(400);
    const QImage light = window->grabWindow();
    canvas->setDarkPages(true);
    settle(400);
    const QImage darkShot = window->grabWindow();
    for (const auto& [x, y]: {std::pair{40.0, 40.0}, {140.0, 100.0}, {140.0, 140.0}}) {
        EXPECT_LE(distance(pixel(darkShot, x, y), pixel(light, x, y)), 1) << x << ", " << y;
    }
}
