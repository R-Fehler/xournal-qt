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
class LibraryArchive;
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
namespace LinkRewrite {
struct Change;
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
    /// The current document is a file the app does not keep as a .xopp or PDF (a .md, a text file, an image to write
    /// on): "Open externally" hands it to its app.
    Q_PROPERTY(bool canOpenExternally READ canOpenExternally NOTIFY titleChanged)
    /// Text files are shown on one continuous page (growing with the text) instead of A4 pages. A setting for all
    /// text documents; switching it lays the current one out again.
    Q_PROPERTY(bool textContinuous READ textContinuous WRITE setTextContinuous NOTIFY textLayoutChanged)
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
    /// How documents are kept (session/DocumentMode.h): "xopp" (Xournal++ files) or "pdf" (PDF files: every document
    /// one PDF with notes). Written by the first-start question and Settings → Documents (stored at once).
    Q_PROPERTY(QString documentMode READ documentMode WRITE setDocumentMode NOTIFY documentModeChanged)
    /// The mode in effect is "PDF files".
    Q_PROPERTY(bool pdfOnly READ pdfOnly NOTIFY documentModeChanged)
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
    bool canOpenExternally() const;
    bool textContinuous() const;
    void setTextContinuous(bool on);
    /// Hand the current document's file to the app the system has for it (SystemApps). Unsaved changes are the
    /// window's business (it asks to save first). When the file comes back changed, the tab reads it again.
    Q_INVOKABLE bool openExternally();
    /// "Edit as notes": the current .md as a new document of notes, in a new tab: its text as the page's Markdown text
    /// flowing over pages, to write on with the pen. The .md stays as it is; saving suggests "name.xopp" next to it
    /// (the library shows the two as two documents: they go their own ways).
    Q_INVOKABLE bool editAsNotes();
    /// The file "Open externally" hands over for a library card's path ("" for notes and PDFs): the Markdown, text or
    /// other file, the image a .xopp annotates.
    Q_INVOKABLE QString externalFileOf(const QString& path) const;
    /// Look whether the files of the open tabs changed on disk (another program, a sync app): the text files, and the
    /// .xopp files and PDFs of the documents (DocumentSession::filesChangedOnDisk; the app's own saves never count).
    /// An unmodified one is read again, a modified one is asked about (textChangedOnDisk, documentChangedOnDisk).
    /// Also done when the window becomes active, and after saves.
    Q_INVOKABLE void checkTextFiles();
    /// The answer to textChangedOnDisk / documentChangedOnDisk for the current tab: read the file again (the changes
    /// here are lost; for a text file undo brings them back), or keep the version here (saving writes over the file).
    Q_INVOKABLE void resolveTextChange(bool reload);
    /// Read a document again from its files (changed by another program): the tab keeps its place and page. False if
    /// it could not be read (the tab stays as it was, with a message).
    bool reloadDocument(xqt::DocumentSession* s);
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
    /// "New Markdown file" / "New text file": an empty "name.md" / "name.txt" (`extension`: ".md" or ".txt") in the
    /// library's current folder, opened for writing (the cursor in it).
    Q_INVOKABLE bool createTextFile(const QString& name, const QString& extension);
    /// Open a document found by the library search, with the search active on its first hit.
    Q_INVOKABLE bool openSearchHit(const QString& path, const QString& query);
    /// The same, at a page (0-based) with hits: its first hit is the current one.
    Q_INVOKABLE bool openSearchHitAt(const QString& path, const QString& query, int page);
    /// The same for a Markdown file, at a passage with hits (0-based, see md::passages): its first hit there is the
    /// current one.
    Q_INVOKABLE bool openSearchHitInPassage(const QString& path, const QString& query, int passage);
    /// The libraries in the standard folder: [{ name, path, current }]
    Q_INVOKABLE QVariantList libraries() const;
    /// Open a folder as library in a new window (another process: one library per window). On Android (one window,
    /// SystemApps::librariesInOwnWindows) this window switches to it (switchLibrary). A folder picked through Android's
    /// picker (content://) is opened by its path in the shared storage (ContentFiles::sharedStoragePath), which needs
    /// "All files access"; a folder there without it is asked for first (storageAccessNeeded).
    Q_INVOKABLE void openLibrary(const QUrl& folder);
    /// This window works in another library from now on (Android: there is one window). The open tabs stay; the
    /// library is remembered and opened at the next start (rememberedLibrary).
    void switchLibrary(const fs::path& root);
    /// The library the window worked in last (Android: opened at start instead of the default; empty: none).
    QString rememberedLibrary() const;
    /// Android: the app may read and write the shared storage ("All files access"), so a folder there can be a
    /// library; true where no such permission exists (the desktop).
    Q_PROPERTY(bool storageAccess READ storageAccess NOTIFY storageAccessChanged)
    bool storageAccess() const;
    /// Libraries open in windows of their own (the desktop; on Android the window switches).
    Q_PROPERTY(bool libraryWindows READ libraryWindows CONSTANT)
    bool libraryWindows() const;
    /// After the explanation (storageAccessNeeded, or before the folder picker): show the system's page for "All
    /// files access". When the app is back with it, `thenOpen` is opened as library, or ("") the folder picker is
    /// shown (pickLibraryFolder); without it, a message says what is possible.
    Q_INVOKABLE void requestStorageAccess(const QString& thenOpen);
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
    /// The app's state changed (QGuiApplication::applicationStateChanged; the main window listens). When it goes to
    /// the background (ApplicationSuspended; on Android also ApplicationInactive, which comes first: Android may kill
    /// a background app without warning) the autosaves of the modified documents are written at once and the journal
    /// too, so a force-stop loses nothing that was drawn. The user's files are not saved (only the autosave).
    void applicationStateChanged(Qt::ApplicationState state);
    /// Write the autosave of every document of this window and its undocked windows that changed since its last
    /// autosave (DocumentSession::autosaveChanges), now, if autosaving is on (Settings → Autosave). The count written.
    int autosaveAll();

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
    bool canShare() const;
    /// Open picked files (file dialogs, drops). Files of other apps that are no paths (Android's content:// URIs from
    /// the system's file picker) are received as with receiveFiles.
    Q_INVOKABLE void openUrls(const QList<QUrl>& urls);
    /// Files handed over by another app (Android: "Open with", the share sheet, the file picker; content:// URIs or
    /// paths): a copy of each goes into the library's folder "Opened" and is opened. A file of the same name and size
    /// there already is that copy (opened again, not copied twice). Without a library the folder is in the app's
    /// data. A short note says where the copies are. The copying runs in the background; `filesReceived` tells how
    /// many were opened.
    void receiveFiles(const QStringList& sources);
    /// The folder receiveFiles copies into.
    fs::path receivedFolder() const;
    /// Drawing with the finger (the tool bar's toggle, setting "touchDrawing") on or off, once: the first start on a
    /// device decides (Android: on when it has no stylus); afterwards the user's choice stays.
    void setFingerDrawingDefault(bool on);

private:
    void openReceived(const fs::path& folder, const std::vector<fs::path>& files, const QStringList& errors);

public:
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
    /// As a PDF with notes (hybrid). `oldXopp`: what happens to the .xopp the document was saved as, after the PDF is
    /// written: "trash" (with its files: its attached or pages PDF, background images; the PDF it annotates stays),
    /// "update" (a .xopp for Xournal++, written again on every save of this document: recorded in the PDF), "keep"
    /// (left as it is); "" or "ask": the setting "hybridOldXopp" ("ask" there: kept). `remember`: store it as the
    /// setting (the dialog's "Don't ask again").
    Q_INVOKABLE bool saveAsHybridInBackground(const QUrl& url, const QJSValue& then = QJSValue(),
                                              const QString& oldXopp = QString(), bool remember = false);
    /// The document was saved as "name.xopp" and Save as writes it as a PDF with notes now: the name of that .xopp
    /// if the window asks what happens to it (the setting "hybridOldXopp" is "ask"), else "".
    Q_INVOKABLE QString oldXoppToAsk() const;
    // --- the document mode (qt/docs/hybrid-pdf.md, "PDF-only mode") ---
    QString documentMode() const;
    void setDocumentMode(const QString& mode);
    bool pdfOnly() const;
    /// The first start (of the main window) asks which way to work: nothing chosen yet, and XQT_DOCUMENT_MODE unset.
    Q_INVOKABLE bool askDocumentMode() const;
    /// Save as: the type the dialog starts on, "pdf" (PDF with notes) or "xopp". A hybrid PDF stays a PDF, a .xopp a
    /// .xopp; other documents (new ones, annotated PDFs, images) take the mode's: "pdf" in PDF files mode.
    Q_INVOKABLE QString saveFormat() const;
    Q_INVOKABLE void exportXoppInBackground(const QUrl& url);
    /// The same, waiting until the file is written (tests): whether that worked.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    // Hybrid PDF (qt/docs/hybrid-pdf.md)
    bool isHybrid() const;
    /// Save as → "PDF with notes": the document's own hybrid PDF; for an annotated PDF "name.notes.pdf" next to it, or the
    /// PDF itself with the setting "Save notes into the PDF itself"; else the .xopp suggestion as .pdf.
    Q_INVOKABLE QUrl suggestedHybridFile() const;
    Q_INVOKABLE bool saveAsHybrid(const QUrl& url, const QString& oldXopp = QString());
    /// Save as: whether `file` is written as a PDF with notes (hybrid) or as a .xopp. The extension typed wins (.pdf;
    /// .xopp, .xoj); without one, the type chosen in the dialog (`pdfChosen`).
    Q_INVOKABLE bool savesAsPdf(const QUrl& file, bool pdfChosen) const;
    /// Save as: the file name for the other type (the dialog's name follows the chosen type). The suggestion for one
    /// type becomes the suggestion for the other; else the extension is swapped (a .pdf that is taken by another PDF:
    /// "name.notes.pdf").
    Q_INVOKABLE QUrl fileForFormat(const QUrl& file, bool pdf) const;
    /// Save writes without asking: the document has a file, or it is an annotated PDF and the notes go into it.
    Q_INVOKABLE bool savesWithoutDialog() const;
    /// "Export as .xopp for Xournal++…": "name.xopp" next to the hybrid PDF.
    Q_INVOKABLE QUrl suggestedXoppExport() const;
    Q_INVOKABLE bool exportXopp(const QUrl& url);
    // --- Share (qt/docs/hybrid-pdf.md) ---
    /// What "Share → PDF with notes" does with the current document: "share" (its file as it is: a hybrid PDF without
    /// unsaved changes, a PDF without notes), "save" (saved first: a hybrid PDF with changes, notes that go into the
    /// PDF itself), "ask" (a .xopp: saved as a PDF with notes, or a PDF copy), "saveAs" (no file yet: Save as).
    Q_INVOKABLE QString shareStep() const;
    /// The steps "share" and "save": then the PDF goes to the system (SystemApps::share: the file manager on the
    /// desktop) or, `toClipboard`, onto the clipboard. False for the other steps (the window asks).
    Q_INVOKABLE bool sharePdf(bool toClipboard);
    /// A PDF with notes as a copy at `target` (empty: in the app cache), the document keeps its file and format; then
    /// shared or copied.
    Q_INVOKABLE bool sharePdfCopy(const QUrl& target, bool toClipboard);
    /// "For Xournal++ (.xopp + PDF)": a one-time export into `folder`, never the document's own folder, as
    /// "name.xopp" + "name.xopp.bg.pdf" (upstream's attached PDF; a free name there), then shared. `file`: that PDF
    /// (a library card) instead of the current document.
    Q_INVOKABLE bool shareForXournal(const QUrl& folder, const QString& file = QString());
    /// A file as it is (a library card's PDF): shared or copied.
    Q_INVOKABLE bool shareFile(const QString& path, bool toClipboard);
    /// The text file Share… offers as it is: the current document's (a .md, a text file; "" if it is none), or for a
    /// library card's path the file itself if it is a Markdown or text file. Never a PDF with notes for those.
    Q_INVOKABLE QString sharedTextFile(const QString& path = QString()) const;
    /// Files onto the clipboard, to paste them into another app (SystemApps::copyToClipboard).
    Q_INVOKABLE bool copyToClipboard(const QStringList& files);
    /// The folder the "For Xournal++" dialog starts in: the one chosen last, else the documents folder.
    Q_INVOKABLE QUrl shareFolder() const;
    // --- Archive PDF (qt/docs/hybrid-pdf.md, "Archive PDF") ---
    /// "Export for the archive…": "name.archive.pdf" next to the current document, or next to `file` (a library card's
    /// PDF). Empty when the document has no file yet (the window then asks for a folder).
    Q_INVOKABLE QUrl suggestedArchiveFile(const QString& file = QString()) const;
    /// The archive PDF of the current document (or of `file`) in `folder`.
    Q_INVOKABLE QUrl archiveFileIn(const QUrl& folder, const QString& file = QString()) const;
    /// Write the archive PDF `target` in the background: from the current document as it is now (unsaved changes
    /// included; it keeps its file and state), or from `file`, loaded on a worker without opening a tab. Then
    /// archiveExported, or a message if it failed.
    Q_INVOKABLE bool exportArchive(const QUrl& target, const QString& file = QString());
    /// Archive exports running (their dialog shows it).
    Q_PROPERTY(int archiveExports READ archiveExports NOTIFY archiveExportsChanged)
    int archiveExports() const { return archiveRunning; }
    /// "Export library as archive…" (LibraryArchive: running, done, total, current; finished(summary)).
    Q_PROPERTY(QObject* libraryArchive READ libraryArchiveObject CONSTANT)
    QObject* libraryArchiveObject() const;
    /// Every document of the library (or of the current folder, with its subfolders) as an archive PDF in a new
    /// folder inside `into`, other files copied; in the background. False (and a message) if it cannot start, e.g.
    /// `into` is inside the library.
    Q_INVOKABLE bool exportLibraryArchive(const QUrl& into, bool currentFolderOnly);
    Q_INVOKABLE void cancelLibraryArchive();
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
    /// "Copy link" (qt/docs/links.md): a link to a page of the current document (-1: the current page) onto the
    /// clipboard, as the app's own format, Markdown and HTML (links::toMime). Pasted into a Markdown text it becomes
    /// `[title](link)` relative to that document, on a page a link marker. A document without a file yet: "#Page:12",
    /// a link within it, as before.
    Q_INVOKABLE void copyPageLink(int page);
    /// The same for a chapter of the contents (its title, its page).
    Q_INVOKABLE void copyChapterLink(int page, const QString& title);
    /// The same for a document of the library (a card, a search hit), or a page of it (0-based; -1: the document).
    Q_INVOKABLE bool copyDocumentLink(const QString& path, int page = -1);
    /// The clipboard holds a link (Copy link): its Markdown for the Markdown text being written beside the page
    /// (relative to the current document), else "".
    Q_INVOKABLE QString clipboardLinkMarkdown() const;
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
    /// Share → "PDF with notes" works here (SystemApps::canShare).
    Q_PROPERTY(bool canShare READ canShare CONSTANT)
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
    /// Open an external link (from a PDF) in the browser / its application. A link to a document (links::parse,
    /// "[[wiki]]") is followed in a new tab.
    Q_INVOKABLE void openLink(const QString& uri);

    // --- links between documents (qt/docs/links.md; AppLinks.cpp) ---
    /// About a tapped link (a Markdown link target, "[[a wiki link]]"): { document: it leads to a document, name: the
    /// file's name ("" for this document), place: "page 12", "chapter …", found: the file is there, here: it is
    /// this document }.
    Q_INVOKABLE QVariantMap documentLink(const QString& uri) const;
    /// Follow a link to a document from the current one: "tab" (switches to it when it is open), "reference" (beside
    /// the current document) or "here" (in place of the current document, which closes when it has no unsaved
    /// changes; Back opens it again). The place is looked up (DocumentLinks::placeIn); what was not found is said.
    /// False when it is no link to a document or the file is not found.
    Q_INVOKABLE bool followDocumentLink(const QString& uri, const QString& how);
    /// "Linked from": the documents of the library whose links lead to the current one (the index's links):
    /// [{ name, path, folder (relative to the library) }].
    Q_INVOKABLE QVariantList backlinks() const;
    /// After linkTargetFound: the link is written anew in the document it was followed from, to the file found
    /// (through that document, with undo; saved when it had no unsaved changes).
    Q_INVOKABLE bool updateFoundLink();
    /// After linkTargetMissing: the file the link means, chosen by the reader. The link is written anew, then followed.
    Q_INVOKABLE bool relinkTo(const QUrl& file);
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
    /// This folder can be a library only with "All files access": the window explains why it is asked for, then
    /// calls requestStorageAccess.
    void storageAccessNeeded(const QString& folder);
    void storageAccessChanged();
    /// "All files access" was just given for opening a folder as library: show the folder picker again.
    void pickLibraryFolder();
    /// The window should come to the front (e.g. another instance handed over files).
    void raiseRequested();
    void recoveryChanged();
    void documentModeChanged();
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
    /// A followed link's file was gone; a document of that name (or with that page's text) was found elsewhere in the
    /// library and opened: the window offers to update the link (updateFoundLink).
    void linkTargetFound(const QString& name, const QString& folder);
    /// A followed link's file is gone and nothing like it is in the library: the window offers to locate it (relinkTo).
    void linkTargetMissing(const QString& name);
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
    /// Files were exported for Xournal++ and shown (`text` says where): the window offers to copy them.
    void sharedForXournal(const QStringList& files, const QString& text);
    /// An archive PDF was written: whether it is PDF/A-3b, else why not; what was changed in its source PDF.
    void archiveExported(const QString& path, bool pdfa, const QStringList& notPdfA, const QStringList& adjusted);
    void archiveExportsChanged();
    /// A page operation happened (e.g. "3 pages deleted"); the UI offers to undo it.
    void pageActionDone(const QString& text, bool undoable);
    /// receiveFiles is done: `opened` documents were opened.
    void filesReceived(int opened);
    /// The text file of the current tab changed on disk while it has changes here: the window asks what to keep
    /// (resolveTextChange).
    void textChangedOnDisk(const QString& name);
    /// The same for a document (.xopp, PDF): changed on disk while it has unsaved changes here.
    void documentChangedOnDisk(const QString& name);
    void textLayoutChanged();
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
    /// ExportHybrid: a hybrid PDF copy (to share); ShareXopp: the export for Xournal++ with an attached PDF.
    enum class SaveWay { Save, SaveAs, Hybrid, ExportXopp, ExportHybrid, ShareXopp };
    /// Hand files to the system (share), or put them on the clipboard.
    bool handOver(const QStringList& files, bool toClipboard);
    fs::path lastShareFolder;
    int archiveRunning = 0;
    std::unique_ptr<xqt::LibraryArchive> libraryArchiveTask;
    /// The document an archive is made of: `file`, or the current document's file (empty: none yet).
    fs::path archiveSource(const QString& file) const;
    void archiveDone(const fs::path& target, bool ok, const std::string& error, bool pdfa,
                     const std::vector<std::string>& notPdfA, const std::vector<std::string>& adjusted);
    /// Start saving the current document (see saveInBackground); `then(ok)` after it was written or failed.
    /// `oldXopp`: see saveAsHybridInBackground.
    /// `compact`: a hybrid PDF is written anew in full, not appended to (before it is shared).
    bool startSave(SaveWay way, const fs::path& target, std::function<void(bool)> then,
                   xqt::DocumentSession* document = nullptr, const QString& oldXopp = QString(), bool compact = false);
    /// Wait for the current document's saves; false if the last one failed.
    bool waitForSave();
    /// `then` from QML, after a save: with the saved document's tab current (from the event loop).
    std::function<void(bool)> callWhenSaved(const QJSValue& then);
    /// After a hybrid PDF was saved: its clean copy in the background, the library.
    void afterHybridSave(xqt::DocumentSession& s);
    /// The .xopp a document was saved as goes to the trash, now that its hybrid PDF `pdf` holds everything.
    void trashOldXopp(xqt::DocumentSession& s, const fs::path& xopp, const fs::path& pdf);
    /// Tabs of this process other than `except` that have `file` open (with the window that has them).
    std::vector<std::pair<AppController*, xqt::DocumentSession*>> tabsWithFile(const fs::path& file,
                                                                               const xqt::DocumentSession* except) const;
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
    void checkDocumentFiles(xqt::DocumentSession* s);
    /// The text file's new bytes are shown (the cursor stays where it was, as far as it can).
    void reloadText(xqt::DocumentSession* s, std::string bytes);
    std::unique_ptr<QFileSystemWatcher> textWatcher;

    // --- links between documents (AppLinks.cpp) ---
    /// A link followed from the document `from` (the file holding the link; empty: the current document).
    bool followDocumentLinkFrom(const QString& uri, const QString& how, const fs::path& from);
    /// Back and forward across documents: a place in a document (its tab while it is open, else its file).
    struct DocPlace {
        QPointer<xqt::DocumentSession> session;
        fs::path file;
        int page = 0;
    };
    /// A link followed from one document to another (`here`: in place of it). `depth`: how many places Back had in
    /// the view it arrived in (going back beyond them goes back to `from`).
    struct DocJump {
        DocPlace from;
        DocPlace to;
        bool here = false;
        size_t depth = 0;
    };
    std::vector<DocJump> docBack, docForward;
    bool isCurrentPlace(const DocPlace& place) const;
    /// The jump Back / Forward would take now (nullptr: the view's own places).
    const DocJump* backJump() const;
    const DocJump* forwardJump() const;
    /// Show a place: its tab, else its file opened again, at its page (`replacing`: the current tab goes if it has
    /// no unsaved changes). False when it cannot be shown.
    bool showPlace(const DocPlace& place, bool replacing);
    bool navigateDocuments(bool back);
    /// A link whose file was gone: where it was followed from, as written, and the file found or chosen instead.
    struct Relink {
        QPointer<xqt::DocumentSession> source;
        QString written;
        QString how;
        fs::path target;
    } relink;
    /// Write links anew in an open document (through it, with undo; saved when it had no unsaved changes). Returns
    /// how many were changed.
    int rewriteOpenDocument(xqt::DocumentSession& s, const std::vector<xqt::LinkRewrite::Change>& changes);
    /// After documents were renamed or moved in the app: the links to them, and their own relative links, are
    /// written anew (open documents through themselves, the others in the background), with a note.
    void rewriteLinksAfter(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// The change of a link as written in `source` to lead to `target`.
    std::vector<xqt::LinkRewrite::Change> relinkChange(const xqt::DocumentSession* source, const QString& written,
                                                     const fs::path& target) const;
    QTimer textCheckTimer;  ///< (programs write in steps: looked at a moment after the last change)
    QPointer<xqt::DocumentSession> askingTextChange;
    /// requestStorageAccess: the system's page is shown (the app went to the background for it)
    bool awaitingStorageAccess = false;
    bool leftForStorageAccess = false;
    QString afterStorageAccess;
    void storageAccessAnswered();
};
