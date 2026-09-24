/*
 * xournal-qt: application entry point.
 *
 *   xournal-qt [FOLDER] [FILE.xopp | FILE.xoj | FILE.pdf ...]
 *
 * A folder is opened as the library of the window (like "code ." opens a workspace); without one, the default
 * library "<Documents>/Xournal_Libraries/Default" is used. Each library has its own window (process): files given
 * to a second start go to the window of their library.
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCommandLineParser>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QtQml/qqmlextensionplugin.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <QDir>
#include <QFileInfo>

#include <optional>

#include "AppController.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/Library.h"
#include "shell/Previews.h"
#include "shell/SessionRecovery.h"
#include "shell/SingleInstance.h"
#include "shell/PageSketches.h"
#include "shell/Thumbnails.h"
#include "DocumentCanvasItem.h"
#include "session/AppContext.h"
#ifdef Q_OS_ANDROID
#include "AndroidActivity.h"
#include "AndroidSetup.h"
#endif
#ifdef Q_OS_WIN
#include "WindowsSetup.h"
#endif

Q_IMPORT_QML_PLUGIN(XournalQtPlugin)

int main(int argc, char* argv[]) {
    // Deliver every pen/touch sample (upstream Xournal++ also disables event compression).
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication::setDesktopFileName("xournal-qt");
    QGuiApplication::setApplicationName("xournal-qt");
    QGuiApplication::setApplicationDisplayName("Xournal Qt");
    QGuiApplication::setApplicationVersion(XQT_VERSION);
    // A QApplication (not only QGuiApplication): the platform theme (e.g. KDE Plasma) then provides its native,
    // resizable file dialogs for QtQuick.Dialogs instead of Qt's built-in QML fallback.
    QApplication qapp(argc, argv);
#ifdef Q_OS_ANDROID
    // Folders, resources and fonts for the core (GLib, fontconfig), before anything reads them.
    xqt::android::prepareEnvironment();
#endif
#ifdef Q_OS_WIN
    // UTF-8 for std::filesystem's narrow strings, GLib's cache folder, fontconfig (see qt/docs/windows.md).
    xqt::windows::prepareEnvironment();
#endif
    // The program icon (the desktop file gives it to the window when installed; this covers the build tree)
    QGuiApplication::setWindowIcon(QIcon::fromTheme(
            "xournal-qt", QIcon(QString::fromStdString((xqt::AppContext::defaultResourceDir() / "icons" / "xournal-qt.svg").string()))));
    // Like upstream initCAndCoutLocales(): numbers in the C locale for cairo, PDF export and the file format.
    setlocale(LC_NUMERIC, "C");
    xqt::AppContext::installQtUiThreadDispatcher();

    QCommandLineParser parser;
    parser.setApplicationDescription("Xournal Qt - note taking and PDF annotation");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("folder", "Folder to open as library (default: the standard library)", "[folder]");
    parser.addPositionalArgument("file", "Documents to open (.xopp, .xoj or .pdf)", "[files...]");
    parser.process(qapp);

    QStringList files;
    QString libraryDir;
    for (const QString& arg: parser.positionalArguments()) {
        const QFileInfo info(arg);
        if (info.isDir() && libraryDir.isEmpty()) {
            libraryDir = info.absoluteFilePath();
        } else {
            files << info.absoluteFilePath();
        }
    }
    const bool offscreen = QGuiApplication::platformName() == "offscreen";
    // The library: the folder given, else the default one (created on first use). Off-screen runs (tests,
    // screenshots) only get one when a folder is given.
    std::optional<xqt::Library> library;
    if (!libraryDir.isEmpty()) {
        library.emplace(fs::path(libraryDir.toStdString()));
    } else if (!offscreen) {
        std::error_code ec;
        fs::create_directories(xqt::Library::defaultRoot(), ec);
        library.emplace(xqt::Library::defaultRoot());
    }
    // One instance per library: a second start hands its files to the running window (as tabs) and exits.
    // Off-screen runs (tests, screenshots) are always independent.
    xqt::SingleInstance instance(library && !library->isDefault()
                                         ? QString("xournal-qt-%1-%2").arg(xqt::SingleInstance::userId()).arg(QString::fromStdString(library->key()))
                                         : QString());
    // Android starts one activity of the app anyway (launchMode singleTop).
#ifdef Q_OS_ANDROID
    const bool independent = true;
#else
    const bool independent = qEnvironmentVariableIsSet("XQT_NO_SINGLE_INSTANCE") ||
                             qEnvironmentVariableIsSet("XQT_SCREENSHOT") || offscreen;
#endif
    if (!independent) {
        if (instance.sendToRunningInstance(files)) {
            return 0;
        }
        instance.listen();
    }

    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
    AppController controller;
    if (library) {
        controller.setLibraryRoot(library->root());
    }
    // Crash recovery and reopening the last tabs; not for off-screen runs (tests, screenshots).
    if (independent && QGuiApplication::platformName() == "offscreen") {
        for (const QString& f: files) {
            controller.openPath(f);
        }
    } else {
#ifndef Q_OS_ANDROID
        // Not on Android yet: the handlers replace the system's, and a crash would then leave no backtrace in
        // logcat (see qt/docs/android-roadmap.md).
        xqt::SessionRecovery::installCrashHandlers();
#endif
        controller.startSession(files);
    }
    QObject::connect(&instance, &xqt::SingleInstance::filesRequested, &controller, &AppController::openPaths);

    QQmlApplicationEngine engine;
    engine.addImageProvider("thumbnail", new xqt::ThumbnailProvider);  // the engine takes ownership
    engine.addImageProvider("sketch", new xqt::SketchProvider);
    engine.addImageProvider("preview", new xqt::PreviewProvider);
    engine.addImageProvider("hitpage", new xqt::HitPageProvider);
    engine.addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
    engine.rootContext()->setContextProperty("app", &controller);
    AppController::setStartMaximized(true);
    // Undocked documents get a window of their own: the same QML, with their own controller as "app".
    AppController::setWindowFactory([&engine](AppController* window) {
        auto* context = new QQmlContext(engine.rootContext(), window);
        context->setContextProperty("app", window);
        auto* component = new QQmlComponent(&engine, QStringLiteral("XournalQt"), QStringLiteral("Main"), window);
        QObject* object = component->create(context);
        if (!object) {
            qWarning("Could not make a window: %s", qPrintable(component->errorString()));
            return;
        }
        object->setParent(window);
        if (auto* w = qobject_cast<QQuickWindow*>(object)) {
            AppController::watchWindow(w);
            w->show();
            w->requestActivate();
        }
    });
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &qapp, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("XournalQt", "Main");
    AppController::watchWindow(qobject_cast<QWindow*>(engine.rootObjects().value(0)));
#ifdef Q_OS_ANDROID
    // A phone without a pen (the Galaxy Fold 7) is written on with the finger: drawing with the finger is on at the
    // first start there, off where a stylus is attached (as on the desktop)
    controller.setFingerDrawingDefault(!xqt::android::hasStylus());
    // "Open with" and the share sheet: files other apps hand over, at start and while the app runs (the window
    // is there to show them and what went wrong)
    xqt::android::watchIncomingFiles([&controller](const QStringList& files) { controller.receiveFiles(files); });
#endif

    // Developer aid: XQT_SCREENSHOT=file.png renders the window after a moment, saves it and quits.
    // XQT_SCREENSHOT_POPUP=<objectName> opens that popup first (e.g. settingsPage, tabOverview).
    if (const auto shot = qEnvironmentVariable("XQT_SCREENSHOT"); !shot.isEmpty()) {
        // XQT_SCREENSHOT_ACTION=<method> calls an AppController method without arguments (e.g. selectAllOnPage),
        // "<method>:<number>" one with a number (e.g. undockTab:0).
        if (const auto action = qEnvironmentVariable("XQT_SCREENSHOT_ACTION"); !action.isEmpty()) {
            QTimer::singleShot(400, &controller, [&controller, action] {
                const QString name = action.section(':', 0, 0);
                if (action.contains(':')) {
                    QMetaObject::invokeMethod(&controller, name.toLatin1().constData(),
                                              Q_ARG(int, action.section(':', 1).toInt()));
                } else {
                    QMetaObject::invokeMethod(&controller, name.toLatin1().constData());
                }
            });
        }
        // XQT_SCREENSHOT_SELECT=1,3,4 selects pages (sidebar, page grid), or library items on the home screen.
        if (const auto sel = qEnvironmentVariable("XQT_SCREENSHOT_SELECT"); !sel.isEmpty()) {
            QTimer::singleShot(200, &controller, [&controller, sel] {
                QList<int> pages;
                for (const QString& p: sel.split(',')) {
                    pages << p.toInt();
                }
                if (controller.homeVisible()) {  // library items
                    for (int row: pages) {
                        QMetaObject::invokeMethod(controller.libraryModel(), "toggleSelected", Q_ARG(int, row));
                    }
                    return;
                }
                QMetaObject::invokeMethod(controller.pagesModel(), "selectPages", Q_ARG(QList<int>, pages));
            });
        }
        // XQT_SCREENSHOT_SEARCH=<text> searches all tabs (and shows the hits of the current one).
        if (const auto query = qEnvironmentVariable("XQT_SCREENSHOT_SEARCH"); !query.isEmpty()) {
            QTimer::singleShot(200, &controller, [&controller, query] {
                if (controller.homeVisible()) {
                    controller.libraryModel()->setProperty("searchQuery", query);  // the library search
                    return;
                }
                controller.searchAllTabs(query);
                controller.openSearchResult(controller.currentTab());
            });
        }
        // XQT_SCREENSHOT_SET=<objectName>.<property>=<value> sets a property of a QML item (e.g. homeView.extended=true).
        if (const auto set = qEnvironmentVariable("XQT_SCREENSHOT_SET"); !set.isEmpty()) {
            QTimer::singleShot(100, [&engine, &controller, set] {
                const QString target = set.section('=', 0, 0);
                const QString name = target.section('.', 0, 0);
                // "app.<property>": the controller, "window.<property>": the window
                QObject* o = name == "app" ? &controller : nullptr;
                if (auto* w = engine.rootObjects().value(0); w && !o) {
                    o = name == "window" ? w : w->findChild<QObject*>(name);
                }
                if (o) {
                    o->setProperty(target.section('.', 1).toLatin1().constData(), set.section('=', 1));
                }
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
