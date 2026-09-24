/*
 * xournal-qt: one open document (one tab).
 *
 * Implements the shadow `Control` interface (qt/compat/include/control/Control.h), i.e. it is what reused upstream
 * code (undo actions, layer controller, tools, input handlers) sees as "the control". Unlike upstream's GTK Control,
 * which exists once per window and swaps documents in and out, a session owns exactly one Document for its whole
 * lifetime. Shared state (settings, tools, page templates, render workers) lives in AppContext.
 *
 * File handling is ported from upstream (Control::openXoppFile/openPdfFile/createNewDocument, SaveJob::save,
 * AutosaveJob::run, Control::insertPage, PageBackgroundChangeController::insertNewPage).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QObject>
#include <QRectF>
#include <QTimer>

#include "control/Control.h"
#include "control/zoom/ZoomControl.h"
#include "pdf/base/XojPdfPage.h"
#include "undo/UndoRedoHandler.h"  // for UndoRedoListener

#include "HeadlessViews.h"
#include "SessionActions.h"
#include "filesystem.h"

class LayerController;

namespace xqt {

class DocumentSearch;
class PdfPageKeeper;
class TextFile;
namespace HybridPdf {
struct Revision;
}
struct PdfMerge;

class AppContext;

class DocumentSession final: public QObject, public Control, private UndoRedoListener {
    Q_OBJECT
public:
    struct LoadResult {
        std::unique_ptr<Document> document;  ///< nullptr if loading failed
        std::string error;                   ///< why loading failed
        std::vector<std::string> warnings;   ///< non-fatal problems: some content may be lost
        fs::path missingPdf;                 ///< background PDF that could not be found
        bool attachedPdfMissing = false;
        int fileVersion = 0;
        bool isNewerFileVersion() const;
        /// A hybrid PDF (HybridPdf.h): the document is its embedded document, the file path is the PDF.
        bool hybrid = false;
        /// A hybrid PDF whose annotations of ours another app changed, moved or deleted (their names)
        std::vector<std::string> hybridChanged;
    };
    /// Load a .xopp, .xoj or .pdf file (a PDF gets one page per PDF page; a hybrid PDF is its embedded document). Does not touch any session, so it may
    /// run on a worker thread before the tab is created.
    static LoadResult loadFile(const fs::path& path, bool attachPdf = false);

    /// A new document with one page from the page template settings.
    explicit DocumentSession(AppContext& app, QObject* parent = nullptr);
    /// A loaded document (see loadFile). The session takes ownership.
    DocumentSession(AppContext& app, std::unique_ptr<Document> document, QObject* parent = nullptr);
    ~DocumentSession() override;

    AppContext& getApp() const { return app; }

    // --- file handling ----------------------------------------------------------------------------------------
    struct SaveResult {
        bool ok = false;
        std::string error;
        std::string exportError;  ///< SaveRequest::exportXopp failed (the save itself may have succeeded)
        // --- ExportArchive (HybridPdf::writeArchive)
        bool pdfa = false;                  ///< the archive PDF carries the PDF/A-3b identification
        std::vector<std::string> notPdfA;   ///< why not
        std::vector<std::string> adjusted;  ///< what was changed in the source PDF to conform
        // --- a hybrid PDF saved again (qt/docs/hybrid-pdf.md, "Saving: incremental updates")
        bool incremental = false;  ///< only what changed was appended
        uint64_t appended = 0;     ///< bytes appended
    };
    enum class SaveKind {
        Save,        ///< to the document's file: its .xopp, or its hybrid PDF. Requires hasFilePath().
        SaveAs,      ///< as .xopp to `target`; the document takes this path ("Save as")
        Hybrid,      ///< as a hybrid PDF to `target` (see saveAsHybrid)
        ExportXopp,  ///< only exportXopp to `target` (the document's state and saved point stay)
        /// A copy as a hybrid PDF at `target` (to share): the document keeps its file, state and saved point.
        ExportHybrid,
        /// A copy as an archive PDF (PDF/A-3b, HybridPdf::writeArchive) at `target`: the document keeps its file, state
        /// and saved point. The result has the PDF/A report.
        ExportArchive,
    };
    static bool isExport(SaveKind kind) {
        return kind == SaveKind::ExportXopp || kind == SaveKind::ExportHybrid || kind == SaveKind::ExportArchive;
    }
    struct SaveRequest {
        SaveKind kind = SaveKind::Save;
        fs::path target;
        /// A hybrid PDF: also export the .xopp for Xournal++ here (exportXopp), from the same state.
        fs::path exportXopp;
        /// Called on this thread when the file is written, or when that failed.
        std::function<void(const SaveResult&)> done;
        /// A hybrid PDF: the .xopp for Xournal++ this document keeps up to date on every save (recorded in the file,
        /// see xoppExport()); empty: none. Usually the same as exportXopp.
        fs::path recordExport;
        /// ExportXopp: the PDF is upstream's attached PDF of the .xopp, "name.xopp.bg.pdf" (a copy for Xournal++ that
        /// travels as a pair), not the export's name.pdf / .name.pages.pdf.
        bool attachedPdf = false;
        /// Save of a hybrid PDF: written anew in full, never as an incremental update (before it is shared: older
        /// revisions in the file may still hold deleted ink).
        bool compact = false;
    };
    /// Save without blocking the window. What the writers need is taken from the document at once on this thread (a
    /// copy of its pages, under its read lock); the heavy file work (the gzip XML, qpdf) runs on a worker, and the
    /// document can be edited meanwhile. It counts as saved only if nothing changed since that copy, and it stays
    /// modified until the file is written (isSaving()). One save at a time: a save asked for while one runs follows
    /// it (several plain saves in a row: one). The merged PDF of pasted pages is written first (on the worker, its
    /// crash-safe steps as before), then the copy is taken.
    void saveInBackground(SaveRequest request);
    /// A save runs or waits.
    bool isSaving() const;
    /// Wait (blocking this thread) until the running and waiting saves are done; false if the last one failed.
    bool waitForSaves();
    /// The merged PDF must not change while a save writes it: pasting waits for that part (usually well under a
    /// second).
    void waitForPdfWork();
    bool pdfWorkRunning() const;
    /// save(), saveAs(), saveAsHybrid(): the same, waiting for the result (tests, library moves).
    /// Save to the document's path (as .xopp). Requires hasFilePath().
    SaveResult save();
    /// Save to a new path; the document takes this path ("Save as").
    SaveResult saveAs(fs::path target);
    /// Save as a hybrid PDF (HybridPdf.h); the document takes this path, and save() writes it again. Writing into a
    /// PDF that is not a hybrid PDF yet (the PDF the document annotates) keeps a copy "name.original.pdf" once.
    SaveResult saveAsHybrid(fs::path target);
    /// The document is saved as a hybrid PDF (its file is a .pdf).
    bool isHybrid() const;
    /// A hybrid PDF whose file holds earlier revisions (incremental updates): written anew (SaveRequest::compact)
    /// before it is shared.
    bool hasEarlierRevisions() const;
    /// A hybrid PDF: the .xopp for Xournal++ it keeps up to date on every save ("Keep it updated for Xournal++",
    /// SaveRequest::recordExport; read from the file the first time). Empty: none.
    fs::path xoppExport() const;
    /// The document's background PDF is one of `files` (they are about to be removed or written over, e.g. the
    /// sidecars of the .xopp it was): it takes its pages from a copy in the app cache from now on (a hard link where
    /// possible), the same pages under the same numbers. False if that failed (`error`).
    bool detachBackground(const std::vector<fs::path>& files, std::string& error);
    /// A hybrid PDF with annotations of ours changed in another app (see LoadResult::hybridChanged).
    void setHybridChanges(std::vector<std::string> names) { hybridChanges = std::move(names); }
    const std::vector<std::string>& getHybridChanges() const { return hybridChanges; }
    /// Take the other app's version of those annotations: they stay in the PDF as plain annotations (shown by the
    /// background), and the layers they stood for are emptied (undoable). False if that failed (`error`).
    bool importHybridChanges(std::string& error);
    /// Export for Xournal++: a plain `xopp` next to the hybrid PDF with the base pages as its PDF (the merged-PDF
    /// rules of qt/pdf-pages: "name.pdf" if free, else ".name.pages.pdf"). The document keeps its file.
    SaveResult exportXopp(const fs::path& xopp);
    /// A copy as an archive PDF (SaveKind::ExportArchive), waiting for it.
    SaveResult exportArchive(const fs::path& pdf);
    /// saveInBackground, waiting for its result.
    SaveResult saveNow(SaveRequest request);
    /// Where exportXopp puts the PDF for this .xopp.
    static fs::path exportPdfFor(const fs::path& xopp);
    /// Write a document that is not open in a session (e.g. a library document being moved) to `target` (.xopp),
    /// with a new preview; the document takes this path.
    static SaveResult writeDocument(Document& doc, const fs::path& target);
    /// The document's files were renamed or moved (library): its .xopp is now `xopp` / its background PDF `pdf`
    /// (empty: unchanged).
    void relocate(const fs::path& xopp, const fs::path& pdf);
    /// Write the autosave file if there are unsaved changes since the last autosave.
    SaveResult autosave();

    /// Suggested target for "Save as" (port of upstream Control::saveImpl): the document's own path; for an
    /// annotated PDF the .xopp next to the PDF ("lecture.pdf" -> "lecture.xopp"), the same for a shown image
    /// ("photo.jpg" -> "photo.xopp": the library pairs them); else the default name (Settings::getDefaultSaveName) in
    /// the last save folder.
    fs::path suggestSavePath() const;

    bool hasFilePath() const;
    fs::path getFilePath() const;
    /// The file the document is known by: its .xopp, or the PDF it annotates while it has no .xopp yet, or the file it
    /// shows (empty: a new document).
    fs::path documentFile() const;
    /// Title for the tab: file name, or "Untitled" / the PDF name / the shown file's name for unsaved documents.
    std::string getDisplayName() const;
    /// The file this new document shows without being that file: a Markdown file shown read-only (MarkdownFile.h), a
    /// text or code file shown read-only as plain text (`readOnly`), an image to write on (ImageFile.h). It is never
    /// written: saving asks for a .xopp (see suggestSavePath), and once saved the document is that .xopp. Empty: none.
    void setShownFile(const fs::path& file, bool readOnly = false);
    const fs::path& shownFile() const { return shownPath; }
    /// It shows a Markdown or text file read-only (not saved as a .xopp): the canvas does not write on it.
    bool isReadOnly() const;

    // --- a text file edited (TextFile.h, qt/docs/md-editor.md) -------------------------------------------------
    /// This document is the text of a file (a .md, a .txt, another text file): its pages hold the text as the page's
    /// Markdown text (MarkdownFile.h), and saving writes the text back to the file (never a .xopp). A text file that
    /// cannot be edited (not UTF-8, too big, not writable, or another text file not accepted for editing) is shown
    /// read-only (isReadOnly). The session takes ownership; the file becomes the shown file.
    void setTextFile(std::unique_ptr<TextFile> file, bool readOnly);
    TextFile* textFile() const { return text.get(); }
    /// It is a text file that is edited (not read-only).
    bool isEditableText() const;
    /// The text the pages hold now (the parts of the page's Markdown text joined). `lock`: under the document's read
    /// lock (not in a crash handler).
    std::string currentText(bool lock = true) const;
    /// The text changed (the pages' boxes): the modified state follows.
    void textEdited();
    /// The text is on one continuous page that grows with it (else on pages; MarkdownFile::relayout switches).
    bool isTextContinuous() const { return textContinuous; }
    void setTextContinuous(bool on) { textContinuous = on; }
    /// The text file changed on disk since it was read or written (by another program). `bytes`: what it holds now.
    /// Not while a save runs (asked again after it).
    bool textChangedOnDisk(std::string& bytes);
    /// Keep the text as it is although the file changed on disk (the next save writes over it): no more questions
    /// about that version.
    void keepTextOverDisk();
    /// The file's new bytes are the text now (after the pages were given its text): not modified.
    void textReloaded(std::string bytes);
    /// A new document made from another file ("Edit as notes" of a .md): saving suggests `file` (e.g. "name.xopp" next
    /// to the .md), the tab is titled so, and it counts as modified until it is saved (its content is nowhere else).
    void setMadeFrom(const fs::path& suggestion);
    /// Where the text of a tab is written for crash recovery (autosave, crash): plain text files in the cache.
    static fs::path textAutosavePath(qint64 pid, quint64 serial);
    static fs::path textEmergencyPath(qint64 pid, quint64 serial);
    /// Write the text to its autosave file if it changed since the last one.
    SaveResult autosaveText();
    bool isModified() const;
    const fs::path& getLastAutosaveFile() const { return lastAutosaveFile; }
    /// Unique number of this session in this process (names its autosave and emergency files).
    quint64 serial() const { return serialNo; }
    /// Where autosave() writes: ".name.autosave.xopp" next to the document (upstream), or for unsaved
    /// documents "<cache>/autosaves/<pid>-<serial>.autosave.xopp" (one file per tab).
    fs::path autosavePath() const;
    /// Where a crash (emergency) save of this session goes: "<cache>/autosaves/<pid>-<serial>.emergency.xopp".
    static fs::path emergencyPath(qint64 pid, quint64 serial);
    static fs::path unnamedAutosavePath(qint64 pid, quint64 serial);
    static fs::path namedAutosavePath(fs::path document);
    /// This document was restored from an autosave/emergency file: it belongs to `original` (empty: unsaved) and
    /// has unsaved changes (upstream's EmergencySaveRestore undo action).
    void markRecovered(const fs::path& original);
    /// Remove the last autosave file (after closing the document without losing data).
    void deleteAutosaveFile();

    // --- PDF pages from other PDFs (MergedPdf.h) ----------------------------------------------------------------
    /// Add PDF pages from another PDF (a PDF in memory) to the document's merged background PDF, which is made from
    /// its own PDF the first time and becomes its background. Returns the number of the first of them in it, or npos
    /// if that failed (`error`). The numbers of the other pages stay. The merged PDF is written in the background
    /// (seconds for a long PDF): pages with these numbers are drawn from `pdf` until then (pendingPdfPage); if it
    /// fails, they get their PDF page as an image and pdfPagesFailed() says why.
    size_t addPdfPages(const std::string& pdf, std::string& error);
    /// A PDF page that is still being added to the merged PDF (any thread; nullptr: none).
    XojPdfPageSPtr pendingPdfPage(size_t number) const;
    /// PDF pages are being added to the merged PDF.
    bool mergingPdfPages() const;
    /// Wait (blocking) until they are (and a save before them is done): e.g. before pages are copied.
    void waitForMerges();
    /// (PdfPageKeeper) Write this merge after the ones before it, before the saves that wait.
    void queueMerge(std::shared_ptr<PdfMerge> merge);
    /// The page numbers in the background PDF stay valid while this does not change (a save dropped unused pages
    /// of the merged PDF and renumbered the pages).
    quint64 pdfNumbering() const;
    /// Where the merged PDF (in the cache until then) goes when the document is saved (empty: not saved yet, or none).
    fs::path mergedPdfPlace() const;
    /// The PDF the document annotates for the user: its background PDF, or while the merged PDF of a document that
    /// was never saved is in the cache, the PDF it was made from (empty if none).
    fs::path annotatedPdf() const;
    /// Load this PDF as the background, whose pages that the document shows look the same as in the one it has now
    /// (pages added to it, or a copy of it): the views swap their PDF without drawing those pages again. False if it
    /// did not load (Document::getLastErrorMsg).
    bool loadPdfKeepingPictures(const fs::path& pdf);
    /// Within loadPdfKeepingPictures (for the views).
    bool pdfKeepsPictures() const { return keepingPictures; }

    // --- view side --------------------------------------------------------------------------------------------
    /// The view showing this session (nullptr: headless). Not owned.
    void setXournalView(XournalView* view);
    /// Zoom values for reused upstream code, kept up to date by the view (nullptr: headless, zoom 1).
    void setZoomControl(ZoomControl* zoom);
    /// Cursor implementation of the view (nullptr: headless). Not owned.
    void setCursor(XournalppCursor* cursor);
    void setCurrentPageNo(size_t page);
    SessionActions& getActions() { return actions; }
    /// Text search in this document.
    DocumentSearch& search() const { return *searcher; }

    // --- Control (shadow interface for reused upstream code) ----------------------------------------------------
    Settings* getSettings() const override;
    ToolHandler* getToolHandler() const override;
    ZoomControl* getZoomControl() const override;
    Document* getDocument() const override;
    UndoRedoHandler* getUndoRedoHandler() const override;
    MainWindow* getWindow() const override;
    ScrollHandler* getScrollHandler() const override;
    PageRef getCurrentPage() override;
    size_t getCurrentPageNo() const override;
    /// Changes whenever the page's picture does (its content, its size): thumbnails are named and kept by it. It stays
    /// with the page when pages before it come or go. Unique in the process, like the page's id (which stays when its
    /// content changes). Any thread for pageStamps() and pageOfRevision().
    struct PageStamp {
        quint64 id = 0;
        quint64 revision = 0;
        PageRef page;
    };
    quint64 pageRevision(size_t page) const;
    quint64 pageId(size_t page) const;
    std::vector<PageStamp> pageStamps() const;
    std::optional<PageStamp> pageOfRevision(quint64 revision) const;
    XournalppCursor* getCursor() const override;
    PageTypeHandler* getPageTypes() const override;
    LayerController* getLayerController() const override;
    ActionDatabase* getActionDatabase() const override;
    void clearSelectionEndText() override;
    void setCopyCutEnabled(bool enabled) override;
    void insertNewPage(size_t position, bool automatedInsertion = false) override;
    void insertPage(const PageRef& page, size_t position, bool shouldScrollToPage = true) override;

    // --- page operations on the current page (ports of upstream Control, undoable) ------------------------------
    void deletePage();
    void duplicatePage();
    void movePageTowardsBeginning();
    void movePageTowardsEnd();

    // --- several pages at once (sidebar / page grid selection) --------------------------------------------------
    /// The undo stack that page changes go onto: the one of everything (as in upstream), see addPageUndoAction().
    UndoRedoHandler* getPageUndoRedoHandler() const { return undoRedo.get(); }
    /// A change of the page structure onto the undo stack; undoing or redoing it emits pageActionUndone.
    void addPageUndoAction(UndoActionPtr action);
    /// Delete pages (indices). Not all of them: a document keeps at least one page. False if nothing was deleted.
    bool deletePages(std::vector<size_t> pages);
    /// Insert pages (not yet in the document) before `position`.
    void insertPages(const std::vector<PageRef>& pages, size_t position);
    /// Move pages (indices) so that they come, in their order, before the page that is at index `target` now
    /// (target = page count: to the end). False if the order does not change.
    bool movePages(std::vector<size_t> pages, size_t target);
    /// The pages in document order.
    std::vector<PageRef> pageOrder() const;
    /// Make the document's pages `target` (used by undo/redo of the above); `moved`: pages that change place.
    void applyPageOrder(const std::vector<PageRef>& target, const std::vector<PageRef>& moved);
    /// xournal-qt: "#Page:12" links in the texts follow the pages when those are inserted, moved or deleted.
    void updatePageLinks(const std::vector<PageRef>& before, const std::vector<PageRef>& after);
    /// One page came (delta 1) or went (delta -1) at this place: the links behind it count on or back.
    void shiftPageLinks(size_t position, int delta);
    /// Hears every page that comes or goes (also through undo) and keeps the page links right.
    class PageLinkKeeper;
    std::unique_ptr<PageLinkKeeper> pageLinkKeeper;
    class PageRevisionKeeper;
    std::unique_ptr<PageRevisionKeeper> pageRevisionKeeper;
    void revisePage(size_t page);
    bool pageLinksPaused = false;
    /// newPage[old page - 1] is the new number (0: leave those links alone).
    void rewritePageLinks(const std::vector<int>& newPage);

Q_SIGNALS:
    void modifiedChanged(bool modified);
    /// isSaving() changed.
    void savingChanged(bool saving);
    /// Pasted PDF pages could not be added to the merged PDF (they show their PDF page as an image instead).
    void pdfPagesFailed(const QString& error);
    void undoRedoStateChanged();
    /// A page change was undone (or redone): its text ("Insert page", ...).
    void pageActionUndone(const QString& text, bool undone);
    void filePathChanged();
    void currentPageChanged(qulonglong page);
    /// Reused upstream code wants a page to be shown (e.g. after undoing a page deletion).
    void scrollToPageRequested(qulonglong page);
    /// The view should end text editing and clear its selection (before document modifications).
    void clearSelectionRequested();
    /// The content of a page changed through an undoable action (thumbnails should be updated).
    void pageContentChanged(qulonglong page);
    /// A pageRevision() changed (or pages came or went).
    void pageRevisionsChanged();
    /// Show this rectangle of a page (page points), e.g. a search hit.
    void scrollToRectRequested(qulonglong page, QRectF rect);

private:
    void init();
    void enableAutosave(bool enable);
    void updatePageActions();
    void setLastAutosaveFile(fs::path file);
    static void updatePreview(Document& doc);

    // Saving in the background (DocumentSave.cpp): a save goes through steps, on this thread or on a worker.
    struct SaveTask;
    void startNextSave();
    void beginSave();
    void beginMerge();
    /// Pages were pasted while this save had not copied the document yet: the merges first, then it starts again.
    bool yieldToMerges();
    void planFiles();
    void takeSnapshot();
    void finishWrite();
    void finishSave(SaveResult result);
    /// Run `work` on a worker, then `then` on this thread.
    void onWorker(std::function<void()> work, std::function<void()> then);
    /// Run `then` on this thread from the event loop.
    void postStep(std::function<void()> then);
    void resumeSave(quint64 stage);
    void updateModified();
    void updateSaving();
    void beginTextSave();

    // UndoRedoListener
    void undoRedoChanged() override;
    void undoRedoPageChanged(PageRef page) override;

    AppContext& app;
    std::unique_ptr<Document> doc;
    std::unique_ptr<UndoRedoHandler> undoRedo;
    std::unique_ptr<LayerController> layerController;
    SessionActions actions;
    SessionWindow window;
    HeadlessXournalView headlessView;
    HeadlessCursor headlessCursor;
    ZoomControl headlessZoom;
    ZoomControl* zoomControl = &headlessZoom;
    XournalppCursor* cursor = &headlessCursor;
    SessionScrollHandler scrollHandler;
    size_t currentPage = 0;
    bool lastModified = false;

    std::unique_ptr<SaveTask> saveTask;  ///< the save that runs
    std::deque<SaveRequest> saveQueue;   ///< the saves after it
    std::deque<std::shared_ptr<PdfMerge>> mergeQueue;  ///< pasted PDF pages to merge (before the saves)
    quint64 saveStage = 0;               ///< the step of the running save (a stale resume is ignored)
    bool lastSaving = false;
    /// The saved point of the undo stack is the state a running save copied: modified until it is written.
    bool saveUnconfirmed = false;
    /// The last save failed after the saved point was moved: modified until a save succeeds.
    bool saveFailed = false;
    bool destroying = false;
    bool keepingPictures = false;
    SaveResult lastSaveResult;

    QTimer autosaveTimer;
    fs::path lastAutosaveFile;
    fs::path shownPath;
    bool shownReadOnly = false;  ///< (a Markdown or text file)
    quint64 serialNo = 0;
    std::unique_ptr<PdfPageKeeper> pdfPages;
    std::vector<fs::path> retainedBases;  ///< clean copies of hybrid PDFs this document uses (HybridPdf::retain)
    std::vector<std::string> hybridChanges;
    /// A hybrid PDF as last written or opened: what the next Ctrl+S appends to (valid while the page numbers of the
    /// background PDF stay, `hybridNumbering`, and for `hybridRevisionFile` only).
    std::shared_ptr<HybridPdf::Revision> hybridRevision;
    quint64 hybridNumbering = 0;
    fs::path hybridRevisionFile;
    /// xoppExport(), known for this file
    mutable fs::path xoppExportFor, xoppExportPath;
    /// Opened from a hybrid PDF: the page of its clean copy each page was (annotations of other apps on pages with a
    /// generated background are kept from there). Forgotten when the pages of the background PDF may be renumbered.
    std::unordered_map<const XojPage*, std::pair<std::weak_ptr<XojPage>, size_t>> hybridBase;
    std::unique_ptr<TextFile> text;  ///< a text file edited (or shown read-only)
    bool textModified = false;
    bool textContinuous = false;
    fs::path madeSuggestion;  ///< setMadeFrom
    bool madeUnsaved = false;
    std::string lastAutosavedText;
    std::unique_ptr<DocumentSearch> searcher;  // last: it listens to this session
};

}  // namespace xqt
