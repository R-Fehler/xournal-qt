/*
 * xournal-qt: a view replays its document's timeline (qt/docs/timeline.md, "Replay"): the pages as of a moment, the
 * stroke being written drawn on along its length, what came since drawn over the pages' pictures without drawing them
 * again, read-only (the pen writes nothing, a tap goes to the moment of the ink), and the document as it was after.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PageRaster.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/StickyNote.h"
#include "session/Timeline.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "TimelineReplay.h"

using namespace xqt;

namespace {
constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC

class TimelineReplayTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
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
    void processEvents(int ms = 60) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    /// A horizontal stroke of `length` points at height y, made at `created`
    Stroke* addStroke(double x, double y, double length, int64_t created, Layer* into = nullptr) {
        auto s = std::make_unique<Stroke>();
        s->setToolType(StrokeTool::PEN);
        s->setWidth(4);
        s->setColor(Color(0, 0, 0));
        for (int i = 0; i <= 20; ++i) {
            s->addPoint(Point(x + length * i / 20, y));
        }
        s->setCreated(created);
        Stroke* raw = s.get();
        Document* doc = session->getDocument();
        {
            std::unique_lock lock(*doc);
            (into ? into : doc->getPage(0)->getSelectedLayer())->addElement(std::move(s));
        }
        doc->getPage(0)->firePageChanged();
        return raw;
    }
    std::shared_ptr<const timeline::Timeline> build() {
        std::shared_lock lock(*session->getDocument());
        return std::make_shared<const timeline::Timeline>(timeline::Timeline::build(*session->getDocument(), {}));
    }
    /// Dark pixels of the canvas in a rectangle of page 0 (points)
    int dark(const QRectF& r) {
        auto* page = view->getPage(0);
        const auto info = page->bufferInfo();
        EXPECT_TRUE(info.valid);
        const double s = info.zoom * info.dpiScale;
        const QImage tile = page->composeTile(QRect(static_cast<int>(r.x() * s), static_cast<int>(r.y() * s),
                                                    static_cast<int>(r.width() * s), static_cast<int>(r.height() * s)));
        int n = 0;
        for (int y = 0; y < tile.height(); ++y) {
            for (int x = 0; x < tile.width(); ++x) {
                n += qGray(tile.pixel(x, y)) < 128 ? 1 : 0;
            }
        }
        return n;
    }
    QPointF viewPos(QPointF pagePoint) const {
        return view->pageViewRect(0).topLeft() + pagePoint * view->getViewController().zoom();
    }
    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"pen",
                        1,
                        QInputDevice::DeviceType::Stylus,
                        QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                        1,
                        3};
    quint64 timestamp = 1000;
};

const QRectF FIRST(100, 90, 160, 20);
const QRectF SECOND(100, 190, 160, 20);
const QRectF SECOND_LEFT(100, 190, 75, 20);
const QRectF SECOND_RIGHT(185, 190, 75, 20);
}  // namespace

// The pages as of a moment: what was written before it, the stroke being written as far as it got (evenly along its
// length), nothing after; leaving shows the whole document again
TEST_F(TimelineReplayTest, thePagesAsOfAMoment) {
    addStroke(100, 100, 160, T0);         // drawn on over 1 s
    addStroke(100, 200, 160, T0 + 3000);  // from 3 s to 4 s on the bar
    processEvents();
    ASSERT_GT(dark(FIRST), 100);
    ASSERT_GT(dark(SECOND), 100);

    view->startReplay(build(), 2000);
    EXPECT_TRUE(view->isReadingOnly());
    processEvents();
    EXPECT_GT(dark(FIRST), 100);
    EXPECT_EQ(dark(SECOND), 0) << "not written yet";

    view->seekReplay(3500);  // half of the second stroke
    EXPECT_GT(dark(SECOND_LEFT), 50) << "its first half, at once (drawn over the picture)";
    EXPECT_EQ(dark(SECOND_RIGHT), 0);
    view->seekReplay(3000 + 1000);
    EXPECT_GT(dark(SECOND_RIGHT), 50);

    view->seekReplay(500);  // back: half of the first one
    processEvents();
    EXPECT_EQ(dark(SECOND), 0);
    EXPECT_GT(dark(QRectF(100, 90, 75, 20)), 50);
    EXPECT_EQ(dark(QRectF(185, 90, 75, 20)), 0);

    view->endReplay();
    EXPECT_FALSE(view->isReadingOnly());
    processEvents();
    EXPECT_GT(dark(FIRST), 100);
    EXPECT_GT(dark(SECOND), 100);
}

// Playing forward does not draw the page's picture again for every element: what came is drawn over it, until there is
// too much of it; a picture drawn with the frame then takes its place, with nothing drawn twice or missing
TEST_F(TimelineReplayTest, playingDrawsOverThePictureAndCommitsNowAndThen) {
    for (int i = 0; i < 80; ++i) {
        addStroke(50 + (i % 10) * 50, 50 + (i / 10) * 40, 30, T0 + i * 1000);
    }
    processEvents();
    view->startReplay(build(), 0);
    processEvents();
    const TimelineReplay* replay = view->replay();
    ASSERT_TRUE(replay);
    const auto rendersBefore = PageRaster::stats().renders;
    for (int t = 0; t <= 20300; t += 100) {
        view->seekReplay(t);
    }
    processEvents();
    EXPECT_EQ(PageRaster::stats().renders, rendersBefore) << "21 strokes came: drawn over the picture";
    EXPECT_EQ(replay->frame().shown, 21u);
    EXPECT_GT(dark(QRectF(45, 40, 40, 20)), 20) << "the first stroke";
    view->seekReplay(79500);  // (more than MAX_OVERLAY at once: committed)
    EXPECT_EQ(replay->committedShown(), replay->frame().shown);
    processEvents();
    EXPECT_GT(PageRaster::stats().renders, rendersBefore);
    EXPECT_GT(dark(QRectF(445, 320, 40, 20)), 20) << "the stroke before the last";
    view->settleReplay();
    EXPECT_EQ(replay->committedShown(), replay->frame().shown);
}

// Read-only: the pen writes nothing, a tap on ink goes to the moment it was written; the document is as it was
TEST_F(TimelineReplayTest, readOnlyATapGoesToTheInksMoment) {
    addStroke(100, 100, 160, T0);
    addStroke(100, 200, 160, T0 + 3000);
    processEvents();
    const bool modified = session->isModified();
    const bool canUndo = session->getUndoRedoHandler()->canUndo();
    session->setReplaying(true);
    view->startReplay(build(), 10000);
    processEvents();
    QSignalSpy tapped(view.get(), &CanvasView::replayTapped);
    // A line with the pen: nothing written
    tablet(QEvent::TabletPress, viewPos({300, 400}), 0.6, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        tablet(QEvent::TabletMove, viewPos({300.0 + i * 10, 400}), 0.6, Qt::NoButton, Qt::LeftButton);
    }
    tablet(QEvent::TabletRelease, viewPos({400, 400}), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    EXPECT_EQ(session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView().size(), 2u);
    EXPECT_EQ(tapped.count(), 0);
    // A tap on the second stroke
    tablet(QEvent::TabletPress, viewPos({180, 201}), 0.6, Qt::LeftButton, Qt::LeftButton);
    tablet(QEvent::TabletRelease, viewPos({180, 201}), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_EQ(tapped.count(), 1);
    EXPECT_EQ(tapped.front().front().toLongLong(), 3000);
    EXPECT_TRUE(session->isReadOnly());
    view->endReplay();
    session->setReplaying(false);
    EXPECT_EQ(session->isModified(), modified);
    EXPECT_EQ(session->getUndoRedoHandler()->canUndo(), canUndo);
}

// A sticky note comes when it was put on the page: its paper and its ink are not there before
TEST_F(TimelineReplayTest, aStickyNoteComesWhenItWasPlaced) {
    addStroke(100, 100, 160, T0);
    sticky::Look look;
    look.rect = {300, 300, 150, 120};
    look.color = Color(0xfff59dU);
    Layer* note = sticky::makeNote(look);
    note->getElements().front()->setCreated(T0 + 5000);
    {
        Document* doc = session->getDocument();
        std::unique_lock lock(*doc);
        doc->getPage(0)->getLayers().push_back(note);
    }
    addStroke(320, 350, 100, T0 + 7000, note);
    session->getDocument()->getPage(0)->firePageChanged();
    processEvents();
    const QRectF paper(310, 310, 20, 20);
    auto* page = view->getPage(0);
    auto colorAt = [&](const QRectF& r) {
        const auto info = page->bufferInfo();
        const double s = info.zoom * info.dpiScale;
        return QColor(
                page->composeTile(QRect(static_cast<int>(r.x() * s), static_cast<int>(r.y() * s), 1, 1)).pixel(0, 0));
    };
    ASSERT_NE(colorAt(paper), QColor(Qt::white));
    view->startReplay(build(), 2000);
    processEvents();
    EXPECT_EQ(colorAt(paper), QColor(Qt::white)) << "the note is not there yet";
    view->seekReplay(5500);
    processEvents();
    EXPECT_NE(colorAt(paper), QColor(Qt::white)) << "its paper";
    EXPECT_EQ(dark(QRectF(320, 340, 100, 20)), 0) << "its ink not yet";
    view->seekReplay(10000);
    processEvents();
    EXPECT_GT(dark(QRectF(320, 340, 100, 20)), 50);
    view->endReplay();
}
