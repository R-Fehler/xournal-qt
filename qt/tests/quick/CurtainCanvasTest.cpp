/*
 * xournal-qt: the curtain on the canvas item (real Qt Quick window, off-screen; qt/docs/curtain.md): a node of its own
 * over the pages, black where it lies; moving, turning and sizing it change only that node (no page tile is drawn
 * again); its handles are drawn while they are shown. Nothing of it is in the pages' own pictures.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "CurtainLayer.h"
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

class CurtainCanvasTest: public ::testing::Test {
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
        settle(400);
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
    QImage shot() {
        QImage image = window->grabWindow();
        if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
            static int shots = 0;
            image.save(QStringLiteral("%1/curtain-%2.png").arg(qEnvironmentVariable("XQT_TEST_SHOT")).arg(++shots));
        }
        return image;
    }
    /// A point of the first page in the window's picture (device pixels)
    QPoint inShot(QPointF pagePoint) const {
        const QPointF p = view->pageViewRect(0).topLeft() + pagePoint * view->getViewController().zoom();
        return (p * window->effectiveDevicePixelRatio()).toPoint();
    }
    static bool black(QRgb c) { return qRed(c) < 20 && qGreen(c) < 20 && qBlue(c) < 20; }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
};
}  // namespace

// The window shows the black where the curtain lies and the page beside it; moved, it goes along and the page shows
// again where it was; taken away, nothing is left.
TEST_F(CurtainCanvasTest, theWindowShowsItBlackWhereItLies) {
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Curtain);
    curtain.place(QPointF(300, 300), QSizeF(200, 120), 0);
    curtain.setHandlesShown(false);
    ASSERT_TRUE(nextFrame());
    settle(100);
    QImage image = shot();
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(300, 300))))) << "black where it lies";
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(390, 350)))));
    EXPECT_FALSE(black(image.pixel(inShot(QPointF(300, 200))))) << "the page above it";
    EXPECT_FALSE(black(image.pixel(inShot(QPointF(450, 300))))) << "and beside it";

    curtain.place(QPointF(300, 450), QSizeF(200, 120), 0);
    ASSERT_TRUE(nextFrame());
    image = shot();
    EXPECT_FALSE(black(image.pixel(inShot(QPointF(300, 300))))) << "nothing left where it was";
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(300, 450)))));

    curtain.hide();
    ASSERT_TRUE(nextFrame());
    EXPECT_FALSE(canvas->curtainShown().shown);
    EXPECT_FALSE(black(shot().pixel(inShot(QPointF(300, 450))))) << "taken away";
}

// Moving, turning and sizing it change only its node: no page tile is composed again.
TEST_F(CurtainCanvasTest, movingTurningAndSizingItDrawNoPageAgain) {
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Curtain);
    ASSERT_TRUE(nextFrame());
    settle(300);
    const auto before = canvas->curtainShown();
    ASSERT_TRUE(before.shown);
    EXPECT_EQ(before.handles, 9) << "put out with its handles: four corners, four edges and the knob";
    canvas->forgetFrameStats();
    for (int i = 0; i < 10; ++i) {
        curtain.place(curtain.centre() + QPointF(3, -4), curtain.size() * 1.01, curtain.rotation() + 0.02);
        ASSERT_TRUE(nextFrame());
    }
    const auto after = canvas->curtainShown();
    EXPECT_NE(after.body, before.body) << "its transform follows it";
    EXPECT_NE(after.sheet, before.sheet) << "and its size";
    EXPECT_EQ(canvas->frameStats().tiles, 0) << "the pages are not drawn again";

    curtain.setHandlesShown(false);
    ASSERT_TRUE(nextFrame());
    EXPECT_EQ(canvas->curtainShown().handles, 0);
    EXPECT_TRUE(canvas->curtainShown().shown);
}

// It is the view's, not the document's: the page's own picture (what thumbnails, previews, export and print are made
// from) has nothing of it.
TEST_F(CurtainCanvasTest, thePagesOwnPictureHasNothingOfIt) {
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Curtain);
    curtain.place(QPointF(300, 300), QSizeF(400, 400), 0);
    ASSERT_TRUE(nextFrame());
    settle(300);
    CanvasPage* page = view->getPage(0);
    const auto info = page->bufferInfo();
    ASSERT_TRUE(info.valid);
    const QImage tile = page->composeTile(QRect(0, 0, 256, 256).translated(
            (QPointF(300, 300) * info.zoom * info.dpiScale).toPoint() - QPoint(128, 128)));
    ASSERT_FALSE(tile.isNull());
    EXPECT_FALSE(black(tile.pixel(128, 128))) << "the page under the curtain is drawn as it is";
}

// The spotlight: the whole canvas is black (around the page as well) but its hole, whose corners are rounded.
TEST_F(CurtainCanvasTest, theSpotlightIsBlackAllAroundItsHole) {
    CurtainLayer& curtain = view->curtain();
    curtain.show(CurtainLayer::Shape::Spotlight);
    curtain.place(QPointF(300, 300), QSizeF(200, 120), 0.3);
    curtain.setHandlesShown(false);
    ASSERT_TRUE(nextFrame());
    settle(100);
    const QImage image = shot();
    const double dpr = window->effectiveDevicePixelRatio();
    EXPECT_TRUE(black(image.pixel(QPoint(3, 3) * dpr))) << "the corner of the window (beside the page) is black";
    EXPECT_TRUE(black(image.pixel(QPoint(996, 796) * dpr)));
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(50, 50))))) << "the page outside the hole is black";
    EXPECT_FALSE(black(image.pixel(inShot(QPointF(300, 300))))) << "its hole shows the page";
    // Turned by 0.3: a point 80 points along its long side from its middle is in the hole
    const QPointF along(80 * std::cos(0.3), 80 * std::sin(0.3));
    EXPECT_FALSE(black(image.pixel(inShot(QPointF(300, 300) + along))));
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(300, 300) + along * 1.5))));
    // Its corner (2 points inside, where a square corner would be in the hole) is black
    const QPointF corner(98, -58);
    const QPointF c(corner.x() * std::cos(0.3) - corner.y() * std::sin(0.3),
                    corner.x() * std::sin(0.3) + corner.y() * std::cos(0.3));
    EXPECT_TRUE(black(image.pixel(inShot(QPointF(300, 300) + c)))) << "rounded";
    EXPECT_TRUE(canvas->curtainShown().spotlight);
}
