/*
 * xournal-qt: test runner for the canvas (needs a QGuiApplication: pointing devices, images).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QGuiApplication>
#include <gtest/gtest.h>

#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
