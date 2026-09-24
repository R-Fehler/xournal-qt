/*
 * xournal-qt: the setsquare and the compass on the canvas item (real Qt Quick window, off-screen): a node of their own
 * over the page, moved, turned and sized as a transform; the page is not drawn again for them.
 *
 * XQT_BENCH_GEOMETRY=1 runs a benchmark: a setsquare 15 cm high (and a compass of 15 cm across) moved, turned and
 * sized step by step, a frame after each step, with what each frame costs on the CPU; then the setsquare moved at 5
 * and 30 cm. Run it once as it is and once with QT_SCALE_FACTOR=2 (a screen at 200 %):
 *
 *   XQT_BENCH_GEOMETRY=1 build-qt/xqt-quick-tests --gtest_filter='GeometryToolTest.bench*'
 *   QT_SCALE_FACTOR=2 XQT_BENCH_GEOMETRY=1 build-qt/xqt-quick-tests --gtest_filter='GeometryToolTest.bench*'
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstdio>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "model/GeometryTool.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "DocumentCanvasItem.h"
#include "GeometryToolLayer.h"
#include "GeometryToolPicture.h"

using namespace xqt;

namespace {
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 1200; height: 900; visible: true
    background: Rectangle { color: "#404040" }
    DocumentCanvas { objectName: "canvas"; anchors.fill: parent }
}
)";

class GeometryToolTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        for (int i = 0; i < 2; ++i) {
            session->insertNewPage(1);
        }
        view = std::make_unique<CanvasView>(*session);
        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        settle(400);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
    }

    /// Events for `ms` and all renders done
    void settle(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// Until a condition holds (or a few seconds passed: the machine may be busy); whether it holds
    template <typename Condition>
    bool until(Condition condition) {
        QElapsedTimer t;
        t.start();
        while (!condition() && t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return condition();
    }
    /// Until the canvas made its next frame
    bool nextFrame() {
        const qint64 before = canvas->frameStats().frames;
        canvas->update();
        QElapsedTimer t;
        t.start();
        while (canvas->frameStats().frames == before && t.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        return canvas->frameStats().frames != before;
    }

    struct Run {
        int steps = 0;
        double stepMs = 0;   ///< the change itself (the layer), per step
        double syncMs = 0;   ///< scene graph sync per frame
        double worstSyncMs = 0;
        double frameMs = 0;  ///< the whole step: change, sync and render (a software renderer draws on the CPU too)
        double tiles = 0;    ///< page tiles composed per frame
        double mpixels = 0;  ///< pixels uploaded per frame (millions)
    };
    template <typename Step>
    Run measure(int steps, Step step) {
        Run run;
        run.steps = steps;
        canvas->forgetFrameStats();
        QElapsedTimer whole;
        whole.start();
        qint64 stepNanos = 0;
        qint64 worst = 0;
        for (int i = 0; i < steps; ++i) {
            const auto before = canvas->frameStats();
            QElapsedTimer t;
            t.start();
            step(i);
            stepNanos += t.nsecsElapsed();
            EXPECT_TRUE(nextFrame());
            worst = std::max(worst, canvas->frameStats().syncNanos - before.syncNanos);
        }
        const double total = static_cast<double>(whole.nsecsElapsed());
        const auto stats = canvas->frameStats();
        const double frames = std::max<qint64>(1, stats.frames);
        run.stepMs = static_cast<double>(stepNanos) / steps / 1e6;
        run.syncMs = static_cast<double>(stats.syncNanos) / frames / 1e6;
        run.worstSyncMs = static_cast<double>(worst) / 1e6;
        run.frameMs = total / steps / 1e6;
        run.tiles = static_cast<double>(stats.tiles) / frames;
        run.mpixels = static_cast<double>(stats.uploadedPixels) / frames / 1e6;
        return run;
    }
    /// Put the tool on the first page and let it be shown
    void putOut(GeometryToolType type) {
        auto& geometry = view->geometryTool();
        geometry.toggle(type);
        ASSERT_TRUE(geometry.visible());
        // Its middle in the middle of the view
        ViewController& vc = view->getViewController();
        vc.setScrollPosition(vc.scrollPosition() + onScreen(geometry.middle()) -
                             QPointF(canvas->width() / 2, canvas->height() / 2));
        ASSERT_TRUE(nextFrame());
        settle(300);
    }
    /// Where a point of the tool's page is in the view (the canvas fills the window)
    QPointF onScreen(QPointF pagePoint) const {
        const QRectF r = view->pageViewRect(0);
        const double zoom = view->getViewController().zoom();
        return r.topLeft() + pagePoint * zoom;
    }
    /// ... and in the window's picture (device pixels)
    QPoint inShot(QPointF pagePoint) const {
        return (onScreen(pagePoint) * window->effectiveDevicePixelRatio()).toPoint();
    }
    /// Red, as the outline of the setsquare and the compass (drawn a little transparent over the page's white)
    static bool reddish(QRgb c) { return qRed(c) > 150 && qGreen(c) < 120 && qBlue(c) < 120; }
    QImage shot() {
        QImage image = window->grabWindow();
        if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {  // (a folder: the pictures one after another)
            static int shots = 0;
            image.save(QStringLiteral("%1/geometry-%2.png").arg(qEnvironmentVariable("XQT_TEST_SHOT")).arg(++shots));
        }
        return image;
    }

    static void report(const char* what, const Run& r) {
        std::printf("  %-28s %4d steps | step %.2f ms | sync %.2f ms (worst %.2f) | frame %.2f ms | tiles %.1f | "
                    "upload %.2f Mpx per frame\n",
                    what, r.steps, r.stepMs, r.syncMs, r.worstSyncMs, r.frameMs, r.tiles, r.mpixels);
        std::fflush(stdout);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
};
}  // namespace

// XQT_BENCH_GEOMETRY=1: what moving and turning a big setsquare and compass costs per frame (see the top).
TEST_F(GeometryToolTest, benchMovingAndTurningTheTools) {
    if (!qEnvironmentVariableIsSet("XQT_BENCH_GEOMETRY")) {
        GTEST_SKIP() << "set XQT_BENCH_GEOMETRY=1";
    }
    ViewController& vc = view->getViewController();
    vc.setZoom(vc.zoom100(), QPointF(0, 0));
    settle(300);
    const auto api = window->rendererInterface()->graphicsApi();
    std::printf("geometry bench: device pixel ratio %.2f, zoom %.2f (100 %%), scene graph %s\n",
                window->effectiveDevicePixelRatio(), vc.zoom(),
                api == QSGRendererInterface::Software ? "software" : "hardware (RHI)");

    auto& geometry = view->geometryTool();
    for (const auto type: {GeometryToolType::SETSQUARE, GeometryToolType::COMPASS}) {
        const bool setsquare = type == GeometryToolType::SETSQUARE;
        geometry.toggle(type);
        ASSERT_TRUE(geometry.visible());
        // The setsquare 15 cm high (its scale reads 15 cm to each side, the long edge is 30 cm), the compass 15 cm
        // across (a radius of 7.5 cm)
        const double wanted = setsquare ? 15 : 7.5;
        geometry.turnAndSize(0, wanted / geometry.height());
        ASSERT_NEAR(geometry.height(), wanted, 1e-6);
        // Its page in view
        const QRectF page = view->pageViewRect(geometry.page() == view->getPage(0) ? 0 : 1);
        vc.setScrollPosition(vc.scrollPosition() + QPointF(0, page.center().y() - canvas->height() / 2));
        settle(300);
        nextFrame();
        const auto info = geometry.page()->bufferInfo();
        std::printf("  (canvas %.0f x %.0f, page rendered %d at zoom %.2f x %.1f, tool at %.0f, %.0f in view)\n",
                    canvas->width(), canvas->height(), info.valid, info.zoom, info.dpiScale,
                    geometry.page()->viewRect().x() + geometry.middle().x() * vc.zoom(),
                    geometry.page()->viewRect().y() + geometry.middle().y() * vc.zoom());

        std::printf("%s, %s %.1f cm:\n", setsquare ? "setsquare" : "compass", setsquare ? "height" : "radius", wanted);
        report("moved (1 pt a step)", measure(120, [&](int i) {
                   geometry.moveBy(QPointF(i < 60 ? 1.0 : -1.0, 0.5));
               }));
        report("turned (0.5 degree a step)", measure(120, [&](int) { geometry.turnAndSize(M_PI / 360, 1.0); }));
        report("sized (0.3 % a step)", measure(60, [&](int i) {
                   geometry.turnAndSize(0, i < 30 ? 1.003 : 1 / 1.003);
               }));
        if (setsquare) {  // the cost of a step by its size
            for (const double size: {5.0, 30.0}) {
                geometry.turnAndSize(0, size / geometry.height());
                settle(300);
                nextFrame();
                char what[64];
                std::snprintf(what, sizeof what, "moved, %.0f cm high", size);
                report(what, measure(120, [&](int i) { geometry.moveBy(QPointF(i < 60 ? 1.0 : -1.0, 0.5)); }));
            }
        }
        geometry.hide();
        settle(100);
    }
}

// The tool is a node of its own over its page. Moving and turning it change only that node's transform: no page tile
// is composed or uploaded again, and its picture is not drawn again (only its small angle display, while it turns).
TEST_F(GeometryToolTest, movingAndTurningTheToolOnlyMovesItsNode) {
    for (const auto type: {GeometryToolType::SETSQUARE, GeometryToolType::COMPASS}) {
        putOut(type);
        auto& geometry = view->geometryTool();
        const auto before = canvas->geometryShown();
        ASSERT_TRUE(before.shown) << "the tool has its node";
        EXPECT_GE(before.bodies, 1);
        canvas->forgetFrameStats();

        for (int i = 0; i < 10; ++i) {
            geometry.moveBy(QPointF(3, 2));
            ASSERT_TRUE(nextFrame());
        }
        const auto moved = canvas->geometryShown();
        EXPECT_NE(moved.body, before.body) << "its transform follows it";
        EXPECT_EQ(moved.bodies, before.bodies) << "moving it does not draw it again";
        EXPECT_EQ(moved.displays, before.displays) << "nor its angle display";
        EXPECT_EQ(canvas->frameStats().tiles, 0) << "nor the page under it";

        for (int i = 0; i < 10; ++i) {
            geometry.turnAndSize(M_PI / 90, 1.0);
            ASSERT_TRUE(nextFrame());
        }
        const auto turned = canvas->geometryShown();
        EXPECT_NE(turned.body, moved.body);
        EXPECT_EQ(turned.bodies, before.bodies) << "turning it does not draw it again";
        EXPECT_EQ(turned.displays, before.displays + 10) << "only its angle display, which shows the new angle";
        EXPECT_EQ(canvas->frameStats().tiles, 0) << "the page is not drawn again";

        settle(400);  // at rest nothing more is needed: it is drawn in its own coordinates
        shot();       // (XQT_TEST_SHOT: to look at)
        EXPECT_EQ(canvas->geometryShown().bodies, before.bodies);
        EXPECT_EQ(canvas->frameStats().tiles, 0);
        geometry.hide();
        ASSERT_TRUE(nextFrame());
        EXPECT_FALSE(canvas->geometryShown().shown) << "taken away, its node goes";
    }
}

// Sizing it scales its picture on the GPU; the new size is drawn anew once it has been stable for a moment. The same
// for the zoom.
TEST_F(GeometryToolTest, aNewSizeOrZoomIsDrawnOnceItIsStable) {
    putOut(GeometryToolType::SETSQUARE);
    auto& geometry = view->geometryTool();
    const auto before = canvas->geometryShown();
    canvas->forgetFrameStats();
    for (int i = 0; i < 8; ++i) {
        geometry.turnAndSize(0, 1.03);
        ASSERT_TRUE(nextFrame());
    }
    EXPECT_EQ(canvas->geometryShown().bodies, before.bodies) << "not drawn while it is being sized";
    EXPECT_TRUE(until([&] { return canvas->geometryShown().bodies > before.bodies; })) << "drawn at its new size";
    settle(300);
    const auto sized = canvas->geometryShown();
    EXPECT_EQ(sized.bodies, before.bodies + 1) << "drawn once, at its new size";
    EXPECT_EQ(canvas->frameStats().tiles, 0) << "the page is not drawn again";

    ViewController& vc = view->getViewController();
    const double wanted = vc.zoom() * 1.5 * window->effectiveDevicePixelRatio();
    vc.setZoom(vc.zoom() * 1.5, QPointF(canvas->width() / 2, canvas->height() / 2));
    ASSERT_TRUE(nextFrame());
    EXPECT_EQ(canvas->geometryShown().bodies, sized.bodies) << "zooming scales it first";
    EXPECT_TRUE(until([&] { return canvas->geometryShown().bodies > sized.bodies; }));
    settle(300);
    EXPECT_EQ(canvas->geometryShown().bodies, sized.bodies + 1) << "then it is drawn at the new zoom, once";
    EXPECT_DOUBLE_EQ(canvas->geometryShown().scale, wanted);
}

// The window shows it where it lies, and where it was nothing is left of it.
TEST_F(GeometryToolTest, theWindowShowsTheToolWhereItLies) {
    putOut(GeometryToolType::SETSQUARE);
    auto& geometry = view->geometryTool();
    const QPointF middle = geometry.middle();
    // The long edge (red) runs through its middle, from -height to +height; a point of it 3 cm right of the middle
    auto redOnEdge = [&](const QImage& image, QPointF mid) {
        for (int dy = -2; dy <= 2; ++dy) {
            if (reddish(image.pixel(inShot(mid + QPointF(3 * CM, 0)) + QPoint(0, dy)))) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(redOnEdge(shot(), middle)) << "its long edge is shown";
    geometry.moveBy(QPointF(0, 2 * CM));
    ASSERT_TRUE(nextFrame());
    const QImage after = shot();
    EXPECT_TRUE(redOnEdge(after, middle + QPointF(0, 2 * CM))) << "it went along";
    EXPECT_FALSE(redOnEdge(after, middle)) << "nothing is left where it was";
    // Put aside, it is not shown
    geometry.setMinimized(true);
    ASSERT_TRUE(nextFrame());
    EXPECT_FALSE(redOnEdge(shot(), middle + QPointF(0, 2 * CM)));
    EXPECT_FALSE(canvas->geometryShown().shown);
}

// A big tool at a high zoom: its whole picture is at most 4096 pixels a side (drawn smaller than it is shown); once it
// rests, the part in view is drawn sharp over it.
TEST_F(GeometryToolTest, aBigToolAtAHighZoomGetsASharpPartInViewWhenItRests) {
    putOut(GeometryToolType::COMPASS);
    auto& geometry = view->geometryTool();
    geometry.turnAndSize(0, GeometryToolLayer::MAX_HEIGHT_CM / geometry.height());
    ViewController& vc = view->getViewController();
    vc.setZoom(4, QPointF(canvas->width() / 2, canvas->height() / 2));
    EXPECT_TRUE(until([&] { return canvas->geometryShown().sharpPart; })) << "the part in view is sharp";
    const auto shown = canvas->geometryShown();
    const double wanted = 4 * window->effectiveDevicePixelRatio();
    EXPECT_LT(shown.scale, wanted) << "the whole of it is drawn smaller";
    const QRectF bounds = GeometryToolPicture::bounds(GeometryToolType::COMPASS, geometry.height());
    EXPECT_LE(std::max(bounds.width(), bounds.height()) * shown.scale, 4096.5) << "at most 4096 pixels a side";
    shot();
    // Moving it keeps that (it is in the tool's own coordinates) and draws nothing
    const int bodies = shown.bodies;
    geometry.moveBy(QPointF(4, 3));
    ASSERT_TRUE(nextFrame());
    EXPECT_EQ(canvas->geometryShown().bodies, bodies);
    EXPECT_TRUE(canvas->geometryShown().sharpPart);
}

// The compass put out in place of the setsquare (in one step, without a frame between) is drawn at once: the pictures
// of the setsquare are not taken for it, even when the new tool's picture gets the old one's address.
TEST_F(GeometryToolTest, anotherToolIsDrawnAtOnce) {
    putOut(GeometryToolType::SETSQUARE);
    auto& geometry = view->geometryTool();
    const int bodies = canvas->geometryShown().bodies;
    geometry.toggle(GeometryToolType::COMPASS);
    ASSERT_EQ(geometry.type(), GeometryToolType::COMPASS);
    ASSERT_TRUE(nextFrame());
    EXPECT_TRUE(canvas->geometryShown().shown);
    EXPECT_EQ(canvas->geometryShown().bodies, bodies + 1) << "the compass got its own picture";
}
