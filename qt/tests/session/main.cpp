/*
 * xournal-qt: test runner for the Qt-side session libraries (needs a QCoreApplication for timers and queued calls).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
