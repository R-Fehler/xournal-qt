/*
 * xournal-qt: application controller exposed to QML.
 *
 * Owns the shared AppContext and the tabs (TabManager). The document properties (title, undo state, zoom, pages,
 * view, ...) always refer to the current tab. The tool state is upstream's ToolHandler, shared by all tabs.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QColor>
#include <QMetaObject>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "filesystem.h"

namespace xqt {
class AppContext;
class CanvasView;
class DocumentSession;
class TabManager;
class PagesModel;
class PageFilterModel;
class PageClipboard;
class SettingsModel;
class SessionRecovery;
class Library;
class LibraryModel;
class RecentFiles;
namespace DocumentFiles {
struct Result;
}
}  // namespace xqt
class Palette;

class AppController: public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* tabs READ tabsModel CONSTANT)
    /// Pages of the current tab (for the page sidebar).
    Q_PROPERTY(QObject* pages READ pagesModel CONSTANT)
    /// The pages for the sidebar and the page grid, optionally only those with search hits.
    Q_PROPERTY(QObject* filteredPages READ filteredPagesModel CONSTANT)
    Q_PROPERTY(QObject* settings READ settingsModel CONSTANT)
    /// The library of this window (a folder of documents) and the recently opened documents (home screen)
    Q_PROPERTY(QObject* library READ libraryModel CONSTANT)
    Q_PROPERTY(QObject* recent READ recentModel CONSTANT)
    /// The home screen (library, recent documents) is shown instead of the current document; always when no
    /// document is open.
    Q_PROPERTY(bool homeVisible READ homeVisible WRITE setHomeVisible NOTIFY homeVisibleChanged)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY documentChanged)
    Q_PROPERTY(QObject* view READ view NOTIFY documentChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool hasFilePath READ hasFilePath NOTIFY titleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color NOTIFY toolChanged)
    /// Font of the text tool (upstream's settings font)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    /// Upstream's drawing type of the tool: default (freehand), strokeRecognizer, line, rectangle, ellipse, arrow,
    /// doubleArrow, drawCoordinateSystem
    Q_PROPERTY(QString drawingType READ drawingType WRITE setDrawingType NOTIFY toolChanged)
    Q_PROPERTY(int size READ size NOTIFY toolChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    /// The colors in the tool bar (user's choice; default: the first colors of the palette, with orange)
    Q_PROPERTY(QVariantList toolbarColors READ toolbarColors NOTIFY toolbarColorsChanged)
    /// Color of PDF text highlights, one of three presets
    Q_PROPERTY(QColor pdfHighlightColor READ pdfHighlightColor WRITE setPdfHighlightColor NOTIFY pdfTextModeChanged)
    Q_PROPERTY(QVariantList pdfHighlightColors READ pdfHighlightColors CONSTANT)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
    Q_PROPERTY(int pageNumber READ pageNumber NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    // Search in the current document
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)
    Q_PROPERTY(int searchHitCount READ searchHitCount NOTIFY searchChanged)
    /// 1-based number of the current hit (0: none)
    Q_PROPERTY(int searchCurrent READ searchCurrent NOTIFY searchChanged)
    Q_PROPERTY(bool searchRunning READ searchRunning NOTIFY searchChanged)
    /// Number of pages with search hits
    Q_PROPERTY(int searchHitPageCount READ searchHitPageCount NOTIFY searchChanged)
    // Page layout of the canvas (upstream settings viewColumns, showPairedPages, numPairsOffset), for all tabs
    Q_PROPERTY(int viewColumns READ viewColumns WRITE setViewColumns NOTIFY viewLayoutChanged)
    Q_PROPERTY(bool pairedPages READ pairedPages WRITE setPairedPages NOTIFY viewLayoutChanged)
    Q_PROPERTY(int pairsOffset READ pairsOffset WRITE setPairsOffset NOTIFY viewLayoutChanged)
    /// Elements are selected on the canvas (select tools).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // Page operations (sidebar, page grid) have their own undo stack
    Q_PROPERTY(bool canUndoPages READ canUndoPages NOTIFY pageUndoChanged)
    Q_PROPERTY(bool canRedoPages READ canRedoPages NOTIFY pageUndoChanged)
    Q_PROPERTY(int copiedPages READ copiedPages NOTIFY copiedPagesChanged)
    /// Back / forward after jumps (links, page grid, sidebar), per tab
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationChanged)
    /// What the PDF text tools do with the selected text: highlight, underline, strikethrough, select
    Q_PROPERTY(QString pdfTextMode READ pdfTextMode WRITE setPdfTextMode NOTIFY pdfTextModeChanged)
    /// Documents of a crashed previous run that can be recovered: [{ title, time }]. Empty when there are none.
    Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY recoveryChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QObject* tabsModel() const;
    QObject* pagesModel() const;
    QObject* filteredPagesModel() const;
    QObject* settingsModel() const;
    QObject* libraryModel() const;
    QObject* recentModel() const;
    bool homeVisible() const;
    void setHomeVisible(bool visible);
    int currentTab() const;
    void setCurrentTab(int index);
    QObject* view() const;
    QString title() const;
    bool modified() const;
    bool hasFilePath() const;
    bool canUndo() const;
    bool canRedo() const;
    QString tool() const;
    QColor color() const;
    QString fontFamily() const;
    double fontSize() const;
    void setFontFamily(const QString& family);
    void setFontSize(double size);
    void setFont(const QString& family, double size);
    Q_INVOKABLE QStringList fontFamilies() const;
    QString drawingType() const;
    void setDrawingType(const QString& type);
    int size() const;
    QVariantList palette() const;
    QVariantList toolbarColors() const;
    /// Add a color to the tool bar (not twice) / remove the color at `index` / back to the default colors.
    Q_INVOKABLE void addToolbarColor(const QColor& color);
    Q_INVOKABLE void removeToolbarColor(int index);
    Q_INVOKABLE void resetToolbarColors();
    QColor pdfHighlightColor() const;
    void setPdfHighlightColor(const QColor& color);
    QVariantList pdfHighlightColors() const;
    int zoomPercent() const;
    int pageNumber() const;
    int pageCount() const;
    QVariantList recoveryItems() const;
    QString searchQuery() const;
    void setSearchQuery(const QString& query);
    int searchHitCount() const;
    int searchCurrent() const;
    bool searchRunning() const;
    int searchHitPageCount() const;
    bool hasSelection() const;
    bool canGoBack() const;
    QString pdfTextMode() const { return pdfMode; }
    void setPdfTextMode(const QString& mode);
    bool canGoForward() const;
    bool canUndoPages() const;
    bool canRedoPages() const;
    int copiedPages() const;
    int viewColumns() const;
    void setViewColumns(int columns);
    bool pairedPages() const;
    void setPairedPages(bool paired);
    int pairsOffset() const;
    void setPairsOffset(int offset);

    // --- home: library and recent documents ---
    /// The folder this window works in (main.cpp: the command line, else the default library). Tabs of this
    /// library are restored at start; another library has its own window.
    void setLibraryRoot(const fs::path& root);
    /// The session journal of a library (so that the windows of two libraries do not share one).
    static fs::path journalFileFor(const xqt::Library& library);
    /// A new document with the page settings (background, paper size, orientation: settings "pageBackground",
    /// "paperFormat", "landscape"). With a name and a library, it is saved at once in the library's current folder
    /// as "<name>.xopp"; else it is a new unsaved document.
    Q_INVOKABLE bool createDocument(const QString& name, bool inLibrary);
    /// Open a document found by the library search, with the search active on its first hit.
    Q_INVOKABLE bool openSearchHit(const QString& path, const QString& query);
    /// The same, at a page (0-based) with hits: its first hit is the current one.
    Q_INVOKABLE bool openSearchHitAt(const QString& path, const QString& query, int page);
    /// The libraries in the standard folder: [{ name, path, current }]
    Q_INVOKABLE QVariantList libraries() const;
    /// Open a folder as library in a new window (another process: one library per window).
    Q_INVOKABLE void openLibrary(const QUrl& folder);
    /// New library in the standard folder, opened in a new window. False if the name is taken or invalid.
    Q_INVOKABLE bool createLibrary(const QString& name);
    Q_INVOKABLE void showInFileManager(const QString& path);

    // --- start and recovery ---
    /// Start of the app: offers recovery after a crash (recoveryItems), else reopens the last tabs (setting), then
    /// opens `files`. Starts protecting the open documents (journal, emergency saves).
    void startSession(const QStringList& files);
    /// Answer to the recovery offer: reopen the tabs of the crashed run, with the recovered changes (accept) or as
    /// last saved (discard; the recovery files are deleted).
    Q_INVOKABLE void recover(bool accept);

    // --- several pages (sidebar / page grid selection; page indices). Empty list: the current page ---
    Q_INVOKABLE void copyPages(const QList<int>& pages);
    Q_INVOKABLE void cutPages(const QList<int>& pages);
    /// Paste the copied pages before `position` (-1: after the selection, else after the current page). The pasted
    /// pages are selected. Returns their number.
    Q_INVOKABLE int pastePages(int position = -1);
    Q_INVOKABLE bool deletePages(const QList<int>& pages);
    /// Move pages before the page at index `target` now (page count: to the end); they stay selected.
    Q_INVOKABLE bool movePages(const QList<int>& pages, int target);
    Q_INVOKABLE void duplicatePages(const QList<int>& pages);
    Q_INVOKABLE void undoPages();
    Q_INVOKABLE void redoPages();

    // --- selected elements on the canvas ---
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool cutSelection();
    /// Paste elements (copied in this or another tab, or another Xournal Qt window) as a selection.
    Q_INVOKABLE bool pasteElements();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void selectAllOnPage();
    /// Insert an image file on the current page (as a selection).
    Q_INVOKABLE bool insertImage(const QUrl& file);
    Q_INVOKABLE void clearSelection();

    // --- search ---
    Q_INVOKABLE void searchNext();
    Q_INVOKABLE void searchPrevious();
    Q_INVOKABLE void clearSearch();
    /// Search all open documents (tab overview); the hits per tab are in the tabs model ("searchHits").
    Q_INVOKABLE void searchAllTabs(const QString& query);
    /// Switch to a tab found by searchAllTabs and show its first hit from the current page on.
    Q_INVOKABLE void openSearchResult(int index);

    // --- tabs ---
    /// New empty document in a new tab.
    Q_INVOKABLE void newDocument();
    /// Open a file in a tab: switches to it if it is already open, replaces an untouched new document, else adds a
    /// tab. Returns false (and reports the error) if it cannot be loaded.
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool openPath(const QString& path);
    /// Open several files (e.g. from the command line or another instance).
    Q_INVOKABLE void openPaths(const QStringList& paths);
    Q_INVOKABLE void openUrls(const QList<QUrl>& urls);
    /// Close a tab without asking (QML asks about unsaved changes first). The last tab is replaced by a new one.
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void moveTab(int from, int to);
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();
    Q_INVOKABLE int tabCount() const;
    Q_INVOKABLE bool tabModified(int index) const;
    Q_INVOKABLE QString tabTitle(int index) const;
    /// Indices of tabs with unsaved changes.
    Q_INVOKABLE QVariantList modifiedTabs() const;

    // --- current document ---
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void fitWidth();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void addPageAfterCurrent();

    // --- pages of the current document (index: 0-based page) ---
    Q_INVOKABLE void goToPage(int index);
    /// Go to a page and remember the place for "back" (links, page grid, sidebar).
    Q_INVOKABLE void jumpToPage(int index);
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    Q_INVOKABLE void clearNavigation();
    /// The selected PDF text (select mode): mark it ("highlight", "underline", "strikethrough") or copy it.
    Q_INVOKABLE bool markPdfText(const QString& mode);
    Q_INVOKABLE bool copyPdfText();
    Q_INVOKABLE void clearPdfTextSelection();
    /// Insert `count` new pages before `position` (0-based; page count: at the end): background `background` (index
    /// in the settings' pageBackgrounds), paper `paper` (index in paperFormats; -1: the size of the current page),
    /// portrait or landscape. One step on the page undo stack.
    Q_INVOKABLE bool insertPages(int position, int background, int paper, bool landscape, int count = 1);
    /// The current page: { background (index in pageBackgrounds, -1: other), landscape }
    Q_INVOKABLE QVariantMap currentPageFormat() const;
    /// Ask the window for the "insert pages" dialog (page menu); position as for insertPages.
    Q_INVOKABLE void requestInsertPages(int position) { Q_EMIT insertPagesRequested(position); }
    Q_INVOKABLE void insertPageBefore(int index);
    Q_INVOKABLE void insertPageAfter(int index);
    Q_INVOKABLE void duplicatePage(int index);
    Q_INVOKABLE void deletePage(int index);
    Q_INVOKABLE void movePageUp(int index);
    Q_INVOKABLE void movePageDown(int index);

    // --- tools (shared by all tabs) ---
    /// "pen", "highlighter", "eraser", "hand"
    Q_INVOKABLE void selectTool(const QString& tool);
    Q_INVOKABLE void setColor(const QColor& color);
    /// 0 = very fine ... 4 = very thick (upstream ToolSize)
    Q_INVOKABLE void setSize(int size);

    // --- misc ---
    Q_INVOKABLE QUrl iconUrl(const QString& name) const;
    /// Folder for the Open dialog: the current document's folder, else the last folder a file was opened from.
    Q_INVOKABLE QUrl openFolder() const;
    /// Suggestion for "Save as" (upstream Control::saveImpl): for an annotated PDF the .xopp next to the PDF.
    Q_INVOKABLE QUrl suggestedSaveFile() const;
    /// Suggestion for "Export as PDF": next to the document (name.pdf), for an unsaved annotated PDF
    /// "name_annotated.pdf" next to the PDF.
    Q_INVOKABLE QUrl suggestedExportFile() const;
    /// Export the document as a PDF (upstream's exporter: background PDF pages, ink, text, images).
    Q_INVOKABLE bool exportPdf(const QUrl& file);
    /// Open an external link (from a PDF) in the browser / its application.
    Q_INVOKABLE void openLink(const QString& uri);
    /// Call before quitting: writes settings.
    Q_INVOKABLE void shutdown();

    xqt::AppContext& context() const { return *app; }
    xqt::TabManager& tabManager() const { return *tabs; }

Q_SIGNALS:
    void documentChanged();
    void homeVisibleChanged();
    void titleChanged();
    void modifiedChanged();
    void undoRedoChanged();
    void toolChanged();
    void zoomChanged();
    void pageChanged();
    /// Messages from the core (XojMsgBox) and file errors, shown by QML.
    void message(const QString& title, const QString& text, bool error);
    /// The window should come to the front (e.g. another instance handed over files).
    void raiseRequested();
    void recoveryChanged();
    void searchChanged();
    void viewLayoutChanged();
    void pageUndoChanged();
    void selectionChanged();
    void fontChanged();
    void navigationChanged();
    void pdfTextModeChanged();
    /// PDF text was selected (select mode); rect in canvas coordinates.
    void pdfTextSelected(QRectF rect);
    void pdfTextSelectionCleared();
    /// A PDF link was tapped: uri (external) or page (of this document, -1: none); rect in canvas coordinates.
    void linkTapped(const QString& uri, int page, QRectF rect);
    void copiedPagesChanged();
    void toolbarColorsChanged();
    void insertPagesRequested(int position);
    /// A page operation happened (e.g. "3 pages deleted"); the UI offers to undo it.
    void pageActionDone(const QString& text, bool undoable);

private:
    xqt::DocumentSession* session() const;
    xqt::CanvasView* canvas() const;
    /// Files were renamed or moved (library, recent files): open documents and the recent list follow.
    void filesChanged(const xqt::DocumentFiles::Result& result);
    void currentTabChanged();
    /// Reopens the tabs of a journal; `recovered`: tab index -> recovery file to load instead of the file.
    void reopenTabs(const std::vector<std::pair<fs::path, int>>& tabs, int current,
                    const std::map<size_t, std::pair<fs::path, fs::path>>& recovered);

    std::unique_ptr<xqt::AppContext> app;
    std::unique_ptr<Palette> colors;
    std::unique_ptr<xqt::TabManager> tabs;
    std::unique_ptr<xqt::PagesModel> pages;
    std::unique_ptr<xqt::PageFilterModel> filteredPages;
    std::unique_ptr<xqt::PageClipboard> pageClipboard;
    std::vector<size_t> pageList(const QList<int>& pages) const;
    std::unique_ptr<xqt::SettingsModel> settingsView;
    std::unique_ptr<xqt::LibraryModel> library;
    std::unique_ptr<xqt::RecentFiles> recent;
    bool home = true;
    fs::path journalFile;
    std::unique_ptr<xqt::SessionRecovery> recovery;  // after `tabs`: destroyed first
    bool recoveryPending = false;
    QString pdfMode = "highlight";
    void applyPdfTextMode();
    void storeToolbarColors(const QVariantList& colors);
    std::vector<QMetaObject::Connection> currentConnections;
};
