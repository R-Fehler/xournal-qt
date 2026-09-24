/*
 * xournal-qt: saving a DocumentSession in the background (DocumentSession::saveInBackground).
 *
 * A save goes through steps. Those that need the document run on its thread, the file work on a worker, and the
 * document can be edited in between:
 *   1. (this thread) What to write first: for a .xopp the merged PDF of pasted pages (PdfPageKeeper::planSave), for
 *      a hybrid PDF the copies it needs of the file it replaces.
 *   2. (worker) Write them, if there are any.
 *   3. (this thread) The document takes the merged PDF (its pages renumbered). If pages came back meanwhile that
 *      show PDF pages it dropped, or pages were pasted, back to 1.
 *   4. (this thread) A copy of the document's pages: the state that is saved. The undo stack's saved point is set to
 *      it; the document stays modified until the file is written.
 *   5. (worker) Write the file from the copy: its preview, then the .xopp (with a merged PDF written under another
 *      name: the .xopp referring to that name, the PDF gets its name, the .xopp again, as before) or the hybrid PDF.
 *   6. (this thread) The document learns what was written; the next waiting save starts.
 * A worker step reports back through the event loop; waitForSaves() runs the same steps without it.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cctype>
#include <exception>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <QMetaObject>
#include <QString>
#include <QThreadPool>

#include <cairo.h>
#include <glib.h>

#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "util/PlaceholderString.h"
#include "util/Util.h"
#include "util/i18n.h"
#include "util/raii/CairoWrappers.h"
#include "util/safe_casts.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

#include "DocumentSaveTask.h"
#include "DocumentSession.h"
#include "HybridPdf.h"
#include "MergedPdf.h"
#include "PdfPageKeeper.h"
#include "TextFile.h"

namespace xqt {

namespace {
QThreadPool& savePool() {
    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;  // (never destroyed: a session waits for its work before it goes)
        p->setMaxThreadCount(2);
        return p;
    }();
    return *pool;
}

/// Receives the events of the copies. It has no listeners.
DocumentHandler& copyHandler() {
    static DocumentHandler handler;
    return handler;
}

bool hasExtension(const fs::path& p, const char* ext) {
    auto e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ext;
}

fs::path backgroundOf(Document& doc) {
    std::shared_lock lock(doc);
    return doc.getPdfFilepath();
}

/// A deep copy of a page: its layers and elements (the copy constructor leaves out what is visible and the name of
/// its background).
PageRef copyOf(const PageRef& page) {
    struct Access: XojPage {
        using XojPage::setLayerVisible;  // (for the LayerController only)
    };
    constexpr auto setLayerVisible = &Access::setLayerVisible;
    auto copy = std::make_shared<XojPage>(*page);
    for (Layer::Index i = 0; i <= page->getLayerCount(); ++i) {  // (0: the background)
        ((*copy).*setLayerVisible)(i, page->isLayerVisible(i));
    }
    if (page->backgroundHasName()) {
        copy->setBackgroundName(page->getBackgroundName());
    }
    return copy;
}

/// What the writers read of a document, copied (the caller holds its read lock). Its PDF is not loaded: the writers
/// only need its file and number of pages.
std::unique_ptr<Document> snapshotOf(const Document& doc) {
    auto copy = std::make_unique<Document>(&copyHandler());
    copy->setFilepath(doc.getFilepath());
    copy->setPdfAttributes(doc.getPdfFilepath(), doc.isAttachPdf());
    copy->setPathStorageMode(doc.getPathStorageMode());
    copy->setCreateBackupOnSave(doc.shouldCreateBackupOnSave());
    copy->setPreview(doc.getPreview());
    std::vector<PageRef> pages;
    pages.reserve(doc.getPageCount());
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        pages.push_back(copyOf(doc.getPage(i)));
    }
    copy->addPages(pages.begin(), pages.end());
    return copy;
}

/// Port of SaveJob::updatePreview: the 128 px preview of the first page stored in the file (any thread).
xoj::util::CairoSurfaceSPtr previewOf(const PageRef& page, const XojPdfPageSPtr& pdf) {
    if (!page) {
        return {};
    }
    const int previewSize = 128;
    double width = page->getWidth();
    double height = page->getHeight();
    const double zoom = width < height ? previewSize / height : previewSize / width;
    width *= zoom;
    height *= zoom;
    xoj::util::CairoSurfaceSPtr buffer(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceil_cast<int>(width), ceil_cast<int>(height)),
            xoj::util::adopt);
    cairo_t* cr = cairo_create(buffer.get());
    cairo_scale(cr, zoom, zoom);
    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
    if (page->getBackgroundType().isPdfPage()) {
        if (pdf) {
            pdf->render(cr);  // (poppler draws one page of a document at a time: its own lock)
        }
        flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
    } else {
        flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
    }
    DocumentView view;
    view.drawPage(page, cr, true, flags);
    cairo_destroy(cr);
    return buffer;
}

/// Port of SaveJob::save for a copy of the document: the file only (the document learns of it on its thread).
DocumentSession::SaveResult writeXoppFile(Document& copy, const fs::path& target, bool createBackup,
                                          std::unique_ptr<SaveHandler>& h) {
    h = std::make_unique<SaveHandler>();
    {
        std::shared_lock lock(copy);
        h->prepareSave(&copy, target);
    }
    if (createBackup) {
        try {
            // The backup must be created for the target: this is the file that will be written.
            Util::safeRenameFile(target, fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not create backup! Failed with %s", fe.what());
            return {false, FS(_F("Save file error, can't backup: {1}") % std::string(fe.what()))};
        }
    }
    h->saveTo(target);
    if (!h->getErrorMessage().empty()) {
        return {false, FS(_F("Save file error: {1}") % h->getErrorMessage())};
    }
    if (createBackup) {
        try {
            fs::remove(fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not delete backup! Failed with %s", fe.what());
        }
    }
    return {true, {}};
}

bool stopAt(int step) { return PdfPageKeeper::stopSaveAt && PdfPageKeeper::stopSaveAt(step); }
}  // namespace

// --- the interface -------------------------------------------------------------------------------------------------

bool DocumentSession::isSaving() const { return (saveTask && !saveTask->merge) || !saveQueue.empty(); }

bool DocumentSession::mergingPdfPages() const { return (saveTask && saveTask->merge) || !mergeQueue.empty(); }

bool DocumentSession::pdfWorkRunning() const { return (saveTask && saveTask->pdfWork) || mergingPdfPages(); }

void DocumentSession::queueMerge(std::shared_ptr<PdfMerge> merge) {
    mergeQueue.push_back(std::move(merge));
    startNextSave();
}

void DocumentSession::saveInBackground(SaveRequest request) {
    if (request.kind == SaveKind::Save && !saveQueue.empty() && saveQueue.back().kind == SaveKind::Save) {
        // Ctrl+S again while a save waits: that one writes the latest state, for both
        SaveRequest& waiting = saveQueue.back();
        waiting.done = [first = std::move(waiting.done), second = std::move(request.done)](const SaveResult& r) {
            if (first) {
                first(r);
            }
            if (second) {
                second(r);
            }
        };
        return;
    }
    saveQueue.push_back(std::move(request));
    startNextSave();
}

auto DocumentSession::saveNow(SaveRequest request) -> SaveResult {
    auto result = std::make_shared<SaveResult>();
    auto done = std::move(request.done);
    request.done = [result, done](const SaveResult& r) {
        *result = r;
        if (done) {
            done(r);
        }
    };
    saveInBackground(std::move(request));
    waitForSaves();
    return *result;
}

auto DocumentSession::save() -> SaveResult { return saveNow({SaveKind::Save, {}, {}, {}}); }

auto DocumentSession::saveAs(fs::path target) -> SaveResult {
    return saveNow({SaveKind::SaveAs, std::move(target), {}, {}});
}

auto DocumentSession::saveAsHybrid(fs::path target) -> SaveResult {
    return saveNow({SaveKind::Hybrid, std::move(target), {}, {}});
}

auto DocumentSession::exportXopp(const fs::path& xopp) -> SaveResult {
    return saveNow({SaveKind::ExportXopp, xopp, {}, {}});
}

bool DocumentSession::waitForSaves() {
    while (saveTask || !saveQueue.empty() || !mergeQueue.empty()) {  // (the merges of pasted pages too)
        if (!saveTask) {
            startNextSave();
            continue;
        }
        if (!saveTask->then) {
            break;  // (called from within one of its own steps: it goes on from there)
        }
        resumeSave(saveStage);
    }
    return lastSaveResult.ok;
}

void DocumentSession::waitForMerges() {
    while (mergingPdfPages()) {
        if (!saveTask) {
            startNextSave();
            continue;
        }
        if (!saveTask->then) {
            break;  // (called from within one of the steps)
        }
        resumeSave(saveStage);
    }
}

void DocumentSession::waitForPdfWork() {
    while (saveTask && saveTask->pdfWork && saveTask->then) {
        resumeSave(saveStage);
    }
}

// --- the steps -----------------------------------------------------------------------------------------------------

void DocumentSession::startNextSave() {
    if (!saveTask && !mergeQueue.empty()) {
        // Pasted PDF pages first: the saves that wait refer to them
        saveTask = std::make_unique<SaveTask>();
        saveTask->merge = std::move(mergeQueue.front());
        mergeQueue.pop_front();
        beginMerge();
    } else if (!saveTask && !saveQueue.empty()) {
        saveTask = std::make_unique<SaveTask>();
        saveTask->request = std::move(saveQueue.front());
        saveQueue.pop_front();
        updateSaving();
        beginSave();  // (a save that fails at once starts the next one from finishSave)
    }
    updateSaving();
}

void DocumentSession::beginMerge() {
    PdfMerge& merge = *saveTask->merge;
    pdfPages->startMerge(merge);
    onWorker([&merge] { PdfPageKeeper::writeMerge(merge); },
             [this] {
                 const std::unique_ptr<SaveTask> task = std::move(saveTask);
                 ++saveStage;
                 const std::string error = pdfPages->finishMerge(*task->merge);
                 if (!error.empty()) {
                     g_warning("Could not add the PDF pages to the document's PDF: %s", error.c_str());
                     Q_EMIT pdfPagesFailed(QString::fromStdString(error));
                 }
                 startNextSave();
             });
}

bool DocumentSession::yieldToMerges() {
    if (mergeQueue.empty()) {
        return false;
    }
    saveQueue.push_front(std::move(saveTask->request));
    saveTask.reset();
    ++saveStage;
    startNextSave();
    return true;
}

void DocumentSession::updateSaving() {
    if (const bool saving = isSaving(); saving != lastSaving) {
        lastSaving = saving;
        Q_EMIT savingChanged(saving);
    }
}

void DocumentSession::beginSave() {
    SaveTask& t = *saveTask;
    if (text && !hasFilePath() && (t.request.kind == SaveKind::Save || t.request.kind == SaveKind::SaveAs)) {
        return beginTextSave();  // a text file: its text, never a .xopp
    }
    switch (t.request.kind) {
        case SaveKind::Save:
            if (!hasFilePath()) {
                return finishSave({false, _("The document has no file name yet (use \"Save as\")."), {}});
            }
            t.target = getFilePath();
            t.hybrid = isHybrid();
            break;
        case SaveKind::SaveAs:
            t.target = t.request.target;
            // Like Control::saveImpl(saveAs=true): the document takes the new path before saving (the location of an
            // attached background PDF is derived from it).
            doc->lock();
            doc->setFilepath(t.target);
            doc->unlock();
            break;
        case SaveKind::Hybrid:
            t.target = t.request.target;
            if (!hasExtension(t.target, ".pdf")) {
                t.target += ".pdf";
            }
            t.hybrid = true;
            break;
        case SaveKind::ExportXopp:
            t.target = t.request.target;
            t.expectedBg = backgroundOf(*doc);
            return takeSnapshot();
    }
    if (!t.hybrid) {
        Util::safeReplaceExtension(t.target, "xopp");
    }
    planFiles();
}

void DocumentSession::planFiles() {
    if (yieldToMerges()) {
        return;
    }
    SaveTask& t = *saveTask;
    const fs::path bg = backgroundOf(*doc);
    t.expectedBg = bg;
    if (t.hybrid) {
        std::error_code ec;
        t.original.clear();
        t.ownCopy.clear();
        const bool exists = fs::exists(t.target, ec);
        if (exists && !HybridPdf::isHybrid(t.target)) {
            // A PDF of the user's becomes a hybrid PDF (notes saved into the PDF itself): its original is kept once
            fs::path original = t.target;
            original.replace_extension(".original.pdf");
            if (!fs::exists(original, ec)) {
                t.original = original;
            }
        }
        if (exists && !bg.empty() && fs::exists(bg, ec) && fs::equivalent(bg, t.target, ec)) {
            // The pages come from the file that is written: from a copy of it from now on (the same pages, numbers)
            t.ownCopy = HybridPdf::cacheFolder() /
                        ("own-" + std::to_string(Util::getPid()) + "-" + std::to_string(serialNo)) / "base.pdf";
        }
        if (t.original.empty() && t.ownCopy.empty()) {
            return takeSnapshot();
        }
        onWorker(
                [&t] {
                    std::error_code ec;
                    if (!t.original.empty()) {
                        fs::copy_file(t.target, t.original, ec);
                        if (ec) {
                            t.result.error = FS(_F("Could not keep the original PDF as \"{1}\": {2}") %
                                                t.original.u8string() % ec.message());
                            return;
                        }
                    }
                    if (!t.ownCopy.empty()) {
                        fs::create_directories(t.ownCopy.parent_path(), ec);
                        fs::copy_file(t.target, t.ownCopy, fs::copy_options::overwrite_existing, ec);
                        if (ec) {
                            t.result.error = FS(_F("Could not copy the PDF \"{1}\" before writing into it: {2}") %
                                                t.target.u8string() % ec.message());
                        }
                    }
                },
                [this] {
                    SaveTask& t = *saveTask;
                    if (!t.result.error.empty()) {
                        return finishSave(t.result);
                    }
                    if (backgroundOf(*doc) != t.expectedBg) {
                        return planFiles();  // (pages were pasted meanwhile)
                    }
                    if (!t.ownCopy.empty()) {
                        if (!loadPdfKeepingPictures(t.ownCopy)) {  // (a copy of the same file)
                            return finishSave({false,
                                               FS(_F("Could not copy the PDF \"{1}\" before writing into it: {2}") %
                                                  t.target.u8string() % doc->getLastErrorMsg()),
                                               {}});
                        }
                        HybridPdf::retain(t.ownCopy);
                        retainedBases.push_back(t.ownCopy);
                        t.expectedBg = t.ownCopy;
                    }
                    takeSnapshot();
                });
        return;
    }
    if (++t.plans > 3) {
        // Pages keep coming back while the merged PDF is written: all at once on this thread (as before)
        pdfPages->beforeSave(t.target);
        hybridBase.clear();
        t.expectedBg = backgroundOf(*doc);
        return takeSnapshot();
    }
    t.plan = pdfPages->planSave(t.target);
    if (!t.plan.needed) {
        return takeSnapshot();
    }
    t.pdfWork = true;
    onWorker([&plan = t.plan] { PdfPageKeeper::writePlanned(plan); },
             [this] {
                 SaveTask& t = *saveTask;
                 t.pdfWork = false;
                 if (!pdfPages->applySave(t.plan)) {
                     return planFiles();  // (pages came back that show PDF pages it dropped)
                 }
                 if (t.plan.written) {
                     hybridBase.clear();  // (the PDF pages may have been renumbered)
                 }
                 t.expectedBg = backgroundOf(*doc);
                 // (from the event loop: not within a paste or an undo that waited for the PDF)
                 postStep([this] { takeSnapshot(); });
             });
}

void DocumentSession::takeSnapshot() {
    if (yieldToMerges()) {
        return;  // (its copy would show pages whose PDF pages are not in the file yet)
    }
    SaveTask& t = *saveTask;
    const bool exporting = t.request.kind == SaveKind::ExportXopp;
    if (!exporting && backgroundOf(*doc) != t.expectedBg) {
        return planFiles();  // pages were pasted meanwhile: their PDF goes next to the document too
    }
    clearSelectionEndText();  // like upstream's Control::saveImpl: the selected elements go back first
    {
        std::shared_lock lock(*doc);
        if (!t.hybrid && !exporting && doc->isAttachPdf() && !doc->getPdfFilepath().empty()) {
            // An attached PDF is written next to the document once (SaveHandler), from the loaded PDF: here
            fs::path attached = doc->getFilepath();
            Util::clearExtensions(attached);
            attached += ".xopp.bg.pdf";
            std::error_code ec;
            if (!fs::exists(attached, ec)) {
                GError* error = nullptr;
                doc->getPdfDocument().save(attached, &error);
                if (error) {
                    g_warning("Could not write the attached PDF: %s", error->message);
                    g_error_free(error);
                }
            }
        }
        t.snapshot = snapshotOf(*doc);
        t.pdfPageCount = doc->getPdfPageCount();
        t.pathWhenTaken = doc->getFilepath();
        t.createBackup = doc->shouldCreateBackupOnSave();
        if (doc->getPageCount() > 0) {
            const PageRef first = doc->getPage(0);
            t.previewPage = t.snapshot->getPage(0);
            if (first->getBackgroundType().isPdfPage()) {
                t.previewPdf = doc->getPdfPage(first->getPdfPageNr());
            }
        }
        const fs::path bg = doc->getPdfFilepath();
        if (t.hybrid && !hybridBase.empty() && (HybridPdf::inCache(bg) || MergedPdf::inCache(bg))) {
            for (size_t i = 0; i < doc->getPageCount(); ++i) {
                const PageRef live = doc->getPage(i);
                auto it = hybridBase.find(live.get());
                if (it != hybridBase.end() && it->second.first.lock() == live) {
                    t.baseOf[t.snapshot->getPage(i).get()] = it->second.second;
                }
            }
        }
    }
    if (!t.hybrid && !exporting && pdfPages->hasStaged()) {
        t.stagedAs = pdfPages->stagedName();
        t.staged = t.snapshot->getPdfFilepath();
    }
    if (!exporting) {
        undoRedo->documentSaved();  // the state copied; it counts as saved once the file is written
        saveUnconfirmed = true;
        t.snapshotTaken = true;
        updateModified();
    }
    onWorker(
            [&t, exporting] {
                t.preview = previewOf(t.previewPage, t.previewPdf);
                if (t.preview) {
                    std::unique_lock lock(*t.snapshot);
                    t.snapshot->setPreview(t.preview);
                }
                if (stopAt(1)) {
                    t.result = {false, "stopped (test)", {}};
                    return;
                }
                if (exporting) {
                    const auto r = HybridPdf::exportXopp(*t.snapshot, t.target, exportPdfFor(t.target), t.pdfPageCount);
                    t.result = r.ok ? SaveResult{true, {}, {}}
                                    : SaveResult{false,
                                                 FS(_F("Could not export \"{1}\": {2}") % t.target.u8string() % r.error),
                                                 {}};
                    return;
                }
                if (t.hybrid) {
                    HybridPdf::BasePageOf baseOf;
                    if (!t.baseOf.empty()) {
                        baseOf = [&t](const XojPage* page) {
                            auto it = t.baseOf.find(page);
                            return it != t.baseOf.end() ? it->second : npos;
                        };
                    }
                    const auto r = HybridPdf::write(*t.snapshot, t.target, baseOf, t.pdfPageCount);
                    if (!r.ok) {
                        t.result = {false,
                                    FS(_F("Could not write the hybrid PDF \"{1}\": {2}") % t.target.u8string() %
                                       r.error),
                                    {}};
                        return;
                    }
                    t.result = {true, {}, {}};
                    if (const fs::path& xopp = t.request.exportXopp; !xopp.empty()) {
                        // The .xopp for Xournal++ (a setting), from the same state
                        const auto e = HybridPdf::exportXopp(*t.snapshot, xopp, exportPdfFor(xopp), t.pdfPageCount);
                        if (!e.ok) {
                            t.result.exportError =
                                    FS(_F("Could not export \"{1}\": {2}") % xopp.u8string() % e.error);
                        }
                    }
                    return;
                }
                // The .xopp. A merged PDF written under another name gets its name after the .xopp refers to that
                // name, then the .xopp is written again (a crash at any point leaves a matching pair).
                t.result = writeXoppFile(*t.snapshot, t.target, t.createBackup, t.handler);
                t.xoppWritten = t.result.ok;
                if (t.result.ok && !t.stagedAs.empty()) {
                    if (stopAt(2)) {
                        t.result = {false, "stopped (test)", {}};
                        return;
                    }
                    std::string error;
                    t.commitTried = true;
                    t.committed = PdfPageKeeper::commitFile(t.staged, t.stagedAs, error);
                    if (!t.committed) {
                        g_warning("Could not give the PDF pages of the document their name: %s", error.c_str());
                    }
                    if (stopAt(3)) {
                        t.result = {false, "stopped (test)", {}};
                        return;
                    }
                    if (t.committed) {
                        {
                            std::unique_lock lock(*t.snapshot);
                            t.snapshot->setPdfAttributes(t.stagedAs, false);
                        }
                        t.result = writeXoppFile(*t.snapshot, t.target, true, t.handler);
                    }
                }
                if (t.result.ok && stopAt(4)) {
                    t.result = {false, "stopped (test)", {}};
                }
            },
            [this] { finishWrite(); });
}

void DocumentSession::beginTextSave() {
    SaveTask& t = *saveTask;
    if (shownReadOnly) {
        return finishSave({false, _("This file is shown read-only."), {}});
    }
    t.textSave = true;
    t.target = t.request.kind == SaveKind::SaveAs ? t.request.target : text->path();
    t.text = currentText();  // (the pages as they are now; editing goes on meanwhile)
    t.textBytes = text->encode(t.text);
    onWorker(
            [&t] {
                std::string error;
                if (!TextFile::writeAtomically(t.target, t.textBytes, error)) {
                    t.result = {false, FS(_F("Could not write \"{1}\": {2}") % t.target.u8string() % error), {}};
                    return;
                }
                t.result = {true, {}, {}};
            },
            [this] {
                SaveTask& t = *saveTask;
                if (t.result.ok) {
                    text->written(t.target, t.text, std::move(t.textBytes));
                    shownPath = text->path();
                    if (lastAutosavedText != t.text) {
                        lastAutosavedText = t.text;
                    }
                    deleteAutosaveFile();  // (older than the file now)
                }
                updateModified();
                finishSave(t.result);
            });
}

void DocumentSession::finishWrite() {
    SaveTask& t = *saveTask;
    if (t.request.kind == SaveKind::ExportXopp) {
        return finishSave(t.result);
    }
    const bool ok = t.result.ok;
    doc->lock();
    if (t.handler) {
        t.handler->updateDocumentInfo(doc.get());
    }
    if (ok && doc->getFilepath() == t.pathWhenTaken) {  // (unless it was moved in the library meanwhile)
        doc->setFilepath(t.target);
    }
    if (ok && t.preview) {
        doc->setPreview(t.preview);
    }
    if (t.xoppWritten && !t.createBackup) {
        doc->setCreateBackupOnSave(true);
    }
    doc->unlock();
    if (!t.hybrid) {
        if (t.commitTried) {
            pdfPages->commitApplied(t.staged, t.committed);
        }
        if (ok) {
            hybridBase.clear();
            pdfPages->finishStaged();  // (the file under the other name: no .xopp refers to it now)
            shownPath.clear();         // (a shown image, Markdown or text file: it is this .xopp now)
            shownReadOnly = false;
        }
    } else if (ok) {
        if (const fs::path bg = t.snapshot->getPdfFilepath(); HybridPdf::inCache(bg)) {
            HybridPdf::touch(bg);  // (still used)
        }
        hybridChanges.clear();  // (written anew from the document)
    }
    finishSave(t.result);
}

void DocumentSession::finishSave(SaveResult result) {
    const std::unique_ptr<SaveTask> task = std::move(saveTask);
    ++saveStage;  // (a late report of its worker is ignored)
    if (task->snapshotTaken) {
        saveUnconfirmed = false;
        saveFailed = !result.ok;  // (the saved point is the copy's state, which is not in the file then)
    }
    lastSaveResult = result;
    if (result.ok && task->request.kind != SaveKind::ExportXopp) {
        Q_EMIT filePathChanged();
    }
    undoRedoChanged();  // (the undo actions and the modified state)
    if (task->request.done && !destroying) {
        task->request.done(result);
    }
    startNextSave();
}

// --- running steps -------------------------------------------------------------------------------------------------

void DocumentSession::onWorker(std::function<void()> work, std::function<void()> then) {
    const quint64 stage = ++saveStage;
    auto finished = std::make_shared<std::promise<void>>();
    saveTask->work = finished->get_future().share();
    saveTask->then = std::move(then);
    SaveTask* task = saveTask.get();
    savePool().start([this, stage, task, work = std::move(work), finished] {
        try {
            work();
        } catch (const std::exception& e) {
            task->result = {false, e.what(), {}};
        } catch (...) {
            task->result = {false, "unknown error", {}};
        }
        // First the report, then `finished`: the session waits for that before it goes, so it is still there
        QMetaObject::invokeMethod(this, [this, stage] { resumeSave(stage); }, Qt::QueuedConnection);
        finished->set_value();
    });
}

void DocumentSession::postStep(std::function<void()> then) {
    const quint64 stage = ++saveStage;
    saveTask->work = {};
    saveTask->then = std::move(then);
    QMetaObject::invokeMethod(this, [this, stage] { resumeSave(stage); }, Qt::QueuedConnection);
}

void DocumentSession::resumeSave(quint64 stage) {
    if (!saveTask || stage != saveStage || !saveTask->then) {
        return;  // (done already: waitForSaves ran it)
    }
    if (saveTask->work.valid()) {
        saveTask->work.wait();
    }
    saveTask->work = {};
    auto then = std::move(saveTask->then);
    saveTask->then = nullptr;
    then();
}

}  // namespace xqt
