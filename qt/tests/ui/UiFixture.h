/*
 * xournal-qt: the fixture of the UI tests: the real window (Main.qml with an AppController, the engine set up as the
 * app sets it up: src/app/EngineSetup.h), off-screen, and the helpers that drive it (find by objectName, click, keys,
 * wait for a state). qt/docs/testing/README.md, "Writing a UI test".
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <source_location>

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QString>
#include <gtest/gtest.h>

#include "AppController.h"

namespace xqt::test {

/// How the fixture's window is loaded.
struct WindowOptions {
    QSize size;             ///< resized to this before it is shown (when valid)
    bool activate = false;  ///< asks for the focus (requestActivate) before it is shown
};

class UiFixture: public ::testing::Test {
protected:
    /// By default a fresh controller (on the home screen) and its window. A test fixture that prepares the controller
    /// before the window is loaded overrides SetUp: makeController(), its preparation, loadWindow().
    void SetUp() override;
    void TearDown() override;

    void makeController();
    /// Loads Main.qml for `controller` (made first if there is none) and waits until it is shown; the pointer then
    /// rests outside the window (nothing hovered, no tool tips). Use with ASSERT_NO_FATAL_FAILURE.
    void loadWindow(const WindowOptions& options = {});
    /// The engine and the controller gone: first the image workers, then the engine that owns their providers.
    void closeApp();

    /// Runs the event loop for `ms`: a fixed wait, for where no state says that something is done.
    /// (XQT_WAIT_LOG=<file>: each fixed wait appends "<file>:<line> <ms>" to it, to find where a run waits)
    static void wait(int ms, std::source_location where = std::source_location::current());
    /// Runs the event loop until `done` is true; a timeout FAILS the test (at the caller's line). `ms` 0: untilMs.
    /// (returns as soon as it is true: the time is for a machine slowed down by other work)
    bool until(const std::function<bool()>& done, int ms = 0,
               std::source_location where = std::source_location::current()) const;
    /// The same without failing, for a state that may or may not come.
    static bool upTo(const std::function<bool()>& done, int ms);
    /// Waits until the popup is fully open (or closed); false on a timeout (the caller asserts).
    static bool waitOpened(QObject* popup, bool opened, int timeoutMs = 5000);
    /// Until the window drew its next frame: layouts changed meanwhile are done, the items are where they are shown.
    void nextFrame();

    /// An object of the window by its objectName (QObject children: not the delegates a Repeater makes).
    template <typename T = QObject>
    T* find(const QString& name) const {
        return window ? window->findChild<T*>(name) : nullptr;
    }
    /// An item by its objectName through the tree of items under the window's content (also a Repeater's delegates);
    /// `shown`: only a visible one.
    QQuickItem* findItem(const QString& name, bool shown = false) const;
    /// The same over the whole scene: also the popups (their overlay is beside the content).
    QQuickItem* findInScene(const QString& name, bool shown = false) const;
    /// An item under `root` by its objectName (nullptr for no root).
    static QQuickItem* findUnder(QQuickItem* root, const QString& name, bool shown = false);
    /// An entry of a menu by its name (entries made by a Repeater are not found through the objects' parents).
    static QObject* entryOf(QObject* menu, const QString& name);

    /// A click in the middle of the item (it must be there).
    void click(QQuickItem* item, Qt::KeyboardModifiers m = Qt::NoModifier, Qt::MouseButton button = Qt::LeftButton);
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier);
    /// Types text into the focused item (QTest::keyClicks is for widgets only).
    void type(const char* text);
    /// Scrolls the flickable (a ScrollView's) that holds `item` so that the item is shown (to be clicked).
    void scrollIntoView(QQuickItem* item);

    /// The time `until` waits by default.
    int untilMs = 5000;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};

}  // namespace xqt::test
