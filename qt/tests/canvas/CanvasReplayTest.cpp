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
#include <functional>

#include <QMouseEvent>
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
#include "model/GeometryTool.h"
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
#include "PenHover.h"
#include "../SearchHits.h"
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
        PenHover::instance().reset();  // (one for the whole application)
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

    void mouse(QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, pos, pos, button, buttons, Qt::NoModifier, &mousePointer);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->mouseEvent(&e, pos);
    }

    /// The pen hovering at this place, at this height (0 on the screen, 1 as far up as it can tell).
    void hover(QPointF pos, double height) {
        QTabletEvent e(QEvent::TabletMove, &pen, pos, pos, 0.0, 0.f, 0.f, 0.f, 0.0,
                       static_cast<float>(height * PenHover::MAX_Z), Qt::NoModifier, Qt::NoButton, Qt::NoButton);
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
    QPointingDevice mousePointer{"test mouse", 1005, QInputDevice::DeviceType::Mouse,
                                 QPointingDevice::PointerType::Generic, QInputDevice::Capability::Position, 3, 3};
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

/// Two fingers at these places.
void touch2(CanvasInput& input, const QPointingDevice& screen, QEvent::Type type, QEventPoint::State state,
            QPointF a, QPointF b) {
    QEventPoint p1(1, state, a, a);
    QEventPoint p2(2, state, b, b);
    QTouchEvent e(type, &screen, Qt::NoModifier, {p1, p2});
    input.touchEvent(&e, [](QPointF scene) { return scene; });
}

struct Finger {
    int id;
    QEventPoint::State state;
    QPointF pos;
};
/// Any number of fingers in one event (view coordinates).
void touchN(CanvasInput& input, const QPointingDevice& screen, QEvent::Type type, const std::vector<Finger>& fingers) {
    QList<QEventPoint> points;
    for (const Finger& f: fingers) {
        points.append(QEventPoint(f.id, f.state, f.pos, f.pos));
    }
    QTouchEvent e(type, &screen, Qt::NoModifier, points);
    input.touchEvent(&e, [](QPointF scene) { return scene; });
}

/// Where two fingers are at step `i` of `steps` (view coordinates).
using FingerPath = std::function<std::pair<QPointF, QPointF>(int i, int steps)>;

/// A two-finger gesture as a hand makes it: one finger lands, then the other; both move along `path`; then they
/// are lifted one after the other.
void twoFingerGesture(CanvasInput& input, const QPointingDevice& screen, const FingerPath& path, int steps = 12) {
    using S = QEventPoint::State;
    const auto [a0, b0] = path(0, steps);
    touchN(input, screen, QEvent::TouchBegin, {{1, S::Pressed, a0}});
    touchN(input, screen, QEvent::TouchUpdate, {{1, S::Stationary, a0}, {2, S::Pressed, b0}});
    for (int i = 1; i <= steps; ++i) {
        const auto [a, b] = path(i, steps);
        touchN(input, screen, QEvent::TouchUpdate, {{1, S::Updated, a}, {2, S::Updated, b}});
    }
    const auto [a1, b1] = path(steps, steps);
    touchN(input, screen, QEvent::TouchUpdate, {{1, S::Stationary, a1}, {2, S::Released, b1}});
    touchN(input, screen, QEvent::TouchEnd, {{1, S::Released, a1}});
}

QPointF turned(QPointF p, QPointF around, double angle) {
    const QPointF d = p - around;
    return around + QPointF(d.x() * std::cos(angle) - d.y() * std::sin(angle),
                            d.x() * std::sin(angle) + d.y() * std::cos(angle));
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

TEST_F(CanvasReplayTest, pastedTextLandsWhereItWasPasted) {
    QGuiApplication::clipboard()->setText("from somewhere else");
    const QPointF where = viewPos(0, QPointF(120, 220));
    ASSERT_TRUE(view->pasteElements(where));
    processEvents();
    const Text* pasted = nullptr;
    for (const auto& element: session->getDocument()->getPage(0)->getSelectedLayer()->getElements()) {
        if (element->getType() == ELEMENT_TEXT) {
            pasted = static_cast<const Text*>(element.get());
        }
    }
    ASSERT_NE(pasted, nullptr) << "the text is on the page";
    EXPECT_EQ(pasted->getText(), "from somewhere else");
    EXPECT_NEAR(pasted->getBoundingBox().x, 120, 3);
    EXPECT_NEAR(pasted->getBoundingBox().y, 220, 3);
}

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

// The handles of a selection are made for fingers, so a finger must work them: inside it moves, a corner resizes,
// and the page does not scroll away under it.
TEST_F(CanvasReplayTest, aFingerMovesAndResizesTheSelection) {
    auto& vc = view->getViewController();
    vc.setViewSize(QSizeF(900, 1200));
    processEvents();
    drawLine(0, QPointF(100, 300), QPointF(400, 500));
    drawLine(0, QPointF(120, 320), QPointF(380, 480));
    processEvents();
    view->selectAllOnPage();
    ASSERT_NE(view->getSelection(), nullptr);
    const double zoom = vc.zoom();
    const double scrolled = vc.visibleContentRect().top();
    const auto box = [&] {
        EditSelection* s = view->getSelection();
        return QRectF(s->getXOnView(), s->getYOnView(), s->getWidth(), s->getHeight());
    };
    const QRectF before = box();

    // A finger in the middle of the selection takes it along
    const QPointF middle = viewPos(0, before.center());
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, middle);
    for (int i = 1; i <= 6; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated,
              middle + QPointF(10 * i, 5 * i));
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, middle + QPointF(60, 30));
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr) << "it stays selected";
    const QRectF moved = box();
    EXPECT_NEAR(moved.x() - before.x(), 60 / zoom, 2) << "it followed the finger";
    EXPECT_NEAR(moved.y() - before.y(), 30 / zoom, 2);
    EXPECT_NEAR(moved.width(), before.width(), 0.5) << "moving does not resize it";
    EXPECT_DOUBLE_EQ(vc.visibleContentRect().top(), scrolled) << "and the page did not scroll";

    // A finger on the bottom right corner makes it bigger
    const QPointF corner = viewPos(0, moved.bottomRight());
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, corner);
    for (int i = 1; i <= 6; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, corner + QPointF(15 * i, 0));
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, corner + QPointF(90, 0));
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_GT(box().width(), moved.width() + 10) << "the corner resized it";
    EXPECT_DOUBLE_EQ(vc.visibleContentRect().top(), scrolled);

    // Held still on it, or dragged slowly for longer than a long press takes: no menu (its pill has the actions)
    QSignalSpy menu(view.get(), &CanvasView::contextRequested);
    const QPointF held = viewPos(0, box().center());
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, held);
    processEvents(700);
    for (int i = 1; i <= 5; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, held + QPointF(0, 4 * i));
        processEvents(150);
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, held + QPointF(0, 20));
    processEvents();
    EXPECT_EQ(menu.count(), 0) << "no long-press menu on a selection";

    // Beside the selection the finger scrolls as before
    const QPointF beside = viewPos(0, QPointF(box().right() + 120, box().bottom() + 200));
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, beside);
    for (int i = 1; i <= 6; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, beside - QPointF(0, 20 * i));
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, beside - QPointF(0, 120));
    vc.stopMomentum();
    processEvents();
    EXPECT_GT(vc.visibleContentRect().top(), scrolled) << "a finger next to the selection scrolls";
}

TEST_F(CanvasReplayTest, theSetsquareGuidesTheStrokeAndCanBeMoved) {
    auto& geometry = view->geometryTool();
    EXPECT_FALSE(geometry.visible());
    geometry.toggle(GeometryToolType::SETSQUARE);
    ASSERT_TRUE(geometry.visible());
    EXPECT_EQ(geometry.type(), GeometryToolType::SETSQUARE);

    // Its long edge lies across the middle of the page: drawing near it follows it
    auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);
    const QPointF nearEdge = middle + QPointF(60, 6);
    const QPointF snapped = geometry.snap(nearEdge);
    EXPECT_NEAR(snapped.y(), middle.y(), 1) << "on the edge";
    EXPECT_NEAR(snapped.x(), nearEdge.x(), 1) << "along it";

    // Far away from it nothing is changed
    const QPointF far = middle + QPointF(60, 300);
    EXPECT_EQ(geometry.snap(far), far);

    // It can be taken along, and the edge goes with it
    EXPECT_TRUE(geometry.contains(middle + QPointF(0, 30)));
    geometry.moveBy(QPointF(0, 100));
    EXPECT_NEAR(geometry.snap(middle + QPointF(60, 106)).y(), middle.y() + 100, 1);

    // Turning it by a quarter turn makes the edge upright
    geometry.turnAndSize(M_PI / 2, 1.0);
    const QPointF onUpright = geometry.snap(middle + QPointF(6, 100 + 60));
    EXPECT_NEAR(onUpright.x(), middle.x(), 1);

    // It is really drawn on the page (an overlay of the page, like the selection)
    EXPECT_TRUE(view->getPage(0)->hasOverlays()) << "the setsquare is an overlay of the page";
    processEvents(300);
    const auto info = view->getPage(0)->bufferInfo();
    const QImage withTool = view->getPage(0)->composeTile(QRect(QPoint(0, 0), info.pixelSize));
    geometry.hide();
    processEvents(300);
    const QImage without = view->getPage(0)->composeTile(QRect(QPoint(0, 0), info.pixelSize));
    EXPECT_NE(withTool, without) << "the page looks different with the setsquare on it";
    geometry.toggle(GeometryToolType::SETSQUARE);
    processEvents(100);

    // The compass instead: its edge is the circle of its radius, and points on the disc are drawn on that circle
    geometry.toggle(GeometryToolType::COMPASS);
    EXPECT_EQ(geometry.type(), GeometryToolType::COMPASS);
    const double radius = geometry.height();
    const QPointF onCircle = geometry.snap(middle + QPointF(0.5 * CM, 0));  // well inside the disc
    EXPECT_NEAR(std::hypot(onCircle.x() - middle.x(), onCircle.y() - middle.y()) / CM, radius, 0.1)
            << "a circle of its radius comes out";
    EXPECT_EQ(geometry.snap(middle + QPointF(0, (radius + 2) * CM)), middle + QPointF(0, (radius + 2) * CM))
            << "far outside it nothing is snapped";
    geometry.toggle(GeometryToolType::COMPASS);
    EXPECT_FALSE(geometry.visible()) << "the same one again takes it away";
}

// Two fingers on the tool itself turn it and size it; the page keeps its zoom (elsewhere they zoom as always).
// The tool is a ruler, not something the pen pushes around: drawing on it follows its nearest edge, and only two
// fingers (or the right mouse button) move it. One finger scrolls the page as always.
TEST_F(CanvasReplayTest, drawingOnTheSetsquareFollowsItsEdgesAndTwoFingersMoveIt) {
    auto& vc = view->getViewController();
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    ASSERT_TRUE(geometry.visible());
    const auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);

    // In its own coordinates the triangle has the corners (-8, 0), (8, 0) and (0, 8) cm: a point on the triangle
    // goes to the nearest of the three edges, not only to the long one.
    const QPointF nearLongEdge = middle + QPointF(0, 1 * CM);
    EXPECT_NEAR(geometry.snap(nearLongEdge).y(), middle.y(), 1) << "the long edge is nearest there";
    const QPointF nearRightLeg = middle + QPointF(5 * CM, 2 * CM);
    const QPointF onLeg = geometry.snap(nearRightLeg);
    EXPECT_NEAR((onLeg.x() - middle.x()) / CM + (onLeg.y() - middle.y()) / CM, 8.0, 0.2)
            << "on the leg from (8, 0) to (0, 8)";
    EXPECT_NE(onLeg, nearRightLeg);

    // A pen line drawn across the triangle comes out on the long edge, and the tool stays where it is
    const size_t before = elementCount(0);
    drawLine(0, middle + QPointF(-2 * CM, 0.6 * CM), middle + QPointF(2 * CM, 0.6 * CM));
    processEvents();
    ASSERT_EQ(elementCount(0), before + 1) << "the pen draws on the tool, it does not drag it";
    const Stroke* stroke = nullptr;
    for (const Layer* l: page->getLayersView()) {
        for (const Element* e: l->getElementsView()) {
            if (e->getType() == ELEMENT_STROKE) {
                stroke = static_cast<const Stroke*>(e);
            }
        }
    }
    ASSERT_NE(stroke, nullptr);
    for (const Point& p: stroke->getPointVector()) {
        EXPECT_NEAR(p.y, middle.y(), 1.5) << "every point lies on the long edge";
    }
    EXPECT_NEAR(geometry.snap(middle + QPointF(0, 20)).y(), middle.y(), 1) << "the tool did not move";

    // The eraser is not guided: it must reach what lies under the tool, not only its edge
    // below the apex of the triangle, so it is a free line, not one along an edge
    drawLine(0, middle + QPointF(-2 * CM, 9 * CM), middle + QPointF(2 * CM, 9 * CM));
    processEvents();
    const size_t withFreeLine = elementCount(0);
    ASSERT_EQ(withFreeLine, before + 2);
    // The eraser end of the pen: it rubs out where it is held, it is not guided to an edge (erasing may also cut a
    // stroke in two, so the number of elements only has to change)
    drawLine(0, middle + QPointF(0, 9 * CM - 6), middle + QPointF(0, 9 * CM + 6), 10, &eraser);
    processEvents();
    EXPECT_NE(elementCount(0), withFreeLine) << "the eraser reaches what lies under the tool";
    session->getUndoRedoHandler()->undo();  // the line back, so the next part starts clean
    processEvents();

    // Two fingers on it carry it along (without turning or sizing it)
    const double turned = geometry.rotation();
    const double high = geometry.height();
    const QPointF a = viewPos(0, middle + QPointF(-1 * CM, 1 * CM));
    const QPointF b = viewPos(0, middle + QPointF(1 * CM, 1 * CM));
    const QPointF by(40, 30);
    touch2(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, a, b);
    for (int i = 1; i <= 4; ++i) {
        touch2(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, a + by * i / 4.0,
               b + by * i / 4.0);
    }
    touch2(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, a + by, b + by);
    processEvents();
    const QPointF moved = middle + by / vc.zoom();
    EXPECT_NEAR(geometry.snap(moved + QPointF(0, 20)).y(), moved.y(), 2) << "its edge came along with the fingers";
    EXPECT_NEAR(geometry.rotation(), turned, 0.05) << "fingers that only slide do not turn it";
    EXPECT_NEAR(geometry.height(), high, 0.2) << "and do not size it";

    // One finger on it scrolls the page as everywhere else
    const double scrolled = vc.visibleContentRect().top();
    const QPointF onTool = viewPos(0, moved + QPointF(0, 2 * CM));
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, onTool);
    for (int i = 1; i <= 6; ++i) {
        touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, onTool - QPointF(0, 25 * i));
    }
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, onTool - QPointF(0, 150));
    vc.stopMomentum();
    processEvents();
    EXPECT_GT(vc.visibleContentRect().top(), scrolled) << "one finger scrolls, it does not move the tool";

    // With a mouse the right button drags it (what two fingers do), and no menu is asked for there
    QSignalSpy context(view.get(), &CanvasView::contextRequested);
    const QPointF grab = viewPos(0, moved + QPointF(0, 2 * CM));
    const QPointF onEdgeBefore = geometry.snap(moved + QPointF(0, 20));
    mouse(QEvent::MouseButtonPress, grab, Qt::RightButton, Qt::RightButton);
    mouse(QEvent::MouseMove, grab + QPointF(0, 50), Qt::NoButton, Qt::RightButton);
    mouse(QEvent::MouseButtonRelease, grab + QPointF(0, 50), Qt::RightButton, Qt::NoButton);
    processEvents();
    EXPECT_NEAR(geometry.snap(moved + QPointF(0, 20 + 50 / vc.zoom())).y(), onEdgeBefore.y() + 50 / vc.zoom(), 2)
            << "the right button took it along";
    EXPECT_EQ(context.count(), 0) << "and offered no menu on the tool";
    geometry.hide();
}

// Turning in steps of 15 degrees, and putting the tool aside for a moment without losing where it lay.
TEST_F(CanvasReplayTest, theSetsquareTurnsInStepsAndCanBePutAside) {
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    ASSERT_TRUE(geometry.visible());
    const double step = GeometryToolLayer::ANGLE_STEP;

    // Freely turned by 20 degrees; switching the steps on puts it on 15
    geometry.turnAndSize(20 * M_PI / 180, 1.0);
    EXPECT_NEAR(geometry.rotation(), 20 * M_PI / 180, 1e-9);
    geometry.setAngleSteps(true);
    EXPECT_NEAR(geometry.rotation(), step, 1e-9) << "onto the nearest step";
    // The fingers turn on underneath: a small turn stays on the step, past the middle it takes the next one
    geometry.turnAndSize(1 * M_PI / 180, 1.0);
    EXPECT_NEAR(geometry.rotation(), step, 1e-9) << "21 degrees: 15";
    geometry.turnAndSize(3 * M_PI / 180, 1.0);
    EXPECT_NEAR(geometry.rotation(), 2 * step, 1e-9) << "24 degrees: 30";
    geometry.turnAndSize(-20 * M_PI / 180, 1.0);
    EXPECT_NEAR(geometry.rotation(), 0, 1e-9) << "4 degrees: 0";
    geometry.setAngleSteps(false);
    geometry.turnAndSize(2 * M_PI / 180, 1.0);
    EXPECT_NEAR(geometry.rotation(), 2 * M_PI / 180, 1e-9) << "without steps it turns freely again";

    // Put aside: not on the page, not guiding the pen, but still out, and back where it was
    const auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);
    geometry.moveBy(QPointF(30, 40));
    const double turned = geometry.rotation();
    geometry.setMinimized(true);
    EXPECT_FALSE(geometry.visible());
    EXPECT_TRUE(geometry.active());
    EXPECT_TRUE(geometry.minimized());
    EXPECT_FALSE(view->getPage(0)->hasOverlays()) << "not drawn";
    const size_t before = elementCount(0);
    drawLine(0, middle + QPointF(-60, 45), middle + QPointF(60, 45));  // on the triangle, 5 pt off its edge
    processEvents();
    ASSERT_EQ(elementCount(0), before + 1);
    const Stroke* last = nullptr;
    for (const Element* e: page->getSelectedLayer()->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE) {
            last = static_cast<const Stroke*>(e);
        }
    }
    ASSERT_NE(last, nullptr);
    EXPECT_NEAR(last->getPoint(0).y, middle.y() + 45, 1) << "put aside, it does not guide the pen";
    geometry.setMinimized(false);
    EXPECT_TRUE(geometry.visible());
    EXPECT_NEAR(geometry.rotation(), turned, 1e-9) << "turned as before";
    EXPECT_TRUE(view->getPage(0)->hasOverlays());
    geometry.hide();
    EXPECT_FALSE(geometry.active());
}

// Held to a stroke, the middle of the setsquare (the 0 of its scale) stays on that stroke: moving it slides it along,
// so lengths can be measured along the line. Only letting go frees it again.
TEST_F(CanvasReplayTest, theSetsquareHoldsOnToTheNearestStroke) {
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    EXPECT_FALSE(geometry.holdToStroke()) << "no ink on the page yet";
    EXPECT_FALSE(geometry.heldToStroke());
    geometry.hide();

    const auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);
    drawLine(0, middle + QPointF(-150, 100), middle + QPointF(150, 100));  // a line below the middle
    drawLine(0, middle + QPointF(-150, -250), middle + QPointF(150, -250));  // and one farther away
    processEvents();

    geometry.toggle(GeometryToolType::SETSQUARE);
    ASSERT_TRUE(geometry.holdToStroke());
    EXPECT_TRUE(geometry.heldToStroke());
    EXPECT_NEAR(geometry.middle().x(), middle.x(), 1.5) << "straight onto the nearest stroke";
    EXPECT_NEAR(geometry.middle().y(), middle.y() + 100, 1.5);

    // Moving it (two fingers or the right button do that) slides it along the stroke
    geometry.moveBy(QPointF(40, 30));
    EXPECT_NEAR(geometry.middle().x(), middle.x() + 40, 1.5) << "along the stroke";
    EXPECT_NEAR(geometry.middle().y(), middle.y() + 100, 1.5) << "and not off it";
    geometry.moveBy(QPointF(500, -60));
    EXPECT_NEAR(geometry.middle().x(), middle.x() + 150, 2) << "no farther than the stroke goes";
    EXPECT_NEAR(geometry.middle().y(), middle.y() + 100, 1.5);

    // Let go: it moves freely again
    geometry.releaseStroke();
    EXPECT_FALSE(geometry.heldToStroke());
    geometry.moveBy(QPointF(0, 30));
    EXPECT_NEAR(geometry.middle().y(), middle.y() + 130, 1.5);
    geometry.hide();
}

// The marks of the setsquare's scale drawn onto the page with the pen: every centimetre (or half), on the paper
// beside the long edge, as one step to undo.
TEST_F(CanvasReplayTest, theSetsquareDrawsItsMarksOntoThePage) {
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    const auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);
    const size_t before = elementCount(0);

    // Every centimetre from -8 to 8: 17 marks, standing on the edge and pointing away from the triangle
    ASSERT_TRUE(view->drawGeometryMarks(1.0));
    ASSERT_EQ(elementCount(0), before + 17);
    std::vector<const Stroke*> marks;
    for (const Element* e: page->getSelectedLayer()->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE) {
            marks.push_back(static_cast<const Stroke*>(e));
        }
    }
    ASSERT_EQ(marks.size(), 17u);
    for (size_t i = 0; i < marks.size(); ++i) {
        const Point a = marks[i]->getPoint(0);
        const Point b = marks[i]->getPoint(1);
        EXPECT_NEAR(a.y, middle.y(), 0.01) << "on the edge";
        EXPECT_NEAR(a.x, middle.x() + (static_cast<double>(i) - 8) * CM, 0.01) << "a centimetre apart";
        EXPECT_NEAR(b.y, middle.y() - GeometryToolLayer::WHOLE_MARK_CM * CM, 0.01) << "outward, off the triangle";
        EXPECT_NEAR(b.x, a.x, 0.01);
    }
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(0), before) << "one step to undo";

    // Half centimetres: 33 marks, those between the centimetres shorter
    ASSERT_TRUE(view->drawGeometryMarks(0.5));
    EXPECT_EQ(elementCount(0), before + 33);
    session->getUndoRedoHandler()->undo();
    const auto lines = geometry.marks(0.5);
    ASSERT_EQ(lines.size(), 33u);
    EXPECT_NEAR(lines[1].first.y() - lines[1].second.y(), GeometryToolLayer::MARK_CM * CM, 0.01) << "a half one";
    EXPECT_NEAR(lines[2].first.y() - lines[2].second.y(), GeometryToolLayer::WHOLE_MARK_CM * CM, 0.01);

    // Turned by a quarter, the marks go along with the edge
    geometry.turnAndSize(M_PI / 2, 1.0);
    const auto turned = geometry.marks(1.0);
    ASSERT_FALSE(turned.empty());
    EXPECT_NEAR(turned[0].first.y(), turned[0].second.y(), 0.01) << "lying now";
    EXPECT_NEAR(turned[0].first.x(), middle.x(), 0.01) << "on the upright edge";

    // The compass has no scale to mark
    geometry.toggle(GeometryToolType::COMPASS);
    EXPECT_FALSE(view->drawGeometryMarks(1.0));
    geometry.hide();
}

TEST_F(CanvasReplayTest, twoFingersOnTheSetsquareTurnAndSizeIt) {
    auto& vc = view->getViewController();
    vc.setViewSize(QSizeF(900, 600));
    processEvents();
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    ASSERT_TRUE(geometry.visible());
    const double wasTurned = geometry.rotation();
    const double wasHigh = geometry.height();
    const double zoom = vc.zoom();

    const auto page = session->getDocument()->getPage(0);
    const QPointF middle(page->getWidth() / 2, page->getHeight() / 2);
    auto onScreen = [&](QPointF onPage) { return view->getPage(0)->viewRect().topLeft() + onPage * vc.zoom(); };
    ASSERT_TRUE(geometry.contains(middle + QPointF(0, 20))) << "the fingers go inside the triangle";

    // Both inside it, then turned by a bit and moved apart
    touch2(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, onScreen(middle + QPointF(-30, 20)),
           onScreen(middle + QPointF(30, 20)));
    touch2(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, onScreen(middle + QPointF(-40, 0)),
           onScreen(middle + QPointF(40, 40)));
    touch2(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, onScreen(middle + QPointF(-40, 0)),
           onScreen(middle + QPointF(40, 40)));
    EXPECT_GT(std::abs(geometry.rotation() - wasTurned), 0.1) << "it turned with the fingers";
    EXPECT_GT(geometry.height(), wasHigh) << "and grew as they moved apart";
    EXPECT_DOUBLE_EQ(vc.zoom(), zoom) << "the page itself is not zoomed";

    // Away from the tool the same gesture zooms the page
    touch2(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, QPointF(200, 500),
           QPointF(260, 500));
    touch2(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, QPointF(180, 500),
           QPointF(300, 500));
    touch2(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, QPointF(180, 500), QPointF(300, 500));
    EXPECT_GT(vc.zoom(), zoom) << "the fingers zoom where the tool is not";
    geometry.hide();
}

// --- The setsquare in the hands of the user: carried, turned and sized with two fingers, dragged with the right
// button, as the touch screen and the mouse send it. The setsquare lies in the middle of the first page; the fingers
// go down on the triangle 2 cm below its long edge, 3 cm apart.
namespace {
struct OnTheSetsquare {
    QPointF pageMiddle;   ///< of the finger pair, page coordinates
    QPointF viewMiddle;   ///< the same on the screen
    double spread = 0;    ///< half the distance of the fingers, screen pixels
};
}  // namespace

#define SETSQUARE_UNDER_TWO_FINGERS()                                                                              \
    auto& geometry = view->geometryTool();                                                                         \
    geometry.toggle(GeometryToolType::SETSQUARE);                                                                  \
    ASSERT_TRUE(geometry.visible());                                                                               \
    const auto firstPage = session->getDocument()->getPage(0);                                                     \
    const QPointF pageMiddle(firstPage->getWidth() / 2, firstPage->getHeight() / 2);                              \
    OnTheSetsquare on;                                                                                             \
    on.pageMiddle = pageMiddle + QPointF(0, 2 * CM);                                                               \
    on.viewMiddle = viewPos(0, on.pageMiddle);                                                                     \
    on.spread = 1.5 * CM * view->getViewController().zoom();                                                       \
    ASSERT_TRUE(geometry.contains(on.pageMiddle))

TEST_F(CanvasReplayTest, carryingTheSetsquareWithTwoFingersNeitherTurnsNorSizesIt) {
    SETSQUARE_UNDER_TWO_FINGERS();
    const double zoom = view->getViewController().zoom();
    const QPointF middle = geometry.middle();
    const double rotation = geometry.rotation();
    const double height = geometry.height();
    // Carried 80 px right and 50 down; fingers are never quite steady: their angle wobbles by a degree, their
    // distance by 2 %
    const QPointF by(80, 50);
    twoFingerGesture(*input, touchscreen, [&](int i, int steps) {
        const double wobble = (i % 2 ? 1.0 : -1.0) * M_PI / 180 * (i > 0);
        const double spread = on.spread * (1 + (i % 2 ? 0.02 : -0.02) * (i > 0));
        const QPointF c = on.viewMiddle + by * i / steps;
        return std::pair{turned(c - QPointF(spread, 0), c, wobble), turned(c + QPointF(spread, 0), c, wobble)};
    });
    EXPECT_NEAR(geometry.middle().x(), middle.x() + by.x() / zoom, 0.5) << "it went along with the fingers";
    EXPECT_NEAR(geometry.middle().y(), middle.y() + by.y() / zoom, 0.5);
    EXPECT_DOUBLE_EQ(geometry.rotation(), rotation) << "a finger wobble does not turn it";
    EXPECT_DOUBLE_EQ(geometry.height(), height) << "nor size it";
}

TEST_F(CanvasReplayTest, turningTheSetsquareWithTwoFingersTurnsItAroundThem) {
    SETSQUARE_UNDER_TWO_FINGERS();
    const double rotation = geometry.rotation();
    const double height = geometry.height();
    const QPointF underTheFingers = geometry.pageToTool(on.pageMiddle);
    const double twist = 40 * M_PI / 180;
    twoFingerGesture(
            *input, touchscreen,
            [&](int i, int steps) {
                const double a = twist * i / steps;
                return std::pair{turned(on.viewMiddle - QPointF(on.spread, 0), on.viewMiddle, a),
                                 turned(on.viewMiddle + QPointF(on.spread, 0), on.viewMiddle, a)};
            },
            20);
    EXPECT_NEAR(geometry.rotation() - rotation, twist - GeometryToolLayer::TURN_SLOP, 0.3 * M_PI / 180)
            << "turned with the fingers (minus the little the fingers may turn without turning it)";
    EXPECT_DOUBLE_EQ(geometry.height(), height) << "and not sized";
    const QPointF nowThere = geometry.toolToPage(underTheFingers);
    EXPECT_NEAR(nowThere.x(), on.pageMiddle.x(), 1) << "turned around the fingers: what was under them still is";
    EXPECT_NEAR(nowThere.y(), on.pageMiddle.y(), 1);
}

TEST_F(CanvasReplayTest, spreadingTwoFingersSizesTheSetsquareAroundThem) {
    SETSQUARE_UNDER_TWO_FINGERS();
    const double rotation = geometry.rotation();
    const double height = geometry.height();
    const QPointF underTheFingers = geometry.pageToTool(on.pageMiddle);
    twoFingerGesture(*input, touchscreen, [&](int i, int steps) {
        const double spread = on.spread * (1 + 0.5 * i / steps);
        return std::pair{on.viewMiddle - QPointF(spread, 0), on.viewMiddle + QPointF(spread, 0)};
    });
    EXPECT_NEAR(geometry.height(), height * 1.5 / (1 + GeometryToolLayer::SIZE_SLOP), 0.02 * height)
            << "half as big again (minus the little the fingers may spread without sizing it)";
    EXPECT_DOUBLE_EQ(geometry.rotation(), rotation) << "and not turned";
    // What was under the fingers is still under them: the tool grew around them (the same spot of the bigger tool
    // is that much farther from its middle, in centimetres)
    const QPointF nowThere = geometry.toolToPage(underTheFingers * geometry.height() / height);
    EXPECT_NEAR(nowThere.x(), on.pageMiddle.x(), 1);
    EXPECT_NEAR(nowThere.y(), on.pageMiddle.y(), 1);
}

TEST_F(CanvasReplayTest, turningPastTheBackOfTheCircleDoesNotSpinTheSetsquare) {
    SETSQUARE_UNDER_TWO_FINGERS();
    using S = QEventPoint::State;
    // The line from the first to the second finger points to the left (180 degrees) and turns on through it
    const auto at = [&](double angle) {
        return std::pair{turned(on.viewMiddle - QPointF(on.spread, 0), on.viewMiddle, angle),
                         turned(on.viewMiddle + QPointF(on.spread, 0), on.viewMiddle, angle)};
    };
    const double start = 160 * M_PI / 180;
    auto [a, b] = at(start);
    // (The fingers are set down the other way round: the second finger left of the first)
    touchN(*input, touchscreen, QEvent::TouchBegin, {{1, S::Pressed, b}, {2, S::Pressed, a}});
    double last = geometry.rotation();
    const double first = last;
    for (int i = 1; i <= 20; ++i) {
        std::tie(a, b) = at(start + 40 * M_PI / 180 * i / 20);
        touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Updated, b}, {2, S::Updated, a}});
        EXPECT_LT(std::abs(std::remainder(geometry.rotation() - last, 2 * M_PI)), 5 * M_PI / 180)
                << "no sudden turn at step " << i;
        last = geometry.rotation();
    }
    touchN(*input, touchscreen, QEvent::TouchEnd, {{1, S::Released, b}, {2, S::Released, a}});
    EXPECT_NEAR(geometry.rotation() - first, 40 * M_PI / 180 - GeometryToolLayer::TURN_SLOP, 0.3 * M_PI / 180)
            << "40 degrees, the same as anywhere else on the circle";
}

TEST_F(CanvasReplayTest, liftingOneOfTwoFingersNeitherScrollsNorMakesTheSetsquareJump) {
    SETSQUARE_UNDER_TWO_FINGERS();
    using S = QEventPoint::State;
    auto& vc = view->getViewController();
    const double zoom = vc.zoom();
    QPointF a = on.viewMiddle - QPointF(on.spread, 0);
    QPointF b = on.viewMiddle + QPointF(on.spread, 0);
    touchN(*input, touchscreen, QEvent::TouchBegin, {{1, S::Pressed, a}, {2, S::Pressed, b}});
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Updated, a + QPointF(20, 0)}, {2, S::Updated, b + QPointF(20, 0)}});
    a += QPointF(20, 0);
    b += QPointF(20, 0);
    const QPointF middle = geometry.middle();
    const double rotation = geometry.rotation();
    const double height = geometry.height();
    const double scrolled = vc.visibleContentRect().top();

    // The second finger goes up; the first one wanders on alone
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Stationary, a}, {2, S::Released, b}});
    for (int i = 1; i <= 6; ++i) {
        touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Updated, a + QPointF(0, 20 * i)}});
    }
    a += QPointF(0, 120);
    EXPECT_DOUBLE_EQ(vc.visibleContentRect().top(), scrolled) << "the finger left over does not scroll the page";
    EXPECT_EQ(geometry.middle(), middle) << "nor carry the setsquare";

    // It comes down again somewhere else - far away and at another angle: nothing jumps
    const QPointF c = a + QPointF(-60, 150);
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Stationary, a}, {3, S::Pressed, c}});
    EXPECT_EQ(geometry.middle(), middle) << "no jump when the finger comes back";
    EXPECT_DOUBLE_EQ(geometry.rotation(), rotation);
    EXPECT_DOUBLE_EQ(geometry.height(), height);

    // and from there both carry it on
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Updated, a + QPointF(30, 0)}, {3, S::Updated, c + QPointF(30, 0)}});
    touchN(*input, touchscreen, QEvent::TouchEnd, {{1, S::Released, a + QPointF(30, 0)}, {3, S::Released, c + QPointF(30, 0)}});
    EXPECT_NEAR(geometry.middle().x(), middle.x() + 30 / zoom, 0.5);
    EXPECT_NEAR(geometry.middle().y(), middle.y(), 0.5);
    EXPECT_NEAR(geometry.rotation(), rotation, 1e-9);
}

TEST_F(CanvasReplayTest, aThirdFingerOnTheSetsquareLeavesNoGhostBehind) {
    SETSQUARE_UNDER_TWO_FINGERS();
    using S = QEventPoint::State;
    const QPointF a = on.viewMiddle - QPointF(on.spread, 0);
    const QPointF b = on.viewMiddle + QPointF(on.spread, 0);
    const QPointF c = on.viewMiddle + QPointF(0, 40);
    touchN(*input, touchscreen, QEvent::TouchBegin, {{1, S::Pressed, a}, {2, S::Pressed, b}});
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Stationary, a}, {2, S::Stationary, b}, {3, S::Pressed, c}});
    touchN(*input, touchscreen, QEvent::TouchUpdate, {{1, S::Stationary, a}, {2, S::Stationary, b}, {3, S::Released, c}});
    touchN(*input, touchscreen, QEvent::TouchEnd, {{1, S::Released, a}, {2, S::Released, b}});
    geometry.hide();
    // All fingers are up: the next finger is a new touch and scrolls the page as always
    processEvents();
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 50) << "the touch before it has ended";
}

TEST_F(CanvasReplayTest, aQuickTwoFingerTurnOfTheSetsquareIsNotAnUndo) {
    drawLine(0, QPointF(80, 80), QPointF(200, 80));
    const size_t strokes = elementCount(0);
    SETSQUARE_UNDER_TWO_FINGERS();
    // Two fingers on the tool, a quick little turn, and up again - faster than a two-finger tap
    twoFingerGesture(*input, touchscreen, [&](int i, int steps) {
        const double a = 20 * M_PI / 180 * i / steps;
        return std::pair{turned(on.viewMiddle - QPointF(on.spread, 0), on.viewMiddle, a),
                         turned(on.viewMiddle + QPointF(on.spread, 0), on.viewMiddle, a)};
    }, 3);
    processEvents();
    EXPECT_EQ(elementCount(0), strokes) << "handling the setsquare must not undo the last stroke";
}

TEST_F(CanvasReplayTest, theRightButtonDragsTheSetsquareAcrossThePages) {
    auto& geometry = view->geometryTool();
    geometry.toggle(GeometryToolType::SETSQUARE);
    const double zoom = view->getViewController().zoom();
    const auto firstPage = session->getDocument()->getPage(0);
    const QPointF grab = viewPos(0, QPointF(firstPage->getWidth() / 2, firstPage->getHeight() / 2 + 2 * CM));
    const QPointF middle = geometry.middle();
    // Down, over the gap between the pages and onto the second one
    mouse(QEvent::MouseButtonPress, grab, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 40; ++i) {
        mouse(QEvent::MouseMove, grab + QPointF(0, 25 * i), Qt::NoButton, Qt::RightButton);
        ASSERT_NEAR(geometry.middle().y(), middle.y() + 25 * i / zoom, 0.5) << "it follows the pointer, step " << i;
    }
    mouse(QEvent::MouseButtonRelease, grab + QPointF(0, 1000), Qt::RightButton, Qt::NoButton);
    EXPECT_NEAR(geometry.middle().x(), middle.x(), 0.5);
}

TEST_F(CanvasReplayTest, theSetsquareOnlyGuidesOnItsOwnPage) {
    auto& geometry = view->geometryTool();
    auto& vc = view->getViewController();
    geometry.toggle(GeometryToolType::SETSQUARE);
    const auto secondPage = session->getDocument()->getPage(1);
    const QPointF middle(secondPage->getWidth() / 2, secondPage->getHeight() / 2);
    // On the second page, where the setsquare would lie if it were on that one: a free line
    drawLine(1, middle + QPointF(-60, 6), middle + QPointF(60, 6));
    processEvents();
    const Stroke* line = nullptr;
    for (const Element* e: secondPage->getSelectedLayer()->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE) {
            line = static_cast<const Stroke*>(e);
        }
    }
    ASSERT_NE(line, nullptr);
    EXPECT_NEAR(line->getPoint(0).y, middle.y() + 6, 1) << "the setsquare lies on the first page";

    // Two fingers on that place of the second page zoom the page, they do not grab the setsquare
    const QPointF toolMiddle = geometry.middle();
    const double zoom = vc.zoom();
    const QPointF c = viewPos(1, middle + QPointF(0, 2 * CM));
    const double spread = 1.5 * CM * zoom;
    twoFingerGesture(*input, touchscreen, [&](int i, int steps) {
        const double s = spread * (1 + 0.6 * i / steps);
        return std::pair{c - QPointF(s, 0), c + QPointF(s, 0)};
    });
    EXPECT_GT(vc.zoom(), zoom * 1.2) << "the page was zoomed";
    EXPECT_EQ(geometry.middle(), toolMiddle) << "the setsquare stayed where it was";
}

TEST_F(CanvasReplayTest, withStepsTwoFingersTurnTheSetsquareStepByStepAroundThem) {
    SETSQUARE_UNDER_TWO_FINGERS();
    geometry.setAngleSteps(true);
    const QPointF underTheFingers = geometry.pageToTool(on.pageMiddle);
    double last = geometry.rotation();
    const double twist = 40 * M_PI / 180;
    twoFingerGesture(
            *input, touchscreen,
            [&](int i, int steps) {
                // every position of the fingers: the tool stands on a step
                EXPECT_NEAR(std::remainder(geometry.rotation(), GeometryToolLayer::ANGLE_STEP), 0, 1e-9);
                last = geometry.rotation();
                const double a = twist * i / steps;
                return std::pair{turned(on.viewMiddle - QPointF(on.spread, 0), on.viewMiddle, a),
                                 turned(on.viewMiddle + QPointF(on.spread, 0), on.viewMiddle, a)};
            },
            20);
    EXPECT_NEAR(geometry.rotation(), 2 * GeometryToolLayer::ANGLE_STEP, 1e-9) << "37 degrees: the step of 30";
    const QPointF nowThere = geometry.toolToPage(underTheFingers);
    EXPECT_NEAR(nowThere.x(), on.pageMiddle.x(), 1) << "turned by its steps around the fingers";
    EXPECT_NEAR(nowThere.y(), on.pageMiddle.y(), 1);
}

TEST_F(CanvasReplayTest, heldToAStrokeTwoFingersSlideTheSetsquareAlongIt) {
    {
        const auto page = session->getDocument()->getPage(0);
        const QPointF centre(page->getWidth() / 2, page->getHeight() / 2);
        drawLine(0, centre + QPointF(-200, 0), centre + QPointF(200, 0));  // right through the setsquare's middle
    }
    SETSQUARE_UNDER_TWO_FINGERS();
    ASSERT_TRUE(geometry.holdToStroke());
    const double zoom = view->getViewController().zoom();
    const QPointF middle = geometry.middle();
    // Carried to the lower right and turned a little: it slides right along the stroke and turns about its middle
    twoFingerGesture(*input, touchscreen, [&](int i, int steps) {
        const double a = 20 * M_PI / 180 * i / steps;
        const QPointF c = on.viewMiddle + QPointF(90, 60) * i / steps;
        return std::pair{turned(c - QPointF(on.spread, 0), c, a), turned(c + QPointF(on.spread, 0), c, a)};
    });
    EXPECT_NEAR(geometry.middle().x(), middle.x() + 90 / zoom, 1) << "along the stroke";
    EXPECT_NEAR(geometry.middle().y(), middle.y(), 1) << "and not off it";
    EXPECT_NEAR(geometry.rotation(), 20 * M_PI / 180 - GeometryToolLayer::TURN_SLOP, 0.3 * M_PI / 180);
    EXPECT_TRUE(geometry.heldToStroke()) << "it holds on until the magnet is tapped again";
}

TEST_F(CanvasReplayTest, theSetsquareStaysOnItsPageWhenThePageMovesAndGoesAsideWhenItIsDeleted) {
    auto& geometry = view->geometryTool();
    session->insertNewPage(2);  // three pages
    processEvents();
    geometry.toggle(GeometryToolType::SETSQUARE);  // on the current page
    ASSERT_NE(geometry.page(), nullptr);
    const PageRef itsPage = geometry.page()->getPage();
    const auto indexOfItsPage = [&] {
        const auto order = session->pageOrder();
        return static_cast<size_t>(std::find(order.begin(), order.end(), itsPage) - order.begin());
    };
    geometry.moveBy(QPointF(20, 30));
    const QPointF middle = geometry.middle();

    // Its page moves to the front: the setsquare goes with it, where it lay on it
    ASSERT_NE(indexOfItsPage(), 0u);
    ASSERT_TRUE(session->movePages({indexOfItsPage()}, 0));
    processEvents();
    ASSERT_EQ(indexOfItsPage(), 0u);
    EXPECT_TRUE(geometry.visible()) << "still out";
    ASSERT_NE(geometry.page(), nullptr);
    EXPECT_EQ(geometry.page()->getPage(), itsPage) << "on the page it lay on";
    EXPECT_EQ(geometry.middle(), middle);
    EXPECT_TRUE(geometry.page()->hasOverlays()) << "and drawn there";
    // It still guides the pen on that page
    EXPECT_NEAR(geometry.snap(middle + QPointF(40, 5)).y(), middle.y(), 0.5);

    // The page is deleted: the setsquare is put aside (its pill stays), and a tap brings it onto the current page
    ASSERT_TRUE(session->deletePages({indexOfItsPage()}));
    processEvents();
    EXPECT_FALSE(geometry.visible());
    EXPECT_TRUE(geometry.minimized()) << "put aside, not lost";
    EXPECT_EQ(geometry.page(), nullptr);
    geometry.setMinimized(false);
    EXPECT_TRUE(geometry.visible());
    ASSERT_NE(geometry.page(), nullptr);
    EXPECT_NE(geometry.page()->getPage(), itsPage);
    // (and the view goes with the setsquare out: nothing is left pointing at a page that is gone)
}

// Dragging a selection on a page that is narrower than the window (grey margins left and right): it follows the
// pointer and nothing else. Upstream's edge panning, which runs 30 times a second while a selection is dragged, took
// the margin for a part of the page out of view and pushed the selection sideways by its width on every tick.
TEST_F(CanvasReplayTest, aSelectionDraggedOnANarrowPageOnlyFollowsThePointer) {
    auto& vc = view->getViewController();
    vc.setViewSize(QSizeF(1600, 1200));
    vc.setZoom(1.0, QPointF(0, 0));
    processEvents();
    ASSERT_GT(vc.contentOrigin().x(), 100) << "the page is centred, with margins left and right";
    app->getSettings()->setSnapGrid(false);  // (snapping to the grid would move it onto grid points on purpose)
    drawLine(0, QPointF(100, 300), QPointF(400, 400));
    drawLine(0, QPointF(120, 320), QPointF(380, 380));
    processEvents();
    view->selectAllOnPage();
    EditSelection* sel = view->getSelection();
    ASSERT_NE(sel, nullptr);
    const double x = sel->getXOnView();
    const double y = sel->getYOnView();

    // The mouse takes it by its middle and moves it straight down, slowly, as a hand does
    const QPointF grab = viewPos(0, QPointF(x + sel->getWidth() / 2, y + sel->getHeight() / 2));
    mouse(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 20; ++i) {
        mouse(QEvent::MouseMove, grab + QPointF(0, 5 * i), Qt::NoButton, Qt::LeftButton);
        processEvents(40);  // the edge-pan timer ticks in between
        ASSERT_NE(view->getSelection(), nullptr);
        EXPECT_NEAR(view->getSelection()->getXOnView(), x, 0.5) << "no sideways jump at step " << i;
    }
    mouse(QEvent::MouseButtonRelease, grab + QPointF(0, 100), Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_NEAR(view->getSelection()->getXOnView(), x, 0.5);
    EXPECT_NEAR(view->getSelection()->getYOnView(), y + 100, 1) << "down by what the mouse moved";

    // The same across: the pages zoomed out so far that they are shorter than the window
    vc.setZoom(0.3, QPointF(0, 0));
    processEvents();
    ASSERT_GT(vc.contentOrigin().y(), 50) << "margins above and below";
    EditSelection* moved = view->getSelection();
    const double x2 = moved->getXOnView();
    const double y2 = moved->getYOnView();
    const QPointF grab2 = viewPos(0, QPointF(x2 + moved->getWidth() / 2, y2 + moved->getHeight() / 2));
    mouse(QEvent::MouseButtonPress, grab2, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        mouse(QEvent::MouseMove, grab2 + QPointF(3 * i, 0), Qt::NoButton, Qt::LeftButton);
        processEvents(40);
        ASSERT_NE(view->getSelection(), nullptr);
        EXPECT_NEAR(view->getSelection()->getYOnView(), y2, 0.5) << "no jump up or down at step " << i;
    }
    mouse(QEvent::MouseButtonRelease, grab2 + QPointF(30, 0), Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_NE(view->getSelection(), nullptr);
    EXPECT_NEAR(view->getSelection()->getXOnView(), x2 + 30 / vc.zoom(), 1.5) << "across by what the mouse moved";
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
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "touch right after the pen left (no wait by default)";
}

// The pen held still on the page (pen, highlighter or hand in hand): what can be done here, as a finger held still
// or a right click (the window offers paste there). The dot it began does not stay. A pen that moved first, or moves
// a little more than it shakes, is writing: nothing is offered however long it then rests.
TEST_F(CanvasReplayTest, aPenHeldStillOffersWhatCanBeDoneHere) {
    QSignalSpy context(view.get(), &CanvasView::contextRequested);
    const QPointF at = viewPos(0, QPointF(200, 200));
    const std::string lastUndo = session->getUndoRedoHandler()->undoDescription();  // (the second page)
    auto hold = [&](QPointF where, int ms) {
        // A hand never holds perfectly still: a pixel or two to and fro, at the pen's rate
        for (int t = 0; t < ms; t += 50) {
            tablet(QEvent::TabletMove, where + QPointF((t / 50) % 2 ? 1.5 : -1.0, (t / 50) % 3 ? 1.0 : 0.0), 0.4,
                   Qt::NoButton, Qt::LeftButton);
            processEvents(50);
        }
    };

    tablet(QEvent::TabletPress, at, 0.4, Qt::LeftButton, Qt::LeftButton);
    hold(at, 700);
    ASSERT_EQ(context.count(), 1) << "held still: what can be done here";
    EXPECT_LT(QLineF(context.first().first().toPointF(), at).length(), 3) << "where the pen is";
    // Moving on after that does not draw (the pen was not writing), until it is lifted
    for (int i = 1; i <= 10; ++i) {
        tablet(QEvent::TabletMove, at + QPointF(10 * i, 5 * i), 0.4, Qt::NoButton, Qt::LeftButton);
    }
    tablet(QEvent::TabletRelease, at + QPointF(100, 50), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(elementCount(0), 0u) << "the dot the long press began is gone, and nothing was drawn after it";
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), lastUndo) << "nothing new to undo";
    EXPECT_EQ(context.count(), 1);

    // The next stroke is written as usual
    drawLine(0, QPointF(100, 300), QPointF(300, 320));
    processEvents();
    EXPECT_EQ(elementCount(0), 1u);

    // Writing, then resting on the page while thinking: no menu, the stroke stays
    const QPointF from = viewPos(0, QPointF(100, 400));
    tablet(QEvent::TabletPress, from, 0.4, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        tablet(QEvent::TabletMove, from + QPointF(8 * i, 0), 0.5, Qt::NoButton, Qt::LeftButton);
    }
    hold(from + QPointF(80, 0), 700);
    tablet(QEvent::TabletRelease, from + QPointF(80, 0), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(context.count(), 1) << "a pen resting after writing is still writing";
    EXPECT_EQ(elementCount(0), 2u);

    // A slow short stroke (more than the pen shakes) is writing too, however long it takes
    const QPointF slow = viewPos(0, QPointF(100, 500));
    tablet(QEvent::TabletPress, slow, 0.4, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i) {
        tablet(QEvent::TabletMove, slow + QPointF(2.0 * i, 0), 0.4, Qt::NoButton, Qt::LeftButton);
        processEvents(100);
    }
    tablet(QEvent::TabletRelease, slow + QPointF(16, 0), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(context.count(), 1) << "a slow stroke is no long press";
    EXPECT_EQ(elementCount(0), 3u);

    // The highlighter as well; the eraser held still just erases (nothing is offered)
    app->getToolHandler()->selectTool(TOOL_HIGHLIGHTER);
    tablet(QEvent::TabletPress, at, 0.4, Qt::LeftButton, Qt::LeftButton);
    hold(at, 700);
    tablet(QEvent::TabletRelease, at, 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(context.count(), 2) << "the highlighter held still";
    EXPECT_EQ(elementCount(0), 3u) << "and its dot is gone too";
    app->getToolHandler()->selectTool(TOOL_ERASER);
    tablet(QEvent::TabletPress, at, 0.4, Qt::LeftButton, Qt::LeftButton);
    hold(at, 700);
    tablet(QEvent::TabletRelease, at, 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(context.count(), 2) << "not for the eraser";

    // The hand: held still, the same (and the page does not move after it)
    app->getToolHandler()->selectTool(TOOL_HAND);
    const QRectF visible = view->getViewController().visibleContentRect();
    tablet(QEvent::TabletPress, at, 0.4, Qt::LeftButton, Qt::LeftButton);
    hold(at, 700);
    tablet(QEvent::TabletMove, at + QPointF(0, -60), 0.4, Qt::NoButton, Qt::LeftButton);
    tablet(QEvent::TabletRelease, at + QPointF(0, -60), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(context.count(), 3) << "the hand held still";
    EXPECT_NEAR(view->getViewController().visibleContentRect().top(), visible.top(), 4)
            << "no scrolling after it (only the shake before it)";
    app->getToolHandler()->selectTool(TOOL_PEN);
}

// How long touch waits once the pen is away is a setting (none by default).
TEST_F(CanvasReplayTest, touchWaitsAfterThePenAsLongAsTheSettingSays) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();
    app->getSettings()->getCustomElement("touch").setInt("timeout", 400);

    input->proximityEvent(true);
    input->proximityEvent(false);
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0) << "right after the pen left: still waiting";
    processEvents(450);
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "after the time set";
}

// A pen that never tells whether it is near: touch works right after it was used (by default), or after the time
// set.
TEST_F(CanvasReplayTest, aPenWithoutProximityDoesNotHoldUpTouch) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();
    drawLine(0, QPointF(100, 100), QPointF(200, 100));
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "right after writing";

    app->getSettings()->getCustomElement("touch").setInt("timeout", 500);
    drawLine(0, QPointF(100, 150), QPointF(200, 150));
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0) << "with a time set: not right after writing";
}

// A pen that tells how high it is counts as near only up to the height set: a finger can scroll while it stays
// close above the screen.
TEST_F(CanvasReplayTest, aPenThatTellsItsHeightIsAwayAboveTheHeightSet) {
    for (int i = 0; i < 6; ++i) {
        session->insertNewPage(1);
    }
    view->getViewController().setViewSize(QSizeF(900, 600));
    processEvents();
    const QPointF over(600, 300);

    input->proximityEvent(true);
    hover(over, 0.5);
    ASSERT_TRUE(PenHover::instance().reportsHeight());
    EXPECT_NEAR(PenHover::instance().height(), 0.5, 0.001);
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0) << "by default all of its range counts as near";

    app->getSettings()->getCustomElement("touch").setInt("nearHeight", 30);
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "above 30 %: away, the finger scrolls";
    hover(over, 0.2);
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0) << "at 20 %: near, touch is ignored";
    hover(over, 0.6);
    EXPECT_GT(fingerPan(*input, touchscreen, *view), 100) << "lifted again: touch works at once";

    // A pen that never tells its height (z stays 0 while hovering) is near all the way, whatever is set
    PenHover::instance().reset();
    input->proximityEvent(true);
    hover(over, 0.0);
    EXPECT_FALSE(PenHover::instance().reportsHeight());
    EXPECT_DOUBLE_EQ(fingerPan(*input, touchscreen, *view), 0.0);
    input->proximityEvent(false);
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

TEST_F(CanvasReplayTest, aLongPressSelectsTheWordOfThePdfAndTheHandlesWidenIt) {
    input.reset();
    view.reset();
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp"));
    ASSERT_TRUE(loaded.document);
    session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    view = std::make_unique<CanvasView>(*session);
    view->getViewController().setViewSize(QSizeF(900, 1200));
    input = std::make_unique<CanvasInput>(*view);
    processEvents();

    // Where "Test PDF" is on the first page
    QSignalSpy searched(&session->search(), &DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
    session->search().clear();

    const QPointF onWord = viewPos(0, hit.center());
    ASSERT_TRUE(view->selectPdfTextAt(onWord, false)) << "the word under the finger";
    EXPECT_TRUE(view->hasPdfTextSelection());
    const QRectF ends = view->pdfSelectionEnds();
    EXPECT_FALSE(ends.isNull());

    // Dragging the end further right takes more text with it
    const auto textOf = [&] { return QString::fromStdString(view->selectedPdfText()); };
    const QString word = textOf();
    EXPECT_FALSE(word.isEmpty());
    EXPECT_TRUE(view->dragPdfSelection(ends.bottomRight() + QPointF(220, 0), false));
    EXPECT_GE(textOf().size(), word.size()) << "more than the word";

    // A finger on the text itself keeps it (a second long press widens it); a tap beside it unselects
    EXPECT_TRUE(view->pdfTextSelectionContains(onWord));
    const QPointF besideTheText = onWord + QPointF(0, 260);
    EXPECT_FALSE(view->pdfTextSelectionContains(besideTheText));
    touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, besideTheText);
    touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, besideTheText);
    processEvents();
    EXPECT_FALSE(view->hasPdfTextSelection()) << "a tap beside the selected text unselects it";
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
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
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

// --- Scrolling sideways (qt/present): the pages side by side, fit to the height; the wheel, a swipe of the finger
// and the touchpad go from page to page when the view stops on whole pages.
namespace {
/// Waits until the view came to rest on a page.
void settle(CanvasReplayTest* t, ViewController& vc, AppContext& app) {
    QElapsedTimer clock;
    clock.start();
    while (vc.isAnimating() && clock.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    (void)t;
    (void)app;
}
/// One finger from `from` by `by` in `steps` moves, `msPerStep` apart (a flick when fast), then lifted.
void swipe(CanvasInput& input, const QPointingDevice& screen, QPointF from, QPointF by, int steps, int msPerStep) {
    touch(input, screen, QEvent::TouchBegin, QEventPoint::State::Pressed, from);
    for (int i = 1; i <= steps; ++i) {
        QThread::msleep(static_cast<unsigned long>(msPerStep));
        touch(input, screen, QEvent::TouchUpdate, QEventPoint::State::Updated, from + by * i / steps);
    }
    touch(input, screen, QEvent::TouchEnd, QEventPoint::State::Released, from + by);
}
}  // namespace

class SidewaysTest: public CanvasReplayTest {
protected:
    /// Six pages side by side in a 900 x 600 view
    void sideways(bool snap) {
        for (int i = 0; i < 4; ++i) {
            session->insertNewPage(1);
        }
        session->setCurrentPageNo(0);  // (it starts on the first page)
        app->getSettings()->setViewFixedRows(true);
        app->getSettings()->getCustomElement("xournalQt").setBool("snapPages", snap);
        Q_EMIT app->settingsChanged();
        view->getViewController().setViewSize(QSizeF(900, 600));
        processEvents();
    }
    ViewController& vc() { return view->getViewController(); }
    double x() { return vc().scrollPosition().x(); }
    void wheel(QPoint angle) {
        QWheelEvent e(QPointF(400, 300), QPointF(400, 300), QPoint(), angle, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
        input->wheelEvent(&e, QPointF(400, 300));
    }
};

TEST_F(SidewaysTest, thePagesFitTheHeightAndOnlyThoseInViewCount) {
    sideways(true);
    ASSERT_EQ(view->pageCount(), 6u);
    const auto& layout = view->documentLayout();
    ASSERT_TRUE(layout.horizontal());
    EXPECT_NEAR(vc().zoom(), layout.fitHeightZoom(600), 1e-9) << "fit to the height";
    EXPECT_LE(layout.contentSize(vc().zoom()).height(), 600.5) << "nothing to scroll up or down";
    EXPECT_EQ(vc().keptFit(), ViewController::Fit::Height);
    const auto [first, last] = view->visiblePages();
    EXPECT_EQ(first, 0u);
    EXPECT_LE(last, 2u) << "only the pages in view, not the whole row";

    // A bigger window: fitted again, on the same page
    vc().stepPages(2);
    settle(this, vc(), *app);
    vc().setViewSize(QSizeF(1200, 900));
    EXPECT_NEAR(vc().zoom(), layout.fitHeightZoom(900), 1e-9);
    EXPECT_EQ(vc().currentGroup(), 2u);
    // Zooming by hand ends that
    vc().zoomBy(1.2, QPointF(600, 450));
    EXPECT_EQ(vc().keptFit(), ViewController::Fit::None);
}

TEST_F(SidewaysTest, theWheelGoesFromPageToPage) {
    sideways(true);
    ASSERT_TRUE(vc().groupFitsView());
    wheel(QPoint(0, -120));  // one notch down
    EXPECT_TRUE(vc().isAnimating()) << "on its way";
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 1u);
    EXPECT_NEAR(x(), vc().restRange(1).first, 0.5);
    processEvents();
    EXPECT_EQ(session->getCurrentPageNo(), 1u);
    // Two notches in a row: two pages on, whatever the animation did in between
    wheel(QPoint(0, -120));
    wheel(QPoint(0, -120));
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 3u);
    wheel(QPoint(0, 120));  // up: back
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 2u);
    // Small steps of a fine wheel add up to a notch
    for (int i = 0; i < 3; ++i) {
        wheel(QPoint(0, -40));
    }
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 3u);
}

TEST_F(SidewaysTest, withoutStoppingOnPagesTheWheelScrollsSideways) {
    sideways(false);
    const double before = x();
    wheel(QPoint(0, -120));
    EXPECT_FALSE(vc().isAnimating());
    EXPECT_NEAR(x() - before, 48, 1) << "down scrolls right: there is nothing to scroll down";
    processEvents(100);
    EXPECT_NEAR(x() - before, 48, 1) << "and stays there";
}

TEST_F(SidewaysTest, aSwipeGoesOnePageOnAndASlowDragComesBack) {
    sideways(true);
    const double pageWidth = view->pageViewRect(1).left() - view->pageViewRect(0).left();
    // A gentle swipe to the left, less than half a page: the next page
    swipe(*input, touchscreen, QPointF(600, 300), QPointF(-pageWidth * 0.3, 0), 6, 40);
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 1u) << "flicked on";
    EXPECT_NEAR(x(), vc().restRange(1).first, 0.5);

    // A slow drag of a third of a page, held before lifting: back where it was
    auto dragAndHold = [&](QPointF from, double by) {
        touch(*input, touchscreen, QEvent::TouchBegin, QEventPoint::State::Pressed, from);
        for (int i = 1; i <= 10; ++i) {
            touch(*input, touchscreen, QEvent::TouchUpdate, QEventPoint::State::Updated, from + QPointF(by * i / 10, 0));
        }
        QThread::msleep(80);  // (held still: no flick)
        touch(*input, touchscreen, QEvent::TouchEnd, QEventPoint::State::Released, from + QPointF(by, 0));
    };
    dragAndHold(QPointF(600, 300), -pageWidth * 0.33);
    EXPECT_TRUE(vc().isAnimating());
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 1u) << "not far enough: it springs back";
    EXPECT_NEAR(x(), vc().restRange(1).first, 0.5);

    // Dragged slowly more than half a page: the page it was dragged to
    dragAndHold(QPointF(800, 300), -pageWidth * 0.7);
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 2u);
    EXPECT_NEAR(x(), vc().restRange(2).first, 0.5);

    // A swipe to the right: back one page
    swipe(*input, touchscreen, QPointF(300, 300), QPointF(pageWidth * 0.3, 0), 6, 40);
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 1u);

    // A strong flick carries on as the momentum would, and comes to rest on a page
    const double before = x();
    swipe(*input, touchscreen, QPointF(700, 300), QPointF(-pageWidth * 0.4, 0), 6, 6);
    settle(this, vc(), *app);
    EXPECT_GT(vc().currentGroup(), 2u) << "further than one page";
    EXPECT_NEAR(x(), vc().restRange(vc().currentGroup()).first, 0.5);
    EXPECT_GT(x(), before);
}

TEST_F(SidewaysTest, theTouchpadComesToRestOnAPage) {
    sideways(true);
    sendWheel(*input, touchpad, QPoint(-20, 0), Qt::ScrollBegin);
    for (int i = 0; i < 6; ++i) {
        QThread::msleep(30);
        sendWheel(*input, touchpad, QPoint(-20, 0), Qt::ScrollUpdate);
    }
    sendWheel(*input, touchpad, QPoint(0, 0), Qt::ScrollEnd);
    EXPECT_TRUE(vc().isAnimating()) << "the momentum carries it on to a page";
    settle(this, vc(), *app);
    EXPECT_EQ(vc().currentGroup(), 1u);
    EXPECT_NEAR(x(), vc().restRange(1).first, 0.5);

    // Two fingers going up or down move sideways too
    sendWheel(*input, touchpad, QPoint(0, -30), Qt::ScrollBegin);
    const double before = x();
    sendWheel(*input, touchpad, QPoint(0, -30), Qt::ScrollUpdate);
    EXPECT_NEAR(x() - before, 30, 1);
    sendWheel(*input, touchpad, QPoint(0, 0), Qt::ScrollEnd);
    settle(this, vc(), *app);
}

// Paging must be instant: the pages next to the one in view are drawn in advance, at the zoom of the view.
TEST_F(SidewaysTest, theNextAndThePreviousPageAreDrawnInAdvance) {
    sideways(true);
    view->setShown(true);
    vc().stepPages(2);
    settle(this, vc(), *app);
    const double zoom = vc().zoom();
    QElapsedTimer clock;
    clock.start();
    auto ready = [&](size_t p) {
        const auto info = view->getPage(p)->bufferInfo();
        return info.valid && info.zoom == zoom;
    };
    while (!(ready(1) && ready(3) && ready(4)) && clock.elapsed() < 3000) {
        processEvents(20);
    }
    EXPECT_TRUE(ready(1)) << "the page before";
    EXPECT_TRUE(ready(3)) << "the page after";
    EXPECT_TRUE(ready(4));
    view->setShown(false);
}
