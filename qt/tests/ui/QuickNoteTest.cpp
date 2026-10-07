/*
 * xournal-qt: Quick note in the real window (qt/docs/quick-note.md): the home screen's button (a wide window) or its
 * entry in "+" (narrower), Ctrl+Alt+N and ⋮ make a new note in the library's Inbox and show it; with the setting
 * "daily" a line "- HH:MM " goes into today's Markdown note, opened with the cursor at its end.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "canvas/CanvasView.h"
#include "session/AppContext.h"
#include "canvas/MarkdownEditor.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"

namespace fs = std::filesystem;

namespace {
class QuickNoteUiTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        xqt::DocumentMode::store(*controller->context().getSettings(), xqt::DocumentMode::Mode::Xopp);
        controller->setLibraryRoot(root);
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));
        wait(100);
    }
    void TearDown() override {
        settings()->set("quickNote", "note");  // (the tests share the config folder)
        xqt::DocumentMode::store(*controller->context().getSettings(), xqt::DocumentMode::Mode::Unset);
        closeApp();
    }

    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }
    fs::path currentFile() const {
        xqt::DocumentSession* s = controller->tabManager().currentSession();
        return s ? s->getFilePath() : fs::path();
    }
    /// The current tab is a quick note made just now: Inbox/<yyyy-MM-dd HH-mm>.xopp
    void expectQuickNote() const {
        const fs::path file = currentFile();
        EXPECT_EQ(file.parent_path(), root / "Inbox");
        EXPECT_TRUE(QRegularExpression("^\\d{4}-\\d\\d-\\d\\d \\d\\d-\\d\\d( \\(\\d+\\))?\\.xopp$")
                            .match(QString::fromStdString(file.filename().string()))
                            .hasMatch())
                << file.filename().string();
        EXPECT_TRUE(fs::is_regular_file(file));
        EXPECT_FALSE(controller->homeVisible());
    }

    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

// A wide window: a button of its own beside New on the home screen.
TEST_F(QuickNoteUiTest, theHomeScreensButton) {
    ASSERT_TRUE(controller->homeVisible());
    QQuickItem* button = findItem("quickNoteButton");
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisible()) << "a button of its own where the header has room for all";
    click(button);
    until([&] { return controller->tabCount() == 1; });
    ASSERT_EQ(controller->tabCount(), 1);
    expectQuickNote();
    EXPECT_EQ(controller->tool(), "pen");
}

// Narrower: the first entry of "+" (the header's ladder: no action in two places).
TEST_F(QuickNoteUiTest, theNewMenuWhenGrouped) {
    window->resize(900, 800);
    wait(200);
    QQuickItem* button = findItem("quickNoteButton");
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isVisible());
    click(findItem("newDocumentButton"));
    QObject* menu = window->findChild<QObject*>("newMenu");
    ASSERT_NE(menu, nullptr);
    until([&] { return menu->property("opened").toBool(); });
    QQuickItem* item = findItem("quickNoteItem");
    ASSERT_NE(item, nullptr);
    until([&] { return item->isVisible() && item->height() > 0; });
    click(item);
    until([&] { return controller->tabCount() == 1; });
    ASSERT_EQ(controller->tabCount(), 1);
    expectQuickNote();
}

// Ctrl+Alt+N (in the shortcut sheet's list), from the home screen and from a document; ⋮ → Document has it too.
TEST_F(QuickNoteUiTest, theShortcutAndTheMoreMenu) {
    EXPECT_EQ(controller->shortcutsModel()->property("revision").isValid(), true);
    QStringList keys;
    QMetaObject::invokeMethod(controller->shortcutsModel(), "keys", Q_RETURN_ARG(QStringList, keys),
                              Q_ARG(QString, "quickNote"));
    EXPECT_EQ(keys, QStringList{"Ctrl+Alt+N"});
    QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return controller->tabCount() == 1; });
    ASSERT_EQ(controller->tabCount(), 1);
    expectQuickNote();

    QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return controller->tabCount() == 2; });
    ASSERT_EQ(controller->tabCount(), 2);
    expectQuickNote();

    // ⋮ → Document → Quick note
    QObject* more = window->findChild<QObject*>("moreMenu");
    ASSERT_NE(more, nullptr);
    QMetaObject::invokeMethod(window->findChild<QObject*>("moreButton"), "clicked");
    until([&] { return more->property("opened").toBool(); });
    QQuickItem* item = window->findChild<QQuickItem*>("documentQuickNoteItem");
    ASSERT_NE(item, nullptr);
    auto* submenu = item->property("menu").value<QObject*>();
    ASSERT_NE(submenu, nullptr);
    EXPECT_EQ(submenu->objectName(), "moreDocumentMenu");
    for (int i = 0; i < more->property("count").toInt(); ++i) {
        QQuickItem* entry = nullptr;
        QMetaObject::invokeMethod(more, "itemAt", Q_RETURN_ARG(QQuickItem*, entry), Q_ARG(int, i));
        if (entry && entry->property("subMenu").value<QObject*>() == submenu) {
            click(entry);
            break;
        }
    }
    until([&] { return submenu->property("opened").toBool() && item->isVisible(); });
    ASSERT_TRUE(item->isVisible());
    EXPECT_EQ(item->property("text").toString(), "Quick note (Ctrl+Alt+N)");
    click(item);
    until([&] { return controller->tabCount() == 3; });
    EXPECT_EQ(controller->tabCount(), 3);
    expectQuickNote();
}

// The setting "daily": a line "- 21:30 " in Inbox/2026-10-04.md (made on first use), opened with the cursor at its
// end; again while it is open, the line goes into the open text (one tab, nothing written behind its back).
TEST_F(QuickNoteUiTest, aLineInTodaysMarkdownNote) {
    ASSERT_TRUE(settings()->set("quickNote", "daily"));
    const QDate day(2026, 10, 4);
    ASSERT_TRUE(controller->quickNoteAt(QDateTime(day, QTime(21, 30))));
    const fs::path md = root / "Inbox" / "2026-10-04.md";
    {
        QFile f(QString::fromStdString(md.string()));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        EXPECT_EQ(f.readAll().toStdString(), "- 21:30 ");
    }
    ASSERT_EQ(controller->tabCount(), 1);
    EXPECT_EQ(controller->tabManager().indexOfFile(md), controller->currentTab());
    EXPECT_EQ(controller->textDocument(), "markdown");
    xqt::CanvasView* view = controller->tabManager().currentView();
    ASSERT_NE(view, nullptr);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr) << "the cursor is in the text";
    EXPECT_EQ(editor->text(), "- 21:30 ");
    EXPECT_EQ(editor->cursorPosition(), editor->text().size());
    wait(100);

    // Typed after the time
    for (const char c: std::string("tea")) {
        QTest::keyClick(window, c);
    }
    wait(50);
    EXPECT_EQ(view->getMarkdownEditor()->text(), "- 21:30 tea");

    // Again, while it is open: into the open text, the cursor after the new line
    ASSERT_TRUE(controller->quickNoteAt(QDateTime(day, QTime(21, 45))));
    EXPECT_EQ(controller->tabCount(), 1);
    editor = controller->tabManager().currentView()->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->text(), "- 21:30 tea\n- 21:45 ");
    EXPECT_EQ(editor->cursorPosition(), editor->text().size());
}
