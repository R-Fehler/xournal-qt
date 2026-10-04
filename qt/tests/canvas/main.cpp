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
    // The tools of before (the classic tool bar; qt/docs/toolbox.md): the tests of the toolbox choose it themselves
    // (setting toolbarMode), the others keep the tool state and the bar they were written for
    qputenv("XQT_TOOLBAR_MODE", "classic");
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
