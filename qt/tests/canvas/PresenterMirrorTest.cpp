/*
 * xournal-qt: the audience's screen of the presenter view (qt/docs/features/presenter-view.md) at the level of the
 * views: the audience's view shows only the slide (not its space for notes), and what the presenter's view shows only
 * for a moment - a stroke being written, the laser pointer's ink, the curtain - shows on it too.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PageNoteSpace.h"

#include "AudienceRegion.h"
#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasTime.h"
#include "CanvasView.h"
#include "CurtainLayer.h"
#include "ViewController.h"

using namespace xqt;

namespace {
/// Three pages; the presenter's view (the tab's) and the audience's (a second view of the document)
class PresenterMirror: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);
        session->insertNewPage(2);
        session->setCurrentPageNo(0);
        presenter = std::make_unique<CanvasView>(*session);
        presenter->setClock(clock);
        presenter->getViewController().setViewSize(QSizeF(900, 700));
        audience = std::make_unique<CanvasView>(*session);
        audience->setClock(clock);
        audience->getViewController().setViewSize(QSizeF(800, 600));
        input = std::make_unique<CanvasInput>(*presenter);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        audience.reset();
        presenter.reset();
        session.reset();
        app.reset();
    }
    /// `ms` pass on the canvas's clock, the event loop and the renders run meanwhile (no real time passes)
    void processEvents(int ms = 30) { test::passTime(clock, *app, ms); }
    void tablet(QEvent::Type type, QPointF onPage, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF pos = presenter->pageViewRect(0).topLeft() + onPage * presenter->getViewController().zoom();
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(time += 5);
        input->tabletEvent(&e, pos);
    }
    /// The pen pressed on page 1 of the presenter's view and moved, not lifted
    void penDown() {
        tablet(QEvent::TabletPress, QPointF(100, 100), 0.5, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) {
            tablet(QEvent::TabletMove, QPointF(100 + 8 * i, 100 + 3 * i), 0.5, Qt::NoButton, Qt::LeftButton);
        }
    }
    void penUp() {
        tablet(QEvent::TabletRelease, QPointF(180, 130), 0.0, Qt::LeftButton, Qt::NoButton);
        tablet(QEvent::TabletLeaveProximity, QPointF(180, 130), 0.0, Qt::NoButton, Qt::NoButton);
        input->proximityEvent(false);
    }
    size_t elementCount(size_t i) const {
        size_t n = 0;
        for (const Layer* l: session->getDocument()->getPage(i)->getLayersView()) {
            n += l->getElementsView().size();
        }
        return n;
    }

    ManualClock clock;  ///< the canvas's time (CanvasTime.h): the tests move it on
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> presenter;
    std::unique_ptr<CanvasView> audience;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    ulong time = 1000;
};
}  // namespace

TEST_F(PresenterMirror, theAudienceSeesTheSlideWithoutItsSpaceForNotes) {
    // Half the slide's width on the right, as high as the slide below (the presets of "Space for notes")
    const QSizeF slide(session->getDocument()->getPage(1)->getWidth(), session->getDocument()->getPage(1)->getHeight());
    ASSERT_EQ(notespace::apply(*session, {1}, {0, 0, 0.5, 1.0, true}), 1u);
    const NoteSpace space = session->getDocument()->getPage(1)->getNoteSpace();
    ASSERT_GT(space.right, 0);
    ASSERT_GT(space.bottom, 0);
    processEvents();

    // The audience's screen is as wide as the slide is (16:9 or whatever it is): the slide fills it, nothing else
    const QSizeF screen(1200, 1200 * slide.height() / slide.width());
    audience->getViewController().setViewSize(screen);
    audience->getViewController().fitPageRect(1, QRectF(QPointF(space.left, space.top), slide));
    processEvents();
    const double z = audience->getViewController().zoom();
    const QRectF page = audience->pageViewRect(1);
    EXPECT_NEAR(z * slide.width(), screen.width(), 0.5);
    EXPECT_NEAR(page.left(), 0, 0.5) << "the slide's left edge at the screen's";
    EXPECT_NEAR(page.top(), 0, 0.5);
    EXPECT_GT(page.width(), screen.width() + 100) << "the space beside it is out of view";
    EXPECT_GT(page.height(), screen.height() + 100);
    EXPECT_EQ(audience->getViewController().keptFit(), ViewController::Fit::Rect);

    // Another size of the screen (the projector's mode changed): the slide fills it again
    const QSizeF bigger(screen * 1.5);
    audience->getViewController().setViewSize(bigger);
    processEvents();
    EXPECT_NEAR(audience->getViewController().zoom() * slide.width(), bigger.width(), 0.5);
    EXPECT_NEAR(audience->pageViewRect(1).left(), 0, 0.5);
    EXPECT_NEAR(audience->pageViewRect(1).top(), 0, 0.5);

    // A screen of another shape: the slide as large as it fits, in the middle
    audience->getViewController().setViewSize(QSizeF(bigger.width(), bigger.height() + 200));
    processEvents();
    EXPECT_NEAR(audience->pageViewRect(1).left(), 0, 0.5);
    EXPECT_NEAR(audience->pageViewRect(1).top(), 100, 0.5);

    // Space on the left and at the top as well: the slide still fills the screen
    ASSERT_EQ(notespace::apply(*session, {2}, {72, 36, 0, 0, false}), 1u);
    processEvents();
    audience->getViewController().setViewSize(screen);
    audience->getViewController().fitPageRect(2, QRectF(QPointF(72, 36), slide));
    processEvents();
    const double z2 = audience->getViewController().zoom();
    EXPECT_NEAR(audience->pageViewRect(2).left() + 72 * z2, 0, 0.5);
    EXPECT_NEAR(audience->pageViewRect(2).top() + 36 * z2, 0, 0.5);
}

TEST_F(PresenterMirror, aStrokeShowsOnTheAudiencesPageWhileItIsWritten) {
    presenter->setMirror(audience.get());
    EXPECT_EQ(presenter->mirror(), audience.get());
    EXPECT_EQ(audience->mirroredView(), presenter.get());
    app->getToolHandler()->selectTool(TOOL_PEN);

    penDown();
    processEvents();
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 1u) << "the stroke being written";
    EXPECT_EQ(audience->getPage(1)->mirroredViewCount(), 0u);
    EXPECT_EQ(elementCount(0), 0u);
    penUp();
    processEvents();
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 0u) << "finished: it is in the document now";
    EXPECT_EQ(elementCount(0), 1u);

    // Without a mirror nothing is shown elsewhere
    presenter->setMirror(nullptr);
    EXPECT_EQ(audience->mirroredView(), nullptr);
    penDown();
    processEvents();
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 0u);
    penUp();
}

TEST_F(PresenterMirror, theLaserPointerShowsOnTheAudiencesPage) {
    app->getSettings()->setLaserPointerFadeOutTime(200);
    presenter->setMirror(audience.get());
    app->getToolHandler()->selectTool(TOOL_LASER_POINTER_PEN);

    penDown();
    processEvents();
    ASSERT_TRUE(presenter->getPage(0)->hasLaserInk());
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 1u);
    penUp();
    processEvents();
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 1u) << "it fades on both screens";
    EXPECT_EQ(elementCount(0), 0u) << "never in the document";

    // It fades away (200 ms, then the steps of the fade)
    QElapsedTimer t;
    t.start();
    while (presenter->getPage(0)->hasLaserInk() && t.elapsed() < 5000) {
        processEvents(50);
    }
    EXPECT_FALSE(presenter->getPage(0)->hasLaserInk());
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 0u) << "gone on the audience's screen with it";
}

TEST_F(PresenterMirror, eitherViewMayGoFirst) {
    app->getToolHandler()->selectTool(TOOL_LASER_POINTER_PEN);
    presenter->setMirror(audience.get());
    penDown();
    processEvents();
    ASSERT_EQ(audience->getPage(0)->mirroredViewCount(), 1u);
    // The presenter's view goes (another tab presents) while the laser's ink is out
    input.reset();
    presenter.reset();
    EXPECT_EQ(audience->mirroredView(), nullptr);
    EXPECT_EQ(audience->getPage(0)->mirroredViewCount(), 0u);
    processEvents();

    // The audience's view goes first (the projector is unplugged): the presenter's writes on
    presenter = std::make_unique<CanvasView>(*session);
    presenter->setClock(clock);
    presenter->getViewController().setViewSize(QSizeF(900, 700));
    input = std::make_unique<CanvasInput>(*presenter);
    processEvents();
    presenter->setMirror(audience.get());
    app->getToolHandler()->selectTool(TOOL_PEN);
    penDown();
    processEvents();
    audience.reset();
    EXPECT_EQ(presenter->mirror(), nullptr);
    penUp();
    processEvents();
    EXPECT_EQ(elementCount(0), 1u);
}

TEST_F(PresenterMirror, theCurtainShowsOnTheAudiencesScreenWithoutHandles) {
    presenter->setMirror(audience.get());
    CurtainLayer& theirs = audience->curtain();
    presenter->curtain().show(CurtainLayer::Shape::Curtain);
    processEvents();
    ASSERT_TRUE(presenter->curtain().handlesShown());
    ASSERT_TRUE(theirs.visible());
    EXPECT_EQ(theirs.shape(), CurtainLayer::Shape::Curtain);
    EXPECT_EQ(theirs.page(), audience->getPage(0));
    EXPECT_EQ(theirs.centre(), presenter->curtain().centre());
    EXPECT_EQ(theirs.size(), presenter->curtain().size());
    EXPECT_FALSE(theirs.handlesShown()) << "the audience sees the sheet, not its handles";

    // Moved, sized and turned: the same on the audience's page (in page coordinates: whatever its zoom)
    presenter->curtain().place(QPointF(200, 300), QSizeF(300, 150), 0.3);
    processEvents();
    EXPECT_EQ(theirs.centre(), QPointF(200, 300));
    EXPECT_EQ(theirs.size(), QSizeF(300, 150));
    EXPECT_DOUBLE_EQ(theirs.rotation(), 0.3);

    // The spotlight instead, then away
    presenter->curtain().show(CurtainLayer::Shape::Spotlight);
    processEvents();
    EXPECT_EQ(theirs.shape(), CurtainLayer::Shape::Spotlight);
    presenter->curtain().hide();
    processEvents();
    EXPECT_FALSE(theirs.active());

    // Out when the audience's screen comes: shown there at once; the mirror ends: away there
    presenter->setMirror(nullptr);
    presenter->curtain().show(CurtainLayer::Shape::Curtain);
    EXPECT_FALSE(theirs.active());
    presenter->setMirror(audience.get());
    EXPECT_TRUE(theirs.visible());
    presenter->setMirror(nullptr);
    EXPECT_FALSE(theirs.active());
}

// Following the presenter's zoom (qt/docs/features/presenter-view.md): what the audience sees is what the presenter
// sees of the slide, widened to the audience's screen's shape, kept within the slide, never less than the presenter
// sees
TEST(AudienceRegion, widenedToTheScreensShapeWithinTheSlide) {
    using presenter::audienceRegion;
    const QRectF slide(0, 0, 960, 540);  // 16:9
    const QSizeF wide(1600, 900);
    const QSizeF fourThree(1024, 768);
    auto contains = [](QRectF outer, QRectF inner) {
        return outer.adjusted(-1e-9, -1e-9, 1e-9, 1e-9).contains(inner);
    };

    // Seen: the whole slide (or more) - the whole slide
    EXPECT_EQ(audienceRegion(slide, QRectF(-100, -50, 2000, 1000), wide), slide);
    // Nothing of the slide seen (the presenter looks at the space for notes): the whole slide
    EXPECT_EQ(audienceRegion(slide, QRectF(1000, 0, 300, 300), wide), slide);

    // A square in the middle on a 16:9 screen: as high, wider, the same middle
    const QRectF square(380, 170, 200, 200);
    const QRectF r = audienceRegion(slide, square, wide);
    EXPECT_TRUE(contains(r, square));
    EXPECT_NEAR(r.height(), 200, 1e-6);
    EXPECT_NEAR(r.width() / r.height(), 16.0 / 9.0, 1e-6);
    EXPECT_NEAR(r.center().x(), square.center().x(), 1e-6);
    EXPECT_NEAR(r.center().y(), square.center().y(), 1e-6);

    // A wide strip on a 4:3 screen: as wide, taller
    const QRectF strip(100, 200, 400, 100);
    const QRectF t = audienceRegion(slide, strip, fourThree);
    EXPECT_TRUE(contains(t, strip));
    EXPECT_NEAR(t.width(), 400, 1e-6);
    EXPECT_NEAR(t.width() / t.height(), 4.0 / 3.0, 1e-6);

    // At the slide's corner: moved into the slide, not cut (it is smaller than the slide), what is seen still in it
    const QRectF corner(0, 0, 150, 150);
    const QRectF c = audienceRegion(slide, corner, wide);
    EXPECT_TRUE(contains(slide, c));
    EXPECT_TRUE(contains(c, corner));
    EXPECT_NEAR(c.left(), 0, 1e-6);
    EXPECT_NEAR(c.top(), 0, 1e-6);
    EXPECT_NEAR(c.width() / c.height(), 16.0 / 9.0, 1e-6);

    // Partly beside the slide (the space for notes at the right in view too): only the slide's part counts
    const QRectF beside(800, 100, 400, 200);
    const QRectF b = audienceRegion(slide, beside, wide);
    EXPECT_TRUE(contains(slide, b));
    EXPECT_TRUE(contains(b, beside.intersected(slide)));

    // A tall part on a wide screen, taller than the slide allows to widen: cut to the slide (letterboxed there)
    const QRectF tall(400, 0, 100, 540);
    const QRectF l = audienceRegion(slide, tall, QSizeF(3200, 900));
    EXPECT_EQ(l, slide);

    // A slide inside a larger page (space for notes on the left and at the top): within the slide, in page points
    const QRectF offset(72, 36, 960, 540);
    const QRectF o = audienceRegion(offset, QRectF(50, 20, 100, 100), wide);
    EXPECT_TRUE(contains(offset, o));
    EXPECT_TRUE(contains(o, QRectF(72, 36, 78, 84)));
}
