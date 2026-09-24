/*
 * xournal-qt: two canvases in one window - the document of a tab and its reference beside it (reference mode).
 *
 * Events go to the canvas under the pen, the finger or the pointer; a stroke that begins on one canvas stays there
 * when it crosses the other one; the reference is for reading only: nothing lands in it, but it scrolls, zooms and
 * selects (to copy). Events are injected through QWindowSystemInterface, as in CanvasItemInputTest.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <qpa/qwindowsysteminterface.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasView.h"
#include "DocumentCanvasItem.h"

using namespace xqt;

namespace {
// The main canvas on the left half, the reference on the right half (as the split of the window lays them out).
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 800; height: 600; visible: true
    DocumentCanvas { objectName: "main"; x: 0; y: 0; width: 396; height: 600 }
    DocumentCanvas { objectName: "reference"; x: 404; y: 0; width: 396; height: 600; readingOnly: true }
}
)";

class ReferenceCanvasTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);
        mainSession = std::make_unique<DocumentSession>(*app);
        refSession = std::make_unique<DocumentSession>(*app);
        for (int i = 0; i < 4; ++i) {  // (something to scroll)
            mainSession->insertNewPage(1);
            refSession->insertNewPage(1);
        }
        refSession->getUndoRedoHandler()->clearContents();
        mainView = std::make_unique<CanvasView>(*mainSession);
        refView = std::make_unique<CanvasView>(*refSession);

        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        mainCanvas = window->findChild<DocumentCanvasItem*>("main");
        refCanvas = window->findChild<DocumentCanvasItem*>("reference");
        ASSERT_NE(mainCanvas, nullptr);
        ASSERT_NE(refCanvas, nullptr);
        mainCanvas->setView(mainView.get());
        refCanvas->setView(refView.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        wait(200);
        QWindowSystemInterface::registerInputDevice(&pen);
    }
    void TearDown() override {
        mainCanvas->setView(nullptr);
        refCanvas->setView(nullptr);
        mainView.reset();
        refView.reset();
        mainSession.reset();
        refSession.reset();
    }

    void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// Until something is true (an animation that takes longer on a busy machine), at most `ms`
    void until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }

    static size_t elementCount(const DocumentSession& s) {
        size_t n = 0;
        for (size_t i = 0; i < s.getDocument()->getPageCount(); ++i) {
            for (const Layer* l: s.getDocument()->getPage(i)->getLayersView()) {
                n += l->getElementsView().size();
            }
        }
        return n;
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
    /// Two fingers moving apart around `centre`
    void pinch(QPoint centre, int from, int to) {
        static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        QTest::touchEvent(window, screen).press(0, centre - QPoint(from, 0)).press(1, centre + QPoint(from, 0));
        for (int i = 1; i <= 10; ++i) {
            const int d = from + (to - from) * i / 10;
            QTest::touchEvent(window, screen).move(0, centre - QPoint(d, 0)).move(1, centre + QPoint(d, 0));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        QTest::touchEvent(window, screen).release(0, centre - QPoint(to, 0)).release(1, centre + QPoint(to, 0));
        wait(50);
    }
    /// A stroke on the first page of the reference (page points)
    void strokeInReference(QPointF a, QPointF b) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(2);
        stroke->addPoint(Point(a.x(), a.y(), 1.0));
        stroke->addPoint(Point(b.x(), b.y(), 1.0));
        refSession->getDocument()->getPage(0)->getSelectedLayer()->addElement(std::move(stroke));
        refSession->firePageChanged(0);
        wait(50);
    }
    /// A place of the first page of the reference (page points) in the window
    QPointF onReferencePage(QPointF pt) const {
        const QRectF r = refView->pageViewRect(0);
        const double zoom = refView->getViewController().zoom();
        return refCanvas->mapToScene(r.topLeft() + pt * zoom);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> mainSession, refSession;
    std::unique_ptr<CanvasView> mainView, refView;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* mainCanvas = nullptr;
    DocumentCanvasItem* refCanvas = nullptr;
    QPointingDevice pen{"test pen", 2002, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    ulong timestamp = 1000;
};
}  // namespace

TEST_F(ReferenceCanvasTest, theReferenceViewIsForReadingOnly) {
    EXPECT_TRUE(refView->isReadingOnly());
    EXPECT_FALSE(mainView->isReadingOnly());
    EXPECT_TRUE(refView->isShown());
    EXPECT_TRUE(mainView->isShown());
}

TEST_F(ReferenceCanvasTest, thePenWritesOnTheMainCanvasAndScrollsTheReference) {
    penStroke(QPointF(100, 200), QPointF(300, 250));
    EXPECT_EQ(elementCount(*mainSession), 1u) << "the pen writes on the document of the tab";

    const double before = refCanvas->contentY();
    penStroke(QPointF(600, 400), QPointF(600, 150));
    EXPECT_EQ(elementCount(*refSession), 0u) << "a stroke landed in the reference";
    EXPECT_FALSE(refSession->isModified());
    EXPECT_GT(refCanvas->contentY(), before + 150) << "the pen scrolls the reference, as the hand does";
    EXPECT_EQ(elementCount(*mainSession), 1u);

    // The mouse as well
    mouseStroke(QPoint(600, 450), QPoint(600, 300));
    EXPECT_EQ(elementCount(*refSession), 0u);
}

TEST_F(ReferenceCanvasTest, aStrokeThatCrossesTheDividerStaysOnTheMainCanvas) {
    const double refY = refCanvas->contentY();
    penStroke(QPointF(200, 300), QPointF(650, 320));
    EXPECT_EQ(elementCount(*mainSession), 1u) << "the stroke did not stay on the canvas it began on";
    EXPECT_EQ(elementCount(*refSession), 0u);
    EXPECT_DOUBLE_EQ(refCanvas->contentY(), refY) << "the reference moved under a stroke of the main canvas";
    // The next stroke begins anew (nothing is left holding the pen)
    penStroke(QPointF(100, 450), QPointF(250, 460));
    EXPECT_EQ(elementCount(*mainSession), 2u);

    mouseStroke(QPoint(200, 500), QPoint(650, 520));
    EXPECT_EQ(elementCount(*mainSession), 3u) << "the mouse stroke did not stay on the main canvas";
    EXPECT_EQ(elementCount(*refSession), 0u);
}

TEST_F(ReferenceCanvasTest, aPinchOnTheReferenceZoomsOnlyTheReference) {
    const double mainZoom = mainView->getViewController().zoom();
    const double refZoom = refView->getViewController().zoom();
    pinch(QPoint(600, 300), 40, 120);
    EXPECT_GT(refView->getViewController().zoom(), refZoom * 1.5) << "the reference did not zoom";
    EXPECT_DOUBLE_EQ(mainView->getViewController().zoom(), mainZoom) << "the main canvas zoomed as well";

    pinch(QPoint(200, 300), 40, 120);
    EXPECT_GT(mainView->getViewController().zoom(), mainZoom * 1.5);
}

TEST_F(ReferenceCanvasTest, theWheelScrollsTheCanvasUnderThePointer) {
    const double mainY = mainCanvas->contentY();
    const double refY = refCanvas->contentY();
    QWheelEvent wheel(QPointF(600, 300), window->mapToGlobal(QPointF(600, 300)), QPoint(), QPoint(0, -240),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    until([&] { return refCanvas->contentY() > refY; });
    EXPECT_GT(refCanvas->contentY(), refY);
    EXPECT_DOUBLE_EQ(mainCanvas->contentY(), mainY);
}

TEST_F(ReferenceCanvasTest, theSelectToolSelectsInTheReferenceButMovesNothing) {
    strokeInReference(QPointF(100, 100), QPointF(200, 150));
    ASSERT_EQ(elementCount(*refSession), 1u);
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    penStroke(onReferencePage(QPointF(80, 80)), onReferencePage(QPointF(230, 180)));
    ASSERT_NE(refView->getSelection(), nullptr) << "nothing was selected in the reference";
    EXPECT_EQ(mainView->getSelection(), nullptr);

    // Dragging the selection moves nothing (the selection stays, to be copied)
    const double x = refView->getSelection()->getXOnView();
    penStroke(onReferencePage(QPointF(150, 125)), onReferencePage(QPointF(250, 300)));
    ASSERT_NE(refView->getSelection(), nullptr) << "a press on the selection ended it";
    EXPECT_DOUBLE_EQ(refView->getSelection()->getXOnView(), x) << "the selection moved";
    EXPECT_TRUE(refView->copySelection());

    refView->clearSelection();
    wait(50);
    EXPECT_EQ(elementCount(*refSession), 1u);
    EXPECT_FALSE(refSession->getUndoRedoHandler()->canUndo()) << "the reference has something to undo";
    EXPECT_FALSE(refSession->isModified());
}

TEST_F(ReferenceCanvasTest, otherToolsNeverChangeTheReference) {
    strokeInReference(QPointF(100, 100), QPointF(200, 150));
    for (ToolType tool: {TOOL_ERASER, TOOL_HIGHLIGHTER, TOOL_TEXT, TOOL_DRAW_RECT}) {
        app->getToolHandler()->selectTool(tool);
        penStroke(onReferencePage(QPointF(90, 90)), onReferencePage(QPointF(210, 160)));
        tablet(onReferencePage(QPointF(150, 125)), Qt::LeftButton, 0.5);
        tablet(onReferencePage(QPointF(150, 125)), Qt::NoButton, 0.0);
        wait(50);
    }
    EXPECT_EQ(elementCount(*refSession), 1u);
    EXPECT_EQ(refView->getTextEditor(), nullptr) << "the text tool began a text in the reference";
    EXPECT_FALSE(refSession->getUndoRedoHandler()->canUndo());
}

TEST_F(ReferenceCanvasTest, aTwoFingerTapOnTheReferenceUndoesNothingThere) {
    // Something to undo in the reference (written there while it was the document of its tab)
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    refCanvas->setReadingOnly(false);
    app->getToolHandler()->selectTool(TOOL_PEN);
    penStroke(QPointF(500, 200), QPointF(650, 220));
    ASSERT_TRUE(refSession->getUndoRedoHandler()->canUndo());
    refCanvas->setReadingOnly(true);
    wait(300);
    const size_t before = elementCount(*refSession);
    QTest::touchEvent(window, screen).press(0, QPoint(560, 300)).press(1, QPoint(640, 300));
    QTest::touchEvent(window, screen).release(0, QPoint(560, 300)).release(1, QPoint(640, 300));
    wait(50);
    EXPECT_EQ(elementCount(*refSession), before) << "a two-finger tap undid something in the reference";
}
