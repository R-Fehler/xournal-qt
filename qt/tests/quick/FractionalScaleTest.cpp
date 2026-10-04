/*
 * xournal-qt: the canvas on a screen at 125 %, 150 % (qt/docs/hidpi.md). Its pictures (page tiles, the selection,
 * the curtain's knob) are drawn with the screen's pixels and land on whole device pixels, so they are as sharp as at
 * 100 %. CTest runs these once as they are and once with QT_SCALE_FACTOR=1.25 (FractionalScaleCanvas.quick@125).
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "CurtainLayer.h"
#include "DevicePixels.h"
#include "DocumentCanvasItem.h"

using namespace xqt;

namespace {
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 1000; height: 800; visible: true
    background: Rectangle { color: "#5f6368" }
    DocumentCanvas { objectName: "canvas"; anchors.fill: parent }
}
)";

bool whole(double devicePixels) {
    return std::abs(devicePixels - std::round(devicePixels)) < 1e-3;
}  // (scene graph rects are floats)
/// Its corners on whole device pixels
bool onDevicePixels(QRectF r, double dpr) {
    return whole(r.left() * dpr) && whole(r.top() * dpr) && whole(r.right() * dpr) && whole(r.bottom() * dpr);
}

class FractionalScaleCanvas: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);
        view = std::make_unique<CanvasView>(*session);
        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        dpr = window->effectiveDevicePixelRatio();
        settle(300);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
    }
    void settle(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// A zoom and scroll position that put the first page at a fraction of a pixel (as most zooms do)
    void zoomToAnOddPlace() {
        ViewController& vc = view->getViewController();
        vc.setZoom(1.137, QPointF(0, 0));
        vc.setScrollPosition(QPointF(-30.37, -20.71));
        settle(500);
        const QPointF at = view->pageViewRect(0).topLeft();
        ASSERT_FALSE(whole(at.x() * dpr) && whole(at.y() * dpr)) << "the page lies on whole pixels: no test";
    }
    void addStroke(QPointF from, QPointF to) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(1.0);
        stroke->setColor(Color(0xff000000U));
        stroke->addPoint(Point(from.x(), from.y(), -1));
        stroke->addPoint(Point(to.x(), to.y(), -1));
        session->getDocument()->getPage(0)->getSelectedLayer()->addElement(std::move(stroke));
        session->firePageChanged(0);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
    double dpr = 1;
};
}  // namespace

// The page's picture has the screen's pixels (its width at the zoom times the pixel ratio, cut at the last whole
// pixel as cairo does) and its tiles land on whole device pixels: the window shows the rendered pixels unchanged,
// not blended with their neighbours.
TEST_F(FractionalScaleCanvas, pageTilesAreShownPixelForPixel) {
    zoomToAnOddPlace();
    addStroke(QPointF(100, 100.3), QPointF(300, 100.3));
    addStroke(QPointF(100.6, 60), QPointF(100.6, 160));
    settle(300);
    CanvasPage* page = view->getPage(0);
    const auto info = page->bufferInfo();
    ASSERT_TRUE(info.valid);
    ASSERT_TRUE(info.whole);
    EXPECT_DOUBLE_EQ(info.dpiScale, dpr);
    const QSizeF size(page->getPage()->getWidth(), page->getPage()->getHeight());
    EXPECT_NEAR(info.pixelSize.width(), size.width() * info.zoom * dpr, 1.0 + dpr) << "the screen's pixels";
    EXPECT_NEAR(info.pixelSize.height(), size.height() * info.zoom * dpr, 1.0 + dpr);

    // The page's top left on the screen, in device pixels: a whole one
    const QPointF at = canvas->mapToScene(view->pageViewRect(0).topLeft());
    const QPoint topLeft(static_cast<int>(std::round(at.x() * dpr)), static_cast<int>(std::round(at.y() * dpr)));
    // A block around the strokes' corner (buffer pixels), as composed for the tiles, and as the window shows it
    const QRect block(QPoint(static_cast<int>(90 * info.zoom * dpr), static_cast<int>(90 * info.zoom * dpr)),
                      QSize(48, 48));
    const QImage tile = page->composeTile(block).convertToFormat(QImage::Format_RGB32);
    const QImage shot = window->grabWindow().convertToFormat(QImage::Format_RGB32);
    int differing = 0, inked = 0;
    for (int y = 0; y < block.height(); ++y) {
        for (int x = 0; x < block.width(); ++x) {
            const QRgb want = tile.pixel(x, y);
            const QRgb got = shot.pixel(topLeft + block.topLeft() + QPoint(x, y));
            differing += std::abs(qGray(want) - qGray(got)) > 2;
            inked += qGray(want) < 128;
        }
    }
    EXPECT_GT(inked, 20) << "the block has the strokes";
    EXPECT_EQ(differing, 0) << "pixels blended: the tiles are not on whole device pixels (dpr " << dpr << ")";
}

// The selection (its frame and handles) is a picture of its own over the page. It is drawn with whole device pixels
// for the pixel ratio of the frame, and placed on whole device pixels, as the page's tiles are. It was placed at the
// page's fractional position (blurred by the texture's filtering, at any scale) and, at 125 % or 150 %, drawn a
// fraction of a pixel narrower than shown.
TEST_F(FractionalScaleCanvas, theSelectionIsDrawnOnWholeDevicePixels) {
    zoomToAnOddPlace();
    addStroke(QPointF(120, 140), QPointF(260, 210));
    settle(100);
    view->selectAllOnPage();
    ASSERT_NE(view->getSelection(), nullptr);
    canvas->update();
    settle(200);
    const auto shown = canvas->selectionShown();
    ASSERT_TRUE(shown.shown);
    EXPECT_DOUBLE_EQ(shown.dpr, dpr);
    EXPECT_NEAR(shown.pixels.width(), shown.rect.width() * dpr, 1e-6) << "one pixel of it for each of the screen";
    EXPECT_NEAR(shown.pixels.height(), shown.rect.height() * dpr, 1e-6);
    const QPointF at = canvas->mapToScene(shown.rect.topLeft());
    EXPECT_TRUE(whole(at.x() * dpr) && whole(at.y() * dpr))
            << "at " << at.x() * dpr << ", " << at.y() * dpr << " device pixels";
}

// The curtain's handles (squares with a white inside) and its knob have whole device pixels: the squares' frames
// are equally thick on all sides and the knob is not blurred, wherever the curtain is moved.
TEST_F(FractionalScaleCanvas, theCurtainsHandlesAndKnobLieOnWholeDevicePixels) {
    zoomToAnOddPlace();
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Curtain);
    curtain.place(QPointF(300.37, 300.21), QSizeF(200.3, 120.7), 0);
    curtain.setHandlesShown(true);
    canvas->update();
    settle(200);
    const auto shown = canvas->curtainShown();
    ASSERT_TRUE(shown.shown);
    ASSERT_FALSE(shown.handleFrames.empty());
    for (size_t i = 0; i < shown.handleFrames.size(); ++i) {
        EXPECT_TRUE(onDevicePixels(shown.handleFrames[i], dpr))
                << "handle " << i << " at " << shown.handleFrames[i].x() * dpr;
        EXPECT_TRUE(onDevicePixels(shown.handleFills[i], dpr)) << "its inside";
    }
    ASSERT_FALSE(shown.knob.isEmpty());
    EXPECT_TRUE(onDevicePixels(shown.knob, dpr)) << "the knob at " << shown.knob.x() * dpr;
    EXPECT_NEAR(shown.knobPixels.width(), shown.knob.width() * dpr, 1e-6) << "one pixel of it for each of the screen";
}

// The window moves to a screen of another scale (Windows and Plasma with two monitors, a projector): the page, the
// selection and the curtain's knob are drawn anew for its pixels. (The knob was made once, for the first screen.)
TEST_F(FractionalScaleCanvas, aScreenOfAnotherScaleGetsPicturesForItsPixels) {
    addStroke(QPointF(120, 140), QPointF(260, 210));
    settle(100);
    view->selectAllOnPage();
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Curtain);
    curtain.place(QPointF(300, 450), QSizeF(200, 120), 0);
    curtain.setHandlesShown(true);
    canvas->update();
    settle(200);
    ASSERT_TRUE(canvas->selectionShown().shown);
    ASSERT_FALSE(canvas->curtainShown().knob.isEmpty());

#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    GTEST_SKIP() << "Qt 6.6 tells windows about a new pixel ratio";
#else
    // As when the window goes to a screen at 175 % of this one (what the platform tells Qt then)
    const auto rescale = [&](double factor) {
        QHighDpiScaling::setScreenFactor(window->screen(), factor);
        QWindowSystemInterface::handleWindowDevicePixelRatioChanged(window);
        QWindowSystemInterface::flushWindowSystemEvents();
    };
    rescale(1.75);
    struct Restore {
        std::function<void(double)> rescale;
        ~Restore() { rescale(1.0); }
    } restore{rescale};
    const double now = window->effectiveDevicePixelRatio();
    ASSERT_NEAR(now, dpr * 1.75, 1e-9) << "the window got the new pixel ratio";
    canvas->update();
    settle(400);
    const auto info = view->getPage(0)->bufferInfo();
    EXPECT_TRUE(info.valid);
    EXPECT_DOUBLE_EQ(info.dpiScale, now) << "the page is drawn for it";
    const auto selection = canvas->selectionShown();
    EXPECT_DOUBLE_EQ(selection.dpr, now) << "the selection";
    EXPECT_NEAR(selection.pixels.width(), selection.rect.width() * now, 1e-6);
    const auto shown = canvas->curtainShown();
    EXPECT_NEAR(shown.knobPixels.width(), shown.knob.width() * now, 1e-6) << "the curtain's knob";
#endif
}
