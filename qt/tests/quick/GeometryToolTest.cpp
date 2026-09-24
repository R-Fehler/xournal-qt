/*
 * xournal-qt: the setsquare and the compass on the canvas item (real Qt Quick window, off-screen).
 *
 * XQT_BENCH_GEOMETRY=1 runs a benchmark: a setsquare 15 cm high (and a compass of 15 cm across) moved, turned and
 * sized step by step, a frame after each step, with what each frame costs on the CPU. Run it once as it is and once with
 * QT_SCALE_FACTOR=2 (a screen at 200 %):
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
        geometry.hide();
        settle(100);
    }
}
