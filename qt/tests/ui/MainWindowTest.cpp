/*
 * xournal-qt: the real main window (Main.qml) off-screen: keyboard shortcuts, settings sheet, tab overview.
 *
 * @license GNU GPLv2 or later
 */
#include <filesystem>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QPointer>
#include <QSignalSpy>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <cairo-pdf.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/LibraryModel.h"
#include "shell/PagesModel.h"
#include "shell/RecentFiles.h"
#include "shell/Previews.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "TextFlow.h"
#include "config-test.h"

namespace {
class MainWindowTest: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        prepareController();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
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
    /// Also items without a QObject parent (made by a Repeater): through the item tree.
    QQuickItem* findItem(const char* name) const {
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
        return walk(window->contentItem());
    }
    /// Waits until something is true (animations, delegates of a list, a popup fading out).
    void until(const std::function<bool()>& done, int ms = 1500) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    void click(QQuickItem* item, Qt::KeyboardModifiers m = Qt::NoModifier) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, m,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    /// Types text into the focused item (QTest::keyClicks is for widgets only).
    void type(const char* text) {
        for (const char* c = text; *c; ++c) {
            QTest::keyClick(window, *c);
        }
        wait(20);
    }

    /// Before the window is loaded. Most tests are about a document (the app starts on the home screen).
    virtual void prepareController() { controller->newDocument(); }

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
    for (int i = 0; i < 80; ++i) {
        controller->addPageAfterCurrent();  // a long document: room to keep scrolling
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
    const double end = grid->property("contentHeight").toDouble() - grid->height();
    ASSERT_LT(atLift, end - 200) << "not at the end of the grid already";
    wheel(0, Qt::ScrollEnd);
    // Momentum: the grid keeps moving after the fingers are lifted (a loaded machine is slow, and a short flick
    // may already be over when we look, so the flick itself counts too)
    auto moved = [&] {
        return grid->property("contentY").toDouble() > atLift + 50 || grid->property("flicking").toBool();
    };
    until(moved, 3000);
    EXPECT_TRUE(moved()) << "no momentum after lifting the fingers";
}

namespace {
QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}
/// The visible items at the center of `target` (diagnostics).
std::string itemsAt(QQuickWindow* window, QQuickItem* target) {
    const QPointF p = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    std::string list;
    for (QQuickItem* c: window->contentItem()->childItems()) {
        if (c->isVisible() && c->contains(c->mapFromScene(p))) {
            list += std::string(c->metaObject()->className()) + "(" + c->objectName().toStdString() +
                    ", z " + std::to_string(c->z()) + ") ";
        }
    }
    return list;
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
    EXPECT_TRUE(canvas->hasActiveFocus()) << "under the click: " << itemsAt(window, canvas);
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

// --- the home screen: library and recent documents ---
namespace {
class HomeScreenTest: public MainWindowTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        const fs::path pdf = fs::path(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"));
        fs::create_directories(root / "Physics");
        fs::copy_file(pdf, root / "Physics" / "sheet.pdf");
        fs::copy_file(pdf, root / "lecture.pdf");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/strokes.xopp")), root / "notes.xopp");
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();  // (tests share the config folder)
    }
    QQuickItem* grid() const { return find<QQuickItem>("libraryGrid"); }
    int gridCount() const { return grid()->property("count").toInt(); }
    QQuickItem* card(int row) const { return itemAt(grid(), row); }
    int rowOf(const char* name) const {
        return qobject_cast<xqt::LibraryModel*>(controller->libraryModel())
                ->rowOf(QString::fromStdString((root / name).string()));
    }
    void click(QQuickItem* item, Qt::KeyboardModifiers m = Qt::NoModifier) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, m,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(HomeScreenTest, startsOnTheLibraryAndOpensDocuments) {
    EXPECT_EQ(controller->tabCount(), 0);
    auto* home = find<QQuickItem>("homeView");
    ASSERT_NE(home, nullptr);
    EXPECT_TRUE(home->isVisible());
    ASSERT_NE(grid(), nullptr);
    ASSERT_EQ(gridCount(), 3);  // Physics, lecture, notes

    click(card(rowOf("Physics")));  // into the folder
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "Physics");
    EXPECT_EQ(gridCount(), 1);
    key(Qt::Key_Backspace);  // up again
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "");
    ASSERT_EQ(gridCount(), 3);

    click(card(rowOf("notes.xopp")));  // opens it
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_FALSE(home->isVisible());
    EXPECT_EQ(controller->title(), "notes.xopp");

    click(find<QQuickItem>("homeTab"));  // back to the library; the document stays open
    EXPECT_TRUE(home->isVisible());
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_EQ(controller->recentModel()->property("count").toInt(), 1);
}

TEST_F(HomeScreenTest, severalDocumentsAreSelectedAndMovedIntoAFolder) {
    ASSERT_EQ(gridCount(), 3);
    click(card(rowOf("lecture.pdf")), Qt::ControlModifier);
    click(card(rowOf("notes.xopp")), Qt::ControlModifier);
    auto* bar = find<QQuickItem>("homeSelectionBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    EXPECT_EQ(controller->libraryModel()->property("selectionCount").toInt(), 2);
    EXPECT_EQ(controller->tabCount(), 0) << "Ctrl+click selects, it does not open";

    click(find<QQuickItem>("moveSelectedButton"));
    QObject* dialog = find("transferDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* folders = find<QQuickItem>("transferFolders");
    ASSERT_NE(folders, nullptr);
    wait(50);
    ASSERT_EQ(folders->property("count").toInt(), 2);  // the library, Physics
    click(itemAt(folders, 1));
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_TRUE(fs::exists(root / "Physics" / "lecture.pdf"));
    EXPECT_TRUE(fs::exists(root / "Physics" / "notes.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    EXPECT_EQ(gridCount(), 1);
    EXPECT_FALSE(bar->isVisible());
}

TEST_F(HomeScreenTest, newDocumentIsSavedInTheLibrary) {
    click(find<QQuickItem>("newDocumentButton"));
    QObject* dialog = find("newDocumentDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    type("Week");
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_TRUE(fs::exists(root / "Week.xopp"));
    EXPECT_EQ(controller->title(), "Week.xopp");
}

TEST_F(MainWindowTest, tabStripUsesTheWholeWidthForManyTabs) {
    auto* list = find<QQuickItem>("tabList");
    auto* plus = find<QQuickItem>("newTabButton");
    ASSERT_NE(list, nullptr);
    ASSERT_NE(plus, nullptr);
    const double few = list->width();
    EXPECT_GT(few, 0);
    for (int i = 0; i < 20; ++i) {
        controller->newDocument();
    }
    wait(100);
    EXPECT_GT(list->property("contentWidth").toDouble(), window->width()) << "more tabs than room";
    EXPECT_GT(plus->mapToScene(QPointF(plus->width(), 0)).x(), window->width() - 60)
            << "the + button follows the tabs to the right end";
    EXPECT_GT(list->width(), window->width() * 0.75);
}

TEST_F(MainWindowTest, shortSearchTextsWaitForEnter) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    key(Qt::Key_F, Qt::ControlModifier);
    type("p1");
    wait(400);
    EXPECT_EQ(controller->searchQuery(), "") << "fewer than 4 characters: not while typing";
    key(Qt::Key_Return);
    EXPECT_EQ(controller->searchQuery(), "p1");
    EXPECT_GT(controller->searchHitCount() + (controller->searchRunning() ? 1 : 0), 0);
    type("0 x");  // "p10 x": long enough
    wait(400);
    EXPECT_EQ(controller->searchQuery(), "p10 x");
}

TEST_F(HomeScreenTest, shortLibrarySearchWaitsForEnter) {
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("le");
    wait(400);
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "");
    key(Qt::Key_Return);
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "le");
}

TEST_F(HomeScreenTest, searchFindsFoldersAndOpensThem) {
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("physics");
    wait(400);
    ASSERT_EQ(controller->libraryModel()->property("searchQuery").toString(), "physics");
    ASSERT_GE(gridCount(), 1);
    auto* first = card(0);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->property("isFolder").toBool());
    click(first);
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "Physics");
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "");
    EXPECT_EQ(field->property("text").toString(), "");
    EXPECT_EQ(gridCount(), 1);  // sheet.pdf
}

TEST_F(HomeScreenTest, libraryMenuMarksThisLibraryWithoutToggles) {
    click(find<QQuickItem>("libraryMenuButton"));
    QObject* menu = find("libraryMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitOpened(menu, true));
    int current = 0;
    const int n = menu->property("count").toInt();
    for (int i = 0; i < n; ++i) {
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, i));
        ASSERT_NE(item, nullptr);
        EXPECT_FALSE(item->property("checkable").toBool()) << "no toggles: " << item->property("text").toString().toStdString();
        if (item->property("current").toBool()) {
            ++current;
            EXPECT_TRUE(item->property("text").toString().contains(QString::fromStdString(root.filename().string())));
            click(item);  // this library: stays in this window
        }
    }
    EXPECT_EQ(current, 1) << "this window's library is marked";
    EXPECT_TRUE(waitOpened(menu, false));
    EXPECT_TRUE(controller->homeVisible());
}

TEST_F(HomeScreenTest, extendedSearchShowsHitPagesAndOpensThePage) {
    auto* lib = controller->libraryModel();
    QElapsedTimer t;
    t.start();
    while (lib->property("indexing").toBool() && t.elapsed() < 5000) {
        wait(20);
    }
    click(find<QQuickItem>("extendedSearchButton"));
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("page 2");
    key(Qt::Key_Return);
    wait(100);
    const int row = rowOf("lecture.pdf");
    ASSERT_GE(row, 0);
    QQuickItem* lecture = card(row);
    ASSERT_NE(lecture, nullptr);
    QQuickItem* strip = nullptr;
    for (auto* c: lecture->findChildren<QQuickItem*>()) {
        if (c->objectName() == "hitPageStrip") {
            strip = c;
        }
    }
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isVisible());
    ASSERT_EQ(strip->property("count").toInt(), 1) << "page 2 has the hit";
    wait(100);
    QQuickItem* page = itemAt(strip, 0);
    ASSERT_NE(page, nullptr);
    EXPECT_GT(grid()->property("cellHeight").toDouble(), grid()->property("cellWidth").toDouble())
            << "taller cells for the page row";

    click(page);  // opens the document at that page, with the search
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_EQ(controller->title(), "lecture.pdf");
    EXPECT_EQ(controller->pageNumber(), 2);
    EXPECT_EQ(controller->searchQuery(), "page 2");

    // Zoom: fewer, bigger cells
    click(find<QQuickItem>("homeTab"));
    const int columns = grid()->property("columns").toInt();
    click(find<QQuickItem>("zoomInButton"));
    EXPECT_EQ(grid()->property("columns").toInt(), std::max(1, columns - 1));
}

TEST_F(MainWindowTest, tabsCloseOnlyOnPurposeAndAllAtOnce) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabManager().count(), 2);

    // A tap (or a middle click) on a tab only shows it; it must not close it
    auto* tab = findItem("tabList");
    ASSERT_NE(tab, nullptr);
    QQuickItem* first = itemAt(tab, 0);
    ASSERT_NE(first, nullptr);
    QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier,
                      first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint());
    wait(50);
    EXPECT_EQ(controller->tabManager().count(), 2) << "the middle button does not close tabs";
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint p = first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint();
    { auto touch = QTest::touchEvent(window, screen); touch.press(0, p); }
    wait(30);
    { auto touch = QTest::touchEvent(window, screen); touch.release(0, p); }
    wait(80);
    EXPECT_EQ(controller->tabManager().count(), 2) << "a finger on a tab does not close it";
    EXPECT_EQ(controller->currentTab(), 0) << "it shows that document";

    // All at once, from the overview (more than one: it asks first)
    auto* overview = find<QObject>("tabOverview");
    QMetaObject::invokeMethod(overview, "open");
    until([&] { return overview->property("visible").toBool(); });
    click(findItem("closeAllButton"));
    auto* dialog = find<QObject>("closeAllDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool()) << "it asks before closing several documents";
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return controller->tabManager().count() == 0; });
    EXPECT_EQ(controller->tabManager().count(), 0);
    EXPECT_TRUE(controller->homeVisible()) << "no documents left: the home screen";
}

TEST_F(MainWindowTest, aTabGetsAWindowOfItsOwn) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabManager().count(), 2);

    // The windows are made like in main(): the same QML with the new controller as "app"
    std::vector<QPointer<QQuickWindow>> made;
    AppController::setWindowFactory([this, &made](AppController* w) {
        auto* context = new QQmlContext(engine->rootContext(), w);
        context->setContextProperty("app", w);
        auto* component = new QQmlComponent(engine.get(), QStringLiteral("XournalQt"), QStringLiteral("Main"), w);
        QObject* object = component->create(context);
        if (!object) {
            qWarning("%s", qPrintable(component->errorString()));
            return;
        }
        object->setParent(w);
        made.push_back(qobject_cast<QQuickWindow*>(object));
    });

    controller->undockTab(0);
    wait(200);
    ASSERT_EQ(made.size(), 1u);
    ASSERT_NE(made[0], nullptr);
    EXPECT_EQ(controller->tabManager().count(), 1) << "the document left the main window";
    ASSERT_EQ(controller->documentWindows().size(), 1u);
    AppController* second = controller->documentWindows().front();
    EXPECT_EQ(second->tabManager().count(), 1);
    // It shows documents only: no home tab
    auto* homeTab = made[0]->findChild<QQuickItem*>("homeTab");
    ASSERT_NE(homeTab, nullptr);
    EXPECT_FALSE(homeTab->isVisible());
    EXPECT_FALSE(second->homeVisible());

    // Back to the main window: the second window asks to be closed
    QSignalSpy closing(second, &AppController::closeWindowRequested);
    second->dockTab(0);
    wait(50);
    EXPECT_EQ(controller->tabManager().count(), 2) << "the document is back in the main window";
    EXPECT_EQ(closing.count(), 1);
    wait(100);  // the window without documents closes itself, with its controller
    EXPECT_TRUE(controller->documentWindows().empty());
    EXPECT_TRUE(made[0].isNull()) << "the window is gone";
    AppController::setWindowFactory({});
}

TEST_F(MainWindowTest, fourOrFiveFingersShowThePagesOrTheDocuments) {
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    auto* grid = find<QQuickItem>("pageGrid");
    auto* overview = find<QObject>("tabOverview");  // a Popup is no Item
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(overview, nullptr);
    auto overviewShown = [&] { return overview->property("visible").toBool(); };
    auto tap = [&](int fingers) {
        {
            auto down = QTest::touchEvent(window, screen);
            for (int i = 0; i < fingers; ++i) {
                down.press(i, QPoint(300 + i * 40, 300));
            }
        }
        wait(30);
        {
            auto up = QTest::touchEvent(window, screen);
            for (int i = 0; i < fingers; ++i) {
                up.release(i, QPoint(300 + i * 40, 300));
            }
        }
        wait(80);
    };

    tap(4);  // the pages of the document
    EXPECT_TRUE(grid->isVisible());
    tap(4);
    until([&] { return !grid->isVisible(); });
    EXPECT_FALSE(grid->isVisible()) << "the same gesture closes it again";

    tap(5);  // all open documents
    until([&] { return overviewShown(); });
    EXPECT_TRUE(overviewShown());
    tap(5);
    until([&] { return !overviewShown(); });
    EXPECT_FALSE(overviewShown());

    tap(3);  // fewer fingers are for the canvas (undo / redo), not for the overviews
    EXPECT_FALSE(grid->isVisible());
    EXPECT_FALSE(overviewShown());
}

TEST_F(MainWindowTest, fiveWidthsInTheToolBar) {
    window->setWidth(1600);  // room for the whole tool bar
    wait(100);
    auto* fifth = findItem("customSizeButton");
    ASSERT_NE(fifth, nullptr);
    click(findItem("sizeButton4"));
    EXPECT_EQ(controller->size(), 4) << "the fourth width (very thick)";

    click(fifth);  // the fifth: the tool's own width
    EXPECT_EQ(controller->size(), 5);
    const double before = controller->customWidth();
    EXPECT_GT(before, 0);

    click(fifth);  // again: change it
    auto* popup = find<QObject>("customWidthPopup");  // a Popup is no Item
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(popup->property("visible").toBool());
    click(findItem("customWidthMore"));
    EXPECT_GT(controller->customWidth(), before);
    click(findItem("customWidthLess"));
    click(findItem("customWidthLess"));
    EXPECT_LT(controller->customWidth(), before);
    EXPECT_EQ(controller->size(), 5);
}

TEST_F(MainWindowTest, toolbarMovesToTheLeftOrRight) {
    auto* gridButton = find<QQuickItem>("contentsButton");  // a tool bar button
    auto* canvas = find<QQuickItem>("canvas");
    ASSERT_NE(gridButton, nullptr);
    auto sceneX = [](QQuickItem* i) { return i->mapToScene(QPointF(0, 0)).x(); };
    const double canvasWidthOnTop = canvas->width();

    controller->setToolbarPosition("left");
    wait(50);
    EXPECT_LT(sceneX(gridButton), 110);
    EXPECT_GT(gridButton->mapToScene(QPointF(0, 0)).y(), 0) << "below the tab strip, not in the header";
    EXPECT_LT(canvas->width(), canvasWidthOnTop);
    EXPECT_GE(sceneX(find<QQuickItem>("sidebar")), 100) << "the page sidebar right of the tools";

    controller->setToolbarPosition("right");
    wait(50);
    EXPECT_GT(sceneX(gridButton), window->width() - 110);
    EXPECT_LE(sceneX(canvas) + canvas->width(), window->width() - 100);

    controller->setToolbarPosition("top");
    wait(50);
    EXPECT_DOUBLE_EQ(canvas->width(), canvasWidthOnTop);
    EXPECT_EQ(controller->toolbarPosition(), "top");
}

TEST_F(MainWindowTest, fullScreenShowsOnlyTheCurrentTool) {
    auto* square = find<QQuickItem>("quickToolSquare");
    ASSERT_NE(square, nullptr);
    EXPECT_FALSE(square->isVisible());
    key(Qt::Key_F11);
    EXPECT_TRUE(window->property("fullScreenMode").toBool());
    EXPECT_TRUE(square->isVisible());
    EXPECT_FALSE(find<QQuickItem>("sidebar")->isVisible());
    auto* gridButton = find<QQuickItem>("contentsButton");  // a tool bar button
    EXPECT_FALSE(gridButton->isVisible()) << "the tools are hidden";
    EXPECT_TRUE(find<QQuickItem>("viewPill")->isVisible()) << "page number, zoom and the page grid stay";

    // Tap the square: all tools; choosing one closes them
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, square->mapToScene(QPointF(28, 28)).toPoint());
    QObject* tools = find("quickTools");
    ASSERT_TRUE(waitOpened(tools, true));
    QQuickItem* eraser = nullptr;
    for (auto* i: window->contentItem()->window()->findChildren<QQuickItem*>()) {
        if (i->property("iconName").toString() == "xopp-tool-eraser" && i->isVisible()) {
            eraser = i;
        }
    }
    ASSERT_NE(eraser, nullptr);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      eraser->mapToScene(QPointF(eraser->width() / 2, eraser->height() / 2)).toPoint());
    EXPECT_EQ(controller->tool(), "eraser");
    EXPECT_TRUE(waitOpened(tools, false));

    key(Qt::Key_Escape);  // leaves full screen
    EXPECT_FALSE(window->property("fullScreenMode").toBool());
    EXPECT_TRUE(gridButton->isVisible());
    controller->selectTool("pen");
}

TEST_F(MainWindowTest, contentsInTheSidebarAndTheOverview) {
    // A 6-page PDF with an outline: "Chapter 1" p.1 (with "Section 1.1" p.3), "Chapter 2" p.5
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("book.pdf").toStdString();
    {
        cairo_surface_t* s = cairo_pdf_surface_create(pdf.c_str(), 300, 400);
        cairo_t* cr = cairo_create(s);
        for (int p = 0; p < 6; ++p) {
            cairo_show_page(cr);
        }
        const int ch1 = cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 1", "page=1", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_pdf_surface_add_outline(s, ch1, "Section 1.1", "page=3", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 2", "page=5", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf)));
    wait(100);

    // Sidebar: Contents
    click(findItem("sidebarContentsButton"));
    auto* outlineList = find<QQuickItem>("outlineList");
    ASSERT_NE(outlineList, nullptr);
    EXPECT_TRUE(outlineList->isVisible());
    ASSERT_EQ(outlineList->property("count").toInt(), 3);
    click(itemAt(outlineList, 2));  // Chapter 2
    EXPECT_EQ(controller->pageNumber(), 5);

    // Overview: the pages of each heading; a page opens there
    click(find<QQuickItem>("contentsButton"));
    auto* overview = find<QQuickItem>("contentsOverview");
    ASSERT_TRUE(overview->isVisible());
    auto* list = find<QQuickItem>("contentsList");
    until([&] { return itemAt(list, 1) != nullptr; });
    QQuickItem* section = itemAt(list, 1);  // Section 1.1: pages 3, 4
    ASSERT_NE(section, nullptr);
    QQuickItem* strip = nullptr;
    for (auto* c: section->findChildren<QQuickItem*>()) {
        if (c->objectName() == "contentsPages") {
            strip = c;
        }
    }
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->property("count").toInt(), 2);
    until([&] { return itemAt(strip, 1) != nullptr; });
    QQuickItem* page4 = itemAt(strip, 1);
    ASSERT_NE(page4, nullptr);
    click(page4);
    until([&] { return !overview->isVisible(); });
    EXPECT_FALSE(overview->isVisible());
    EXPECT_EQ(controller->pageNumber(), 4);
}

TEST_F(MainWindowTest, pageAndLayoutShortcutsInThePill) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int pages = controller->pageCount();

    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages + 1) << "Ctrl+N adds a page after the current one";

    // A tap on the layout button switches between one page and two side by side
    auto* layout = find<QQuickItem>("layoutButton");
    ASSERT_NE(layout, nullptr);
    EXPECT_FALSE(controller->pairedPages());
    click(layout);
    EXPECT_TRUE(controller->pairedPages());
    click(layout);
    EXPECT_FALSE(controller->pairedPages()) << "and back";

    // Fitting: the whole page is smaller than the width of one
    controller->fitWidth();
    wait(30);
    const int wide = controller->zoomPercent();
    controller->fitHeight();
    wait(30);
    EXPECT_LT(controller->zoomPercent(), wide) << "the height of a portrait page needs less zoom";
    controller->fitPage();
    wait(30);
    EXPECT_LE(controller->zoomPercent(), wide);
    // Press and hold offers the other fits, and the menu opens at its button (not at some stale mouse position)
    auto* zoomButton = find<QQuickItem>("zoomButton");
    ASSERT_NE(zoomButton, nullptr);
    const QPoint at = zoomButton->mapToScene(QPointF(zoomButton->width() / 2, zoomButton->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(1000);  // (press and hold)
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
    auto* fitMenu = find<QObject>("fitMenu");
    ASSERT_NE(fitMenu, nullptr);
    until([&] { return fitMenu->property("visible").toBool(); });
    EXPECT_TRUE(fitMenu->property("visible").toBool());
    // At its button: the pill sits at the bottom, so the menu flips above it (never at the window corner)
    EXPECT_LT(fitMenu->property("y").toDouble(), 0);
    EXPECT_GT(fitMenu->property("y").toDouble(), -window->height());
}

TEST_F(MainWindowTest, pagesAreAppendedFromTheSidebar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int before = controller->pageCount();
    auto* sidebarList = find<QQuickItem>("sidebarList");
    ASSERT_NE(sidebarList, nullptr);
    QMetaObject::invokeMethod(sidebarList, "positionViewAtEnd");
    until([&] { return findItem("appendButton") != nullptr; });

    click(findItem("appendMore"));
    click(findItem("appendMore"));  // three pages
    click(findItem("appendButton"));
    wait(80);
    EXPECT_EQ(controller->pageCount(), before + 3);
    controller->undoPages();  // one step for all of them
    wait(50);
    EXPECT_EQ(controller->pageCount(), before);
}

TEST_F(MainWindowTest, aPressOnTheCanvasClosesAnOpenMenu) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* layoutButton = find<QQuickItem>("layoutButton");
    ASSERT_NE(layoutButton, nullptr);
    const QPoint at =
            layoutButton->mapToScene(QPointF(layoutButton->width() / 2, layoutButton->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(1000);  // press and hold
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
    auto* menu = find<QObject>("layoutMenu");
    ASSERT_NE(menu, nullptr);
    until([&] { return menu->property("visible").toBool(); });
    ASSERT_TRUE(menu->property("visible").toBool());

    auto elements = [&] {
        auto* s = controller->tabManager().currentSession();
        return s->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    const size_t before = elements();
    click(find<QQuickItem>("canvas"));  // a menu without a close button: touching the page closes it
    until([&] { return !menu->property("visible").toBool(); });
    EXPECT_FALSE(menu->property("visible").toBool());
    EXPECT_EQ(elements(), before) << "that press must not draw";
}

TEST_F(MainWindowTest, pageGridButtonIsInTheZoomPill) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* button = find<QQuickItem>("pageGridButton");
    ASSERT_NE(button, nullptr);
    auto* canvas = find<QQuickItem>("canvas");
    const QPointF p = button->mapToScene(QPointF(0, 0));
    EXPECT_GT(p.y(), canvas->mapToScene(QPointF(0, canvas->height() / 2)).y()) << "at the bottom, over the canvas";
    click(button);
    EXPECT_TRUE(find<QQuickItem>("pageGrid")->isVisible());
}

TEST_F(HomeScreenTest, movesToAnotherLibraryAndWarnsAboutDownloads) {
    // A Downloads folder of our own (XDG user dirs in the tests' config folder)
    QTemporaryDir dl;
    const fs::path downloads = fs::path(dl.path().toStdString());
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    fs::create_directories(config.toStdString());
    {
        QFile dirs(config + "/user-dirs.dirs");
        ASSERT_TRUE(dirs.open(QIODevice::WriteOnly));
        dirs.write(("XDG_DOWNLOAD_DIR=\"" + downloads.string() + "\"\n").c_str());
    }
    wait(50);
    // Menu of "notes" → Move to… → the Downloads library → its top folder
    const int row = rowOf("notes.xopp");
    ASSERT_GE(row, 0);
    QQuickItem* menuButton = nullptr;
    for (auto* c: card(row)->findChildren<QQuickItem*>()) {
        if (c->objectName() == "cardMenuButton") {
            menuButton = c;
        }
    }
    click(menuButton);
    QObject* menu = find("homeItemMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* moveItem = nullptr;
    for (auto* c: menu->findChildren<QQuickItem*>()) {
        if (c->objectName() == "moveToItem") {
            moveItem = c;
        }
    }
    click(moveItem);
    QObject* dialog = find("transferDialog");
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* box = find<QQuickItem>("transferLibraryBox");
    ASSERT_NE(box, nullptr);
    const QVariantList libs = box->property("model").toList();
    int downloadsIndex = -1;
    for (int i = 0; i < libs.size(); ++i) {
        if (libs[i].toMap().value("downloads").toBool()) {
            downloadsIndex = i;
        }
    }
    ASSERT_GE(downloadsIndex, 0) << "the Downloads folder is offered";
    box->setProperty("currentIndex", downloadsIndex);
    QMetaObject::invokeMethod(box, "activated", Q_ARG(int, downloadsIndex));
    wait(50);
    auto* folders = find<QQuickItem>("transferFolders");
    ASSERT_GE(folders->property("count").toInt(), 1);
    click(itemAt(folders, 0));

    // Into Downloads: asked first
    QObject* warning = find("temporaryImportDialog");
    ASSERT_TRUE(waitOpened(warning, true));
    EXPECT_TRUE(fs::exists(root / "notes.xopp")) << "not before the answer";
    QMetaObject::invokeMethod(warning, "accept");
    wait(100);
    EXPECT_TRUE(fs::exists(downloads / "notes.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    QFile::remove(config + "/user-dirs.dirs");
}

TEST_F(MainWindowTest, textModeTypesThePageText) {
    auto* panel = find<QQuickItem>("textFlowPanel");
    ASSERT_NE(panel, nullptr);
    click(find<QQuickItem>("textModeButton"));
    ASSERT_TRUE(panel->isVisible());
    EXPECT_TRUE(controller->textFlowActive());
    auto* area = find<QQuickItem>("textFlowArea");
    ASSERT_TRUE(area->hasActiveFocus());
    // Beside the pages, not over them
    auto* canvasItem = find<QQuickItem>("canvas");
    EXPECT_LE(canvasItem->mapToScene({canvasItem->width(), 0}).x(), panel->mapToScene({0, 0}).x() + 0.5);
    EXPECT_GT(canvasItem->width(), 100);

    type("# Lecture 5");
    key(Qt::Key_Return);  // after a heading: a paragraph
    type("Some text.");
    key(Qt::Key_Return);
    type("- first");
    key(Qt::Key_Return);  // the list goes on
    type("second");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // an empty item ends the list
    type("After the list.");
    wait(300);

    // The page: Xournal++ text boxes in the layer "Text"
    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::TextFlow::textLayer(page);
    ASSERT_NE(layer, nullptr);
    std::vector<std::string> texts;
    for (const auto& e: layer->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            texts.push_back(static_cast<const Text*>(e)->getText());
        }
    }
    EXPECT_EQ(texts, (std::vector<std::string>{"Lecture 5", "Some text.", "•", "first", "•", "second", "After the list."}));
    const auto blocks = xqt::TextFlow::read(page, xqt::TextFlow::Style{});
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_EQ(blocks[0].kind, xqt::TextBlock::Kind::Heading1);
    EXPECT_EQ(blocks[2].kind, xqt::TextBlock::Kind::Bullet);
    EXPECT_EQ(blocks[4].kind, xqt::TextBlock::Kind::Paragraph);
    EXPECT_TRUE(controller->modified()) << "unsaved changes while typing";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    click(find<QQuickItem>("textFlowDone"));
    EXPECT_FALSE(panel->isVisible());
    EXPECT_FALSE(controller->textFlowActive());
    // One step for the whole text; undo / redo are in the page pill
    auto* undoButton = find<QQuickItem>("undoButton");
    ASSERT_NE(undoButton, nullptr);
    EXPECT_TRUE(find<QQuickItem>("viewPill")->isAncestorOf(undoButton));
    click(undoButton);
    EXPECT_TRUE(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).empty());
    click(find<QQuickItem>("redoButton"));
    EXPECT_EQ(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).size(), 5u);

    // Cancel restores the page
    click(find<QQuickItem>("textModeButton"));
    type("x");
    wait(300);
    click(find<QQuickItem>("textFlowCancel"));
    EXPECT_EQ(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).size(), 5u);
}
