/*
 * xournal-qt: the canvas turned (qt/docs/canvas-rotation.md) in a real Qt Quick window: the pages are shown turned,
 * and the pen, the mouse, the wheel and the fingers act where they are on the screen.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QInputMethodQueryEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <qpa/qwindowsysteminterface.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Text.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "undo/UndoRedoHandler.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
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
    DocumentCanvas { id: canvas; objectName: "canvas"; anchors.fill: parent; clip: true }
}
)";

class CanvasRotationItemTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);
        session = std::make_unique<DocumentSession>(*app);
        for (int i = 0; i < 3; ++i) {
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
        QWindowSystemInterface::registerInputDevice(&pen);
        wait(200);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
    }
    void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    ViewController& vc() { return view->getViewController(); }

    /// Zoomed in on the middle of the second page (nothing is clamped at the ends of the document)
    void zoomIn() {
        const QSizeF v = vc().viewSize();
        vc().setZoom(vc().zoom() * 2.5, QPointF(v.width() / 2, v.height() / 2));
        const QRectF r = view->pageViewRect(1).translated(-vc().contentOrigin());
        vc().setScrollPosition(r.center() - QPointF(vc().viewSize().width() / 2, vc().viewSize().height() / 2));
        wait(50);
    }
    /// The page and the point on it (points) under a point of the canvas
    std::pair<size_t, QPointF> under(QPointF screen) {
        const QPointF v = vc().screenToView(screen);
        for (size_t i = 0; i < view->pageCount(); ++i) {
            const QRectF r = view->pageViewRect(i);
            if (r.contains(v)) {
                return {i, (v - r.topLeft()) / vc().zoom()};
            }
        }
        return {~size_t(0), {}};
    }
    /// Where a point of a page (points) is on the canvas
    QPointF onScreen(size_t page, QPointF pt) {
        return vc().viewToScreen(view->pageViewRect(page).topLeft() + pt * vc().zoom());
    }
    const Stroke* lastStroke() const {
        const Stroke* found = nullptr;
        for (size_t i = 0; i < session->getDocument()->getPageCount(); ++i) {
            for (const Layer* l: session->getDocument()->getPage(i)->getLayersView()) {
                for (const Element* e: l->getElementsView()) {
                    if (e->getType() == ELEMENT_STROKE) {
                        found = static_cast<const Stroke*>(e);
                    }
                }
            }
        }
        return found;
    }
    size_t pageOf(const Stroke* s) const {
        for (size_t i = 0; i < session->getDocument()->getPageCount(); ++i) {
            for (const Layer* l: session->getDocument()->getPage(i)->getLayersView()) {
                for (const Element* e: l->getElementsView()) {
                    if (e == s) {
                        return i;
                    }
                }
            }
        }
        return ~size_t(0);
    }

    void tablet(QPointF pos, Qt::MouseButtons buttons, double pressure) {
        QWindowSystemInterface::handleTabletEvent(window, timestamp, &pen, xqt::test::nativeLocal(window, pos),
                                                  xqt::test::nativeGlobal(window, pos), buttons, pressure, 0, 0, 0, 0,
                                                  0, Qt::NoModifier);
        timestamp += 5;
        QWindowSystemInterface::flushWindowSystemEvents();
    }
    void penStroke(QPointF from, QPointF to) {
        tablet(from, Qt::LeftButton, 0.5);
        for (int i = 1; i <= 10; ++i) {
            tablet(from + (to - from) * (i / 10.0), Qt::LeftButton, 0.6);
        }
        tablet(to, Qt::NoButton, 0.0);
        wait(50);
    }
    void mouseStroke(QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(window, from + (to - from) * i / 10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(50);
    }
    /// The stroke's first and last point are where the pointer pressed and let go, on the screen
    void expectStrokeFromTo(QPointF from, QPointF to, const char* what) {
        const Stroke* s = lastStroke();
        ASSERT_NE(s, nullptr) << what;
        const size_t page = pageOf(s);
        const auto a = s->getPoint(0);
        const auto b = s->getPoint(s->getPointCount() - 1);
        const QPointF sa = onScreen(page, QPointF(a.x, a.y)), sb = onScreen(page, QPointF(b.x, b.y));
        EXPECT_NEAR(sa.x(), from.x(), 1.5) << what;
        EXPECT_NEAR(sa.y(), from.y(), 1.5) << what;
        EXPECT_NEAR(sb.x(), to.x(), 1.5) << what;
        EXPECT_NEAR(sb.y(), to.y(), 1.5) << what;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
    QPointingDevice pen{"test pen", 2001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    ulong timestamp = 1000;
};
}  // namespace

TEST_F(CanvasRotationItemTest, thePenAndTheMouseWriteWhereTheyAreOnTheScreen) {
    zoomIn();
    for (const double angle: {90.0, 37.0, 180.0}) {
        vc().setRotation(angle);
        wait(50);
        const QPointF from(250, 220), to(520, 330);
        penStroke(from, to);
        expectStrokeFromTo(from, to, angle == 90 ? "pen at 90°" : angle == 37 ? "pen at 37°" : "pen at 180°");
        mouseStroke(QPoint(300, 450), QPoint(450, 380));
        expectStrokeFromTo(QPointF(300, 450), QPointF(450, 380), "mouse");
    }
}

TEST_F(CanvasRotationItemTest, thePagesAreShownTurned) {
    zoomIn();
    // A thick stroke on the upright canvas
    app->getToolHandler()->setSize(TOOL_SIZE_VERY_THICK);
    penStroke(QPointF(200, 200), QPointF(300, 200));
    const Stroke* s = lastStroke();
    ASSERT_NE(s, nullptr);
    const size_t page = pageOf(s);
    const auto mid = s->getPoint(s->getPointCount() / 2);
    // (the pen's colour: red, #cc3333, or blue in a grab whose red and blue are swapped; the paper is white, its lines
    // light blue, the background grey)
    auto darkNear = [&](const QImage& shot, QPointF at) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const QPointF p = at + QPointF(dx, dy);
                if (p.x() < 0 || p.y() < 0 || p.x() >= canvas->width() || p.y() >= canvas->height()) {
                    continue;
                }
                const QColor c(xqt::test::pixelAt(shot, window, p));
                if (std::max(c.red(), c.blue()) > 150 && std::min(c.red(), c.blue()) < 110 && c.green() < 110) {
                    return true;
                }
            }
        }
        return false;
    };
    wait(100);
    const QPointF upright = onScreen(page, QPointF(mid.x, mid.y));
    EXPECT_TRUE(darkNear(window->grabWindow(), upright)) << "drawn upright first";
    for (const double angle: {90.0, 30.0}) {
        vc().setRotation(angle, QPointF(400, 350));
        wait(150);
        const QPointF turned = onScreen(page, QPointF(mid.x, mid.y));
        ASSERT_GT(std::hypot(turned.x() - upright.x(), turned.y() - upright.y()), 40) << "it moved on the screen";
        const QImage shot = window->grabWindow();
        EXPECT_TRUE(darkNear(shot, turned)) << "the stroke is shown where the turned page has it, at " << angle << "°";
        EXPECT_FALSE(darkNear(shot, upright)) << "and no longer where it was upright, at " << angle << "°";
    }
}

TEST_F(CanvasRotationItemTest, theWheelScrollsTheWayItTurnsOnTheScreen) {
    zoomIn();
    vc().setRotation(90);
    wait(50);
    const QPointF middle(400, 350);
    const auto [page, pt] = under(middle);
    ASSERT_LT(page, view->pageCount());
    QWheelEvent wheel(middle, window->mapToGlobal(middle), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    wait(30);
    const QPointF now = onScreen(page, pt);
    EXPECT_NEAR(now.x(), middle.x(), 0.5) << "down on the screen, also with the canvas turned";
    EXPECT_NEAR(now.y(), middle.y() - 48, 0.5);
}

TEST_F(CanvasRotationItemTest, aPinchOnATurnedCanvasStaysUnderTheFingers) {
    zoomIn();
    const QPoint centre(380, 330);
    // The same pinch upright and turned: the point under the fingers ends where they are, on the screen
    auto pinch = [&](double angle) {
        vc().setRotation(angle, QPointF(centre));
        wait(50);
        const auto [page, pt] = under(centre);
        EXPECT_LT(page, view->pageCount());
        const double zoom = vc().zoom();
        static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        QTest::touchEvent(window, screen).press(0, centre - QPoint(50, 0)).press(1, centre + QPoint(50, 0));
        QPoint shift;
        for (int i = 1; i <= 10; ++i) {
            const int d = 50 + 5 * i;
            shift = QPoint(0, 3 * i);  // (and they move down a little)
            QTest::touchEvent(window, screen).move(0, centre - QPoint(d, 0) + shift).move(1, centre + QPoint(d, 0) + shift);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        EXPECT_NEAR(vc().zoom(), zoom * 2, zoom * 0.02);
        const QPointF now = onScreen(page, pt);
        QTest::touchEvent(window, screen).release(0, centre - QPoint(100, 0) + shift).release(1, centre + QPoint(100, 0) + shift);
        wait(50);
        EXPECT_EQ(vc().rotation(), angle) << "a pinch without a twist does not turn it";
        vc().setZoom(zoom, QPointF(centre));
        return now - QPointF(centre);
    };
    const QPointF upright = pinch(0);
    EXPECT_NEAR(upright.y(), 30, 3) << "it followed the fingers";
    for (const double angle: {90.0, 200.0}) {
        const QPointF turned = pinch(angle);
        EXPECT_NEAR(turned.x(), upright.x(), 0.5) << "as upright, at " << angle << "°";
        EXPECT_NEAR(turned.y(), upright.y(), 0.5) << "as upright, at " << angle << "°";
    }
}

// The scroll bars are the screen's: turned by 90° the one at the right scrolls the view sideways; at a free angle there
// are none
TEST_F(CanvasRotationItemTest, theScrollBarsFollowTheScreen) {
    zoomIn();
    const QSizeF content = view->documentLayout().contentSize(vc().zoom());
    EXPECT_DOUBLE_EQ(canvas->contentWidth(), content.width());
    EXPECT_DOUBLE_EQ(canvas->contentY(), vc().scrollPosition().y());
    vc().setRotation(90);
    EXPECT_DOUBLE_EQ(canvas->contentWidth(), content.height()) << "across the screen: the view's up and down";
    EXPECT_DOUBLE_EQ(canvas->contentHeight(), content.width());
    EXPECT_DOUBLE_EQ(canvas->contentY(), vc().scrollPosition().x()) << "down the screen: the view's x, the same way";
    EXPECT_NEAR(canvas->contentX(), content.height() - vc().scrollPosition().y() - vc().viewSize().height(), 1e-9)
            << "across the screen: the view's y, the other way";
    // Dragging the bar at the right down moves the pages up on the screen, by as much
    const QPointF middle(400, 350);
    const auto [page, pt] = under(middle);
    canvas->scrollTo(canvas->contentX(), canvas->contentY() + 60);
    const QPointF now = onScreen(page, pt);
    EXPECT_NEAR(now.x(), middle.x(), 1e-6);
    EXPECT_NEAR(now.y(), middle.y() - 60, 1e-6);
    // ... and the bar at the bottom moved right: the pages go left
    const auto [page2, pt2] = under(middle);
    canvas->scrollTo(canvas->contentX() + 40, canvas->contentY());
    const QPointF now2 = onScreen(page2, pt2);
    EXPECT_NEAR(now2.x(), middle.x() - 40, 1e-6);
    EXPECT_NEAR(now2.y(), middle.y(), 1e-6);
    vc().setRotation(37);
    EXPECT_EQ(canvas->contentWidth(), 0) << "no scroll bars at a free angle";
    EXPECT_EQ(canvas->contentHeight(), 0);
}

// A link on a turned canvas: the mouse over it shows where it leads, a click follows it, and the rectangle the window
// gets for it is where it is on the screen
TEST_F(CanvasRotationItemTest, linksAreFoundAndShownWhereTheyAreOnTheScreen) {
    auto text = std::make_unique<Text>();
    text->setText("see https://example.org/turned for more");
    text->setFont(XojFont("Sans", 14));
    text->move(80, 120);
    const Text* raw = text.get();
    {
        auto p = session->getDocument()->getPage(0);
        std::unique_lock lock(*session->getDocument());
        p->getSelectedLayer()->addElement(std::move(text));
    }
    session->getDocument()->getPage(0)->firePageChanged();
    vc().setRotation(90);
    const auto& box = raw->getBoundingBox();
    const QPointF onPage(box.x + box.width / 2, box.y + box.height / 2);
    // (the link in the middle of the view)
    const QPointF content = view->pageViewRect(0).topLeft() - vc().contentOrigin() + onPage * vc().zoom();
    vc().setScrollPosition(content - QPointF(vc().viewSize().width() / 2, vc().viewSize().height() / 2));
    wait(100);
    const QPointF link = onScreen(0, onPage);
    ASSERT_TRUE(QRectF(0, 0, canvas->width(), canvas->height()).contains(link));
    for (int i = 0; i <= 6; ++i) {
        QTest::mouseMove(window, (link + QPointF(0, -12 + 2 * i)).toPoint());  // (along the link: down the screen)
    }
    wait(DocumentCanvasItem::LINK_HOVER_MS + 150);
    EXPECT_EQ(canvas->hoveredLink().value("uri").toString(), "https://example.org/turned");
    EXPECT_EQ(canvas->cursor().shape(), Qt::PointingHandCursor);
    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, link.toPoint());
    wait(50);
    ASSERT_EQ(followed.count(), 1);
    const QRectF rect = followed.at(0).at(2).toRectF();
    EXPECT_TRUE(rect.adjusted(-1, -1, 1, 1).contains(link)) << "the link's rectangle on the screen";
    EXPECT_GT(rect.height(), rect.width()) << "turned by 90°, the line of text runs down the screen";
}

// Writing text on a turned canvas: the on-screen keyboard and the input method are told where the cursor is on the
// screen
TEST_F(CanvasRotationItemTest, theTextCursorIsReportedWhereItIsOnTheScreen) {
    zoomIn();
    vc().setRotation(90);
    wait(50);
    app->getToolHandler()->selectTool(TOOL_TEXT);
    const QPoint at(300, 260);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(50);
    ASSERT_TRUE(canvas->textEditing());
    QInputMethodQueryEvent query(Qt::ImCursorRectangle);
    QCoreApplication::sendEvent(canvas, &query);
    const QRectF cursor = query.value(Qt::ImCursorRectangle).toRectF();
    ASSERT_FALSE(cursor.isEmpty());
    EXPECT_LT(std::hypot(cursor.center().x() - at.x(), cursor.center().y() - at.y()), 40)
            << "near where the text tool was pressed";
    EXPECT_GT(cursor.width(), cursor.height()) << "turned by 90°, the text cursor lies across the screen";
}

namespace {
/// Two fingers on the canvas, `radius` from `centre`, turned from `from` to `to` degrees in steps; lifted at the end
void twist(QQuickWindow* window, QPointF centre, double radius, double from, double to, int steps = 12) {
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    auto at = [&](double degrees, int finger) {
        const double r = (degrees + (finger ? 180.0 : 0.0)) * M_PI / 180.0;
        return (centre + QPointF(radius * std::cos(r), radius * std::sin(r))).toPoint();
    };
    QTest::touchEvent(window, screen).press(0, at(from, 0)).press(1, at(from, 1));
    for (int i = 1; i <= steps; ++i) {
        const double a = from + (to - from) * i / steps;
        QTest::touchEvent(window, screen).move(0, at(a, 0)).move(1, at(a, 1));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QTest::touchEvent(window, screen).release(0, at(to, 0)).release(1, at(to, 1));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}
}  // namespace

// Two fingers twisted turn the canvas once the twist is deliberate, snapping to quarters; the point between them stays
// under them; a twist is no two-finger tap (no undo)
TEST_F(CanvasRotationItemTest, twoFingersTwistedTurnTheCanvas) {
    zoomIn();
    penStroke(QPointF(200, 200), QPointF(300, 220));  // (something to undo)
    const size_t strokes = session->getUndoRedoHandler()->canUndo() ? 1 : 0;
    ASSERT_EQ(strokes, 1u);
    const QPointF centre(400, 350);
    const auto [page, pt] = under(centre);
    twist(window, centre, 80, 0, 8);
    EXPECT_EQ(vc().rotation(), 0) << "a little twist does not turn it";
    twist(window, centre, 80, 0, 52);  // 52 - 12 = 40
    EXPECT_NEAR(vc().rotation(), 40, 1.5);
    const QPointF now = onScreen(page, pt);
    EXPECT_NEAR(now.x(), centre.x(), 2) << "turned about the fingers";
    EXPECT_NEAR(now.y(), centre.y(), 2);
    twist(window, centre, 80, 0, 57);  // 40 + 45 = 85: snaps to 90
    EXPECT_EQ(vc().rotation(), 90);
    twist(window, centre, 80, 10, -50);  // back by 48
    EXPECT_NEAR(vc().rotation(), 42, 1.5);
    EXPECT_TRUE(session->getUndoRedoHandler()->canUndo()) << "the twists undid nothing";
    // Quickly (as fast as a tap): still a turn, not an undo
    twist(window, centre, 80, 0, 40, 2);
    EXPECT_NEAR(vc().rotation(), 70, 1.5);
    EXPECT_TRUE(session->getUndoRedoHandler()->canUndo());

    // Off in the settings: two fingers only pan and zoom
    vc().setRotation(0);
    session->getSettings()->getCustomElement("xournalQt").setBool("rotateGesture", false);
    twist(window, centre, 80, 0, 60);
    EXPECT_EQ(vc().rotation(), 0);
    session->getSettings()->getCustomElement("xournalQt").setBool("rotateGesture", true);
    // Not where the canvas may not turn (the reference)
    view->setRotatable(false);
    twist(window, centre, 80, 0, 60);
    EXPECT_EQ(vc().rotation(), 0);
    view->setRotatable(true);
}

// The touchpad's rotate gesture (macOS, Wayland): it adds up, starts after the same twist, and snaps
TEST_F(CanvasRotationItemTest, theTouchpadsRotateGestureTurnsTheCanvas) {
    zoomIn();
    static QPointingDevice* pad = QTest::createTouchDevice(QInputDevice::DeviceType::TouchPad);
    const QPointF at(300, 300);
    const auto [page, pt] = under(at);
    auto send = [&](Qt::NativeGestureType type, double value) {
        QNativeGestureEvent e(type, pad, 2, at, at, window->mapToGlobal(at), value, QPointF());
        QCoreApplication::sendEvent(window, &e);
    };
    send(Qt::BeginNativeGesture, 0);
    for (int i = 0; i < 10; ++i) {
        send(Qt::RotateNativeGesture, 4);  // 40 in all: 28
    }
    send(Qt::EndNativeGesture, 0);
    EXPECT_NEAR(vc().rotation(), 28, 1e-6);
    const QPointF now = onScreen(page, pt);
    EXPECT_NEAR(now.x(), at.x(), 1e-6) << "about the pointer";
    EXPECT_NEAR(now.y(), at.y(), 1e-6);
    send(Qt::BeginNativeGesture, 0);
    send(Qt::RotateNativeGesture, -8);
    EXPECT_NEAR(vc().rotation(), 28, 1e-6) << "a new gesture starts from nothing";
    send(Qt::RotateNativeGesture, -14);  // -22 + 12 = -10
    EXPECT_NEAR(vc().rotation(), 18, 1e-6);
    send(Qt::RotateNativeGesture, -15);  // -37 + 12 = -25: 3, snaps to 0
    EXPECT_EQ(vc().rotation(), 0);
    send(Qt::EndNativeGesture, 0);
}
