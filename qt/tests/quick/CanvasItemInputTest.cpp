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
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <qpa/qwindowsysteminterface.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Font.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "DevicePixels.h"
#include "DocumentCanvasItem.h"
#include "HoverPointer.h"
#include "MarkdownBoxResize.h"
#include "MarkdownEditor.h"
#include "MdBox.h"

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
        QElapsedTimer t;  // (until the opening transition is done: it takes longer on a busy machine)
        t.start();
        while (!dialog->property("opened").toBool() && t.elapsed() < 5000) {
            wait(20);
        }
        ASSERT_TRUE(dialog->property("opened").toBool());
    }

    QPoint okButtonCenter() const {
        auto* ok = window->findChild<QQuickItem*>("ok");
        return ok->mapToScene(QPointF(ok->width() / 2, ok->height() / 2)).toPoint();
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

// The Surface Pro 8 (2026-09-24): pen strokes had one width. Here the pen's pressure must reach the stroke; the
// mouse has none.
TEST_F(CanvasItemInputTest, thePensPressureReachesTheStroke) {
    auto lastStroke = [this]() -> const Stroke* {
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
    };
    const QPointF from(200, 150), to(500, 200);
    tablet(from, Qt::LeftButton, 0.1);
    for (int i = 1; i <= 10; ++i) {
        tablet(from + (to - from) * (i / 10.0), Qt::LeftButton, 0.1 + 0.08 * i);
    }
    tablet(to, Qt::NoButton, 0.0);
    wait(50);
    const Stroke* pen = lastStroke();
    ASSERT_NE(pen, nullptr);
    ASSERT_TRUE(pen->hasPressure());
    double lowest = 1e9, highest = 0;
    for (size_t i = 0; i + 1 < pen->getPointCount(); ++i) {  // (the last point: the stroke's end)
        lowest = std::min(lowest, pen->getPoint(i).z);
        highest = std::max(highest, pen->getPoint(i).z);
    }
    EXPECT_GT(highest, lowest * 3) << "the width follows the pressure";

    mouseStroke(QPoint(200, 300), QPoint(500, 350));
    const Stroke* mouse = lastStroke();
    ASSERT_NE(mouse, pen);
    EXPECT_FALSE(mouse->hasPressure());
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
        ASSERT_EQ(QColor(xqt::test::pixelAt(shot, window, QPointF(x, y))), QColor("#404040"))
                << "page drawn left of its position at x=" << x;
    }
    EXPECT_EQ(QColor(xqt::test::pixelAt(shot, window, QPointF(static_cast<int>(page.left()) + 3, y))),
              QColor(Qt::white))
            << "page missing";
}

namespace {
/// A text with a web address on the first page; the place of the link in the window (scene coordinates)
QPointF addWebLink(DocumentSession& session, CanvasView& view, DocumentCanvasItem& canvas) {
    auto text = std::make_unique<Text>();
    text->setText("see https://example.org/hover for more");
    text->setFont(XojFont("Sans", 14));
    text->move(80, 120);
    const Text* raw = text.get();
    {
        auto page = session.getDocument()->getPage(0);
        std::unique_lock lock(*session.getDocument());
        page->getSelectedLayer()->addElement(std::move(text));
    }
    session.getDocument()->getPage(0)->firePageChanged();
    const auto& box = raw->getBoundingBox();
    const QPointF onPage(box.x + box.width / 2, box.y + box.height / 2);
    const QPointF inView = view.pageViewRect(0).topLeft() + onPage * view.getViewController().zoom();
    return canvas.mapToScene(inView);
}
}  // namespace

// Links with the mouse (qt/docs/links.md): resting on a link shows where it leads after a moment, the cursor is a
// pointing hand where a click follows it, and a click follows it (the pen tool draws nothing there).
TEST_F(CanvasItemInputTest, theMouseOverALinkShowsItsTargetAndAClickFollowsIt) {
    const QPointF link = addWebLink(*session, *view, *canvas);
    wait(100);
    const int lookups = view->linkLookups();
    QTest::mouseMove(window, (link + QPointF(0, 300)).toPoint());  // (off the link first)
    wait(50);
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);
    for (int i = 0; i <= 10; ++i) {
        QTest::mouseMove(window, (link + QPointF(-30 + 3 * i, 0)).toPoint());
    }
    EXPECT_EQ(canvas->cursor().shape(), Qt::PointingHandCursor) << "a click follows it";
    EXPECT_TRUE(canvas->hoveredLink().isEmpty()) << "not at once: passing over a link shows nothing";
    wait(DocumentCanvasItem::LINK_HOVER_MS + 150);
    EXPECT_EQ(canvas->hoveredLink().value("uri").toString(), "https://example.org/hover");
    EXPECT_LE(view->linkLookups(), lookups + 1) << "the moves looked up the page's links at most once";

    // Off the link: gone at once, the cursor is the tool's again
    QTest::mouseMove(window, (link + QPointF(0, 300)).toPoint());
    wait(20);
    EXPECT_TRUE(canvas->hoveredLink().isEmpty());
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);

    // Passing over it quickly: nothing
    QTest::mouseMove(window, link.toPoint());
    wait(60);
    QTest::mouseMove(window, (link + QPointF(0, 300)).toPoint());
    wait(DocumentCanvasItem::LINK_HOVER_MS + 100);
    EXPECT_TRUE(canvas->hoveredLink().isEmpty());

    // A click with the pen tool follows it and draws nothing
    QSignalSpy followed(view.get(), &CanvasView::linkTapped);
    const size_t before = strokeCount();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, link.toPoint());
    wait(50);
    ASSERT_EQ(followed.count(), 1);
    EXPECT_EQ(followed.at(0).at(0).toString(), "https://example.org/hover");
    EXPECT_EQ(strokeCount(), before);

    // The text tool writes where it clicks: an ordinary cursor, but the target is still shown
    app->getToolHandler()->selectTool(TOOL_TEXT);
    QTest::mouseMove(window, (link + QPointF(4, 0)).toPoint());
    wait(DocumentCanvasItem::LINK_HOVER_MS + 150);
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);
    EXPECT_FALSE(canvas->hoveredLink().isEmpty());
}

TEST_F(CanvasItemInputTest, theHoveringPenShowsALinksTargetToo) {
    const QPointF link = addWebLink(*session, *view, *canvas);
    wait(100);
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(window, timestamp++, &pen, true,
                                                                 xqt::test::nativeLocal(window, link),
                                                                 xqt::test::nativeGlobal(window, link));
    for (int i = 0; i <= 5; ++i) {
        tablet(link + QPointF(i, 0), Qt::NoButton, 0.0);  // (hovering: no button, no pressure)
    }
    EXPECT_TRUE(canvas->hoveredLink().isEmpty());
    wait(DocumentCanvasItem::LINK_HOVER_MS + 150);
    EXPECT_EQ(canvas->hoveredLink().value("uri").toString(), "https://example.org/hover");
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor) << "the pen keeps its tool (it writes on a link)";
    // The pen goes away: nothing is shown
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(window, timestamp++, &pen, false,
                                                                 xqt::test::nativeLocal(window, link),
                                                                 xqt::test::nativeGlobal(window, link));
    QWindowSystemInterface::flushWindowSystemEvents();
    wait(20);
    EXPECT_TRUE(canvas->hoveredLink().isEmpty());
}

// The handle that sets a Markdown text box's width (MarkdownBoxResize): the mouse over it shows the horizontal
// resize cursor, which stays while it is dragged; the drag sets the width.
TEST_F(CanvasItemInputTest, theMouseOverAMarkdownBoxHandleShowsTheResizeCursor) {
    app->getToolHandler()->selectTool(TOOL_TEXT);
    view->setMarkdownText(true, 10, false);
    view->startMarkdown(0, false, 100, 150);
    MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    for (const char c: std::string("Some words in a Markdown text box that is resized.")) {
        QKeyEvent e(QEvent::KeyPress, static_cast<Qt::Key>(QChar(c).toUpper().unicode()), Qt::NoModifier,
                    QString(QChar(c)));
        bool finish = false;
        editor->keyPressed(&e, finish);
    }
    wait(100);
    const auto handle = editor->widthHandle();
    ASSERT_TRUE(handle);
    const double zoom = view->getViewController().zoom();
    const auto scenePos = [&](QPointF onPage) {
        return canvas->mapToScene(view->pageViewRect(0).topLeft() + onPage * zoom).toPoint();
    };
    const double width = editor->boxWidth();
    QTest::mouseMove(window, scenePos(*handle + QPointF(-100, 60)));
    wait(20);
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);
    QTest::mouseMove(window, scenePos(*handle));
    wait(20);
    EXPECT_EQ(canvas->cursor().shape(), Qt::SizeHorCursor) << "over the handle";
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scenePos(*handle));
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, scenePos(*handle - QPointF(10 * i, 0)));
    }
    EXPECT_EQ(canvas->cursor().shape(), Qt::SizeHorCursor) << "while it is dragged";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scenePos(*handle - QPointF(100, 0)));
    wait(50);
    EXPECT_NEAR(editor->boxWidth(), width - 100, 2);
    QTest::mouseMove(window, scenePos(*handle + QPointF(-300, 80)));
    wait(20);
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor) << "away from it";
}

// The pointer over the page (qt/docs/hover-cursors.md): a cursor of the platform, which the compositor moves at no cost
// to the app: a small dot by default, the crosshair as a setting.
TEST_F(CanvasItemInputTest, thePointerIsADotCursorOrTheCrosshair) {
    QTest::mouseMove(window, QPoint(300, 300));
    wait(20);
    ASSERT_EQ(canvas->cursor().shape(), Qt::BitmapCursor) << "the dot";
    const QPixmap dot = canvas->cursor().pixmap();
    EXPECT_EQ(dot.deviceIndependentSize().toSize(), QSize(hover::dotSide(), hover::dotSide()));
    EXPECT_EQ(canvas->cursor().hotSpot(), QPoint(hover::dotSide() / 2, hover::dotSide() / 2)) << "its middle";
    const QImage pixels = dot.toImage();
    EXPECT_GT(qAlpha(pixels.pixel(pixels.width() / 2, pixels.height() / 2)), 200) << "a dot in the middle";
    EXPECT_EQ(qAlpha(pixels.pixel(0, 0)), 0) << "nothing around it";

    hover::setPointerSetting(*app->getSettings(), hover::Pointer::Crosshair);
    Q_EMIT app->settingsChanged();
    EXPECT_EQ(canvas->cursor().shape(), Qt::CrossCursor) << "the setting";
    EXPECT_FALSE(canvas->hoverMarkShown().visible) << "the mouse has its cursor: nothing drawn";

    hover::setPointerSetting(*app->getSettings(), hover::Pointer::Dot);
    Q_EMIT app->settingsChanged();
    EXPECT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);
}

// A pen the platform shows no cursor for (Android, iOS; here off-screen): the canvas draws the dot itself where the
// pen hovers. Moving it moves only the dot's item: no frame of the pages, no tile drawn anew.
TEST_F(CanvasItemInputTest, aPenWithoutACursorOfThePlatformGetsADrawnDotThatDrawsNoPage) {
    hover::setPlatformShowsPenCursorForTests(false);
    const QPointF at(300, 250);
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(
            window, timestamp++, &pen, true, xqt::test::nativeLocal(window, at), xqt::test::nativeGlobal(window, at));
    tablet(at, Qt::NoButton, 0.0);
    wait(100);
    auto shown = canvas->hoverMarkShown();
    ASSERT_TRUE(shown.visible);
    EXPECT_EQ(shown.side, hover::dotSide());
    const QPointF inItem = canvas->mapFromScene(at);
    EXPECT_NEAR(shown.center.x(), inItem.x(), 0.5);
    EXPECT_NEAR(shown.center.y(), inItem.y(), 0.5);

    canvas->forgetFrameStats();
    for (int i = 1; i <= 20; ++i) {
        tablet(at + QPointF(4 * i, 2 * i), Qt::NoButton, 0.0);
        wait(5);
    }
    wait(50);
    shown = canvas->hoverMarkShown();
    EXPECT_NEAR(shown.center.x(), inItem.x() + 80, 0.5) << "it follows the pen";
    EXPECT_NEAR(shown.center.y(), inItem.y() + 40, 0.5);
    EXPECT_EQ(canvas->frameStats().frames, 0) << "the pages were not drawn again for it";
    EXPECT_EQ(canvas->frameStats().tiles, 0);

    // The pen goes away: so does the dot
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(
            window, timestamp++, &pen, false, xqt::test::nativeLocal(window, at), xqt::test::nativeGlobal(window, at));
    QWindowSystemInterface::flushWindowSystemEvents();
    wait(20);
    EXPECT_FALSE(canvas->hoverMarkShown().visible) << "the pen went away";
    hover::setPlatformShowsPenCursorForTests(std::nullopt);
}

// A pen the platform shows a cursor for (Wayland's tablet tools, Windows Ink, X11): that cursor is the window's, which
// Qt Quick sets for the item under the mouse. The hovering pen makes it the canvas's, and gives it back when it goes.
TEST_F(CanvasItemInputTest, aPenWithACursorOfThePlatformShowsTheCanvassCursor) {
    hover::setPlatformShowsPenCursorForTests(true);
    window->setCursor(Qt::ArrowCursor);  // (as left by a control the mouse was over)
    const QPointF at(300, 250);
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(
            window, timestamp++, &pen, true, xqt::test::nativeLocal(window, at), xqt::test::nativeGlobal(window, at));
    tablet(at, Qt::NoButton, 0.0);
    wait(20);
    EXPECT_FALSE(canvas->hoverMarkShown().visible) << "the platform shows it: nothing drawn";
    ASSERT_EQ(window->cursor().shape(), Qt::BitmapCursor) << "the window shows the canvas's dot for the pen";
    EXPECT_EQ(window->cursor().pixmap().cacheKey(), canvas->cursor().pixmap().cacheKey());

    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(
            window, timestamp++, &pen, false, xqt::test::nativeLocal(window, at), xqt::test::nativeGlobal(window, at));
    QWindowSystemInterface::flushWindowSystemEvents();
    wait(20);
    EXPECT_EQ(window->cursor().shape(), Qt::ArrowCursor) << "given back";
    window->unsetCursor();
    hover::setPlatformShowsPenCursorForTests(std::nullopt);
}

namespace {
/// The side of the cursor's picture, logical pixels
int cursorSide(const QCursor& c) { return c.pixmap().deviceIndependentSize().toSize().width(); }
}  // namespace

// The eraser as the pointer (qt/docs/hover-cursors.md): gray, its real size at the zoom (upstream's square, 2 × its
// width a side), following the zoom and the eraser's size; dashed when it deletes whole strokes, round for whiteout.
TEST_F(CanvasItemInputTest, theEraserCursorHasTheErasersSizeAtTheZoom) {
    ToolHandler* tools = app->getToolHandler();
    tools->selectTool(TOOL_ERASER);
    tools->setEraserSize(TOOL_SIZE_MEDIUM);
    tools->fireToolChanged();
    QTest::mouseMove(window, QPoint(300, 300));
    wait(20);
    const double thickness = tools->getThickness();
    auto expected = [&](double size) { return hover::eraserSide(hover::EraserMark{size}); };
    double zoom = view->getViewController().zoom();
    ASSERT_EQ(canvas->cursor().shape(), Qt::BitmapCursor);
    EXPECT_EQ(cursorSide(canvas->cursor()), expected(2 * thickness * zoom)) << "the eraser's square";
    EXPECT_EQ(canvas->cursor().hotSpot(), QPoint(cursorSide(canvas->cursor()) / 2, cursorSide(canvas->cursor()) / 2));

    // The zoom
    view->getViewController().setZoom(zoom * 2, QPointF(300, 300));
    wait(20);
    zoom = view->getViewController().zoom();
    EXPECT_EQ(cursorSide(canvas->cursor()), expected(2 * thickness * zoom)) << "twice as big at twice the zoom";

    // The eraser's size
    tools->setEraserSize(TOOL_SIZE_FINE);
    wait(20);
    EXPECT_EQ(cursorSide(canvas->cursor()), expected(2 * tools->getThickness() * zoom));
    const QImage standard = canvas->cursor().pixmap().toImage();

    // Whole strokes: the same square, dashed
    tools->setEraserType(ERASER_TYPE_DELETE_STROKE);
    Q_EMIT app->settingsChanged();  // (as the settings do)
    const QImage dashed = canvas->cursor().pixmap().toImage();
    ASSERT_EQ(dashed.size(), standard.size());
    EXPECT_NE(dashed, standard) << "dashed";

    // Whiteout: a round brush as wide as the eraser
    tools->setEraserSize(TOOL_SIZE_VERY_THICK);  // (big enough to tell a circle from a square)
    tools->setEraserType(ERASER_TYPE_WHITEOUT);
    Q_EMIT app->settingsChanged();
    EXPECT_EQ(cursorSide(canvas->cursor()), expected(tools->getThickness() * zoom));
    const QImage round = canvas->cursor().pixmap().toImage();
    const int edge = static_cast<int>(3 * round.devicePixelRatio()) + 1;  // (inside the halo's room)
    EXPECT_EQ(qAlpha(round.pixel(edge, edge)), 0) << "no corner: round";
    EXPECT_GT(qAlpha(round.pixel(round.width() / 2, edge)), 0) << "its edge at the middle of the side";
    tools->setEraserType(ERASER_TYPE_DEFAULT);

    // The pen again: the dot
    tools->selectTool(TOOL_PEN);
    tools->fireToolChanged();
    EXPECT_EQ(cursorSide(canvas->cursor()), hover::dotSide());
    EXPECT_FALSE(canvas->hoverMarkShown().visible);
}

// An eraser bigger than a cursor may be (MAX_CURSOR_PX): the cursor is the dot, the canvas draws the eraser around
// it and moves it with the mouse.
TEST_F(CanvasItemInputTest, anEraserTooBigForACursorIsDrawnByTheCanvas) {
    ToolHandler* tools = app->getToolHandler();
    tools->selectTool(TOOL_ERASER);
    tools->setCustomThickness(TOOL_ERASER, 100, true);
    tools->fireToolChanged();
    const QPoint at(310, 290);  // (not where an earlier test left the mouse: Qt drops a move to the same place)
    QTest::mouseMove(window, at);
    wait(50);
    const double size = 200 * view->getViewController().zoom();
    ASSERT_GT(size * window->effectiveDevicePixelRatio(), hover::MAX_CURSOR_PX);
    EXPECT_EQ(cursorSide(canvas->cursor()), hover::dotSide()) << "the dot in its middle";
    auto shown = canvas->hoverMarkShown();
    ASSERT_TRUE(shown.visible);
    ASSERT_TRUE(shown.eraser);
    EXPECT_DOUBLE_EQ(shown.eraser->size, size);
    EXPECT_NEAR(shown.center.x(), canvas->mapFromScene(at).x(), 0.5);
    QTest::mouseMove(window, at + QPoint(40, 20));
    wait(20);
    shown = canvas->hoverMarkShown();
    EXPECT_NEAR(shown.center.x(), canvas->mapFromScene(at).x() + 40, 0.5) << "it follows the mouse";
    EXPECT_NEAR(shown.center.y(), canvas->mapFromScene(at).y() + 20, 0.5);

    // Zoomed out it fits a cursor again
    view->getViewController().setZoom(view->getViewController().zoom() / 4, QPointF(300, 300));
    wait(20);
    EXPECT_FALSE(canvas->hoverMarkShown().visible);
    EXPECT_GT(cursorSide(canvas->cursor()), hover::dotSide());
    tools->setCustomThickness(TOOL_ERASER, 100, false);
    tools->selectTool(TOOL_PEN);
    tools->fireToolChanged();
}

// The pen's eraser end, and a side button that erases while it is held, show the eraser too; the pen tip the dot.
TEST_F(CanvasItemInputTest, thePensEraserEndAndSideButtonShowTheEraser) {
    QPointingDevice eraserEnd{"test pen eraser", 2002, QInputDevice::DeviceType::Stylus,
                              QPointingDevice::PointerType::Eraser,
                              QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    QWindowSystemInterface::registerInputDevice(&eraserEnd);
    const QPointF at(300, 250);
    const auto hoverWith = [&](QPointingDevice* device, QPointF pos, Qt::MouseButtons buttons = Qt::NoButton) {
        QWindowSystemInterface::handleTabletEvent(window, timestamp, device, xqt::test::nativeLocal(window, pos),
                                                  xqt::test::nativeGlobal(window, pos), buttons, 0.0, 0, 0, 0, 0, 0,
                                                  Qt::NoModifier);
        timestamp += 5;
        QWindowSystemInterface::flushWindowSystemEvents();
    };

    // A platform with a cursor for the pen: the cursor is the eraser
    hover::setPlatformShowsPenCursorForTests(true);
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(window, timestamp++, &eraserEnd, true,
                                                                 xqt::test::nativeLocal(window, at),
                                                                 xqt::test::nativeGlobal(window, at));
    hoverWith(&eraserEnd, at);
    wait(20);
    EXPECT_GT(cursorSide(canvas->cursor()), hover::dotSide()) << "the eraser end: the eraser";
    EXPECT_EQ(app->getToolHandler()->getToolType(), TOOL_PEN) << "the tool changes only when it touches";
    hoverWith(&pen, at);
    wait(20);
    EXPECT_EQ(cursorSide(canvas->cursor()), hover::dotSide()) << "the tip: the dot";

    // The lower side button (erases by default) held while hovering
    hoverWith(&pen, at, Qt::MiddleButton);
    wait(20);
    EXPECT_EQ(app->getToolHandler()->getToolType(), TOOL_ERASER);
    EXPECT_GT(cursorSide(canvas->cursor()), hover::dotSide()) << "the side button erases: the eraser";
    hoverWith(&pen, at);
    wait(20);
    EXPECT_EQ(cursorSide(canvas->cursor()), hover::dotSide()) << "let go: the dot";

    // No cursor for the pen (Android): the canvas draws the eraser at the eraser end
    hover::setPlatformShowsPenCursorForTests(false);
    hoverWith(&eraserEnd, at + QPointF(10, 0));
    wait(20);
    auto shown = canvas->hoverMarkShown();
    ASSERT_TRUE(shown.visible);
    EXPECT_TRUE(shown.eraser) << "the eraser's outline";
    hoverWith(&pen, at + QPointF(20, 0));
    wait(20);
    shown = canvas->hoverMarkShown();
    ASSERT_TRUE(shown.visible);
    EXPECT_FALSE(shown.eraser) << "the dot";
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(
            window, timestamp++, &pen, false, xqt::test::nativeLocal(window, at), xqt::test::nativeGlobal(window, at));
    QWindowSystemInterface::flushWindowSystemEvents();
    hover::setPlatformShowsPenCursorForTests(std::nullopt);
}
