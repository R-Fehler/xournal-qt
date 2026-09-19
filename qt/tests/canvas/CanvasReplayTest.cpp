/*
 * xournal-qt: replay synthetic pen input through the whole canvas pipeline:
 * Qt tablet events -> CanvasInput (port of PenInputHandler) -> CanvasPage (port of XojPageView)
 * -> upstream StrokeHandler / EraseHandler -> document + undo, and the page raster.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"

using namespace xqt;

namespace {
class CanvasReplayTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);  // two pages
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 2400));
        input = std::make_unique<CanvasInput>(*view);
        app->getToolHandler()->selectTool(TOOL_PEN);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }

    void processEvents(int ms = 50) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    /// View position of a point on a page (in points).
    QPointF viewPos(size_t page, QPointF pagePoint) const {
        const QRectF r = view->pageViewRect(page);
        return r.topLeft() + pagePoint * view->getViewController().zoom();
    }

    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons,
                const QPointingDevice* device = nullptr) {
        QTabletEvent e(type, device ? device : &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier,
                       button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }

    /// A pressure stroke along a line (points in page coordinates), like a real pen at 200 Hz.
    void drawLine(size_t page, QPointF from, QPointF to, int steps = 30, const QPointingDevice* device = nullptr) {
        tablet(QEvent::TabletPress, viewPos(page, from), 0.3, Qt::LeftButton, Qt::LeftButton, device);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            tablet(QEvent::TabletMove, viewPos(page, from + (to - from) * t), 0.3 + 0.5 * t, Qt::NoButton,
                   Qt::LeftButton, device);
        }
        tablet(QEvent::TabletRelease, viewPos(page, to), 0.0, Qt::LeftButton, Qt::NoButton, device);
    }

    size_t elementCount(size_t page) const {
        auto p = session->getDocument()->getPage(page);
        size_t n = 0;
        for (const Layer* l: p->getLayersView()) {
            n += l->getElementsView().size();
        }
        return n;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    QPointingDevice eraser{"test eraser", 1002, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Eraser,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    QPointingDevice touchpad{"test touchpad", 1003, QInputDevice::DeviceType::TouchPad,
                             QPointingDevice::PointerType::Finger,
                             QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 2, 0};
    ulong timestamp = 1000;
};
}  // namespace

TEST_F(CanvasReplayTest, penStrokeGoesThroughUpstreamStrokeHandler) {
    drawLine(0, QPointF(100, 100), QPointF(300, 180));
    processEvents();

    ASSERT_EQ(elementCount(0), 1u);
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const auto* stroke = dynamic_cast<const Stroke*>(layer->getElementsView().front());
    ASSERT_NE(stroke, nullptr);
    EXPECT_GE(stroke->getPointCount(), 20u);
    EXPECT_TRUE(stroke->hasPressure());
    EXPECT_EQ(stroke->getToolType(), StrokeTool::PEN);
    // The stroke lies where the pen went (page coordinates).
    const auto box = stroke->getBoundingBox();
    EXPECT_NEAR(box.x, 100, 3);
    EXPECT_NEAR(box.y, 100, 3);
    EXPECT_NEAR(box.x + box.width, 300, 3);

    EXPECT_TRUE(session->isModified());
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 0u);
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(elementCount(0), 1u);
}

TEST_F(CanvasReplayTest, strokeIsPaintedIntoThePageBuffer) {
    processEvents(100);
    auto* page = view->getPage(0);
    auto before = page->bufferInfo();
    ASSERT_TRUE(before.valid) << "visible page was not rendered";
    drawLine(0, QPointF(100, 300), QPointF(400, 300));
    processEvents();
    const double s = before.zoom * before.dpiScale;
    const QImage tile = page->composeTile(QRect(static_cast<int>(90 * s), static_cast<int>(290 * s),
                                                static_cast<int>(320 * s), static_cast<int>(20 * s)));
    int dark = 0;
    for (int y = 0; y < tile.height(); ++y) {
        for (int x = 0; x < tile.width(); ++x) {
            dark += qGray(tile.pixel(x, y)) < 128;
        }
    }
    EXPECT_GT(dark, 50) << "no ink found where the stroke was drawn";
}

TEST_F(CanvasReplayTest, eraserEndDeletesStrokes) {
    drawLine(0, QPointF(100, 100), QPointF(300, 100));
    drawLine(0, QPointF(100, 200), QPointF(300, 200));
    processEvents();
    ASSERT_EQ(elementCount(0), 2u);

    // The eraser end (pointerType Eraser) switches to the eraser tool via ButtonConfig, like the side button of
    // the user's AES pen. The default eraser button tool erases whole strokes or parts, depending on the settings.
    drawLine(0, QPointF(200, 60), QPointF(200, 140), 20, &eraser);
    processEvents();
    EXPECT_NE(elementCount(0), 2u) << "the first stroke was not touched by the eraser";
    // After the eraser, the pen draws again with the toolbar tool.
    EXPECT_EQ(app->getToolHandler()->getToolType(), TOOL_PEN);

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 2u);
}

TEST_F(CanvasReplayTest, strokeCrossingPagesIsSplit) {
    const QRectF p0 = view->pageViewRect(0);
    const double zoom = view->getViewController().zoom();
    const double h0 = p0.height() / zoom;
    // From near the bottom of page 0 to the top of page 1 (through the gap between the pages).
    const QPointF start = viewPos(0, QPointF(200, h0 - 40));
    const QPointF end = viewPos(1, QPointF(200, 40));
    tablet(QEvent::TabletPress, start, 0.5, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 40; ++i) {
        tablet(QEvent::TabletMove, start + (end - start) * (i / 40.0), 0.5, Qt::NoButton, Qt::LeftButton);
    }
    tablet(QEvent::TabletRelease, end, 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(elementCount(0), 1u);
    EXPECT_EQ(elementCount(1), 1u);
}

TEST_F(CanvasReplayTest, highlighterAndWhiteout) {
    app->getToolHandler()->selectTool(TOOL_HIGHLIGHTER);
    drawLine(0, QPointF(100, 400), QPointF(300, 400));
    app->getToolHandler()->selectTool(TOOL_ERASER);
    app->getToolHandler()->setEraserType(ERASER_TYPE_WHITEOUT);
    drawLine(0, QPointF(100, 500), QPointF(300, 500));
    processEvents();
    ASSERT_EQ(elementCount(0), 2u);
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    auto it = layer->getElementsView().begin();
    EXPECT_EQ(dynamic_cast<const Stroke*>(*it)->getToolType(), StrokeTool::HIGHLIGHTER);
    ++it;
    EXPECT_EQ(dynamic_cast<const Stroke*>(*it)->getToolType(), StrokeTool::ERASER);  // whiteout stroke
}

namespace {
void sendWheel(CanvasInput& input, const QPointingDevice& touchpad, QPoint pixelDelta, Qt::ScrollPhase phase) {
    QWheelEvent e(QPointF(400, 300), QPointF(400, 300), pixelDelta, pixelDelta * 2, Qt::NoButton, Qt::NoModifier, phase,
                  false, Qt::MouseEventNotSynthesized, &touchpad);
    input.wheelEvent(&e, QPointF(400, 300));
}
}  // namespace

TEST_F(CanvasReplayTest, touchpadScrollContinuesWithMomentumAfterLift) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);  // make the document scrollable
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();
    auto& vc = view->getViewController();
    const double startY = vc.visibleContentRect().top();

    sendWheel(*input, touchpad, QPoint(0, -20), Qt::ScrollBegin);
    for (int i = 0; i < 8; ++i) {
        QThread::msleep(10);
        sendWheel(*input, touchpad, QPoint(0, -20), Qt::ScrollUpdate);
    }
    const double atLift = vc.visibleContentRect().top();
    EXPECT_NEAR(atLift - startY, 180, 1) << "two-finger scrolling moves the content by the deltas";
    sendWheel(*input, touchpad, QPoint(0, 0), Qt::ScrollEnd);
    processEvents(300);
    EXPECT_GT(vc.visibleContentRect().top(), atLift + 50) << "no momentum after lifting the fingers";

    // Fingers down again stop the fling.
    sendWheel(*input, touchpad, QPoint(0, 0), Qt::ScrollBegin);
    const double stopped = vc.visibleContentRect().top();
    processEvents(100);
    EXPECT_DOUBLE_EQ(vc.visibleContentRect().top(), stopped);
}

TEST_F(CanvasReplayTest, mouseWheelHasNoMomentum) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();
    auto& vc = view->getViewController();
    const double startY = vc.visibleContentRect().top();
    QWheelEvent e(QPointF(400, 300), QPointF(400, 300), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                  Qt::NoScrollPhase, false);
    input->wheelEvent(&e, QPointF(400, 300));
    const double after = vc.visibleContentRect().top();
    EXPECT_NEAR(after - startY, 48, 1);
    processEvents(200);
    EXPECT_DOUBLE_EQ(vc.visibleContentRect().top(), after);
}
