/*
 * xournal-qt M0 spike ("inkpad"): compares a Qt Quick and a QWidget canvas host for pen/touch input on
 * the target device. See qt/docs/adr/0001-ui-host.md for the evaluation checklist.
 *
 *   xqt-inkpad [--host quick|widget] [--log FILE.jsonl | --no-log]
 *
 * @license GNU GPLv2 or later
 */
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <glib.h>

#include "SpikeContext.h"
#include "WidgetHost.h"

int main(int argc, char** argv) {
    // Qt logs to journald when stderr is not a console; the spike's report should go to the terminal/file.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING")) {
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    }
    // Deliver every pen/touch sample (Xournal++ also disables event compression).
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication::setDesktopFileName("xournal-qt-inkpad");
    QApplication app(argc, argv);
    QApplication::setApplicationName("xqt-inkpad");

    QCommandLineParser parser;
    parser.setApplicationDescription("xournal-qt M0 input/canvas spike");
    parser.addHelpOption();
    QCommandLineOption hostOpt("host", "Canvas host: quick (default) or widget.", "host", "quick");
    QCommandLineOption logOpt("log", "Write every input event as JSON lines to FILE.", "file");
    QCommandLineOption noLogOpt("no-log", "Do not write an event log.");
    parser.addOptions({hostOpt, logOpt, noLogOpt});
    parser.process(app);

    const QString host = parser.value(hostOpt);
    if (host != "quick" && host != "widget") {
        qCritical("--host must be 'quick' or 'widget'");
        return 2;
    }
    QString logPath;
    if (!parser.isSet(noLogOpt)) {
        logPath = parser.isSet(logOpt) ? parser.value(logOpt)
                                       : QString("inkpad-%1-%2.jsonl")
                                                 .arg(host, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    }

    SpikeContext ctx(host, logPath);
    qInfo("%s", qPrintable(ctx.environment()));
    if (ctx.log()->isOpen()) {
        qInfo("event log: %s", qPrintable(ctx.log()->path()));
    }
    // Verify that the Qt event loop dispatches GLib sources (Xournal++ core relies on g_idle_add/g_timeout_add).
    g_idle_add(
            +[](gpointer) -> gboolean {
                qInfo("GLib idle source fired: the Qt event loop dispatches GLib sources");
                return G_SOURCE_REMOVE;
            },
            nullptr);

    if (host == "widget") {
        WidgetHostWindow window(&ctx);
        window.show();
        return app.exec();
    }

    QQuickStyle::setStyle("Fusion");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("spike", &ctx);
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("Inkpad", "Main");
    return app.exec();
}
