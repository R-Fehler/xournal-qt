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

#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QRectF>
#include <QTimer>

#include "control/Control.h"
#include "undo/UndoRedoHandler.h"  // for UndoRedoListener

#include "HeadlessViews.h"
#include "SessionActions.h"
#include "filesystem.h"

class LayerController;

namespace xqt {

class DocumentSearch;

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
    };
    /// Load a .xopp, .xoj or .pdf file (a PDF gets one page per PDF page). Does not touch any session, so it may
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
    };
    /// Save to the document's path (as .xopp). Requires hasFilePath().
    SaveResult save();
    /// Save to a new path; the document takes this path ("Save as").
    SaveResult saveAs(fs::path target);
    /// Write the autosave file if there are unsaved changes since the last autosave.
    SaveResult autosave();

    /// Suggested target for "Save as" (port of upstream Control::saveImpl): the document's own path; for an
    /// annotated PDF the .xopp next to the PDF ("lecture.pdf" -> "lecture.xopp"); else the default name
    /// (Settings::getDefaultSaveName) in the last save folder.
    fs::path suggestSavePath() const;

    bool hasFilePath() const;
    fs::path getFilePath() const;
    /// Title for the tab: file name, or "Untitled" / the PDF name for unsaved documents.
    std::string getDisplayName() const;
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

    // --- view side --------------------------------------------------------------------------------------------
    /// The view showing this session (nullptr: headless). Not owned.
    void setXournalView(XournalView* view);
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
    /// Undo stack of the page structure (insert, delete, move, paste); the annotations have getUndoRedoHandler().
    UndoRedoHandler* getPageUndoRedoHandler() const { return pageUndo.get(); }
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

Q_SIGNALS:
    void modifiedChanged(bool modified);
    void undoRedoStateChanged();
    void filePathChanged();
    void currentPageChanged(qulonglong page);
    /// Reused upstream code wants a page to be shown (e.g. after undoing a page deletion).
    void scrollToPageRequested(qulonglong page);
    /// The view should end text editing and clear its selection (before document modifications).
    void clearSelectionRequested();
    /// The content of a page changed through an undoable action (thumbnails should be updated).
    void pageContentChanged(qulonglong page);
    /// Show this rectangle of a page (page points), e.g. a search hit.
    void scrollToRectRequested(qulonglong page, QRectF rect);

private:
    void init();
    void enableAutosave(bool enable);
    void updatePageActions();
    void setLastAutosaveFile(fs::path file);
    void updatePreview();
    SaveResult saveImpl(fs::path target);

    // UndoRedoListener
    void undoRedoChanged() override;
    void undoRedoPageChanged(PageRef page) override;

    AppContext& app;
    std::unique_ptr<Document> doc;
    std::unique_ptr<UndoRedoHandler> undoRedo;
    std::unique_ptr<UndoRedoHandler> pageUndo;
    std::unique_ptr<LayerController> layerController;
    SessionActions actions;
    SessionWindow window;
    HeadlessXournalView headlessView;
    HeadlessCursor headlessCursor;
    XournalppCursor* cursor = &headlessCursor;
    SessionScrollHandler scrollHandler;
    size_t currentPage = 0;
    bool lastModified = false;

    QTimer autosaveTimer;
    fs::path lastAutosaveFile;
    quint64 serialNo = 0;
    std::unique_ptr<DocumentSearch> searcher;  // last: it listens to this session
};

}  // namespace xqt
