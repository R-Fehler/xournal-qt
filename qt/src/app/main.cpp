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
#include <cstdio>

#include <QCommandLineParser>
#include <QFontDatabase>
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

#include <functional>
#include <optional>
#ifdef Q_OS_MACOS
#include <QFileOpenEvent>
#endif

#include "AppController.h"
#include "AppServices.h"
#include "AudioControl.h"
#include "EngineSetup.h"
#include "audio/AudioDevice.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/ModelInfo.h"
#ifdef XQT_HWR_ONNX
#include "hwr/CtcRecognizer.h"
#include "hwr/TrocrRecognizer.h"
#endif
#include "EmojiFont.h"
#include "shell/Library.h"
#include "shell/SessionRecovery.h"
#include "shell/SingleInstance.h"
#include "shell/SystemApps.h"
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

namespace {
/// The window's safe area (Main.qml's safeTop, safeRight, safeBottom, safeLeft; qt/docs/features/adaptive-layout.md,
/// "Safe areas"): edge to edge (Android 15 and newer, iOS) the status bar lies over the window's top, the navigation
/// bar or the gesture bar over its bottom, and a camera cut-out over a side when the phone is held sideways. The
/// controls stay clear of them; the pages are drawn under them. Qt 6.9+ reports them (and when they change: the phone
/// turned, folded or unfolded); older Qt: 0. XQT_SAFE_AREA="top,right,bottom,left" sets them by hand (to look at a
/// phone's insets on the desktop); XQT_FAKE_KEYBOARD=<height> shows the layout with a soft keyboard of that height
/// (Main.qml's fakeKeyboardHeight).
void watchSafeArea(QQuickWindow* w) {
    if (!w) {
        return;
    }
    if (const QStringList fake = qEnvironmentVariable("XQT_SAFE_AREA").split(','); fake.size() == 4) {
        const char* names[] = {"safeTop", "safeRight", "safeBottom", "safeLeft"};
        for (int i = 0; i < 4; ++i) {
            w->setProperty(names[i], fake[i].trimmed().toDouble());
        }
    } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        auto apply = [w] {
            const QMargins m = w->safeAreaMargins();
            w->setProperty("safeTop", m.top());
            w->setProperty("safeRight", m.right());
            w->setProperty("safeBottom", m.bottom());
            w->setProperty("safeLeft", m.left());
        };
        QObject::connect(w, &QWindow::safeAreaMarginsChanged, w, apply);
        apply();
#endif
    }
    if (const int keyboard = qEnvironmentVariableIntValue("XQT_FAKE_KEYBOARD"); keyboard > 0) {
        w->setProperty("fakeKeyboardHeight", keyboard);
    }
}
#ifdef Q_OS_MACOS
/// The documents Finder opens with the app (QFileOpenEvent, qt/docs/development/macos.md), handed to `open`.
class FileOpenFilter: public QObject {
public:
    FileOpenFilter(std::function<void(const QString&)> open, QObject* parent): QObject(parent), open(std::move(open)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            if (const QString file = static_cast<QFileOpenEvent*>(event)->file(); !file.isEmpty()) {
                open(file);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(const QString&)> open;
};
#endif
}  // namespace

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
    xqt::android::addSymbolFallback();
#endif
#ifdef Q_OS_WIN
    // UTF-8 for std::filesystem's narrow strings, GLib's cache folder, fontconfig (see qt/docs/development/windows.md).
    xqt::windows::prepareEnvironment();
#endif
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // The app's colour emoji font (EmojiFont.h) for Qt's text as well (Pango gets it in AppContext). Windows, macOS
    // and Android have emoji fonts that Qt draws.
    if (QFontDatabase::addApplicationFont(QString::fromStdString(
                (xqt::AppContext::defaultResourceDir() / "fonts" / xqt::emoji::FONT_FILE).string())) >= 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        QFontDatabase::addApplicationEmojiFontFamily(QString::fromUtf8(xqt::emoji::FONT_FAMILY));
#endif
    }
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
    // Quick note (qt/docs/features/quick-note.md): also handed to the window that runs already
    const QCommandLineOption quickNoteOption(
            "quick-note", "Make a quick note: a new note in the library's Inbox, named by the date and time (or a line "
                          "in today's Markdown note there, as Settings - Documents says)");
    parser.addOption(quickNoteOption);
    // What recording runs on (qt/docs/features/audio.md, "Platforms"; the CI's smoke tests of the packages ask)
    const QCommandLineOption audioInfoOption(
            "audio-info", "Print whether recording is offered and the microphones and speakers found, then exit "
                          "(exit code 1: recording is not offered)");
    parser.addOption(audioInfoOption);
    parser.process(qapp);
    if (parser.isSet(audioInfoOption)) {
        std::fputs(xqt::audio::describe().c_str(), stdout);
        std::fflush(stdout);
        return xqt::audio::available() ? 0 : 1;
    }
    const bool quickNote = parser.isSet(quickNoteOption);

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
#ifndef Q_OS_ANDROID  // (Android: once the libraries' home is chosen, below)
        std::error_code ec;
        fs::create_directories(xqt::Library::defaultRoot(), ec);
        library.emplace(xqt::Library::defaultRoot());
#endif
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
        if (instance.sendToRunningInstance(quickNote ? files + QStringList{xqt::SingleInstance::QUICK_NOTE} : files)) {
            return 0;
        }
        instance.listen();
    }

    QQuickStyle::setStyle("Material");
    xqt::registerQuickTypes();
#ifdef XQT_HWR_ONNX
    // The handwriting search's recognisers, by their manifests' kind: TrOCR or a CTC model in ONNX Runtime (loaded
    // only when the search is switched on); a folder without a model gets TrOCR's, which says what is missing
    xqt::hwr::HandwritingSearch::setFactory([](const QString& dir) -> std::shared_ptr<xqt::hwr::Recognizer> {
        if (xqt::hwr::ModelInfo::read(dir).kind == QLatin1String("ctc")) {
            return std::make_shared<xqt::hwr::CtcRecognizer>(dir);
        }
        return std::make_shared<xqt::hwr::TrocrRecognizer>(dir);
    });
#endif
    // What the windows share (settings, tools, library, the background jobs): made before the first window, gone
    // after the last one
    xqt::AppServices services;
    AppController controller(services);
#ifdef Q_OS_ANDROID
    // The libraries' home: the phone's Documents/Xournal_Libraries once they were moved there (with "All files
    // access"), else the app's own folder (qt/docs/development/android.md)
    controller.chooseLibrariesHome();
    if (libraryDir.isEmpty()) {
        std::error_code ec;
        fs::create_directories(xqt::Library::defaultRoot(), ec);
        library.emplace(xqt::Library::defaultRoot());
    }
    // One window: it opens the library it showed last (a folder of the shared storage only while the app may read it)
    if (const QString last = controller.rememberedLibrary(); libraryDir.isEmpty() && !last.isEmpty()) {
        xqt::SystemApps& apps = xqt::SystemApps::instance();
        if (QFileInfo(last).isDir() && (!apps.needsAllFilesAccess(last) || apps.hasAllFilesAccess())) {
            library.emplace(fs::path(last.toStdString()));
        }
    }
#endif
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
        // logcat (see qt/docs/development/android-roadmap.md).
        xqt::SessionRecovery::installCrashHandlers();
#endif
        controller.startSession(files);
    }
    QObject::connect(&instance, &xqt::SingleInstance::filesRequested, &controller, &AppController::openPaths);
    QObject::connect(&instance, &xqt::SingleInstance::quickNoteRequested, &controller, &AppController::quickNote);
    if (quickNote) {
        // (once the window is there: a Markdown note opens with the cursor in it)
        QTimer::singleShot(0, &controller, [&controller] { controller.quickNote(); });
    }
#ifdef Q_OS_MACOS
    // Finder hands documents over as events, not as arguments: a double click, "Open With", a drop on the Dock icon.
    qapp.installEventFilter(new FileOpenFilter([&controller](const QString& f) { controller.openPaths({f}); }, &qapp));
#endif

    QQmlApplicationEngine engine;
    xqt::setUpEngine(engine, &controller);  // (the image providers and `app`: the UI tests' fixture uses it too)
    services.setStartMaximized(true);
    // Undocked documents get a window of their own: the same QML, with their own controller as "app".
    services.setWindowFactory([&engine](AppController* window) {
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
            watchSafeArea(w);
            w->show();
            w->requestActivate();
        }
    });
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &qapp, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("XournalQt", "Main");
    AppController::watchWindow(qobject_cast<QWindow*>(engine.rootObjects().value(0)));
    watchSafeArea(qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)));
#ifdef Q_OS_ANDROID
    // A phone without a pen (the Galaxy Fold 7) is written on with the finger: drawing with the finger is on at the
    // first start there, off where a stylus is attached (as on the desktop)
    controller.setFingerDrawingDefault(!xqt::android::hasStylus());
    // A recording keeps the microphone in the background through a foreground service, whose notification shows
    // its time with Pause/Resume and Stop (qt/docs/features/audio.md, "Android"); refused, the microphone is allowed on
    // the app's page of the system's settings
    xqt::AudioControl::setPlatformHook([](const xqt::AudioControl::PlatformState& state) {
        xqt::android::setRecording(state.recording, state.paused, state.recordedMs, state.title,
                                   {QCoreApplication::translate("AudioControl", "Recording"),
                                    QCoreApplication::translate("AudioControl", "Recording paused"),
                                    QCoreApplication::translate("AudioControl", "Pause"),
                                    QCoreApplication::translate("AudioControl", "Resume"),
                                    QCoreApplication::translate("AudioControl", "Stop")});
    });
    xqt::android::watchRecordingCommands([&controller](int command) {
        if (auto* audio = qobject_cast<xqt::AudioControl*>(controller.audioObject())) {
            audio->platformCommand(static_cast<xqt::AudioControl::PlatformCommand>(command));
        }
    });
    xqt::AudioControl::setSettingsOpener([] { return xqt::android::openAppSettings(); });
    // "Open with" and the share sheet: files other apps hand over, at start and while the app runs (the window
    // is there to show them and what went wrong)
    // The launcher's shortcut "Quick note" (qt/docs/features/quick-note.md) comes the same way, as an entry of its own.
    xqt::android::watchIncomingFiles([&controller](QStringList files) {
        const bool quickNote = files.removeAll(QLatin1String(xqt::android::QUICK_NOTE)) > 0;
        if (!files.isEmpty()) {
            controller.receiveFiles(files);
        }
        if (quickNote) {
            controller.quickNote();
        }
    });
#endif

    // XQT_SCREENSHOT=file.png renders the window after a moment (XQT_SCREENSHOT_DELAY_MS, default 1.5 s), saves it
    // and quits (the CI's smoke tests of the Windows and macOS packages, qt/scripts/*-smoke.sh).
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
