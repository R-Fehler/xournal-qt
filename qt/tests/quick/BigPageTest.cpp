/*
 * xournal-qt: posters (qt/page-sizes). An A0 page with a few hundred strokes in the canvas item (real Qt Quick window,
 * off-screen): its render stays within the memory and size limits at every zoom (at 100 % and more the whole page is
 * far bigger than a texture or a sensible buffer), what is in view is sharp, scrolling over it stays fast, and fit page
 * shows it whole.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PageRaster.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasMemory.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "DocumentCanvasItem.h"
#include "ScreenCalibration.h"

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

constexpr double MM = 72.0 / 25.4;
constexpr double A0_W = 841 * MM, A0_H = 1189 * MM;
constexpr double MARK_X = A0_W * 0.6 + 30, MARK_Y = A0_H * 0.55 + 30;
/// What a page's buffer may take at most: 24 megapixels (96 MB), sides of at most 16384 pixels
constexpr qint64 MAX_BUFFER_PIXELS = 24LL * 1024 * 1024;
constexpr int MAX_BUFFER_SIDE = 16384;
static_assert(PageRaster::WHOLE_PAGE_PIXELS == MAX_BUFFER_PIXELS && PageRaster::MAX_SIDE == MAX_BUFFER_SIDE);

class BigPageTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        // An A0 poster on graph paper, with 300 blue lines across it (one every 11 pt), then a second A0 page (new
        // pages take the size of the one before)
        Document* doc = session->getDocument();
        const PageRef poster = doc->getPage(0);
        poster->setSize(A0_W, A0_H);
        poster->setBackgroundType(PageType(PageTypeFormat::Graph));
        for (int i = 0; i < STROKES; ++i) {
            auto s = std::make_unique<Stroke>();
            s->setWidth(1.5);
            s->setColor(Color(0xff0000ffU));
            const double y = 20 + i * 11.0;
            for (int k = 0; k <= 40; ++k) {
                s->addPoint(Point(20 + k * (A0_W - 40) / 40, y + (k % 2) * 3, -1));
            }
            poster->getSelectedLayer()->addElement(std::move(s));
        }
        // A red mark near the middle: where it is on screen tells that the part drawn is in its place
        auto mark = std::make_unique<Stroke>();
        mark->setWidth(8);
        mark->setColor(Color(0xffff0000U));
        mark->addPoint(Point(MARK_X - 4, MARK_Y, -1));
        mark->addPoint(Point(MARK_X + 4, MARK_Y, -1));
        poster->getSelectedLayer()->addElement(std::move(mark));
        session->insertNewPage(1);
        ASSERT_DOUBLE_EQ(doc->getPage(1)->getWidth(), A0_W);

        view = std::make_unique<CanvasView>(*session);
        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        // 100 %: 150 dpi (the calibration counts, not what the off-screen screen says)
        const auto display = ScreenCalibration::displayOf(window->screen(), window->effectiveDevicePixelRatio());
        ScreenCalibration::store(*app->getSettings(), display.key, 150.0 * display.dpr);
        Q_EMIT app->settingsChanged();
        settle(300);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
    }

    void run(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void settle(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// The view rests: the poster is rendered at the zoom it is shown at (how long that took, ms; -1: it was not)
    qint64 rendered() {
        const double zoom = view->getViewController().zoom();
        QElapsedTimer t;
        t.start();
        auto sharp = [&] {
            const auto info = view->getPage(0)->bufferInfo();
            return info.valid && info.zoom == zoom && !app->getRenderService()->hasWork(RenderService::Priority::Visible);
        };
        while (!sharp() && t.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        const qint64 took = sharp() ? t.elapsed() : -1;
        run(200);  // (the frames that compose it)
        return took;
    }
    /// Blue pixels of the lines in the middle of the window (the page as it is in view, sharp or not)
    int bluePixels() const {
        const QImage shot = window->grabWindow();
        int n = 0;
        const QRect middle(shot.width() / 2 - 150, shot.height() / 2 - 150, 300, 300);
        for (int y = middle.top(); y <= middle.bottom(); ++y) {
            for (int x = middle.left(); x <= middle.right(); ++x) {
                const QColor c(shot.pixel(x, y));
                n += c.blue() > 150 && c.red() < 110 && c.green() < 110;
            }
        }
        return n;
    }
    /// The red mark is where it belongs on screen
    bool markInPlace() const {
        const QImage shot = window->grabWindow();
        const QRectF page = view->pageViewRect(0);
        const QPointF at = canvas->mapToScene(page.topLeft() + QPointF(MARK_X, MARK_Y) * view->getViewController().zoom());
        const QColor c(shot.pixel(at.toPoint()));
        return c.red() > 200 && c.green() < 80 && c.blue() < 80;
    }
    /// Show the point (x, y) of the poster (points) in the middle of the view
    void centreOn(double x, double y) {
        ViewController& vc = view->getViewController();
        const QRectF page = view->pageViewRect(0);
        const QPointF at = page.topLeft() + QPointF(x, y) * vc.zoom();
        vc.setScrollPosition(vc.scrollPosition() + at - QPointF(canvas->width() / 2, canvas->height() / 2));
    }

    static constexpr int STROKES = 300;
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
};
}  // namespace

// At 100 % the A0 page is 4966 x 7021 pixels here (35 megapixels, 140 MB), at 700 % 34762 x 49147: more than
// cairo can make (32767 a side) and a few GB. Only the part in view and around it is rendered then; the page stays
// sharp where it is looked at.
TEST_F(BigPageTest, anA0PageStaysWithinTheMemoryLimitsAtEveryZoom) {
    ViewController& vc = view->getViewController();
    ASSERT_NEAR(vc.zoom100(), 150.0 / 72.0, 1e-9);
    for (const double percent: {50.0, 100.0, 300.0, 700.0}) {
        SCOPED_TRACE(percent);
        vc.setZoom(vc.zoom100() * percent / 100, QPointF(canvas->width() / 2, canvas->height() / 2));
        centreOn(MARK_X - 10, MARK_Y - 10);  // (near the middle of the page)
        const qint64 took = rendered();
        ASSERT_GE(took, 0) << "the poster was not rendered at " << percent << " %";
        const auto info = view->getPage(0)->bufferInfo();
        const qint64 pixels = static_cast<qint64>(info.pixelSize.width()) * info.pixelSize.height();
        const double wholePage = A0_W * vc.zoom() * info.dpiScale * A0_H * vc.zoom() * info.dpiScale;
        std::cout << "[ A0 at " << percent << " % ] whole page " << std::lround(wholePage / 1e6)
                  << " Mpx; buffer " << info.pixelSize.width() << " x " << info.pixelSize.height() << " ("
                  << pixels * 4 / (1024 * 1024) << " MB), rendered in " << took << " ms; canvas holds "
                  << view->bufferBytes() / (1024 * 1024) << " MB" << std::endl;
        EXPECT_GT(pixels, 0);
        EXPECT_LE(pixels, MAX_BUFFER_PIXELS);
        EXPECT_LE(info.pixelSize.width(), MAX_BUFFER_SIDE);
        EXPECT_LE(info.pixelSize.height(), MAX_BUFFER_SIDE);
        EXPECT_LE(view->bufferBytes(), 2 * MAX_BUFFER_PIXELS * 4) << "the poster and the A4 page";
        EXPECT_GT(bluePixels(), 300) << "the lines in view are drawn";
        EXPECT_TRUE(markInPlace()) << "the part drawn is in its place";
    }
}

// Fit page shows the whole poster: the zoom goes below the usual 30 % when a page needs it.
TEST_F(BigPageTest, fitPageShowsAWholeA0Page) {
    ViewController& vc = view->getViewController();
    vc.fitPage(0, true);
    rendered();
    const QRectF page = view->pageViewRect(0);
    const QRectF item(0, 0, canvas->width(), canvas->height());
    EXPECT_TRUE(item.contains(page)) << "the page " << page.width() << " x " << page.height() << " in the view "
                                     << item.width() << " x " << item.height() << " at "
                                     << vc.zoom() / vc.zoom100() * 100 << " %";
    EXPECT_LT(vc.zoom(), 0.3 * vc.zoom100());
    // And zooming out by hand gets there as well
    vc.setZoom(vc.zoom100(), QPointF(0, 0));
    for (int i = 0; i < 40; ++i) {
        vc.zoomBy(0.8, QPointF(canvas->width() / 2, canvas->height() / 2));
    }
    EXPECT_LE(vc.zoom(), vc.fitWidthZoom(0)) << "out far enough to see the whole width";
    EXPECT_LE(A0_H * vc.zoom(), canvas->height()) << "and the whole height";
}

// Scrolling down the poster at 200 %: the frames stay short (tiles in view only, the rest shows the preview), and the
// renders follow the view instead of drawing the whole page.
TEST_F(BigPageTest, scrollingOverAnA0PageStaysFast) {
    ViewController& vc = view->getViewController();
    vc.setZoom(vc.zoom100() * 2, QPointF(0, 0));
    centreOn(A0_W / 2, 300);
    ASSERT_GE(rendered(), 0);
    canvas->forgetFrameStats();
    const auto before = PageRaster::stats();
    QElapsedTimer t;
    t.start();
    int steps = 0;
    const double bottom = A0_H * vc.zoom() - canvas->height();
    while (view->pageViewRect(0).top() > -bottom * 0.5 && t.elapsed() < 20000) {
        vc.panBy(QPointF(0, -40));  // (a fast drag: 40 px a frame)
        run(16);
        ++steps;
    }
    const qint64 scrollMs = t.elapsed();
    const auto stats = canvas->frameStats();
    const qint64 took = rendered();
    const auto after = PageRaster::stats();
    const long long renders = after.renders - before.renders;
    std::cout << "[ A0 scroll at 200 % ] " << steps << " steps in " << scrollMs << " ms; " << stats.frames
              << " frames, sync " << (stats.frames ? stats.syncNanos / stats.frames / 1000 : 0)
              << " us on average, " << stats.tiles << " tiles; " << renders << " renders of "
              << (renders ? (after.pixels - before.pixels) / renders / 1000 : 0) << " kpx, "
              << (renders ? (after.nanos - before.nanos) / renders / 1000000 : 0) << " ms each; sharp " << took
              << " ms after it stopped" << std::endl;
    ASSERT_GE(took, 0);
    EXPECT_GT(bluePixels(), 300);
    centreOn(MARK_X, MARK_Y);
    ASSERT_GE(rendered(), 0);
    EXPECT_TRUE(markInPlace());
    ASSERT_GT(stats.frames, 0);
    EXPECT_LT(stats.syncNanos / stats.frames, 20'000'000) << "20 ms a frame on average at most";
}
