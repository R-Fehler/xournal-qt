/*
 * xournal-qt: Esc and Android's back key in the real window (qt/docs/features/zen.md, "Esc and Back"): with more than
 * one thing to leave (full screen with a selected note, an armed snip, the to-do stamp, the replay; presenting
 * likewise), one press does the first of them in a fixed order, the next press the next one. And the keys a button
 * names are the keys set in the shortcuts (a rebound key shows in the tips).
 *
 * @license GNU GPLv2 or later
 */
#include <filesystem>

#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "shell/CanvasActions.h"
#include "shell/ShortcutsModel.h"

#include "AppController.h"
#include "support/TestSupport.h"
#include "UiFixture.h"

namespace {
class EscapeKeysTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        makeController();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1280, 900)}));
        // (a copy: the fixtures are read only)
        const QString copy = tmp.filePath("strokes.xopp");
        ASSERT_TRUE(QFile::copy(xqt::test::fixturePath(u8"load/strokes.xopp"), copy));
        ASSERT_TRUE(controller->openPath(copy));
        until([&] { return !controller->property("homeVisible").toBool(); });
    }
    void TearDown() override {
        shortcuts()->resetAll();  // (the tests share the config folder)
        closeApp();
    }
    xqt::ShortcutsModel* shortcuts() const {
        return qobject_cast<xqt::ShortcutsModel*>(controller->property("shortcuts").value<QObject*>());
    }
    bool flag(const char* name) const { return window->property(name).toBool(); }
    void fullScreen() {
        window->setProperty("fullScreenMode", true);
        ASSERT_TRUE(until([&] { return window->visibility() == QWindow::FullScreen; }));
    }
    QObject* timeline() const { return controller->property("timeline").value<QObject*>(); }
    bool replaying() const { return timeline()->property("active").toBool(); }

    QTemporaryDir tmp;
};
/// The same order for Esc and Android's back key (the author, 2026-10-07: "I want a consistent android back
/// behavior"): every step of the list is taken by either key
class EscapeOrBackTest: public EscapeKeysTest, public ::testing::WithParamInterface<int> {
protected:
    void press() { key(static_cast<Qt::Key>(GetParam())); }
};
}  // namespace

// Full screen with a selected sticky note: Esc unselects the note, the next Esc leaves full screen (both wanted the key:
// Qt called it ambiguous and neither acted)
TEST_P(EscapeOrBackTest, fullScreenWithASelectedNote) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    ASSERT_TRUE(controller->insertStickyNote());
    ASSERT_TRUE(until([&] { return controller->edit().noteSelected(); }));
    press();
    EXPECT_FALSE(controller->edit().noteSelected()) << "the note first";
    EXPECT_TRUE(flag("fullScreenMode")) << "full screen stays";
    press();
    EXPECT_FALSE(flag("fullScreenMode")) << "then full screen";
}

// Full screen with an armed snip: Esc puts the snip away first
TEST_P(EscapeOrBackTest, fullScreenWithAnArmedSnip) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    controller->startSnip("rect");
    ASSERT_EQ(controller->snipShape(), "rect");
    press();
    EXPECT_EQ(controller->snipShape(), "") << "the snip first";
    EXPECT_TRUE(flag("fullScreenMode"));
    press();
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Full screen with the to-do stamp armed: Esc puts the stamp away first
TEST_P(EscapeOrBackTest, fullScreenWithTheToDoStamp) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    controller->startTodoStamp();
    ASSERT_TRUE(controller->todoStampArmed());
    press();
    EXPECT_FALSE(controller->todoStampArmed()) << "the stamp first";
    EXPECT_TRUE(flag("fullScreenMode"));
    press();
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Full screen during the replay (its button is on the default top bar, which full screen's ⋯ lists): Esc ends the
// replay first
TEST_P(EscapeOrBackTest, fullScreenDuringTheReplay) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    QMetaObject::invokeMethod(timeline(), "start");
    ASSERT_TRUE(replaying());
    press();
    EXPECT_FALSE(replaying()) << "the replay first";
    EXPECT_TRUE(flag("fullScreenMode"));
    press();
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Presenting with a selected note, an armed snip or the stamp: Esc puts that away first, presenting goes on; the next
// Esc ends presenting
TEST_P(EscapeOrBackTest, presentingWithASelectionASnipOrTheStamp) {
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    ASSERT_TRUE(until([&] { return controller->presenting(); }));
    ASSERT_TRUE(controller->insertStickyNote());
    ASSERT_TRUE(until([&] { return controller->edit().noteSelected(); }));
    press();
    EXPECT_FALSE(controller->edit().noteSelected()) << "the note first";
    EXPECT_TRUE(controller->presenting());

    controller->startSnip("lasso");
    ASSERT_EQ(controller->snipShape(), "lasso");
    press();
    EXPECT_EQ(controller->snipShape(), "") << "the snip first";
    EXPECT_TRUE(controller->presenting());

    controller->startTodoStamp();
    ASSERT_TRUE(controller->todoStampArmed());
    press();
    EXPECT_FALSE(controller->todoStampArmed()) << "the stamp first";
    EXPECT_TRUE(controller->presenting());

    press();
    EXPECT_FALSE(controller->presenting()) << "then presenting ends";
}

// Zen in full screen: Esc leaves Zen, the next one full screen (the order: Zen before full screen)
TEST_P(EscapeOrBackTest, zenThenFullScreen) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    ASSERT_TRUE(until([&] { return flag("zen"); }));
    press();
    EXPECT_FALSE(flag("zen"));
    EXPECT_TRUE(flag("fullScreenMode"));
    press();
    EXPECT_FALSE(flag("fullScreenMode"));
}

INSTANTIATE_TEST_SUITE_P(Keys, EscapeOrBackTest, ::testing::Values(int(Qt::Key_Escape), int(Qt::Key_Back)),
                         [](const ::testing::TestParamInfo<int>& info) {
                             return info.param == Qt::Key_Back ? std::string("Back") : std::string("Escape");
                         });

// Android's back key with nothing left to step out of asks before the app is left (the author: "leaving the app should
// only happen after confirming it in a small dialog on android"): Stay keeps it, Leave closes the window; Esc never
// asks
TEST_F(EscapeKeysTest, backWithNothingLeftAsksBeforeLeaving) {
    auto* shortcuts = window->findChild<QObject*>("windowShortcuts");
    ASSERT_NE(shortcuts, nullptr);
    shortcuts->setProperty("confirmLeave", true);  // (on Android by itself)
    auto* dialog = window->findChild<QObject*>("leaveAppDialog");
    ASSERT_NE(dialog, nullptr);
    key(Qt::Key_Escape);
    wait(100);
    EXPECT_FALSE(dialog->property("visible").toBool()) << "Esc does not ask";
    key(Qt::Key_Back);
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); })) << "Back asks";
    EXPECT_TRUE(window->isVisible());
    QMetaObject::invokeMethod(dialog, "reject");
    ASSERT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_TRUE(window->isVisible()) << "Stay keeps the app";
    // Back while it asks closes the question (it is in front), the app stays
    key(Qt::Key_Back);
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); }));
    key(Qt::Key_Back);
    ASSERT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_TRUE(window->isVisible());
    // Leave: the window closes as ⋮ → Quit does
    key(Qt::Key_Back);
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "accept");
    EXPECT_TRUE(until([&] { return !window->isVisible(); })) << "Leave closes the window";
}

// Recording has a key of the shortcuts (Ctrl+Shift+R): listed, and it can be changed
TEST_F(EscapeKeysTest, recordingIsAShortcutThatCanBeChanged) {
    EXPECT_EQ(shortcuts()->keys("record"), QStringList{"Ctrl+Shift+R"});
    ASSERT_TRUE(shortcuts()->setKeys("record", "Ctrl+Alt+Y"));
    EXPECT_EQ(shortcuts()->keys("record"), QStringList{"Ctrl+Alt+Y"});
    auto* button = find<QQuickItem>("recordButton");
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->property("tip").toString().contains("Ctrl+Alt+Y")) << button->property("tip").toString().toStdString();
}

// A button's tip names the keys as they are set: rebound, the tip says the new keys
TEST_F(EscapeKeysTest, tipsNameTheKeysAsTheyAreSet) {
    auto* button = find<QQuickItem>("fullScreenButton");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->property("tip").toString(), "Full screen (F11)");
    ASSERT_TRUE(shortcuts()->setKeys("fullScreen", "F10"));
    EXPECT_EQ(button->property("tip").toString(), "Full screen (F10)");
}
