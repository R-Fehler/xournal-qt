/*
 * xournal-qt: Esc and Android's back key in the real window (qt/docs/zen.md, "Esc and Back"): with more than one
 * thing to leave (full screen with a selected note, an armed snip, the to-do stamp, the replay; presenting likewise),
 * one press does the first of them in a fixed order, the next press the next one.
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
        closeApp();
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
}  // namespace

// Full screen with a selected sticky note: Esc unselects the note, the next Esc leaves full screen (both wanted the key:
// Qt called it ambiguous and neither acted)
TEST_F(EscapeKeysTest, fullScreenWithASelectedNote) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    ASSERT_TRUE(controller->insertStickyNote());
    ASSERT_TRUE(until([&] { return controller->noteSelected(); }));
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->noteSelected()) << "the note first";
    EXPECT_TRUE(flag("fullScreenMode")) << "full screen stays";
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("fullScreenMode")) << "then full screen";
}

// Full screen with an armed snip: Esc puts the snip away first
TEST_F(EscapeKeysTest, fullScreenWithAnArmedSnip) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    controller->startSnip("rect");
    ASSERT_EQ(controller->snipShape(), "rect");
    key(Qt::Key_Escape);
    EXPECT_EQ(controller->snipShape(), "") << "the snip first";
    EXPECT_TRUE(flag("fullScreenMode"));
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Full screen with the to-do stamp armed: Esc puts the stamp away first
TEST_F(EscapeKeysTest, fullScreenWithTheToDoStamp) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    controller->startTodoStamp();
    ASSERT_TRUE(controller->todoStampArmed());
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->todoStampArmed()) << "the stamp first";
    EXPECT_TRUE(flag("fullScreenMode"));
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Full screen during the replay (its button is on the default top bar, which full screen's ⋯ lists): Esc ends the
// replay first
TEST_F(EscapeKeysTest, fullScreenDuringTheReplay) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    QMetaObject::invokeMethod(timeline(), "start");
    ASSERT_TRUE(replaying());
    key(Qt::Key_Escape);
    EXPECT_FALSE(replaying()) << "the replay first";
    EXPECT_TRUE(flag("fullScreenMode"));
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("fullScreenMode"));
}

// Presenting with a selected note, an armed snip or the stamp: Esc puts that away first, presenting goes on; the next
// Esc ends presenting
TEST_F(EscapeKeysTest, presentingWithASelectionASnipOrTheStamp) {
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    ASSERT_TRUE(until([&] { return controller->presenting(); }));
    ASSERT_TRUE(controller->insertStickyNote());
    ASSERT_TRUE(until([&] { return controller->noteSelected(); }));
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->noteSelected()) << "the note first";
    EXPECT_TRUE(controller->presenting());

    controller->startSnip("lasso");
    ASSERT_EQ(controller->snipShape(), "lasso");
    key(Qt::Key_Escape);
    EXPECT_EQ(controller->snipShape(), "") << "the snip first";
    EXPECT_TRUE(controller->presenting());

    controller->startTodoStamp();
    ASSERT_TRUE(controller->todoStampArmed());
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->todoStampArmed()) << "the stamp first";
    EXPECT_TRUE(controller->presenting());

    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting()) << "then presenting ends";
}

// Zen in full screen: Esc leaves Zen, the next one full screen (the order: Zen before full screen)
TEST_F(EscapeKeysTest, zenThenFullScreen) {
    ASSERT_NO_FATAL_FAILURE(fullScreen());
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    ASSERT_TRUE(until([&] { return flag("zen"); }));
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("zen"));
    EXPECT_TRUE(flag("fullScreenMode"));
    key(Qt::Key_Escape);
    EXPECT_FALSE(flag("fullScreenMode"));
}
