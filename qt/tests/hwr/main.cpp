/*
 * xournal-qt: test runner of the handwriting search (label hwr). Off-screen, with temporary config and cache folders.
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    QTemporaryDir home;
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    QCoreApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
