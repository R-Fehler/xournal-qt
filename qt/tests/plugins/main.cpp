/*
 * xournal-qt: test runner of the plugin host (headless: a QCoreApplication, the Markdown renderer installed so
 * boxes are measured as drawn).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/AppContext.h"

#include "MdBox.h"

int main(int argc, char* argv[]) {
    QTemporaryDir home;
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    QCoreApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    xqt::md::installRenderer();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
