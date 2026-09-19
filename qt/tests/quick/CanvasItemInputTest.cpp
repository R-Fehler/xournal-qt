/*
 * xournal-qt: input routing of the canvas item in a real Qt Quick window.
 *
 * Regression test for: dialogs (Save as, unsaved changes, messages) could not be used because the canvas took all
 * pen/touch/mouse events inside its rectangle, including those for the dialog drawn on top of it.
 * Events are injected through QWindowSystemInterface, i.e. the same path as events from Wayland (including Qt's
 * synthesis of mouse events from unaccepted tablet events).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <qpa/qwindowsysteminterface.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "DocumentCanvasItem.h"

using namespace xqt;

namespace {
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 800; height: 700; visible: true
    // A background item (z -1, declared after the content) must not hide the canvas from input.
    background: Rectangle { color: "#404040" }
    property int clicks: 0
    property alias dialog: dialog
    property alias tip: tip
    ToolTip { id: tip; x: 10; y: 10; text: "a tool tip somewhere else" }
    DocumentCanvas { id: canvas; objectName: "canvas"; anchors.fill: parent }
    ScrollBar {
        id: vbar
        objectName: "vbar"
        orientation: Qt.Vertical
        anchors.top: canvas.top; anchors.right: canvas.right; anchors.bottom: canvas.bottom
        width: 20
        visible: canvas.contentHeight > canvas.height + 1
        policy: ScrollBar.AlwaysOn
        size: canvas.contentHeight > 0 ? Math.min(1, canvas.height / canvas.contentHeight) : 1
        position: canvas.contentHeight > 0 ? canvas.contentY / canvas.contentHeight : 0
        onPositionChanged: if (pressed) canvas.scrollTo(canvas.contentX, position * canvas.contentHeight)
    }
    Dialog {
        id: dialog
        modal: true
        x: 250; y: 250; width: 300; height: 200
        Button { objectName: "ok"; anchors.centerIn: parent; width: 120; height: 50; text: "OK"; onClicked: clicks++ }
    }
}
)";

class CanvasItemInputTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);

        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        wait(200);
        QWindowSystemInterface::registerInputDevice(&pen);
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

    size_t strokeCount() const {
        size_t n = 0;
        for (size_t i = 0; i < session->getDocument()->getPageCount(); ++i) {
            for (const Layer* l: session->getDocument()->getPage(i)->getLayersView()) {
                n += l->getElementsView().size();
            }
        }
        return n;
    }

    void openDialog() {
        QObject* dialog = window->property("dialog").value<QObject*>();
        QMetaObject::invokeMethod(dialog, "open");
        wait(400);  // opening transition
        ASSERT_TRUE(dialog->property("opened").toBool());
    }

    QPoint okButtonCenter() const {
        auto* ok = window->findChild<QQuickItem*>("ok");
        return ok->mapToScene(QPointF(ok->width() / 2, ok->height() / 2)).toPoint();
    }

    void tablet(QPointF pos, Qt::MouseButtons buttons, double pressure) {
        QWindowSystemInterface::handleTabletEvent(window, timestamp, &pen, pos, window->mapToGlobal(pos), buttons,
                                                  pressure, 0, 0, 0, 0, 0, Qt::NoModifier);
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

TEST_F(CanvasItemInputTest, penAndMouseDrawOnTheCanvas) {
    penStroke(QPointF(200, 150), QPointF(500, 200));
    EXPECT_EQ(strokeCount(), 1u);
    mouseStroke(QPoint(200, 300), QPoint(500, 350));
    EXPECT_EQ(strokeCount(), 2u);
}

TEST_F(CanvasItemInputTest, dialogButtonWorksWithMouse) {
    openDialog();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, okButtonCenter());
    wait(50);
    EXPECT_EQ(window->property("clicks").toInt(), 1);
    EXPECT_EQ(strokeCount(), 0u) << "the click went to the canvas behind the dialog";
}

namespace {
struct EventTracer: QObject {
    bool eventFilter(QObject* o, QEvent* e) override {
        switch (e->type()) {
            case QEvent::TabletPress:
            case QEvent::TabletRelease:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
                fprintf(stderr, "TRACE %s -> %s(%s) accepted=%d\n",
                        e->type() == QEvent::TabletPress     ? "TabletPress" :
                        e->type() == QEvent::TabletRelease   ? "TabletRelease" :
                        e->type() == QEvent::MouseButtonPress ? "MousePress" :
                                                                "MouseRelease",
                        o->metaObject()->className(), qPrintable(o->objectName()), e->isAccepted());
                break;
            default:
                break;
        }
        return false;
    }
};
}  // namespace

TEST_F(CanvasItemInputTest, dialogButtonWorksWithPen) {
    openDialog();
    EventTracer tracer;
    if (qEnvironmentVariableIsSet("XQT_TRACE")) {
        qApp->installEventFilter(&tracer);
    }
    const QPointF c = okButtonCenter();
    tablet(c, Qt::LeftButton, 0.5);
    tablet(c, Qt::NoButton, 0.0);
    wait(50);
    EXPECT_EQ(window->property("clicks").toInt(), 1);
    EXPECT_EQ(strokeCount(), 0u);
}

TEST_F(CanvasItemInputTest, modalDialogBlocksCanvasInput) {
    openDialog();
    // Outside the dialog, on the modal dimmer.
    penStroke(QPointF(50, 50), QPointF(200, 120));
    mouseStroke(QPoint(50, 600), QPoint(200, 650));
    EXPECT_EQ(strokeCount(), 0u) << "the canvas received input while a modal dialog was open";
    EXPECT_EQ(window->property("clicks").toInt(), 0);
}

TEST_F(CanvasItemInputTest, openToolTipDoesNotBlockTheCanvas) {
    // A (non-modal) tool tip makes the popup overlay visible over the whole window.
    QObject* tip = window->property("tip").value<QObject*>();
    ASSERT_NE(tip, nullptr);
    QMetaObject::invokeMethod(tip, "open");
    wait(100);
    ASSERT_TRUE(tip->property("visible").toBool());
    penStroke(QPointF(200, 150), QPointF(500, 200));
    mouseStroke(QPoint(200, 300), QPoint(500, 350));
    EXPECT_EQ(strokeCount(), 2u);
}

TEST_F(CanvasItemInputTest, scrollBarScrollsWithMouseAndPenWithoutDrawing) {
    for (int i = 0; i < 5; ++i) {
        session->insertNewPage(1);
    }
    wait(100);
    auto* vbar = window->findChild<QQuickItem*>("vbar");
    ASSERT_NE(vbar, nullptr);
    ASSERT_TRUE(vbar->isVisible()) << "the document should be scrollable";
    auto contentY = [&] { return canvas->property("contentY").toDouble(); };

    // Start at the top; the handle is then at the top of the bar. Drag it down with the mouse.
    QMetaObject::invokeMethod(canvas, "scrollTo", Q_ARG(qreal, 0), Q_ARG(qreal, 0));
    wait(50);
    const double before = contentY();
    ASSERT_DOUBLE_EQ(before, 0.0);
    const QPointF handleTop = vbar->mapToScene(QPointF(vbar->width() / 2, 15));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, handleTop.toPoint());
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, (handleTop + QPointF(0, i * 10)).toPoint());
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, (handleTop + QPointF(0, 100)).toPoint());
    wait(50);
    const double afterMouse = contentY();
    EXPECT_GT(afterMouse, before + 50) << "dragging the scroll bar did not scroll";

    // Same with the pen (Qt turns the unhandled tablet events into mouse events for the scroll bar).
    const QPointF handle = vbar->mapToScene(QPointF(vbar->width() / 2, vbar->property("position").toDouble() *
                                                                               vbar->height() + 15));
    tablet(handle, Qt::LeftButton, 0.5);
    for (int i = 1; i <= 10; ++i) {
        tablet(handle + QPointF(0, i * 10), Qt::LeftButton, 0.5);
    }
    tablet(handle + QPointF(0, 100), Qt::NoButton, 0.0);
    wait(50);
    EXPECT_GT(contentY(), afterMouse + 50) << "dragging the scroll bar with the pen did not scroll";
    EXPECT_EQ(strokeCount(), 0u) << "scroll bar drags drew on the canvas";
}

// Regression test: with Qt Quick's software backend (offscreen, no GPU) the pages were drawn at the canvas's old
// position after the canvas moved (sidebar shown), over the items next to it.
TEST_F(CanvasItemInputTest, pagesFollowTheCanvasWhenItMoves) {
    QQmlProperty(canvas, "anchors.leftMargin").write(300);
    wait(100);
    QMetaObject::invokeMethod(canvas, "scrollTo", Q_ARG(qreal, 0), Q_ARG(qreal, 0));
    wait(300);
    ASSERT_DOUBLE_EQ(canvas->mapToScene(QPointF(0, 0)).x(), 300.0);
    const QImage shot = window->grabWindow();
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        shot.save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    // The first page's left edge is where the view says it is, in the moved canvas.
    const QRectF page = view->pageViewRect(0).translated(canvas->mapToScene(QPointF(0, 0)));
    ASSERT_GT(page.left(), 305.0);
    const int y = static_cast<int>(page.top()) + 40;
    for (int x = 0; x < static_cast<int>(page.left()) - 1; x += 5) {
        ASSERT_EQ(QColor(shot.pixel(x, y)), QColor("#404040")) << "page drawn left of its position at x=" << x;
    }
    EXPECT_EQ(QColor(shot.pixel(static_cast<int>(page.left()) + 3, y)), QColor(Qt::white)) << "page missing";
}
