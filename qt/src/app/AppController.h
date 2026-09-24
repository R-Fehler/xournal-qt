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
#include <optional>
#include <utility>
#include <vector>

#include <QColor>
#include <QJSValue>
#include <QMetaObject>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

#include "filesystem.h"

class QWindow;

namespace xqt {
class AppContext;
class CanvasView;
class FuzzyQuery;
class DocumentSession;
class TabManager;
class PagesModel;
class PageFilterModel;
class LayersModel;
class ShortcutsModel;
class OutlineModel;
class TextFlowSession;
class MarkdownSession;
class PageClipboard;
class SettingsModel;
class SessionRecovery;
class Library;
class LibraryModel;
class RecentFiles;
class ReferenceMode;
namespace DocumentFiles {
struct Result;
}
}  // namespace xqt
class Palette;

class AppController: public QObject {
    Q_OBJECT
    /// A window of its own for undocked documents: no home screen, it closes with its last tab.
    Q_PROPERTY(bool secondaryWindow READ isSecondary CONSTANT)
    /// Windows open maximized (set by main(); the tests keep their fixed window size)
    Q_PROPERTY(bool startMaximized READ startMaximized CONSTANT)
    Q_PROPERTY(QObject* tabs READ tabsModel CONSTANT)
    /// Pages of the current tab (for the page sidebar).
    Q_PROPERTY(QObject* pages READ pagesModel CONSTANT)
    /// The pages for the sidebar and the page grid, optionally only those with search hits.
    Q_PROPERTY(QObject* filteredPages READ filteredPagesModel CONSTANT)
    /// Table of contents of the current tab (PDF outline)
    Q_PROPERTY(QObject* outline READ outlineModel CONSTANT)
    /// The layers of the current page
    Q_PROPERTY(QObject* layers READ layersModel CONSTANT)
    /// The keyboard shortcuts (the same ones in every window)
    Q_PROPERTY(QObject* shortcuts READ shortcutsModel CONSTANT)
    Q_PROPERTY(QObject* settings READ settingsModel CONSTANT)
    /// How high the pen is above the screen, for pens that tell it (xqt::PenHover)
    Q_PROPERTY(QObject* penHover READ penHover CONSTANT)
    QObject* penHover() const;
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
    /// The current document is being saved (in the background; it stays modified until the file is written).
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    /// A document of this window is being saved.
    Q_PROPERTY(bool anySaving READ anySaving NOTIFY anySavingChanged)
    Q_PROPERTY(bool hasFilePath READ hasFilePath NOTIFY titleChanged)
    /// The current document shows a file it is not (a Markdown file, read-only for now; an image to write on): what
    /// the note over the canvas says about it ("": nothing to say).
    Q_PROPERTY(QString shownFileNote READ shownFileNote NOTIFY titleChanged)
    /// The current document is a text file (qt/docs/md-editor.md): "markdown" or "plain" ("" if not).
    Q_PROPERTY(QString textDocument READ textDocument NOTIFY titleChanged)
    /// ... and it is edited (not shown read-only).
    Q_PROPERTY(bool textEditable READ textEditable NOTIFY titleChanged)
    /// The current document is another text file (code, LaTeX, JSON, ...) shown read-only: it can be edited as plain
    /// text after a warning (editAnyway).
    Q_PROPERTY(bool canEditAnyway READ canEditAnyway NOTIFY titleChanged)
    /// The document is saved as a hybrid PDF (Ctrl+S writes it again).
    Q_PROPERTY(bool isHybrid READ isHybrid NOTIFY titleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color NOTIFY toolChanged)
    /// Font of the text tool (upstream's settings font)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    /// The text tool makes Markdown text boxes (drawn formatted) instead of ordinary texts
    Q_PROPERTY(bool textMarkdown READ textMarkdown WRITE setTextMarkdown NOTIFY fontChanged)
    /// Font size of new Markdown text (default: 60 % of the text font's size)
    Q_PROPERTY(double markdownFontSize READ markdownFontSize WRITE setMarkdownFontSize NOTIFY fontChanged)
    /// Font size of the Markdown text edited beside the page
    Q_PROPERTY(double markdownBoxSize READ markdownBoxSize NOTIFY markdownChanged)
    /// Markdown is written beside the page (its source; the page shows it formatted while typing), else on the page
    /// (formatted while typing, the block with the cursor showing its Markdown)
    Q_PROPERTY(bool markdownInPanel READ markdownInPanel WRITE setMarkdownInPanel NOTIFY fontChanged)
    /// The Markdown text edited beside the page is the page's text (else a text box)
    Q_PROPERTY(bool markdownIsPageText READ markdownIsPageText NOTIFY markdownChanged)
    /// Upstream's drawing type of the tool: default (freehand), strokeRecognizer, line, rectangle, ellipse, arrow,
    /// doubleArrow, drawCoordinateSystem
    Q_PROPERTY(QString drawingType READ drawingType WRITE setDrawingType NOTIFY toolChanged)
    /// 0 = very fine ... 4 = very thick (upstream ToolSize), 5 = the tool's own width (customWidth)
    Q_PROPERTY(int size READ size NOTIFY toolChanged)
    /// The adjustable width of the tool (points; 0: the tool has no sizes). Setting it selects it (size 5).
    Q_PROPERTY(double customWidth READ customWidth WRITE setCustomWidth NOTIFY toolChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    /// The colors in the tool bar (user's choice; default: the first colors of the palette, with orange)
    Q_PROPERTY(QVariantList toolbarColors READ toolbarColors NOTIFY toolbarColorsChanged)
    /// Color of PDF text highlights, one of three presets
    Q_PROPERTY(QColor pdfHighlightColor READ pdfHighlightColor WRITE setPdfHighlightColor NOTIFY pdfTextModeChanged)
    Q_PROPERTY(QVariantList pdfHighlightColors READ pdfHighlightColors CONSTANT)
    /// The text mode edits the typed text of a page (textFlowPage, 0-based); how far it goes below the page (points)
    Q_PROPERTY(bool textFlowActive READ textFlowActive NOTIFY textFlowChanged)
    Q_PROPERTY(int textFlowPage READ textFlowPage NOTIFY textFlowChanged)
    Q_PROPERTY(double textFlowOverflow READ textFlowOverflow NOTIFY textFlowChanged)
    /// A Markdown box is being edited (markdownPage, 0-based); how far it goes below the page (points)
    Q_PROPERTY(bool markdownActive READ markdownActive NOTIFY markdownChanged)
    Q_PROPERTY(int markdownPage READ markdownPage NOTIFY markdownChanged)
    /// The last page of the Markdown text being edited (the page's text flows over pages)
    Q_PROPERTY(int markdownLastPage READ markdownLastPage NOTIFY markdownChanged)
    Q_PROPERTY(double markdownOverflow READ markdownOverflow NOTIFY markdownChanged)
    /// Where the tool bar is: "top", "left" or "right"
    Q_PROPERTY(QString toolbarPosition READ toolbarPosition WRITE setToolbarPosition NOTIFY toolbarPositionChanged)
    /// The tool bar is put away (the small tool square of the full screen takes over)
    Q_PROPERTY(bool toolbarHidden READ toolbarHidden WRITE setToolbarHidden NOTIFY toolbarPositionChanged)
    /// The pen pill without a tool bar: its colors, which side of the screen it is on, and where along that side
    Q_PROPERTY(QVariantList penColors READ penColors NOTIFY penPillChanged)
    Q_PROPERTY(QString penPillSide READ penPillSide WRITE setPenPillSide NOTIFY penPillChanged)
    Q_PROPERTY(double penPillOffset READ penPillOffset WRITE setPenPillOffset NOTIFY penPillChanged)
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
    /// Scrolling sideways: the pages in a row (in viewRows rows), each fit to the height (upstream settings
    /// viewFixedRows with viewLayoutVert, viewRows); snapPages: coming to rest on whole pages (ours)
    Q_PROPERTY(bool horizontalScrolling READ horizontalScrolling WRITE setHorizontalScrolling NOTIFY viewLayoutChanged)
    Q_PROPERTY(int viewRows READ viewRows WRITE setViewRows NOTIFY viewLayoutChanged)
    Q_PROPERTY(bool snapPages READ snapPages WRITE setSnapPages NOTIFY viewLayoutChanged)
    /// Presenting the current document in this window: a page fills the view, a swipe or a key goes one page on
    /// (the window goes full screen for it, in QML)
    Q_PROPERTY(bool presenting READ presenting WRITE setPresenting NOTIFY presentingChanged)
    /// Elements are selected on the canvas (select tools).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // Page operations (sidebar, page grid) go onto the one undo stack of the document (these are the same as undo)
    Q_PROPERTY(bool canUndoPages READ canUndoPages NOTIFY pageUndoChanged)
    Q_PROPERTY(bool canRedoPages READ canRedoPages NOTIFY pageUndoChanged)
    Q_PROPERTY(int copiedPages READ copiedPages NOTIFY copiedPagesChanged)
    /// Back / forward after jumps (links, page grid, sidebar), per tab
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationChanged)
    /// What the PDF text tools do with the selected text: highlight, underline, strikethrough, select
    Q_PROPERTY(QString pdfTextMode READ pdfTextMode WRITE setPdfTextMode NOTIFY pdfTextModeChanged)
    /// Reference mode: another document beside the current one (xqt::ReferenceMode).
    Q_PROPERTY(QObject* reference READ referenceObject CONSTANT)
    /// Documents of a crashed previous run that can be recovered: [{ title, time }]. Empty when there are none.
    Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY recoveryChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    /// A window of its own: the settings, tools, library and rendering of the main window, own tabs.
    explicit AppController(AppController& mainWindow, QObject* parent = nullptr);
    ~AppController() override;

    QObject* tabsModel() const;
    QObject* pagesModel() const;
    QObject* filteredPagesModel() const;
    QObject* outlineModel() const;
    QObject* layersModel() const;
    QObject* shortcutsModel() const;
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
    bool saving() const;
    bool anySaving() const;
    bool hasFilePath() const;
    QString shownFileNote() const;
    QString textDocument() const;
    bool textEditable() const;
    bool canEditAnyway() const;
    /// Edit the text file shown read-only as plain text: the first time for a file the window warns first
    /// (editAnywayWarning), and "OK" calls this again with `confirmed`. From then on the file opens for editing.
    Q_INVOKABLE bool editAnyway(bool confirmed = false);
    /// Look whether the text files of the open tabs changed on disk (another program): an unmodified one is read
    /// again, a modified one is asked about (textChangedOnDisk). Also done when the window becomes active.
    Q_INVOKABLE void checkTextFiles();
    /// The answer to textChangedOnDisk for the current tab: read the file again (the changes here are lost; undo
    /// brings them back), or keep the text here (saving writes over the file).
    Q_INVOKABLE void resolveTextChange(bool reload);
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
    /// The tools' own widths and which are chosen (settings "customWidths": "pen=8.5*,highlighter=42.5,...")
    void loadCustomWidths();
    void storeCustomWidths();
    /// Upstream's palette without white
    QVariantList defaultToolbarColors() const;
    /// Add a color to the tool bar (not twice) / remove the color at `index` / back to the default colors.
    Q_INVOKABLE void addToolbarColor(const QColor& color);
    Q_INVOKABLE void removeToolbarColor(int index);
    Q_INVOKABLE void resetToolbarColors();
    QColor pdfHighlightColor() const;
    void setPdfHighlightColor(const QColor& color);
    QVariantList pdfHighlightColors() const;
    QString toolbarPosition() const;
    bool toolbarHidden() const;
    void setToolbarHidden(bool hidden);
    QVariantList penColors() const;
    Q_INVOKABLE void addPenColor(const QColor& color);
    Q_INVOKABLE void removePenColor(int index);
    Q_INVOKABLE void resetPenColors();
    QString penPillSide() const;
    void setPenPillSide(const QString& side);
    double penPillOffset() const;
    void setPenPillOffset(double offset);
    bool textFlowActive() const;
    int textFlowPage() const { return flowPage; }
    double textFlowOverflow() const { return flowOverflow; }
    /// Text mode: start on the current page; returns its blocks (TextFlow::toVariant) for the editor.
    Q_INVOKABLE QVariantList beginTextFlow();
    /// The blocks as typed: the page follows.
    Q_INVOKABLE void updateTextFlow(const QVariantList& blocks);
    /// Done (keep: one undo step) or cancel.
    Q_INVOKABLE void endTextFlow(bool keep);
    /// The text font (text tool) for the editor.
    Q_INVOKABLE QString textFlowFamily() const;
    bool markdownActive() const;
    bool textMarkdown() const;
    void setTextMarkdown(bool markdown);
    double markdownFontSize() const;
    void setMarkdownFontSize(double size);
    double markdownBoxSize() const;
    bool markdownInPanel() const;
    void setMarkdownInPanel(bool inPanel);
    bool markdownIsPageText() const;
    /// Start editing the Markdown text box drawn at a point of a page (page coordinates), or a new one there, beside
    /// the page. Returns its source.
    Q_INVOKABLE QString beginMarkdownBox(int page, double x, double y);
    /// Markdown being written on the page ends there, to be opened beside the page: {page, pageText, x, y} (empty if
    /// none is written on the page).
    Q_INVOKABLE QVariantMap takeMarkdownFromPage();
    /// The size of the Markdown text edited beside the page (also the size of new Markdown text from now on).
    Q_INVOKABLE void setMarkdownBoxSize(double size);
    int markdownPage() const { return mdPage; }
    int markdownLastPage() const { return mdLastPage; }
    double markdownOverflow() const { return mdOverflow; }
    /// Start editing the Markdown box of a page (-1: the current page; made when there is none). Returns its source.
    Q_INVOKABLE QString beginMarkdown(int page = -1);
    /// The source as typed: the page follows.
    Q_INVOKABLE void updateMarkdown(const QString& source);
    /// Done (keep: one undo step) or cancel.
    Q_INVOKABLE void endMarkdown(bool keep);
    void setToolbarPosition(const QString& position);
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
    bool horizontalScrolling() const;
    void setHorizontalScrolling(bool on);
    int viewRows() const;
    void setViewRows(int rows);
    bool snapPages() const;
    void setSnapPages(bool snap);
    bool presenting() const { return presentingOn; }
    void setPresenting(bool on);
    /// The previous / next page (scrolling sideways: its group, animated), the first / the last one; of the reference
    /// while it has the keys
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void firstPage();
    Q_INVOKABLE void lastPage();

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
    /// The same for a Markdown file, at a passage with hits (0-based, see md::passages): its first hit there is the
    /// current one.
    Q_INVOKABLE bool openSearchHitInPassage(const QString& path, const QString& query, int passage);
    /// The libraries in the standard folder: [{ name, path, current }]
    Q_INVOKABLE QVariantList libraries() const;
    /// Open a folder as library in a new window (another process: one library per window).
    Q_INVOKABLE void openLibrary(const QUrl& folder);
    /// The same by its path (a folder of the library, a library of the Recent grid).
    Q_INVOKABLE void openLibraryAt(const QString& folder) { openLibrary(QUrl::fromLocalFile(folder)); }
    /// New library in the standard folder, opened in a new window. False if the name is taken or invalid.
    Q_INVOKABLE bool createLibrary(const QString& name);
    /// The file manager with the file selected (a folder: opened), see SystemApps.h.
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
    /// Paste what is in the clipboard at a place on the canvas (text becomes a text element there).
    Q_INVOKABLE bool pasteAt(qreal x, qreal y);
    /// There is something to paste (text, an image or elements).
    Q_INVOKABLE bool canPaste() const;
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
    /// The fuzzy search (FuzzyQuery.h) of a name alone: { match: the expression holds with the name, marks: [the
    /// characters matched] } (a query that is not valid: whether the name contains it, no marks).
    Q_INVOKABLE QVariantMap fuzzyName(const QString& query, const QString& name) const;
    /// Why a fuzzy query is searched as plain text ("": it is not).
    Q_INVOKABLE QString fuzzyHint(const QString& query) const;
    /// Switch to a tab found by searchAllTabs and show its first hit from the current page on.
    Q_INVOKABLE void openSearchResult(int index);
    /// The same, at the first hit on or after `page` (a page of the extended search).
    Q_INVOKABLE void openSearchResultAt(int index, int page);
    /// The title page of the current document (its preview in the library and the overview; 0-based, -1: the
    /// document has no file yet, so there is nowhere to keep it).
    Q_PROPERTY(int titlePage READ titlePage NOTIFY titlePageChanged)
    int titlePage() const;
    Q_INVOKABLE bool setTitlePage(int page);

    // --- tabs ---
    /// New empty document in a new tab.
    Q_INVOKABLE void newDocument();
    /// Open a file in a tab: switches to it if it is already open, replaces an untouched new document, else adds a
    /// tab. Returns false (and reports the error) if it cannot be loaded.
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool openPath(const QString& path);
    /// Open several files (e.g. from the command line or another instance).
    Q_INVOKABLE void openPaths(const QStringList& paths);
    /// Open what the home screen lists: documents and text files as tabs, other files with the system app.
    Q_INVOKABLE void openListed(const QStringList& paths);
    /// Open a file with the app the system has for it (SystemApps.h).
    Q_INVOKABLE bool openWithSystemApp(const QString& path);
    /// There is a file manager to show files in (not on Android).
    bool canShowInFileManager() const;
    Q_INVOKABLE void openUrls(const QList<QUrl>& urls);
    /// Show a file beside the current document, as its reference (opened as a tab if it is not open yet; an untouched
    /// new document stays, to write the notes in). Without a document open: opened as the document.
    Q_INVOKABLE bool openAsReference(const QString& path);
    QObject* referenceObject() const;
    xqt::ReferenceMode& reference() const { return *referenceMode; }
    /// Ctrl+S: while the reference has the keys and is written in, it is saved (true). When it needs a file first,
    /// its tab becomes the current one and false is returned (the window then asks for the file as for any
    /// document); false as well when the notes are meant.
    Q_INVOKABLE bool saveReferenceInHand();
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
    /// The tab's document is being saved.
    Q_INVOKABLE bool tabSaving(int index) const;
    /// Call `then(index)` once the tab's document is saved (at once if it is not being saved): with the index it has
    /// then. Not called if the tab was closed meanwhile.
    Q_INVOKABLE void whenSaved(int index, const QJSValue& then);
    /// Call `then()` once no document of this window is being saved (at once if none is).
    Q_INVOKABLE void whenAllSaved(const QJSValue& then);

    // --- current document ---
    /// Save the current document in the background (the window stays usable; errors come as message()). `then()`
    /// is called after it was written, with its tab current again. False if it could not start.
    Q_INVOKABLE bool saveInBackground(const QJSValue& then = QJSValue());
    Q_INVOKABLE bool saveAsInBackground(const QUrl& url, const QJSValue& then = QJSValue());
    Q_INVOKABLE bool saveAsHybridInBackground(const QUrl& url, const QJSValue& then = QJSValue());
    Q_INVOKABLE void exportXoppInBackground(const QUrl& url);
    /// The same, waiting until the file is written (tests): whether that worked.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    // Hybrid PDF (qt/docs/hybrid-pdf.md)
    bool isHybrid() const;
    /// "Save as hybrid PDF…": the document's own hybrid PDF; for an annotated PDF "name.notes.pdf" next to it, or the
    /// PDF itself with the setting "Save notes into the PDF itself"; else the .xopp suggestion as .pdf.
    Q_INVOKABLE QUrl suggestedHybridFile() const;
    Q_INVOKABLE bool saveAsHybrid(const QUrl& url);
    /// Save writes without asking: the document has a file, or it is an annotated PDF and the notes go into it.
    Q_INVOKABLE bool savesWithoutDialog() const;
    /// "Export as .xopp for Xournal++…": "name.xopp" next to the hybrid PDF.
    Q_INVOKABLE QUrl suggestedXoppExport() const;
    Q_INVOKABLE bool exportXopp(const QUrl& url);
    /// After hybridEditedElsewhere: take the other app's version of the changed annotations (or keep ours).
    Q_INVOKABLE bool importHybridChanges();
    Q_INVOKABLE void keepHybridData();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void fitWidth();
    /// The height of the current page fills the view, or the whole page fits into it.
    Q_INVOKABLE void fitHeight();
    Q_INVOKABLE void fitPage();
    /// The current page has another size than the page before or after it (then fitting it alone helps).
    Q_PROPERTY(bool currentPageDiffers READ currentPageDiffers NOTIFY pageChanged)
    bool currentPageDiffers() const;
    Q_INVOKABLE void zoomIn();
    /// Zoom to this (around the middle of the view), e.g. back to what it was.
    Q_INVOKABLE void setZoomPercent(int percent);
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
    /// Select the word of the PDF at this place on the canvas (again at the same word: its whole line).
    Q_INVOKABLE bool selectPdfTextAt(qreal x, qreal y);
    /// Drag one end of that selection (true: the beginning).
    Q_INVOKABLE bool dragPdfSelection(qreal x, qreal y, bool startEnd);
    /// Where the selection begins and ends, for the handles (an empty rect: nothing selected).
    Q_INVOKABLE QRectF pdfSelectionEnds() const;
    /// All of the selected text on the canvas, for the actions beside it (an empty rect: nothing selected).
    Q_INVOKABLE QRectF pdfSelectionBox() const;
    /// Scroll back to the selected text (it can be far away after scrolling).
    Q_INVOKABLE void showPdfSelection();
    /// PDF text is selected right now (then only copying and marking it make sense).
    Q_PROPERTY(bool pdfTextIsSelected READ pdfTextIsSelected NOTIFY pdfTextSelectionChanged)
    bool pdfTextIsSelected() const;
    Q_INVOKABLE void clearPdfTextSelection();
    /// Insert `count` new pages before `position` (0-based; page count: at the end): background `background` (index
    /// in the settings' pageBackgrounds), paper `paper` (index in paperFormats; -1: the size of the current page),
    /// portrait or landscape. One step to undo.
    Q_INVOKABLE bool insertPages(int position, int background, int paper, bool landscape, int count = 1);
    /// Give these pages another background (index in the settings' pageBackgrounds); one undo step.
    Q_INVOKABLE bool changePageBackground(const QList<int>& pages, int background);
    /// One of the pages shows a page of the PDF (changing the background takes that away).
    Q_INVOKABLE bool pagesHavePdfBackground(const QList<int>& pages) const;
    /// The current page: { background (index in pageBackgrounds, -1: other), landscape }
    Q_INVOKABLE QVariantMap currentPageFormat() const;
    /// Ask the window for the "insert pages" dialog (page menu); position as for insertPages.
    Q_INVOKABLE void requestInsertPages(int position) { Q_EMIT insertPagesRequested(position); }
    /// Ask the window for the background dialog for these pages (0-based).
    Q_INVOKABLE void requestPageBackground(const QList<int>& pages) { Q_EMIT pageBackgroundRequested(pages); }
    /// Ask the window for the "new chapter" dialog on that page.
    Q_INVOKABLE void requestChapter(int page) { Q_EMIT chapterRequested(page); }
    /// Write a chapter heading on a page (level 0-2): the contents sidebar and overview show it. Undoable.
    Q_INVOKABLE bool addChapter(int page, const QString& title, int level);
    /// Puts a link to a page ("#Page:12") into the clipboard: pasted into a text it becomes a tappable link.
    Q_INVOKABLE void copyPageLink(int page);
    /// Ask the window for the print dialog, with these pages (0-based; empty: the whole document).
    Q_INVOKABLE void requestPrint(const QList<int>& pages) { Q_EMIT printRequested(pages); }
    Q_INVOKABLE void insertPageBefore(int index);
    Q_INVOKABLE void insertPageAfter(int index);
    Q_INVOKABLE void duplicatePage(int index);
    Q_INVOKABLE void deletePage(int index);
    Q_INVOKABLE void movePageUp(int index);
    Q_INVOKABLE void movePageDown(int index);

    // --- tools (shared by all tabs) ---
    /// "pen", "highlighter", "eraser", "hand"
    Q_INVOKABLE void selectTool(const QString& tool);
    /// Put the setsquare ("setsquare") or the compass ("compass") on the page, or take it away again.
    Q_INVOKABLE void toggleGeometryTool(const QString& which);
    /// For the screenshot hook (it calls methods without arguments)
    Q_INVOKABLE void toggleSetsquare() { toggleGeometryTool("setsquare"); }
    Q_INVOKABLE void toggleCompass() { toggleGeometryTool("compass"); }
    /// Which one lies on the page ("" if none).
    Q_PROPERTY(bool canShowInFileManager READ canShowInFileManager CONSTANT)
    Q_PROPERTY(QString geometryTool READ geometryTool NOTIFY toolChanged)
    QString geometryTool() const;
    /// The setsquare / compass is put aside for a moment (its pill stays, small; a tap brings it back).
    Q_PROPERTY(bool geometryMinimized READ geometryMinimized WRITE setGeometryMinimized NOTIFY toolChanged)
    bool geometryMinimized() const;
    void setGeometryMinimized(bool minimized);
    /// The middle of the setsquare / compass holds on to the nearest ink stroke and slides along it.
    Q_PROPERTY(bool geometryHeldToStroke READ geometryHeldToStroke WRITE setGeometryHeldToStroke NOTIFY toolChanged)
    bool geometryHeldToStroke() const;
    void setGeometryHeldToStroke(bool held);
    /// Draw the marks of the setsquare's scale, every `geometryMarkSpacing` centimetres (1 or 0.5), with the pen.
    Q_INVOKABLE bool drawGeometryMarks();
    Q_PROPERTY(qreal geometryMarkSpacing READ geometryMarkSpacing WRITE setGeometryMarkSpacing NOTIFY toolChanged)
    qreal geometryMarkSpacing() const { return markSpacing; }
    void setGeometryMarkSpacing(qreal cm);
    /// Turning the setsquare / compass goes in steps of 15 degrees.
    Q_PROPERTY(bool geometryAngleSteps READ geometryAngleSteps WRITE setGeometryAngleSteps NOTIFY toolChanged)
    bool geometryAngleSteps() const;
    void setGeometryAngleSteps(bool steps);
    Q_INVOKABLE void setColor(const QColor& color);
    /// 0 = very fine ... 4 = very thick (upstream ToolSize), 5 = the tool's own width
    Q_INVOKABLE void setSize(int size);
    double customWidth() const;
    void setCustomWidth(double points);
    /// Width of a size of the tool (points)
    Q_INVOKABLE double sizeWidth(int size) const;

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
    /// The document annotates a PDF (then printing can offer to print that PDF alone).
    Q_INVOKABLE bool hasPdfBackground() const;
    /// Print: makes a PDF (with what was written on it, or the background PDF alone) and hands it to the system's
    /// print dialog. `range`: "" for everything, else e.g. "2-5" or "3".
    Q_INVOKABLE bool printDocument(bool withAnnotations, const QString& range);
    /// Open an external link (from a PDF) in the browser / its application.
    Q_INVOKABLE void openLink(const QString& uri);
    /// Call before quitting: writes settings.
    Q_INVOKABLE void shutdown();

    xqt::AppContext& context() const { return *app; }
    xqt::TabManager& tabManager() const { return *tabs; }

    // --- windows (undocked documents) ---
    /// The controller of the main window (this one is a window of its own if it has one).
    AppController* mainWindow() const { return primary; }
    /// The windows of undocked documents (of the main window).
    const std::vector<AppController*>& documentWindows() const { return windows; }
    bool isSecondary() const { return primary != nullptr; }
    /// Makes the windows of undocked documents. Set once, from main().
    static void setWindowFactory(std::function<void(AppController*)> factory);
    /// Open windows maximized (people make them smaller with the tiling of their desktop). Set once, from main().
    static void setStartMaximized(bool on);
    bool startMaximized() const;
    /// XQT_LOG_WINDOW=1: every change of a window's state, size and position, the touch and mouse presses around
    /// it, and what the app itself asks of the window, on stderr (the compositor may change a window unasked).
    static void watchWindow(QWindow* window);
    Q_INVOKABLE void logWindow(const QString& what) const;
    /// Move the tab into a window of its own (a new one). Does nothing for the last tab of such a window.
    /// Close every tab of this window (unsaved changes are the UI's business).
    Q_INVOKABLE void closeAllTabs();
    Q_INVOKABLE void undockTab(int index);
    /// Move the tab back into the main window (and close this window if it was the last one).
    Q_INVOKABLE void dockTab(int index);
    /// The window was closed: its documents go back to the main window if they have unsaved changes.
    Q_INVOKABLE void windowClosed();

Q_SIGNALS:
    /// This window should be closed (its last document was moved away).
    void closeWindowRequested();
    void documentChanged();
    void homeVisibleChanged();
    void titleChanged();
    void modifiedChanged();
    void savingChanged();
    void anySavingChanged();
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
    void presentingChanged();
    void pageUndoChanged();
    void selectionChanged();
    void fontChanged();
    void navigationChanged();
    void pdfTextModeChanged();
    /// PDF text was selected (select mode); rect in canvas coordinates.
    void pdfTextSelected(QRectF rect);
    void pdfTextSelectionCleared();
    /// Something was selected or unselected: `pdfTextIsSelected` and the ends of the selection are different now.
    void pdfTextSelectionChanged();
    void titlePageChanged();
    /// A long press or right click on the canvas: the window shows the action pill there.
    void contextRequested(QPointF viewPos);
    /// A PDF link was tapped: uri (external) or page (of this document, -1: none); rect in canvas coordinates.
    void linkTapped(const QString& uri, int page, QRectF rect);
    void copiedPagesChanged();
    void toolbarColorsChanged();
    void insertPagesRequested(int position);
    void pageBackgroundRequested(const QList<int>& pages);
    void printRequested(const QList<int>& pages);
    void chapterRequested(int page);
    void toolbarPositionChanged();
    void penPillChanged();
    void textFlowChanged();
    void markdownChanged();
    /// The text tool tapped a Markdown box: the window opens its editor.
    void markdownRequested(int page);
    /// The text tool tapped a Markdown text box, or a place for a new one: the window opens its editor.
    void markdownBoxRequested(int page, double x, double y);
    /// The hybrid PDF just opened was edited in another app: its ink differs from the Xournal data.
    void hybridEditedElsewhere(const QString& file);
    /// A page operation happened (e.g. "3 pages deleted"); the UI offers to undo it.
    void pageActionDone(const QString& text, bool undoable);
    /// The text file of the current tab changed on disk while it has changes here: the window asks what to keep
    /// (resolveTextChange).
    void textChangedOnDisk(const QString& name);
    /// "Edit anyway" for a file not accepted before: the window warns (OK: editAnyway(true)).
    void editAnywayWarning(const QString& name);

private:
    /// The last query fuzzyName() parsed
    mutable QString fuzzyText;
    mutable std::shared_ptr<const xqt::FuzzyQuery> fuzzyParsed;
    xqt::DocumentSession* session() const;
    xqt::CanvasView* canvas() const;
    /// The current document is a text file: its pages follow its text (no page operations, no ink, no images).
    bool textPagesFixed() const;
    /// Presenting: the view that presents (the current one; another tab takes it over)
    bool presentingOn = false;
    QPointer<xqt::CanvasView> presentedView;
    void updatePresentedView();
    /// The canvas the keys act on: the reference while it has the focus, else the main document's
    xqt::CanvasView* keyCanvas() const;
    void stepPage(int delta);
    void showPage(size_t page);
    /// The reference while it has the keys and is written in (its edit switch), else nullptr: then undo, cut,
    /// paste, delete and select all act on it.
    xqt::CanvasView* editedReference() const;
    bool savesWithoutDialog(const xqt::DocumentSession* s) const;
    enum class SaveWay { Save, SaveAs, Hybrid, ExportXopp };
    /// Start saving the current document (see saveInBackground); `then(ok)` after it was written or failed.
    bool startSave(SaveWay way, const fs::path& target, std::function<void(bool)> then,
                   xqt::DocumentSession* document = nullptr);
    /// Wait for the current document's saves; false if the last one failed.
    bool waitForSave();
    /// `then` from QML, after a save: with the saved document's tab current (from the event loop).
    std::function<void(bool)> callWhenSaved(const QJSValue& then);
    /// After a hybrid PDF was saved: its clean copy in the background, the library.
    void afterHybridSave(xqt::DocumentSession& s);
    std::vector<QJSValue> whenAllSavedCalls;
    /// The text tool of the current tab makes Markdown text or not (textMarkdown, markdownFontSize).
    void applyMarkdownText();
    /// Editing beside the page: the page's text, or the text box at a point.
    QString startMarkdown(int page, std::optional<QPointF> at);
    /// After a change of the Markdown being edited: its pages and how far it goes below one.
    void markdownPagesChanged(double overflow);
    qreal markSpacing = 1.0;
    /// Files were renamed or moved (library, recent files): open documents and the recent list follow.
    void filesChanged(const xqt::DocumentFiles::Result& result);
    void currentTabChanged();
    /// The tab list of this window (with its signals).
    void makeTabs();
    /// Reopens the tabs of a journal; `recovered`: tab index -> recovery file to load instead of the file.
    void reopenTabs(const std::vector<std::pair<fs::path, int>>& tabs, int current,
                    const std::map<size_t, std::pair<fs::path, fs::path>>& recovered);

    /// Shared by all windows of the process (settings, tools, rendering)
    std::shared_ptr<xqt::AppContext> app;
    std::shared_ptr<Palette> colors;
    AppController* primary = nullptr;  ///< the main window's controller (nullptr: this is the main window)
    bool windowGone = false;           ///< its window was closed (it is on its way out)
    std::vector<AppController*> windows;  ///< the main window: the windows of undocked documents
    std::unique_ptr<xqt::TabManager> tabs;
    std::unique_ptr<xqt::ReferenceMode> referenceMode;  ///< (after `tabs`, reset before it)
    bool replacePristine = true;  ///< opening a file replaces an untouched new document (not for a reference)
    std::unique_ptr<xqt::PagesModel> pages;
    std::unique_ptr<xqt::PageFilterModel> filteredPages;
    std::unique_ptr<xqt::OutlineModel> outline;
    std::unique_ptr<xqt::LayersModel> layers;
    std::unique_ptr<xqt::ShortcutsModel> ownShortcuts;
    xqt::ShortcutsModel* shortcuts = nullptr;  ///< the main window's
    std::unique_ptr<xqt::TextFlowSession> flow;
    xqt::DocumentSession* flowSession = nullptr;
    int flowPage = -1;
    double flowOverflow = 0;
    std::unique_ptr<xqt::MarkdownSession> markdown;
    xqt::DocumentSession* mdSession = nullptr;
    int mdPage = -1;
    int mdLastPage = -1;
    double mdOverflow = 0;
    std::unique_ptr<xqt::PageClipboard> ownPageClipboard;
    xqt::PageClipboard* pageClipboard = nullptr;  ///< the main window's: pages can be pasted into any window
    std::vector<size_t> pageList(const QList<int>& pages) const;
    /// A saved document's entry for the library index, from memory (see LibraryIndex::documentSaved).
    void handOverToLibrary(xqt::DocumentSession& s);
    // The main window owns these; the other windows use the same ones (one library and one list of recent files).
    std::unique_ptr<xqt::SettingsModel> ownSettingsView;
    std::unique_ptr<xqt::LibraryModel> ownLibrary;
    std::unique_ptr<xqt::RecentFiles> ownRecent;
    xqt::SettingsModel* settingsView = nullptr;
    xqt::LibraryModel* library = nullptr;
    xqt::RecentFiles* recent = nullptr;
    bool home = true;
    fs::path journalFile;
    std::unique_ptr<xqt::SessionRecovery> recovery;  // after `tabs`: destroyed first
    bool recoveryPending = false;
    QString pdfMode = "highlight";
    void applyPdfTextMode();
    void storeToolbarColors(const QVariantList& colors);
    std::vector<QMetaObject::Connection> currentConnections;

    // --- text files (AppTextFiles.cpp) ---
    /// Open a Markdown or text file as a text document (editable when it can be). nullptr: not such a file.
    std::unique_ptr<xqt::DocumentSession> openTextFile(const fs::path& file, std::string& error);
    /// Watch the files of the open text documents (changes by other programs).
    void watchTextFiles();
    void checkTextFile(xqt::DocumentSession* s);
    /// The text file's new bytes are shown (the cursor stays where it was, as far as it can).
    void reloadText(xqt::DocumentSession* s, std::string bytes);
    std::unique_ptr<QFileSystemWatcher> textWatcher;
    QTimer textCheckTimer;  ///< (programs write in steps: looked at a moment after the last change)
    QPointer<xqt::DocumentSession> askingTextChange;
};
