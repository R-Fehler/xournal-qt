#include "AppController.h"

#include <algorithm>
#include <limits>

#include <shared_mutex>

#include <QFileInfo>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
#include "util/NamedColor.h"
#include "util/XojMsgBox.h"

#include "CanvasView.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/PageFilterModel.h"
#include "shell/PagesModel.h"
#include "shell/SessionRecovery.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
QColor toQColor(Color c) { return QColor(c.red, c.green, c.blue); }
Color toColor(const QColor& c) {
    return Color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
}
}  // namespace

AppController::AppController(QObject* parent): QObject(parent) {
    app = std::make_unique<AppContext>(AppContext::defaultResourceDir());
    colors = std::make_unique<Palette>(app->getResourceDir() / "palettes" / "xournal.gpl");
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

    pages = std::make_unique<PagesModel>();
    filteredPages = std::make_unique<PageFilterModel>(*pages);
    // "Only pages with hits" ends with the search.
    connect(this, &AppController::searchChanged, this, [this] {
        if (searchQuery().isEmpty()) {
            filteredPages->setOnlySearchHits(false);
        }
    });
    settingsView = std::make_unique<SettingsModel>(*app);
    tabs = std::make_unique<TabManager>(*app);
    connect(tabs.get(), &TabManager::currentTabChanged, this, &AppController::currentTabChanged);
    newDocument();
}

AppController::~AppController() {
    xoj::compat::setMessageSink({});
    for (auto& c: currentConnections) {
        disconnect(c);
    }
    pages->setSession(nullptr);
    recovery.reset();  // unregisters the sessions from the crash handler before they go away
    tabs.reset();
}

void AppController::shutdown() {
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
CanvasView* AppController::canvas() const { return tabs->currentView(); }

void AppController::currentTabChanged() {
    // Follow the signals of the current tab only.
    for (auto& c: currentConnections) {
        disconnect(c);
    }
    currentConnections.clear();
    if (DocumentSession* s = session()) {
        currentConnections.push_back(
                connect(s, &DocumentSession::modifiedChanged, this, &AppController::modifiedChanged));
        currentConnections.push_back(
                connect(s, &DocumentSession::undoRedoStateChanged, this, &AppController::undoRedoChanged));
        currentConnections.push_back(connect(s, &DocumentSession::filePathChanged, this, &AppController::titleChanged));
        currentConnections.push_back(
                connect(s, &DocumentSession::currentPageChanged, this, &AppController::pageChanged));
        currentConnections.push_back(
                connect(&s->search(), &DocumentSearch::changed, this, &AppController::searchChanged));
        currentConnections.push_back(
                connect(&s->search(), &DocumentSearch::finished, this, &AppController::searchChanged));
    }
    pages->setSession(session());
    if (CanvasView* v = canvas()) {
        currentConnections.push_back(connect(v, &CanvasView::pagesChanged, this, &AppController::pageChanged));
        currentConnections.push_back(connect(&v->getViewController(), &ViewController::zoomChanged, this,
                                             &AppController::zoomChanged));
    }
    Q_EMIT documentChanged();
    Q_EMIT titleChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT undoRedoChanged();
    Q_EMIT zoomChanged();
    Q_EMIT pageChanged();
    Q_EMIT searchChanged();
}

QString AppController::searchQuery() const { return session() ? session()->search().query() : QString(); }
void AppController::setSearchQuery(const QString& query) {
    if (session()) {
        session()->search().setQuery(query, true);
    }
}
int AppController::searchHitCount() const {
    return session() ? static_cast<int>(session()->search().hits().size()) : 0;
}
int AppController::searchCurrent() const { return session() ? session()->search().currentHit() + 1 : 0; }
bool AppController::searchRunning() const { return session() && session()->search().isRunning(); }
int AppController::searchHitPageCount() const {
    if (!session()) {
        return 0;
    }
    int pages = 0;
    size_t last = std::numeric_limits<size_t>::max();
    for (const auto& h: session()->search().hits()) {  // ordered by page
        if (h.page != last) {
            ++pages;
            last = h.page;
        }
    }
    return pages;
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
        tabs->session(i)->search().setQuery(query, false);
    }
}

void AppController::openSearchResult(int index) {
    tabs->setCurrentIndex(index);
    if (DocumentSession* s = session(); s && !s->search().query().isEmpty()) {
        s->search().jumpToFirstFromCurrentPage();
    }
}

QObject* AppController::tabsModel() const { return tabs.get(); }
QObject* AppController::pagesModel() const { return pages.get(); }
QObject* AppController::settingsModel() const { return settingsView.get(); }
QObject* AppController::filteredPagesModel() const { return filteredPages.get(); }
int AppController::currentTab() const { return tabs->currentIndex(); }
void AppController::setCurrentTab(int index) { tabs->setCurrentIndex(index); }
QObject* AppController::view() const { return canvas(); }

QString AppController::title() const {
    return session() ? QString::fromStdString(session()->getDisplayName()) : QString();
}
bool AppController::modified() const { return session() && session()->isModified(); }
bool AppController::hasFilePath() const { return session() && session()->hasFilePath(); }
bool AppController::canUndo() const { return session() && session()->getUndoRedoHandler()->canUndo(); }
bool AppController::canRedo() const { return session() && session()->getUndoRedoHandler()->canRedo(); }

QString AppController::tool() const {
    switch (app->getToolHandler()->getToolType()) {
        case TOOL_PEN:
            return "pen";
        case TOOL_HIGHLIGHTER:
            return "highlighter";
        case TOOL_ERASER:
            return "eraser";
        case TOOL_HAND:
            return "hand";
        default:
            return "other";
    }
}

QColor AppController::color() const { return toQColor(app->getToolHandler()->getColor()); }
int AppController::size() const { return static_cast<int>(app->getToolHandler()->getSize()); }

QVariantList AppController::palette() const {
    QVariantList list;
    for (size_t i = 0; i < colors->size(); ++i) {
        list.append(toQColor(colors->getColorAt(i).getColor()));
    }
    return list;
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

void AppController::newDocument() { tabs->addTab(std::make_unique<DocumentSession>(*app)); }

void AppController::startSession(const QStringList& files) {
    recovery = std::make_unique<SessionRecovery>(*tabs, SessionRecovery::defaultJournalFile());
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
        if (auto it = recovered.find(i); it != recovered.end()) {
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

void AppController::closeTab(int index) {
    tabs->closeTab(index);
    if (tabs->count() == 0) {
        newDocument();  // there is always a document to write on
    }
}

void AppController::moveTab(int from, int to) { tabs->moveTab(from, to); }

void AppController::nextTab() {
    if (tabs->count() > 1) {
        tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
    }
}

void AppController::previousTab() {
    if (tabs->count() > 1) {
        tabs->setCurrentIndex((tabs->currentIndex() + tabs->count() - 1) % tabs->count());
    }
}

int AppController::tabCount() const { return tabs->count(); }
bool AppController::tabModified(int index) const { return tabs->session(index) && tabs->session(index)->isModified(); }

QString AppController::tabTitle(int index) const {
    return tabs->session(index) ? QString::fromStdString(tabs->session(index)->getDisplayName()) : QString();
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

bool AppController::openFile(const QUrl& url) { return openPath(url.toLocalFile()); }

bool AppController::openPath(const QString& path) {
    const fs::path file(path.toStdString());
    if (int existing = tabs->indexOfFile(file); existing >= 0) {
        tabs->setCurrentIndex(existing);  // already open: show it
        return true;
    }
    auto result = DocumentSession::loadFile(file);
    if (!result.document) {
        Q_EMIT message(tr("Cannot open file"), QString::fromStdString(result.error), true);
        return false;
    }
    // An untouched new document is replaced instead of keeping an empty tab around.
    const int pristine = tabs->isPristine(tabs->currentIndex()) ? tabs->currentIndex() : -1;
    tabs->addTab(std::make_unique<DocumentSession>(*app, std::move(result.document)));
    if (pristine >= 0) {
        tabs->closeTab(pristine);
    }
    app->getSettings()->setLastOpenPath(fs::path(path.toStdString()).parent_path());
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
    return true;
}

bool AppController::save() {
    if (!session() || !session()->hasFilePath()) {
        return false;
    }
    auto r = session()->save();
    if (!r.ok) {
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
    }
    Q_EMIT titleChanged();
    return r.ok;
}

bool AppController::saveAs(const QUrl& url) {
    if (!session()) {
        return false;
    }
    const fs::path target(url.toLocalFile().toStdString());
    auto r = session()->saveAs(target);
    if (!r.ok) {
        Q_EMIT message(tr("Saving failed"), QString::fromStdString(r.error), true);
    } else {
        app->getSettings()->setLastSavePath(target.parent_path());
    }
    Q_EMIT titleChanged();
    return r.ok;
}

void AppController::undo() {
    if (canUndo()) {
        session()->clearSelectionEndText();
        session()->getUndoRedoHandler()->undo();
    }
}

void AppController::redo() {
    if (canRedo()) {
        session()->clearSelectionEndText();
        session()->getUndoRedoHandler()->redo();
    }
}

void AppController::selectTool(const QString& name) {
    ToolType type = TOOL_PEN;
    if (name == "highlighter") {
        type = TOOL_HIGHLIGHTER;
    } else if (name == "eraser") {
        type = TOOL_ERASER;
    } else if (name == "hand") {
        type = TOOL_HAND;
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
    app->getToolHandler()->setSize(static_cast<ToolSize>(std::clamp(s, 0, 4)));
    Q_EMIT toolChanged();
}

void AppController::fitWidth() {
    if (canvas()) {
        canvas()->getViewController().fitWidth();
    }
}

void AppController::zoomIn() {
    if (canvas()) {
        auto& vc = canvas()->getViewController();
        vc.zoomBy(1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::zoomOut() {
    if (canvas()) {
        auto& vc = canvas()->getViewController();
        vc.zoomBy(1 / 1.2, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    }
}

void AppController::addPageAfterCurrent() {
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

// Upstream's page operations work on the current page: select the page first.
void AppController::insertPageBefore(int index) {
    if (session()) {
        goToPage(index);
        session()->insertNewPage(static_cast<size_t>(std::max(0, index)));
    }
}

void AppController::insertPageAfter(int index) {
    if (session()) {
        goToPage(index);
        session()->insertNewPage(static_cast<size_t>(index) + 1);
    }
}

void AppController::duplicatePage(int index) {
    if (session()) {
        goToPage(index);
        session()->duplicatePage();
    }
}

void AppController::deletePage(int index) {
    if (session()) {
        goToPage(index);
        session()->deletePage();
    }
}

void AppController::movePageUp(int index) {
    if (session()) {
        goToPage(index);
        session()->movePageTowardsBeginning();
    }
}

void AppController::movePageDown(int index) {
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

QUrl AppController::suggestedSaveFile() const {
    if (!session()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(session()->suggestSavePath().string()));
}
