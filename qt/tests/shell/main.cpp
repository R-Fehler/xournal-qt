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
#include "shell/ImageWorkers.h"

namespace {
/// A test that shut the application down (AppController::shutdown) stopped the image workers: the next one gets them
/// again (when the tests run in one process).
class ReopenImageWorkers final: public ::testing::EmptyTestEventListener {
    void OnTestStart(const ::testing::TestInfo& /*info*/) override { xqt::ImageWorkers::reopen(); }
};
}  // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The controller uses the default config/cache folders: keep them out of the user's home.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    // (the app-wide stickers, qt/docs/features/stickers.md)
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XQT_RESOURCE_DIR", XQT_BUILD_RESOURCE_DIR);
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new ReopenImageWorkers);
    return RUN_ALL_TESTS();
}
