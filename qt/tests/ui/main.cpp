/*
 * xournal-qt: test runner for the real main window (Main.qml with an AppController), off-screen.
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QGuiApplication>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <gtest/gtest.h>

#include "DocumentCanvasItem.h"
#include "session/AppContext.h"

Q_IMPORT_QML_PLUGIN(XournalQtPlugin)

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The controller uses the default config/cache folders: keep them out of the user's home.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("XQT_RESOURCE_DIR", XQT_BUILD_RESOURCE_DIR);
    // The first start asks how to keep documents (DocumentMode.h): the tests work with Xournal++ files unless they set
    // another mode, and the question stays away (the tests of the question unset this)
    qputenv("XQT_DOCUMENT_MODE", "xopp");
    // No hover on the controls: the pointer rests where a test last clicked, and a button that appears under it
    // opened its tool tip 600 ms later, over whatever the test clicked next (a test slowed down by load missed it).
    qputenv("QT_QUICK_CONTROLS_HOVER_ENABLED", "0");
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();
    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
