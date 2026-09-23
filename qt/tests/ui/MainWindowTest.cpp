/*
 * xournal-qt: the real main window (Main.qml) off-screen: keyboard shortcuts, settings sheet, tab overview.
 *
 * @license GNU GPLv2 or later
 */
#include <filesystem>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <cmath>

#include <QDir>
#include <QElapsedTimer>
#include <iostream>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QClipboard>
#include <QPointer>
#include <QSignalSpy>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTabletEvent>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <cairo-pdf.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "canvas/CanvasView.h"
#include "canvas/CanvasMemory.h"
#include "canvas/CanvasPage.h"
#include "markdown/MdBox.h"
#include "canvas/PenHover.h"
#include "canvas/MarkdownEditor.h"
#include "canvas/TextEditor.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/LibraryModel.h"
#include "shell/PagesModel.h"
#include "shell/RecentFiles.h"
#include "shell/Previews.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/PageSketches.h"
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
        engine->addImageProvider("sketch", new xqt::SketchProvider);
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
        controller->shutdown();  // first the image workers, then the engine that owns their providers
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

    // Escape closes it, and types nothing into the search (its text is a control character)
    auto* search = find<QQuickItem>("overviewSearchField");
    ASSERT_NE(search, nullptr);
    key(Qt::Key_Escape);
    EXPECT_TRUE(waitOpened(overview, false)) << "Escape closes the overview";
    EXPECT_EQ(search->property("text").toString(), QString());
    // A letter still starts the search
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_A);
    EXPECT_EQ(search->property("text").toString(), QStringLiteral("a"));
}

// Palm rejection: touch does not wait after the pen by default; the height up to which the pen counts as near can
// be chosen, but is only offered once the pen has told its height.
TEST_F(MainWindowTest, penHeightIsOfferedOnceThePenTellsIt) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->get("palmRejectionTimeout").toInt(), 0) << "no wait after the pen by default";
    EXPECT_EQ(settings->get("palmNearHeight").toInt(), 100) << "all of the pen's range by default";
    xqt::PenHover::instance().reset();
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("touchTab"));
    auto* row = findItem("palmNearHeightRow");
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->isVisible()) << "a pen that has not told its height: not offered";

    // The pen hovers at a quarter of its range
    static QPointingDevice pen("test pen", 3001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position | QInputDevice::Capability::ZPosition, 1, 3);
    xqt::PenHover::instance().setProximity(true);
    QTabletEvent hover(QEvent::TabletMove, &pen, QPointF(10, 10), QPointF(10, 10), 0.0, 0.f, 0.f, 0.f, 0.0,
                       static_cast<float>(0.25 * xqt::PenHover::MAX_Z), Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    xqt::PenHover::instance().record(hover);
    until([&] { return row->isVisible(); });
    EXPECT_TRUE(row->isVisible()) << "offered now";
    ASSERT_TRUE(settings->set("palmNearHeight", 40));
    EXPECT_EQ(settings->get("palmNearHeight").toInt(), 40);
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    xqt::PenHover::instance().reset();
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

// Tapping pages in the overview with a finger must select exactly the page that was tapped.
TEST_F(MainWindowTest, tappingPagesInTheOverviewSelectsThem) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* gridPanel = find<QQuickItem>("pageGrid");
    ASSERT_NE(gridPanel, nullptr);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(gridPanel->isVisible());
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    gridPanel->setProperty("selectionMode", true);  // "Select" in the overview: taps select
    wait(50);
    auto* pagesModel = controller->property("pages").value<QObject*>();
    ASSERT_NE(pagesModel, nullptr);
    const auto isSelected = [&](int page) {
        bool result = false;
        QMetaObject::invokeMethod(pagesModel, "isSelected", Q_RETURN_ARG(bool, result), Q_ARG(int, page));
        return result;
    };
    const auto cellCenter = [&](int index) {
        QQuickItem* cell = nullptr;
        QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, index));
        return cell ? cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2)).toPoint() : QPoint();
    };

    static QPointingDevice* finger = QTest::createTouchDevice();
    for (int page: {0, 3, 7}) {
        const QPoint at = cellCenter(page);
        ASSERT_FALSE(at.isNull());
        QTest::touchEvent(window, finger).press(1, at);
        QTest::touchEvent(window, finger).release(1, at);
        wait(60);
        EXPECT_TRUE(isSelected(page)) << "tapped page " << page + 1;
    }
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 3) << "and no others";

    // A finger never holds perfectly still: a tap that slides a few pixels still selects that page
    const QPoint at = cellCenter(5);
    QTest::touchEvent(window, finger).press(1, at);
    QTest::touchEvent(window, finger).move(1, at + QPoint(4, 5));
    QTest::touchEvent(window, finger).release(1, at + QPoint(4, 5));
    wait(60);
    EXPECT_TRUE(isSelected(5)) << "a tap with a little movement still selects";
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 4);

    // A slow tap (a finger easily rests longer than the press-and-hold time) must not throw the selection away:
    // dragging pages only starts once the finger really moves.
    const QPoint slow = cellCenter(9);
    QTest::touchEvent(window, finger).press(1, slow);
    wait(600);
    QTest::touchEvent(window, finger).release(1, slow);
    wait(80);
    EXPECT_TRUE(isSelected(9)) << "the page that was held is selected";
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 5) << "the pages selected before stay selected";
    EXPECT_FALSE(controller->canUndoPages()) << "holding a page still must not move any page";
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

// XQT_SHOTS=<folder>: writes the pictures for the README (off-screen, so no display is needed).
//   XQT_SHOTS=/tmp/shots ./xqt-ui-tests --gtest_filter='*shot*'
namespace {
bool wantShots() { return qEnvironmentVariableIsSet("XQT_SHOTS"); }

void saveShot(QQuickWindow* window, const char* name) {
    const QString folder = qEnvironmentVariable("XQT_SHOTS");
    QDir().mkpath(folder);
    const QImage picture = window->grabWindow();
    ASSERT_FALSE(picture.isNull());
    ASSERT_TRUE(picture.save(folder + '/' + QLatin1String(name) + ".png"));
    std::cerr << "shot " << name << ": " << picture.width() << "x" << picture.height() << "\n";
}
/// No tool tip over the tool bar in the picture: the pointer leaves the buttons and comes to rest on the page
void restPointer(QQuickWindow* window) {
    for (const QPoint& p: {QPoint(window->width() / 2, 300), QPoint(window->width() / 2, window->height() - 60),
                           QPoint(window->width() / 2, window->height() - 30)}) {
        QTest::mouseMove(window, p);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

/// A curve as a hand draws it, from `origin` to the right
void drawCurve(QQuickWindow* window, QPoint origin, int width, int height, double turns) {
    auto at = [&](double t) {
        return origin + QPoint(static_cast<int>(t * width),
                               static_cast<int>(-std::sin(t * turns * 2 * M_PI) * height / 2 * (1 - 0.6 * t)));
    };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(0));
    for (int i = 1; i <= 60; ++i) {
        QTest::mouseMove(window, at(static_cast<double>(i) / 60));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(1));
}
}  // namespace

// A page of notes: Markdown written on the page, a curve drawn by hand, the pages beside it.
TEST_F(MainWindowTest, shotOfTheCanvas) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    window->setProperty("sidebarShown", true);
    controller->goToPage(7);  // squared paper
    wait(600);

    // The note where the page is seen, and the curve under it
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "openBox", Q_ARG(QVariant, 7), Q_ARG(QVariant, 60.0), Q_ARG(QVariant, 300.0));
    wait(200);
    find<QQuickItem>("markdownArea")
            ->setProperty("text", QStringLiteral("## Damped oscillation\n\n"
                                                 "Measured on the shaker at 12 Hz. The envelope follows `exp(-t/tau)`, "
                                                 "with tau about 0.8 s.\n\n"
                                                 "- [x] set up the sensor\n- [ ] repeat it with the heavier mass\n"));
    wait(500);
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, true));
    wait(1200);  // (closing puts the zoom back to what it was before it opened)
    controller->goToPage(7);
    wait(600);

    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    const QPoint origin = canvas->mapToScene(QPointF(canvas->width() * 0.32, canvas->height() * 0.42)).toPoint();
    drawCurve(window, origin, 430, 150, 2.5);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, origin - QPoint(30, 0));  // the axis
    QTest::mouseMove(window, origin + QPoint(460, 0));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, origin + QPoint(460, 0));
    restPointer(window);
    wait(900);  // (the thumbnails of the pages follow)
    saveShot(window, "canvas");
}

// Markdown written on a page, with its source beside it.
TEST_F(MainWindowTest, shotOfMarkdown) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    controller->newDocument();
    while (controller->tabCount() > 1) {
        controller->closeTab(0);
    }
    window->setProperty("sidebarShown", false);
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
    wait(200);
    find<QQuickItem>("markdownArea")->setProperty("text", QStringLiteral(
            "# Seminar, week 3\n\n"
            "Wave equation, **separation of variables**. The ansatz `u(x,t) = X(x)T(t)` gives two problems that are\n"
            "each of one variable.\n\n"
            "## To do\n\n"
            "- [x] read chapter 4\n"
            "- [ ] exercise 4.2 (the boundary conditions!)\n"
            "- [ ] ask about the third eigenvalue\n\n"
            "> The eigenvalues are what the boundary asks for, not what the equation gives.\n\n"
            "```python\n"
            "def modes(n, L):\n"
            "    return [k * pi / L for k in range(1, n + 1)]\n"
            "```\n\n"
            "| mode | n | note |\n| --- | --- | --- |\n| fundamental | 1 | drawn below |\n| first | 2 | node in the middle |\n"));
    restPointer(window);
    wait(900);
    saveShot(window, "markdown");
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, true));
}

// All pages of a document at once.
TEST_F(MainWindowTest, shotOfThePageGrid) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(400);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    restPointer(window);
    wait(1200);
    saveShot(window, "page-grid");
}

// The open documents, and the search over all of them with the pages that have hits.
TEST_F(MainWindowTest, shotOfTheOverview) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/strokes.xopp")));
    wait(600);
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    QObject* overview = find("tabOverview");
    ASSERT_TRUE(waitOpened(overview, true));
    type("page");
    key(Qt::Key_Return);
    wait(800);
    if (!overview->property("extendedView").toBool()) {
        click(find<QQuickItem>("overviewExtendedButton"));
    }
    restPointer(window);
    wait(1200);
    saveShot(window, "overview");
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

// Typing on the library starts a search, but keys that only have a control character as their text do not type it.
TEST_F(HomeScreenTest, onlyVisibleCharactersStartTheLibrarySearch) {
    auto* grid = find<QQuickItem>("libraryGrid");
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(field, nullptr);
    grid->forceActiveFocus();
    key(Qt::Key_Escape);
    key(Qt::Key_Delete);
    key(Qt::Key_Tab);
    grid->forceActiveFocus();
    EXPECT_EQ(field->property("text").toString(), QString()) << "no control characters in the search";
    key(Qt::Key_L);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("l")) << "a letter starts the search";
}

// "Names" next to the library search: the reduced search over names only.
TEST_F(HomeScreenTest, theLibrarySearchCanBeLimitedToNames) {
    auto* button = find<QQuickItem>("searchNamesOnly");
    ASSERT_NE(button, nullptr);
    QObject* library = controller->libraryModel();
    EXPECT_FALSE(library->property("namesOnly").toBool());
    click(button);
    EXPECT_TRUE(library->property("namesOnly").toBool()) << "names only";
    EXPECT_TRUE(button->property("checked").toBool());
    click(button);
    EXPECT_FALSE(library->property("namesOnly").toBool()) << "the full search again";
}

// The library has a button for the settings (no tool bar there, and not everybody has a keyboard at hand).
TEST_F(HomeScreenTest, theSettingsOpenFromTheLibrary) {
    auto* button = find<QQuickItem>("homeSettingsButton");
    ASSERT_NE(button, nullptr);
    QObject* sheet = find("settingsPage");
    ASSERT_NE(sheet, nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(800);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(button);
    EXPECT_TRUE(waitOpened(sheet, true));
}

// A document read in the app shows on its card when and at which page; "Last read first" sorts by that.
TEST_F(HomeScreenTest, cardsShowWhenAndWhereADocumentWasLastRead) {
    const QString lecture = QString::fromStdString((root / "lecture.pdf").string());
    ASSERT_TRUE(controller->openPath(lecture));
    controller->goToPage(1);
    wait(50);
    controller->closeTab(controller->currentTab());
    controller->setHomeVisible(true);
    wait(80);
    const int row = rowOf("lecture.pdf");
    ASSERT_GE(row, 0);
    until([&] { return card(row) != nullptr; });
    QQuickItem* tag = nullptr;
    QQuickItem* text = nullptr;
    for (auto* c: card(row)->findChildren<QQuickItem*>()) {
        if (c->objectName() == "lastReadTag") tag = c;
        if (c->objectName() == "lastReadText") text = c;
    }
    ASSERT_NE(tag, nullptr);
    ASSERT_NE(text, nullptr);
    until([&] { return tag->isVisible(); });
    EXPECT_TRUE(tag->isVisible()) << "read just now";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1000);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    EXPECT_TRUE(text->property("text").toString().endsWith("p.2")) << text->property("text").toString().toStdString();
    QQuickItem* notesTag = nullptr;
    for (auto* c: card(rowOf("notes.xopp"))->findChildren<QQuickItem*>()) {
        if (c->objectName() == "lastReadTag") notesTag = c;
    }
    ASSERT_NE(notesTag, nullptr);
    EXPECT_FALSE(notesTag->isVisible()) << "never read in the app";

    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setSortBy("read");
    EXPECT_EQ(library->data(library->index(0), xqt::LibraryModel::IsFolderRole).toBool(), true) << "Physics";
    EXPECT_EQ(library->data(library->index(1), xqt::LibraryModel::NameRole).toString(), "lecture")
            << "the one read last comes first";
    library->setSortBy("name");
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

TEST_F(MainWindowTest, draggingATabOffTheStripSaysWhatHappens) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    AppController::setWindowFactory({});  // no window is made in this test
    auto* list = findItem("tabList");
    ASSERT_NE(list, nullptr);
    QQuickItem* tab = itemAt(list, 0);
    ASSERT_NE(tab, nullptr);
    auto* hint = find<QQuickItem>("tabDragHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());

    const QPoint start = tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    for (int dy = 10; dy <= 160; dy += 15) {
        QTest::mouseMove(window, start + QPoint(0, dy));
        wait(20);
    }
    EXPECT_TRUE(hint->isVisible()) << "the window says what letting go does";
    EXPECT_TRUE(hint->property("willMove").toBool());

    const int before = controller->tabManager().count();
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, 160));
    wait(100);
    EXPECT_EQ(controller->tabManager().count(), before - 1) << "the document went to a window of its own";
    EXPECT_FALSE(hint->isVisible());
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
    until([&] { return outlineList->isVisible() && outlineList->property("count").toInt() == 3; });
    EXPECT_TRUE(outlineList->isVisible());
    ASSERT_EQ(outlineList->property("count").toInt(), 3);
    until([&] { return itemAt(outlineList, 2) != nullptr; });
    click(itemAt(outlineList, 2));  // Chapter 2
    until([&] { return controller->pageNumber() == 5; });
    EXPECT_EQ(controller->pageNumber(), 5);

    // Overview: the pages of each heading; a page opens there. One button opens it, the one in the tool bar, and
    // it shows whether the overview is open.
    auto* contentsButton = find<QQuickItem>("contentsButton");
    ASSERT_NE(contentsButton, nullptr);
    EXPECT_FALSE(contentsButton->property("checked").toBool());
    int tocButtonsInTheSidebar = 0;
    for (auto* i: find<QQuickItem>("sidebar")->findChildren<QQuickItem*>()) {
        if (i->property("iconName").toString() == QStringLiteral("xqt-toc")) {
            ++tocButtonsInTheSidebar;
        }
    }
    EXPECT_EQ(tocButtonsInTheSidebar, 0) << "not a second button for it beside the pages";
    click(contentsButton);
    auto* overview = find<QQuickItem>("contentsOverview");
    ASSERT_NE(overview, nullptr);
    until([&] { return overview->isVisible(); });
    ASSERT_TRUE(overview->isVisible());
    EXPECT_TRUE(contentsButton->property("checked").toBool()) << "the button shows that it is open";
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
    until([&] { return !overview->isVisible() && controller->pageNumber() == 4; });
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
    // A quick tap with a finger must switch the layout, not open the menu
    auto* menuOnTouch = find<QObject>("layoutMenu");
    ASSERT_NE(menuOnTouch, nullptr);
    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint onLayout = layout->mapToScene(QPointF(layout->width() / 2, layout->height() / 2)).toPoint();
    const bool paired = controller->pairedPages();
    { auto down = QTest::touchEvent(window, finger); down.press(0, onLayout); }
    wait(30);
    { auto up = QTest::touchEvent(window, finger); up.release(0, onLayout); }
    wait(120);
    EXPECT_NE(controller->pairedPages(), paired) << "a tap switches the layout";
    EXPECT_FALSE(menuOnTouch->property("visible").toBool()) << "and opens no menu";
    click(layout);  // back

    // Right click does what press and hold does
    auto* layoutMenu = find<QObject>("layoutMenu");
    ASSERT_NE(layoutMenu, nullptr);
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      layout->mapToScene(QPointF(layout->width() / 2, layout->height() / 2)).toPoint());
    until([&] { return layoutMenu->property("visible").toBool(); });
    EXPECT_TRUE(layoutMenu->property("visible").toBool()) << "the layout menu on right click";
    QMetaObject::invokeMethod(layoutMenu, "close");
    until([&] { return !layoutMenu->property("visible").toBool(); });

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

TEST_F(MainWindowTest, theToolBarCanBePutAway) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* tools = find<QQuickItem>("sideTools");
    auto* square = find<QQuickItem>("quickToolSquare");  // the small tool square
    auto* pen = find<QQuickItem>("contentsButton");  // a tool bar button
    ASSERT_NE(pen, nullptr);
    EXPECT_TRUE(pen->isVisible());

    // The little tab at the end of the bar puts it away
    auto* toggle = find<QQuickItem>("toolbarToggle");
    auto* show = find<QQuickItem>("toolbarShow");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(show, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_FALSE(show->isVisible());
    click(toggle);
    until([&] { return controller->toolbarHidden(); });
    EXPECT_TRUE(controller->toolbarHidden());
    EXPECT_TRUE(show->isVisible()) << "a slim strip brings it back";
    EXPECT_FALSE(toggle->isVisible());
    click(show);
    until([&] { return !controller->toolbarHidden(); });
    EXPECT_FALSE(controller->toolbarHidden());

    controller->setToolbarHidden(true);
    wait(80);
    EXPECT_FALSE(find<QQuickItem>("sideTools")->isVisible()) << "no bar at the side either";
    ASSERT_NE(square, nullptr);
    EXPECT_FALSE(square->isVisible()) << "the tool square is for full screen only";

    controller->setToolbarHidden(false);
    wait(80);
    EXPECT_TRUE(pen->isVisible());
    EXPECT_FALSE(square->isVisible());
    (void)tools;
}

// Docked at a side, the little tab points towards the bar it puts away, and the strip that brings the bar back is at
// that side, pointing into the pages (where the bar will come from).
TEST_F(MainWindowTest, theToolBarTabAndStripFollowTheDockSide) {
    auto* toggle = find<QQuickItem>("toolbarToggle");
    auto* show = find<QQuickItem>("toolbarShow");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(show, nullptr);
    // Where the chevron on it points, from the icon and its rotation (clockwise, y down)
    auto pointsTo = [](QQuickItem* item) -> std::string {
        QQuickItem* arrow = nullptr;
        for (QQuickItem* c: item->childItems()) {
            if (c->property("source").isValid()) {
                arrow = c;
            }
        }
        if (!arrow) {
            return "no arrow";
        }
        const QString source = arrow->property("source").toUrl().toString();
        QPointF d = source.contains("chevron-up")      ? QPointF(0, -1)
                    : source.contains("chevron-down")  ? QPointF(0, 1)
                    : source.contains("chevron-right") ? QPointF(1, 0)
                    : source.contains("chevron-left")  ? QPointF(-1, 0)
                                                       : QPointF();
        const double a = arrow->rotation() * M_PI / 180.0;
        const QPointF r(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
        if (r.y() < -0.5) return "up";
        if (r.y() > 0.5) return "down";
        if (r.x() < -0.5) return "left";
        if (r.x() > 0.5) return "right";
        return "nowhere";
    };
    QQuickItem* area = show->parentItem();  // the window below the tab strip
    ASSERT_NE(area, nullptr);
    const double w = area->width();
    const double h = area->height();
    struct Case {
        const char* position;
        const char* hideArrow;  // towards the bar
        const char* showArrow;  // from the bar into the pages
    };
    for (const Case c: {Case{"top", "up", "down"}, Case{"left", "left", "right"}, Case{"right", "right", "left"}}) {
        SCOPED_TRACE(c.position);
        controller->setToolbarHidden(false);
        controller->setToolbarPosition(c.position);
        wait(60);
        ASSERT_TRUE(toggle->isVisible());
        EXPECT_EQ(pointsTo(toggle), c.hideArrow) << "the tab points towards the bar it puts away";

        controller->setToolbarHidden(true);
        wait(60);
        ASSERT_TRUE(show->isVisible());
        EXPECT_EQ(pointsTo(show), c.showArrow) << "the strip points to where the bar comes in";
        const QRectF strip(show->mapToItem(area, QPointF(0, 0)), show->size());
        if (std::string(c.position) == "top") {
            EXPECT_NEAR(strip.top(), 0, 1) << "at the top edge";
            EXPECT_GT(strip.width(), strip.height()) << "lying along the top edge";
        } else {
            EXPECT_GT(strip.height(), strip.width()) << "standing along the side";
            EXPECT_GT(strip.top(), h / 4) << "about the middle of the side, not at the top";
            EXPECT_LT(strip.bottom(), h * 3 / 4);
            if (std::string(c.position) == "left") {
                EXPECT_NEAR(strip.left(), 0, 1) << "at the left edge";
            } else {
                EXPECT_NEAR(strip.right(), w, 1) << "at the right edge";
            }
        }
        click(show);
        until([&] { return !controller->toolbarHidden(); });
        EXPECT_FALSE(controller->toolbarHidden()) << "a tap on the strip brings the bar back";
    }
    controller->setToolbarPosition("top");
}

TEST_F(MainWindowTest, penPillWithoutAToolBar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* pill = find<QQuickItem>("penPill");
    ASSERT_NE(pill, nullptr);
    EXPECT_FALSE(pill->isVisible()) << "the tool bar is there, no pill";

    controller->setToolbarHidden(true);
    controller->selectTool("pen");
    wait(80);
    EXPECT_TRUE(pill->isVisible());

    // The colors of the pill
    // (items a Repeater made have no QObject parent: through the item tree)
    std::vector<QQuickItem*> colors;
    std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
        for (QQuickItem* child: item->childItems()) {
            if (child->objectName() == "penPillColor") {
                colors.push_back(child);
            }
            collect(child);
        }
    };
    collect(pill);
    QQuickItem* second = colors.size() > 1 ? colors[1] : nullptr;
    ASSERT_NE(second, nullptr) << "three colors to start with";
    click(second);
    EXPECT_EQ(controller->color(), QColor(0xff, 0x00, 0x00)) << "red";

    // The width goes through the five of the tool bar, one per tap, and starts over after the fifth
    const int size = controller->size();
    click(findItem("penPillWidth"));
    EXPECT_EQ(controller->size(), size >= 5 ? 1 : size + 1);
    for (int i = 0; i < 4; ++i) {
        click(findItem("penPillWidth"));
    }
    EXPECT_EQ(controller->size(), size) << "five taps: round once";
    click(findItem("penPillTool"));
    EXPECT_EQ(controller->tool(), "highlighter");
    click(findItem("penPillTool"));
    EXPECT_EQ(controller->tool(), "pen");

    controller->setToolbarHidden(false);
    wait(80);
    EXPECT_FALSE(pill->isVisible());
}

TEST_F(MainWindowTest, ctrlShiftShortcutsWork) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int tabs = controller->tabCount();
    key(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    wait(50);
    EXPECT_EQ(controller->tabCount(), tabs + 1) << "Ctrl+Shift+N makes a document";

    auto* overview = find<QObject>("tabOverview");
    ASSERT_NE(overview, nullptr);
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    until([&] { return overview->property("visible").toBool(); });
    EXPECT_TRUE(overview->property("visible").toBool()) << "Ctrl+Shift+E shows all documents";
    QMetaObject::invokeMethod(overview, "close");
    until([&] { return !overview->property("visible").toBool(); });

    key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    wait(50);
    EXPECT_TRUE(controller->homeVisible()) << "Ctrl+Shift+L shows the library";
}

TEST_F(MainWindowTest, shortcutSheetAndCustomKeys) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* shortcuts = controller->shortcutsModel();
    ASSERT_NE(shortcuts, nullptr);

    // F1 shows them all
    key(Qt::Key_F1);
    auto* sheet = find<QObject>("shortcutSheet");
    ASSERT_NE(sheet, nullptr);
    until([&] { return sheet->property("visible").toBool(); });
    EXPECT_TRUE(sheet->property("visible").toBool());
    QMetaObject::invokeMethod(sheet, "close");
    until([&] { return !sheet->property("visible").toBool(); });  // (it is modal while it fades out)

    // Another key for "add a page", and it works
    bool changedKeys = false;
    QStringList keys;
    QMetaObject::invokeMethod(shortcuts, "setKeys", Q_RETURN_ARG(bool, changedKeys), Q_ARG(QString, "addPage"),
                              Q_ARG(QString, "Ctrl+Alt+P"));
    ASSERT_TRUE(changedKeys);
    QMetaObject::invokeMethod(shortcuts, "keys", Q_RETURN_ARG(QStringList, keys), Q_ARG(QString, "addPage"));
    EXPECT_EQ(keys, QStringList{"Ctrl+Alt+P"});
    wait(50);
    const int before = controller->pageCount();
    key(Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(controller->pageCount(), before + 1) << "the new keys add a page";
    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), before + 1) << "the old ones do not";

    // A conflict is reported, and the defaults come back
    QString conflict;
    QMetaObject::invokeMethod(shortcuts, "conflict", Q_RETURN_ARG(QString, conflict), Q_ARG(QString, "addPage"),
                              Q_ARG(QString, "Ctrl+S"));
    EXPECT_FALSE(conflict.isEmpty()) << "Ctrl+S saves";
    QMetaObject::invokeMethod(shortcuts, "resetAll");
    wait(50);
    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), before + 2) << "Ctrl+N again";
}

TEST_F(MainWindowTest, backgroundOfExistingPagesAndTheInsertDialog) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(50);
    auto backgroundOf = [&](int page) {
        auto* s = controller->tabManager().currentSession();
        return s->getDocument()->getPage(static_cast<size_t>(page))->getBackgroundType().format;
    };
    ASSERT_TRUE(controller->pagesHavePdfBackground({0})) << "the fixture annotates a PDF";

    // The dialog warns before a PDF page is replaced
    auto* dialog = find<QObject>("backgroundDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{0})));
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("overPdf").toBool());
    auto* warning = find<QQuickItem>("pdfWarning");
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->isVisible());
    QMetaObject::invokeMethod(dialog, "reject");
    wait(50);
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Pdf) << "cancelling changes nothing";

    // Graph paper instead, and back with one undo
    const int graph = controller->settingsModel()->property("pageBackgroundFormats").toStringList().indexOf("graph");
    ASSERT_GE(graph, 0);
    EXPECT_TRUE(controller->changePageBackground({0}, graph));
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Graph);
    controller->undoPages();
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Pdf);

    // The insert dialog still opens from the page menu (it is asked for through the controller)
    auto* insert = find<QObject>("insertPagesDialog");
    ASSERT_NE(insert, nullptr);
    QMetaObject::invokeMethod(insert, "openAt", Q_ARG(QVariant, QVariant::fromValue(1)));
    until([&] { return insert->property("visible").toBool(); });
    EXPECT_TRUE(insert->property("visible").toBool());
    QMetaObject::invokeMethod(insert, "reject");
}

TEST_F(MainWindowTest, rightClickOffersPasteWhereItWasClicked) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* pill = find<QObject>("contextPill");
    ASSERT_NE(pill, nullptr);
    EXPECT_FALSE(pill->property("visible").toBool());

    // Something to paste
    QGuiApplication::clipboard()->setText("pasted here");
    auto* canvas = find<QQuickItem>("canvas");
    const QPoint at = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() * 0.3)).toPoint();
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, at);
    until([&] { return pill->property("visible").toBool(); });
    ASSERT_TRUE(pill->property("visible").toBool()) << "the pill appears where it was clicked";

    auto elements = [&] {  // on any page and layer: the click may be on the first or the second page
        auto* s = controller->tabManager().currentSession();
        size_t count = 0;
        for (size_t p = 0; p < s->getDocument()->getPageCount(); ++p) {
            for (const Layer* layer: s->getDocument()->getPage(p)->getLayersView()) {
                count += layer->getElementsView().size();
            }
        }
        return count;
    };
    const size_t before = elements();
    QQuickItem* pasteButton = findItem("contextPaste");
    ASSERT_NE(pasteButton, nullptr);
    QMetaObject::invokeMethod(pasteButton, "clicked");  // (a click in the overlay is unreliable off screen)
    until([&] { return elements() > before; });
    EXPECT_EQ(elements(), before + 1) << "the text went onto the page";
    until([&] { return !pill->property("visible").toBool(); });
    EXPECT_FALSE(pill->property("visible").toBool());
}

TEST_F(MainWindowTest, printingAsksWhatAndWhichPages) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* dialog = find<QObject>("printDialog");
    ASSERT_NE(dialog, nullptr);

    // Ctrl+P asks first (printer and copies come from the system afterwards)
    key(Qt::Key_P, Qt::ControlModifier);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(dialog->property("range").toString(), QString()) << "everything by default";
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // Pages chosen in the overview come as a range
    QMetaObject::invokeMethod(dialog, "openFor",
                              Q_ARG(QVariant, QVariant::fromValue(QVariantList{0, 1, 2, 4})));
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("1-3,5"));
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // The whole way of the "Print" button in the page overview: the pages selected there must arrive as page
    // numbers in the dialog (they came through as "QVariant()1,QVariant()1" before).
    click(find<QQuickItem>("pageGridButton"));
    until([&] { return find<QQuickItem>("pageGrid")->isVisible(); });
    auto* pagesModel = controller->property("pages").value<QObject*>();
    ASSERT_NE(pagesModel, nullptr);
    const QList<int> firstAndThird{0, 2};
    QMetaObject::invokeMethod(pagesModel, "selectPages", Q_ARG(QList<int>, firstAndThird));
    auto* printSelected = find<QQuickItem>("printSelectedButton");
    ASSERT_NE(printSelected, nullptr);
    until([&] { return printSelected->isVisible(); });
    click(printSelected);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("1,3")) << "the pages that were chosen";
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // Two pages next to each other: one range
    const QList<int> lastTwo{3, 4};
    QMetaObject::invokeMethod(pagesModel, "selectPages", Q_ARG(QList<int>, lastTwo));
    click(printSelected);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("4-5"));
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });
}

TEST_F(MainWindowTest, layersInTheSidebar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    click(findItem("sidebarLayersButton"));
    auto* list = findItem("layerList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() > 0; });
    EXPECT_EQ(list->property("count").toInt(), 2) << "one layer and the background";

    click(findItem("addLayerButton"));
    until([&] { return list->property("count").toInt() == 3; });
    EXPECT_EQ(list->property("count").toInt(), 3);

    // The eye of the top layer hides it
    QQuickItem* first = itemAt(list, 0);
    ASSERT_NE(first, nullptr);
    QQuickItem* eye = nullptr;
    for (auto* i: first->findChildren<QQuickItem*>()) {
        if (i->objectName() == "layerVisibleButton") {
            eye = i;
        }
    }
    ASSERT_NE(eye, nullptr);
    click(eye);
    wait(50);
    // The list makes its delegates again when the layers change, so `first` must not be used any more
    first = itemAt(list, 0);
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->property("layerVisible").toBool());
    click(findItem("showAllLayersButton"));
    wait(50);
    EXPECT_TRUE(itemAt(list, 0)->property("layerVisible").toBool());
}

// The menu of a page: the everyday actions are icons, so it stays small (it used to be a list of sixteen lines).
TEST_F(MainWindowTest, thePageMenuIsSmallAndWorksByIcons) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(80);
    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 2);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    ASSERT_TRUE(menu->property("visible").toBool());
    const double height = menu->property("height").toDouble();
    EXPECT_GT(height, 100) << "it is there";
    EXPECT_LT(height, 320) << "and less than half of a line for every action (sixteen of them once)";
    EXPECT_LT(menu->property("width").toDouble(), 300) << "and not wide either";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(700);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // The icons do the work of the lines
    auto* paste = find<QQuickItem>("pageMenuPaste");
    ASSERT_NE(paste, nullptr);
    EXPECT_FALSE(paste->isEnabled()) << "nothing copied yet";
    click(find<QQuickItem>("pageMenuCopy"));
    until([&] { return !menu->property("visible").toBool(); });
    EXPECT_EQ(controller->copiedPages(), 1) << "the page was copied and the menu closed";

    // Moving the third page up puts it before the second one, as one page change to undo
    const int pageCount = controller->pageCount();
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    auto* up = find<QQuickItem>("pageMenuUp");
    ASSERT_NE(up, nullptr);
    EXPECT_TRUE(up->isEnabled());
    click(up);
    wait(80);
    EXPECT_TRUE(controller->canUndoPages()) << "the page moved";
    EXPECT_EQ(controller->pageCount(), pageCount) << "and none was lost";

    // On the first page there is nowhere to move it up to
    menu->setProperty("page", 0);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    EXPECT_FALSE(find<QQuickItem>("pageMenuUp")->isEnabled());
    QMetaObject::invokeMethod(menu, "close");
}

// Scrolling through the document moves the highlight of the current page in the sidebar; that must not make its
// thumbnails load again (they blinked: the thicker border of the current page changed the size they were drawn at).
TEST_F(MainWindowTest, sidebarThumbnailsStayWhenTheCurrentPageChanges) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(100);
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    until([&] { return itemAt(list, 1) != nullptr; });
    QQuickItem* second = itemAt(list, 1);
    ASSERT_NE(second, nullptr);
    QQuickItem* image = nullptr;
    for (auto* c: second->findChildren<QQuickItem*>()) {
        if (c->objectName() == "sidebarThumbnail") {
            image = c;
        }
    }
    ASSERT_NE(image, nullptr);
    QQuickItem* frame = image->parentItem();
    ASSERT_NE(frame, nullptr);
    const qreal dpr = window->effectiveDevicePixelRatio();
    const auto drawnAtFrameWidth = [&] {
        return image->property("sourceSize").toSize().width() == qRound(frame->width() * dpr);
    };
    EXPECT_TRUE(drawnAtFrameWidth());
    controller->goToPage(1);  // the second page becomes the current one: its frame gets the thick border
    until([&] { return image->width() < frame->width() - 4; });
    ASSERT_LT(image->width(), frame->width() - 4) << "the image is narrower now (the thick border)";
    EXPECT_TRUE(drawnAtFrameWidth()) << "but it is still drawn at the width of the frame: not drawn again";
    controller->goToPage(0);
    until([&] { return image->width() > frame->width() - 4; });
    EXPECT_TRUE(drawnAtFrameWidth());
}

// Flying through the sidebar shows every page at once as its sketch (drawn in advance); the sharp thumbnails are
// asked for when it slows down.
TEST_F(MainWindowTest, sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    auto parts = [](QQuickItem* entry) {
        std::pair<QQuickItem*, QQuickItem*> p{nullptr, nullptr};  // sketch, sharp
        for (auto* c: entry->findChildren<QQuickItem*>()) {
            if (c->objectName() == "pageSketch") {
                p.first = c;
            } else if (c->objectName() == "pageSharp") {
                p.second = c;
            }
        }
        return p;
    };
    until([&] { return itemAt(list, 0) != nullptr; });
    auto [sketch, sharp] = parts(itemAt(list, 0));
    ASSERT_NE(sketch, nullptr);
    ASSERT_NE(sharp, nullptr);
    until([&] { return sketch->property("source").toUrl().toString().startsWith("image://sketch/"); });
    EXPECT_TRUE(sketch->property("source").toUrl().toString().startsWith("image://sketch/"));
    EXPECT_TRUE(sharp->property("source").toUrl().toString().startsWith("image://thumbnail/"));

    // Racing to the end: the pages that come into view have their sketch only
    auto* race = list->property("race").value<QObject*>();
    ASSERT_NE(race, nullptr);
    // (it calms down 150 ms after the last move: not while this test is slow under load)
    race->property("calm").value<QObject*>()->setProperty("interval", 60000);
    race->setProperty("racing", true);
    const int last = controller->pageCount() - 1;
    QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, last), Q_ARG(int, 2 /* ListView.End */));
    until([&] { return itemAt(list, last) != nullptr; });
    auto [lastSketch, lastSharp] = parts(itemAt(list, last));
    ASSERT_NE(lastSketch, nullptr);
    EXPECT_TRUE(lastSketch->property("source").toUrl().toString().startsWith("image://sketch/"));
    EXPECT_TRUE(lastSharp->property("source").toUrl().isEmpty()) << "no sharp one while racing";
    race->setProperty("racing", false);
    EXPECT_TRUE(lastSharp->property("source").toUrl().toString().startsWith("image://thumbnail/")) << "slowed down";
    sketches.setDelays(400, 1500);
}

// A page that is not rendered yet shows its preview (drawn in advance) on the canvas, not a white page.
TEST_F(MainWindowTest, theCanvasShowsThePreviewUntilThePageIsRendered) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    auto* render = controller->context().getRenderService();
    render->blockRerenderZoom(std::chrono::milliseconds(60000));  // (as if rendering took long)
    xqt::CanvasView* view = controller->tabManager().currentView();
    const size_t target = 6;
    view->getPage(target)->deleteViewBuffer();
    controller->goToPage(static_cast<int>(target));
    int shown = 0;
    until([&] {
        QMetaObject::invokeMethod(canvas, "previewsShown", Q_RETURN_ARG(int, shown));
        return shown > 0;
    });
    EXPECT_GE(shown, 1) << "the preview instead of a white page";
    render->blockRerenderZoom(std::chrono::milliseconds(0));
    until([&] { return view->getPage(target)->bufferInfo().valid; }, 5000);
    ASSERT_TRUE(view->getPage(target)->bufferInfo().valid);
    until([&] {
        QMetaObject::invokeMethod(canvas, "previewsShown", Q_RETURN_ARG(int, shown));
        return shown == 0;
    });
    EXPECT_EQ(shown, 0) << "rendered: the page itself";
    sketches.setDelays(400, 1500);
}

// Scrolling fast must not compose and upload whole pages in one frame (that froze the canvas): only the tiles in
// view, a few per frame, with the page's preview under what is not composed yet.
TEST_F(MainWindowTest, fastScrollingComposesFewTilesPerFrameAndShowsPreviews) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    xqt::CanvasView* view = controller->tabManager().currentView();
    xqt::CanvasMemory::instance().planNow();
    until([&] { return view->getPage(view->pageCount() - 1)->bufferInfo().valid; }, 10000);
    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    // Zoomed in, a page is many tiles (as on a big screen)
    view->getViewController().setZoom(3.0, QPointF(100, 100));
    wait(400);  // (renders wait until the zoom is stable)
    xqt::CanvasMemory::instance().planNow();
    until(
            [&] {
                for (size_t p = 0; p < view->pageCount(); ++p) {
                    if (view->getPage(p)->bufferInfo().zoom != 3.0) {
                        return false;
                    }
                }
                return true;
            },
            20000);
    ASSERT_DOUBLE_EQ(view->getPage(view->pageCount() - 1)->bufferInfo().zoom, 3.0);
    wait(100);
    QMetaObject::invokeMethod(canvas, "forgetTileCount");

    int previews = 0, tiles = 0;
    for (int page = 1; page < controller->pageCount(); ++page) {  // (as if the scroll bar were dragged)
        controller->goToPage(page);
        wait(16);
    }
    QMetaObject::invokeMethod(canvas, "mostTilesInAFrame", Q_RETURN_ARG(int, tiles));
    QMetaObject::invokeMethod(canvas, "framesWithPreviews", Q_RETURN_ARG(int, previews));
    EXPECT_LE(tiles, 64) << "never a whole page (about 120 tiles) at once";
    EXPECT_GT(previews, 0) << "the pages scrolled past show their preview while their tiles are missing";
    sketches.setDelays(400, 1500);
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

// The setsquare and the compass sit in the shapes menu (they are not a way of drawing, they lie on the page).
TEST_F(MainWindowTest, theShapesMenuPutsTheSetsquareOnThePage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    click(find<QQuickItem>("shapeButton"));
    auto* item = find<QObject>("setsquareItem");
    ASSERT_NE(item, nullptr);
    until([&] { return item->property("visible").toBool(); });
    EXPECT_FALSE(item->property("checked").toBool());
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return controller->geometryTool() == QStringLiteral("setsquare"); });
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("setsquare"));
    EXPECT_TRUE(item->property("checked").toBool()) << "the entry shows that it is out";

    // Its pill: the tool can be put aside and brought back, and turned in steps of 15 degrees
    auto* pill = find<QQuickItem>("geometryPill");
    ASSERT_NE(pill, nullptr);
    until([&] { return pill->isVisible(); });
    EXPECT_TRUE(pill->isVisible()) << "the pill comes with the tool";
    auto* steps = find<QQuickItem>("geometrySteps");
    ASSERT_NE(steps, nullptr);
    click(steps);
    EXPECT_TRUE(controller->geometryAngleSteps());
    EXPECT_TRUE(steps->property("checked").toBool());
    // The button shows the setting also when it changes elsewhere (another tab has its own setsquare)
    controller->setGeometryAngleSteps(false);
    EXPECT_FALSE(steps->property("checked").toBool()) << "it follows the setting after it was tapped";
    click(steps);
    EXPECT_TRUE(controller->geometryAngleSteps());
    EXPECT_TRUE(steps->property("checked").toBool());
    const double wide = pill->width();
    click(find<QQuickItem>("geometryToggle"));
    EXPECT_TRUE(controller->geometryMinimized()) << "put aside";
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("setsquare")) << "but still switched on";
    EXPECT_TRUE(pill->isVisible()) << "the pill stays";
    until([&] { return pill->width() < wide; });
    EXPECT_LT(pill->width(), wide) << "small, only the icon";
    EXPECT_FALSE(steps->isVisible());
    click(find<QQuickItem>("geometryToggle"));
    EXPECT_FALSE(controller->geometryMinimized()) << "back again";
    EXPECT_TRUE(controller->geometryAngleSteps()) << "with its steps";
    click(find<QQuickItem>("geometryHold"));
    EXPECT_FALSE(controller->geometryHeldToStroke()) << "a page without ink: nothing to hold on to";

    // Its marks, every centimetre or every half
    auto elements = [&] {
        return controller->tabManager().currentSession()->getDocument()->getPage(0)->getSelectedLayer()
                ->getElements().size();
    };
    const size_t before = elements();
    click(find<QQuickItem>("geometryMarks"));
    EXPECT_EQ(elements(), before + 17) << "a mark every centimetre along the edge";
    click(find<QQuickItem>("geometryMarkSpacing"));
    EXPECT_DOUBLE_EQ(controller->geometryMarkSpacing(), 0.5);
    click(find<QQuickItem>("geometryMarks"));
    EXPECT_EQ(elements(), before + 17 + 33) << "and every half centimetre";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        auto* v = qobject_cast<xqt::CanvasView*>(find<QQuickItem>("canvas")->property("view").value<QObject*>());
        v->getViewController().panBy(QPointF(0, -420));
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Snapping to the grid: off at first, switched from the same menu
    auto* snap = find<QObject>("snapGridItem");
    ASSERT_NE(snap, nullptr);
    auto* settingsModel = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(snap->property("checked").toBool());
    QMetaObject::invokeMethod(snap, "toggle");
    QMetaObject::invokeMethod(snap, "triggered");
    EXPECT_TRUE(settingsModel->get("snapGrid").toBool()) << "switched on from the shapes menu";
    QMetaObject::invokeMethod(snap, "toggle");
    QMetaObject::invokeMethod(snap, "triggered");
    EXPECT_FALSE(settingsModel->get("snapGrid").toBool());

    // The compass instead, then away again
    auto* compass = find<QObject>("compassItem");
    ASSERT_NE(compass, nullptr);
    QMetaObject::invokeMethod(compass, "triggered");
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("compass"));
    QMetaObject::invokeMethod(compass, "triggered");
    EXPECT_TRUE(controller->geometryTool().isEmpty());
    until([&] { return !pill->isVisible(); });
    EXPECT_FALSE(pill->isVisible()) << "the Shapes entry takes tool and pill away";
}

// Selected PDF text used to freeze the canvas: its knobs lie over the whole canvas, and after copying nothing
// told the UI that nothing is selected any more, so the (still visible) overlay swallowed every press.
TEST_F(MainWindowTest, theCanvasKeepsItsInputWhilePdfTextIsSelected) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    ASSERT_NE(canvasItem, nullptr);
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto* handles = find<QQuickItem>("pdfTextHandles");
    ASSERT_NE(handles, nullptr);
    EXPECT_TRUE(handles->property("inputTransparent").toBool()) << "only its knobs take presses";

    // Where a word of the PDF is (the search knows it)
    auto* session = controller->tabManager().currentSession();
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    ASSERT_FALSE(session->search().hits().empty());
    const QRectF hit = session->search().hits().front().rect;
    session->search().clear();
    const QPointF onWord =
            view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();
    const QPoint onWordInWindow = canvasItem->mapToScene(onWord).toPoint();

    QSignalSpy changed(controller.get(), &AppController::pdfTextSelectionChanged);
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y())) << "the word under the finger";
    until([&] { return handles->isVisible(); });
    ASSERT_TRUE(controller->pdfTextIsSelected());
    EXPECT_TRUE(handles->isVisible()) << "the knobs are shown";
    EXPECT_GE(changed.count(), 1) << "the UI is told that something is selected";

    // A press far from the text unselects it - the overlay of the knobs must not swallow that press
    const auto elements = [&] {
        return session->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    const size_t before = elements();
    controller->selectTool("pen");
    const QPoint far = onWordInWindow + QPoint(0, 260);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, far);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, far);
    wait(50);
    EXPECT_FALSE(controller->pdfTextIsSelected()) << "a press beside the text unselects it";
    until([&] { return !handles->isVisible(); });
    EXPECT_FALSE(handles->isVisible());
    EXPECT_EQ(elements(), before) << "that press only unselects, it does not draw";

    // Copying ends the selection as well, and the canvas draws again afterwards (it used to be frozen)
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y()));
    until([&] { return handles->isVisible(); });
    EXPECT_TRUE(controller->copyPdfText());
    EXPECT_FALSE(controller->pdfTextIsSelected());
    until([&] { return !handles->isVisible(); });
    EXPECT_FALSE(handles->isVisible()) << "nothing is selected: the knobs are gone";
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, far);
    QTest::mouseMove(window, far + QPoint(40, 20));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, far + QPoint(40, 20));
    wait(50);
    EXPECT_EQ(elements(), before + 1) << "the pen works again after copying the text";
}

// The knobs and the actions belong to the text, so they travel with the page. Once the text is scrolled out of
// sight the pill waits at the top edge and takes the reader back to it.
TEST_F(MainWindowTest, theSelectedPdfTextTakesItsHandlesAndActionsAlong) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto* bar = find<QQuickItem>("pdfTextBar");
    auto* handles = find<QQuickItem>("pdfTextHandles");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(handles, nullptr);

    // Select a word of the PDF (the search knows where one is)
    auto* session = controller->tabManager().currentSession();
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    ASSERT_FALSE(session->search().hits().empty());
    const QRectF hit = session->search().hits().front().rect;
    session->search().clear();
    const QPointF onWord = view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y()));
    until([&] { return bar->isVisible(); });
    ASSERT_TRUE(bar->isVisible());
    EXPECT_FALSE(bar->property("away").toBool()) << "the text is in view";
    const double barY = bar->y();
    const double textY = controller->pdfSelectionBox().y();

    // Scrolling moves the text under the pill, so the pill goes along (a little, the word stays in view)
    const double pan = std::max(10.0, std::min(40.0, textY - 20));
    view->getViewController().panBy(QPointF(0, -pan));
    wait(60);
    EXPECT_NEAR(controller->pdfSelectionBox().y(), textY - pan, 2);
    EXPECT_NEAR(bar->y(), barY - pan, 3) << "the actions stay at the text";
    EXPECT_FALSE(bar->property("away").toBool()) << "still in view";

    // Far away: the pill waits at the top of the canvas, the knobs are out of the way
    controller->jumpToPage(controller->pageCount() - 1);
    wait(80);
    until([&] { return bar->property("away").toBool(); });
    EXPECT_TRUE(bar->property("away").toBool()) << "the text is out of sight";
    EXPECT_TRUE(bar->isVisible()) << "but the actions stay, the text is still selected";
    EXPECT_LT(bar->y(), canvasItem->mapToScene(QPointF(0, 0)).y() + 40) << "at the top edge";
    auto* back = find<QQuickItem>("pdfBackToSelection");
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->isVisible()) << "with the way back to the text";
    for (auto* knob: handles->findChildren<QQuickItem*>()) {
        EXPECT_FALSE(knob->isVisible() && knob->property("startEnd").isValid())
                << "no knobs while the text is away";
    }

    // The way back brings it into view again
    click(back);
    until([&] { return !bar->property("away").toBool(); });
    EXPECT_FALSE(bar->property("away").toBool());
    const QRectF box = controller->pdfSelectionBox();
    EXPECT_GE(box.y(), 0);
    EXPECT_LE(box.y(), canvasItem->height());
    EXPECT_TRUE(controller->pdfTextIsSelected()) << "and it is still the same selection";
}

// The eraser button: a tap takes the eraser, tapped again it offers how the eraser erases.
TEST_F(MainWindowTest, theEraserButtonOffersHowItErases) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    auto* button = find<QQuickItem>("eraserButton");
    ASSERT_NE(button, nullptr);
    auto* menu = find<QObject>("eraserMenu");
    ASSERT_NE(menu, nullptr);
    click(button);
    EXPECT_EQ(controller->tool(), "eraser");
    EXPECT_FALSE(menu->property("visible").toBool()) << "the first tap only takes the eraser";
    click(button);
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool()) << "the second tap offers its kinds";
    auto* whole = find<QObject>("eraserWholeStrokes");
    ASSERT_NE(whole, nullptr);
    QMetaObject::invokeMethod(whole, "triggered");
    EXPECT_EQ(settings->get("eraserMode").toString(), QStringLiteral("deleteStroke"));
    EXPECT_TRUE(whole->property("checked").toBool());
    QMetaObject::invokeMethod(menu, "close");
    until([&] { return !menu->property("visible").toBool(); });

    // The right mouse button opens it as well, with another tool in hand
    controller->selectTool("pen");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool()) << "right click";
    QMetaObject::invokeMethod(find<QObject>("eraserStandard"), "triggered");
    EXPECT_EQ(settings->get("eraserMode").toString(), QStringLiteral("default"));
    EXPECT_EQ(controller->tool(), "eraser") << "choosing a kind takes the eraser";
}

// The tools on single keys, as in Xournal++: P pen, E eraser, H highlighter, T text, S select, L lasso, A hand. Not
// while typing into a field, and not in the overviews (they search what is typed).
TEST_F(MainWindowTest, singleKeysTakeTheTools) {
    const std::vector<std::pair<Qt::Key, QString>> keys{{Qt::Key_E, "eraser"},      {Qt::Key_H, "highlighter"},
                                                        {Qt::Key_T, "text"},        {Qt::Key_S, "selectRect"},
                                                        {Qt::Key_L, "selectRegion"}, {Qt::Key_A, "hand"},
                                                        {Qt::Key_P, "pen"}};
    for (const auto& [k, tool]: keys) {
        key(k);
        EXPECT_EQ(controller->tool(), tool) << "key " << QKeySequence(k).toString().toStdString();
    }

    // Typing into the search bar: the letters are text there
    key(Qt::Key_F, Qt::ControlModifier);
    auto* field = find<QQuickItem>("searchField");
    ASSERT_NE(field, nullptr);
    until([&] { return field->hasActiveFocus(); });
    key(Qt::Key_E);
    EXPECT_EQ(controller->tool(), "pen") << "E typed into the search, not the eraser";
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("e"));
    key(Qt::Key_Escape);

    // In the page grid they do nothing to the tools
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(find<QQuickItem>("pageGrid")->isVisible());
    key(Qt::Key_H);
    EXPECT_EQ(controller->tool(), "pen");
}

// Ctrl+Shift+F searches all open documents, Ctrl+Alt+F the library - from anywhere.
// While a search runs, every hit found rebuilds the list of pages with hits. Their pictures must stay (QML keeps
// them by their URL), not blink away and be asked for again.
TEST_F(MainWindowTest, tabOverviewHitPicturesStayWhenMoreHitsCome) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };
    type("page");
    key(Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1); }));
    if (!overview->property("extendedView").toBool()) {
        click(find<QQuickItem>("overviewExtendedButton"));
    }
    QQuickItem* picture = nullptr;
    until([&] { return (picture = findItem("overviewHitPagePicture")) != nullptr && picture->isVisible(); }, 5000);
    ASSERT_NE(picture, nullptr);
    const QString source = picture->property("source").toUrl().toString();
    EXPECT_TRUE(source.startsWith("image://thumbnail/")) << source.toStdString();
    EXPECT_TRUE(picture->property("cache").toBool()) << "kept by QML: every hit builds the list again";
    EXPECT_EQ(source.mid(QString("image://thumbnail/").size()).count('/'), 2)
            << "the page and its revision only, nothing that changes while the overview is open: " << source.toStdString();
    until([&] { return picture->property("status").toInt() == 1 /* Image.Ready */; });
    ASSERT_EQ(picture->property("status").toInt(), 1);

    // As if another hit came: the list is built again
    key(Qt::Key_Return);
    wait(300);
    picture = findItem("overviewHitPagePicture");
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->property("source").toUrl().toString(), source) << "the same page: the same URL";
    EXPECT_EQ(picture->property("status").toInt(), 1) << "there at once, not loaded again";
}

// XQT_BENCH_SCROLL=1: what one scroll change costs (the visible pages, the current page, the models, the sidebar
// that follows) - with the page sidebar shown and without it.
TEST_F(MainWindowTest, benchScrollCost) {
    if (!qEnvironmentVariableIsSet("XQT_BENCH_SCROLL")) {
        GTEST_SKIP() << "set XQT_BENCH_SCROLL=1";
    }
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->insertPages(11, 0, -1, false, 30);  // 41 pages
    until([&] { return sketches.idle(); }, 30000);
    xqt::CanvasView* view = controller->tabManager().currentView();
    view->setVisibilityDelay(0);  // (measure every change)
    xqt::CanvasMemory::instance().planNow();
    wait(2000);
    for (const bool sidebar: {true, false}) {
        window->setProperty("sidebarShown", sidebar);
        wait(300);
        const quint64 before = view->visibilityUpdates();
        QElapsedTimer t;
        t.start();
        qint64 worst = 0;
        double y = 0;
        for (int i = 0; i < 40; ++i) {  // (as if the scroll bar were dragged)
            y += 700;
            QElapsedTimer step;
            step.start();
            view->getViewController().setScrollPosition(QPointF(0, y));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            worst = std::max(worst, step.elapsed());
        }
        const quint64 updates = view->visibilityUpdates() - before;
        std::cout << (sidebar ? "with the sidebar" : "without the sidebar") << ": " << t.elapsed() << " ms for "
                  << updates << " scroll changes (" << (updates ? t.elapsed() / static_cast<qint64>(updates) : 0)
                  << " ms each, worst step " << worst << " ms)\n";
    }
    sketches.setDelays(400, 1500);
}

// The overview of open documents searches them like the library: extended, with the pages that have hits (tap one
// to open the document there), and by name only.
TEST_F(MainWindowTest, tabOverviewHasTheExtendedAndTheNameSearch) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };

    type("page");
    key(Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1); }));
    const QVariantList pages = tabs->index(1, 0).data(xqt::TabManager::HitPagesRole).toList();
    ASSERT_FALSE(pages.isEmpty()) << "old.xopp has \"Page 2\"";
    EXPECT_EQ(pages.first().toMap().value("page").toInt(), 1);
    EXPECT_FALSE(pages.first().toMap().value("rects").toList().isEmpty()) << "with the places of the hits";

    // Extended: the pages with hits under the document; one of them opens it right there
    click(find<QQuickItem>("overviewExtendedButton"));
    EXPECT_TRUE(overview->property("extendedView").toBool());
    QQuickItem* hitPage = nullptr;
    until([&] { return (hitPage = findItem("overviewHitPage")) != nullptr && hitPage->isVisible(); });
    ASSERT_NE(hitPage, nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(hitPage);
    ASSERT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);
    EXPECT_EQ(controller->pageNumber(), 2) << "at the page with the hit";

    // Names only: "pages" is in the name of the first document only, its text is not searched
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    click(find<QQuickItem>("overviewNamesOnly"));
    EXPECT_TRUE(overview->property("namesOnly").toBool());
    auto* field = find<QQuickItem>("overviewSearchField");
    field->setProperty("text", QStringLiteral("pages"));
    QMetaObject::invokeMethod(overview, "runSearch", Q_ARG(QVariant, QStringLiteral("pages")));
    EXPECT_EQ(tabs->index(1, 0).data(xqt::TabManager::SearchHitsRole).toInt(), 0) << "no text search";
    const auto nameMatches = [&](int row) {
        QVariant matches;
        QMetaObject::invokeMethod(overview, "nameMatches", Q_RETURN_ARG(QVariant, matches),
                                  Q_ARG(QVariant, tabs->index(row, 0).data(xqt::TabManager::TitleRole)));
        return matches.toBool();
    };
    EXPECT_TRUE(nameMatches(0)) << "pages.xopp";
    EXPECT_FALSE(nameMatches(1)) << "old.xopp";
    EXPECT_FALSE(overview->property("extendedView").toBool()) << "no pages to show for names";
}

TEST_F(MainWindowTest, searchShortcutsForAllDocumentsAndTheLibrary) {
    controller->newDocument();
    QObject* overview = find("tabOverview");
    auto* overviewSearch = find<QQuickItem>("overviewSearchField");
    ASSERT_NE(overviewSearch, nullptr);
    key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true)) << "the overview of all documents opens";
    until([&] { return overviewSearch->hasActiveFocus(); });
    EXPECT_TRUE(overviewSearch->hasActiveFocus()) << "with the cursor in its search";

    key(Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_TRUE(waitOpened(overview, false)) << "the library search closes the overview";
    auto* librarySearch = find<QQuickItem>("librarySearchField");
    ASSERT_NE(librarySearch, nullptr);
    until([&] { return librarySearch->hasActiveFocus(); });
    EXPECT_TRUE(controller->homeVisible()) << "the library is shown";
    EXPECT_TRUE(librarySearch->hasActiveFocus()) << "with the cursor in its search";

    // and back into a document the other shortcut works as well
    key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    EXPECT_FALSE(controller->homeVisible());
}

// One undo stack, as in upstream: Ctrl+N adds a page, Ctrl+Z on the page (or the undo button of the pill) takes it
// away again, and a notice says what was undone - the page may be out of view.
TEST_F(MainWindowTest, addingAPageIsUndoneLikeEverythingElse) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int pages = controller->pageCount();
    auto* snackbar = find<QQuickItem>("snackbar");
    auto* snackbarText = findItem("snackbarText");
    ASSERT_NE(snackbar, nullptr);
    ASSERT_NE(snackbarText, nullptr);

    key(Qt::Key_N, Qt::ControlModifier);
    ASSERT_EQ(controller->pageCount(), pages + 1) << "Ctrl+N adds a page";
    EXPECT_TRUE(controller->canUndo()) << "the same undo as for writing";

    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages) << "Ctrl+Z takes it away";
    until([&] { return snackbar->isVisible(); });
    EXPECT_TRUE(snackbar->isVisible());
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith(QStringLiteral("Undone:")))
            << snackbarText->property("text").toString().toStdString();

    key(Qt::Key_Y, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages + 1) << "and redo brings it back";
    until([&] { return snackbarText->property("text").toString().startsWith(QStringLiteral("Redone:")); });
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith(QStringLiteral("Redone:")));

    // The undo button of the pill as well
    click(find<QQuickItem>("undoButton"));
    EXPECT_EQ(controller->pageCount(), pages);
}

// The overview of open documents shows the stored preview of a saved document that is on its title page (nothing to
// draw), else the page it is on. The title page is chosen in the menu of a page.
TEST_F(MainWindowTest, theOverviewShowsTheStoredPreviewOnTheTitlePage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto picture = [&] { return tabs->index(controller->currentTab(), 0).data(xqt::TabManager::ThumbnailRole).toString(); };
    EXPECT_EQ(controller->titlePage(), 0);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on page 1, saved: the stored preview";

    controller->goToPage(2);
    wait(50);
    EXPECT_TRUE(picture().startsWith("image://thumbnail/")) << "on another page: that page";

    // Page 3 becomes the title page through the menu of the page
    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 2);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    auto* item = find<QQuickItem>("titlePageItem");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isVisible());
    click(item);
    EXPECT_EQ(controller->titlePage(), 2);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its title page now";

    controller->setTitlePage(0);  // (kept in the test's own cache, but tidy anyway)
}

// "Open where I left off" (off by default): a document opens at the page it was left at.
TEST_F(MainWindowTest, documentsOpenWhereTheyWereLeftOffIfWanted) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(settings->get("resumeAtLastPage").toBool()) << "off by default";
    const QString file = fixturePath(u8"load/pages.xopp");
    ASSERT_TRUE(controller->openPath(file));
    controller->goToPage(5);
    wait(50);
    controller->closeTab(controller->currentTab());

    ASSERT_TRUE(controller->openPath(file));
    EXPECT_EQ(controller->pageNumber(), 1) << "off: at the first page";
    controller->goToPage(5);
    controller->closeTab(controller->currentTab());

    // The switch on the library
    controller->setHomeVisible(true);
    auto* toggle = find<QQuickItem>("resumeSwitch");
    ASSERT_NE(toggle, nullptr);
    until([&] { return toggle->isVisible(); });
    click(toggle);
    EXPECT_TRUE(settings->get("resumeAtLastPage").toBool());
    ASSERT_TRUE(controller->openPath(file));
    EXPECT_EQ(controller->pageNumber(), 6) << "on: where it was left";
    settings->set("resumeAtLastPage", false);
}

// A PDF opened as it is (no .xopp beside it) gets all of it too: the stored preview in the overview, a title page,
// and it opens again where it was left - also when it was closed without saving.
TEST_F(MainWindowTest, aPdfWithoutXoppHasTitleAndLastPageToo) {
    // (a copy under an ordinary name: "<name>.xopp.bg.pdf" is the attached PDF of a .xopp, not a document)
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    ASSERT_TRUE(QFile::copy(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), pdf));
    ASSERT_TRUE(controller->openPath(pdf));
    wait(80);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto picture = [&] { return tabs->index(controller->currentTab(), 0).data(xqt::TabManager::ThumbnailRole).toString(); };
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its first page: the stored preview";
    EXPECT_FALSE(controller->tabManager().currentSession()->hasFilePath()) << "(no .xopp: a PDF as it is)";
    EXPECT_EQ(controller->titlePage(), 0) << "it has a title page";

    ASSERT_TRUE(controller->setTitlePage(1));
    controller->goToPage(1);
    wait(50);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its (new) title page";

    // Left on the second page; closed without saving; opened again with "Last page" on
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    settings->set("resumeAtLastPage", true);
    controller->closeTab(controller->currentTab());
    ASSERT_TRUE(controller->openPath(pdf));
    EXPECT_EQ(controller->pageNumber(), 2) << "where it was left";
    settings->set("resumeAtLastPage", false);
    controller->setTitlePage(0);
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

// Markdown: written beside the page into a box (a text in the layer "Markdown"), opened again with the text tool.
TEST_F(MainWindowTest, markdownBoxIsWrittenAndOpenedAgainWithTheTextTool) {
    controller->setMarkdownInPanel(true);  // (the text tool opens it beside the page)
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    auto* markdownItem = find<QObject>("markdownItem");  // in the menu of the writing button
    ASSERT_NE(markdownItem, nullptr);
    QMetaObject::invokeMethod(markdownItem, "triggered");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_TRUE(controller->markdownActive());
    auto* area = find<QQuickItem>("markdownArea");
    ASSERT_TRUE(area->hasActiveFocus());

    type("# Notes");
    key(Qt::Key_Return);
    type("- one");
    key(Qt::Key_Return);  // the list goes on
    type("two");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // an empty item ends the list
    type("Some ");
    key(Qt::Key_B, Qt::ControlModifier);
    type("bold");
    wait(300);

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    const std::string source = "# Notes\n- one\n- two\n\nSome **bold**";
    EXPECT_EQ(box->getText(), source);
    EXPECT_TRUE(controller->modified());
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    EXPECT_FALSE(panel->isVisible());
    EXPECT_FALSE(controller->markdownActive());
    EXPECT_TRUE(find<QQuickItem>("textModeButton")->property("markdownMode").toBool()) << "the button's mode now";

    // The text tool on the box opens it again (not the text of the source in a text box)
    controller->selectTool("text");
    wait(200);  // (the zoom from before the panel comes back)
    QSignalSpy requested(controller.get(), &AppController::markdownRequested);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    const auto r = xqt::md::boxRect(*xqt::md::boxOf(*layer));
    view->getViewController().scrollToPageRect(0, QRectF(r.x, r.y, r.width, r.height));
    wait(100);
    const QPointF onBox = view->pageViewRect(0).topLeft() +
                          QPointF(r.x + 20, r.y + r.height / 2) * view->getViewController().zoom();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, canvasItem->mapToScene(onBox).toPoint());
    until([&] { return panel->isVisible(); });
    EXPECT_EQ(requested.count(), 1) << "tool " << controller->tool().toStdString() << " at " << onBox.x() << ","
                                    << onBox.y() << " box " << r.x << "," << r.y << " " << r.width << "x" << r.height;
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(area->property("text").toString().toStdString(), source);
    click(find<QQuickItem>("markdownCancel"));
    EXPECT_EQ(xqt::md::boxOf(*layer)->getText(), source);

    // One undo step for the whole text
    controller->undo();
    EXPECT_EQ(xqt::md::boxOf(*layer), nullptr);
}

// Markdown text boxes: the text tool with "Markdown" places them anywhere; edited on the page (the source is shown
// while editing), drawn formatted.
TEST_F(MainWindowTest, markdownTextBoxesAnywhereWithTheTextTool) {
    controller->setMarkdownInPanel(false);  // (written on the page)
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(200, 300, 200, 300));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(200, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr) << "written on the page";
    type("Some **bold**");
    key(Qt::Key_Escape);
    EXPECT_EQ(view->getMarkdownEditor(), nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxAt(*layer, 205, 300);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), "Some **bold**");
    EXPECT_EQ(box->getFontSize(), 10) << "the size of Markdown text";
    EXPECT_GT(box->getWrap(), 100) << "as wide as there is room";
    EXPECT_NE(page->getSelectedLayer(), layer) << "the pen still writes into its layer";

    // A tap on it edits it on the page again
    QSignalSpy requested(controller.get(), &AppController::markdownRequested);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(210, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    EXPECT_EQ(view->getMarkdownEditor()->text(), "Some **bold**");
    EXPECT_EQ(requested.count(), 0);
    key(Qt::Key_Escape);

    // Markdown off: ordinary texts again
    controller->setTextMarkdown(false);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(200, 500));
    wait(50);
    ASSERT_NE(view->getTextEditor(), nullptr) << "an ordinary text box";
    EXPECT_FALSE(view->getTextEditor()->isMarkdown());
    type("plain");
    key(Qt::Key_Escape);
    EXPECT_EQ(layer->getElements().size(), 1u);
    bool plain = false;
    for (const auto& e: page->getSelectedLayer()->getElements()) {
        plain = plain || (e->getType() == ELEMENT_TEXT && static_cast<const Text*>(e.get())->getText() == "plain");
    }
    EXPECT_TRUE(plain);
}

// Markdown text boxes are selected (rectangle, lasso, tap) and moved like other elements: in their layer
// "Markdown", which is the selected layer only while they are selected.
TEST_F(MainWindowTest, markdownTextBoxesAreSelectedAndMoved) {
    controller->setMarkdownInPanel(false);  // (written on the page)
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 150, 350, 300));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    const auto drag = [&](QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(window, from + (to - from) * i / 10);
            wait(10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(50);
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 200));
    wait(50);
    type("# Moved box");
    key(Qt::Key_Escape);

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    Text* box = xqt::md::boxAt(*layer, 155, 200);
    ASSERT_NE(box, nullptr);
    ASSERT_TRUE(box->isMarkdown());
    const auto before = box->getBoundingBox();
    const Layer* penLayer = page->getSelectedLayer();
    ASSERT_NE(penLayer, layer);

    // A rectangle around it selects it
    controller->selectTool("selectRect");
    drag(pagePoint(before.x - 10, before.y - 10), pagePoint(before.x + before.width + 10, before.y + before.height + 10));
    ASSERT_TRUE(controller->hasSelection());
    EXPECT_EQ(page->getSelectedLayer(), layer) << "while it is selected";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Dragged by (40, 60) points
    const QPoint inside = pagePoint(before.x + before.width / 2, before.y + before.height / 2);
    drag(inside, pagePoint(before.x + before.width / 2 + 40, before.y + before.height / 2 + 60));
    // A tap elsewhere ends the selection (with any tool: it only deselects)
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(420, 430));
    wait(50);
    EXPECT_FALSE(controller->hasSelection());
    EXPECT_EQ(page->getSelectedLayer(), penLayer) << "the pen writes into its layer again";
    ASSERT_EQ(layer->getElements().size(), 1u);
    const auto* moved = static_cast<const Text*>(layer->getElements().front().get());
    EXPECT_TRUE(moved->isMarkdown()) << "still a Markdown text, in its layer";
    EXPECT_NEAR(moved->getBoundingBox().x, before.x + 40, 3);
    EXPECT_NEAR(moved->getBoundingBox().y, before.y + 60, 3);
    EXPECT_NEAR(moved->getBoundingBox().width, before.width, 0.5) << "the drawn box moved as a whole";

    // A lasso around it, then Delete; undo brings it back
    controller->selectTool("selectRegion");
    const auto b = moved->getBoundingBox();
    const QPoint a = pagePoint(b.x - 15, b.y - 15);
    const QPoint c = pagePoint(b.x + b.width + 15, b.y + b.height + 15);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (const QPoint& p: {QPoint(c.x(), a.y()), c, QPoint(a.x(), c.y()), a}) {
        for (int i = 1; i <= 5; ++i) {
            QTest::mouseMove(window, p);
            wait(5);
        }
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a);
    wait(50);
    ASSERT_TRUE(controller->hasSelection());
    controller->deleteSelection();
    EXPECT_TRUE(layer->getElements().empty());
    EXPECT_EQ(page->getSelectedLayer(), penLayer);
    controller->undo();
    EXPECT_EQ(layer->getElements().size(), 1u);
}

// Markdown text boxes written beside the page: the page shows them formatted while typing.
TEST_F(MainWindowTest, markdownTextBoxesAreWrittenBesideThePage) {
    controller->setMarkdownInPanel(true);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 200));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_FALSE(controller->markdownIsPageText()) << "a text box, not the page's text";
    EXPECT_EQ(view->getTextEditor(), nullptr) << "not edited on the page";
    ASSERT_TRUE(area->hasActiveFocus());
    type("## Box");
    key(Qt::Key_Return);
    type("- one");
    wait(300);

    // Live on the page, where it was tapped
    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxAt(*layer, 155, 300);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), "## Box\n- one");
    EXPECT_TRUE(box->isMarkdown());
    EXPECT_NEAR(box->getTransformation().shift.x, 150, 1);  // (where it was tapped, to the pixel)
    EXPECT_EQ(box->getFontSize(), 10);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    EXPECT_FALSE(panel->isVisible());

    // A tap on it opens it again; the page's own text is another one
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(160, 300));
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(area->property("text").toString().toStdString(), "## Box\n- one");
    click(find<QQuickItem>("markdownCancel"));
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, QVariant(0)));
    EXPECT_TRUE(controller->markdownIsPageText());
    EXPECT_EQ(area->property("text").toString(), QString()) << "the page has no text of its own yet";
    click(find<QQuickItem>("markdownCancel"));

    controller->undo();
    EXPECT_EQ(xqt::md::boxAt(*layer, 155, 300), nullptr) << "one undo step for the box";
}

// The page's Markdown text flows onto new pages while it is written.
TEST_F(MainWindowTest, markdownFlowsOntoNewPages) {
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, QVariant(0)));
    ASSERT_TRUE(panel->isVisible());
    std::string text = "# A long text\n\n";
    for (int i = 0; i < 30; ++i) {
        text += "## Part " + std::to_string(i + 1) + "\n\n";
        for (int j = 0; j < 4; ++j) {
            text += "Some sentences of part " + std::to_string(i + 1) +
                    ", long enough to take a few lines on the page, so that the parts need several pages.\n\n";
        }
    }
    area->setProperty("text", QString::fromStdString(text));
    until([&] { return controller->pageCount() > 1; }, 3000);
    const int pages = controller->pageCount();
    EXPECT_GE(pages, 3);
    EXPECT_EQ(controller->markdownPage(), 0);
    EXPECT_EQ(controller->markdownLastPage(), pages - 1);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        controller->setZoomPercent(30);
        wait(2500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    controller->undo();
    EXPECT_EQ(controller->pageCount(), 1) << "one undo step: text and pages";
}

// Markdown written on the page, as in Typora: formatted while typing, the block with the cursor shows its Markdown.
TEST_F(MainWindowTest, markdownIsWrittenOnThePage) {
    controller->setMarkdownInPanel(false);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 250));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    wait(50);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(find<QQuickItem>("markdownPanel")->isVisible()) << "not beside the page";
    type("# Title");
    key(Qt::Key_Return);  // a new paragraph
    type("Some **bold** text");
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** text");

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    Text* box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), editor->text()) << "the page has it while typing";
    EXPECT_TRUE(box->isInEditing()) << "drawn by the editor while it is written";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Keys: the line's start, words, undo in the text being written
    key(Qt::Key_Home);
    EXPECT_EQ(editor->cursorPosition(), 9u) << "the start of the line";
    key(Qt::Key_Right, Qt::ControlModifier);
    EXPECT_EQ(editor->cursorPosition(), 13u) << "after \"Some\"";
    key(Qt::Key_End);
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** ") << "the last word";
    key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** text");

    // A tap on the heading puts the cursor there
    const auto r = xqt::md::boxRect(*box);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(r.x + 1, r.y + 4));
    wait(50);
    EXPECT_EQ(view->getMarkdownEditor(), editor) << "still writing";
    EXPECT_LE(editor->cursorPosition(), 7u) << "in the heading";

    // Bold with Ctrl+B around a selection
    key(Qt::Key_End);
    key(Qt::Key_Left, Qt::ShiftModifier | Qt::ControlModifier);
    key(Qt::Key_B, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), "# **Title**\n\nSome **bold** text");

    // Ctrl+Alt+M: the same text, its source beside the page
    key(Qt::Key_M, Qt::ControlModifier | Qt::AltModifier);
    auto* panel = find<QQuickItem>("markdownPanel");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(view->getMarkdownEditor(), nullptr) << "not on the page any more";
    EXPECT_EQ(find<QQuickItem>("markdownArea")->property("text").toString().toStdString(),
              "# **Title**\n\nSome **bold** text");
    click(find<QQuickItem>("markdownDone"));
    box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_FALSE(box->isInEditing());
    controller->undo();
    EXPECT_EQ(xqt::md::boxOf(*layer), nullptr) << "one undo step for all of it";
}

// The page's text written on the page flows onto the next pages; the cursor goes with it.
TEST_F(MainWindowTest, markdownWrittenOnThePageFlowsOntoPages) {
    controller->setMarkdownInPanel(false);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->startMarkdown(0, true, 100, 100);  // (the writing button's page text, on the page)
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    std::string text;
    for (int i = 0; i < 60; ++i) {
        text += "Paragraph " + std::to_string(i) + " with enough words to take a line or two of the page.\n\n";
    }
    QGuiApplication::clipboard()->setText(QString::fromStdString(text));
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), text);
    ASSERT_GE(controller->pageCount(), 2);
    EXPECT_EQ(view->indexOf(&editor->getPage()), std::optional<size_t>(controller->pageCount() - 1))
            << "the cursor (at the end) is on the last page";
    key(Qt::Key_Home, Qt::ControlModifier);
    EXPECT_EQ(view->indexOf(&editor->getPage()), std::optional<size_t>(0)) << "back on the first page";
    key(Qt::Key_Escape);
    controller->undo();
    EXPECT_EQ(controller->pageCount(), 1);
}

// A tap on a task's check box switches it: with the hand tool (or a finger), and while writing on the page.
TEST_F(MainWindowTest, markdownCheckBoxesAreTapped) {
    controller->setMarkdownInPanel(false);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 250));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    type("- [ ] milk");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // (the list ends)
    type("Shopping");
    key(Qt::Key_Escape);

    auto* session = controller->tabManager().currentSession();
    Layer* layer = xqt::md::markdownLayer(session->getDocument()->getPage(0));
    ASSERT_NE(layer, nullptr);
    const auto box = [&] { return xqt::md::boxOf(*layer); };
    ASSERT_NE(box(), nullptr);
    const std::string before = box()->getText();
    ASSERT_EQ(before, "- [ ] milk\n\nShopping") << "Enter on an empty item: the list ends, a paragraph follows";
    const auto checkBox = [&](size_t active) {
        const auto& l = xqt::md::cachedLayout(box()->getText(), xqt::md::styleOf(*box()), active);
        EXPECT_EQ(l.checkBoxes.size(), 1u) << box()->getText();
        if (l.checkBoxes.empty()) {
            return QPoint();
        }
        const auto& at = box()->getTransformation().shift;
        const auto& b = l.checkBoxes.front();
        return pagePoint(at.x + b.x + b.size / 2, at.y + b.y + b.size / 2);
    };

    // The hand tool: switched, one undo step
    controller->selectTool("hand");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, checkBox(xqt::md::NO_SOURCE));
    wait(50);
    EXPECT_NE(box()->getText().find("- [x] milk"), std::string::npos) << box()->getText();
    controller->undo();
    EXPECT_EQ(box()->getText(), before);

    // While writing on the page (the cursor in the paragraph after the list)
    controller->selectTool("text");
    const auto r = xqt::md::boxRect(*box());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(r.x + r.width - 5, r.y + r.height - 3));
    wait(50);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    const size_t cursor = editor->cursorPosition();
    ASSERT_GT(cursor, before.find("Shopping")) << "in the paragraph";
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, checkBox(cursor));
    wait(50);
    EXPECT_EQ(view->getMarkdownEditor(), editor) << "still writing";
    EXPECT_NE(editor->text().find("- [x] milk"), std::string::npos) << editor->text();
    EXPECT_EQ(editor->cursorPosition(), cursor) << "the cursor stays";
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), before) << "undone in the text being written";
    key(Qt::Key_Escape);
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
