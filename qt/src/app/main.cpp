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
#include <QtQml/qqmlextensionplugin.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <QDir>
#include <QFileInfo>

#include "AppController.h"
#include "shell/SessionRecovery.h"
#include "shell/SingleInstance.h"
#include "shell/Thumbnails.h"
#include "DocumentCanvasItem.h"
#include "session/AppContext.h"

Q_IMPORT_QML_PLUGIN(XournalQtPlugin)

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

    QStringList files;
    for (const QString& arg: parser.positionalArguments()) {
        files << QFileInfo(arg).absoluteFilePath();
    }
    // One instance per user: a second start hands its files to the running window (as tabs) and exits.
    // Off-screen runs (tests, screenshots) are always independent.
    xqt::SingleInstance instance;
    const bool independent = qEnvironmentVariableIsSet("XQT_NO_SINGLE_INSTANCE") ||
                             qEnvironmentVariableIsSet("XQT_SCREENSHOT") ||
                             QGuiApplication::platformName() == "offscreen";
    if (!independent) {
        if (instance.sendToRunningInstance(files)) {
            return 0;
        }
        instance.listen();
    }

    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
    AppController controller;
    // Crash recovery and reopening the last tabs; not for off-screen runs (tests, screenshots).
    if (independent && QGuiApplication::platformName() == "offscreen") {
        for (const QString& f: files) {
            controller.openPath(f);
        }
    } else {
        xqt::SessionRecovery::installCrashHandlers();
        controller.startSession(files);
    }
    QObject::connect(&instance, &xqt::SingleInstance::filesRequested, &controller, &AppController::openPaths);

    QQmlApplicationEngine engine;
    engine.addImageProvider("thumbnail", new xqt::ThumbnailProvider);  // the engine takes ownership
    engine.rootContext()->setContextProperty("app", &controller);
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &qapp, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("XournalQt", "Main");

    // Developer aid: XQT_SCREENSHOT=file.png renders the window after a moment, saves it and quits.
    // XQT_SCREENSHOT_POPUP=<objectName> opens that popup first (e.g. settingsPage, tabOverview).
    if (const auto shot = qEnvironmentVariable("XQT_SCREENSHOT"); !shot.isEmpty()) {
        // XQT_SCREENSHOT_ACTION=<method> calls an AppController method without arguments (e.g. selectAllOnPage).
        if (const auto action = qEnvironmentVariable("XQT_SCREENSHOT_ACTION"); !action.isEmpty()) {
            QTimer::singleShot(400, &controller, [&controller, action] {
                QMetaObject::invokeMethod(&controller, action.toLatin1().constData());
            });
        }
        // XQT_SCREENSHOT_SELECT=1,3,4 selects pages (sidebar, page grid).
        if (const auto sel = qEnvironmentVariable("XQT_SCREENSHOT_SELECT"); !sel.isEmpty()) {
            QTimer::singleShot(200, &controller, [&controller, sel] {
                QList<int> pages;
                for (const QString& p: sel.split(',')) {
                    pages << p.toInt();
                }
                QMetaObject::invokeMethod(controller.pagesModel(), "selectPages", Q_ARG(QList<int>, pages));
            });
        }
        // XQT_SCREENSHOT_SEARCH=<text> searches all tabs (and shows the hits of the current one).
        if (const auto query = qEnvironmentVariable("XQT_SCREENSHOT_SEARCH"); !query.isEmpty()) {
            QTimer::singleShot(200, &controller, [&controller, query] {
                controller.searchAllTabs(query);
                controller.openSearchResult(controller.currentTab());
            });
        }
        if (const auto popup = qEnvironmentVariable("XQT_SCREENSHOT_POPUP"); !popup.isEmpty()) {
            QTimer::singleShot(300, [&engine, popup] {
                if (auto* w = engine.rootObjects().value(0)) {
                    if (QObject* p = w->findChild<QObject*>(popup)) {
                        QMetaObject::invokeMethod(p, "open");
                    }
                    if (QObject* field = w->findChild<QObject*>("overviewSearchField")) {
                        field->setProperty("text", qEnvironmentVariable("XQT_SCREENSHOT_SEARCH"));
                    }
                }
            });
        }
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
