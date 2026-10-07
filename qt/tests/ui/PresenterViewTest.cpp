/*
 * xournal-qt: the presenter view on a second screen (qt/docs/presenter-view.md) in the real window, off-screen.
 *
 * Qt's off-screen platform takes its screens from a file: PresenterView.ui@2screens runs these tests with two
 * (qt/tests/ui/offscreen-two-screens.json: a laptop 1920 x 1080, the primary one, and a projector 1280 x 720 at its
 * right). In the ordinary run (one screen) the tests that need two skip, and the one-screen test checks that
 * presenting is as before.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTest>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "canvas/CanvasPage.h"
#include "canvas/CanvasView.h"
#include "canvas/CurtainLayer.h"
#include "canvas/ViewController.h"
#include "session/DocumentSession.h"
#include "session/PageNoteSpace.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/PresenterConsole.h"
#include "shell/DocumentCovers.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"
#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::fixturePath;

namespace {

class PresenterView: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        twoScreens = QGuiApplication::screens().size() >= 2;
        makeController();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.activate = true}));
        ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
        wait(100);
        controller->jumpToPage(0);  // (not where an earlier test of this run left it)
        wait(50);
        console = qobject_cast<xqt::PresenterConsole*>(controller->presenterObject());
        ASSERT_NE(console, nullptr);
        audienceWindow = window->findChild<QQuickWindow*>("audienceWindow");
        ASSERT_NE(audienceWindow, nullptr);
    }
    void TearDown() override {
        controller->setPresenting(false);
        settings()->set("presenterView", true);  // (the settings are shared by the tests of this run)
        settings()->set("presenterSwapScreens", false);
        settings()->set("presenterShowNotes", false);
        settings()->set("presenterFollowView", true);
        closeApp();
    }

    QQuickItem* findIn(QQuickWindow* w, const char* name) const {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == name) {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        QQuickItem* root = w->contentItem()->parentItem() ? w->contentItem()->parentItem() : w->contentItem();
        return walk(root);
    }
    QQuickItem* find(const char* name) const { return findIn(window, name); }
    static QRectF sceneRect(const QQuickItem* i) { return i->mapRectToScene(QRectF(0, 0, i->width(), i->height())); }
    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }
    xqt::DocumentSession* session() const { return controller->tabManager().currentSession(); }
    xqt::CanvasView* presenterView() const { return controller->tabManager().currentView(); }
    xqt::CanvasView* audience() const { return console->audienceView(); }
    /// F5, until the presentation settled (and with two screens, the audience's window shows)
    void present() {
        key(Qt::Key_F5);
        until([&] { return controller->presenting() && (!console->available() || audienceWindow->isVisible()); });
        wait(200);
    }
    /// The page goes on (or back) and the views settle
    void settle() {
        until([&] { return !presenterView()->getViewController().isAnimating(); }, 2000);
        wait(50);
    }
    size_t strokes(size_t page) const {
        return session()->getDocument()->getPage(page)->getSelectedLayer()->getElementsView().size();
    }

    bool twoScreens = false;
    QQuickWindow* audienceWindow = nullptr;
    xqt::PresenterConsole* console = nullptr;
};
}  // namespace

// With one screen (or the presenter view switched off) presenting is as before: the page fills this window, no
// audience's window, no console
TEST_F(PresenterView, withOneScreenPresentingIsAsBefore) {
    if (twoScreens) {
        settings()->set("presenterView", false);
    }
    EXPECT_FALSE(console->available());
    present();
    ASSERT_TRUE(controller->presenting());
    EXPECT_FALSE(console->active());
    EXPECT_EQ(console->audienceView(), nullptr);
    EXPECT_FALSE(audienceWindow->isVisible());
    EXPECT_FALSE(find("presenterPanel")->isVisible());
    const QRectF canvas = sceneRect(find("canvas"));
    EXPECT_NEAR(canvas.right(), window->width(), 1) << "the page has the whole window";
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting());
}

// Two screens: the slide alone full screen on the projector (without its space for notes), the console on the laptop
// with the page and its space for notes, the next page, the page number; the keys go from page to page in both
// windows and the audience follows; Escape ends it everywhere
TEST_F(PresenterView, theSlideOnTheAudiencesScreenTheConsoleOnTheLaptop) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    QScreen* laptop = QGuiApplication::primaryScreen();
    QScreen* projector = QGuiApplication::screens().at(1);
    ASSERT_NE(laptop, projector);
    // The first page gets space for notes at its right (half the slide's width)
    const double slideWidth = session()->getDocument()->getPage(0)->getWidth();
    const double slideHeight = session()->getDocument()->getPage(0)->getHeight();
    ASSERT_EQ(xqt::notespace::apply(*session(), {0}, {0, 0, 0.5, 0, true}), 1u);
    wait(100);
    const double pageWidth = session()->getDocument()->getPage(0)->getWidth();
    ASSERT_GT(pageWidth, slideWidth * 1.4);

    EXPECT_TRUE(console->available());
    EXPECT_EQ(console->audienceScreen(), projector);
    EXPECT_EQ(console->consoleScreen(), laptop);
    present();
    ASSERT_TRUE(controller->presenting());
    ASSERT_TRUE(console->active());
    ASSERT_NE(audience(), nullptr);
    EXPECT_NE(audience(), presenterView()) << "a view of its own";
    EXPECT_EQ(&audience()->getSession(), session()) << "of the same document";
    EXPECT_EQ(presenterView()->mirror(), audience());

    // The windows on their screens
    EXPECT_TRUE(audienceWindow->isVisible());
    EXPECT_EQ(audienceWindow->screen(), projector);
    until([&] { return audienceWindow->geometry() == projector->geometry(); });
    EXPECT_EQ(audienceWindow->geometry(), projector->geometry()) << "full screen on the projector";
    EXPECT_EQ(window->screen(), laptop);

    // The console: the page with its space for notes at the left, the panel at the right
    auto* panel = find("presenterPanel");
    ASSERT_TRUE(panel->isVisible());
    auto* canvas = find("canvas");
    EXPECT_LE(sceneRect(canvas).right(), sceneRect(panel).left() + 0.5) << "the page beside the panel";
    const QRectF consolePage = presenterView()->pageViewRect(0);
    EXPECT_GE(consolePage.left(), -0.5);
    EXPECT_LE(consolePage.right(), canvas->width() + 0.5) << "the whole page, with its space for notes";
    EXPECT_NEAR(consolePage.width() / consolePage.height(), pageWidth / slideHeight, 0.01);
    EXPECT_TRUE(find("presenterNotesHint")->isVisible());
    EXPECT_EQ(find("presenterPageLabel")->property("text").toString(),
              QString("Page 1 of %1").arg(controller->pageCount()));

    // The audience's screen: only the slide, as large as the screen allows, its space for notes out of view
    auto* slide = findIn(audienceWindow, "audienceCanvas");
    ASSERT_NE(slide, nullptr);
    EXPECT_EQ(slide->property("view").value<QObject*>(), audience());
    EXPECT_NEAR(slide->width() / slide->height(), slideWidth / slideHeight, 0.01) << "the slide's shape";
    EXPECT_NEAR(slide->height(), projector->geometry().height(), 1) << "an A4 page upright fills the height";
    const QRectF audiencePage = audience()->pageViewRect(0);
    const double z = audience()->getViewController().zoom();
    EXPECT_NEAR(audiencePage.left(), 0, 0.5);
    EXPECT_NEAR(audiencePage.top(), 0, 0.5);
    EXPECT_NEAR(slideWidth * z, slide->width(), 0.5) << "the slide fills the canvas";
    EXPECT_GT(audiencePage.right(), slide->width() + 100) << "the space for notes is beside it, out of view";
    EXPECT_TRUE(slide->clip());

    // The next page, smaller
    EXPECT_TRUE(console->nextPicture().contains(QString("/1/"))) << console->nextPicture().toStdString();
    EXPECT_TRUE(find("presenterNext")->isVisible());

    // Keys in the console: on, and the audience follows
    key(Qt::Key_Space);
    settle();
    EXPECT_EQ(controller->pageNumber(), 2);
    until([&] { return audience()->currentPageNo() == 1; });
    EXPECT_EQ(audience()->currentPageNo(), 1u);
    EXPECT_EQ(console->page(), 1);
    EXPECT_FALSE(find("presenterNotesHint")->isVisible()) << "no space for notes on page 2";
    EXPECT_NEAR(audience()->pageViewRect(1).left(), 0, 0.5);
    EXPECT_NEAR(audience()->pageViewRect(1).top(), 0, 0.5);
    EXPECT_EQ(find("presenterPageLabel")->property("text").toString(),
              QString("Page 2 of %1").arg(controller->pageCount()));

    // Keys in the audience's window (a clicker sends them to whichever window has the focus)
    audienceWindow->requestActivate();
    until([&] { return audienceWindow->isActive(); });
    ASSERT_TRUE(audienceWindow->isActive());
    QTest::keyClick(audienceWindow, Qt::Key_Right);
    settle();
    EXPECT_EQ(controller->pageNumber(), 3);
    until([&] { return audience()->currentPageNo() == 2; });
    EXPECT_EQ(audience()->currentPageNo(), 2u);
    QTest::keyClick(audienceWindow, Qt::Key_PageUp);
    settle();
    EXPECT_EQ(controller->pageNumber(), 2);
    QTest::keyClick(audienceWindow, Qt::Key_End);
    settle();
    EXPECT_EQ(controller->pageNumber(), controller->pageCount());
    until([&] { return console->page() == controller->pageCount() - 1; });
    EXPECT_TRUE(console->nextPicture().isEmpty()) << "after the last page nothing comes";
    EXPECT_EQ(find("presenterNextLabel")->property("text").toString(), QString("The last page"));
    // A page number typed there: on in the console (the audience does not see it), Enter goes there
    QTest::keyClick(audienceWindow, Qt::Key_3);
    until([&] { return window->isActive(); });
    EXPECT_TRUE(window->isActive()) << "the number is typed on in the console";
    EXPECT_TRUE(find("pageJump")->isVisible());
    key(Qt::Key_Return);
    settle();
    EXPECT_EQ(controller->pageNumber(), 3);
    until([&] { return audience()->currentPageNo() == 2; });
    EXPECT_EQ(audience()->currentPageNo(), 2u);

    // Escape in the audience's window ends the presentation: its window goes, the console too
    audienceWindow->requestActivate();
    until([&] { return audienceWindow->isActive(); });
    QTest::keyClick(audienceWindow, Qt::Key_Escape);
    until([&] { return !audienceWindow->isVisible(); });
    EXPECT_FALSE(controller->presenting());
    EXPECT_FALSE(console->active());
    EXPECT_EQ(presenterView()->mirror(), nullptr);
    EXPECT_FALSE(audienceWindow->isVisible());
    EXPECT_FALSE(panel->isVisible());
}

// "Notes for the audience too" (the console's switch, a setting): the audience sees the whole page with its space for
// notes, at once while presenting; off again, only the slide. A page without space for notes looks the same either way.
TEST_F(PresenterView, theAudienceSeesTheSpaceForNotesWhenAsked) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    const double slideWidth = session()->getDocument()->getPage(0)->getWidth();
    const double slideHeight = session()->getDocument()->getPage(0)->getHeight();
    ASSERT_EQ(xqt::notespace::apply(*session(), {0}, {0, 0, 0.5, 0.25, true}), 1u);
    wait(100);
    const double pageWidth = session()->getDocument()->getPage(0)->getWidth();
    const double pageHeight = session()->getDocument()->getPage(0)->getHeight();
    ASSERT_GT(pageWidth, slideWidth * 1.4);
    ASSERT_GT(pageHeight, slideHeight * 1.2);
    EXPECT_FALSE(console->showNotes()) << "off by default";

    present();
    ASSERT_TRUE(console->active());
    auto* canvas = findIn(audienceWindow, "audienceCanvas");
    ASSERT_NE(canvas, nullptr);
    // What the audience's canvas shows of the page (page points): its size over the zoom, from the page's corner
    auto fitted = [&] {
        const double z = audience()->getViewController().zoom();
        const QRectF page = audience()->pageViewRect(0);
        return QRectF(-page.left() / z, -page.top() / z, canvas->width() / z, canvas->height() / z);
    };
    auto near = [](QRectF a, QRectF b) {
        return std::abs(a.left() - b.left()) < 1 && std::abs(a.top() - b.top()) < 1 &&
               std::abs(a.right() - b.right()) < 1 && std::abs(a.bottom() - b.bottom()) < 1;
    };
    const QRectF slide(0, 0, slideWidth, slideHeight);
    const QRectF whole(0, 0, pageWidth, pageHeight);
    EXPECT_EQ(console->shownRect(), slide);
    EXPECT_TRUE(near(fitted(), slide)) << "only the slide";
    EXPECT_TRUE(find("presenterNotesHint")->isVisible());

    // On, from the console: at once the whole page
    click(find("presenterShowNotes"));
    EXPECT_TRUE(console->showNotes());
    EXPECT_TRUE(settings()->get("presenterShowNotes").toBool()) << "remembered";
    until([&] { return near(fitted(), whole); }, 2000);
    EXPECT_EQ(console->shownRect(), whole);
    EXPECT_TRUE(near(fitted(), whole)) << "the page with its space for notes";
    EXPECT_NEAR(canvas->width() / canvas->height(), pageWidth / pageHeight, 0.01) << "the page's shape";
    EXPECT_TRUE(audienceWindow->isVisible());

    // A page without space for notes: the same either way
    key(Qt::Key_Space);
    settle();
    until([&] { return audience()->currentPageNo() == 1; });
    const QSizeF second(session()->getDocument()->getPage(1)->getWidth(), session()->getDocument()->getPage(1)->getHeight());
    EXPECT_EQ(console->shownRect(), QRectF(QPointF(), second));
    key(Qt::Key_Backspace);
    settle();
    until([&] { return audience()->currentPageNo() == 0; });

    // Off again (the setting): only the slide
    settings()->set("presenterShowNotes", false);
    until([&] { return near(fitted(), slide); }, 2000);
    EXPECT_EQ(console->shownRect(), slide);
    EXPECT_TRUE(near(fitted(), slide));
    EXPECT_FALSE(find("presenterShowNotes")->property("checked").toBool());
}

// "The audience follows my zoom" (on by default): zoomed in on the console, the audience sees the same part of the
// slide, widened to its screen's shape, never less than the presenter sees, a frame on the console around it; Fit, a
// page change and the switch off bring the whole slide back; with the notes shown, the part may reach into them
TEST_F(PresenterView, theAudienceFollowsThePresentersZoom) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    const double slideWidth = session()->getDocument()->getPage(0)->getWidth();
    const double slideHeight = session()->getDocument()->getPage(0)->getHeight();
    ASSERT_EQ(xqt::notespace::apply(*session(), {0}, {0, 0, 0.5, 0, true}), 1u);
    wait(100);
    const double pageWidth = session()->getDocument()->getPage(0)->getWidth();
    const QRectF slide(0, 0, slideWidth, slideHeight);
    EXPECT_TRUE(console->followView()) << "on by default";

    present();
    ASSERT_TRUE(console->active());
    xqt::CanvasView* view = presenterView();
    xqt::ViewController& vc = view->getViewController();
    auto* canvas = findIn(audienceWindow, "audienceCanvas");
    auto* frame = find("audienceFrame");
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(frame, nullptr);
    // What the audience's canvas shows of the page (points) and what the presenter sees of it
    auto fitted = [&] {
        const size_t page = audience()->currentPageNo();
        const double z = audience()->getViewController().zoom();
        const QRectF p = audience()->pageViewRect(page);
        return QRectF(-p.left() / z, -p.top() / z, canvas->width() / z, canvas->height() / z);
    };
    auto seen = [&] {
        const auto r = view->viewOnPage(view->currentPageNo());
        return QRectF(r.x, r.y, r.width, r.height);
    };
    auto near = [](QRectF a, QRectF b, double d = 1) {
        return std::abs(a.left() - b.left()) < d && std::abs(a.top() - b.top()) < d &&
               std::abs(a.right() - b.right()) < d && std::abs(a.bottom() - b.bottom()) < d;
    };
    auto covers = [](QRectF outer, QRectF inner) { return outer.adjusted(-0.5, -0.5, 0.5, 0.5).contains(inner); };
    const double screenAspect = audienceWindow->width() / double(audienceWindow->height());
    EXPECT_EQ(console->shownRect(), slide);
    EXPECT_FALSE(frame->isVisible()) << "no frame while the whole page shows";

    // Zoomed in on the slide (the zoom pill's +): the audience sees that part, at its screen's shape
    const QPointF middle = view->pageViewRect(0).topLeft() + QPointF(slideWidth / 2, slideHeight / 3) * vc.zoom();
    vc.setZoom(vc.zoom() * 3, middle);
    controller->zoomIn();
    wait(100);
    const QRectF part = console->shownRect();
    EXPECT_NE(part, slide);
    EXPECT_TRUE(covers(slide, part)) << "within the slide";
    EXPECT_TRUE(covers(part, seen().intersected(slide))) << "never less than the presenter sees";
    EXPECT_NEAR(part.width() / part.height(), screenAspect, 0.01) << "the audience's screen's shape";
    until([&] { return near(fitted(), part); }, 2000);
    EXPECT_TRUE(near(fitted(), part)) << "the audience's view shows exactly that part";
    EXPECT_NEAR(canvas->width(), audienceWindow->width(), 1) << "the whole screen";
    // The frame on the console: around what the audience sees
    EXPECT_TRUE(frame->isVisible());
    const double z = vc.zoom();
    const QRectF p = view->pageViewRect(0);
    EXPECT_NEAR(frame->x(), p.left() + part.left() * z, 1);
    EXPECT_NEAR(frame->width(), part.width() * z, 1);
    EXPECT_NEAR(frame->y(), p.top() + part.top() * z, 1);

    // Scrolled: the part goes along
    vc.panBy(QPointF(-60, -40));
    wait(50);
    EXPECT_NE(console->shownRect(), part);
    EXPECT_TRUE(covers(console->shownRect(), seen().intersected(slide)));
    until([&] { return near(fitted(), console->shownRect()); }, 2000);
    EXPECT_TRUE(near(fitted(), console->shownRect()));

    // Fit in the console: both back to the whole slide
    click(find("presenterFit"));
    EXPECT_EQ(vc.keptFit(), xqt::ViewController::Fit::Page);
    EXPECT_EQ(console->shownRect(), slide);
    until([&] { return near(fitted(), slide); }, 2000);
    EXPECT_TRUE(near(fitted(), slide));
    EXPECT_FALSE(frame->isVisible());

    // Zoomed in again, then the next page: the audience sees it whole, the console too
    vc.setZoom(vc.zoom() * 3, middle);
    wait(50);
    ASSERT_NE(console->shownRect(), slide);
    key(Qt::Key_Space);
    settle();
    until([&] { return audience()->currentPageNo() == 1; });
    const QRectF second(0, 0, session()->getDocument()->getPage(1)->getWidth(),
                        session()->getDocument()->getPage(1)->getHeight());
    EXPECT_EQ(console->shownRect(), second);
    EXPECT_NEAR(vc.zoom(), vc.presentedZoom(1), 1e-6) << "the console shows the whole page too";
    EXPECT_FALSE(frame->isVisible());
    key(Qt::Key_Backspace);
    settle();
    until([&] { return audience()->currentPageNo() == 0; });

    // With the notes shown: zoomed in on the slide's right edge, the part reaches into the space for notes
    settings()->set("presenterShowNotes", true);
    wait(50);
    const QRectF whole(0, 0, pageWidth, slideHeight);
    EXPECT_EQ(console->shownRect(), whole);
    const QPointF edge = view->pageViewRect(0).topLeft() + QPointF(slideWidth, slideHeight / 2) * vc.zoom();
    vc.setZoom(vc.zoom() * 4, edge);
    wait(50);
    EXPECT_GT(console->shownRect().right(), slideWidth + 10) << "beside the slide too";
    EXPECT_LT(console->shownRect().left(), slideWidth - 10);
    EXPECT_TRUE(covers(whole, console->shownRect()));
    EXPECT_TRUE(covers(console->shownRect(), seen().intersected(whole)));
    // Without the notes again: only the slide's part, still never less than the presenter sees of it
    settings()->set("presenterShowNotes", false);
    wait(50);
    EXPECT_LE(console->shownRect().right(), slideWidth + 0.5);
    EXPECT_TRUE(covers(console->shownRect(), seen().intersected(slide)));

    // Off: the audience sees the whole slide, whatever the zoom here; on again: it follows at once
    click(find("presenterFollowView"));
    EXPECT_FALSE(console->followView());
    EXPECT_FALSE(settings()->get("presenterFollowView").toBool()) << "remembered";
    EXPECT_EQ(console->shownRect(), slide);
    until([&] { return near(fitted(), slide); }, 2000);
    EXPECT_TRUE(near(fitted(), slide));
    EXPECT_FALSE(frame->isVisible());
    vc.panBy(QPointF(30, 0));
    wait(50);
    EXPECT_EQ(console->shownRect(), slide);
    settings()->set("presenterFollowView", true);
    wait(50);
    EXPECT_NE(console->shownRect(), slide);
    EXPECT_TRUE(frame->isVisible());
}

// The time since the start: it runs from the start of the presentation, pauses, goes on, goes back to 0
TEST_F(PresenterView, theTimeSinceTheStart) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    present();
    ASSERT_TRUE(console->active());
    EXPECT_TRUE(console->timerRunning()) << "it starts with the presentation";
    wait(120);
    const qint64 a = console->elapsedMs();
    EXPECT_GE(a, 100);
    EXPECT_EQ(find("presenterElapsed")->property("text").toString(), QString("0:00"));

    click(find("presenterTimerToggle"));
    EXPECT_FALSE(console->timerRunning());
    const qint64 paused = console->elapsedMs();
    wait(120);
    EXPECT_EQ(console->elapsedMs(), paused) << "paused";
    click(find("presenterTimerToggle"));
    EXPECT_TRUE(console->timerRunning());
    wait(120);
    EXPECT_GE(console->elapsedMs(), paused + 100) << "goes on";

    click(find("presenterTimerReset"));
    EXPECT_LT(console->elapsedMs(), 100) << "back to 0";
    EXPECT_TRUE(console->timerRunning()) << "and runs on";

    // The clock shows the time of day
    EXPECT_FALSE(find("presenterClock")->property("text").toString().isEmpty());
}

// Written on the console's page: the stroke shows on the audience's screen while it is written, then it is in the
// document; the laser pointer and the curtain show there too
TEST_F(PresenterView, inkLaserAndCurtainShowOnTheAudiencesScreen) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    present();
    ASSERT_TRUE(console->active());
    auto* canvas = find("canvas");
    xqt::CanvasView* view = presenterView();
    const QPoint a = canvas->mapToScene(view->pageViewRect(0).center()).toPoint();
    auto drawFrom = [&](QPoint at) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        for (int i = 1; i <= 8; ++i) {
            QTest::mouseMove(window, at + QPoint(-20 * i, 10 * i));
        }
        wait(30);
    };

    controller->selectTool("pen");
    const size_t before = strokes(0);
    drawFrom(a);
    EXPECT_EQ(audience()->getPage(0)->mirroredViewCount(), 1u) << "the audience sees it being written";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(-160, 80));
    wait(100);
    EXPECT_EQ(strokes(0), before + 1);
    EXPECT_EQ(audience()->getPage(0)->mirroredViewCount(), 0u) << "now it is in the document";

    controller->selectTool("laserPointerPen");
    drawFrom(a + QPoint(0, 40));
    EXPECT_EQ(audience()->getPage(0)->mirroredViewCount(), 1u) << "the laser pointer";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(-160, 120));
    wait(50);
    EXPECT_EQ(strokes(0), before + 1) << "its ink is never kept";

    controller->toggleCurtain("curtain");
    wait(50);
    ASSERT_TRUE(view->curtain().visible());
    EXPECT_TRUE(audience()->curtain().visible()) << "the curtain on the audience's screen";
    EXPECT_FALSE(audience()->curtain().handlesShown());
    EXPECT_EQ(audience()->curtain().centre(), view->curtain().centre());
    controller->toggleCurtain("");
    wait(50);
    EXPECT_FALSE(audience()->curtain().active());
    controller->selectTool("pen");
}

// "Swap screens" in the console: the audience's window goes to the laptop's screen, the console to the projector's;
// the controls over the page (the toolbox, floating while presenting) stay beside the panel
TEST_F(PresenterView, swapScreensAndTheToolboxBesideThePanel) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    QScreen* laptop = QGuiApplication::primaryScreen();
    QScreen* projector = QGuiApplication::screens().at(1);
    present();
    ASSERT_TRUE(console->active());
    auto* toolbox = find("toolbox");
    ASSERT_NE(toolbox, nullptr);
    until([&] { return toolbox->isVisible(); });
    EXPECT_TRUE(toolbox->isVisible()) << "presenting keeps the floating toolbox";
    EXPECT_LE(sceneRect(toolbox).right(), sceneRect(find("presenterPanel")).left()) << "beside the panel";

    click(find("presenterSwapScreens"));
    EXPECT_TRUE(console->swapScreens());
    until([&] { return audienceWindow->screen() == laptop && window->screen() == projector; });
    EXPECT_EQ(audienceWindow->screen(), laptop);
    EXPECT_EQ(window->screen(), projector);
    until([&] { return audienceWindow->geometry() == laptop->geometry(); });
    EXPECT_EQ(audienceWindow->geometry(), laptop->geometry());
    EXPECT_TRUE(console->active()) << "still presenting";

    click(find("presenterSwapScreens"));
    until([&] { return audienceWindow->screen() == projector; });
    EXPECT_EQ(audienceWindow->screen(), projector);
}

// Another tab while presenting: the audience's screen shows that document
TEST_F(PresenterView, anotherTabPresentsOnTheAudiencesScreenToo) {
    if (!twoScreens) {
        GTEST_SKIP() << "needs two screens (PresenterView.ui@2screens)";
    }
    controller->newDocument();
    wait(100);
    controller->setCurrentTab(0);
    wait(100);
    present();
    ASSERT_TRUE(console->active());
    xqt::DocumentSession* first = session();
    EXPECT_EQ(&audience()->getSession(), first);
    controller->setCurrentTab(1);
    wait(100);
    ASSERT_TRUE(controller->presenting());
    ASSERT_TRUE(console->active());
    EXPECT_NE(session(), first);
    EXPECT_EQ(&audience()->getSession(), session());
    EXPECT_EQ(presenterView()->mirror(), audience());
    // The presented tab is closed: the next one presents
    controller->closeTab(1);
    wait(100);
    if (controller->presenting()) {
        ASSERT_NE(audience(), nullptr);
        EXPECT_EQ(&audience()->getSession(), session());
    }
}
