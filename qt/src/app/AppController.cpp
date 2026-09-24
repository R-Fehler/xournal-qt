#include "AppController.h"

#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>

#include <shared_mutex>

#include <QCoreApplication>
#include <QFile>
#include <QDesktopServices>
#include <QProcess>
#include <QFileInfo>
#include <QClipboard>
#include <QMimeData>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPrintDialog>
#include <QPrinter>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWindow>

#include "control/ToolEnums.h"
#include "control/ExportHelper.h"
#include "control/ToolHandler.h"
#include "TextEditor.h"
#include "model/Font.h"
#include "control/tools/EditSelection.h"
#include "control/settings/Settings.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "model/Document.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "control/pagetype/PageTypeHandler.h"
#include "undo/GroupUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/PageBackgroundChangedUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/NamedColor.h"
#include "util/TextLinks.h"
#include "util/XojMsgBox.h"

#include "CanvasView.h"
#include "PenHover.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentTextIndex.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/Previews.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/DocumentChapters.h"
#include "shell/LayersModel.h"
#include "shell/ShortcutsModel.h"
#include "shell/OutlineModel.h"
#include "ImageFile.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPassages.h"
#include "session/FuzzyQuery.h"
#include "session/TextMatch.h"
#include "TextFlow.h"
#include "session/HybridPdf.h"
#include "session/MergedPdf.h"
#include "session/TextFile.h"
#include "shell/PageClipboard.h"
#include "shell/RecentFiles.h"
#include "shell/PageFilterModel.h"
#include "shell/PagesModel.h"
#include "shell/SessionRecovery.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/ReferenceMode.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
QColor toQColor(Color c) { return QColor(c.red, c.green, c.blue); }
Color toColor(const QColor& c) {
    return Color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
}
}  // namespace

AppController::AppController(QObject* parent): QObject(parent) {
    app = std::make_shared<AppContext>(AppContext::defaultResourceDir());
    colors = std::make_shared<Palette>(app->getResourceDir() / "palettes" / "xournal.gpl");
    try {
        colors->load();
    } catch (const std::exception& e) {
        colors->load_default();
    }
    // Messages from the reused core (XojMsgBox) are shown by the QML UI.
    xoj::compat::setMessageSink([this](xoj::compat::MessageRequest r, xoj::util::move_only_function<void(int)> done) {
        const bool error = r.kind == xoj::compat::MessageKind::Error;
        QMetaObject::invokeMethod(this, [this, title = QString::fromStdString(r.title),
                                         text = QString::fromStdString(r.text),
                                         error] { Q_EMIT message(title, text, error); });
        if (done) {
            done(r.buttons.empty() ? 0 : r.buttons.front().response);  // questions: first button until dialogs exist
        }
    });
    connect(app.get(), &AppContext::activeToolChanged, this, &AppController::toolChanged);
    connect(app.get(), &AppContext::toolPropertiesChanged, this, &AppController::toolChanged);
    loadCustomWidths();
    SettingsModel::applyPreviewMemory(*app->getSettings());
    SettingsModel::applyCanvasMemory(*app->getSettings());
    SettingsModel::applyFuzzyTypos(*app->getSettings());

    pages = std::make_unique<PagesModel>();
    filteredPages = std::make_unique<PageFilterModel>(*pages);
    outline = std::make_unique<OutlineModel>();
    layers = std::make_unique<LayersModel>();
    ownPageClipboard = std::make_unique<PageClipboard>();
    pageClipboard = ownPageClipboard.get();
    // "Only pages with hits" ends with the search.
    connect(this, &AppController::searchChanged, this, [this] {
        if (searchQuery().isEmpty()) {
            filteredPages->setOnlySearchHits(false);
        }
    });
    ownSettingsView = std::make_unique<SettingsModel>(*app);
    settingsView = ownSettingsView.get();
    ownShortcuts = std::make_unique<ShortcutsModel>(*app->getSettings());
    shortcuts = ownShortcuts.get();
    makeTabs();
    ownLibrary = std::make_unique<LibraryModel>();
    library = ownLibrary.get();
    // Open documents take the PDF text the library index read before (their search has all counts at once)
    DocumentTextIndex::setSeeder([lib = QPointer<LibraryModel>(library)](const fs::path& pdf) {
        LibraryIndex* index = lib ? lib->searchIndex() : nullptr;
        return index ? index->knownPdfText(pdf) : std::map<int, QString>();
    });
    library->onFilesChanged = [this](const DocumentFiles::Result& r) { filesChanged(r); };
    // The fuzzy search's toggle is an app-wide setting (shared by all windows through the library model)
    {
        bool fuzzy = false;
        app->getSettings()->getCustomElement("xournalQt").getBool("fuzzySearch", fuzzy);
        library->setFuzzySearch(fuzzy);
        connect(library, &LibraryModel::fuzzySearchChanged, this, [this] {
            app->getSettings()->getCustomElement("xournalQt").setBool("fuzzySearch", library->fuzzySearch());
            app->getSettings()->customSettingsChanged();
        });
    }
    ownRecent = std::make_unique<RecentFiles>(RecentFiles::defaultStoreFile());
    recent = ownRecent.get();
    recent->onFilesChanged = [this](const DocumentFiles::Result& r) {
        library->filesMoved(r);  // a renamed library document keeps its search index entry
        filesChanged(r);
    };
    journalFile = SessionRecovery::defaultJournalFile();
}

// A window of its own: the same settings, tools, library and rendering, but its own documents.
AppController::AppController(AppController& mainWindow, QObject* parent): QObject(parent) {
    primary = &mainWindow;
    app = mainWindow.app;
    colors = mainWindow.colors;
    settingsView = mainWindow.settingsView;
    shortcuts = mainWindow.shortcuts;
    library = mainWindow.library;
    recent = mainWindow.recent;
    pageClipboard = mainWindow.pageClipboard;  // copied pages can be pasted in any window
    connect(app.get(), &AppContext::activeToolChanged, this, &AppController::toolChanged);
    connect(app.get(), &AppContext::toolPropertiesChanged, this, &AppController::toolChanged);
    pages = std::make_unique<PagesModel>();
    filteredPages = std::make_unique<PageFilterModel>(*pages);
    outline = std::make_unique<OutlineModel>();
    layers = std::make_unique<LayersModel>();
    connect(this, &AppController::searchChanged, this, [this] {
        if (searchQuery().isEmpty()) {
            filteredPages->setOnlySearchHits(false);
        }
    });
    makeTabs();
    home = false;  // it shows documents, never the home screen
}

void AppController::makeTabs() {
    tabs = std::make_unique<TabManager>(*app);
    connect(tabs.get(), &TabManager::currentTabChanged, this, &AppController::currentTabChanged);
    referenceMode = std::make_unique<ReferenceMode>(*tabs, app->getSettings());
    connect(referenceMode.get(), &ReferenceMode::openExternal, this, &AppController::openLink);
    connect(referenceMode.get(), &ReferenceMode::copied, this, [this](const QString& what) {
        Q_EMIT pageActionDone(what, false);
    });
    // The reference written in: its text tool makes Markdown text as the notes' does, edited in the same panel
    connect(referenceMode.get(), &ReferenceMode::changed, this, &AppController::applyMarkdownText);
    connect(referenceMode.get(), &ReferenceMode::markdownRequested, this, &AppController::markdownRequested);
    connect(referenceMode.get(), &ReferenceMode::markdownBoxRequested, this, &AppController::markdownBoxRequested);
    connect(tabs.get(), &TabManager::pdfPagesFailed, this, [this](const QString& error) {
        Q_EMIT message(tr("Pasting PDF pages failed"),
                       tr("The pasted pages show their PDF page as a picture (its text cannot be searched).\n\n%1")
                               .arg(error),
                       true);
    });
    connect(tabs.get(), &TabManager::savingChanged, this, [this] {
        Q_EMIT anySavingChanged();
        if (!tabs->anySaving() && !whenAllSavedCalls.empty()) {
            // (from the event loop: a call may close tabs)
            QTimer::singleShot(0, this, [this] {
                if (tabs->anySaving()) {
                    return;  // (another save started meanwhile: they wait for it too)
                }
                auto calls = std::move(whenAllSavedCalls);
                whenAllSavedCalls.clear();
                for (QJSValue& f: calls) {
                    f.call();
                }
            });
        }
    });
    connect(tabs.get(), &TabManager::countChanged, this, [this] {
        if (textWatcher) {
            watchTextFiles();  // (a text file closed)
        }
        if (tabs->count() == 0) {
            if (isSecondary()) {
                Q_EMIT closeWindowRequested();  // the last document went away with its window
            } else {
                setHomeVisible(true);  // no document: the home screen
            }
        }
    });
}

AppController::~AppController() {
    if (!isSecondary()) {
        xoj::compat::setMessageSink({});  // (the main window set it)
    }
    for (auto& c: currentConnections) {
        disconnect(c);
    }
    flow.reset();  // (before the sessions)
    pages->setSession(nullptr);
    outline->setSession(nullptr);
    layers->setSession(nullptr);
    recovery.reset();  // unregisters the sessions from the crash handler before they go away
    referenceMode.reset();
    tabs.reset();
}

namespace {
std::function<void(AppController*)> windowFactory;  // set by main(): makes the window for a controller
bool windowsStartMaximized = false;                  // set by main()
}  // namespace

void AppController::setStartMaximized(bool on) { windowsStartMaximized = on; }

namespace {
bool windowLogOn() {
    static const bool on = qEnvironmentVariableIsSet("XQT_LOG_WINDOW");
    return on;
}
/// On stderr like XQT_PERF (qInfo may go to the journal when stderr is not a terminal)
template <typename... Args>
void windowLogLine(const char* format, Args... args) {
    std::fprintf(stderr, format, args...);
    std::fputc('\n', stderr);
}
qint64 windowLogMs() {
    static QElapsedTimer clock;
    if (!clock.isValid()) {
        clock.start();
    }
    return clock.elapsed();
}
QString statesText(Qt::WindowStates s) {
    QStringList parts;
    if (s & Qt::WindowMinimized) parts << "minimized";
    if (s & Qt::WindowMaximized) parts << "maximized";
    if (s & Qt::WindowFullScreen) parts << "fullscreen";
    if (s & Qt::WindowActive) parts << "active";
    return parts.isEmpty() ? QStringLiteral("normal") : parts.join('+');
}
/// Logs what happens to a window (XQT_LOG_WINDOW)
class WindowWatcher final: public QObject {
public:
    explicit WindowWatcher(QWindow* w): QObject(w) {
        w->installEventFilter(this);
        connect(w, &QWindow::windowStateChanged, this, [w](Qt::WindowState) {
            windowLogLine("[window %6lld ms] %p states -> %s (geometry %d,%d %dx%d)", windowLogMs(), static_cast<void*>(w),
                  qPrintable(statesText(w->windowStates())), w->x(), w->y(), w->width(), w->height());
        });
    }
    bool eventFilter(QObject* o, QEvent* e) override {
        auto* w = static_cast<QWindow*>(o);
        switch (e->type()) {
            case QEvent::Resize:
                windowLogLine("[window %6lld ms] %p resized to %dx%d (%s)", windowLogMs(), static_cast<void*>(w), w->width(),
                      w->height(), qPrintable(statesText(w->windowStates())));
                break;
            case QEvent::Move:
                windowLogLine("[window %6lld ms] %p moved to %d,%d", windowLogMs(), static_cast<void*>(w), w->x(), w->y());
                break;
            case QEvent::TouchBegin:
            case QEvent::TouchEnd:
            case QEvent::TouchCancel:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease: {
                const auto* pe = static_cast<QPointerEvent*>(e);
                const auto& points = pe->points();
                const QPointF at = points.isEmpty() ? QPointF() : points.first().scenePosition();
                windowLogLine("[window %6lld ms] %p %s at %.0f,%.0f (%d points)", windowLogMs(), static_cast<void*>(w),
                      e->type() == QEvent::TouchBegin         ? "touch begin"
                      : e->type() == QEvent::TouchEnd         ? "touch end"
                      : e->type() == QEvent::TouchCancel      ? "touch cancel"
                      : e->type() == QEvent::MouseButtonPress ? "mouse press"
                                                              : "mouse release",
                      at.x(), at.y(), static_cast<int>(points.size()));
                break;
            }
            default:
                break;
        }
        return false;
    }
};
}  // namespace

void AppController::watchWindow(QWindow* window) {
    if (window && windowLogOn()) {
        new WindowWatcher(window);
        windowLogLine("[window %6lld ms] %p watched (%s, %dx%d)", windowLogMs(), static_cast<void*>(window),
              qPrintable(statesText(window->windowStates())), window->width(), window->height());
    }
}

void AppController::logWindow(const QString& what) const {
    if (windowLogOn()) {
        windowLogLine("[window %6lld ms] app asks: %s", windowLogMs(), qPrintable(what));
    }
}

bool AppController::startMaximized() const { return windowsStartMaximized; }

void AppController::setWindowFactory(std::function<void(AppController*)> factory) {
    windowFactory = std::move(factory);
}

void AppController::closeAllTabs() {
    while (tabs->count() > 0) {
        tabs->closeTab(tabs->count() - 1);
    }
}

void AppController::undockTab(int index) {
    if (index < 0 || index >= tabs->count() || (isSecondary() && tabs->count() == 1)) {
        return;  // (the only document of its own window is undocked already)
    }
    AppController* main = isSecondary() ? primary : this;
    auto tab = tabs->takeTab(index);
    if (!tab) {
        return;
    }
    auto* window = new AppController(*main, main);
    main->windows.push_back(window);
    window->tabManager().adoptTab(std::move(tab));
    if (main->recovery) {
        main->recovery->watch(window->tabManager());  // its changes survive a crash as well
    }
    if (windowFactory) {
        windowFactory(window);
    }
}

void AppController::dockTab(int index) {
    if (!isSecondary() || index < 0 || index >= tabs->count()) {
        return;
    }
    if (auto tab = tabs->takeTab(index)) {
        primary->tabManager().adoptTab(std::move(tab));
        primary->setHomeVisible(false);
        Q_EMIT primary->raiseRequested();
    }
}

void AppController::windowClosed() {
    if (!isSecondary() || windowGone) {
        return;  // (once: the window is gone and this controller with it)
    }
    windowGone = true;
    // Documents with unsaved changes are not lost: they go back to the main window.
    for (int i = tabs->count() - 1; i >= 0; --i) {
        if (tabs->session(i) && tabs->session(i)->isModified()) {
            primary->tabManager().adoptTab(tabs->takeTab(i));
            primary->setHomeVisible(false);
        }
    }
    auto& list = primary->windows;
    list.erase(std::remove(list.begin(), list.end(), this), list.end());
    deleteLater();
}

void AppController::shutdown() {
    // Saves that run are finished first (the window waited for them already; this is the last resort)
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->session(i)->waitForSaves();
    }
    // The image workers draw with Qt: they must be done before the application takes its plugins away
    PreviewProvider::shutdown();
    HitPageProvider::shutdown();
    MdSnippetProvider::shutdown();
    settingsView->end();  // settings screen still open: save its changes
    if (recovery) {
        recovery->finish();  // a normal exit: reopen these tabs next time
    }
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->session(i)->deleteAutosaveFile();
    }
    app->getToolHandler()->saveSettings();
    app->getSettings()->save();
}

DocumentSession* AppController::session() const { return tabs->currentSession(); }
bool AppController::textPagesFixed() const { return session() && session()->textFile() && !session()->hasFilePath(); }
CanvasView* AppController::canvas() const { return tabs->currentView(); }

bool AppController::textFlowActive() const { return flow && flow->active(); }

QString AppController::textFlowFamily() const {
    QString family = QString::fromStdString(app->getSettings()->getFont().getName());
    // (a font name may have a style, e.g. "Sans Bold": the family only)
    for (const char* style: {" Bold", " Italic", " Regular"}) {
        family.remove(QLatin1String(style));
    }
    return family.trimmed().isEmpty() ? QStringLiteral("Sans") : family.trimmed();
}

QVariantList AppController::beginTextFlow() {
    endTextFlow(true);
    endMarkdown(true);
    if (!session()) {
        return {};
    }
    flowSession = session();
    flow = std::make_unique<TextFlowSession>(*flowSession);
    TextFlow::Style style;
    style.family = textFlowFamily().toStdString();
    style.bodySize = app->getSettings()->getFont().getSize();
    flowPage = static_cast<int>(flowSession->getCurrentPageNo());
    QVariantList list;
    for (const auto& b: flow->begin(static_cast<size_t>(flowPage), style)) {
        list.append(TextFlow::toVariant(b));
    }
    flowOverflow = 0;
    Q_EMIT textFlowChanged();
    return list;
}

void AppController::updateTextFlow(const QVariantList& blocks) {
    if (!textFlowActive()) {
        return;
    }
    std::vector<TextBlock> list;
    for (const QVariant& v: blocks) {
        list.push_back(TextFlow::fromVariant(v.toMap()));
    }
    const double overflow = flow->update(list);
    if (overflow != flowOverflow) {
        flowOverflow = overflow;
        Q_EMIT textFlowChanged();
    }
}

void AppController::endTextFlow(bool keep) {
    if (!flow) {
        return;
    }
    if (keep) {
        flow->finish();
    } else {
        flow->cancel();
    }
    flow.reset();
    flowSession = nullptr;
    flowPage = -1;
    flowOverflow = 0;
    Q_EMIT textFlowChanged();
    Q_EMIT undoRedoChanged();
}

bool AppController::markdownActive() const { return markdown && markdown->active(); }


QString AppController::beginMarkdown(int page) { return startMarkdown(page, std::nullopt); }

QString AppController::beginMarkdownBox(int page, double x, double y) { return startMarkdown(page, QPointF(x, y)); }

QVariantMap AppController::takeMarkdownFromPage() {
    CanvasView* v = canvas();
    if (!v || !v->getMarkdownEditor()) {
        return {};
    }
    const MarkdownEditor::Target t = v->getMarkdownEditor()->target();
    v->endTextEditing();
    return {{"page", static_cast<int>(t.page)}, {"pageText", t.pageText}, {"x", t.x}, {"y", t.y}};
}

QString AppController::startMarkdown(int page, std::optional<QPointF> at) {
    endMarkdown(true);
    endTextFlow(true);
    // The document with the keys: the reference while it is written in, else the notes
    DocumentSession* target = editedReference() ? &editedReference()->getSession() : session();
    if (!target || target->isReadOnly() || target->textFile()) {
        return {};  // (a text file is written on its pages; one shown read-only is not edited at all)
    }
    mdSession = target;
    markdown = std::make_unique<MarkdownSession>(*mdSession);
    md::Style style;
    style.family = textFlowFamily().toStdString();
    style.size = markdownFontSize();
    style.color = app->getToolHandler()->getColor();
    if (!at) {
        style.color = Color(0, 0, 0);  // (the page's text: black, as the text mode)
    }
    mdPage = page >= 0 ? page : static_cast<int>(mdSession->getCurrentPageNo());
    const QString source = QString::fromStdString(
            at ? markdown->beginBox(static_cast<size_t>(mdPage), style, at->x(), at->y())
               : markdown->begin(static_cast<size_t>(mdPage), style));
    mdPage = static_cast<int>(markdown->pageIndex());  // (the page's text: its first page)
    mdLastPage = static_cast<int>(markdown->lastPageIndex());
    mdOverflow = 0;
    Q_EMIT markdownChanged();
    return source;
}

void AppController::markdownPagesChanged(double overflow) {
    const int first = static_cast<int>(markdown->pageIndex());
    const int lastPage = static_cast<int>(markdown->lastPageIndex());
    if (overflow != mdOverflow || first != mdPage || lastPage != mdLastPage) {
        mdOverflow = overflow;
        mdPage = first;
        mdLastPage = lastPage;
        Q_EMIT markdownChanged();
    }
}

void AppController::updateMarkdown(const QString& source) {
    if (!markdownActive()) {
        return;
    }
    markdownPagesChanged(markdown->update(source.toStdString()));
}

void AppController::endMarkdown(bool keep) {
    if (!markdown) {
        return;
    }
    if (keep) {
        markdown->finish();
    } else {
        markdown->cancel();
    }
    markdown.reset();
    mdSession = nullptr;
    mdPage = -1;
    mdLastPage = -1;
    mdOverflow = 0;
    Q_EMIT markdownChanged();
    Q_EMIT undoRedoChanged();
}

void AppController::currentTabChanged() {
    if (flow && flowSession != session()) {
        endTextFlow(true);  // another document: the text mode ends (kept)
    }
    if (markdown && mdSession != session()) {
        endMarkdown(true);  // another document: editing the box ends (kept)
    }
    // Follow the signals of the current tab only.
    for (auto& c: currentConnections) {
        disconnect(c);
    }
    currentConnections.clear();
    if (DocumentSession* s = session()) {
        currentConnections.push_back(
                connect(s, &DocumentSession::modifiedChanged, this, &AppController::modifiedChanged));
        currentConnections.push_back(connect(s, &DocumentSession::savingChanged, this, &AppController::savingChanged));
        currentConnections.push_back(
                connect(s, &DocumentSession::undoRedoStateChanged, this, &AppController::undoRedoChanged));
        currentConnections.push_back(
                connect(s, &DocumentSession::undoRedoStateChanged, this, &AppController::pageUndoChanged));
        // Undoing a page change says so: the page that changed may be far from the one in view
        currentConnections.push_back(connect(s, &DocumentSession::pageActionUndone, this,
                                             [this](const QString& text, bool undone) {
                                                 Q_EMIT pageActionDone(
                                                         (undone ? tr("Undone: %1") : tr("Redone: %1")).arg(text), false);
                                             }));
        currentConnections.push_back(connect(s, &DocumentSession::filePathChanged, this, &AppController::titleChanged));
        currentConnections.push_back(
                connect(s, &DocumentSession::currentPageChanged, this, &AppController::pageChanged));
        currentConnections.push_back(
                connect(&s->search(), &DocumentSearch::changed, this, &AppController::searchChanged));
        currentConnections.push_back(
                connect(&s->search(), &DocumentSearch::finished, this, &AppController::searchChanged));
    }
    pages->setSession(session());
    outline->setSession(session());
    layers->setSession(session());
    if (CanvasView* v = canvas()) {
        currentConnections.push_back(connect(v, &CanvasView::pagesChanged, this, &AppController::pageChanged));
        currentConnections.push_back(connect(v, &CanvasView::selectionChanged, this, &AppController::selectionChanged));
        currentConnections.push_back(connect(v, &CanvasView::linkTapped, this, &AppController::linkTapped));
        currentConnections.push_back(
                connect(v, &CanvasView::markdownRequested, this, &AppController::markdownRequested));
        currentConnections.push_back(
                connect(v, &CanvasView::markdownBoxRequested, this, &AppController::markdownBoxRequested));
        currentConnections.push_back(
                connect(v, &CanvasView::contextRequested, this, &AppController::contextRequested));
        currentConnections.push_back(
                connect(v, &CanvasView::navigationChanged, this, &AppController::navigationChanged));
        currentConnections.push_back(connect(v, &CanvasView::pdfTextSelected, this, &AppController::pdfTextSelected));
        currentConnections.push_back(connect(v, &CanvasView::geometryChanged, this, &AppController::toolChanged));
        currentConnections.push_back(
                connect(v, &CanvasView::pdfTextSelectionCleared, this, &AppController::pdfTextSelectionCleared));
        // Whoever changes the selection (a press on the page, copying, marking, a page change): the knobs and the
        // pill follow it.
        currentConnections.push_back(
                connect(v, &CanvasView::pdfTextSelected, this, &AppController::pdfTextSelectionChanged));
        currentConnections.push_back(
                connect(v, &CanvasView::pdfTextSelectionCleared, this, &AppController::pdfTextSelectionChanged));
        applyPdfTextMode();
        applyMarkdownText();
        currentConnections.push_back(connect(&v->getViewController(), &ViewController::zoomChanged, this,
                                             &AppController::zoomChanged));
    }
    updatePresentedView();  // (another tab: it presents now)
    if (session() && session()->textFile()) {
        QTimer::singleShot(0, this, [this, s = QPointer<DocumentSession>(session())] { checkTextFile(s); });
    }
    Q_EMIT documentChanged();
    Q_EMIT titleChanged();
    Q_EMIT textLayoutChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT savingChanged();
    Q_EMIT undoRedoChanged();
    Q_EMIT zoomChanged();
    Q_EMIT pageChanged();
    Q_EMIT searchChanged();
    Q_EMIT pageUndoChanged();
    Q_EMIT selectionChanged();
    Q_EMIT navigationChanged();
    Q_EMIT pdfTextSelectionChanged();
    Q_EMIT toolChanged();  // the setsquare / compass of that tab
    Q_EMIT titlePageChanged();
}

bool AppController::hasSelection() const { return canvas() && canvas()->getSelection(); }
bool AppController::copySelection() {
    if (referenceMode->focused() && referenceMode->hasSelection()) {
        return referenceMode->copy();  // (the keys are for the reference while it has the focus)
    }
    return canvas() && canvas()->copySelection();
}
CanvasView* AppController::editedReference() const {
    return referenceMode->focused() && referenceMode->editing() ? referenceMode->canvas() : nullptr;
}
bool AppController::cutSelection() {
    if (CanvasView* r = editedReference()) {
        return r->cutSelection();
    }
    if (referenceMode->focused()) {
        return false;  // (the keys are with a reference for reading: nothing is cut, neither there nor in the notes)
    }
    return canvas() && canvas()->cutSelection();
}
bool AppController::pasteElements() {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    if (CanvasView* r = editedReference()) {
        return !r->getSession().isReadOnly() && r->pasteElements();
    }
    return canvas() && !session()->isReadOnly() && canvas()->pasteElements();
}
bool AppController::pasteAt(qreal x, qreal y) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    return canvas() && !session()->isReadOnly() && canvas()->pasteElements(QPointF(x, y));
}
bool AppController::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasImage() || mime->hasText() || mime->hasFormat("application/xournal"));
}
void AppController::deleteSelection() {
    if (CanvasView* r = editedReference()) {
        r->deleteSelection();
    } else if (canvas() && !referenceMode->focused()) {
        canvas()->deleteSelection();
    }
}
void AppController::selectAllOnPage() {
    if (CanvasView* target = editedReference() ? editedReference() : canvas()) {
        if (app->getToolHandler()->getToolType() != TOOL_SELECT_RECT &&
            app->getToolHandler()->getToolType() != TOOL_SELECT_REGION) {
            selectTool("selectRegion");  // so that the selection can be moved right away
        }
        target->selectAllOnPage();
    }
}
bool AppController::insertImage(const QUrl& url) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    QFile f(url.toLocalFile());
    if (!canvas() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (app->getToolHandler()->getToolType() != TOOL_SELECT_RECT &&
        app->getToolHandler()->getToolType() != TOOL_SELECT_REGION) {
        selectTool("selectRect");  // so that the image can be moved and resized right away
    }
    if (!canvas()->insertImage(f.readAll())) {
        Q_EMIT message(tr("Insert image"), tr("\"%1\" is not an image that can be read.").arg(url.fileName()), true);
        return false;
    }
    return true;
}

void AppController::clearSelection() {
    if (canvas()) {
        canvas()->clearSelection();
    }
}

QString AppController::searchQuery() const { return session() ? session()->search().query() : QString(); }
void AppController::setSearchQuery(const QString& query) {
    if (session()) {
        // A search handed over by the fuzzy search stays one while it is refined here (until it is cleared)
        session()->search().setQuery(query, true, session()->search().fuzzy() && !query.isEmpty());
    }
}
int AppController::searchHitCount() const {
    return session() ? session()->search().hitCount() : 0;
}
int AppController::searchCurrent() const { return session() ? session()->search().currentHit() + 1 : 0; }
bool AppController::searchRunning() const { return session() && session()->search().isRunning(); }
// --- several pages ---------------------------------------------------------------------------------------------

std::vector<size_t> AppController::pageList(const QList<int>& list) const {
    std::vector<size_t> result;
    if (list.isEmpty() && session()) {
        result.push_back(session()->getCurrentPageNo());
    }
    for (int p: list) {
        if (p >= 0) {
            result.push_back(static_cast<size_t>(p));
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool AppController::canUndoPages() const { return session() && session()->getPageUndoRedoHandler()->canUndo(); }
bool AppController::canRedoPages() const { return session() && session()->getPageUndoRedoHandler()->canRedo(); }
int AppController::copiedPages() const { return static_cast<int>(pageClipboard->size()); }

void AppController::copyPages(const QList<int>& list) {
    if (!session()) {
        return;
    }
    const auto indices = pageList(list);
    pageClipboard->copy(*session(), indices);
    Q_EMIT copiedPagesChanged();
    Q_EMIT pageActionDone(indices.size() == 1 ? tr("Page copied") : tr("%1 pages copied").arg(indices.size()), false);
}

void AppController::cutPages(const QList<int>& list) {
    copyPages(list);
    deletePages(list);
}

int AppController::pastePages(int position) {
    if (textPagesFixed()) {
        return 0;  // (a text file: its pages are its text)
    }
    if (!session() || pageClipboard->isEmpty()) {
        return 0;
    }
    if (position < 0) {
        const QList<int> sel = pages->selectedPages();
        position = (sel.isEmpty() ? static_cast<int>(session()->getCurrentPageNo()) : sel.last()) + 1;
    }
    bool keptIn = false;  // PDF pages from another PDF: in the document's merged PDF
    auto copies = pageClipboard->pagesFor(*session(), &keptIn);
    const int n = static_cast<int>(copies.size());
    session()->insertPages(copies, static_cast<size_t>(position));
    QList<int> pasted;
    for (int i = 0; i < n; ++i) {
        pasted.append(position + i);
    }
    pages->selectPages(pasted);
    QString note = n == 1 ? tr("Page pasted") : tr("%1 pages pasted").arg(n);
    if (keptIn) {
        // Once per paste: where the PDF pages went (a new file next to the document)
        const fs::path place = session()->mergedPdfPlace();  // (in the cache until it is saved)
        const QString where = QString::fromStdString(place.filename().string());
        if (place.empty()) {
            note = n == 1 ? tr("Page pasted. Its PDF text stays searchable: it is saved next to the document.")
                          : tr("%1 pages pasted. Their PDF text stays searchable: they are saved next to the document.")
                                    .arg(n);
        } else {
            note = n == 1 ? tr("Page pasted. Its PDF text stays searchable: it is saved in %1 next to the document.")
                                    .arg(where)
                          : tr("%1 pages pasted. Their PDF text stays searchable: they are saved in %2 next to the "
                               "document.")
                                    .arg(n)
                                    .arg(where);
        }
    }
    Q_EMIT pageActionDone(note, true);
    return n;
}

bool AppController::deletePages(const QList<int>& list) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    if (!session()) {
        return false;
    }
    const auto indices = pageList(list);
    if (indices.size() >= session()->getDocument()->getPageCount()) {
        Q_EMIT message(tr("Delete pages"), tr("A document keeps at least one page."), false);
        return false;
    }
    if (!session()->deletePages(indices)) {
        return false;
    }
    pages->clearSelection();
    Q_EMIT pageActionDone(indices.size() == 1 ? tr("Page deleted") : tr("%1 pages deleted").arg(indices.size()),
                          true);
    return true;
}

bool AppController::movePages(const QList<int>& list, int target) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    if (!session() || target < 0) {
        return false;
    }
    const auto indices = pageList(list);
    std::vector<PageRef> moved;
    {
        const auto order = session()->pageOrder();
        for (size_t i: indices) {
            if (i < order.size()) {
                moved.push_back(order[i]);
            }
        }
    }
    if (!session()->movePages(indices, static_cast<size_t>(target))) {
        return false;
    }
    // Keep them selected at their new place.
    const auto order = session()->pageOrder();
    QList<int> now;
    for (const auto& p: moved) {
        now.append(static_cast<int>(std::find(order.begin(), order.end(), p) - order.begin()));
    }
    pages->selectPages(now);
    Q_EMIT pageActionDone(moved.size() == 1 ? tr("Page moved") : tr("%1 pages moved").arg(moved.size()), true);
    return true;
}

void AppController::duplicatePages(const QList<int>& list) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (!session()) {
        return;
    }
    const auto indices = pageList(list);
    PageClipboard copies;  // (not the user's clipboard)
    copies.copy(*session(), indices, /*withPdf=*/false);
    auto newPages = copies.pagesFor(*session());
    const size_t position = indices.back() + 1;
    session()->insertPages(newPages, position);
    QList<int> added;
    for (size_t i = 0; i < newPages.size(); ++i) {
        added.append(static_cast<int>(position + i));
    }
    pages->selectPages(added);
}

void AppController::undoPages() {
    if (session() && session()->getPageUndoRedoHandler()->canUndo()) {
        session()->getPageUndoRedoHandler()->undo();
        pages->clearSelection();
    }
}

void AppController::redoPages() {
    if (session() && session()->getPageUndoRedoHandler()->canRedo()) {
        session()->getPageUndoRedoHandler()->redo();
        pages->clearSelection();
    }
}

int AppController::viewColumns() const { return std::max(1, app->getSettings()->getViewColumns()); }
bool AppController::pairedPages() const { return app->getSettings()->isShowPairedPages(); }
int AppController::pairsOffset() const { return app->getSettings()->getPairsOffset(); }

void AppController::setViewColumns(int columns) {
    columns = std::clamp(columns, 1, 8);  // upstream's view menu offers 1..8
    if (columns != viewColumns()) {
        app->getSettings()->setViewColumns(columns);
        Q_EMIT app->settingsChanged();  // the views lay out again
        Q_EMIT viewLayoutChanged();
    }
}
void AppController::setPairedPages(bool paired) {
    if (paired != pairedPages()) {
        app->getSettings()->setShowPairedPages(paired);
        Q_EMIT app->settingsChanged();
        Q_EMIT viewLayoutChanged();
    }
}
void AppController::setPairsOffset(int offset) {
    offset = std::max(0, offset);
    if (offset != pairsOffset()) {
        app->getSettings()->setPairsOffset(offset);
        Q_EMIT app->settingsChanged();
        Q_EMIT viewLayoutChanged();
    }
}

bool AppController::horizontalScrolling() const { return app->getSettings()->isViewFixedRows(); }
int AppController::viewRows() const { return std::max(1, app->getSettings()->getViewRows()); }
bool AppController::snapPages() const { return CanvasView::snapSetting(*app->getSettings()); }

void AppController::setHorizontalScrolling(bool on) {
    if (on != horizontalScrolling()) {
        // Upstream's "fixed rows", filled column by column (its vertical layout): the same pages side by side
        app->getSettings()->setViewFixedRows(on);
        app->getSettings()->setViewLayoutVert(on);
        Q_EMIT app->settingsChanged();
        Q_EMIT viewLayoutChanged();
    }
}
void AppController::setViewRows(int rows) {
    rows = std::clamp(rows, 1, 8);
    if (rows != viewRows()) {
        app->getSettings()->setViewRows(rows);
        Q_EMIT app->settingsChanged();
        Q_EMIT viewLayoutChanged();
    }
}
void AppController::setSnapPages(bool snap) {
    if (snap != snapPages()) {
        app->getSettings()->getCustomElement("xournalQt").setBool("snapPages", snap);  // (see CanvasView::snapSetting)
        app->getSettings()->customSettingsChanged();
        Q_EMIT app->settingsChanged();
        Q_EMIT viewLayoutChanged();
    }
}

void AppController::setPresenting(bool on) {
    if (on == presentingOn || (on && !canvas())) {
        return;
    }
    presentingOn = on;
    updatePresentedView();
    Q_EMIT presentingChanged();
}

void AppController::updatePresentedView() {
    CanvasView* wanted = presentingOn ? canvas() : nullptr;
    if (presentedView == wanted) {
        return;
    }
    if (presentedView) {
        presentedView->setPresenting(false);
    }
    presentedView = wanted;
    if (presentedView) {
        presentedView->setPresenting(true);
    }
}

xqt::CanvasView* AppController::keyCanvas() const {
    if (referenceMode->focused() && referenceMode->canvas()) {
        return referenceMode->canvas();  // (the reference has the keys)
    }
    return canvas();
}

void AppController::stepPage(int delta) {
    CanvasView* v = keyCanvas();
    if (!v || v->pageCount() == 0 || v->getViewController().stepPages(delta)) {
        return;
    }
    const auto page = static_cast<std::ptrdiff_t>(v->getSession().getCurrentPageNo()) + delta;
    showPage(static_cast<size_t>(std::clamp<std::ptrdiff_t>(page, 0, static_cast<std::ptrdiff_t>(v->pageCount()) - 1)));
}

void AppController::showPage(size_t page) {
    if (CanvasView* v = keyCanvas(); v && page < v->pageCount()) {
        v->getSession().setCurrentPageNo(page);
        v->getViewController().scrollToPage(page);
    }
}

void AppController::previousPage() { stepPage(-1); }
void AppController::nextPage() { stepPage(1); }
void AppController::firstPage() { showPage(0); }
void AppController::lastPage() {
    if (CanvasView* v = keyCanvas(); v && v->pageCount() > 0) {
        showPage(v->pageCount() - 1);
    }
}

int AppController::searchHitPageCount() const {
    return session() ? static_cast<int>(session()->search().pages().size()) : 0;
}
void AppController::searchNext() {
    if (session()) {
        session()->search().next();
    }
}
void AppController::searchPrevious() {
    if (session()) {
        session()->search().previous();
    }
}
void AppController::clearSearch() {
    if (session()) {
        session()->search().clear();
    }
}

void AppController::searchAllTabs(const QString& query) {
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->session(i)->search().setQuery(query, false, library->fuzzySearch());
    }
}

QVariantMap AppController::fuzzyName(const QString& query, const QString& name) const {
    if (query != fuzzyText || !fuzzyParsed) {
        fuzzyText = query;  // (parsed once per query, not per card)
        fuzzyParsed = std::make_shared<FuzzyQuery>(query);
    }
    const FuzzyQuery& q = *fuzzyParsed;
    if (!q.isValid()) {
        return {{"match", name.contains(query.trimmed(), Qt::CaseInsensitive)}, {"marks", QVariantList()}};
    }
    const FuzzyQuery::NameMatch m = q.matchName(name);
    QVariantList marks;
    for (const int p: m.positions) {
        marks.append(p);
    }
    return {{"match", q.evaluate([&](size_t t) { return m.found[t] != 0; })}, {"marks", marks}};
}

QString AppController::fuzzyHint(const QString& query) const {
    return query.trimmed().isEmpty() ? QString() : FuzzyQuery(query).hint();
}

void AppController::openSearchResult(int index) {
    tabs->setCurrentIndex(index);
    if (DocumentSession* s = session(); s && !s->search().query().isEmpty()) {
        s->search().jumpToFirstFromCurrentPage();
    }
}

void AppController::openSearchResultAt(int index, int page) {
    tabs->setCurrentIndex(index);
    if (DocumentSession* s = session(); s && page >= 0 && static_cast<size_t>(page) < s->getDocument()->getPageCount()) {
        s->setCurrentPageNo(static_cast<size_t>(page));
        if (!s->search().query().isEmpty()) {
            s->search().jumpToFirstFromCurrentPage();
        }
    }
}

int AppController::titlePage() const {
    DocumentSession* s = session();
    const fs::path file = s ? s->documentFile() : fs::path();  // (a PDF without a .xopp as well)
    return file.empty() ? -1 : DocumentPlaces::titlePage(DocumentPlaces::keyOf(file));
}

bool AppController::setTitlePage(int page) {
    DocumentSession* s = session();
    const fs::path file = s ? s->documentFile() : fs::path();
    if (file.empty() || page < 0 || static_cast<size_t>(page) >= s->getDocument()->getPageCount()) {
        return false;
    }
    DocumentPlaces::setTitlePage(DocumentPlaces::keyOf(file), page);
    // Its previews show that page now (they have new names, so they are drawn anew)
    library->refresh();
    recent->refresh();
    tabs->thumbnailChanged(s);
    Q_EMIT titlePageChanged();
    Q_EMIT pageActionDone(tr("Page %1 is the title page now").arg(page + 1), false);
    return true;
}

QObject* AppController::tabsModel() const { return tabs.get(); }
QObject* AppController::pagesModel() const { return pages.get(); }
QObject* AppController::settingsModel() const { return settingsView; }
QObject* AppController::libraryModel() const { return library; }
QObject* AppController::recentModel() const { return recent; }
bool AppController::homeVisible() const { return home || tabs->count() == 0; }
void AppController::setHomeVisible(bool visible) {
    if (visible != home) {
        home = visible;
        if (home) {
            recent->refresh();  // documents may have been deleted or moved meanwhile
            library->placesChanged();  // documents were read meanwhile: when, and to which page
        }
        Q_EMIT homeVisibleChanged();
    }
}
QObject* AppController::filteredPagesModel() const { return filteredPages.get(); }
QObject* AppController::outlineModel() const { return outline.get(); }
QObject* AppController::layersModel() const { return layers.get(); }
QObject* AppController::shortcutsModel() const { return shortcuts; }
int AppController::currentTab() const { return tabs->currentIndex(); }
void AppController::setCurrentTab(int index) {
    tabs->setCurrentIndex(index);
    if (tabs->session(index)) {
        setHomeVisible(false);
    }
}
QObject* AppController::view() const { return canvas(); }

QString AppController::title() const {
    return session() ? QString::fromStdString(session()->getDisplayName()) : QString();
}
bool AppController::modified() const { return session() && session()->isModified(); }
bool AppController::saving() const { return session() && session()->isSaving(); }
bool AppController::anySaving() const { return tabs->anySaving(); }
bool AppController::hasFilePath() const { return session() && session()->hasFilePath(); }

QString AppController::shownFileNote() const {
    const fs::path file = session() && !session()->hasFilePath() ? session()->shownFile() : fs::path();
    if (file.empty()) {
        return {};
    }
    const QString name = QString::fromStdString(file.filename().string());
    if (const TextFile* text = session()->textFile()) {
        if (session()->isEditableText()) {
            return {};  // (edited: nothing to say)
        }
        const QString why = text->isTooBig() ? tr("it is bigger than %1 MB").arg(TextFile::MAX_EDIT_BYTES / (1024 * 1024))
                            : !text->isUtf8() ? tr("it is not UTF-8 text")
                                              : tr("the file cannot be written");
        std::error_code ec;
        const bool cut = fs::file_size(file, ec) > MarkdownFile::MAX_BYTES && !ec;
        return tr("Read-only: %1 is shown to read and search, as %2.").arg(name, why) +
               (cut ? ' ' + tr("Only its first %1 MB are shown.").arg(MarkdownFile::MAX_BYTES / (1024 * 1024))
                    : QString());
    }
    if (DocumentFiles::isTextFile(file)) {
        std::error_code ec;
        const bool cut = fs::file_size(file, ec) > MarkdownFile::MAX_BYTES && !ec;
        return tr("Read-only: %1 is shown to read and search. \"Edit anyway\" edits it here as plain text; \"Open "
                  "externally\" opens it in its app.")
                       .arg(name) +
               (cut ? ' ' + tr("Only its first %1 MB are shown.").arg(MarkdownFile::MAX_BYTES / (1024 * 1024))
                    : QString());
    }
    fs::path xopp = file;
    xopp.replace_extension(".xopp");
    return tr("%1 is the background of this new page. Saving keeps what you write as %2 next to it.")
            .arg(name, QString::fromStdString(xopp.filename().string()));
}
bool AppController::canUndo() const { return session() && session()->getUndoRedoHandler()->canUndo(); }
bool AppController::canRedo() const { return session() && session()->getUndoRedoHandler()->canRedo(); }

QString AppController::tool() const {
    // Upstream's tool names (pen, highlighter, eraser, hand, selectRect, selectRegion, text, image, ...).
    return QString::fromUtf8(toolTypeToString(app->getToolHandler()->getToolType()).data());
}

QString AppController::drawingType() const {
    return QString::fromUtf8(drawingTypeToString(app->getToolHandler()->getDrawingType()).data());
}

void AppController::setDrawingType(const QString& name) {
    const DrawingType type = drawingTypeFromString(name.toStdString());
    ToolHandler* th = app->getToolHandler();
    if (th->getToolType() != TOOL_PEN && th->getToolType() != TOOL_HIGHLIGHTER) {
        th->selectTool(TOOL_PEN);  // shapes are drawn with the pen (or the highlighter)
    }
    th->setDrawingType(type == DRAWING_TYPE_DONT_CHANGE ? DRAWING_TYPE_DEFAULT : type);
    th->fireToolChanged();
    Q_EMIT toolChanged();
}

QColor AppController::color() const { return toQColor(app->getToolHandler()->getColor()); }
int AppController::size() const {
    ToolHandler* th = app->getToolHandler();
    return th->isCustomThicknessActive() ? 5 : static_cast<int>(th->getSize());
}

namespace {
const char* const CUSTOM = "xournalQt";  // our settings (in upstream's settings file)
/// The tools with an own width, and its default (points)
constexpr std::array<std::pair<ToolType, double>, 3> CUSTOM_WIDTH_TOOLS{
        {{TOOL_PEN, 8.5}, {TOOL_HIGHLIGHTER, 42.5}, {TOOL_ERASER, 28.35}}};  // 3, 15, 10 mm
}  // namespace

double AppController::customWidth() const {
    ToolHandler* th = app->getToolHandler();
    return th->getCustomThickness(th->getToolType());
}

void AppController::setCustomWidth(double points) {
    ToolHandler* th = app->getToolHandler();
    const ToolType type = th->getToolType();
    if (th->getCustomThickness(type) <= 0) {
        return;  // (no sizes)
    }
    th->setCustomThickness(type, std::clamp(points, 0.1, 150.0), true);
    storeCustomWidths();
    Q_EMIT toolChanged();
}

double AppController::sizeWidth(int s) const {
    ToolHandler* th = app->getToolHandler();
    const ToolType type = th->getToolType();
    if (s == 5) {
        return th->getCustomThickness(type);
    }
    const bool sized = std::any_of(CUSTOM_WIDTH_TOOLS.begin(), CUSTOM_WIDTH_TOOLS.end(),
                                   [type](const auto& t) { return t.first == type; });
    return sized && s >= 0 && s < 5 ? th->getToolThickness(type)[s] : 0;
}

void AppController::loadCustomWidths() {
    ToolHandler* th = app->getToolHandler();
    std::string stored;
    app->getSettings()->getCustomElement(CUSTOM).getString("customWidths", stored);
    std::map<std::string, std::pair<double, bool>> read;
    for (const QString& entry: QString::fromStdString(stored).split(',', Qt::SkipEmptyParts)) {
        QString value = entry.section('=', 1);
        const bool active = value.endsWith('*');
        bool ok = false;
        const double width = value.remove('*').toDouble(&ok);
        if (ok && width > 0) {
            read[entry.section('=', 0, 0).trimmed().toStdString()] = {width, active};
        }
    }
    for (const auto& [type, width]: CUSTOM_WIDTH_TOOLS) {
        const auto it = read.find(std::string(toolTypeToString(type)));
        if (it == read.end()) {
            th->setCustomThickness(type, width, false);
        } else {
            th->setCustomThickness(type, it->second.first, it->second.second);
        }
    }
}

void AppController::storeCustomWidths() {
    ToolHandler* th = app->getToolHandler();
    QStringList entries;
    for (const auto& [type, width]: CUSTOM_WIDTH_TOOLS) {
        const bool active = th->isCustomThicknessActive(type);
        entries << QString("%1=%2%3")
                           .arg(QString::fromUtf8(toolTypeToString(type).data()))
                           .arg(th->getCustomThickness(type))
                           .arg(active ? "*" : "");
    }
    app->getSettings()->getCustomElement(CUSTOM).setString("customWidths", entries.join(',').toStdString());
    app->getSettings()->customSettingsChanged();
}

QVariantList AppController::palette() const {
    QVariantList list;
    for (size_t i = 0; i < colors->size(); ++i) {
        list.append(toQColor(colors->getColorAt(i).getColor()));
    }
    return list;
}

QVariantList AppController::defaultToolbarColors() const {
    // Upstream's palette (black, green, light blue, light green, blue, gray, red, magenta, orange, yellow), not white
    QVariantList list;
    for (const QVariant& c: palette()) {
        if (c.value<QColor>() != QColor(Qt::white)) {
            list.append(c);
        }
    }
    return list;
}

QVariantList AppController::toolbarColors() const {
    std::string stored;
    QVariantList list;
    if (app->getSettings()->getCustomElement(CUSTOM).getString("toolbarColors", stored)) {
        for (const QString& c: QString::fromStdString(stored).split(',', Qt::SkipEmptyParts)) {
            if (const QColor color(c.trimmed()); color.isValid()) {
                list.append(color);
            }
        }
        return list;
    }
    return defaultToolbarColors();
}

void AppController::storeToolbarColors(const QVariantList& list) {
    QStringList names;
    for (const QVariant& c: list) {
        names << c.value<QColor>().name();
    }
    app->getSettings()->getCustomElement(CUSTOM).setString("toolbarColors", names.join(',').toStdString());
    app->getSettings()->customSettingsChanged();
    Q_EMIT toolbarColorsChanged();
}

void AppController::addToolbarColor(const QColor& color) {
    QVariantList list = toolbarColors();
    for (const QVariant& c: list) {
        if (c.value<QColor>().rgb() == color.rgb()) {
            return;
        }
    }
    list.append(QColor(color.rgb()));
    storeToolbarColors(list);
}

void AppController::removeToolbarColor(int index) {
    QVariantList list = toolbarColors();
    if (index >= 0 && index < list.size()) {
        list.removeAt(index);
        storeToolbarColors(list);
    }
}

void AppController::resetToolbarColors() { storeToolbarColors(defaultToolbarColors()); }

bool AppController::textMarkdown() const {
    bool on = false;
    app->getSettings()->getCustomElement(CUSTOM).getBool("textMarkdown", on);
    return on;
}

void AppController::setTextMarkdown(bool on) {
    if (on != textMarkdown()) {
        app->getSettings()->getCustomElement(CUSTOM).setBool("textMarkdown", on);
        app->getSettings()->customSettingsChanged();
        applyMarkdownText();
        Q_EMIT fontChanged();
    }
}

double AppController::markdownFontSize() const {
    double size = 0;
    app->getSettings()->getCustomElement(CUSTOM).getDouble("markdownFontSize", size);
    return size > 0 ? size : md::defaultFontSize(fontSize());
}

void AppController::setMarkdownFontSize(double size) {
    size = std::clamp(size, 4.0, 400.0);
    app->getSettings()->getCustomElement(CUSTOM).setDouble("markdownFontSize", size);
    app->getSettings()->customSettingsChanged();
    if (canvas() && canvas()->getTextEditor() && canvas()->getTextEditor()->isMarkdown()) {
        canvas()->getTextEditor()->setFont(XojFont(fontFamily().toStdString(), size));  // the text being edited
    }
    applyMarkdownText();
    Q_EMIT fontChanged();
}

double AppController::markdownBoxSize() const { return markdownActive() ? markdown->fontSize() : markdownFontSize(); }

bool AppController::markdownInPanel() const {
    bool on = false;  // (written on the page, formatted while typing)
    app->getSettings()->getCustomElement(CUSTOM).getBool("markdownInPanel", on);
    return on;
}

void AppController::setMarkdownInPanel(bool on) {
    if (on != markdownInPanel()) {
        app->getSettings()->getCustomElement(CUSTOM).setBool("markdownInPanel", on);
        app->getSettings()->customSettingsChanged();
        applyMarkdownText();
        Q_EMIT fontChanged();
    }
}

bool AppController::markdownIsPageText() const { return !markdownActive() || markdown->isPageText(); }

void AppController::setMarkdownBoxSize(double size) {
    if (markdownActive()) {
        markdownPagesChanged(markdown->setFontSize(std::clamp(size, 4.0, 400.0)));
    }
    setMarkdownFontSize(size);
    Q_EMIT markdownChanged();
}

void AppController::applyMarkdownText() {
    // (the notes, and the reference beside them: it may be written in)
    for (CanvasView* v: {canvas(), referenceMode ? referenceMode->canvas() : nullptr}) {
        if (v) {
            v->setMarkdownText(textMarkdown(), markdownFontSize(), markdownInPanel());
        }
    }
}

QString AppController::toolbarPosition() const {
    std::string stored;
    app->getSettings()->getCustomElement(CUSTOM).getString("toolbarPosition", stored);
    return stored == "left" || stored == "right" ? QString::fromStdString(stored) : QStringLiteral("top");
}

void AppController::setToolbarPosition(const QString& position) {
    if (position != toolbarPosition() && (position == "top" || position == "left" || position == "right")) {
        app->getSettings()->getCustomElement(CUSTOM).setString("toolbarPosition", position.toStdString());
        app->getSettings()->customSettingsChanged();
        Q_EMIT toolbarPositionChanged();
    }
}

bool AppController::toolbarHidden() const {
    bool hidden = false;
    app->getSettings()->getCustomElement(CUSTOM).getBool("toolbarHidden", hidden);
    return hidden;
}

void AppController::setToolbarHidden(bool hidden) {
    if (hidden != toolbarHidden()) {
        app->getSettings()->getCustomElement(CUSTOM).setBool("toolbarHidden", hidden);
        app->getSettings()->customSettingsChanged();
        Q_EMIT toolbarPositionChanged();
    }
}

QVariantList AppController::penColors() const {
    std::string stored;
    QVariantList list;
    if (app->getSettings()->getCustomElement(CUSTOM).getString("penColors", stored)) {
        for (const QString& name: QString::fromStdString(stored).split(',', Qt::SkipEmptyParts)) {
            if (const QColor color(name.trimmed()); color.isValid()) {
                list.append(color);
            }
        }
        return list;
    }
    return {QColor(Qt::black), QColor(0xff, 0x00, 0x00), QColor(0x31, 0x71, 0xd8)};  // black, red, blue
}

namespace {
void storePenColors(Settings* settings, const QVariantList& list) {
    QStringList names;
    for (const QVariant& c: list) {
        names << c.value<QColor>().name();
    }
    settings->getCustomElement("xournalQt").setString("penColors", names.join(',').toStdString());
    settings->customSettingsChanged();
}
}  // namespace

void AppController::addPenColor(const QColor& color) {
    QVariantList list = penColors();
    for (const QVariant& c: list) {
        if (c.value<QColor>().rgb() == color.rgb()) {
            return;
        }
    }
    list.append(QColor(color.rgb()));
    storePenColors(app->getSettings(), list);
    Q_EMIT penPillChanged();
}

void AppController::removePenColor(int index) {
    QVariantList list = penColors();
    if (index >= 0 && index < list.size() && list.size() > 1) {
        list.removeAt(index);
        storePenColors(app->getSettings(), list);
        Q_EMIT penPillChanged();
    }
}

void AppController::resetPenColors() {
    app->getSettings()->getCustomElement(CUSTOM).setString("penColors", std::string());
    app->getSettings()->customSettingsChanged();
    Q_EMIT penPillChanged();
}

QString AppController::penPillSide() const {
    std::string stored;
    app->getSettings()->getCustomElement(CUSTOM).getString("penPillSide", stored);
    return stored == "left" || stored == "top" || stored == "bottom" ? QString::fromStdString(stored)
                                                                     : QStringLiteral("right");
}

void AppController::setPenPillSide(const QString& side) {
    if (side != penPillSide() && (side == "left" || side == "right" || side == "top" || side == "bottom")) {
        app->getSettings()->getCustomElement(CUSTOM).setString("penPillSide", side.toStdString());
        app->getSettings()->customSettingsChanged();
        Q_EMIT penPillChanged();
    }
}

double AppController::penPillOffset() const {
    double offset = 0.35;
    app->getSettings()->getCustomElement(CUSTOM).getDouble("penPillOffset", offset);
    return std::clamp(offset, 0.0, 1.0);
}

void AppController::setPenPillOffset(double offset) {
    app->getSettings()->getCustomElement(CUSTOM).setDouble("penPillOffset", std::clamp(offset, 0.0, 1.0));
    app->getSettings()->customSettingsChanged();
    Q_EMIT penPillChanged();
}

QVariantList AppController::pdfHighlightColors() const {
    // Yellow (upstream's highlighter), green, pink
    return {QColor(0xff, 0xff, 0x00), QColor(0x7c, 0xfc, 0x3c), QColor(0xff, 0x80, 0xc0)};
}

QColor AppController::pdfHighlightColor() const {
    std::string stored;
    if (app->getSettings()->getCustomElement(CUSTOM).getString("pdfHighlightColor", stored)) {
        if (const QColor c(QString::fromStdString(stored)); c.isValid()) {
            return c;
        }
    }
    return pdfHighlightColors().value(0).value<QColor>();
}

void AppController::setPdfHighlightColor(const QColor& color) {
    if (!color.isValid() || color.rgb() == pdfHighlightColor().rgb()) {
        return;
    }
    app->getSettings()->getCustomElement(CUSTOM).setString("pdfHighlightColor", color.name().toStdString());
    app->getSettings()->customSettingsChanged();
    applyPdfTextMode();
    Q_EMIT pdfTextModeChanged();
}

int AppController::zoomPercent() const {
    if (!canvas()) {
        return 100;
    }
    const auto& vc = canvas()->getViewController();
    return static_cast<int>(std::lround(vc.zoom() / vc.zoom100() * 100.0));
}

int AppController::pageNumber() const { return session() ? static_cast<int>(session()->getCurrentPageNo()) + 1 : 0; }
int AppController::pageCount() const { return canvas() ? static_cast<int>(canvas()->pageCount()) : 0; }

void AppController::newDocument() {
    tabs->addTab(std::make_unique<DocumentSession>(*app));
    setHomeVisible(false);
}

void AppController::setLibraryRoot(const fs::path& root) {
    auto lib = std::make_unique<Library>(root);
    journalFile = journalFileFor(*lib);
    // A folder opened as a library outside the standard folder: in the Recent grid, to find it again
    if (!lib->isInLibrariesFolder()) {
        recent->addLibrary(lib->root());
    }
    library->setLibrary(std::move(lib));
}

fs::path AppController::journalFileFor(const Library& lib) {
    if (lib.isDefault()) {
        return SessionRecovery::defaultJournalFile();
    }
    const fs::path file = SessionRecovery::defaultJournalFile().parent_path() / "sessions" / (lib.key() + ".json");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return file;
}

bool AppController::createDocument(const QString& name, bool inLibrary) {
    // The page template settings (background, size) are what the dialog changed.
    auto s = std::make_unique<DocumentSession>(*app);
    DocumentSession* created = s.get();
    tabs->addTab(std::move(s));
    setHomeVisible(false);
    if (!inLibrary || !library->available()) {
        return true;
    }
    const QString path = library->newDocumentPath(name);
    auto r = created->saveAs(fs::path(path.toStdString()));
    if (!r.ok) {
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
        return false;
    }
    recent->add(created->getFilePath());
    library->refresh();
    Q_EMIT titleChanged();
    return true;
}

namespace {
/// A file the home screen lists that the app does not open itself (an Office file, ...)
bool isOtherFile(const QString& path) {
    const fs::path file(path.toStdString());
    std::error_code ec;
    return DocumentFiles::isOtherFile(file) && fs::is_regular_file(file, ec);
}
}  // namespace

bool AppController::openSearchHit(const QString& path, const QString& query) {
    if (isOtherFile(path)) {
        return openWithSystemApp(path);  // (found by its name)
    }
    if (!openPath(path)) {
        return false;
    }
    if (session() && !query.trimmed().isEmpty()) {
        session()->setCurrentPageNo(0);
        session()->search().setQuery(query, true, library->fuzzySearch());  // shows the first hit
    }
    return true;
}

bool AppController::openSearchHitAt(const QString& path, const QString& query, int page) {
    if (!openPath(path) || !session()) {
        return false;
    }
    DocumentSession* s = session();
    const size_t count = s->getDocument()->getPageCount();
    const size_t p = std::min<size_t>(static_cast<size_t>(std::max(0, page)), count > 0 ? count - 1 : 0);
    s->setCurrentPageNo(p);
    s->getScrollHandler()->scrollToPage(p);  // right away; the hit follows when the search found it
    if (!query.trimmed().isEmpty()) {
        if (s->search().query() == query && s->search().fuzzy() == library->fuzzySearch()) {
            s->search().jumpToFirstFromCurrentPage();
        } else {
            s->search().setQuery(query, true, library->fuzzySearch());  // current: the first hit from this page on
        }
    }
    return true;
}

bool AppController::openSearchHitInPassage(const QString& path, const QString& query, int passage) {
    if (!openPath(path) || !session()) {
        return false;
    }
    DocumentSession* s = session();
    // The passage in the text the document shows (read as it was, parsed as the index does), and its page
    const std::string source = MarkdownFile::read(fs::path(path.toStdString()));
    const std::vector<md::Passage> passages = md::passages(md::parse(source));
    if (passage < 0 || static_cast<size_t>(passage) >= passages.size()) {
        return openSearchHit(path, query);
    }
    const md::Passage& target = passages[static_cast<size_t>(passage)];
    std::vector<size_t> starts = MarkdownFile::pageStarts(*s->getDocument());
    if (starts.empty()) {
        starts.push_back(0);
    }
    size_t page = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (starts[i] <= target.begin) {
            page = i;
        }
    }
    // The hits on that page before the passage: its first hit is the one after them
    const auto terms = FuzzyQuery::textTerms(LibraryIndex::simplified(query), library->fuzzySearch());
    int before = 0;
    for (size_t i = 0; i < static_cast<size_t>(passage); ++i) {
        if (passages[i].begin != md::NO_SOURCE && passages[i].begin >= starts[page]) {
            before += textmatch::count(LibraryIndex::simplified(QString::fromStdString(passages[i].text)), terms);
        }
    }
    s->setCurrentPageNo(page);
    s->getScrollHandler()->scrollToPage(page);  // right away; the hit follows when the search found it
    if (!query.trimmed().isEmpty()) {
        if (s->search().query() != query || s->search().fuzzy() != library->fuzzySearch()) {
            s->search().setQuery(query, false, library->fuzzySearch());
        }
        s->search().jumpToHit(page, before);
    }
    return true;
}

QVariantList AppController::libraries() const {
    QVariantList list;
    const fs::path current = library->library() ? library->library()->root() : fs::path();
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (auto it = fs::directory_iterator(Library::librariesFolder(), ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        if (it->is_directory() && it->path().filename().string().front() != '.') {
            dirs.push_back(it->path());
        }
    }
    if (!current.empty() && std::find(dirs.begin(), dirs.end(), current) == dirs.end()) {
        dirs.push_back(current);  // a folder opened as library
    }
    std::sort(dirs.begin(), dirs.end());
    // The Downloads folder: a quick library (all downloaded papers at once), last
    const fs::path downloads = Library(Library::downloadsFolder()).root();
    std::erase_if(dirs, [&](const fs::path& d) { return Library(d).root() == downloads; });
    if (fs::is_directory(downloads, ec)) {
        dirs.push_back(downloads);
    }
    for (const auto& d: dirs) {
        const bool isDownloads = Library(d).root() == downloads;
        list.append(QVariantMap{{"name", isDownloads ? tr("Downloads") : QString::fromStdString(d.filename().string())},
                                {"path", QString::fromStdString(d.string())},
                                {"current", Library(d).root() == current},
                                {"downloads", isDownloads}});
    }
    return list;
}

void AppController::openLibrary(const QUrl& folder) {
    const QString dir = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (library->library() && Library(fs::path(dir.toStdString())).root() == library->library()->root()) {
        setHomeVisible(true);  // this one
        return;
    }
    // One library per window: another process (it becomes the single instance of that library).
    SystemApps::instance().startLibraryWindow(dir);
}

bool AppController::createLibrary(const QString& name) {
    const std::string n = name.trimmed().toStdString();
    if (!DocumentFiles::validName(n)) {
        return false;
    }
    const fs::path dir = Library::librariesFolder() / n;
    std::error_code ec;
    if (fs::exists(dir, ec) || !fs::create_directories(dir, ec)) {
        return false;
    }
    openLibrary(QUrl::fromLocalFile(QString::fromStdString(dir.string())));
    return true;
}

void AppController::showInFileManager(const QString& path) { SystemApps::instance().showInFileManager(path); }

bool AppController::canShowInFileManager() const { return SystemApps::canShowInFileManager(); }

bool AppController::openWithSystemApp(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return false;
    }
    if (!SystemApps::instance().openWithSystemApp(path)) {
        Q_EMIT message(tr("Cannot open file"), tr("No app is set up to open %1.").arg(QFileInfo(path).fileName()), true);
        return false;
    }
    return true;
}

void AppController::openListed(const QStringList& paths) {
    for (const QString& p: paths) {
        if (isOtherFile(p)) {
            openWithSystemApp(p);
        } else {
            openPath(p);
        }
    }
}

void AppController::filesChanged(const DocumentFiles::Result& r) {
    for (const auto& [from, to]: r.moved) {
        for (int i = 0; i < tabs->count(); ++i) {
            DocumentSession* s = tabs->session(i);
            const fs::path file = s->hasFilePath() ? s->getFilePath() : fs::path();
            const fs::path pdf = s->getDocument()->getPdfFilepath();
            const fs::path newFile = file.empty() ? file : DocumentFiles::remap(file, from, to);
            const fs::path newPdf = pdf.empty() ? pdf : DocumentFiles::remap(pdf, from, to);
            if (newFile != file || newPdf != pdf) {
                s->relocate(newFile != file ? newFile : fs::path(), newPdf != pdf ? newPdf : fs::path());
            }
        }
        recent->remap(from, to);
    }
    recent->refresh();
    Q_EMIT titleChanged();
}

void AppController::startSession(const QStringList& files) {
    recovery = std::make_unique<SessionRecovery>(*tabs, journalFile);
    const auto& previous = recovery->previous();
    if (!recovery->candidates().empty()) {
        // Ask first (QML shows recoveryItems); the previous tabs are reopened by recover().
        recoveryPending = true;
        Q_EMIT recoveryChanged();
        for (const QString& f: files) {
            openPath(f);
        }
        return;
    }
    if (previous && (previous->clean || recovery->previousCrashed()) && settingsView->get("restoreSession").toBool()) {
        std::vector<std::pair<fs::path, int>> last;
        for (const auto& t: previous->tabs) {
            last.emplace_back(t.file, t.page);
        }
        reopenTabs(last, previous->current, {});
    }
    for (const QString& f: files) {
        openPath(f);
    }
    recovery->start();
}

QVariantList AppController::recoveryItems() const {
    QVariantList items;
    if (!recoveryPending || !recovery) {
        return items;
    }
    for (const auto& c: recovery->candidates()) {
        const QString name = c.originalFile.empty() ? tr("Unsaved document")
                                                    : QString::fromStdString(c.originalFile.filename().string());
        items.append(QVariantMap{{"title", name}, {"time", c.time.toString("yyyy-MM-dd hh:mm")}});
    }
    return items;
}

void AppController::recover(bool accept) {
    if (!recoveryPending || !recovery || !recovery->previous()) {
        return;
    }
    const auto& previous = *recovery->previous();
    const auto candidates = recovery->candidates();
    std::map<size_t, std::pair<fs::path, fs::path>> recovered;
    if (accept) {
        for (const auto& c: candidates) {
            recovered[c.tab] = {c.recoveryFile, c.originalFile};
        }
    }
    std::vector<std::pair<fs::path, int>> last;
    for (const auto& t: previous.tabs) {
        last.emplace_back(t.file, t.page);
    }
    reopenTabs(last, previous.current, recovered);
    // The recovered content is in the tabs now (unsaved), or was discarded.
    for (const auto& c: candidates) {
        std::error_code ec;
        if (!accept) {
            // With it goes a merged PDF of pasted pages in the cache that it used (never saved anywhere else)
            if (auto r = DocumentSession::loadFile(c.recoveryFile); r.document) {
                if (const fs::path pdf = r.document->getPdfFilepath(); MergedPdf::inCache(pdf)) {
                    r.document.reset();
                    fs::remove(pdf, ec);
                }
            }
        }
        fs::remove(c.recoveryFile, ec);
    }
    recoveryPending = false;
    Q_EMIT recoveryChanged();
    recovery->start();
}

void AppController::reopenTabs(const std::vector<std::pair<fs::path, int>>& last, int current,
                               const std::map<size_t, std::pair<fs::path, fs::path>>& recovered) {
    int currentTabAfter = -1;
    for (size_t i = 0; i < last.size(); ++i) {
        const auto& [file, page] = last[i];
        bool opened = false;
        if (auto it = recovered.find(i); it != recovered.end() && it->second.first.extension() == ".text") {
            // A text file: opened as itself, with the text it had (unsaved)
            std::ifstream in(it->second.first, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::error_code ec;
            if (in && fs::exists(file, ec) && openPath(QString::fromStdString(file.string())) &&
                tabs->currentSession()->isEditableText()) {
                MarkdownFile::setText(*tabs->currentSession(), text);
                opened = true;
            }
        } else if (auto it = recovered.find(i); it != recovered.end()) {
            auto result = DocumentSession::loadFile(it->second.first);
            if (result.document) {
                const int pristine = tabs->isPristine(tabs->currentIndex()) ? tabs->currentIndex() : -1;
                tabs->addTab(std::make_unique<DocumentSession>(*app, std::move(result.document)));
                tabs->currentSession()->markRecovered(it->second.second);
                if (pristine >= 0) {
                    tabs->closeTab(pristine);
                }
                opened = true;
            }
        }
        if (!opened && !file.empty()) {
            std::error_code ec;
            opened = fs::exists(file, ec) && openPath(QString::fromStdString(file.string()));
        }
        if (opened) {
            DocumentSession* s = tabs->currentSession();
            const size_t p = std::min<size_t>(static_cast<size_t>(std::max(0, page)),
                                              s->getDocument()->getPageCount() - 1);
            s->setCurrentPageNo(p);
            s->getScrollHandler()->scrollToPage(p);
            if (static_cast<int>(i) == current) {
                currentTabAfter = tabs->currentIndex();
            }
        }
    }
    if (currentTabAfter >= 0) {
        tabs->setCurrentIndex(currentTabAfter);
    }
    if (tabs->count() > 0) {
        setHomeVisible(false);
    }
}

void AppController::openPaths(const QStringList& paths) {
    for (const QString& p: paths) {
        openPath(p);
    }
    Q_EMIT raiseRequested();
}

void AppController::openUrls(const QList<QUrl>& urls) {
    for (const QUrl& u: urls) {
        openPath(u.toLocalFile());
    }
}

QObject* AppController::referenceObject() const { return referenceMode.get(); }

bool AppController::openAsReference(const QString& path) {
    DocumentSession* main = session();
    if (!main) {
        return openPath(path);  // nothing to show it beside
    }
    int index = tabs->indexOfFile(fs::path(path.toStdString()));
    if (index < 0) {
        replacePristine = false;  // (the new document is for the notes)
        const bool opened = openPath(path);
        replacePristine = true;
        if (!opened) {
            tabs->setCurrentIndex(tabs->indexOf(main));
            return false;
        }
        index = tabs->currentIndex();  // (the opened document)
    }
    tabs->setCurrentIndex(tabs->indexOf(main));
    referenceMode->showTab(index);  // (not beside itself)
    setHomeVisible(false);
    return true;
}

void AppController::closeTab(int index) {
    if (flow && flowSession == tabs->session(index)) {
        endTextFlow(true);
    }
    if (markdown && mdSession == tabs->session(index)) {
        endMarkdown(true);
    }
    tabs->closeTab(index);  // the last one: the home screen
}

void AppController::moveTab(int from, int to) { tabs->moveTab(from, to); }

void AppController::nextTab() {
    if (home && tabs->count() > 0) {
        setHomeVisible(false);  // from the home screen to the document behind it
    } else if (tabs->count() > 1) {
        tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
    }
}

void AppController::previousTab() {
    if (home && tabs->count() > 0) {
        setHomeVisible(false);
    } else if (tabs->count() > 1) {
        tabs->setCurrentIndex((tabs->currentIndex() + tabs->count() - 1) % tabs->count());
    }
}

int AppController::tabCount() const { return tabs->count(); }
bool AppController::tabModified(int index) const { return tabs->session(index) && tabs->session(index)->isModified(); }

QString AppController::tabTitle(int index) const {
    return tabs->session(index) ? QString::fromStdString(tabs->session(index)->getDisplayName()) : QString();
}

bool AppController::tabSaving(int index) const { return tabs->session(index) && tabs->session(index)->isSaving(); }

void AppController::whenSaved(int index, const QJSValue& then) {
    DocumentSession* s = tabs->session(index);
    if (!s || !s->isSaving()) {
        QTimer::singleShot(0, this, [then, index]() mutable { then.call({index}); });
        return;
    }
    QPointer<DocumentSession> guard(s);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(s, &DocumentSession::savingChanged, this, [this, guard, then, connection](bool saving) {
        if (saving) {
            return;
        }
        disconnect(*connection);
        // (from the event loop: the call may close the tab)
        QTimer::singleShot(0, this, [this, guard, then]() mutable {
            if (const int i = guard ? tabs->indexOf(guard) : -1; i >= 0) {
                then.call({i});
            }
        });
    });
}

void AppController::whenAllSaved(const QJSValue& then) {
    if (!tabs->anySaving()) {
        QTimer::singleShot(0, this, [then]() mutable { then.call(); });
        return;
    }
    whenAllSavedCalls.push_back(then);
}

QVariantList AppController::modifiedTabs() const {
    QVariantList list;
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->session(i)->isModified()) {
            list.append(i);
        }
    }
    return list;
}

namespace {
/// A new document made from a Markdown file (MarkdownFile.h), a text file (as plain text) or an image (ImageFile.h):
/// it shows the file and is never written back to it.
DocumentSession::LoadResult loadShownFile(const fs::path& file) {
    DocumentSession::LoadResult result;
    if (DocumentFiles::isMarkdownFile(file) || DocumentFiles::isTextFile(file)) {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) {
            result.error =
                    AppController::tr("\"%1\" cannot be read.").arg(QString::fromStdString(file.string())).toStdString();
            return result;
        }
        result.document = MarkdownFile::document(DocumentFiles::isTextFile(file) ? MarkdownFile::readAsPlainText(file)
                                                                                 : MarkdownFile::read(file));
        return result;
    }
    if (DocumentFiles::isImageFile(file)) {
        result.document = ImageFile::document(file, result.error);
        return result;
    }
    result.error =
            AppController::tr("\"%1\" cannot be opened.").arg(QString::fromStdString(file.string())).toStdString();
    return result;
}
}  // namespace

bool AppController::openFile(const QUrl& url) { return openPath(url.toLocalFile()); }

bool AppController::openPath(const QString& path) {
    const fs::path file(path.toStdString());
    if (int existing = tabs->indexOfFile(file); existing >= 0) {
        tabs->setCurrentIndex(existing);  // already open: show it
        setHomeVisible(false);
        return true;
    }
    // A Markdown file, an image: a new document made from it (the file is not written); an image with its .xopp:
    // the .xopp
    const bool shown =
            DocumentFiles::isMarkdownFile(file) || DocumentFiles::isImageFile(file) || DocumentFiles::isTextFile(file);
    if (shown && !DocumentFiles::itemOf(file).xopp.empty()) {
        return openPath(QString::fromStdString(DocumentFiles::itemOf(file).xopp.string()));
    }
    // A Markdown file: its text, edited (written back to it)
    std::string textError;
    std::unique_ptr<DocumentSession> textSession = openTextFile(file, textError);
    if (!textSession && !textError.empty()) {
        Q_EMIT message(tr("Cannot open file"), QString::fromStdString(textError), true);
        return false;
    }
    auto result = textSession ? DocumentSession::LoadResult{} : shown ? loadShownFile(file) : DocumentSession::loadFile(file);
    if (!textSession && !result.document) {
        Q_EMIT message(tr("Cannot open file"), QString::fromStdString(result.error), true);
        return false;
    }
    const std::vector<std::string> hybridChanged = result.hybridChanged;
    // An untouched new document is replaced instead of keeping an empty tab around.
    const int pristine = replacePristine && tabs->isPristine(tabs->currentIndex()) ? tabs->currentIndex() : -1;
    auto opened = textSession ? std::move(textSession) : std::make_unique<DocumentSession>(*app, std::move(result.document));
    if (shown && !opened->textFile()) {
        opened->setShownFile(file, !DocumentFiles::isImageFile(file));
    }
    const bool isText = opened->textFile() != nullptr;
    tabs->addTab(std::move(opened));
    if (pristine >= 0) {
        tabs->closeTab(pristine);
    }
    if (isText) {
        watchTextFiles();
    }
    app->getSettings()->setLastOpenPath(fs::path(path.toStdString()).parent_path());
    recent->add(file);
    DocumentPlaces::setRead(DocumentPlaces::keyOf(file));
    // "Open documents where they were left off": at the page it was left at (setting, off by default)
    if (bool resume = false;
        app->getSettings()->getCustomElement("xournalQt").getBool("resumeAtLastPage", resume) && resume) {
        DocumentSession* s = tabs->currentSession();
        const int page = DocumentPlaces::lastPage(DocumentPlaces::keyOf(file));
        if (s && page > 0 && static_cast<size_t>(page) < s->getDocument()->getPageCount()) {
            s->setCurrentPageNo(static_cast<size_t>(page));
            s->getScrollHandler()->scrollToPage(static_cast<size_t>(page));
        }
    }
    setHomeVisible(false);
    if (!result.missingPdf.empty() || result.attachedPdfMissing) {
        Q_EMIT message(tr("PDF background missing"),
                       tr("The background PDF \"%1\" could not be found. The annotations are shown without it.")
                               .arg(QString::fromStdString(result.missingPdf.string())),
                       false);
    }
    if (!result.warnings.empty()) {
        QStringList w;
        for (const auto& s: result.warnings) {
            w << QString::fromStdString(s);
        }
        Q_EMIT message(tr("Problems while loading"),
                       tr("Some content might be lost. Do not overwrite the original file unless you are sure.\n\n") +
                               w.join('\n'),
                       false);
    }
    if (!hybridChanged.empty() && tabs->currentSession()) {
        // A hybrid PDF whose ink was moved, changed or deleted in another app: the window asks what to keep
        tabs->currentSession()->setHybridChanges(hybridChanged);
        Q_EMIT hybridEditedElsewhere(QString::fromStdString(file.filename().string()));
    }
    return true;
}

namespace {
bool settingOn(Settings* settings, const char* key);
}  // namespace

bool AppController::startSave(SaveWay way, const fs::path& target, std::function<void(bool)> then,
                              DocumentSession* document) {
    DocumentSession* s = document ? document : session();
    if (!s) {
        return false;
    }
    if (s->textFile() && !s->hasFilePath() && way != SaveWay::Save && way != SaveWay::SaveAs) {
        return false;  // (a text file is saved as itself: no hybrid PDF, no .xopp)
    }
    if (way == SaveWay::Save && !s->hasFilePath() && !s->isEditableText()) {
        // "Save notes into the PDF itself": an annotated PDF is saved into it, as a hybrid PDF
        if (!savesWithoutDialog(s)) {
            return false;
        }
        return startSave(SaveWay::Hybrid, s->annotatedPdf(), std::move(then), s);
    }
    DocumentSession::SaveRequest request;
    switch (way) {
        case SaveWay::Save:
            request.kind = DocumentSession::SaveKind::Save;
            break;
        case SaveWay::SaveAs:
            request.kind = DocumentSession::SaveKind::SaveAs;
            break;
        case SaveWay::Hybrid:
            request.kind = DocumentSession::SaveKind::Hybrid;
            break;
        case SaveWay::ExportXopp:
            request.kind = DocumentSession::SaveKind::ExportXopp;
            break;
    }
    request.target = target;
    const bool hybrid = way == SaveWay::Hybrid || (way == SaveWay::Save && s->isHybrid());
    if (hybrid && settingOn(app->getSettings(), "hybridExportXopp")) {
        // "On every save of a hybrid PDF, also write a .xopp for Xournal++": from the same state, in the same job
        fs::path xopp = way == SaveWay::Save ? s->getFilePath() : target;
        if (way != SaveWay::Save && xopp.extension() != ".pdf") {
            xopp += ".pdf";
        }
        xopp.replace_extension(".xopp");
        request.exportXopp = xopp;
    }
    QPointer<DocumentSession> guard(s);
    request.done = [this, guard, way, target, then = std::move(then)](const DocumentSession::SaveResult& r) {
        if (!guard) {
            return;
        }
        DocumentSession& saved = *guard;
        if (!r.ok) {
            Q_EMIT message(way == SaveWay::ExportXopp ? tr("Export failed") : tr("Saving failed"),
                           QString::fromStdString(r.error), true);
        } else if (way == SaveWay::ExportXopp) {
            library->refresh();
            Q_EMIT pageActionDone(tr("Exported to %1").arg(QString::fromStdString(target.filename().string())), false);
        } else {
            if (way != SaveWay::Save) {
                app->getSettings()->setLastSavePath(target.parent_path());
                recent->add(saved.getFilePath());
            }
            if (saved.textFile() && !saved.hasFilePath()) {
                library->refresh();  // (the library reads the text file again: its card, its index entry)
                watchTextFiles();
            } else if (saved.isHybrid()) {
                afterHybridSave(saved);  // (the library index reads a hybrid PDF itself)
                if (!r.exportError.empty()) {
                    Q_EMIT message(tr("Export for Xournal++ failed"), QString::fromStdString(r.exportError), true);
                }
            } else {
                handOverToLibrary(saved);
                if (way == SaveWay::SaveAs) {
                    library->refresh();  // a new document in the library
                }
            }
        }
        Q_EMIT titleChanged();
        if (then) {
            then(r.ok);
        }
    };
    s->saveInBackground(std::move(request));
    return true;
}

bool AppController::waitForSave() {
    DocumentSession* s = session();
    return s && s->waitForSaves();
}

std::function<void(bool)> AppController::callWhenSaved(const QJSValue& then) {
    if (!then.isCallable()) {
        return {};
    }
    QPointer<DocumentSession> guard(session());
    return [this, guard, then](bool ok) {
        if (!ok) {
            return;  // (the message says why; the document stays open and modified)
        }
        // From the event loop (a call may close the tab), with the saved document's tab current (the window's flows
        // go on with the current tab)
        QTimer::singleShot(0, this, [this, guard, then]() mutable {
            const int i = guard ? tabs->indexOf(guard) : -1;
            if (i < 0) {
                return;
            }
            tabs->setCurrentIndex(i);
            then.call();
        });
    };
}

bool AppController::saveInBackground(const QJSValue& then) { return startSave(SaveWay::Save, {}, callWhenSaved(then)); }

bool AppController::saveAsInBackground(const QUrl& url, const QJSValue& then) {
    return startSave(SaveWay::SaveAs, fs::path(url.toLocalFile().toStdString()), callWhenSaved(then));
}

bool AppController::saveAsHybridInBackground(const QUrl& url, const QJSValue& then) {
    return startSave(SaveWay::Hybrid, fs::path(url.toLocalFile().toStdString()), callWhenSaved(then));
}

void AppController::exportXoppInBackground(const QUrl& url) {
    fs::path xopp(url.toLocalFile().toStdString());
    if (xopp.extension() != ".xopp") {
        xopp += ".xopp";
    }
    startSave(SaveWay::ExportXopp, xopp, {});
}

bool AppController::save() {
    bool ok = false;
    return startSave(SaveWay::Save, {}, [&ok](bool r) { ok = r; }) && (waitForSave(), ok);
}

void AppController::handOverToLibrary(DocumentSession& s) {
    // Its library entry from the document in memory: the index does not read the file again
    if (LibraryIndex* index = library->searchIndex()) {
        index->documentSaved(s.getFilePath(), *s.getDocument(), s.search().textIndex().pdfTexts());
    }
}

namespace {
bool settingOn(Settings* settings, const char* key) {
    bool on = false;
    settings->getCustomElement("xournalQt").getBool(key, on);
    return on;
}
}  // namespace

bool AppController::isHybrid() const { return session() && session()->isHybrid(); }

bool AppController::savesWithoutDialog() const { return savesWithoutDialog(session()); }

bool AppController::saveReferenceInHand() {
    CanvasView* r = editedReference();
    if (!r) {
        return false;  // (the notes)
    }
    DocumentSession& s = r->getSession();
    if (!savesWithoutDialog(&s)) {
        tabs->setCurrentIndex(tabs->indexOf(&s));  // it needs a file: asked for in its own tab
        return false;
    }
    return startSave(SaveWay::Save, {}, {}, &s);
}

bool AppController::savesWithoutDialog(const DocumentSession* s) const {
    if (!s) {
        return false;
    }
    if (s->hasFilePath() || s->isEditableText()) {
        return true;  // (a text file is written back to itself)
    }
    const fs::path pdf = s->annotatedPdf();
    return !pdf.empty() && settingOn(app->getSettings(), "hybridIntoPdf") && !HybridPdf::inCache(pdf) &&
           !MergedPdf::inCache(pdf);
}

QUrl AppController::suggestedHybridFile() const {
    if (!session()) {
        return {};
    }
    fs::path target;
    if (session()->isHybrid()) {
        target = session()->getFilePath();
    } else if (const fs::path pdf = session()->annotatedPdf(); !pdf.empty() && !HybridPdf::inCache(pdf)) {
        target = settingOn(app->getSettings(), "hybridIntoPdf") ? pdf
                                                                 : pdf.parent_path() / (pdf.stem().string() + ".notes.pdf");
    } else {
        target = fs::path(suggestedSaveFile().toLocalFile().toStdString());
        target.replace_extension(".pdf");
        std::error_code ec;
        if (fs::exists(target, ec) && !HybridPdf::isHybrid(target)) {
            target.replace_extension(".notes.pdf");  // (never over another PDF by default)
        }
    }
    return QUrl::fromLocalFile(QString::fromStdString(target.string()));
}

bool AppController::saveAsHybrid(const QUrl& url) {
    bool ok = false;
    return startSave(SaveWay::Hybrid, fs::path(url.toLocalFile().toStdString()), [&ok](bool r) { ok = r; }) &&
           (waitForSave(), ok);
}

void AppController::afterHybridSave(DocumentSession& s) {
    // The clean copy of the new version, in the background: opening it again (also the library's index and preview)
    // does not have to make it (seconds for a long PDF)
    QThreadPool::globalInstance()->start([file = s.getFilePath()] { HybridPdf::open(file); });
    library->refresh();
}

QUrl AppController::suggestedXoppExport() const {
    if (!session() || !session()->hasFilePath()) {
        return {};
    }
    fs::path xopp = session()->getFilePath();
    xopp.replace_extension(".xopp");
    return QUrl::fromLocalFile(QString::fromStdString(xopp.string()));
}

bool AppController::exportXopp(const QUrl& url) {
    fs::path xopp(url.toLocalFile().toStdString());
    if (xopp.extension() != ".xopp") {
        xopp += ".xopp";
    }
    bool ok = false;
    return startSave(SaveWay::ExportXopp, xopp, [&ok](bool r) { ok = r; }) && (waitForSave(), ok);
}

bool AppController::importHybridChanges() {
    if (!session()) {
        return false;
    }
    std::string error;
    if (!session()->importHybridChanges(error)) {
        if (!error.empty()) {
            Q_EMIT message(tr("Import failed"), QString::fromStdString(error), true);
        }
        return false;
    }
    Q_EMIT pageActionDone(tr("The other app's ink is kept as plain annotations"), false);
    return true;
}

void AppController::keepHybridData() {
    if (session()) {
        session()->setHybridChanges({});
    }
}

bool AppController::saveAs(const QUrl& url) {
    bool ok = false;
    return startSave(SaveWay::SaveAs, fs::path(url.toLocalFile().toStdString()), [&ok](bool r) { ok = r; }) &&
           (waitForSave(), ok);
}

void AppController::undo() {
    if (editedReference()) {
        referenceMode->undo();  // (the canvas last written on has the keys)
        return;
    }
    if (!session()) {
        return;
    }
    if (MarkdownEditor* editor = canvas() && canvas()->textMode() ? canvas()->getMarkdownEditor() : nullptr;
        editor && editor->canUndo()) {
        editor->undo();  // a text file: the text being written, step by step (not the whole edit at once)
        return;
    }
    session()->clearSelectionEndText();  // first: finishing a text edit is itself an undo step
    endMarkdown(true);                   // (as is the Markdown being written beside the page)
    if (canUndo()) {
        session()->getUndoRedoHandler()->undo();
    }
}

void AppController::redo() {
    if (editedReference()) {
        referenceMode->redo();
        return;
    }
    if (!session()) {
        return;
    }
    if (MarkdownEditor* editor = canvas() && canvas()->textMode() ? canvas()->getMarkdownEditor() : nullptr;
        editor && editor->canRedo()) {
        editor->redo();
        return;
    }
    session()->clearSelectionEndText();
    endMarkdown(true);
    if (canRedo()) {
        session()->getUndoRedoHandler()->redo();
    }
}

QString AppController::fontFamily() const { return QString::fromStdString(app->getSettings()->getFont().getName()); }
double AppController::fontSize() const { return app->getSettings()->getFont().getSize(); }

void AppController::setFont(const QString& family, double size) {
    XojFont font(family.toStdString(), std::clamp(size, 4.0, 400.0));
    app->getSettings()->setFont(font);
    if (canvas() && canvas()->getTextEditor()) {
        auto* editor = canvas()->getTextEditor();
        // The text being edited follows (a Markdown text keeps its own size, see markdownFontSize)
        editor->setFont(editor->isMarkdown() ? XojFont(font.getName(), editor->fontSize()) : font);
    }
    Q_EMIT fontChanged();
}
void AppController::setFontFamily(const QString& family) { setFont(family, fontSize()); }
void AppController::setFontSize(double size) { setFont(fontFamily(), size); }

QStringList AppController::fontFamilies() const { return QFontDatabase::families(); }

void AppController::toggleGeometryTool(const QString& which) {
    if (!canvas()) {
        return;
    }
    if (which == "setsquare") {
        canvas()->geometryTool().toggle(GeometryToolType::SETSQUARE);
    } else if (which == "compass") {
        canvas()->geometryTool().toggle(GeometryToolType::COMPASS);
    } else {
        canvas()->geometryTool().hide();
    }
    Q_EMIT toolChanged();
}

bool AppController::geometryMinimized() const { return canvas() && canvas()->geometryTool().minimized(); }

void AppController::setGeometryMinimized(bool minimized) {
    if (canvas()) {
        canvas()->geometryTool().setMinimized(minimized);
        Q_EMIT toolChanged();
    }
}

bool AppController::geometryHeldToStroke() const { return canvas() && canvas()->geometryTool().heldToStroke(); }

void AppController::setGeometryHeldToStroke(bool held) {
    if (!canvas()) {
        return;
    }
    if (!held) {
        canvas()->geometryTool().releaseStroke();
    } else if (!canvas()->geometryTool().holdToStroke()) {
        Q_EMIT pageActionDone(tr("There is no ink on this page to hold on to"), false);
    }
    Q_EMIT toolChanged();
}

bool AppController::drawGeometryMarks() { return canvas() && canvas()->drawGeometryMarks(markSpacing); }

void AppController::setGeometryMarkSpacing(qreal cm) {
    if (cm > 0 && cm != markSpacing) {
        markSpacing = cm;
        Q_EMIT toolChanged();
    }
}

bool AppController::geometryAngleSteps() const { return canvas() && canvas()->geometryTool().angleSteps(); }

void AppController::setGeometryAngleSteps(bool steps) {
    if (canvas()) {
        canvas()->geometryTool().setAngleSteps(steps);
        Q_EMIT toolChanged();
    }
}

QObject* AppController::penHover() const { return &PenHover::instance(); }

QString AppController::geometryTool() const {
    if (!canvas() || !canvas()->geometryTool().active()) {
        return {};
    }
    return canvas()->geometryTool().type() == GeometryToolType::SETSQUARE ? QStringLiteral("setsquare")
                                                                          : QStringLiteral("compass");
}

void AppController::selectTool(const QString& name) {
    ToolType type = toolTypeFromString(name.toStdString());
    if (type == TOOL_NONE) {
        type = TOOL_PEN;
    }
    ToolHandler* th = app->getToolHandler();
    th->selectTool(type);
    th->fireToolChanged();
}

void AppController::setColor(const QColor& c) {
    app->getToolHandler()->setColor(toColor(c), true);
    Q_EMIT toolChanged();
}

void AppController::setSize(int s) {
    ToolHandler* th = app->getToolHandler();
    const ToolType type = th->getToolType();
    if (s == 5) {
        th->setCustomThickness(type, th->getCustomThickness(type), true);
    } else {
        th->setSize(static_cast<ToolSize>(std::clamp(s, 0, 4)));
    }
    storeCustomWidths();
    Q_EMIT toolChanged();
}

void AppController::fitWidth() {
    if (referenceMode->focused()) {
        referenceMode->fitWidth();  // (the page in view there)
    } else if (canvas() && session()) {
        canvas()->getViewController().fitWidth(session()->getCurrentPageNo());
    }
}

void AppController::fitHeight() {
    if (canvas() && session() && canvas()->documentLayout().horizontal()) {
        canvas()->getViewController().fitHeight();  // sideways: all rows, and kept
    } else if (canvas() && session()) {
        canvas()->getViewController().fitPage(session()->getCurrentPageNo(), false);
    }
}

void AppController::fitPage() {
    if (canvas() && session()) {
        canvas()->getViewController().fitPage(session()->getCurrentPageNo(), true);
    }
}

bool AppController::currentPageDiffers() const {
    if (!session()) {
        return false;
    }
    Document* doc = session()->getDocument();
    std::shared_lock lock(*doc);
    const size_t count = doc->getPageCount();
    const size_t current = std::min(session()->getCurrentPageNo(), count - 1);
    if (count < 2) {
        return false;
    }
    const PageRef page = doc->getPage(current);
    const PageRef other = doc->getPage(current == 0 ? 1 : current - 1);
    return page->getWidth() != other->getWidth() || page->getHeight() != other->getHeight();
}

void AppController::zoomIn() {
    if (referenceMode->focused()) {
        referenceMode->zoomIn();
    } else if (canvas()) {
        auto& vc = canvas()->getViewController();
        vc.zoomBy(1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::setZoomPercent(int percent) {
    if (canvas() && percent > 0 && zoomPercent() > 0) {
        auto& vc = canvas()->getViewController();
        vc.zoomBy(percent / (vc.zoom() / vc.zoom100() * 100.0),
                  QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::zoomOut() {
    if (referenceMode->focused()) {
        referenceMode->zoomOut();
    } else if (canvas()) {
        auto& vc = canvas()->getViewController();
        vc.zoomBy(1 / 1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::addPageAfterCurrent() {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        session()->insertNewPage(session()->getCurrentPageNo() + 1);
    }
}

void AppController::goToPage(int index) {
    if (session() && index >= 0 && static_cast<size_t>(index) < session()->getDocument()->getPageCount()) {
        session()->setCurrentPageNo(index);
        canvas()->getViewController().scrollToPage(index);
    }
}

namespace {
CanvasView::PdfTextMode pdfModeFrom(const QString& m) {
    return m == "underline"       ? CanvasView::PdfTextMode::Underline
           : m == "strikethrough" ? CanvasView::PdfTextMode::Strikethrough
           : m == "select"        ? CanvasView::PdfTextMode::Select
                                  : CanvasView::PdfTextMode::Highlight;
}
}  // namespace

void AppController::applyPdfTextMode() {
    const Color highlight = toColor(pdfHighlightColor());
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->view(i)->setPdfTextMode(pdfModeFrom(pdfMode));
        tabs->view(i)->setPdfHighlightColor(highlight);
    }
}

void AppController::setPdfTextMode(const QString& mode) {
    if (mode != pdfMode) {
        pdfMode = mode;
        applyPdfTextMode();
        Q_EMIT pdfTextModeChanged();
    }
}

bool AppController::markPdfText(const QString& mode) { return canvas() && canvas()->markPdfText(pdfModeFrom(mode)); }
bool AppController::copyPdfText() {
    const bool ok = canvas() && canvas()->copyPdfText();
    if (ok) {
        canvas()->clearPdfTextSelection();
        Q_EMIT pageActionDone(tr("Text copied"), false);
    }
    return ok;
}
void AppController::clearPdfTextSelection() {
    if (canvas()) {
        canvas()->clearPdfTextSelection();
    }
}

void AppController::jumpToPage(int index) {
    if (canvas() && index >= 0) {
        canvas()->jumpToPage(static_cast<size_t>(index));
    }
}
bool AppController::canGoBack() const { return canvas() && canvas()->canGoBack(); }
bool AppController::canGoForward() const { return canvas() && canvas()->canGoForward(); }
void AppController::navigateBack() {
    if (referenceMode->focused()) {
        referenceMode->navigateBack();
    } else if (canvas()) {
        canvas()->navigateBack();
    }
}
void AppController::navigateForward() {
    if (referenceMode->focused()) {
        referenceMode->navigateForward();
    } else if (canvas()) {
        canvas()->navigateForward();
    }
}
void AppController::clearNavigation() {
    if (canvas()) {
        canvas()->clearNavigation();
    }
}

// Upstream's page operations work on the current page: select the page first.
bool AppController::insertPages(int position, int background, int paper, bool landscape, int count) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    DocumentSession* s = session();
    const auto& types = app->getPageTypes()->getPageTypes();
    if (!s || background < 0 || background >= static_cast<int>(types.size()) || count < 1) {
        return false;
    }
    Document* doc = s->getDocument();
    QSizeF size = SettingsModel::paperSize(paper);
    if (!size.isValid()) {
        std::shared_lock lock(*doc);
        const PageRef current = doc->getPage(std::min(s->getCurrentPageNo(), doc->getPageCount() - 1));
        size = QSizeF(std::min(current->getWidth(), current->getHeight()), std::max(current->getWidth(), current->getHeight()));
    }
    if (landscape) {
        size.transpose();
    }
    const Color bgColor = app->getSettings()->getPageTemplateSettings().getBackgroundColor();
    std::vector<PageRef> pages;
    for (int i = 0; i < count; ++i) {
        auto page = std::make_shared<XojPage>(size.width(), size.height());
        page->setBackgroundType(types[static_cast<size_t>(background)]->page);
        page->setBackgroundColor(bgColor);
        pages.push_back(std::move(page));
    }
    const size_t at = std::min<size_t>(static_cast<size_t>(std::max(0, position)), doc->getPageCount());
    s->clearSelectionEndText();
    s->insertPages(pages, at);
    s->setCurrentPageNo(at);
    s->getScrollHandler()->scrollToPage(at);
    Q_EMIT pageActionDone(count == 1 ? tr("Page inserted") : tr("%1 pages inserted").arg(count), true);
    return true;
}

bool AppController::pagesHavePdfBackground(const QList<int>& pages) const {
    DocumentSession* s = session();
    if (!s) {
        return false;
    }
    Document* doc = s->getDocument();
    std::shared_lock lock(*doc);
    for (size_t index: pageList(pages)) {
        if (index < doc->getPageCount() && doc->getPage(index)->getBackgroundType().isPdfPage()) {
            return true;
        }
    }
    return false;
}

bool AppController::changePageBackground(const QList<int>& pages, int background) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    DocumentSession* s = session();
    const auto& types = app->getPageTypes()->getPageTypes();
    if (!s || background < 0 || background >= static_cast<int>(types.size())) {
        return false;
    }
    const PageType& type = types[static_cast<size_t>(background)]->page;
    const Color bgColor = app->getSettings()->getPageTemplateSettings().getBackgroundColor();
    Document* doc = s->getDocument();
    std::vector<size_t> changed;
    auto group = std::make_unique<GroupUndoAction>();
    {
        doc->lock();
        for (size_t index: pageList(pages)) {
            PageRef page = index < doc->getPageCount() ? doc->getPage(index) : PageRef();
            if (!page) {
                continue;
            }
            // Port of PageBackgroundChangeController::commitPageTypeChange (patterns only: no PDF or image here)
            group->addAction(std::make_unique<PageBackgroundChangedUndoAction>(
                    page, page->getBackgroundType(), page->getPdfPageNr(), page->getBackgroundImage(),
                    page->getWidth(), page->getHeight()));
            page->setBackgroundType(type);
            page->setBackgroundColor(bgColor);
            changed.push_back(index);
        }
        doc->unlock();
    }
    if (changed.empty()) {
        return false;
    }
    s->addPageUndoAction(std::move(group));
    for (size_t index: changed) {
        type.isSpecial() ? s->firePageSizeChanged(index) : s->firePageChanged(index);
    }
    Q_EMIT pageActionDone(changed.size() == 1 ? tr("Background changed")
                                              : tr("Background of %1 pages changed").arg(changed.size()),
                          true);
    return true;
}

QVariantMap AppController::currentPageFormat() const {
    if (!session()) {
        return {};
    }
    Document* doc = session()->getDocument();
    std::shared_lock lock(*doc);
    const PageRef p = doc->getPage(std::min(session()->getCurrentPageNo(), doc->getPageCount() - 1));
    int background = -1;
    const auto& types = app->getPageTypes()->getPageTypes();
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i]->page == p->getBackgroundType()) {
            background = static_cast<int>(i);
            break;
        }
    }
    return {{"background", background}, {"landscape", p->getWidth() > p->getHeight()}};
}

void AppController::insertPageBefore(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->insertNewPage(static_cast<size_t>(std::max(0, index)));
    }
}

void AppController::insertPageAfter(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->insertNewPage(static_cast<size_t>(index) + 1);
    }
}

void AppController::duplicatePage(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->duplicatePage();
    }
}

void AppController::deletePage(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->deletePage();
    }
}

void AppController::movePageUp(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->movePageTowardsBeginning();
    }
}

void AppController::movePageDown(int index) {
    if (textPagesFixed()) {
        return;  // (a text file: its pages are its text)
    }
    if (session()) {
        goToPage(index);
        session()->movePageTowardsEnd();
    }
}

QUrl AppController::iconUrl(const QString& name) const {
    return QUrl::fromLocalFile(QString::fromStdString((app->getResourceDir() / "icons" / name.toStdString()).string()) +
                               ".svg");
}

QUrl AppController::openFolder() const {
    if (session() && session()->hasFilePath()) {
        return QUrl::fromLocalFile(QString::fromStdString(session()->getFilePath().parent_path().string()));
    }
    const fs::path& last = app->getSettings()->getLastOpenPath();
    return last.empty() ? QUrl() : QUrl::fromLocalFile(QString::fromStdString(last.string()));
}

void AppController::openLink(const QString& uri) {
    QUrl url(uri);
    if (const QString host = uri.section(QLatin1Char('/'), 0, 0);
        url.scheme().isEmpty() && !uri.startsWith(QLatin1Char('/')) && !uri.contains(QLatin1Char(':')) &&
        host.contains(QLatin1Char('.')) && !host.contains(QLatin1Char(' '))) {
        url = QUrl(QStringLiteral("https://") + uri);  // "example.org/page", as a browser reads it
    }
    // Only what a note may open by itself: a document from somewhere else must not start a program or open a file on
    // this computer with one tap. The address is shown next to the button, so it can be copied.
    static const QStringList opened{QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto")};
    if (!url.isValid() || !opened.contains(url.scheme().toLower())) {
        Q_EMIT message(tr("Link not opened"),
                       tr("Only web and mail addresses are opened from a document. This link is:\n%1").arg(uri), true);
        return;
    }
    QDesktopServices::openUrl(url);
}

QUrl AppController::suggestedExportFile() const {
    if (!session()) {
        return {};
    }
    fs::path target;
    if (session()->isHybrid()) {
        target = session()->getFilePath();
        target = target.parent_path() / (target.stem().string() + "_export.pdf");  // never the hybrid PDF itself
    } else if (session()->hasFilePath()) {
        target = session()->getFilePath();
        target.replace_extension(".pdf");
    } else if (const fs::path pdf = session()->annotatedPdf(); !pdf.empty()) {
        target = pdf.parent_path() / (pdf.stem().string() + "_annotated.pdf");  // never the background PDF itself
    } else {
        target = session()->suggestSavePath();
        target.replace_extension(".pdf");
    }
    return QUrl::fromLocalFile(QString::fromStdString(target.string()));
}

bool AppController::exportPdf(const QUrl& url) {
    if (!session()) {
        return false;
    }
    session()->clearSelectionEndText();  // everything back in the document
    session()->waitForMerges();          // (pages pasted just now: their PDF pages)
    fs::path target(url.toLocalFile().toStdString());
    if (target.extension() != ".pdf") {
        target += ".pdf";
    }
    try {
        // Port of PdfExportJob: upstream blocks the UI while exporting, too.
        ExportHelper::exportPdf(session()->getDocument(), target, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    } catch (const std::exception& e) {
        Q_EMIT message(tr("Export failed"), QString::fromUtf8(e.what()), true);
        return false;
    }
    Q_EMIT pageActionDone(tr("Exported to %1").arg(QString::fromStdString(target.filename().string())), false);
    return true;
}

bool AppController::addChapter(int page, const QString& title, int level) {
    if (textPagesFixed()) {
        return false;  // (a text file: its pages are its text)
    }
    DocumentSession* s = session();
    if (!s || title.trimmed().isEmpty()) {
        return false;
    }
    Document* doc = s->getDocument();
    const size_t index = static_cast<size_t>(std::clamp(page, 0, std::max(0, pageCount() - 1)));
    auto text = std::make_unique<Text>();
    text->setText(DocumentChapters::headingText(title.trimmed().toStdString(), level));
    text->setFont(XojFont("Sans Bold", DocumentChapters::headingSize(level)));
    text->setColor(Color(0, 0, 0));
    text->move(TextFlow::MARGIN, TextFlow::MARGIN);
    const Text* raw = text.get();
    Layer* layer = nullptr;
    PageRef pageRef;
    {
        doc->lock();
        pageRef = doc->getPage(index);
        layer = pageRef ? pageRef->getSelectedLayer() : nullptr;
        if (layer) {
            layer->addElement(std::move(text));
        }
        doc->unlock();
    }
    if (!layer) {
        return false;
    }
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(pageRef, layer, raw));
    pageRef->firePageChanged();
    s->firePageChanged(index);
    outline->rebuild();
    Q_EMIT pageActionDone(tr("Chapter “%1” added").arg(title.trimmed()), false);
    return true;
}

void AppController::copyPageLink(int page) {
    const int number = page >= 0 ? page + 1 : pageNumber();
    QGuiApplication::clipboard()->setText(QString::fromStdString(xoj::util::pageLinkText(number)));
    Q_EMIT pageActionDone(tr("Link to page %1 copied").arg(number), false);
}

bool AppController::pdfTextIsSelected() const { return canvas() && canvas()->hasPdfTextSelection(); }

bool AppController::selectPdfTextAt(qreal x, qreal y) {
    if (!canvas()) {
        return false;
    }
    // The same word again: its whole line (like a phone widens the selection)
    const QPointF where(x, y);
    const bool again = canvas()->hasPdfTextSelection() && canvas()->pdfSelectionEnds().adjusted(-8, -8, 8, 8).contains(where);
    const bool selected = canvas()->selectPdfTextAt(where, again);
    Q_EMIT pdfTextSelectionChanged();
    return selected;
}

bool AppController::dragPdfSelection(qreal x, qreal y, bool startEnd) {
    const bool changed = canvas() && canvas()->dragPdfSelection(QPointF(x, y), startEnd);
    if (changed) {
        Q_EMIT pdfTextSelectionChanged();
    }
    return changed;
}

QRectF AppController::pdfSelectionEnds() const { return canvas() ? canvas()->pdfSelectionEnds() : QRectF(); }

QRectF AppController::pdfSelectionBox() const { return canvas() ? canvas()->pdfSelectionBox() : QRectF(); }

void AppController::showPdfSelection() {
    if (canvas()) {
        canvas()->scrollToPdfSelection();
    }
}

bool AppController::hasPdfBackground() const {
    return session() && !session()->getDocument()->getPdfFilepath().empty();
}

bool AppController::printDocument(bool withAnnotations, const QString& range) {
    DocumentSession* s = session();
    if (!s) {
        return false;
    }
    s->clearSelectionEndText();
    s->waitForMerges();  // (pages pasted just now: their PDF pages)
    // What is printed: the document as a PDF, or the PDF it annotates as it is
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        Q_EMIT message(tr("Printing failed"), tr("No place for the file to print."), true);
        return false;
    }
    temporary.setAutoRemove(false);  // (the printer reads it after we return)
    const fs::path file = fs::path(temporary.filePath("print.pdf").toStdString());
    const fs::path background = s->getDocument()->getPdfFilepath();
    try {
        if (!withAnnotations && !background.empty()) {
            fs::copy_file(background, file, fs::copy_options::overwrite_existing);
        } else {
            const std::string pages = range.trimmed().toStdString();
            ExportHelper::exportPdf(s->getDocument(), file, pages.empty() ? nullptr : pages.c_str(), nullptr,
                                    EXPORT_BACKGROUND_ALL, false);
        }
    } catch (const std::exception& e) {
        Q_EMIT message(tr("Printing failed"), QString::fromUtf8(e.what()), true);
        return false;
    }

    // The system's print dialog: printer, copies, pages, duplex ...
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(title());
    QPrintDialog dialog(&printer);
    dialog.setOption(QAbstractPrintDialog::PrintToFile, true);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (!printer.outputFileName().isEmpty()) {  // "print to a file": our PDF is the result
        QFile::remove(printer.outputFileName());
        if (!QFile::copy(QString::fromStdString(file.string()), printer.outputFileName())) {
            Q_EMIT message(tr("Printing failed"), tr("Could not write %1.").arg(printer.outputFileName()), true);
            return false;
        }
        Q_EMIT pageActionDone(tr("Written to %1").arg(QFileInfo(printer.outputFileName()).fileName()), false);
        return true;
    }
    // Send the PDF to the printer as it is (printing it ourselves would turn it into pixels)
    QStringList arguments{"-d", printer.printerName(), "-n", QString::number(std::max(1, printer.copyCount()))};
    if (printer.printRange() == QPrinter::PageRange && printer.fromPage() > 0) {
        arguments << "-P" << QString("%1-%2").arg(printer.fromPage()).arg(printer.toPage());
    }
    if (printer.duplex() == QPrinter::DuplexLongSide) {
        arguments << "-o" << "sides=two-sided-long-edge";
    } else if (printer.duplex() == QPrinter::DuplexShortSide) {
        arguments << "-o" << "sides=two-sided-short-edge";
    }
    if (printer.colorMode() == QPrinter::GrayScale) {
        arguments << "-o" << "print-color-mode=monochrome";
    }
    arguments << QString::fromStdString(file.string());
    if (!QProcess::startDetached("lp", arguments)) {
        Q_EMIT message(tr("Printing failed"), tr("Could not hand the document to the printer (lp)."), true);
        return false;
    }
    Q_EMIT pageActionDone(tr("Sent to %1").arg(printer.printerName()), false);
    return true;
}

QUrl AppController::suggestedSaveFile() const {
    if (!session()) {
        return {};
    }
    fs::path suggested = session()->suggestSavePath();
    // A document that was never saved and does not annotate a PDF belongs in the library of this window. Upstream
    // suggests the folder something was saved to last, which is shared by all libraries and windows.
    // (An image to write on: its .xopp next to it, so the library pairs them.)
    const fs::path shown = session()->shownFile();
    if (!session()->hasFilePath() && session()->annotatedPdf().empty() && library->available() &&
        (shown.empty() || DocumentFiles::isMarkdownFile(shown) || DocumentFiles::isTextFile(shown))) {
        suggested = fs::path(library->rootPath().toStdString()) / suggested.filename();
    }
    return QUrl::fromLocalFile(QString::fromStdString(suggested.string()));
}
