/*
 * xournal-qt: the pen's gestures (qt/docs/features/pen-gestures.md), replayed as Qt tablet events through the canvas:
 * hold to straighten (a stroke held still before the pen is lifted becomes the shape upstream's ShapeRecognizer sees
 * in it) and scratch out to erase (a quick zigzag over ink deletes it; never a stroke of the handwriting fixture).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <iostream>
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
#include "CanvasTime.h"
#include "CanvasView.h"
#include "PenGestures.h"
#include "PenHover.h"
#include "ScratchOut.h"
#include "config-test.h"

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
        view->setClock(clock);
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

    /// `ms` pass on the canvas's clock, the event loop and the renders run meanwhile (no real time passes)
    void processEvents(int ms = 30) { test::passTime(clock, *app, ms); }

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
        for (int i = 0, rested = 0; rested < ms; ++i, rested += 20) {
            const double shake = (i % 2 ? 0.8 : -0.8) / view->getViewController().zoom();
            tablet(QEvent::TabletMove, at + QPointF(shake, -shake), 0.5, Qt::NoButton, Qt::LeftButton);
            processEvents(20);
        }
    }
    /// A whole stroke, `stepMs` between the pen's reports (5: a pen at 200 Hz)
    void drawPath(const std::vector<QPointF>& path, ulong stepMs = 5) {
        tablet(QEvent::TabletPress, path.front(), 0.5, Qt::LeftButton, Qt::LeftButton, stepMs);
        for (size_t i = 1; i < path.size(); ++i) {
            tablet(QEvent::TabletMove, path[i], 0.5, Qt::NoButton, Qt::LeftButton, stepMs);
        }
        tablet(QEvent::TabletRelease, path.back(), 0.0, Qt::LeftButton, Qt::NoButton, stepMs);
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

    ManualClock clock;  ///< the canvas's time (CanvasTime.h): the tests move it on
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

// --- scratch out to erase -------------------------------------------------------------------------------------------

namespace {
/// A quick zigzag over [x0, x0 + w] at y0, `sweeps` sweeps, going down by `drift` per sweep
std::vector<QPointF> zigzag(double x0, double y0, double w, int sweeps, double drift) {
    std::vector<QPointF> path;
    for (int k = 0; k < sweeps; ++k) {
        const double a = k % 2 ? x0 + w : x0, b = k % 2 ? x0 : x0 + w;
        for (int i = 0; i < 12; ++i) {
            const double t = i / 12.0;
            path.emplace_back(a + (b - a) * t, y0 + drift * (k + t) + 1.5 * std::sin(t * M_PI));
        }
    }
    path.emplace_back(sweeps % 2 ? x0 + w : x0, y0 + drift * sweeps);
    return path;
}
std::vector<Point> points(const std::vector<QPointF>& path) {
    std::vector<Point> out;
    for (const QPointF& p: path) {
        out.emplace_back(p.x(), p.y());
    }
    return out;
}
}  // namespace

TEST_F(PenGesturesTest, scratchOutIsOffByDefault) {
    EXPECT_FALSE(pengestures::scratchOut(settings()));
    drawPath(wobblyLine({100, 200}, {200, 205}));
    drawPath(zigzag(90, 195, 120, 5, 2));
    processEvents();
    EXPECT_EQ(strokes().size(), 2u);  // (the zigzag is a stroke like any other)
}

TEST_F(PenGesturesTest, aQuickZigzagOverInkErasesItInOneUndoStep) {
    pengestures::setScratchOut(settings(), true);
    drawPath(wobblyLine({100, 200}, {200, 205}));  // a "word"
    drawPath(wobblyLine({110, 190}, {120, 215}));  // a letter of it, upright
    drawPath(wobblyLine({100, 300}, {200, 305}));  // the next line: stays
    ASSERT_EQ(strokes().size(), 3u);

    drawPath(zigzag(90, 194, 120, 5, 3));
    processEvents();
    auto now = strokes();
    ASSERT_EQ(now.size(), 1u);  // the zigzag is not kept, the word is gone
    EXPECT_NEAR(now.front()->getBoundingBox().y, 300, 5);

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(strokes().size(), 3u);
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(strokes().size(), 1u);
}

TEST_F(PenGesturesTest, aZigzagOverNothingOrDrawnSlowlyStaysAStroke) {
    pengestures::setScratchOut(settings(), true);
    drawPath(zigzag(100, 400, 120, 5, 3));  // nothing under it
    processEvents();
    EXPECT_EQ(strokes().size(), 1u);

    drawPath(wobblyLine({100, 200}, {200, 205}));
    drawPath(zigzag(90, 194, 120, 5, 3), 400);  // (0.4 s between reports: 25 s for 600 pt, slower than writing)
    processEvents();
    EXPECT_EQ(strokes().size(), 3u);
}

TEST_F(PenGesturesTest, aZigzagCrossingTheEndOfALongStrokeLeavesIt) {
    pengestures::setScratchOut(settings(), true);
    drawPath(wobblyLine({100, 200}, {400, 205}));  // a long line: the zigzag covers its start only
    drawPath(zigzag(90, 194, 60, 5, 3));
    processEvents();
    EXPECT_EQ(strokes().size(), 2u);
}

TEST_F(PenGesturesTest, theHighlighterDoesNotScratchOut) {
    pengestures::setScratchOut(settings(), true);
    drawPath(wobblyLine({100, 200}, {200, 205}));
    tools()->selectTool(TOOL_HIGHLIGHTER);
    drawPath(zigzag(90, 194, 120, 5, 3));
    processEvents();
    EXPECT_EQ(strokes().size(), 2u);
}

TEST_F(PenGesturesTest, zigzagsAreRecognisedAndHandwritingIsNot) {
    EXPECT_TRUE(scratchout::analyse(points(zigzag(100, 100, 80, 5, 3))).zigzag);
    EXPECT_TRUE(scratchout::analyse(points(zigzag(100, 100, 30, 4, 2))).zigzag);
    EXPECT_TRUE(scratchout::analyse(points(zigzag(100, 100, 60, 4, 8))).zigzag);  // (going down a block of lines)
    EXPECT_FALSE(scratchout::analyse(points(zigzag(100, 100, 60, 3, 3))).zigzag);  // (two turns: a "z")

    // Cursive "mmm" and "www": up and down while moving on along the line
    std::vector<QPointF> m, w, loops;
    for (int arch = 0; arch < 6; ++arch) {
        for (int i = 0; i <= 12; ++i) {
            const double a = M_PI * i / 12;
            m.emplace_back(100 + arch * 5 + 2.5 * (1 - std::cos(a)), 100 - 10 * std::sin(a));
        }
    }
    for (int v = 0; v < 6; ++v) {
        for (int i = 0; i <= 6; ++i) {
            w.emplace_back(100 + v * 5 + 2.5 * i / 6.0, 90 + 10 * i / 6.0);
        }
        for (int i = 0; i <= 6; ++i) {
            w.emplace_back(102.5 + v * 5 + 2.5 * i / 6.0, 100 - 10 * i / 6.0);
        }
    }
    // Tight loops ("eeee", "llll"): they turn back, on curves
    for (int i = 0; i < 190; ++i) {
        const double t = i * 0.1;
        loops.emplace_back(100 + 0.5 * t - 5 * std::sin(t), 100 - 5 * std::cos(t));
    }
    EXPECT_FALSE(scratchout::analyse(points(m)).zigzag);
    EXPECT_FALSE(scratchout::analyse(points(w)).zigzag);
    EXPECT_FALSE(scratchout::analyse(points(loops)).zigzag);
}

TEST_F(PenGesturesTest, noStrokeOfTheHandwritingFixtureIsAScratchOut) {
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"benchmark/handwritten-text.xopp"));
    ASSERT_TRUE(loaded.document);
    size_t seen = 0;
    int mostReversals = 0;
    for (size_t p = 0; p < loaded.document->getPageCount(); ++p) {
        for (const Layer* layer: loaded.document->getPage(p)->getLayersView()) {
            for (const Element* e: layer->getElementsView()) {
                if (const auto* s = dynamic_cast<const Stroke*>(e)) {
                    const auto shape = scratchout::analyse(s->getPointVector());
                    EXPECT_FALSE(shape.zigzag) << "page " << p << ", a stroke at " << s->getBoundingBox().x << ", "
                                               << s->getBoundingBox().y;
                    mostReversals = std::max(mostReversals, shape.reversals);
                    ++seen;
                }
            }
        }
    }
    EXPECT_GT(seen, 10000u);
    std::cout << "handwriting fixture: " << seen << " strokes, at most " << mostReversals << " turns" << std::endl;
}
