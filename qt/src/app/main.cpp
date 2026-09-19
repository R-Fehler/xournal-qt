/*
 * xournal-qt: application entry point.
 *
 *   xournal-qt [FILE.xopp | FILE.xoj | FILE.pdf]
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCommandLineParser>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "AppController.h"
#include "DocumentCanvasItem.h"
#include "session/AppContext.h"

int main(int argc, char* argv[]) {
    // Deliver every pen/touch sample (upstream Xournal++ also disables event compression).
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication::setDesktopFileName("xournal-qt");
    QGuiApplication::setApplicationName("xournal-qt");
    QGuiApplication::setApplicationDisplayName("Xournal Qt");
    // A QApplication (not only QGuiApplication): the platform theme (e.g. KDE Plasma) then provides its native,
    // resizable file dialogs for QtQuick.Dialogs instead of Qt's built-in QML fallback.
    QApplication qapp(argc, argv);
    // Like upstream initCAndCoutLocales(): numbers in the C locale for cairo, PDF export and the file format.
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();

    QCommandLineParser parser;
    parser.setApplicationDescription("Xournal Qt - note taking and PDF annotation");
    parser.addHelpOption();
    parser.addPositionalArgument("file", "Document to open (.xopp, .xoj or .pdf)");
    parser.process(qapp);

    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
    AppController controller;
    if (!parser.positionalArguments().isEmpty()) {
        controller.openPath(parser.positionalArguments().first());
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &qapp, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("XournalQt", "Main");

    // Developer aid: XQT_SCREENSHOT=file.png renders the window after a moment, saves it and quits.
    if (const auto shot = qEnvironmentVariable("XQT_SCREENSHOT"); !shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("XQT_SCREENSHOT_DELAY_MS") > 0
                                   ? qEnvironmentVariableIntValue("XQT_SCREENSHOT_DELAY_MS")
                                   : 1500,
                           [&engine, shot] {
                               if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) {
                                   w->grabWindow().save(shot);
                               }
                               QCoreApplication::quit();
                           });
    }
    const int rc = QApplication::exec();
    controller.shutdown();
    return rc;
}
