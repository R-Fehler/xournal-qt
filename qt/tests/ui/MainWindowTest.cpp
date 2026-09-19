/*
 * xournal-qt: the real main window (Main.qml) off-screen: keyboard shortcuts, settings sheet, tab overview.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include "session/DocumentSession.h"
#include "shell/PagesModel.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "config-test.h"

namespace {
class MainWindowTest: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        window->requestActivate();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        wait(100);
    }
    void TearDown() override {
        engine.reset();
        controller->shutdown();
        controller.reset();
    }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    template <typename T = QObject>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
    }
    /// Waits until the popup is fully open (or closed).
    static bool waitOpened(QObject* popup, bool opened) {
        auto done = [&] {
            return popup->property("opened").toBool() == opened && popup->property("visible").toBool() == opened;
        };
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return done();
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QTest::keyClick(window, k, m);
        wait(20);
    }
    /// Types text into the focused item (QTest::keyClicks is for widgets only).
    void type(const char* text) {
        for (const char* c = text; *c; ++c) {
            QTest::keyClick(window, *c);
        }
        wait(20);
    }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(MainWindowTest, ctrlTabSwitchesTabs) {
    controller->newDocument();
    controller->newDocument();
    ASSERT_EQ(controller->tabCount(), 3);
    ASSERT_EQ(controller->currentTab(), 2);
    key(Qt::Key_Tab, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 0) << "Ctrl+Tab wraps around to the first tab";
    key(Qt::Key_Tab, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 1);
    key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(controller->currentTab(), 0) << "Ctrl+Shift+Tab goes back";
    key(Qt::Key_PageDown, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 1);
}

TEST_F(MainWindowTest, tabOverviewSwitchesAndCloses) {
    controller->newDocument();
    controller->newDocument();
    ASSERT_EQ(controller->tabCount(), 3);
    QObject* overview = find("tabOverview");
    ASSERT_NE(overview, nullptr);

    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* grid = find<QQuickItem>("tabGrid");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 3);

    // Tap the first card.
    QQuickItem* first = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0));
    ASSERT_NE(first, nullptr);
    const QPointF center = first->mapToScene(QPointF(first->width() / 2, first->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    EXPECT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 0);

    // Keyboard: open, go right, Enter.
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_Right);
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);

    // Delete closes the highlighted (unmodified) document.
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_Delete);
    EXPECT_EQ(controller->tabCount(), 2);
}

TEST_F(MainWindowTest, settingsSheetAppliesAndSavesOnClose) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    QObject* sheet = find("settingsPage");
    ASSERT_NE(sheet, nullptr);
    const QString file = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         "/xournal-qt/settings.xml";

    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    ASSERT_TRUE(settings->set("pressureMultiplier", 2.5));
    EXPECT_DOUBLE_EQ(settings->get("pressureMultiplier").toDouble(), 2.5) << "applies immediately";
    QFile::remove(file);
    ASSERT_TRUE(settings->set("autosaveMinutes", 7));
    EXPECT_FALSE(QFile::exists(file)) << "saved only when the sheet closes";

    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    QFile f(file);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << file.toStdString();
    const QByteArray xml = f.readAll();
    EXPECT_TRUE(xml.contains("name=\"pressureMultiplier\" value=\"2.5")) << xml.left(400).toStdString();
    EXPECT_TRUE(xml.contains("name=\"autosaveTimeout\" value=\"7\""));
}

namespace {
QString fixturePath(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}
bool waitFor(const std::function<bool()>& done, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}
}  // namespace

TEST_F(MainWindowTest, searchBarFindsAndSteps) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* bar = find<QQuickItem>("searchBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible());
    key(Qt::Key_F, Qt::ControlModifier);
    EXPECT_TRUE(bar->isVisible());
    type("p1");
    key(Qt::Key_Return);  // search now
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 3 && !controller->searchRunning(); }));
    EXPECT_EQ(controller->searchCurrent(), 1);
    key(Qt::Key_Return);
    EXPECT_EQ(controller->searchCurrent(), 2);
    EXPECT_EQ(controller->pageNumber(), 10);
    key(Qt::Key_Return, Qt::ShiftModifier);
    EXPECT_EQ(controller->searchCurrent(), 1);
    key(Qt::Key_Escape);
    EXPECT_FALSE(bar->isVisible());
    EXPECT_EQ(controller->searchQuery(), "");
}

TEST_F(MainWindowTest, tabOverviewSearchesAllDocuments) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/strokes.xopp")));
    ASSERT_EQ(controller->tabCount(), 3);
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));

    // Typing in the grid starts the search in all documents.
    type("page");
    key(Qt::Key_Return);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto hits = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchHitsRole).toInt(); };
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1) && !running(2); }));
    EXPECT_EQ(hits(0), 0) << "pages.xopp: only p1..p11";
    EXPECT_GE(hits(1), 1) << "old.xopp: \"Page 2\" in the PDF";
    EXPECT_EQ(hits(2), 0);

    // Opening the document with hits shows its search, at its first hit.
    auto* grid = find<QQuickItem>("tabGrid");
    QQuickItem* card = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, card), Q_ARG(int, 1));
    ASSERT_NE(card, nullptr);
    const QPointF center = card->mapToScene(QPointF(card->width() / 2, card->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    ASSERT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);
    EXPECT_EQ(controller->searchQuery(), "page");
    EXPECT_EQ(controller->searchCurrent(), 1);
    EXPECT_TRUE(find<QQuickItem>("searchBar")->isVisible());
    EXPECT_EQ(controller->pageNumber(), 2) << "scrolled to the hit on page 2";
}

TEST_F(MainWindowTest, pageGridZoomsAndJumpsToAPage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* gridPanel = find<QQuickItem>("pageGrid");
    ASSERT_NE(gridPanel, nullptr);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(gridPanel->isVisible());
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 11);

    const int columns = gridPanel->property("columns").toInt();
    ASSERT_GE(columns, 2);
    key(Qt::Key_Plus);  // bigger previews: one column less
    EXPECT_EQ(gridPanel->property("columns").toInt(), columns - 1);
    key(Qt::Key_Minus);
    key(Qt::Key_Minus);
    EXPECT_EQ(gridPanel->property("columns").toInt(), columns + 1);

    wait(50);
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 4));
    ASSERT_NE(cell, nullptr);
    const QPointF center = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    wait(50);
    EXPECT_FALSE(gridPanel->isVisible());
    EXPECT_EQ(controller->pageNumber(), 5);
}

TEST_F(MainWindowTest, pageGridCanShowOnlyPagesWithHits) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->setSearchQuery("p1");
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 3 && !controller->searchRunning(); }));
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 11);

    // The filter chip of the grid (the sidebar has one too: take the visible one inside the grid).
    QQuickItem* chip = nullptr;
    for (auto* c: find<QQuickItem>("pageGrid")->findChildren<QQuickItem*>("searchFilterChip")) {
        if (c->isVisible()) {
            chip = c;
        }
    }
    ASSERT_NE(chip, nullptr);
    const QPointF p = chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p.toPoint());
    wait(50);
    EXPECT_EQ(grid->property("count").toInt(), 3);
    auto* sidebarList = find<QQuickItem>("sidebarList");
    ASSERT_NE(sidebarList, nullptr);
    EXPECT_EQ(sidebarList->property("count").toInt(), 3) << "the sidebar shares the filter";

    // The second page shown is page 10.
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 1));
    ASSERT_NE(cell, nullptr);
    const QPointF c = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, c.toPoint());
    wait(50);
    EXPECT_EQ(controller->pageNumber(), 10);
}

TEST_F(MainWindowTest, pageGridKeepsScrollingAfterTouchpadLift) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    for (int i = 0; i < 30; ++i) {
        controller->addPageAfterCurrent();  // a long document
    }
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    wait(50);
    static QPointingDevice touchpad("test touchpad", 2003, QInputDevice::DeviceType::TouchPad,
                                    QPointingDevice::PointerType::Finger,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 2, 0);
    const QPointF pos = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
    auto wheel = [&](int dy, Qt::ScrollPhase phase) {
        QWheelEvent e(pos, window->mapToGlobal(pos), QPoint(0, dy), QPoint(0, dy * 4), Qt::NoButton, Qt::NoModifier,
                      phase, false, Qt::MouseEventNotSynthesized, &touchpad);
        QCoreApplication::sendEvent(window, &e);
    };
    const double start = grid->property("contentY").toDouble();
    wheel(-20, Qt::ScrollBegin);
    for (int i = 0; i < 8; ++i) {
        wait(10);
        wheel(-20, Qt::ScrollUpdate);
    }
    const double atLift = grid->property("contentY").toDouble();
    EXPECT_GT(atLift, start + 100) << "two-finger scrolling moves the grid";
    wheel(0, Qt::ScrollEnd);
    wait(300);
    EXPECT_GT(grid->property("contentY").toDouble(), atLift + 50) << "no momentum after lifting the fingers";
}

namespace {
QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}
QPoint centerOf(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 3)).toPoint(); }
}  // namespace

TEST_F(MainWindowTest, sidebarSelectCopyPasteDeleteWithPageUndo) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    wait(100);
    auto* pagesModel = qobject_cast<xqt::PagesModel*>(controller->pagesModel());

    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(itemAt(list, 0)));
    QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, centerOf(itemAt(list, 1)));
    EXPECT_EQ(pagesModel->selectedPages(), QList<int>({0, 1})) << "Shift+click from the clicked page";
    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, centerOf(itemAt(list, 1)));  // off
    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, centerOf(itemAt(list, 2)));  // on
    ASSERT_EQ(pagesModel->selectedPages(), QList<int>({0, 2}));

    key(Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(controller->copiedPages(), 2);
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), 13) << "pasted after the selection";

    key(Qt::Key_Delete);
    EXPECT_EQ(controller->pageCount(), 11) << "the pasted (selected) pages are deleted";
    auto* snackbar = find<QQuickItem>("snackbar");
    ASSERT_NE(snackbar, nullptr);
    EXPECT_TRUE(snackbar->isVisible());
    key(Qt::Key_Z, Qt::ControlModifier);  // page undo (the sidebar has the focus)
    EXPECT_EQ(controller->pageCount(), 13);

    // On the canvas, Ctrl+Z is the annotation undo again.
    auto* canvas = find<QQuickItem>("canvas");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2)).toPoint());
    EXPECT_TRUE(canvas->hasActiveFocus());
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), 13) << "no page undo from the canvas";
}

TEST_F(MainWindowTest, pageGridDragAndDropMovesSelectedPages) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* session = controller->tabManager().currentSession();
    const auto original = session->pageOrder();
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    wait(100);
    auto* pagesModel = qobject_cast<xqt::PagesModel*>(controller->pagesModel());
    pagesModel->selectPages({0, 1});

    // Press and hold on page 1, drag to the right half of page 4 (index 3), drop.
    const QPoint from = centerOf(itemAt(grid, 0));
    QQuickItem* targetCell = itemAt(grid, 3);
    const QPoint to = targetCell->mapToScene(QPointF(targetCell->width() * 0.8, targetCell->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    wait(600);  // press and hold
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, from + (to - from) * i / 10);
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    wait(50);
    const auto order = session->pageOrder();
    EXPECT_EQ(order[2], original[0]);
    EXPECT_EQ(order[3], original[1]);
    EXPECT_EQ(order[1], original[3]);
    EXPECT_EQ(pagesModel->selectedPages(), QList<int>({2, 3}));
    EXPECT_TRUE(find<QQuickItem>("pageGrid")->isVisible()) << "dragging does not open the page";

    controller->undoPages();
    EXPECT_EQ(session->pageOrder(), original);
}
