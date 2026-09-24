#include "DocumentSession.h"

#include <functional>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <limits>
#include <set>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <glib.h>

#include <QCryptographicHash>
#include <QFile>

#include "control/layer/LayerController.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "util/TextLinks.h"
#include "model/Text.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertDeletePageUndoAction.h"
#include "undo/EmergencySaveRestore.h"
#include "undo/SwapUndoAction.h"
#include "util/PathUtil.h"
#include "util/Util.h"
#include "util/PlaceholderString.h"
#include "util/i18n.h"
#include "util/raii/CairoWrappers.h"
#include "util/safe_casts.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

#include "AppContext.h"
#include "DocumentMode.h"
#include "DocumentSaveTask.h"
#include "DocumentSearch.h"
#include "HybridPdf.h"
#include "MergedPdf.h"
#include "PageOrderUndoAction.h"
#include "PdfPageKeeper.h"
#include "MdBox.h"
#include "MdPaginate.h"
#include "TextFile.h"
#include "config.h"  // for FILE_FORMAT_VERSION

namespace xqt {

namespace {
/// A change of the page structure on the undo stack: undoing or redoing it tells the session (the pages that
/// change may not be in view, so the window says what happened).
class AnnouncedPageAction final: public UndoAction {
public:
    AnnouncedPageAction(UndoActionPtr inner, std::function<void(const std::string&, bool)> announce):
            UndoAction("AnnouncedPageAction"), inner(std::move(inner)), announce(std::move(announce)) {}
    bool undo(Control* control) override { return tell(inner->undo(control), true); }
    bool redo(Control* control) override { return tell(inner->redo(control), false); }
    std::string getText() override { return inner->getText(); }
    std::vector<PageRef> getPages() override { return inner->getPages(); }

private:
    bool tell(bool done, bool undone) {
        if (done && announce) {
            announce(inner->getText(), undone);
        }
        return done;
    }
    UndoActionPtr inner;
    std::function<void(const std::string&, bool)> announce;
};
}  // namespace

void DocumentSession::addPageUndoAction(UndoActionPtr action) {
    undoRedo->addUndoAction(std::make_unique<AnnouncedPageAction>(
            std::move(action), [this](const std::string& text, bool undone) {
                Q_EMIT pageActionUndone(QString::fromStdString(text), undone);
            }));
}

namespace {
/// Receives the events of documents that are not owned by a session yet (while loading). It has no listeners.
DocumentHandler& detachedHandler() {
    static DocumentHandler handler;
    return handler;
}

bool hasExtension(const fs::path& p, const char* ext) {
    auto e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ext;
}
}  // namespace

bool DocumentSession::LoadResult::isNewerFileVersion() const { return fileVersion > FILE_FORMAT_VERSION; }

namespace {
void prepareLoaded(Document& doc) {
    doc.setDocumentHandler(&detachedHandler());  // the LoadHandler's handler dies with it
    // Element sizes are computed lazily, also by the (parallel) renderers: compute them once, here, before any
    // renderer sees the document.
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* layer: doc.getPage(i)->getLayers()) {
            for (const auto& e: layer->getElementsView()) {
                e->getBoundingBox();
            }
        }
    }
}
}  // namespace

auto DocumentSession::loadFile(const fs::path& path, bool attachPdf) -> LoadResult {
    LoadResult result;
    if (hasExtension(path, ".pdf")) {
        // xournal-qt: a hybrid PDF opens as the document it carries (else, if that fails, as a plain PDF)
        if (HybridPdf::isHybrid(path)) {
            auto opened = HybridPdf::open(path);
            if (opened.document) {
                result.document = std::move(opened.document);
                result.warnings = std::move(opened.warnings);
                result.hybrid = true;
                result.hybridChanged = std::move(opened.changed);
                prepareLoaded(*result.document);
                return result;
            }
            result.warnings.push_back(opened.error + " " + _("It is opened as a plain PDF."));
        }
        // Port of Control::openPdfFile: annotate a PDF, one page per PDF page.
        auto doc = std::make_unique<Document>(&detachedHandler());
        if (doc->readPdf(path, /*initPages=*/true, attachPdf)) {
            result.document = std::move(doc);
        } else {
            result.error = FS(_F("Error reading PDF file \"{1}\"\n{2}") % path.u8string() % doc->getLastErrorMsg());
        }
        return result;
    }
    // Port of Control::openXoppFile
    try {
        LoadHandler loadHandler(&result.warnings);
        result.document = loadHandler.loadDocument(path);
        result.missingPdf = loadHandler.getMissingPdfFilename();
        result.attachedPdfMissing = loadHandler.isAttachedPdfMissing();
        result.fileVersion = loadHandler.getFileVersion();
        if (result.document) {
            prepareLoaded(*result.document);
        }
    } catch (const std::exception& e) {
        result.document.reset();
        result.error = FS(_F("Error opening file \"{1}\"") % path.u8string()) + "\n" + e.what();
    }
    return result;
}

DocumentSession::DocumentSession(AppContext& app, QObject* parent): QObject(parent), app(app) {
    // Port of createNewDocument / Control::addDefaultPage
    doc = std::make_unique<Document>(this);
    const auto& model = app.getSettings()->getPageTemplateSettings();
    auto page = std::make_shared<XojPage>(model.getPageWidth(), model.getPageHeight());
    page->setBackgroundColor(model.getBackgroundColor());
    page->setBackgroundType(model.getBackgroundType());
    doc->addPage(std::move(page));
    init();
}

DocumentSession::DocumentSession(AppContext& app, std::unique_ptr<Document> document, QObject* parent):
        QObject(parent), app(app), doc(std::move(document)) {
    doc->setDocumentHandler(this);
    init();
    stampFiles();  // (as read)
}

void DocumentSession::init() {
    static std::atomic<quint64> nextSerial{1};
    serialNo = nextSerial++;
    searcher = std::make_unique<DocumentSearch>(*this);
    window.view = &headlessView;
    // One undo stack for everything, as in upstream: what is written and the page structure (insert, delete,
    // move, paste). Undoing a page change tells about it (pageActionUndone), it may not be in view.
    undoRedo = std::make_unique<UndoRedoHandler>(this);
    undoRedo->addUndoRedoListener(this);
    layerController = std::make_unique<LayerController>(this);
    layerController->registerListener(this);
    pageLinkKeeper = std::make_unique<PageLinkKeeper>(*this);
    pageRevisionKeeper = std::make_unique<PageRevisionKeeper>(*this);
    pdfPages = std::make_unique<PdfPageKeeper>(*this);  // (after the revisions: it hears pages that come after them)
    if (const fs::path bg = doc->getPdfFilepath(); HybridPdf::inCache(bg)) {
        HybridPdf::retain(bg);  // (the clean copy of a hybrid PDF: kept while this document uses it)
        retainedBases.push_back(bg);
        if (hasExtension(doc->getFilepath(), ".pdf")) {  // opened from it: page i was its page i
            for (size_t i = 0; i < doc->getPageCount(); ++i) {
                PageRef p = doc->getPage(i);
                hybridBase[p.get()] = {p, i};
            }
            // What the next Ctrl+S appends to (if the file is still the version the clean copy was made from)
            if (auto rev = HybridPdf::revisionOf(bg, doc->getFilepath()); rev.valid()) {
                hybridRevision = std::make_shared<HybridPdf::Revision>(std::move(rev));
                hybridNumbering = pdfPages->numbering();
                hybridRevisionFile = doc->getFilepath();
            }
        }
    }

    scrollHandler.indexOf = [this](const PageRef& page) { return doc->indexOf(page); };
    scrollHandler.onScrollToPage = [this](size_t page, XojPdfRectangle) {
        setCurrentPageNo(page);
        Q_EMIT scrollToPageRequested(page);
    };

    autosaveTimer.setSingleShot(false);
    connect(&autosaveTimer, &QTimer::timeout, this, [this] { autosaveChanges(); });
    enableAutosave(app.getSettings()->isAutosaveEnabled());
    connect(&app, &AppContext::settingsChanged, this, [this] {
        const int minutes = std::max(1, this->app.getSettings()->getAutosaveTimeout());
        if (this->app.getSettings()->isAutosaveEnabled() != autosaveTimer.isActive() ||
            autosaveTimer.interval() != minutes * 60 * 1000) {
            enableAutosave(this->app.getSettings()->isAutosaveEnabled());
        }
    });
    updatePageActions();
    firePageSelected(0);  // LayerController tracks the current page through the document events
}

DocumentSession::~DocumentSession() {
    // A save that runs (or waits) is finished first: closing never loses it. Nobody hears of it any more.
    destroying = true;
    blockSignals(true);
    waitForSaves();
    for (const auto& b: retainedBases) {
        HybridPdf::release(b);
    }
    autosaveTimer.stop();
    layerController->unregisterListener();
    layerController.reset();
    undoRedo.reset();
}

// --- Control ---------------------------------------------------------------------------------------------------

Settings* DocumentSession::getSettings() const { return app.getSettings(); }
ToolHandler* DocumentSession::getToolHandler() const { return app.getToolHandler(); }
ZoomControl* DocumentSession::getZoomControl() const { return zoomControl; }
Document* DocumentSession::getDocument() const { return doc.get(); }
UndoRedoHandler* DocumentSession::getUndoRedoHandler() const { return undoRedo.get(); }
MainWindow* DocumentSession::getWindow() const { return const_cast<SessionWindow*>(&window); }
ScrollHandler* DocumentSession::getScrollHandler() const { return const_cast<SessionScrollHandler*>(&scrollHandler); }
XournalppCursor* DocumentSession::getCursor() const { return cursor; }
PageTypeHandler* DocumentSession::getPageTypes() const { return app.getPageTypes(); }
LayerController* DocumentSession::getLayerController() const { return layerController.get(); }
ActionDatabase* DocumentSession::getActionDatabase() const { return const_cast<SessionActions*>(&actions); }

PageRef DocumentSession::getCurrentPage() {
    std::shared_lock lock(*doc);
    return doc->getPage(std::min(currentPage, doc->getPageCount() - 1));
}

size_t DocumentSession::getCurrentPageNo() const { return currentPage; }

void DocumentSession::setCurrentPageNo(size_t page) {
    if (page >= doc->getPageCount() || page == currentPage) {
        return;
    }
    currentPage = page;
    headlessView.currentPage = page;
    firePageSelected(page);
    updatePageActions();
    Q_EMIT currentPageChanged(page);
}

void DocumentSession::clearSelectionEndText() { Q_EMIT clearSelectionRequested(); }

void DocumentSession::setCopyCutEnabled(bool enabled) {
    actions.enableAction(Action::COPY, enabled);
    actions.enableAction(Action::CUT, enabled);
}

void DocumentSession::setXournalView(XournalView* view) { window.view = view ? view : &headlessView; }

void DocumentSession::setCursor(XournalppCursor* c) { cursor = c ? c : &headlessCursor; }

void DocumentSession::setZoomControl(ZoomControl* z) { zoomControl = z ? z : &headlessZoom; }

void DocumentSession::insertNewPage(size_t position, bool automatedInsertion) {
    // Port of PageBackgroundChangeController::insertNewPage (default branch: no page type chosen for new pages,
    // so the new page copies the size and background of the current page).
    if (!automatedInsertion) {
        clearSelectionEndText();
    }
    position = std::min(position, doc->getPageCount());
    PageRef current = getCurrentPage();
    auto page = std::make_shared<XojPage>(current->getWidth(), current->getHeight());

    // Port of PageBackgroundChangeController::copyBackgroundFromOtherPage
    PageType bg = current->getBackgroundType();
    page->setBackgroundType(bg);
    if (bg.isPdfPage()) {
        page->setSize(current->getWidth(), current->getHeight());
        page->setBackgroundPdfPageNr(current->getPdfPageNr());
    } else if (bg.isImagePage()) {
        page->setSize(current->getWidth(), current->getHeight());
        page->setBackgroundImage(current->getBackgroundImage());
    } else {
        page->setBackgroundColor(current->getBackgroundColor());
    }
    insertPage(page, position, !automatedInsertion);
}

void DocumentSession::insertPage(const PageRef& page, size_t position, bool shouldScrollToPage) {
    // Port of Control::insertPage
    doc->lock();
    doc->insertPage(page, position);
    doc->unlock();
    addPageUndoAction(std::make_unique<InsertDeletePageUndoAction>(page, position, true));
    firePageInserted(position);
    getCursor()->updateCursor();
    if (shouldScrollToPage) {
        scrollHandler.scrollToPage(position);
        firePageSelected(position);
    }
    updatePageActions();
}

void DocumentSession::updatePageActions() {
    // Port of Control::updatePageActions
    const auto nbPages = doc->getPageCount();
    actions.enableAction(Action::DELETE_PAGE, nbPages > 1);
    actions.enableAction(Action::MOVE_PAGE_TOWARDS_BEGINNING, currentPage != 0);
    actions.enableAction(Action::MOVE_PAGE_TOWARDS_END, currentPage + 1 < nbPages);
}

// --- page operations on several pages (own undo action, see PageOrderUndoAction) ------------------------------

std::vector<PageRef> DocumentSession::pageOrder() const {
    std::shared_lock lock(*doc);
    std::vector<PageRef> pages;
    pages.reserve(doc->getPageCount());
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        pages.push_back(doc->getPage(i));
    }
    return pages;
}

// Every page that comes or goes fires an event - also when it happens through undo - so the links are kept right
// in one place instead of in every operation.
class DocumentSession::PageLinkKeeper final: public DocumentListener {
public:
    explicit PageLinkKeeper(DocumentSession& session): session(session) { registerListener(&session); }
    void documentChanged(DocumentChangeType) override {}
    void pageInserted(size_t page) override { session.shiftPageLinks(page, 1); }
    void pageDeleted(size_t page) override { session.shiftPageLinks(page, -1); }

private:
    DocumentSession& session;
};

// The page revisions follow the same events: every change of a page, whichever way, comes by here. Workers (the
// thumbnails) look pages up by revision, so the list has a lock of its own.
namespace {
std::atomic<quint64> nextPageStamp{1};
}
class DocumentSession::PageRevisionKeeper final: public DocumentListener {
public:
    explicit PageRevisionKeeper(DocumentSession& session): session(session) {
        registerListener(&session);
        rebuild();
    }
    void documentChanged(DocumentChangeType type) override {
        if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
            rebuild();
            Q_EMIT session.pageRevisionsChanged();
        }
    }
    void pageSizeChanged(size_t page) override { session.revisePage(page); }
    void pageChanged(size_t page) override { session.revisePage(page); }
    void pageInserted(size_t page) override {
        PageRef ref;
        {
            std::shared_lock lock(*session.getDocument());
            if (page < session.getDocument()->getPageCount()) {
                ref = session.getDocument()->getPage(page);
            }
        }
        {
            std::lock_guard lock(mtx);
            if (page <= stamps.size() && ref) {
                stamps.insert(stamps.begin() + static_cast<std::ptrdiff_t>(page),
                              PageStamp{nextPageStamp++, nextPageStamp++, ref});
            }
        }
        if (stamps.size() != session.getDocument()->getPageCount()) {
            rebuild();
        }
        Q_EMIT session.pageRevisionsChanged();
    }
    void pageDeleted(size_t page) override {
        // (before the page is gone)
        {
            std::lock_guard lock(mtx);
            if (page < stamps.size()) {
                stamps.erase(stamps.begin() + static_cast<std::ptrdiff_t>(page));
            }
        }
        Q_EMIT session.pageRevisionsChanged();
    }
    void rebuild() {
        std::vector<PageStamp> fresh;
        {
            std::shared_lock lock(*session.getDocument());
            for (size_t i = 0; i < session.getDocument()->getPageCount(); ++i) {
                fresh.push_back(PageStamp{nextPageStamp++, nextPageStamp++, session.getDocument()->getPage(i)});
            }
        }
        std::lock_guard lock(mtx);
        stamps = std::move(fresh);
    }
    mutable std::mutex mtx;
    std::vector<PageStamp> stamps;

private:
    DocumentSession& session;
};

quint64 DocumentSession::pageRevision(size_t page) const {
    std::lock_guard lock(pageRevisionKeeper->mtx);
    const auto& stamps = pageRevisionKeeper->stamps;
    return page < stamps.size() ? stamps[page].revision : 0;
}

quint64 DocumentSession::pageId(size_t page) const {
    std::lock_guard lock(pageRevisionKeeper->mtx);
    const auto& stamps = pageRevisionKeeper->stamps;
    return page < stamps.size() ? stamps[page].id : 0;
}

std::vector<DocumentSession::PageStamp> DocumentSession::pageStamps() const {
    std::lock_guard lock(pageRevisionKeeper->mtx);
    return pageRevisionKeeper->stamps;
}

std::optional<DocumentSession::PageStamp> DocumentSession::pageOfRevision(quint64 revision) const {
    std::lock_guard lock(pageRevisionKeeper->mtx);
    for (const auto& s: pageRevisionKeeper->stamps) {
        if (s.revision == revision) {
            return s;
        }
    }
    return std::nullopt;
}

void DocumentSession::revisePage(size_t page) {
    {
        std::lock_guard lock(pageRevisionKeeper->mtx);
        auto& stamps = pageRevisionKeeper->stamps;
        if (page >= stamps.size()) {
            return;
        }
        stamps[page].revision = nextPageStamp++;
    }
    Q_EMIT pageRevisionsChanged();
}

void DocumentSession::shiftPageLinks(size_t position, int delta) {
    if (pageLinksPaused) {
        return;  // applyPageOrder knows where every page went and does it exactly
    }
    const size_t count = doc->getPageCount();
    // pageDeleted comes before the page is gone, pageInserted after it is there
    const size_t oldCount = delta > 0 ? (count > 0 ? count - 1 : 0) : count;
    std::vector<int> newPage(oldCount, 0);
    for (size_t i = 0; i < oldCount; ++i) {
        if (delta > 0) {
            newPage[i] = static_cast<int>(i < position ? i + 1 : i + 2);
        } else {
            newPage[i] = i == position ? 0 : static_cast<int>(i < position ? i + 1 : i);
        }
    }
    rewritePageLinks(newPage);
}

void DocumentSession::rewritePageLinks(const std::vector<int>& newPage) {
    std::vector<size_t> rewritten;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            const PageRef page = doc->getPage(i);
            for (Layer* layer: page->getLayers()) {
                for (const auto& element: layer->getElements()) {
                    if (element->getType() != ELEMENT_TEXT) {
                        continue;
                    }
                    auto* text = static_cast<Text*>(element.get());
                    std::string content = text->getText();
                    if (content.find('#') != std::string::npos && xoj::util::renumberPageLinks(content, newPage)) {
                        text->setText(std::move(content));
                        if (rewritten.empty() || rewritten.back() != i) {
                            rewritten.push_back(i);
                        }
                    }
                }
            }
        }
    }
    for (size_t i: rewritten) {
        revisePage(i);  // the link text on it changed
    }
}

void DocumentSession::updatePageLinks(const std::vector<PageRef>& before, const std::vector<PageRef>& after) {
    if (before.empty()) {
        return;
    }
    // Where each page went: newPage[old] is the new number (0: it is gone, the link stays as it is)
    std::vector<int> newPage(before.size(), 0);
    for (size_t i = 0; i < before.size(); ++i) {
        const auto it = std::find(after.begin(), after.end(), before[i]);
        if (it != after.end()) {
            newPage[i] = static_cast<int>(std::distance(after.begin(), it)) + 1;
        }
    }
    if (std::equal(newPage.begin(), newPage.end(), before.begin(),
                   [i = 0](int now, const PageRef&) mutable { return now == ++i; })) {
        return;  // nothing moved
    }
    rewritePageLinks(newPage);
}

void DocumentSession::applyPageOrder(const std::vector<PageRef>& target, const std::vector<PageRef>& moved) {
    const auto before = pageOrder();
    pageLinksPaused = true;
    // Pages that go away or move are removed (last first), then the missing ones inserted at their place (first
    // first): the pages in between keep their order, so each insertion index is final. Events as in upstream's
    // InsertDeletePageUndoAction (first the event, then the deletion).
    clearSelectionEndText();
    std::unordered_set<const XojPage*> keep, move;
    for (const auto& p: target) {
        keep.insert(p.get());
    }
    for (const auto& p: moved) {
        move.insert(p.get());
    }
    for (size_t i = doc->getPageCount(); i-- > 0;) {
        const XojPage* p = doc->getPage(i).get();
        if (!keep.count(p) || move.count(p)) {
            firePageDeleted(i);
            doc->lock();
            doc->deletePage(i);
            doc->unlock();
        }
    }
    std::unordered_set<const XojPage*> present;
    for (const auto& p: pageOrder()) {
        present.insert(p.get());
    }
    size_t firstChange = npos;
    for (size_t i = 0; i < target.size(); ++i) {
        if (!present.count(target[i].get())) {
            doc->lock();
            doc->insertPage(target[i], i);
            doc->unlock();
            firePageInserted(i);
            firstChange = std::min(firstChange, i);
        }
    }
    pageLinksPaused = false;
    updatePageLinks(before, target);
    getCursor()->updateCursor();
    const size_t count = doc->getPageCount();
    const size_t show = firstChange != npos ? firstChange : std::min(getCurrentPageNo(), count - 1);
    currentPage = std::numeric_limits<size_t>::max();  // force the update below
    setCurrentPageNo(std::min(show, count - 1));
    scrollHandler.scrollToPage(std::min(show, count - 1));
    updatePageActions();
}

bool DocumentSession::deletePages(std::vector<size_t> pages) {
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    const auto before = pageOrder();
    pages.erase(std::remove_if(pages.begin(), pages.end(), [&](size_t p) { return p >= before.size(); }), pages.end());
    if (pages.empty() || pages.size() >= before.size()) {
        return false;  // there is always at least one page (upstream)
    }
    std::vector<PageRef> after;
    for (size_t i = 0, k = 0; i < before.size(); ++i) {
        if (k < pages.size() && pages[k] == i) {
            ++k;
        } else {
            after.push_back(before[i]);
        }
    }
    applyPageOrder(after, {});
    addPageUndoAction(std::make_unique<PageOrderUndoAction>(
            before, after, std::vector<PageRef>{},
            pages.size() == 1 ? "Delete page" : "Delete " + std::to_string(pages.size()) + " pages"));
    return true;
}

void DocumentSession::insertPages(const std::vector<PageRef>& pages, size_t position) {
    if (pages.empty()) {
        return;
    }
    const auto before = pageOrder();
    position = std::min(position, before.size());
    std::vector<PageRef> after(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(position));
    after.insert(after.end(), pages.begin(), pages.end());
    after.insert(after.end(), before.begin() + static_cast<std::ptrdiff_t>(position), before.end());
    applyPageOrder(after, {});
    addPageUndoAction(std::make_unique<PageOrderUndoAction>(
            before, after, std::vector<PageRef>{},
            pages.size() == 1 ? "Insert page" : "Insert " + std::to_string(pages.size()) + " pages"));
}

bool DocumentSession::movePages(std::vector<size_t> pages, size_t target) {
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    const auto before = pageOrder();
    pages.erase(std::remove_if(pages.begin(), pages.end(), [&](size_t p) { return p >= before.size(); }), pages.end());
    if (pages.empty()) {
        return false;
    }
    target = std::min(target, before.size());
    std::vector<PageRef> moved, rest;
    size_t insertAt = 0;  // in `rest`
    for (size_t i = 0, k = 0; i < before.size(); ++i) {
        if (k < pages.size() && pages[k] == i) {
            moved.push_back(before[i]);
            ++k;
        } else {
            rest.push_back(before[i]);
            if (i < target) {
                ++insertAt;
            }
        }
    }
    std::vector<PageRef> after(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(insertAt));
    after.insert(after.end(), moved.begin(), moved.end());
    after.insert(after.end(), rest.begin() + static_cast<std::ptrdiff_t>(insertAt), rest.end());
    if (after == before) {
        return false;
    }
    applyPageOrder(after, moved);
    addPageUndoAction(std::make_unique<PageOrderUndoAction>(
            before, after, moved, moved.size() == 1 ? "Move page" : "Move " + std::to_string(moved.size()) + " pages"));
    return true;
}

// --- undo/redo ---------------------------------------------------------------------------------------------------

void DocumentSession::undoRedoChanged() {
    actions.enableAction(Action::UNDO, undoRedo->canUndo());
    actions.enableAction(Action::REDO, undoRedo->canRedo());
    Q_EMIT undoRedoStateChanged();
    updateModified();
}

void DocumentSession::undoRedoPageChanged(PageRef page) {
    if (!page) {
        return;
    }
    size_t index = 0;
    {
        std::shared_lock lock(*doc);
        index = doc->indexOf(page);
    }
    if (index != npos) {
        revisePage(index);
        Q_EMIT pageContentChanged(index);
    }
}

void DocumentSession::deletePage() {
    // Port of Control::deletePage
    clearSelectionEndText();
    size_t pNr = getCurrentPageNo();
    // Don't delete the last page: there is always at least one page.
    if (doc->getPageCount() < 2 || pNr >= doc->getPageCount()) {
        return;
    }
    PageRef page;
    {
        std::shared_lock lock(*doc);
        page = doc->getPage(pNr);
    }
    // Upstream: first send the event, then delete the page.
    firePageDeleted(pNr);
    doc->lock();
    doc->deletePage(pNr);
    doc->unlock();
    addPageUndoAction(std::make_unique<InsertDeletePageUndoAction>(page, pNr, false));
    if (pNr >= doc->getPageCount()) {
        pNr = doc->getPageCount() - 1;
    }
    currentPage = std::numeric_limits<size_t>::max();  // force the update below
    setCurrentPageNo(pNr);
    scrollHandler.scrollToPage(pNr);
    updatePageActions();
}

void DocumentSession::duplicatePage() {
    // Port of Control::duplicatePage
    auto page = getCurrentPage();
    if (!page) {
        return;
    }
    auto pageCopy = std::make_shared<XojPage>(*page);
    insertPage(pageCopy, getCurrentPageNo() + 1);
}

void DocumentSession::movePageTowardsBeginning() {
    // Port of Control::movePageTowardsBeginning
    const size_t currentPageNo = getCurrentPageNo();
    if (currentPageNo < 1 || currentPageNo >= doc->getPageCount()) {
        return;
    }
    auto lock = std::unique_lock(*doc);
    PageRef page = doc->getPage(currentPageNo);
    PageRef otherPage = doc->getPage(currentPageNo - 1);
    doc->deletePage(currentPageNo);
    doc->insertPage(page, currentPageNo - 1);
    lock.unlock();

    addPageUndoAction(std::make_unique<SwapUndoAction>(currentPageNo - 1, true, page, otherPage));
    firePageDeleted(currentPageNo);
    firePageInserted(currentPageNo - 1);
    firePageSelected(currentPageNo - 1);
    setCurrentPageNo(currentPageNo - 1);
    scrollHandler.scrollToPage(currentPageNo - 1);
}

void DocumentSession::movePageTowardsEnd() {
    // Port of Control::movePageTowardsEnd
    const size_t currentPageNo = getCurrentPageNo();
    auto lock = std::unique_lock(*doc);
    if (currentPageNo + 1 >= doc->getPageCount()) {
        return;
    }
    PageRef page = doc->getPage(currentPageNo);
    PageRef otherPage = doc->getPage(currentPageNo + 1);
    doc->deletePage(currentPageNo);
    doc->insertPage(page, currentPageNo + 1);
    lock.unlock();

    addPageUndoAction(std::make_unique<SwapUndoAction>(currentPageNo, false, page, otherPage));
    firePageDeleted(currentPageNo);
    firePageInserted(currentPageNo + 1);
    firePageSelected(currentPageNo + 1);
    setCurrentPageNo(currentPageNo + 1);
    scrollHandler.scrollToPage(currentPageNo + 1);
}

// --- file handling -----------------------------------------------------------------------------------------------

bool DocumentSession::hasFilePath() const { return !getFilePath().empty(); }

fs::path DocumentSession::suggestSavePath() const {
    if (text && !hasFilePath()) {
        return text->path();  // (a text file is saved as itself)
    }
    if (!hasFilePath() && !madeSuggestion.empty()) {
        return madeSuggestion;  // (made from another file: next to it)
    }
    Settings* settings = getSettings();
    fs::path background;
    {
        std::shared_lock lock(*doc);
        background = doc->getPdfFilepath();
    }
    if (!hasFilePath() && MergedPdf::inCache(background)) {
        // The merged PDF of pasted pages is no place to save: as for the PDF it was made from, or a new document
        Document shown(&detachedHandler());
        if (const fs::path pdf = annotatedPdf(); !pdf.empty()) {
            shown.setPdfAttributes(pdf, false);
        }
        fs::path suggested = shown.createSaveFoldername(settings->getLastSavePath());
        suggested /= shown.createSaveFilename(Document::XOPP, settings->getDefaultSaveName());
        if (suggested.extension() != ".xopp") {
            suggested += ".xopp";
        }
        return suggested;
    }
    if (!hasFilePath() && !shownPath.empty() && background.empty() && !shownReadOnly) {
        // An image: its .xopp next to it (one document with it in the library)
        fs::path suggested = shownPath;
        suggested.replace_extension(".xopp");
        return suggested;
    }
    std::shared_lock lock(*doc);
    fs::path suggested = doc->createSaveFoldername(settings->getLastSavePath());
    suggested /= doc->createSaveFilename(Document::XOPP, settings->getDefaultSaveName());
    if (suggested.extension() != ".xopp") {
        suggested += ".xopp";
    }
    return suggested;
}

fs::path DocumentSession::getFilePath() const {
    std::shared_lock lock(*doc);
    return doc->getFilepath();
}

fs::path DocumentSession::documentFile() const {
    const fs::path file = getFilePath();
    if (!file.empty()) {
        return file;
    }
    const fs::path pdf = annotatedPdf();
    return pdf.empty() ? shownPath : pdf;
}

bool DocumentSession::isReadOnly() const {
    return !shownPath.empty() && shownReadOnly && !hasFilePath();
}

void DocumentSession::setShownFile(const fs::path& file, bool readOnly) {
    shownPath = file;
    shownReadOnly = readOnly || hasExtension(file, ".md");
    Q_EMIT filePathChanged();
}

// --- a text file edited --------------------------------------------------------------------------------------------

void DocumentSession::setTextFile(std::unique_ptr<TextFile> file, bool readOnly) {
    text = std::move(file);
    shownPath = text ? text->path() : fs::path();
    shownReadOnly = readOnly || !text;
    textModified = false;
    lastAutosavedText = text ? text->text() : std::string();
    updateModified();
    Q_EMIT filePathChanged();
}

bool DocumentSession::isEditableText() const { return text && !hasFilePath() && !shownReadOnly; }

std::string DocumentSession::currentText(bool lock) const {
    // The pages' parts of the page's Markdown text, from the first page on (MarkdownFile.h)
    std::vector<std::string> slices;
    std::shared_lock<Document> guard(*doc, std::defer_lock);
    if (lock) {
        guard.lock();
    }
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        const Layer* layer = md::markdownLayer(doc->getPage(i));
        const Text* box = layer ? md::pageBoxOf(*layer, TextFile::PAGE_MARGIN, TextFile::PAGE_MARGIN) : nullptr;
        std::string slice = box ? box->getText() : std::string();
        if (i > 0 && !md::continues(slice)) {
            break;  // (the text ends before this page)
        }
        slices.push_back(std::move(slice));
    }
    return md::join(slices);
}

void DocumentSession::textEdited() { updateModified(); }

bool DocumentSession::textChangedOnDisk(std::string& bytes) {
    if (!text || hasFilePath() || isSaving()) {
        return false;
    }
    return text->changedOnDisk(&bytes);
}

void DocumentSession::keepTextOverDisk() {
    if (text) {
        text->setStamp(TextFile::stampOf(text->path()));
    }
}

void DocumentSession::textReloaded(std::string bytes) {
    if (!text) {
        return;
    }
    text->setBytes(std::move(bytes));
    text->setStamp(TextFile::stampOf(text->path()));
    lastAutosavedText = text->text();
    updateModified();
}

fs::path DocumentSession::textAutosavePath(qint64 pid, quint64 serial) {
    fs::path p = Util::getAutosaveFilepath();
    p.replace_filename(std::to_string(pid) + "-" + std::to_string(serial) + ".autosave.text");
    return p;
}

fs::path DocumentSession::textEmergencyPath(qint64 pid, quint64 serial) {
    fs::path p = Util::getAutosaveFilepath();
    p.replace_filename(std::to_string(pid) + "-" + std::to_string(serial) + ".emergency.text");
    return p;
}

auto DocumentSession::autosaveText() -> SaveResult {
    if (!text || !isModified()) {
        return {true, {}, {}};
    }
    const std::string now = currentText();
    if (now == lastAutosavedText) {
        return {true, {}, {}};
    }
    const fs::path target = textAutosavePath(Util::getPid(), serialNo);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    std::string error;
    if (!TextFile::writeAtomically(target, now, error)) {
        return {false, FS(_F("Error while autosaving: {1}") % error), {}};
    }
    lastAutosavedText = now;
    setLastAutosaveFile(target);
    return {true, {}, {}};
}

size_t DocumentSession::addPdfPages(const std::string& pdf, std::string& error) { return pdfPages->add(pdf, error); }

XojPdfPageSPtr DocumentSession::pendingPdfPage(size_t number) const { return pdfPages->pendingPage(number); }

fs::path DocumentSession::annotatedPdf() const { return pdfPages->annotatedPdf(); }

bool DocumentSession::loadPdfKeepingPictures(const fs::path& pdf) {
    keepingPictures = true;
    const bool ok = doc->readPdfKeepingOutline(pdf);  // (the same pages: the same outline)
    keepingPictures = false;
    return ok;
}

quint64 DocumentSession::pdfNumbering() const { return pdfPages->numbering(); }

fs::path DocumentSession::mergedPdfPlace() const { return pdfPages->placeFor(getFilePath()); }

std::string DocumentSession::getDisplayName() const {
    std::shared_lock lock(*doc);
    if (auto p = doc->getFilepath(); !p.empty()) {
        return p.filename().u8string().empty() ? std::string() : char_cast(p.filename().u8string().c_str());
    }
    lock.unlock();
    if (auto pdf = annotatedPdf(); !pdf.empty()) {
        return char_cast(pdf.filename().u8string().c_str());
    }
    if (!shownPath.empty()) {
        return char_cast(shownPath.filename().u8string().c_str());
    }
    if (!madeSuggestion.empty()) {
        return char_cast(madeSuggestion.filename().u8string().c_str());
    }
    return _("Untitled");
}

bool DocumentSession::isModified() const {
    if (text && !hasFilePath()) {
        return textModified;  // (the text differs from the file's)
    }
    return undoRedo->isChanged() || saveUnconfirmed || saveFailed || madeUnsaved;
}

void DocumentSession::setMadeFrom(const fs::path& suggestion) {
    madeSuggestion = suggestion;
    madeUnsaved = true;
    updateModified();
    Q_EMIT filePathChanged();
}

void DocumentSession::updateModified() {
    if (text && !hasFilePath()) {
        textModified = currentText() != text->text();
    }
    if (const bool modified = isModified(); modified != lastModified) {
        lastModified = modified;
        Q_EMIT modifiedChanged(modified);
    }
}

void DocumentSession::updatePreview(Document& document) {
    Document* doc = &document;
    // Port of SaveJob::updatePreview: 128 px preview of the first page stored in the file.
    const int previewSize = 128;
    xoj::util::CairoSurfaceSPtr crBuffer;

    doc->lock_shared();
    if (doc->getPageCount() > 0) {
        PageRef page = doc->getPage(0);
        double width = page->getWidth();
        double height = page->getHeight();
        const double zoom = width < height ? previewSize / height : previewSize / width;
        width *= zoom;
        height *= zoom;

        crBuffer.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceil_cast<int>(width), ceil_cast<int>(height)),
                       xoj::util::adopt);
        cairo_t* cr = cairo_create(crBuffer.get());
        cairo_scale(cr, zoom, zoom);

        xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
        // No PdfCache here: render the PDF background by hand (as upstream).
        if (page->getBackgroundType().isPdfPage()) {
            if (XojPdfPageSPtr pdfPage = doc->getPdfPage(page->getPdfPageNr())) {
                pdfPage->render(cr);
            }
            flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
        } else {
            flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
        }
        DocumentView view;
        view.drawPage(page, cr, true, flags);
        cairo_destroy(cr);
    }
    doc->unlock_shared();

    doc->lock();
    doc->setPreview(std::move(crBuffer));
    doc->unlock();
}

auto DocumentSession::writeDocument(Document& doc, const fs::path& target) -> SaveResult {
    updatePreview(doc);
    doc.lock();
    doc.setFilepath(target);  // an attached background PDF is written next to it
    doc.unlock();
    SaveHandler h;
    doc.lock_shared();
    h.prepareSave(&doc, target);
    doc.unlock_shared();
    h.saveTo(target);
    if (!h.getErrorMessage().empty()) {
        return {false, FS(_F("Save file error: {1}") % h.getErrorMessage())};
    }
    doc.lock();
    h.updateDocumentInfo(&doc);
    doc.unlock();
    return {true, {}};
}

void DocumentSession::relocate(const fs::path& xopp, const fs::path& pdf) {
    doc->lock();
    if (!xopp.empty()) {
        doc->setFilepath(xopp);
    }
    if (!pdf.empty()) {
        doc->setPdfAttributes(pdf, doc->isAttachPdf());
    }
    doc->unlock();
    stampFiles();  // (moved or written by the app)
    Q_EMIT filePathChanged();
}

// --- changes on disk -------------------------------------------------------------------------------------------------

std::vector<fs::path> DocumentSession::filesOnDisk() const {
    if (text || (!shownPath.empty() && !hasFilePath())) {
        return {};
    }
    std::vector<fs::path> files;
    const fs::path main = documentFile();
    if (!main.empty()) {
        files.push_back(main);
    }
    fs::path pdf;
    {
        std::shared_lock lock(*doc);
        pdf = doc->getPdfFilepath();
    }
    // (the merged PDF of pasted pages and the clean copies of hybrid PDFs in the app cache are the app's alone)
    const auto inCache = [](const fs::path& p) {
        const fs::path cache = Util::getCacheSubfolder().lexically_normal();
        const fs::path rel = p.lexically_normal().lexically_relative(cache);
        return !rel.empty() && *rel.begin() != "..";
    };
    if (!pdf.empty() && pdf != main && !inCache(pdf)) {
        files.push_back(pdf);
    }
    return files;
}

std::optional<DocumentSession::DiskStamp> DocumentSession::diskStampOf(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = fs::last_write_time(file, ec);
    if (ec) {
        return std::nullopt;
    }
    DiskStamp stamp;
    stamp.size = size;
    stamp.time = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    // The first and the last 64 KB (the end of a PDF has its cross-reference table, of a .xopp gzip's checksum)
    QFile f(QString::fromStdString(file.string()));
    if (f.open(QIODevice::ReadOnly)) {
        constexpr qint64 PART = 64 * 1024;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(f.read(PART));
        if (f.size() > PART) {
            f.seek(std::max(PART, f.size() - PART));
            hash.addData(f.read(PART));
        }
        stamp.sample = hash.result();
    }
    return stamp;
}

void DocumentSession::stampFiles() {
    diskStamps.clear();
    for (const fs::path& f: filesOnDisk()) {
        if (auto stamp = diskStampOf(f)) {
            diskStamps[f] = std::move(*stamp);
        }
    }
}

bool DocumentSession::filesChangedOnDisk() {
    if (isSaving() || pdfWorkRunning() || mergingPdfPages()) {
        return false;  // (asked again after it: the save records what it wrote)
    }
    bool changed = false;
    std::map<fs::path, DiskStamp> next;
    for (const fs::path& f: filesOnDisk()) {
        const auto now = diskStampOf(f);
        const auto known = diskStamps.find(f);
        if (known == diskStamps.end()) {
            if (now) {
                next[f] = *now;  // (a file the document took since: as it is)
            }
            continue;
        }
        if (!now || (now->size == known->second.size && now->time == known->second.time)) {
            next[f] = known->second;  // (unchanged; or gone for a moment: asked again when it is back)
            continue;
        }
        if (now->size == known->second.size && now->sample == known->second.sample) {
            next[f] = *now;  // (only touched)
            continue;
        }
        changed = true;
        next[f] = known->second;  // (until it is read again or kept: stampFiles)
    }
    diskStamps = std::move(next);
    return changed;
}

bool DocumentSession::isHybrid() const { return hasExtension(getFilePath(), ".pdf"); }

bool DocumentSession::hasEarlierRevisions() const {
    std::error_code ec;
    return isHybrid() && fs::exists(getFilePath(), ec) && HybridPdf::hasEarlierRevisions(getFilePath());
}

fs::path DocumentSession::xoppExport() const {
    if (!isHybrid()) {
        return {};
    }
    if (const fs::path file = getFilePath(); xoppExportFor != file) {
        xoppExportPath = HybridPdf::xoppExportOf(file);
        xoppExportFor = file;
    }
    return xoppExportPath;
}

bool DocumentSession::detachBackground(const std::vector<fs::path>& files, std::string& error) {
    fs::path bg;
    {
        std::shared_lock lock(*doc);
        bg = doc->getPdfFilepath();
    }
    std::error_code ec;
    if (bg.empty() || !fs::exists(bg, ec) ||
        std::none_of(files.begin(), files.end(), [&](const fs::path& f) {
            std::error_code eec;
            return fs::exists(f, eec) && fs::equivalent(f, bg, eec);
        })) {
        return true;
    }
    static std::atomic<unsigned> counter{0};
    const fs::path copy = HybridPdf::cacheFolder() /
                          ("moved-" + std::to_string(Util::getPid()) + "-" + std::to_string(serialNo) + "-" +
                           std::to_string(++counter)) /
                          "base.pdf";
    fs::create_directories(copy.parent_path(), ec);
    fs::create_hard_link(bg, copy, ec);  // (no copying of a long PDF; the file keeps its data when it goes)
    if (ec) {
        ec.clear();
        fs::copy_file(bg, copy, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        error = FS(_F("Could not copy the PDF \"{1}\": {2}") % bg.u8string() % ec.message());
        return false;
    }
    if (!loadPdfKeepingPictures(copy)) {  // (the same file)
        error = FS(_F("Could not copy the PDF \"{1}\": {2}") % bg.u8string() % doc->getLastErrorMsg());
        return false;
    }
    HybridPdf::retain(copy);
    retainedBases.push_back(copy);
    return true;
}

bool DocumentSession::importHybridChanges(std::string& error) {
    if (!isHybrid() || hybridChanges.empty()) {
        return false;
    }
    const fs::path base = HybridPdf::importCopy(getFilePath(), hybridChanges, error);
    if (base.empty()) {
        return false;
    }
    hybridRevision.reset();  // (the other app's ink becomes plain annotations: the next save writes the file anew)
    if (!doc->readPdf(base, /*initPages=*/false, /*attachToDocument=*/false)) {
        error = doc->getLastErrorMsg();
        return false;
    }
    HybridPdf::retain(base);
    retainedBases.push_back(base);
    clearSelectionEndText();
    std::set<size_t> pages;
    for (const auto& name: hybridChanges) {
        size_t pageNo = 0, layerNo = 0;
        if (!HybridPdf::parseName(name, pageNo, layerNo)) {
            continue;
        }
        pages.insert(pageNo);
        std::unique_lock lock(*doc);
        if (pageNo >= doc->getPageCount()) {
            continue;
        }
        PageRef page = doc->getPage(pageNo);
        auto& layers = page->getLayers();
        if (layerNo >= layers.size()) {
            continue;
        }
        Layer* layer = layers[layerNo];
        auto elements = layer->clearNoFree();
        if (elements.empty()) {
            continue;
        }
        auto undo = std::make_unique<DeleteUndoAction>(page, false);
        Element::Index pos = 0;
        for (auto& e: elements) {
            undo->addElement(layer, std::move(e), pos++);
        }
        lock.unlock();
        undoRedo->addUndoAction(std::move(undo));
    }
    hybridChanges.clear();
    for (size_t p: pages) {
        if (p < doc->getPageCount()) {
            firePageChanged(p);
            revisePage(p);
        }
    }
    return true;
}

fs::path DocumentSession::exportPdfFor(const fs::path& xopp) {
    const fs::path pair = MergedPdf::pairOf(xopp);
    std::error_code ec;
    if (!fs::exists(pair, ec) || MergedPdf::kindOf(pair) != MergedPdf::Kind::None) {
        return pair;  // (the library shows the two as one document)
    }
    return MergedPdf::sidecarOf(xopp);
}

auto DocumentSession::autosave() -> SaveResult {
    // Port of AutosaveJob::run
    SaveHandler handler;
    undoRedo->documentAutosaved();

    const fs::path filepath = autosavePath();
    doc->lock_shared();
    handler.prepareSave(doc.get(), filepath);
    doc->unlock_shared();

    g_message("%s", FS(_F("Autosaving to {1}") % filepath.string()).c_str());

    fs::path tempfile = filepath;
    tempfile += u8"~";
    handler.saveTo(tempfile);

    doc->lock();
    handler.updateDocumentInfo(doc.get());
    doc->unlock();

    if (const auto& error = handler.getErrorMessage(); !error.empty()) {
        return {false, FS(_F("Error while autosaving: {1}") % error)};
    }
    try {
        if (fs::exists(filepath)) {
            fs::path swaptmpfile = filepath;
            swaptmpfile += u8".swap";
            Util::safeRenameFile(filepath, swaptmpfile);
            Util::safeRenameFile(tempfile, filepath);
            fs::remove(swaptmpfile);
        } else {
            Util::safeRenameFile(tempfile, filepath);
        }
        setLastAutosaveFile(filepath);
    } catch (const fs::filesystem_error& e) {
        return {false, FS(_F("Could not rename autosave file from \"{1}\" to \"{2}\": {3}") % tempfile.u8string() %
                          filepath.u8string() % e.what())};
    }
    return {true, {}};
}

bool DocumentSession::autosaveChanges() {
    // (not while a save writes the merged PDF: the autosave would refer to a file that is being moved)
    if (text) {
        const std::string before = lastAutosavedText;
        return autosaveText().ok && lastAutosavedText != before;  // (a text file: its text, if it changed)
    }
    if (undoRedo->isChangedAutosave() && !pdfWorkRunning()) {
        return autosave().ok;
    }
    return false;
}

namespace {
#ifdef Q_OS_ANDROID
bool autosavesInAppCache = true;
#else
bool autosavesInAppCache = false;
#endif
}  // namespace

bool DocumentSession::autosaveInAppCache() { return autosavesInAppCache; }
void DocumentSession::setAutosaveInAppCache(bool inAppCache) { autosavesInAppCache = inAppCache; }

fs::path DocumentSession::unnamedAutosavePath(qint64 pid, quint64 serial) {
    // xournal-qt: upstream uses "<pid>.xopp" (one document per process); here every tab needs its own file.
    fs::path p = Util::getAutosaveFilepath();
    p.replace_filename(std::to_string(pid) + "-" + std::to_string(serial) + ".autosave.xopp");
    return p;
}

fs::path DocumentSession::emergencyPath(qint64 pid, quint64 serial) {
    fs::path p = Util::getAutosaveFilepath();
    p.replace_filename(std::to_string(pid) + "-" + std::to_string(serial) + ".emergency.xopp");
    return p;
}

fs::path DocumentSession::autosavePath() const {
    // Port of AutosaveJob::run (target path)
    fs::path filepath;
    {
        std::shared_lock lock(*doc);
        filepath = doc->getFilepath();
    }
    // PDF files mode (DocumentMode.h): nothing next to the user's files, the autosave of a saved document goes to the
    // app cache too (under the tab's name, as for unsaved documents; recovery looks there as well)
    // The same on Android (autosaveInAppCache): sync apps would upload a ".name.autosave.xopp" next to the document.
    if (filepath.empty() || DocumentMode::pdfOnly(*app.getSettings()) || autosaveInAppCache()) {
        return unnamedAutosavePath(Util::getPid(), serialNo);
    }
    return namedAutosavePath(filepath);
}

fs::path DocumentSession::namedAutosavePath(fs::path document) {
    document.replace_filename(fs::path(".") += document.filename());
    Util::clearExtensions(document);
    document += ".autosave.xopp";
    return document;
}

void DocumentSession::markRecovered(const fs::path& original) {
    // Like upstream's checkForEmergencySave: the content is not saved anywhere yet.
    doc->lock();
    doc->setFilepath(original);
    doc->unlock();
    undoRedo->addUndoAction(std::make_unique<EmergencySaveRestore>());
    Q_EMIT filePathChanged();
}

void DocumentSession::setLastAutosaveFile(fs::path file) {
    // Port of Control::setLastAutosaveFile
    try {
        if (!lastAutosaveFile.empty() && fs::exists(file) && !fs::equivalent(file, lastAutosaveFile)) {
            deleteAutosaveFile();
        }
    } catch (const fs::filesystem_error& e) {
        g_warning("Could not compare autosave files: %s", e.what());
    }
    lastAutosaveFile = std::move(file);
}

void DocumentSession::deleteAutosaveFile() {
    try {
        if (!lastAutosaveFile.empty() && fs::exists(lastAutosaveFile)) {
            fs::remove(lastAutosaveFile);
        }
    } catch (const fs::filesystem_error& e) {
        g_warning("%s", FS(_F("Could not remove old autosave file \"{1}\": {2}") % lastAutosaveFile.u8string() %
                           e.what())
                                .c_str());
    }
    lastAutosaveFile.clear();
}

void DocumentSession::enableAutosave(bool enable) {
    autosaveTimer.stop();
    if (enable) {
        const int minutes = std::max(1, app.getSettings()->getAutosaveTimeout());
        autosaveTimer.start(minutes * 60 * 1000);
    }
}

}  // namespace xqt
