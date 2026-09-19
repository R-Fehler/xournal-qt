/*
 * xournal-qt: test runner for the Qt Quick canvas item (offscreen window, real event delivery).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QGuiApplication>
#include <QQuickStyle>
#include <gtest/gtest.h>
#include <qpa/qwindowsysteminterface_p.h>

#include "DocumentCanvasItem.h"
#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
    // Like the Wayland and xcb platforms (unlike offscreen): Qt itself turns unhandled tablet events into mouse
    // events, which is how the pen operates QML controls.
    QWindowSystemInterfacePrivate::TabletEvent::setPlatformSynthesizesMouse(false);
    xqt::AppContext::installQtUiThreadDispatcher();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
