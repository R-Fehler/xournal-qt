/*
 * xournal-qt: the pen's gestures (qt/docs/pen-gestures.md), replayed as Qt tablet events through the canvas:
 * hold to straighten (a stroke held still before the pen is lifted becomes the shape upstream's ShapeRecognizer sees
 * in it).
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>

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
#include "CanvasView.h"
#include "PenGestures.h"
#include "PenHover.h"

using namespace xqt;

namespace {
class PenGesturesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        tools()->selectTool(TOOL_PEN);
        tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
        PenHover::instance().reset();
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }

    ToolHandler* tools() const { return app->getToolHandler(); }
    Settings& settings() const { return *app->getSettings(); }

    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    QPointF viewPos(QPointF pagePoint) const {
        const QRectF r = view->pageViewRect(0);
        return r.topLeft() + pagePoint * view->getViewController().zoom();
    }

    void tablet(QEvent::Type type, QPointF pagePoint, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons,
                ulong step = 5) {
        const QPointF pos = viewPos(pagePoint);
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += step;
        input->tabletEvent(&e, pos);
    }

    /// The pen goes down at the first point and through the others (page coordinates); it stays down.
    void penDownAlong(const std::vector<QPointF>& path) {
        tablet(QEvent::TabletPress, path.front(), 0.5, Qt::LeftButton, Qt::LeftButton);
        for (size_t i = 1; i < path.size(); ++i) {
            tablet(QEvent::TabletMove, path[i], 0.5, Qt::NoButton, Qt::LeftButton);
        }
    }
    /// The pen rests where it is for `ms`, shaking by a pixel or so (a real pen keeps reporting).
    void rest(QPointF at, int ms) {
        QElapsedTimer t;
        t.start();
        int i = 0;
        while (t.elapsed() < ms) {
            const double shake = (i++ % 2 ? 0.8 : -0.8) / view->getViewController().zoom();
            tablet(QEvent::TabletMove, at + QPointF(shake, -shake), 0.5, Qt::NoButton, Qt::LeftButton);
            processEvents(20);
        }
    }
    void lift(QPointF at) { tablet(QEvent::TabletRelease, at, 0.0, Qt::LeftButton, Qt::NoButton); }

    /// A wobbly line from `a` to `b`: a hand's line, not a straight one
    static std::vector<QPointF> wobblyLine(QPointF a, QPointF b, int steps = 40) {
        std::vector<QPointF> path;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            path.push_back(a + (b - a) * t + QPointF(0, 2.0 * std::sin(t * 9)));
        }
        return path;
    }
    /// A rectangle drawn by hand (closed, corners a little round)
    static std::vector<QPointF> handRectangle(QRectF r) {
        std::vector<QPointF> path;
        const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()};
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < 20; ++i) {
                const double t = i / 20.0;
                path.push_back(corners[c] + (corners[c + 1] - corners[c]) * t + QPointF(0, 0.6 * std::sin(i)));
            }
        }
        path.push_back(r.topLeft() + QPointF(2, 1));
        return path;
    }

    std::vector<const Stroke*> strokes() const {
        std::vector<const Stroke*> out;
        for (const Element* e: session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView()) {
            if (auto* s = dynamic_cast<const Stroke*>(e)) {
                out.push_back(s);
            }
        }
        return out;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    ulong timestamp = 1000;
};
}  // namespace

// --- hold to straighten ---------------------------------------------------------------------------------------------

TEST_F(PenGesturesTest, holdingThePenStillStraightensALineBeforeTheLift) {
    ASSERT_TRUE(pengestures::holdToStraighten(settings()));  // (on by default)
    const auto path = wobblyLine({100, 200}, {400, 230});
    penDownAlong(path);
    rest(path.back(), pengestures::DEFAULT_HOLD_MS + 250);

    // Already while the pen rests: the line is in the document, straight (two points)
    auto now = strokes();
    ASSERT_EQ(now.size(), 1u);
    EXPECT_EQ(now.front()->getPointCount(), 2u);
    // Moving on after it does not draw on, nor start a stroke
    tablet(QEvent::TabletMove, {450, 300}, 0.5, Qt::NoButton, Qt::LeftButton);
    tablet(QEvent::TabletMove, {500, 350}, 0.5, Qt::NoButton, Qt::LeftButton);
    lift({500, 350});
    processEvents();

    now = strokes();
    ASSERT_EQ(now.size(), 1u);
    const Stroke* line = now.front();
    ASSERT_EQ(line->getPointCount(), 2u);
    EXPECT_NEAR(line->getPoint(0).x, 100, 6);
    EXPECT_NEAR(line->getPoint(1).x, 400, 6);
    EXPECT_FALSE(line->hasPressure());

    // Undo: the stroke as drawn; undo again: nothing. Redo twice: the line again.
    UndoRedoHandler* undo = session->getUndoRedoHandler();
    undo->undo();
    now = strokes();
    ASSERT_EQ(now.size(), 1u);
    EXPECT_GT(now.front()->getPointCount(), 20u);
    undo->undo();
    EXPECT_TRUE(strokes().empty());
    undo->redo();
    undo->redo();
    ASSERT_EQ(strokes().size(), 1u);
    EXPECT_EQ(strokes().front()->getPointCount(), 2u);
}

TEST_F(PenGesturesTest, aRectangleHeldStillBecomesARectangleAlsoWithTheHighlighter) {
    tools()->selectTool(TOOL_HIGHLIGHTER);
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    const auto path = handRectangle({150, 300, 250, 150});
    penDownAlong(path);
    rest(path.back(), pengestures::DEFAULT_HOLD_MS + 250);
    lift(path.back());
    processEvents();

    const auto now = strokes();
    ASSERT_EQ(now.size(), 1u);
    EXPECT_EQ(now.front()->getToolType(), StrokeTool::HIGHLIGHTER);
    EXPECT_LE(now.front()->getPointCount(), 5u);  // (four corners, closed)
    const auto box = now.front()->getBoundingBox();
    EXPECT_NEAR(box.width, 250, 15);
    EXPECT_NEAR(box.height, 150, 15);
}

TEST_F(PenGesturesTest, liftedAtOnceOrRestingTooShortlyTheStrokeStaysAsDrawn) {
    const auto path = wobblyLine({100, 200}, {400, 230});
    penDownAlong(path);
    lift(path.back());
    processEvents();
    ASSERT_EQ(strokes().size(), 1u);
    EXPECT_GT(strokes().front()->getPointCount(), 20u);

    const auto second = wobblyLine({100, 400}, {400, 430});
    penDownAlong(second);
    rest(second.back(), pengestures::DEFAULT_HOLD_MS / 2);
    lift(second.back());
    processEvents();
    ASSERT_EQ(strokes().size(), 2u);
    EXPECT_GT(strokes().back()->getPointCount(), 20u);
}

TEST_F(PenGesturesTest, withTheSettingOffNothingIsStraightened) {
    pengestures::setHoldToStraighten(settings(), false);
    const auto path = wobblyLine({100, 200}, {400, 230});
    penDownAlong(path);
    rest(path.back(), pengestures::DEFAULT_HOLD_MS + 250);
    lift(path.back());
    processEvents();
    ASSERT_EQ(strokes().size(), 1u);
    EXPECT_GT(strokes().front()->getPointCount(), 20u);
}

TEST_F(PenGesturesTest, theHoldTimeIsASetting) {
    pengestures::setHoldTime(settings(), 1200);
    const auto path = wobblyLine({100, 200}, {400, 230});
    penDownAlong(path);
    rest(path.back(), 700);  // (longer than the default, shorter than the setting)
    ASSERT_EQ(strokes().size(), 0u);
    rest(path.back(), 700);
    ASSERT_EQ(strokes().size(), 1u);
    EXPECT_EQ(strokes().front()->getPointCount(), 2u);
    lift(path.back());
}

TEST_F(PenGesturesTest, noShapeRecognisedTheStrokeGoesOnAndOnlyThePauseWasThere) {
    // A spiral: no line, polygon or circle
    std::vector<QPointF> spiral;
    for (int i = 0; i <= 120; ++i) {
        const double a = i * 0.15;
        spiral.push_back(QPointF(300, 300) + QPointF(std::cos(a), std::sin(a)) * (10 + i * 1.2));
    }
    penDownAlong(spiral);
    rest(spiral.back(), pengestures::DEFAULT_HOLD_MS + 250);
    EXPECT_TRUE(strokes().empty());  // (still being drawn)
    // It writes on after the pause
    tablet(QEvent::TabletMove, spiral.back() + QPointF(40, 0), 0.5, Qt::NoButton, Qt::LeftButton);
    tablet(QEvent::TabletMove, spiral.back() + QPointF(80, 10), 0.5, Qt::NoButton, Qt::LeftButton);
    lift(spiral.back() + QPointF(80, 10));
    processEvents();
    ASSERT_EQ(strokes().size(), 1u);
    const Stroke* s = strokes().front();
    EXPECT_GT(s->getPointCount(), 100u);
    EXPECT_NEAR(s->getPoint(s->getPointCount() - 1).x, spiral.back().x() + 80, 3);
}

TEST_F(PenGesturesTest, aPenHeldStillWhereItWentDownIsALongPressNotAShape) {
    QSignalSpy context(view.get(), &CanvasView::contextRequested);
    tablet(QEvent::TabletPress, {200, 200}, 0.5, Qt::LeftButton, Qt::LeftButton);
    rest({200, 200}, pengestures::DEFAULT_HOLD_MS + 250);
    lift({200, 200});
    processEvents();
    EXPECT_TRUE(strokes().empty());
    EXPECT_EQ(context.count(), 1);
}
