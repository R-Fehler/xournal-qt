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
#include <gtest/gtest.h>

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
