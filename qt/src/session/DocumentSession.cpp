#include "DocumentSession.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <utility>

#include <glib.h>

#include "control/layer/LayerController.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "undo/InsertDeletePageUndoAction.h"
#include "util/PathUtil.h"
#include "util/PlaceholderString.h"
#include "util/i18n.h"
#include "util/raii/CairoWrappers.h"
#include "util/safe_casts.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

#include "AppContext.h"
#include "config.h"  // for FILE_FORMAT_VERSION

namespace xqt {

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

auto DocumentSession::loadFile(const fs::path& path, bool attachPdf) -> LoadResult {
    LoadResult result;
    if (hasExtension(path, ".pdf")) {
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
            result.document->setDocumentHandler(&detachedHandler());  // the LoadHandler's handler dies with it
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
}

void DocumentSession::init() {
    window.view = &headlessView;
    undoRedo = std::make_unique<UndoRedoHandler>(this);
    undoRedo->addUndoRedoListener(this);
    layerController = std::make_unique<LayerController>(this);
    layerController->registerListener(this);

    scrollHandler.indexOf = [this](const PageRef& page) { return doc->indexOf(page); };
    scrollHandler.onScrollToPage = [this](size_t page, XojPdfRectangle) {
        setCurrentPageNo(page);
        Q_EMIT scrollToPageRequested(page);
    };

    autosaveTimer.setSingleShot(false);
    connect(&autosaveTimer, &QTimer::timeout, this, [this] {
        if (undoRedo->isChangedAutosave()) {
            autosave();
        }
    });
    enableAutosave(app.getSettings()->isAutosaveEnabled());
    updatePageActions();
    firePageSelected(0);  // LayerController tracks the current page through the document events
}

DocumentSession::~DocumentSession() {
    autosaveTimer.stop();
    layerController->unregisterListener();
    layerController.reset();
    undoRedo.reset();
}

// --- Control ---------------------------------------------------------------------------------------------------

Settings* DocumentSession::getSettings() const { return app.getSettings(); }
ToolHandler* DocumentSession::getToolHandler() const { return app.getToolHandler(); }
ZoomControl* DocumentSession::getZoomControl() const { return nullptr; }  // provided by the canvas (M4)
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
    undoRedo->addUndoAction(std::make_unique<InsertDeletePageUndoAction>(page, position, true));
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

// --- undo/redo ---------------------------------------------------------------------------------------------------

void DocumentSession::undoRedoChanged() {
    actions.enableAction(Action::UNDO, undoRedo->canUndo());
    actions.enableAction(Action::REDO, undoRedo->canRedo());
    Q_EMIT undoRedoStateChanged();
    if (const bool modified = undoRedo->isChanged(); modified != lastModified) {
        lastModified = modified;
        Q_EMIT modifiedChanged(modified);
    }
}

void DocumentSession::undoRedoPageChanged(PageRef) {}

// --- file handling -----------------------------------------------------------------------------------------------

bool DocumentSession::hasFilePath() const { return !getFilePath().empty(); }

fs::path DocumentSession::getFilePath() const {
    std::shared_lock lock(*doc);
    return doc->getFilepath();
}

std::string DocumentSession::getDisplayName() const {
    std::shared_lock lock(*doc);
    if (auto p = doc->getFilepath(); !p.empty()) {
        return p.filename().u8string().empty() ? std::string() : char_cast(p.filename().u8string().c_str());
    }
    if (auto pdf = doc->getPdfFilepath(); !pdf.empty()) {
        return char_cast(pdf.filename().u8string().c_str());
    }
    return _("Untitled");
}

bool DocumentSession::isModified() const { return undoRedo->isChanged(); }

void DocumentSession::updatePreview() {
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

auto DocumentSession::saveImpl(fs::path target) -> SaveResult {
    // Port of SaveJob::save
    updatePreview();
    SaveHandler h;

    doc->lock_shared();
    Util::safeReplaceExtension(target, "xopp");
    h.prepareSave(doc.get(), target);
    doc->unlock_shared();

    const bool createBackup = doc->shouldCreateBackupOnSave();
    if (createBackup) {
        try {
            // The backup must be created for the target: this is the file that will be written.
            Util::safeRenameFile(target, fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not create backup! Failed with %s", fe.what());
            return {false, FS(_F("Save file error, can't backup: {1}") % std::string(fe.what()))};
        }
    }

    h.saveTo(target);

    doc->lock();
    h.updateDocumentInfo(doc.get());
    doc->setFilepath(target);
    doc->unlock();

    if (!h.getErrorMessage().empty()) {
        return {false, FS(_F("Save file error: {1}") % h.getErrorMessage())};
    }
    if (createBackup) {
        try {
            fs::remove(fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not delete backup! Failed with %s", fe.what());
        }
    } else {
        doc->setCreateBackupOnSave(true);
    }

    // Port of Control::resetSavedStatus
    undoRedo->documentSaved();
    undoRedoChanged();
    Q_EMIT filePathChanged();
    return {true, {}};
}

auto DocumentSession::save() -> SaveResult {
    if (!hasFilePath()) {
        return {false, _("The document has no file name yet (use \"Save as\").")};
    }
    return saveImpl(getFilePath());
}

auto DocumentSession::saveAs(fs::path target) -> SaveResult {
    // Like Control::saveImpl(saveAs=true): the document takes the new path before saving (the location of an
    // attached background PDF is derived from it).
    doc->lock();
    doc->setFilepath(target);
    doc->unlock();
    return saveImpl(std::move(target));
}

auto DocumentSession::autosave() -> SaveResult {
    // Port of AutosaveJob::run
    SaveHandler handler;
    undoRedo->documentAutosaved();

    doc->lock_shared();
    auto filepath = doc->getFilepath();
    if (filepath.empty()) {
        filepath = Util::getAutosaveFilepath();
    } else {
        filepath.replace_filename(fs::path(".") += filepath.filename());
    }
    Util::clearExtensions(filepath);
    filepath += ".autosave.xopp";
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
