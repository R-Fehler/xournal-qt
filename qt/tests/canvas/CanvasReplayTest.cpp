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

#include <cairo-pdf.h>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QClipboard>
#include <QImage>
#include <QBuffer>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QTouchEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/tools/EditSelection.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Font.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "config-test.h"
#include "TextEditor.h"

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
    QPointingDevice touchscreen{"test touchscreen", 1004, QInputDevice::DeviceType::TouchScreen,
                                QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0};
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

TEST_F(CanvasReplayTest, selectionHandlesAreBigEnoughForAFinger) {
    drawLine(0, QPointF(100, 300), QPointF(400, 500));
    drawLine(0, QPointF(120, 320), QPointF(380, 480));
    processEvents();
    view->selectAllOnPage();
    EditSelection* sel = view->getSelection();
    ASSERT_NE(sel, nullptr);
    const double zoom = view->getViewController().zoom();
    const double x1 = sel->getXOnView() * zoom;
    const double y1 = sel->getYOnView() * zoom;

    EXPECT_EQ(sel->getSelectionTypeForPos(x1, y1, zoom), CURSOR_SELECTION_TOP_LEFT);
    // A finger never lands exactly on the corner
    EXPECT_EQ(sel->getSelectionTypeForPos(x1 + 14, y1 + 14, zoom), CURSOR_SELECTION_TOP_LEFT)
            << "the corner reacts around it, not only on the pixel";
    EXPECT_EQ(sel->getSelectionTypeForPos(x1 - 14, y1 - 14, zoom), CURSOR_SELECTION_TOP_LEFT);
    // The middle still moves the selection
    EXPECT_EQ(sel->getSelectionTypeForPos(x1 + sel->getWidth() * zoom / 2, y1 + sel->getHeight() * zoom / 2, zoom),
              CURSOR_SELECTION_MOVE);
}

TEST_F(CanvasReplayTest, sideButtonOfThePenErases) {
    ToolHandler* th = app->getToolHandler();
    EXPECT_EQ(th->getToolType(), TOOL_PEN);
    drawLine(0, QPointF(100, 400), QPointF(300, 400));
    processEvents();
    ASSERT_EQ(elementCount(0), 1u);

    // Hold the lower side button: the eraser (the pen stays chosen in the tool bar)
    tablet(QEvent::TabletPress, viewPos(0, QPointF(90, 400)), 0.0, Qt::MiddleButton, Qt::MiddleButton);
    EXPECT_EQ(th->getToolType(), TOOL_ERASER) << "the side button erases";
    tablet(QEvent::TabletPress, viewPos(0, QPointF(90, 400)), 0.5, Qt::LeftButton,
           Qt::LeftButton | Qt::MiddleButton);
    for (int i = 1; i <= 20; ++i) {
        tablet(QEvent::TabletMove, viewPos(0, QPointF(90 + 11.0 * i, 400)), 0.5, Qt::NoButton,
               Qt::LeftButton | Qt::MiddleButton);
    }
    tablet(QEvent::TabletRelease, viewPos(0, QPointF(310, 400)), 0.0, Qt::LeftButton, Qt::MiddleButton);
    processEvents();
    EXPECT_EQ(elementCount(0), 0u) << "the stroke is erased";

    // Letting go: the tool of the tool bar again
    tablet(QEvent::TabletRelease, viewPos(0, QPointF(310, 400)), 0.0, Qt::MiddleButton, Qt::NoButton);
    EXPECT_EQ(th->getToolType(), TOOL_PEN);

    // The upper side button as well
    drawLine(0, QPointF(100, 500), QPointF(300, 500));
    processEvents();
    ASSERT_EQ(elementCount(0), 1u);
    tablet(QEvent::TabletPress, viewPos(0, QPointF(90, 500)), 0.0, Qt::RightButton, Qt::RightButton);
    EXPECT_EQ(th->getToolType(), TOOL_ERASER);
    tablet(QEvent::TabletRelease, viewPos(0, QPointF(90, 500)), 0.0, Qt::RightButton, Qt::NoButton);
    EXPECT_EQ(th->getToolType(), TOOL_PEN);
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

namespace {
// (The touch screen device lives in the test, not in a static: QPointingDevice must not outlive the application.)
void touch(CanvasInput& input, const QPointingDevice& screen, QEvent::Type type, QEventPoint::State state,
           QPointF pos) {
    QEventPoint p(1, state, pos, pos);
    QTouchEvent e(type, &screen, Qt::NoModifier, {p});
    input.touchEvent(&e, [](QPointF scene) { return scene; });
}

/// One finger dragging upwards; returns how far the view scrolled.
double fingerPan(CanvasInput& input, const QPointingDevice& screen, CanvasView& view) {
    auto& vc = view.getViewController();
    const double before = vc.visibleContentRect().top();
    touch(input, screen, QEvent::TouchBegin, QEventPoint::State::Pressed, QPointF(400, 400));
    for (int i = 1; i <= 10; ++i) {
        touch(input, screen, QEvent::TouchUpdate, QEventPoint::State::Updated, QPointF(400, 400 - 20 * i));
    }
    touch(input, screen, QEvent::TouchEnd, QEventPoint::State::Released, QPointF(400, 200));
    vc.stopMomentum();
    return vc.visibleContentRect().top() - before;
}
}  // namespace

TEST_F(CanvasReplayTest, aWebAddressInATextIsALink) {
    // A text with a link on the page
    auto text = std::make_unique<Text>();
    text->setText("see https://example.org/paper for the details");
    text->setFont(XojFont("Sans", 12));
    text->move(50, 100);
    const Text* raw = text.get();
    auto page = session->getDocument()->getPage(0);
    session->getDocument()->lock();
    page->getSelectedLayer()->addElement(std::move(text));
    session->getDocument()->unlock();
    processEvents();

    const auto& box = raw->getBoundingBox();
    const QPointF middle = viewPos(0, QPointF(box.x + box.width / 2, box.y + box.height / 2));
    const auto link = view->textLinkAt(middle);
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->uri, QString("https://example.org/paper"));
    EXPECT_EQ(link->page, -1) << "it leads out of the document";

    // Tapping it reports the link instead of doing nothing
    QSignalSpy tapped(view.get(), &CanvasView::linkTapped);
    EXPECT_TRUE(view->tapAt(middle));
    EXPECT_EQ(tapped.count(), 1);

    // Somewhere else on the page there is no link
    EXPECT_FALSE(view->textLinkAt(viewPos(0, QPointF(box.x + box.width + 80, box.y + 200))).has_value());
}

TEST_F(CanvasReplayTest, twoTapsZoomInAndOutAgain) {
    auto& vc = view->getViewController();
    vc.setViewSize(QSizeF(900, 600));
    processEvents();
    vc.fitWidth();
    processEvents();
    const double fitted = vc.zoom();

    auto tap = [&](QPointF pos) {
        touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, pos);
        touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, pos);
    };
    // Two taps in the same spot: closer (the page has no PDF columns, so the width of the page)
    tap(QPointF(400, 300));
    tap(QPointF(402, 302));
    processEvents();
    EXPECT_GE(vc.zoom(), fitted) << "the second tap zooms";

    // Zoomed in far: two taps go back to the whole page
    vc.setZoom(fitted * 3, QPointF(450, 300));
    processEvents();
    tap(QPointF(400, 300));
    tap(QPointF(402, 302));
    processEvents();
    EXPECT_LT(vc.zoom(), fitted * 3) << "and back out again";

    // A single tap alone changes nothing
    const double before = vc.zoom();
    tap(QPointF(300, 200));
    processEvents(400);
    EXPECT_DOUBLE_EQ(vc.zoom(), before);
}

// Palm rejection with a pen that reports proximity: no long dead time after writing (pinch/pan right away), but
// a hand that rests on the screen while the pen is near stays ignored.
TEST_F(CanvasReplayTest, touchWorksRightAfterThePenLeaves) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();

    input->proximityEvent(true);
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0) << "touch while the pen is near";
    input->proximityEvent(false);
    processEvents(200);
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "touch shortly after the pen left";
}

TEST_F(CanvasReplayTest, restingHandStaysIgnoredAfterThePenLeaves) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();

    input->proximityEvent(true);
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, QPointF(400, 400));  // palm
    input->proximityEvent(false);
    processEvents(200);
    const double before = view->getViewController().visibleContentRect().top();
    for (int i = 1; i <= 10; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, QPointF(400, 400 - 20 * i));
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, QPointF(400, 200));
    EXPECT_DOUBLE_EQ(view->getViewController().visibleContentRect().top(), before);
}

// Shape tools: upstream's shape handlers behind the pen (drawing type of the tool).
TEST_F(CanvasReplayTest, shapesThroughUpstreamHandlers) {
    ToolHandler* tools = app->getToolHandler();
    auto lastStroke = [&]() -> const Stroke* {
        const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
        return dynamic_cast<const Stroke*>(layer->getElementsView().back());
    };
    struct Case {
        DrawingType type;
        size_t minPoints, maxPoints;
    };
    for (const Case& c: {Case{DRAWING_TYPE_LINE, 2, 2}, Case{DRAWING_TYPE_RECTANGLE, 5, 5}, Case{DRAWING_TYPE_ARROW, 5, 7},
                         Case{DRAWING_TYPE_ELLIPSE, 20, 1000}, Case{DRAWING_TYPE_COORDINATE_SYSTEM, 3, 3}}) {
        tools->setDrawingType(c.type);
        const size_t before = elementCount(0);
        drawLine(0, QPointF(100, 100), QPointF(300, 200));
        processEvents();
        ASSERT_EQ(elementCount(0), before + 1) << "drawing type " << c.type;
        const Stroke* s = lastStroke();
        ASSERT_NE(s, nullptr);
        EXPECT_GE(s->getPointCount(), c.minPoints) << "drawing type " << c.type;
        EXPECT_LE(s->getPointCount(), c.maxPoints) << "drawing type " << c.type;
        const auto box = s->getBoundingBox();
        EXPECT_NEAR(box.x + box.width / 2, 200, 15) << "drawing type " << c.type;
    }
    tools->setDrawingType(DRAWING_TYPE_DEFAULT);
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 4u) << "each shape is one undo step";
}

TEST_F(CanvasReplayTest, shapeRecognizerStraightensARectangle) {
    app->getToolHandler()->setDrawingType(DRAWING_TYPE_SHAPE_RECOGNIZER);
    const std::vector<QPointF> corners{{100, 100}, {300, 102}, {302, 220}, {98, 218}, {100, 100}};
    tablet(QEvent::TabletPress, viewPos(0, corners[0]), 0.5, Qt::LeftButton, Qt::LeftButton);
    for (size_t k = 1; k < corners.size(); ++k) {
        for (int i = 1; i <= 20; ++i) {
            const double t = i / 20.0;
            tablet(QEvent::TabletMove, viewPos(0, corners[k - 1] + (corners[k] - corners[k - 1]) * t), 0.5,
                   Qt::NoButton, Qt::LeftButton);
        }
    }
    tablet(QEvent::TabletRelease, viewPos(0, corners.back()), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    app->getToolHandler()->setDrawingType(DRAWING_TYPE_DEFAULT);
    ASSERT_EQ(elementCount(0), 1u);
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const auto* s = dynamic_cast<const Stroke*>(layer->getElementsView().front());
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getPointCount(), 5u) << "recognized as a rectangle";
}

// Selection: upstream's selectors and EditSelection behind the Qt canvas.
TEST_F(CanvasReplayTest, rectangleSelectionMovesElementsWithUndo) {
    view->getViewController().setViewSize(QSizeF(1000, 1200));
    processEvents();
    drawLine(0, QPointF(100, 100), QPointF(200, 150));
    processEvents();
    ASSERT_EQ(elementCount(0), 1u);
    auto* tools = app->getToolHandler();

    tools->selectTool(TOOL_SELECT_RECT);
    drawLine(0, QPointF(80, 80), QPointF(250, 200));  // the rubber band
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_EQ(view->getSelection()->getElementsView().size(), 1u);
    EXPECT_EQ(elementCount(0), 0u) << "selected elements are held by the selection";

    drawLine(0, QPointF(150, 125), QPointF(250, 225));  // drag the selection
    processEvents();
    view->clearSelection();
    ASSERT_EQ(elementCount(0), 1u);
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    auto box = layer->getElementsView().front()->getBoundingBox();
    EXPECT_NEAR(box.x, 200, 5) << "moved by the drag";
    EXPECT_NEAR(box.y, 200, 5);

    session->getUndoRedoHandler()->undo();  // the move
    box = layer->getElementsView().front()->getBoundingBox();
    EXPECT_NEAR(box.x, 100, 5);
}

TEST_F(CanvasReplayTest, lassoTapDeleteCopyPaste) {
    view->getViewController().setViewSize(QSizeF(1000, 1200));
    processEvents();
    drawLine(0, QPointF(100, 100), QPointF(200, 150));
    drawLine(0, QPointF(400, 400), QPointF(450, 480));
    processEvents();
    auto* tools = app->getToolHandler();

    // Lasso around the first stroke only.
    tools->selectTool(TOOL_SELECT_REGION);
    const std::vector<QPointF> loop{{80, 80}, {260, 80}, {260, 180}, {80, 180}, {80, 82}};
    tablet(QEvent::TabletPress, viewPos(0, loop[0]), 0.5, Qt::LeftButton, Qt::LeftButton);
    for (size_t k = 1; k < loop.size(); ++k) {
        for (int i = 1; i <= 10; ++i) {
            tablet(QEvent::TabletMove, viewPos(0, loop[k - 1] + (loop[k] - loop[k - 1]) * (i / 10.0)), 0.5,
                   Qt::NoButton, Qt::LeftButton);
        }
    }
    tablet(QEvent::TabletRelease, viewPos(0, loop.back()), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_EQ(view->getSelection()->getElementsView().size(), 1u);
    EXPECT_EQ(elementCount(0), 1u);

    // Copy, paste: a second copy (as a new selection).
    EXPECT_TRUE(view->copySelection());
    EXPECT_TRUE(view->pasteElements());
    view->clearSelection();
    EXPECT_EQ(elementCount(0), 3u);

    // Tap on the second stroke selects it; delete (undoable).
    tools->selectTool(TOOL_SELECT_RECT);
    tablet(QEvent::TabletPress, viewPos(0, QPointF(425, 440)), 0.5, Qt::LeftButton, Qt::LeftButton);
    tablet(QEvent::TabletRelease, viewPos(0, QPointF(425, 440)), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_EQ(view->getSelection()->getElementsView().size(), 1u);
    view->deleteSelection();
    EXPECT_EQ(elementCount(0), 2u);
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 3u);

    // Select all on the page.
    view->selectAllOnPage();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_EQ(view->getSelection()->getElementsView().size(), 3u);
    view->clearSelection();
    EXPECT_EQ(elementCount(0), 3u);
}

namespace {
void typeInto(xqt::TextEditor& editor, const QString& text) {
    for (QChar c: text) {
        const int key = c == '\n' ? Qt::Key_Return : c.toUpper().unicode();
        QKeyEvent e(QEvent::KeyPress, key, Qt::NoModifier, c == '\n' ? QString() : QString(c));
        bool finish = false;
        editor.keyPressed(&e, finish);
    }
}
void pressKey(xqt::TextEditor& editor, int key, Qt::KeyboardModifiers m = Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress, key, m);
    bool finish = false;
    editor.keyPressed(&e, finish);
}
}  // namespace

TEST_F(CanvasReplayTest, textToolWritesAndEditsText) {
    view->getViewController().setViewSize(QSizeF(1000, 1200));
    processEvents();
    app->getToolHandler()->selectTool(TOOL_TEXT);
    auto tap = [&](QPointF p) {
        tablet(QEvent::TabletPress, viewPos(0, p), 0.5, Qt::LeftButton, Qt::LeftButton);
        tablet(QEvent::TabletRelease, viewPos(0, p), 0.0, Qt::LeftButton, Qt::NoButton);
        processEvents();
    };
    auto onlyText = [&]() -> const Text* {
        const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
        return layer->getElementsView().size() == 0 ? nullptr
                                                      : dynamic_cast<const Text*>(layer->getElementsView().front());
    };

    tap(QPointF(100, 100));
    ASSERT_NE(view->getTextEditor(), nullptr);
    typeInto(*view->getTextEditor(), "Hello\nWorld");
    QInputMethodEvent im;  // an accented letter from the on-screen keyboard / a dead key
    im.setCommitString(QString::fromUtf8(" é"));
    view->getTextEditor()->inputMethodEvent(&im);
    view->endTextEditing();
    ASSERT_NE(onlyText(), nullptr);
    EXPECT_EQ(onlyText()->getText(), "Hello\nWorld é");

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(onlyText(), nullptr);
    session->getUndoRedoHandler()->redo();
    ASSERT_NE(onlyText(), nullptr);

    // Edit: tap on the text, go to the end, add.
    const auto box = onlyText()->getBoundingBox();
    tap(QPointF(box.x + 5, box.y + 5));
    ASSERT_NE(view->getTextEditor(), nullptr);
    EXPECT_TRUE(onlyText()->isInEditing()) << "the original is hidden while editing";
    pressKey(*view->getTextEditor(), Qt::Key_End, Qt::ControlModifier);
    typeInto(*view->getTextEditor(), "!");
    view->endTextEditing();
    ASSERT_NE(onlyText(), nullptr);
    EXPECT_EQ(onlyText()->getText(), "Hello\nWorld é!");
    EXPECT_FALSE(onlyText()->isInEditing());
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(onlyText()->getText(), "Hello\nWorld é");

    // Emptying a text deletes it (undoable).
    tap(QPointF(box.x + 5, box.y + 5));
    pressKey(*view->getTextEditor(), Qt::Key_A, Qt::ControlModifier);
    pressKey(*view->getTextEditor(), Qt::Key_Backspace);
    view->endTextEditing();
    EXPECT_EQ(onlyText(), nullptr);
    session->getUndoRedoHandler()->undo();
    ASSERT_NE(onlyText(), nullptr);
    EXPECT_FALSE(onlyText()->isInEditing());

    // Tapping into a text and out again changes nothing (no undo step).
    const bool couldRedo = session->getUndoRedoHandler()->canRedo();
    tap(QPointF(box.x + 5, box.y + 5));
    view->endTextEditing();
    EXPECT_EQ(session->getUndoRedoHandler()->canRedo(), couldRedo);
}

TEST_F(CanvasReplayTest, insertedImageIsSelectedAndFitsTheView) {
    view->getViewController().setViewSize(QSizeF(800, 600));
    processEvents();
    QImage big(4000, 2000, QImage::Format_RGB32);
    big.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    ASSERT_TRUE(big.save(&buffer, "PNG"));

    ASSERT_TRUE(view->insertImage(png));
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_EQ(view->getSelection()->getElementsView().size(), 1u);
    view->clearSelection();
    ASSERT_EQ(elementCount(0), 1u);
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const Element* e = layer->getElementsView().front();
    EXPECT_EQ(e->getType(), ELEMENT_IMAGE);
    const auto box = e->getBoundingBox();
    const auto* page = session->getDocument()->getPage(0).get();
    EXPECT_LE(box.width, page->getWidth()) << "scaled down to fit";
    EXPECT_NEAR(box.width / box.height, 2.0, 0.01) << "aspect ratio kept";
    EXPECT_GE(box.x, 0);

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 0u);
    EXPECT_FALSE(view->insertImage(QByteArray("not an image")));
}

TEST_F(CanvasReplayTest, aTwoColumnPageIsZoomedColumnByColumn) {
    // A page with two columns of text
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("columns.pdf").toStdString();
    {
        cairo_surface_t* surface = cairo_pdf_surface_create(pdf.c_str(), 600, 800);
        cairo_t* cr = cairo_create(surface);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        for (int line = 0; line < 30; ++line) {
            const double y = 80 + line * 20;
            cairo_move_to(cr, 60, y);
            cairo_show_text(cr, "the left column of the page");
            cairo_move_to(cr, 330, y);
            cairo_show_text(cr, "the right column of it");
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    input.reset();
    view.reset();
    auto loaded = DocumentSession::loadFile(fs::path(pdf));
    ASSERT_TRUE(loaded.document) << loaded.error;
    session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    view = std::make_unique<CanvasView>(*session);
    view->getViewController().setViewSize(QSizeF(900, 1200));
    input = std::make_unique<CanvasInput>(*view);
    processEvents();

    const auto left = view->textColumnAt(0, QPointF(120, 300));
    ASSERT_TRUE(left.has_value()) << "the left column";
    EXPECT_LT(left->width(), 300) << "not the whole page";
    EXPECT_LT(left->right(), 320);
    const auto right = view->textColumnAt(0, QPointF(420, 300));
    ASSERT_TRUE(right.has_value());
    EXPECT_GT(right->left(), 300);

    // Two taps on the right column: it fills the view
    auto& vc = view->getViewController();
    vc.fitWidth();
    processEvents();
    const double fitted = vc.zoom();
    const QPointF onRightColumn = viewPos(0, QPointF(420, 300));
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, onRightColumn);
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, onRightColumn);
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, onRightColumn + QPointF(2, 2));
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, onRightColumn + QPointF(2, 2));
    processEvents();
    EXPECT_GT(vc.zoom(), fitted * 1.3) << "the column is bigger than the page width";
}

// PDF text tools: select text of the background PDF and mark it (upstream's PdfElemSelection + marker strokes).
TEST_F(CanvasReplayTest, pdfTextIsHighlightedByDraggingOverIt) {
    // Use a document with a PDF background instead of the fixture's blank one.
    input.reset();
    view.reset();
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp"));
    ASSERT_TRUE(loaded.document);
    session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    view = std::make_unique<CanvasView>(*session);
    view->getViewController().setViewSize(QSizeF(900, 1200));
    input = std::make_unique<CanvasInput>(*view);
    processEvents();

    // Where is "Test PDF" on page 1?
    QSignalSpy searched(&session->search(), &DocumentSearch::finished);
    session->search().setQuery("Test PDF", false);
    ASSERT_TRUE(searched.wait(3000));
    ASSERT_FALSE(session->search().hits().empty());
    const QRectF hit = session->search().hits().front().rect;
    session->search().clear();

    auto dragOver = [&] {
        tablet(QEvent::TabletPress, viewPos(0, QPointF(hit.left() - 2, hit.center().y())), 0.5, Qt::LeftButton,
               Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) {
            tablet(QEvent::TabletMove, viewPos(0, QPointF(hit.left() - 2 + (hit.width() + 4) * i / 10.0, hit.center().y())),
                   0.5, Qt::NoButton, Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, viewPos(0, QPointF(hit.right() + 2, hit.center().y())), 0.0, Qt::LeftButton,
               Qt::NoButton);
        processEvents();
    };

    app->getToolHandler()->selectTool(TOOL_SELECT_PDF_TEXT_LINEAR);
    view->setPdfTextMode(CanvasView::PdfTextMode::Highlight);
    dragOver();
    const size_t marks = elementCount(0);
    ASSERT_GE(marks, 1u) << "highlight strokes over the text";
    const auto* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const auto* s = dynamic_cast<const Stroke*>(layer->getElementsView().front());
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getToolType(), StrokeTool::HIGHLIGHTER);
    EXPECT_FALSE(view->hasPdfTextSelection()) << "marked right away";
    EXPECT_EQ(s->getColor(), app->getToolHandler()->getTool(TOOL_HIGHLIGHTER).getColor())
            << "no highlight color chosen: the highlighter's";
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), 0u) << "one undo step for all lines";

    // A chosen highlight color (one of the presets)
    view->setPdfHighlightColor(Color(0xff80c0u));
    dragOver();
    ASSERT_GE(elementCount(0), 1u);
    EXPECT_EQ(dynamic_cast<const Stroke*>(layer->getElementsView().front())->getColor(), Color(0xff80c0u));
    session->getUndoRedoHandler()->undo();
    view->setPdfHighlightColor(std::nullopt);

    // Select mode: the selection stays for copying.
    view->setPdfTextMode(CanvasView::PdfTextMode::Select);
    QSignalSpy selected(view.get(), &CanvasView::pdfTextSelected);
    dragOver();
    EXPECT_TRUE(view->hasPdfTextSelection());
    EXPECT_EQ(selected.count(), 1);
    EXPECT_TRUE(view->copyPdfText());
    EXPECT_TRUE(QGuiApplication::clipboard()->text().contains("Test"));
    EXPECT_TRUE(view->markPdfText(CanvasView::PdfTextMode::Underline));
    EXPECT_EQ(elementCount(0), marks);
}
