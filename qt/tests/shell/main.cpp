/*
 * xournal-qt: test runner for the application shell (tabs, single instance, controller).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QGuiApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The controller uses the default config/cache folders: keep them out of the user's home.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());  // (the app-wide stickers, qt/docs/stickers.md)
    qputenv("XQT_RESOURCE_DIR", XQT_BUILD_RESOURCE_DIR);
    // The tools of before (the classic tool bar; qt/docs/toolbox.md): the tests of the toolbox choose it themselves
    // (setting toolbarMode), the others keep the tool state and the bar they were written for
    qputenv("XQT_TOOLBAR_MODE", "classic");
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
