#include "AppController.h"

#include <algorithm>
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
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/HitPages.h"
#include "shell/Previews.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/DocumentChapters.h"
#include "shell/LayersModel.h"
#include "shell/ShortcutsModel.h"
#include "shell/OutlineModel.h"
#include "MarkdownEditor.h"
#include "MarkdownSession.h"
#include "MdBox.h"
#include "TextFlow.h"
#include "session/MergedPdf.h"
#include "shell/PageClipboard.h"
#include "shell/RecentFiles.h"
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
    library->onFilesChanged = [this](const DocumentFiles::Result& r) { filesChanged(r); };
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
    connect(tabs.get(), &TabManager::countChanged, this, [this] {
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
    tabs.reset();
}

namespace {
std::function<void(AppController*)> windowFactory;  // set by main(): makes the window for a controller
}  // namespace

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
    // The image workers draw with Qt: they must be done before the application takes its plugins away
    PreviewProvider::shutdown();
    HitPageProvider::shutdown();
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
    if (!session()) {
        return {};
    }
    mdSession = session();
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
    Q_EMIT documentChanged();
    Q_EMIT titleChanged();
    Q_EMIT modifiedChanged();
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
bool AppController::copySelection() { return canvas() && canvas()->copySelection(); }
bool AppController::cutSelection() { return canvas() && canvas()->cutSelection(); }
bool AppController::pasteElements() { return canvas() && canvas()->pasteElements(); }
bool AppController::pasteAt(qreal x, qreal y) { return canvas() && canvas()->pasteElements(QPointF(x, y)); }
bool AppController::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasImage() || mime->hasText() || mime->hasFormat("application/xournal"));
}
void AppController::deleteSelection() {
    if (canvas()) {
        canvas()->deleteSelection();
    }
}
void AppController::selectAllOnPage() {
    if (canvas()) {
        if (app->getToolHandler()->getToolType() != TOOL_SELECT_RECT &&
            app->getToolHandler()->getToolType() != TOOL_SELECT_REGION) {
            selectTool("selectRegion");  // so that the selection can be moved right away
        }
        canvas()->selectAllOnPage();
    }
}
bool AppController::insertImage(const QUrl& url) {
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
        session()->search().setQuery(query, true);
    }
}
int AppController::searchHitCount() const {
    return session() ? static_cast<int>(session()->search().hits().size()) : 0;
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
    if (!session() || pageClipboard->isEmpty()) {
        return 0;
    }
    if (position < 0) {
        const QList<int> sel = pages->selectedPages();
        position = (sel.isEmpty() ? static_cast<int>(session()->getCurrentPageNo()) : sel.last()) + 1;
    }
    fs::path keptIn;  // PDF pages from another PDF: the document's merged PDF
    auto copies = pageClipboard->pagesFor(*session(), &keptIn);
    const int n = static_cast<int>(copies.size());
    session()->insertPages(copies, static_cast<size_t>(position));
    QList<int> pasted;
    for (int i = 0; i < n; ++i) {
        pasted.append(position + i);
    }
    pages->selectPages(pasted);
    QString note = n == 1 ? tr("Page pasted") : tr("%1 pages pasted").arg(n);
    if (!keptIn.empty()) {
        // Once per paste: where the PDF pages went (a new file next to the document)
        const QString where = QString::fromStdString(keptIn.filename().string());
        if (MergedPdf::inCache(keptIn)) {
            note = n == 1 ? tr("Page pasted. Its PDF text stays searchable: it is saved next to the document.")
                          : tr("%1 pages pasted. Their PDF text stays searchable: they are saved next to the document.")
                                    .arg(n);
        } else {
            note = n == 1 ? tr("Page pasted. Its PDF text stays searchable: it is kept in %1 next to the document.")
                                    .arg(where)
                          : tr("%1 pages pasted. Their PDF text stays searchable: they are kept in %2 next to the "
                               "document.")
                                    .arg(n)
                                    .arg(where);
        }
    }
    Q_EMIT pageActionDone(note, true);
    return n;
}

bool AppController::deletePages(const QList<int>& list) {
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
bool AppController::hasFilePath() const { return session() && session()->hasFilePath(); }
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
    if (CanvasView* v = canvas()) {
        v->setMarkdownText(textMarkdown(), markdownFontSize(), markdownInPanel());
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

bool AppController::openSearchHit(const QString& path, const QString& query) {
    if (!openPath(path)) {
        return false;
    }
    if (session() && !query.trimmed().isEmpty()) {
        session()->setCurrentPageNo(0);
        session()->search().setQuery(query, true);  // shows the first hit
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
        if (s->search().query() == query) {
            s->search().jumpToFirstFromCurrentPage();
        } else {
            s->search().setQuery(query, true);  // current: the first hit from this page on
        }
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
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {dir});
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

void AppController::showInFileManager(const QString& path) {
    const QFileInfo info(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? path : info.absolutePath()));
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
        setHomeVisible(false);
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
        recent->add(session()->getFilePath());
        library->refresh();  // a new document in the library
    }
    Q_EMIT titleChanged();
    return r.ok;
}

void AppController::undo() {
    if (!session()) {
        return;
    }
    session()->clearSelectionEndText();  // first: finishing a text edit is itself an undo step
    endMarkdown(true);                   // (as is the Markdown being written beside the page)
    if (canUndo()) {
        session()->getUndoRedoHandler()->undo();
    }
}

void AppController::redo() {
    if (!session()) {
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
    if (canvas()) {
        canvas()->getViewController().fitWidth();
    }
}

void AppController::fitHeight() {
    if (canvas() && session()) {
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
    if (canvas()) {
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
    if (canvas()) {
        canvas()->navigateBack();
    }
}
void AppController::navigateForward() {
    if (canvas()) {
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
    if (session()->hasFilePath()) {
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
    if (!session()->hasFilePath() && session()->annotatedPdf().empty() && library->available()) {
        suggested = fs::path(library->rootPath().toStdString()) / suggested.filename();
    }
    return QUrl::fromLocalFile(QString::fromStdString(suggested.string()));
}
