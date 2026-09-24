/*
 * xournal-qt: reference mode in the real window (Main.qml, off-screen): the split of the canvas area, its divider and
 * pill, the menus that open a reference, full screen and the docked tool bar, the keys.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "canvas/CanvasPage.h"
#include "canvas/CanvasView.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/ReferenceMode.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "../SearchHits.h"
#include "config-test.h"

namespace {
QString fixturePath(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

class ReferenceWindowTest: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        controller->newDocument();
        controller->newDocument();
        controller->setCurrentTab(0);
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        window->requestActivate();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        wait(100);
        main = findItem("canvas");
        reference = findItem("referenceCanvas");
        split = findItem("referenceSplit");
        ASSERT_NE(main, nullptr);
        ASSERT_NE(reference, nullptr);
        ASSERT_NE(split, nullptr);
    }
    void TearDown() override {
        ref().setRatio(0.5);  // (the settings are shared by the tests of this run)
        ref().setOnLeft(true);
        controller->shutdown();
        engine.reset();
        controller.reset();
    }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    void until(const std::function<bool()>& done, int ms = 1500) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    /// Also in the popups (the overlay) and items made by a Repeater; `shown`: only a visible one
    QQuickItem* findItem(const char* name, bool shown = false) const {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == name && (!shown || i->isVisible())) {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        QQuickItem* root = window->contentItem()->parentItem() ? window->contentItem()->parentItem()
                                                               : window->contentItem();
        return walk(root);
    }
    void click(QQuickItem* item, Qt::MouseButton button = Qt::LeftButton) {
        ASSERT_NE(item, nullptr);
        ASSERT_TRUE(item->isVisible());
        QTest::mouseClick(window, button, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QTest::keyClick(window, k, m);
        wait(20);
    }
    xqt::ReferenceMode& ref() const { return controller->reference(); }
    xqt::TabManager& tabs() const { return controller->tabManager(); }
    static QRectF sceneRect(QQuickItem* i) { return QRectF(i->mapToScene(QPointF(0, 0)), i->size()); }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    QQuickItem* main = nullptr;
    QQuickItem* reference = nullptr;
    QQuickItem* split = nullptr;
};
}  // namespace

TEST_F(ReferenceWindowTest, theReferenceIsShownBesideTheMainDocument) {
    const double fullWidth = main->width();
    EXPECT_FALSE(reference->isVisible());
    EXPECT_FALSE(findItem("mainFrame")->isVisible());
    ref().showTab(1);
    wait(50);
    ASSERT_TRUE(reference->isVisible());
    EXPECT_EQ(reference->property("view").value<QObject*>(), tabs().view(1));
    EXPECT_EQ(main->property("view").value<QObject*>(), tabs().view(0));
    EXPECT_TRUE(reference->property("readingOnly").toBool());
    EXPECT_FALSE(main->property("readingOnly").toBool());
    EXPECT_TRUE(findItem("mainFrame")->isVisible()) << "the main document is framed";
    EXPECT_TRUE(findItem("referencePill")->isVisible());
    // Both halves share the canvas area, the reference on the left (default), without overlapping
    const QRectF m = sceneRect(main), r = sceneRect(reference);
    EXPECT_NEAR(m.width() + r.width() + 8, fullWidth, 1.5);
    EXPECT_NEAR(m.width(), r.width(), 1.5);
    EXPECT_LE(r.right(), m.left());
    EXPECT_EQ(sceneRect(split).left(), r.left());
    // Its width fits the half it is shown in
    auto* refView = tabs().view(1);
    EXPECT_NEAR(refView->getViewController().viewSize().width(), r.width(), 0.5);
    EXPECT_NEAR(refView->pageViewRect(0).width(), r.width() - 20, 30) << "not fitted to its width";

    // The tab strip marks it
    auto* badge = findItem("referenceBadge");
    ASSERT_NE(badge, nullptr);
    until([&] {
        auto* b = findItem("referenceBadge");
        return b && b->isVisible();
    });
    int shownBadges = 0;
    std::function<void(QQuickItem*)> count = [&](QQuickItem* i) {
        if (i->objectName() == "referenceBadge" && i->isVisible()) {
            ++shownBadges;
        }
        for (QQuickItem* c: i->childItems()) {
            count(c);
        }
    };
    count(window->contentItem());
    EXPECT_EQ(shownBadges, 1);

    // Another tab: its own reference (none)
    controller->setCurrentTab(1);
    wait(50);
    EXPECT_FALSE(reference->isVisible());
    EXPECT_NEAR(main->width(), fullWidth, 0.5);
    controller->setCurrentTab(0);
    wait(50);
    EXPECT_TRUE(reference->isVisible());
    // The home screen hides it (and gives its view up)
    controller->setHomeVisible(true);
    wait(50);
    EXPECT_FALSE(reference->isVisible());
    EXPECT_EQ(reference->property("view").value<QObject*>(), nullptr);
    controller->setHomeVisible(false);
    wait(50);
    EXPECT_TRUE(reference->isVisible());
}

TEST_F(ReferenceWindowTest, theDividerIsDraggedAndRemembered) {
    ref().showTab(1);
    wait(50);
    auto* grip = findItem("referenceDividerGrip");
    ASSERT_NE(grip, nullptr);
    ASSERT_TRUE(grip->isVisible());
    const double refWidth = reference->width();
    const QPoint from = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, from + QPoint(12 * i, 0));
        wait(5);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + QPoint(120, 0));
    wait(50);
    EXPECT_NEAR(reference->width(), refWidth + 120, 20) << "the reference (left) got wider";
    EXPECT_LT(ref().ratio(), 0.45) << "the main document's share is remembered";
    EXPECT_EQ(controller->tabManager().session(0)->getDocument()->getPage(0)->getSelectedLayer()->getElements().size(),
              0u)
            << "dragging the divider drew";

    // Never beyond the limits
    const QPoint grip2 = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grip2);
    for (int i = 1; i <= 20; ++i) {
        QTest::mouseMove(window, grip2 + QPoint(60 * i, 0));
        wait(5);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, grip2 + QPoint(1200, 0));
    wait(50);
    EXPECT_NEAR(ref().ratio(), xqt::ReferenceMode::MIN_RATIO, 1e-6);
    EXPECT_GT(main->width(), 100);
}

TEST_F(ReferenceWindowTest, thePillSwapsSidesAndRolesAndCloses) {
    ref().showTab(1);
    wait(50);
    click(findItem("referenceSwapSidesButton"));
    EXPECT_FALSE(ref().onLeft());
    EXPECT_GE(sceneRect(reference).left(), sceneRect(main).right()) << "the reference is on the right now";
    click(findItem("referenceSwapSidesButton"));
    EXPECT_TRUE(ref().onLeft());

    auto* notes = tabs().view(0);
    auto* book = tabs().view(1);
    click(findItem("referenceSwapRolesButton"));
    EXPECT_EQ(controller->currentTab(), 1);
    EXPECT_EQ(main->property("view").value<QObject*>(), book);
    EXPECT_EQ(reference->property("view").value<QObject*>(), notes);
    EXPECT_TRUE(notes->isReadingOnly());
    EXPECT_FALSE(book->isReadingOnly());
    EXPECT_TRUE(notes->isShown()) << "both are shown (in view for the render service)";
    EXPECT_TRUE(book->isShown());

    click(findItem("referenceCloseButton"));
    EXPECT_FALSE(reference->isVisible());
    EXPECT_FALSE(ref().active());
    EXPECT_EQ(controller->tabCount(), 2) << "its tab stays open";
    EXPECT_FALSE(notes->isShown());
    EXPECT_FALSE(notes->isReadingOnly());
}

TEST_F(ReferenceWindowTest, closingTheReferenceTabClosesTheSplit) {
    ref().showTab(1);
    wait(50);
    controller->closeTab(1);
    wait(50);
    EXPECT_FALSE(reference->isVisible());
    EXPECT_FALSE(findItem("mainFrame")->isVisible());
}

TEST_F(ReferenceWindowTest, thePageButtonGoesToAPage) {
    for (int i = 0; i < 5; ++i) {
        tabs().session(1)->insertNewPage(1);
    }
    ref().showTab(1);
    wait(100);
    EXPECT_EQ(ref().pageCount(), 6);
    click(findItem("referencePageButton"));
    auto* popup = findItem("referencePageField");
    ASSERT_NE(popup, nullptr);
    until([&] { return popup->hasActiveFocus(); });
    QTest::keyClick(window, Qt::Key_4);
    key(Qt::Key_Return);
    wait(300);
    EXPECT_EQ(ref().pageNumber(), 4);
    EXPECT_EQ(controller->pageNumber(), 1) << "the main document stays where it is";
}

TEST_F(ReferenceWindowTest, theKeysActOnTheReferenceAfterATapOnIt) {
    ref().showTab(1);
    wait(100);
    auto& mainVc = tabs().view(0)->getViewController();
    auto& refVc = tabs().view(1)->getViewController();
    const double mainZoom = mainVc.zoom(), refZoom = refVc.zoom();
    click(findItem("referenceFitWidthButton"));  // (a tap on the pill gives it the keys)
    EXPECT_TRUE(ref().focused());
    key(Qt::Key_Plus, Qt::ControlModifier);
    EXPECT_GT(refVc.zoom(), refZoom * 1.1) << "Ctrl++ did not zoom the reference";
    EXPECT_DOUBLE_EQ(mainVc.zoom(), mainZoom);

    // A tap on the main document: the keys are its own again
    controller->selectTool("hand");
    click(main);
    EXPECT_FALSE(ref().focused());
    key(Qt::Key_Plus, Qt::ControlModifier);
    EXPECT_GT(mainVc.zoom(), mainZoom * 1.1);

    // A press on the reference gives it the keys as well
    click(reference);
    EXPECT_TRUE(ref().focused());
}

TEST_F(ReferenceWindowTest, thePenWritesOnlyOnTheMainDocument) {
    ref().showTab(1);
    wait(100);
    controller->selectTool("pen");
    auto elements = [&](int tab) {
        return tabs().session(tab)->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    const QPoint a = reference->mapToScene(QPointF(reference->width() / 2, 200)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseMove(window, a + QPoint(0, 60));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(0, 60));
    wait(50);
    EXPECT_EQ(elements(1), 0u);
    EXPECT_FALSE(tabs().session(1)->isModified());
    // Across the divider, from the main document
    const QPoint b = main->mapToScene(QPointF(80, 200)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, b);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, b - QPoint(30 * i, 0));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b - QPoint(300, 0));
    wait(50);
    EXPECT_EQ(elements(0), 1u);
    EXPECT_EQ(elements(1), 0u);
}

TEST_F(ReferenceWindowTest, pdfTextOfTheReferenceIsCopiedForTheNotes) {
    ASSERT_TRUE(controller->openAsReference(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(200);
    ASSERT_TRUE(ref().active());
    ASSERT_EQ(controller->currentTab(), 0);
    auto* view = ref().canvas();
    auto* session = tabs().session(ref().tab());
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
    session->search().clear();
    view->getViewController().scrollToPage(0);
    wait(100);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {  // (a picture of the split, to look at)
        wait(1500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    const QPointF onWord = view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();

    // The highlighter of PDF text selects there, it never marks
    controller->setPdfTextMode("highlight");
    controller->selectTool("selectPdfTextLinear");
    const QPoint from = reference->mapToScene(onWord - QPointF(hit.width() * 0.6, 0)).toPoint();
    const QPoint to = reference->mapToScene(onWord + QPointF(hit.width() * 0.6, 0)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 5; ++i) {
        QTest::mouseMove(window, from + (to - from) * i / 5);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    wait(100);
    EXPECT_TRUE(view->hasPdfTextSelection());
    EXPECT_FALSE(session->isModified()) << "the PDF text was marked in the reference";
    EXPECT_TRUE(ref().hasSelection());
    auto* copy = findItem("referenceCopyButton");
    until([&] { return copy->isVisible(); });
    click(copy);
    EXPECT_TRUE(QGuiApplication::clipboard()->text().contains("Test")) << QGuiApplication::clipboard()->text().toStdString();
    EXPECT_FALSE(ref().hasSelection());

    // A right click (a long press) on a word selects it, to copy it
    controller->selectTool("pen");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, reference->mapToScene(onWord).toPoint());
    wait(100);
    EXPECT_TRUE(view->hasPdfTextSelection());
    EXPECT_FALSE(controller->pdfTextIsSelected()) << "the main document selected something";
    // Ctrl+C with the reference in hand
    QGuiApplication::clipboard()->clear();
    key(Qt::Key_C, Qt::ControlModifier);
    EXPECT_TRUE(QGuiApplication::clipboard()->text().contains("Test"));
    EXPECT_FALSE(session->isModified());
}

TEST_F(ReferenceWindowTest, theMenusOpenAReference) {
    // The tab strip's menu of the other tab (the menu of the current tab has no such entry)
    auto* list = findItem("tabList");
    ASSERT_NE(list, nullptr);
    auto menuEntry = [&](int index) -> QQuickItem* {
        QQuickItem* delegate = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(int, index));
        if (!delegate) {
            return nullptr;
        }
        QObject* menu = delegate->findChild<QObject*>("tabMenu");
        QMetaObject::invokeMethod(menu, "open");
        QQuickItem* entry = nullptr;
        until([&] { return menu->property("opened").toBool(); });
        entry = findItem("openAsReferenceTabItem", true);
        return entry;
    };
    QQuickItem* entry = menuEntry(1);
    ASSERT_NE(entry, nullptr) << "no entry for the other tab";
    click(entry);
    EXPECT_EQ(ref().tab(), 1);
    wait(300);  // (the menu closes)
    entry = menuEntry(1);
    ASSERT_NE(entry, nullptr);
    click(entry);  // "Close the reference" now
    EXPECT_FALSE(ref().active());
    wait(300);
    EXPECT_EQ(menuEntry(0), nullptr) << "the current tab is not its own reference";
    key(Qt::Key_Escape);
    wait(300);

    // The tab overview: the button on the card of another document
    auto* overview = window->findChild<QObject*>("tabOverview");
    ASSERT_NE(overview, nullptr);
    QMetaObject::invokeMethod(overview, "open");
    until([&] { return overview->property("opened").toBool(); });
    QQuickItem* button = nullptr;
    until([&] { return (button = findItem("overviewReferenceButton", true)) != nullptr; });
    ASSERT_NE(button, nullptr);
    click(button);
    EXPECT_EQ(ref().tab(), 1);
    until([&] { return !overview->property("visible").toBool(); });
    EXPECT_FALSE(overview->property("visible").toBool());
    EXPECT_EQ(controller->currentTab(), 0);
}

TEST_F(ReferenceWindowTest, itWorksInFullScreenAndWithTheToolBarAtASide) {
    ref().showTab(1);
    wait(50);
    for (const char* position: {"left", "right", "top"}) {
        controller->setToolbarPosition(position);
        wait(50);
        auto* tools = findItem("sideTools");
        const QRectF m = sceneRect(main), r = sceneRect(reference);
        EXPECT_FALSE(m.intersects(r)) << position;
        if (tools->isVisible()) {
            const QRectF t = sceneRect(tools);
            EXPECT_FALSE(t.intersects(m)) << position;
            EXPECT_FALSE(t.intersects(r)) << position;
        }
    }
    window->setProperty("fullScreenMode", true);
    wait(200);
    EXPECT_TRUE(reference->isVisible());
    const QRectF m = sceneRect(main), r = sceneRect(reference);
    EXPECT_NEAR(m.width() + r.width() + 8, split->width(), 1.5);
    auto* square = findItem("quickToolSquare");
    ASSERT_TRUE(square->isVisible());
    EXPECT_TRUE(m.contains(sceneRect(square))) << "the tool square is over the main document";
    window->setProperty("fullScreenMode", false);
    wait(200);
}

TEST_F(ReferenceWindowTest, theEditSwitchLetsThePenWriteInTheReference) {
    ref().showTab(1);
    wait(100);
    controller->selectTool("pen");
    auto elements = [&](int tab) {
        return tabs().session(tab)->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    auto* edit = findItem("referenceEditButton");
    ASSERT_NE(edit, nullptr);
    EXPECT_FALSE(edit->property("checked").toBool());
    click(edit);
    EXPECT_TRUE(ref().editing());
    EXPECT_TRUE(edit->property("checked").toBool()) << "the switch shows that the reference is written in";
    EXPECT_FALSE(reference->property("readingOnly").toBool());
    EXPECT_FALSE(tabs().view(1)->isReadingOnly());

    const QPoint a = reference->mapToScene(QPointF(reference->width() / 2, 200)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, a + QPoint(0, 6 * i));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(0, 60));
    wait(50);
    EXPECT_EQ(elements(1), 1u) << "the pen did not write in the reference";
    EXPECT_TRUE(tabs().data(tabs().index(1), xqt::TabManager::ModifiedRole).toBool()) << "its tab has its dot";
    // A stroke from the reference across the divider stays in the reference
    const QPoint b = reference->mapToScene(QPointF(reference->width() - 60, 300)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, b);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, b + QPoint(20 * i, 0));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b + QPoint(200, 0));
    wait(50);
    EXPECT_EQ(elements(1), 2u);
    EXPECT_EQ(elements(0), 0u) << "the stroke went over to the notes";
    // Ctrl+Z undoes in the canvas that was written on
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(elements(1), 1u);
    EXPECT_EQ(elements(0), 0u);

    // Switched off: for reading again
    click(edit);
    EXPECT_FALSE(ref().editing());
    EXPECT_TRUE(tabs().view(1)->isReadingOnly());
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseMove(window, a + QPoint(0, 60));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(0, 60));
    wait(50);
    EXPECT_EQ(elements(1), 1u);
}

TEST_F(ReferenceWindowTest, theGridOfTheReferenceShowsItsPagesInItsHalf) {
    for (int i = 0; i < 7; ++i) {
        tabs().session(1)->insertNewPage(1);
    }
    ref().showTab(1);
    wait(100);
    auto* gridButton = findItem("referenceGridButton");
    ASSERT_NE(gridButton, nullptr);
    click(gridButton);
    auto* grid = findItem("referenceGrid");
    ASSERT_NE(grid, nullptr);
    EXPECT_TRUE(grid->isVisible());
    EXPECT_EQ(sceneRect(grid), sceneRect(reference)) << "the grid takes the reference's half, not the notes'";
    auto* gridView = findItem("referenceGridView");
    until([&] { return gridView->property("count").toInt() == 8; });
    EXPECT_EQ(gridView->property("count").toInt(), 8);
    EXPECT_FALSE(findItem("pageGrid")->isVisible()) << "the notes' page grid stays closed";
    EXPECT_EQ(qobject_cast<QAbstractItemModel*>(controller->pagesModel())->rowCount(), 1)
            << "the page sidebar shows the notes";

    // A tap on a page: the reference goes there, the grid closes
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(gridView, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 2));
    ASSERT_NE(cell, nullptr);
    click(cell);
    wait(200);
    EXPECT_EQ(ref().pageNumber(), 3);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_EQ(controller->pageNumber(), 1);

    // The button closes it as well
    click(gridButton);
    EXPECT_TRUE(grid->isVisible());
    click(gridButton);
    EXPECT_FALSE(grid->isVisible());
}

// Both documents in sight are drawn as pages in view; swapping their roles moves them to the other canvas and draws
// nothing again (the zoom and the rendered pages stay with the view).
TEST_F(ReferenceWindowTest, swappingRolesDrawsNothingAgain) {
    for (int tab: {0, 1}) {
        for (int i = 0; i < 3; ++i) {
            tabs().session(tab)->insertNewPage(1);
        }
    }
    ref().showTab(1);
    auto* renders = controller->context().getRenderService();
    for (int i = 0; i < 3; ++i) {  // (renders after the zoom settled)
        wait(150);
        renders->waitForIdle();
    }
    auto visibleRendered = [&](xqt::CanvasView* v) {
        const auto [first, last] = v->visiblePages();
        bool all = first <= last;
        for (size_t i = first; i <= last; ++i) {
            const auto info = v->getPage(i)->bufferInfo();
            all = all && info.valid && info.zoom == v->getViewController().zoom();
        }
        return all;
    };
    auto* notes = tabs().view(0);
    auto* book = tabs().view(1);
    ASSERT_TRUE(visibleRendered(notes));
    ASSERT_TRUE(visibleRendered(book)) << "the reference's pages in view were not drawn";
    const double notesZoom = notes->getViewController().zoom(), bookZoom = book->getViewController().zoom();

    click(findItem("referenceSwapRolesButton"));
    EXPECT_FALSE(renders->hasWork(xqt::RenderService::Priority::Visible)) << "a page was drawn again";
    EXPECT_DOUBLE_EQ(notes->getViewController().zoom(), notesZoom);
    EXPECT_DOUBLE_EQ(book->getViewController().zoom(), bookZoom);
    EXPECT_TRUE(visibleRendered(notes));
    EXPECT_TRUE(visibleRendered(book));
}
