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
    // (or offscreen with options: FractionalScale.ui@150 gives it a bigger screen, qt/tests/ui/offscreen-hidpi.json)
    if (!qEnvironmentVariable("QT_QPA_PLATFORM").startsWith("offscreen")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // The controller uses the default config/cache folders: keep them out of the user's home.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());  // (the tutorial's copy, qt/docs/onboarding.md)
    qputenv("XQT_RESOURCE_DIR", XQT_BUILD_RESOURCE_DIR);
    // The first start asks how to keep documents (DocumentMode.h): the tests work with Xournal++ files unless they set
    // another mode, and the question stays away (the tests of the question unset this)
    qputenv("XQT_DOCUMENT_MODE", "xopp");
    // No hover on the controls: the pointer rests where a test last clicked, and a button that appears under it
    // opened its tool tip 600 ms later, over whatever the test clicked next (a test slowed down by load missed it).
    // The tools of before (the classic tool bar; qt/docs/toolbox.md): the tests of the toolbox choose it themselves
    // (setting toolbarMode), the others keep the tool state and the bar they were written for
    qputenv("XQT_TOOLBAR_MODE", "classic");
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
