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
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QColor>
#include <QDateTime>
#include <QMap>
#include <QJSValue>
#include <QMetaObject>
#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QPolygonF>
#include <QQuickTextDocument>
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

#include "control/ToolEnums.h"
#include "filesystem.h"
#include "model/PageRef.h"
#include "model/PageType.h"
#include "util/Color.h"

#include "session/DocumentSession.h"  // (LoadResult: openLoaded)
#include "session/PdfEncryption.h"
#include "session/TextMatch.h"

#include "WindowContext.h"

class QQuickTextDocument;
class QWindow;

namespace xqt::hwr {
class HandwritingSearch;
struct InkStroke;
}

namespace xqt {
class AppServices;
class AudioControl;
class TimelineControl;
class AppContext;
class Citations;
class LibraryInkJob;
class HandwritingSettings;
class CanvasActions;
class CurrentDocument;
class CanvasView;
class LibraryArchive;
class LibraryShare;
class LibraryUnzip;
class LibraryBookmarksModel;
class LibraryTodosModel;
class LibraryTagsModel;
class LibraryMove;
namespace LibraryMigration {
struct Plan;
struct Cleanup;
}
class FuzzyQuery;
class DocumentSession;
class TabManager;
class PagesModel;
class PageFilterModel;
class LayersModel;
class ShortcutsModel;
class OutlineModel;
class AnnotationsModel;
class MarkdownSession;
class MarkdownEditor;
class PageClipboard;
class SettingsModel;
class ToolboxModel;
class SessionRecovery;
class Library;
class LibraryModel;
class RecentFiles;
class ReferenceMode;
class VersionsModel;
class VersionCompare;
class PresenterConsole;
class StickersModel;
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
    /// Highlights and notes of the current tab (the sidebar's Annotations panel, qt/docs/features/annotations-md.md)
    Q_PROPERTY(QObject* annotations READ annotationsModel CONSTANT)
    /// The version history of the current tab (the sidebar's History panel; xqt::VersionsModel, PdfHistory.h)
    Q_PROPERTY(QObject* versions READ versionsModel CONSTANT)
    /// The layers of the current page
    Q_PROPERTY(QObject* layers READ layersModel CONSTANT)
    /// The keyboard shortcuts (the same ones in every window)
    Q_PROPERTY(QObject* shortcuts READ shortcutsModel CONSTANT)
    Q_PROPERTY(QObject* settings READ settingsModel CONSTANT)
    /// The toolbox's tools (qt/docs/features/toolbox.md): the user's own ordered tools, each a tool with its settings;
    /// shared by all windows, stored per device (ToolboxModel)
    Q_PROPERTY(QObject* toolbox READ toolboxObject CONSTANT)
    /// How high the pen is above the screen, for pens that tell it (xqt::PenHover)
    Q_PROPERTY(QObject* penHover READ penHover CONSTANT)
    QObject* penHover() const;
    /// The library of this window (a folder of documents) and the recently opened documents (home screen)
    Q_PROPERTY(QObject* library READ libraryModel CONSTANT)
    /// The handwriting search: its switch, model, download and progress (HandwritingSettings)
    Q_PROPERTY(QObject* handwriting READ handwritingSettings CONSTANT)
    /// The handwriting language of the current document (⋮ → Document → Handwriting language; hwr/LanguagePlan.h):
    /// "auto", "en", "de" or "both"; kept in the library's handwriting cache, not in the file
    Q_PROPERTY(QString handwritingLanguage READ handwritingLanguage WRITE setHandwritingLanguage
                       NOTIFY handwritingLanguageChanged)
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
    /// The current document is a text file (qt/docs/features/md-editor.md): "markdown" or "plain" ("" if not).
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
    // --- encrypted PDFs (AppEncryption.cpp; qt/docs/features/hybrid-pdf.md, "Encrypted PDFs")
    /// The current document is protected with a password (its PDF, or the PDF its notes are on).
    Q_PROPERTY(bool protectedDocument READ protectedDocument NOTIFY titleChanged)
    /// Its password can be set, changed or removed here: its file is a PDF (not an archive PDF).
    Q_PROPERTY(bool canProtect READ canProtect NOTIFY titleChanged)
    // --- annotations of other apps made editable (AppAdopt.cpp; qt/docs/features/adopt-annotations.md)
    /// How many annotations of other apps the current document's PDF has that can be made editable (0: none, or not
    /// looked at yet), and the app that made them (empty: not known).
    Q_PROPERTY(int adoptableCount READ adoptableCount NOTIFY adoptableChanged)
    Q_PROPERTY(QString adoptableApp READ adoptableApp NOTIFY adoptableChanged)
    /// Making them editable runs (on a worker).
    Q_PROPERTY(bool adopting READ adopting NOTIFY adoptableChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color NOTIFY toolChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    /// Dark pages (qt/docs/features/dark-pages.md): "off", "on", or "system" (dark while the system's colors are dark);
    /// the setting darkPages, for every window. Only what is shown: the documents do not change.
    Q_PROPERTY(QString darkPagesMode READ darkPagesMode WRITE setDarkPagesMode NOTIFY darkPagesChanged)
    /// ... whether the pages are shown dark now (the canvases, the page lists)
    Q_PROPERTY(bool darkPagesShown READ darkPagesShown NOTIFY darkPagesChanged)
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
    /// The pen's line style, by upstream's names (StrokeStyle): plain, dash, dashdot, dot ("custom": dashes of a file
    /// or of upstream's settings). Upstream keeps it per tool, in its settings, and in .xopp (`style`).
    Q_PROPERTY(QString lineStyle READ lineStyle WRITE setLineStyle NOTIFY toolChanged)
    /// The tool in hand draws with a line style (upstream: the pen only, TOOL_CAP_LINE_STYLE)
    Q_PROPERTY(bool hasLineStyle READ hasLineStyle NOTIFY toolChanged)
    /// The tool in hand fills what it draws (upstream's tool fill: pen and highlighter, shapes and freehand strokes)
    Q_PROPERTY(bool fillEnabled READ fillEnabled WRITE setFillEnabled NOTIFY toolChanged)
    /// The opacity of the filling, 0-255 (upstream's fill alpha of the tool)
    Q_PROPERTY(int fillAlpha READ fillAlpha WRITE setFillAlpha NOTIFY toolChanged)
    /// The color of the filling; transparent: the stroke's color (only the pen has another, PenFill.h)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY toolChanged)
    /// 0 = very fine ... 4 = very thick (upstream ToolSize), 5 = the tool's own width (customWidth)
    Q_PROPERTY(int size READ size NOTIFY toolChanged)
    /// The adjustable width of the tool (points; 0: the tool has no sizes). Setting it selects it (size 5).
    Q_PROPERTY(double customWidth READ customWidth WRITE setCustomWidth NOTIFY toolChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    /// Color of PDF text highlights, one of three presets
    Q_PROPERTY(QColor pdfHighlightColor READ pdfHighlightColor WRITE setPdfHighlightColor NOTIFY pdfTextModeChanged)
    Q_PROPERTY(QVariantList pdfHighlightColors READ pdfHighlightColors CONSTANT)
    /// The color palettes of the color chooser (ColorPalettes.h, qt/resources/palettes/palettes.json):
    /// [{ id, name, source, background, dark, roles: [{ key, name, ink, highlight }] }]
    Q_PROPERTY(QVariantList colorPalettes READ colorPalettes CONSTANT)
    /// The chosen palette's id (setting "colorPalette"; default: the first, "classic")
    Q_PROPERTY(QString colorPalette READ colorPalette WRITE setColorPalette NOTIFY colorPaletteChanged)
    /// The role of the color in hand when it was taken from a palette: "marker:warnings" (else "")
    Q_PROPERTY(QString colorRole READ colorRole NOTIFY toolChanged)
    /// A Markdown box is being edited (markdownPage, 0-based); how far it goes below the page (points)
    Q_PROPERTY(bool markdownActive READ markdownActive NOTIFY markdownChanged)
    /// Markdown is being written on the page itself (formatted while typing), not in the panel beside it.
    Q_PROPERTY(bool markdownOnPage READ markdownOnPage NOTIFY markdownOnPageChanged)
    /// What is at the cursor of the Markdown being written on the page or in a .md, for the formatting bar
    /// (md::format::State: bold, italic, strike, code, math, link, heading, list, quote, codeBlock, table)
    Q_PROPERTY(QVariantMap markdownFormat READ markdownFormat NOTIFY markdownFormatChanged)
    Q_PROPERTY(int markdownPage READ markdownPage NOTIFY markdownChanged)
    /// The last page of the Markdown text being edited (the page's text flows over pages)
    Q_PROPERTY(int markdownLastPage READ markdownLastPage NOTIFY markdownChanged)
    Q_PROPERTY(double markdownOverflow READ markdownOverflow NOTIFY markdownChanged)
    /// The command bar is put away (its tab at the top edge; a slim strip brings it back)
    Q_PROPERTY(bool toolbarHidden READ toolbarHidden WRITE setToolbarHidden NOTIFY toolbarHiddenChanged)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
    /// The canvas turned (qt/docs/features/canvas-rotation.md): degrees clockwise, in [0, 360); 0 upright
    Q_PROPERTY(double canvasRotation READ canvasRotation NOTIFY canvasRotationChanged)
    /// It may be turned here (not while presenting, not in a text file or a text document of notes)
    Q_PROPERTY(bool canRotateCanvas READ canRotateCanvas NOTIFY canvasRotationChanged)
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
    /// The search is read with the fuzzy search's syntax: that of the current search while there is one (e.g. handed
    /// over from the library), else the app-wide setting (app.library.fuzzySearch) new searches take. Set (the toggle
    /// in the search bar): the setting, and the current search runs again in that mode.
    Q_PROPERTY(bool searchFuzzy READ searchFuzzy WRITE setSearchFuzzy NOTIFY searchFuzzyChanged)
    /// Why the current (fuzzy) search is read as plain text, or a regular expression is not searched ("": neither).
    Q_PROPERTY(QString searchHint READ searchHint NOTIFY searchChanged)
    /// Find and replace (qt/docs/features/md-editor.md, "Find and replace"): the replace row of the search bar is
    /// shown, and its options, in effect while it is (the search runs again with them, never fuzzy then). For this
    /// window.
    Q_PROPERTY(bool replacing READ replacing WRITE setReplacing NOTIFY searchOptionsChanged)
    Q_PROPERTY(bool searchCaseSensitive READ searchCaseSensitive WRITE setSearchCaseSensitive NOTIFY
                       searchOptionsChanged)
    Q_PROPERTY(bool searchWholeWord READ searchWholeWord WRITE setSearchWholeWord NOTIFY searchOptionsChanged)
    Q_PROPERTY(bool searchRegex READ searchRegex WRITE setSearchRegex NOTIFY searchOptionsChanged)
    /// The current document has text that find and replace can change: an edited text file, or notes with Markdown
    /// text shown (a PDF text document, Markdown text boxes, sticky notes' texts). Not read-only files, not PDF text.
    Q_PROPERTY(bool canReplace READ canReplace NOTIFY markdownOnPageChanged)
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
    /// What acts on the current document's canvas (xqt::CanvasActions): its selection, sticky notes, PDF text, the
    /// clipboard, page, zoom and Back; the pills of the notes take it as their target (the reference's: app.reference.edit)
    Q_PROPERTY(QObject* edit READ editObject CONSTANT)
    /// What the keys act on (a CanvasActions): the reference's canvas while it has the focus, else the current
    /// document's (app.edit). Group, ungroup, cut, delete, zoom and the fits of the keys and the view pill go here; the
    /// reference refuses what changes it unless it is written in (CanvasActions::readingOnly).
    Q_PROPERTY(QObject* keyTarget READ keyTargetObject NOTIFY keyTargetChanged)
    /// The snip tool (qt/docs/features/snip.md) is armed: "rect" or "lasso" ("": not). The next rectangle or lasso
    /// dragged on a page copies its picture to the clipboard, then the tool used before comes back.
    Q_PROPERTY(QString snip READ snipShape NOTIFY snipChanged)
    /// How sharp a snip's picture is: "screen" (the screen's, at least 200 dpi; the default), "high" (300 dpi) or
    /// "veryHigh" (600 dpi); Settings and the snip's list (Snip.h: the limits)
    Q_PROPERTY(QString snipResolution READ snipResolution WRITE setSnipResolution NOTIFY snipResolutionChanged)
    /// "Copy handwriting as text" is armed (qt/docs/features/handwriting-search.md, AppInkCopy.cpp): the next sweep
    /// (the lasso's path) over ink copies the words there as text, then the tool used before comes back
    Q_PROPERTY(bool inkCopy READ inkCopyArmed NOTIFY snipChanged)
    /// The selection holds handwriting (pen strokes): its pill offers "Copy as text"
    Q_PROPERTY(bool selectionHasInk READ selectionHasInk NOTIFY selectionChanged)
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
    /// Comparing two versions, or a version and now, in the reference view (xqt::VersionCompare)
    Q_PROPERTY(QObject* compare READ compareObject CONSTANT)
    /// The current document is a version of a PDF with notes cut out of its file: shown read-only
    Q_PROPERTY(bool viewingVersion READ viewingVersion NOTIFY documentChanged)
    /// The presenter view on a second screen while presenting (xqt::PresenterConsole,
    /// qt/docs/features/presenter-view.md)
    Q_PROPERTY(QObject* presenter READ presenterObject CONSTANT)
    /// Looking up selected text, papers of references, arXiv (Citations.h, qt/docs/features/citations.md)
    Q_PROPERTY(QObject* citations READ citationsObject CONSTANT)
    /// Recording and playing (AudioControl.h, qt/docs/features/audio.md)
    Q_PROPERTY(QObject* audio READ audioObject CONSTANT)
    /// The replay of the document's timeline (TimelineControl.h, qt/docs/features/timeline.md)
    Q_PROPERTY(QObject* timeline READ timelineObject CONSTANT)
    /// Documents of a crashed previous run that can be recovered: [{ title, time }]. Empty when there are none.
    Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY recoveryChanged)
    /// How documents are kept (session/DocumentMode.h): "xopp" (Xournal++ files) or "pdf" (PDF files: every document
    /// one PDF with notes). Written by the first-start question and Settings → Documents (stored at once).
    Q_PROPERTY(QString documentMode READ documentMode WRITE setDocumentMode NOTIFY documentModeChanged)
    /// The introduction was shown (qt/docs/features/onboarding.md): finished or skipped, at the first start or later
    /// from Help. The setting "introSeen" (stored at once).
    Q_PROPERTY(bool introSeen READ introSeen WRITE setIntroSeen NOTIFY documentModeChanged)
    /// The user's copy of the tutorial is there (Help → Start the tutorial again is offered).
    Q_PROPERTY(bool tutorialExists READ tutorialExists NOTIFY tutorialChanged)
    /// The mode in effect is "PDF files".
    Q_PROPERTY(bool pdfOnly READ pdfOnly NOTIFY documentModeChanged)
    /// New text documents are PDF text documents (the setting "newTextDocuments", qt/docs/features/md-pdf.md), not
    /// ".md" files.
    Q_PROPERTY(bool newTextAsPdf READ newTextAsPdf NOTIFY documentModeChanged)
    /// The current document is a text document of notes (qt/docs/features/md-pdf.md: page 1 starts the page's Markdown
    /// text; a PDF text document): typing goes into its text, the formatting bar is shown.
    Q_PROPERTY(bool textNotes READ textNotes NOTIFY markdownOnPageChanged)
    /// The current document (notes) has a page's Markdown text: "Export as Markdown" is offered.
    Q_PROPERTY(bool hasMarkdownText READ hasMarkdownText NOTIFY markdownOnPageChanged)
public:
    /// A window on its own services (made here: the tests; the app gives main()'s).
    explicit AppController(QObject* parent = nullptr);
    /// A window on the process's services: the main window if it is the first one made on them, else a window of
    /// undocked documents (the main window's settings, tools, library and rendering, its own tabs).
    explicit AppController(xqt::AppServices& services, QObject* parent = nullptr);
    ~AppController() override;

    QObject* tabsModel() const;
    QObject* pagesModel() const;
    QObject* filteredPagesModel() const;
    QObject* outlineModel() const;
    QObject* annotationsModel() const;
    QObject* versionsModel() const;
    QObject* layersModel() const;
    QObject* shortcutsModel() const;
    QObject* settingsModel() const;
    QObject* libraryModel() const;
    QObject* handwritingSettings() const;
    QString handwritingLanguage() const;
    void setHandwritingLanguage(const QString& language);
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
    /// "Open as PDF document" (qt/docs/features/md-pdf.md): the current .md as a new PDF text document next to it
    /// ("name.pdf", or "name (2).pdf" when taken), built as editAsNotes builds its notes, saved at once as a PDF with
    /// notes and opened in a new tab with the cursor in its text. The .md stays as it is.
    Q_INVOKABLE bool openAsPdfDocument();
    bool textNotes() const;
    bool hasMarkdownText() const;
    /// "Export as Markdown": where it writes without asking ("name.md" next to the document, Xournal++ files mode);
    /// empty when the window asks where (PDF files mode, a document without a file).
    Q_INVOKABLE QUrl markdownExportFile() const;
    /// Where the dialog of "Export as Markdown" starts: "name.md" next to the document, else in the library's folder.
    Q_INVOKABLE QUrl suggestedMarkdownExport() const;
    /// Write the Markdown of the current document's page texts (TextDocument::markdown) to `file` (".md" added when it
    /// has no extension). The window says what was written (pageActionDone) or why not (message).
    Q_INVOKABLE bool exportMarkdown(const QUrl& file);
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
    QString lineStyle() const;
    void setLineStyle(const QString& name);
    bool hasLineStyle() const;
    bool fillEnabled() const;
    void setFillEnabled(bool on);
    int fillAlpha() const;
    void setFillAlpha(int alpha);
    QColor fillColor() const;
    void setFillColor(const QColor& c);
    int size() const;
    QVariantList palette() const;
    /// The tools' own widths and which are chosen (settings "customWidths": "pen=8.5*,highlighter=42.5,...")
    void loadCustomWidths();
    void storeCustomWidths();
    QColor pdfHighlightColor() const;
    void setPdfHighlightColor(const QColor& color);
    QVariantList pdfHighlightColors() const;
    QVariantList colorPalettes() const;
    QString colorPalette() const;
    void setColorPalette(const QString& id);
    /// The background color of the current page (white without a document)
    Q_INVOKABLE QColor paperColor() const;
    /// The highlighter's opacity on the current page's paper: 0.5 on light paper, 0.8 on dark
    /// (ColorPalettes::highlighterOpacity)
    Q_INVOKABLE double highlighterOpacity() const;
    // --- colors taken from a palette remember their role (qt/docs/features/color-palettes.md) ---
    /// The tool in hand takes the color of `role` in palette `paletteId` (its ink; the highlight color for the
    /// highlighter) and remembers the role; the palette becomes the chosen one, and the other tools' colors taken from
    /// a palette follow it.
    Q_INVOKABLE void setPaletteColor(const QString& paletteId, const QString& role);
    QString colorRole() const;
    /// The role of a tool's color ("pen", "highlighter", "text"): "marker:warnings", or "" when it was not taken
    /// from a palette (or has been changed since)
    Q_INVOKABLE QString colorRoleOf(const QString& tool) const;
    /// The color of `role` in `paletteId` (invalid: the palette leaves the role out). For tool presets (qt/toolbox).
    Q_INVOKABLE QColor paletteColor(const QString& paletteId, const QString& role, bool highlight) const;
    /// What a color taken as `ref` ("marker:warnings") becomes in `paletteId` (invalid: that palette has no such
    /// role, the color stays as it is). For tool presets that follow a palette switch (qt/toolbox).
    Q_INVOKABLE QColor followPalette(const QString& ref, const QString& paletteId, bool highlight) const;

    // --- the toolbox (qt/docs/features/toolbox.md; AppToolbox.cpp) ---
    QObject* toolboxObject() const;
    xqt::ToolboxModel* toolboxModel() const { return toolbox; }
    /// Takes the toolbox's entry `id`: its tool with all its settings (color, width, line style, filling, eraser kind,
    /// font, …), and it becomes the active entry. A sticky note entry puts a note in its color on the page instead.
    Q_INVOKABLE bool applyToolEntry(const QString& id);
    /// The tool in hand is this entry's tool (its kind, drawing type and variant)
    Q_INVOKABLE bool entryInHand(const QVariantMap& entry) const;
    /// The color an entry draws with now: its role's in the chosen palette, else its own
    Q_INVOKABLE QColor toolEntryColor(const QVariantMap& entry) const;
    /// The keys P, H, E, T: the toolbox's entry of that type used last ("pen", "highlighter", "eraser", "text");
    /// without one, the plain tool
    Q_INVOKABLE void takeToolOfType(const QString& type);
    bool toolbarHidden() const;
    void setToolbarHidden(bool hidden);
    /// The text font's family (the text tool's font without its style)
    QString textFontFamily() const;
    bool markdownActive() const;
    QString darkPagesMode() const;
    void setDarkPagesMode(const QString& mode);
    bool darkPagesShown() const;
    /// The curated page colors (qt/docs/features/dark-pages.md, "Page colors"): [{ id, name, color, dark }]
    Q_INVOKABLE QVariantList paperSwatches() const;
    /// The pages printed (`range` as printDocument takes it) have dark paper (a page color, not a PDF page): printing
    /// them takes a lot of ink, the print dialog says so
    Q_INVOKABLE bool printUsesDarkPaper(const QString& range) const;
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
    bool markdownOnPage() const;
    // --- the formatting bar (qt/docs/features/md-editor.md, "Formatting bar"; AppMarkdownFormat.cpp) -----------------
    QVariantMap markdownFormat() const;
    /// A formatting tool (md::format::actionNamed: "bold", "heading2", "codeBlock" with the language as `arg`, ...)
    /// on the Markdown written on the page or in the .md: one undo step. False if no Markdown is written.
    Q_INVOKABLE bool formatMarkdown(const QString& action, const QString& arg = QString());
    /// Pictures (files: the formatting bar's picker, a drop) into the Markdown written on the page or in the .md:
    /// saved where the document keeps its pictures ("name.assets/"), their Markdown at the cursor, one undo step
    /// (qt/docs/features/md-images.md). False if nothing was inserted (a message says why when a picture could not be
    /// saved).
    Q_INVOKABLE bool insertMarkdownImages(const QList<QUrl>& files);
    /// Fetch the web picture at `url` (its "Load image" was tapped, the window showed the address and the user
    /// agreed; qt/docs/features/md-images.md) into the app cache, and lay out the texts that show it again. Choosing it
    /// is the opt-in to networking when that was not decided yet; false (a message) when networking is off.
    Q_INVOKABLE bool loadWebImage(const QString& url);
    /// "Remove unused images" of a .md: the files in its "name.assets" folder that its text (as it is now) does not
    /// link to, as paths relative to that folder; empty when there are none (or it is no .md).
    Q_INVOKABLE QStringList unusedMarkdownImages() const;
    /// Move these files of the .md's "name.assets" folder (as unusedMarkdownImages gives them) to the trash. The count
    /// moved.
    Q_INVOKABLE int trashMarkdownImages(const QStringList& files);
    /// The same on the source beside the page (its TextArea's document: one undo step there). The selection after it:
    /// {anchor, caret} (the text's offsets); empty if nothing was done.
    Q_INVOKABLE QVariantMap formatMarkdownIn(QQuickTextDocument* document, int anchor, int caret,
                                             const QString& action, const QString& arg = QString());
    /// What is at the cursor of a source (the editor beside the page), as markdownFormat.
    Q_INVOKABLE QVariantMap markdownFormatOf(const QString& text, int anchor, int caret) const;
    /// The table at the cursor, for the table editor: {found, cells: [[header cells], [row cells], ...], aligns:
    /// ["left" | "center" | "right" | "", ...], row (0: the header), column}; found false: none there.
    Q_INVOKABLE QVariantMap markdownTable() const;
    Q_INVOKABLE QVariantMap markdownTableIn(const QString& text, int caret) const;
    /// The table editor's table as a GFM pipe table over the table at the cursor, or as a new one there. One undo
    /// step.
    Q_INVOKABLE bool writeMarkdownTable(const QVariantList& cells, const QStringList& aligns);
    Q_INVOKABLE QVariantMap writeMarkdownTableIn(QQuickTextDocument* document, int anchor, int caret,
                                                 const QVariantList& cells, const QStringList& aligns);
    /// Write the current page's Markdown text on the page, formatted while typing (the default of the tool bar's
    /// write button; its source beside the page is the button's menu). The cursor goes to the end of what that page
    /// holds; a page without Markdown text starts one at its top. False if nothing can be written there.
    Q_INVOKABLE bool writeMarkdownOnPage();
    /// Stop writing on the page (the text stays).
    Q_INVOKABLE void endMarkdownOnPage();
    /// The source as typed: the page follows.
    Q_INVOKABLE void updateMarkdown(const QString& source);
    /// Done (keep: one undo step) or cancel.
    Q_INVOKABLE void endMarkdown(bool keep);
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
    bool searchFuzzy() const;
    void setSearchFuzzy(bool fuzzy);
    QString searchHint() const;
    bool replacing() const { return replaceRow; }
    void setReplacing(bool on);
    bool searchCaseSensitive() const { return replaceOptions.caseSensitive; }
    void setSearchCaseSensitive(bool on);
    bool searchWholeWord() const { return replaceOptions.wholeWord; }
    void setSearchWholeWord(bool on);
    bool searchRegex() const { return replaceOptions.regex; }
    void setSearchRegex(bool on);
    bool canReplace() const;
    /// What acts on the current document's canvas (the pills; the keys go through keyTarget)
    xqt::CanvasActions& edit() const { return *edits; }
    QObject* editObject() const;
    /// What the keys act on now (app.keyTarget)
    xqt::CanvasActions& keyTarget() const { return *keyActions(); }
    QObject* keyTargetObject() const;
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
    /// A new document of notes saved at once as `path` (".pdf": a PDF with notes, else a .xopp), in a new tab.
    bool createDocumentAt(fs::path path);
    /// Quick note (qt/docs/features/quick-note.md; AppQuickNote.cpp): as the setting "quickNote" says, a new note in
    /// the library's folder "Inbox" named by the date and time ("2026-10-04 21-30.xopp", or ".pdf" in the PDF files
    /// mode), opened at once with the pen in hand; or ("daily") a line "- 21:30 " added to "Inbox/2026-10-04.md",
    /// opened with the cursor at its end. The folder and the file are made on first use. Without a library: a new
    /// unsaved document. The home screen, ⋮, Ctrl+Alt+N and `xournal-qt --quick-note` call it.
    Q_INVOKABLE bool quickNote();
    /// The same at a given time (tests).
    bool quickNoteAt(const QDateTime& when);
    /// The library's folder of quick notes (a fixed English name, as "Stickers")
    static constexpr const char* QUICK_NOTE_FOLDER = "Inbox";
    /// "New Markdown file" / "New text file": an empty "name.md" / "name.txt" (`extension`: ".md" or ".txt") in the
    /// library's current folder, opened for writing (the cursor in it).
    Q_INVOKABLE bool createTextFile(const QString& name, const QString& extension);
    /// "New text document" (qt/docs/features/md-pdf.md): a PDF text document "name.pdf" (a PDF with notes whose page 1
    /// starts an empty Markdown text) in the library's current folder, saved at once and opened with the cursor in its
    /// text; or, when new text documents are Markdown files (newTextAsPdf false), "name.md" as createTextFile.
    Q_INVOKABLE bool createTextDocument(const QString& name);
    bool newTextAsPdf() const;
    /// A new PDF text document of `text` saved as `pdf` (a PDF with notes), in a new tab with the cursor in its text.
    bool makeTextPdf(const std::string& text, const fs::path& pdf);
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

    // --- where the libraries live (Android: the app's own folder or the phone's Documents; LibraryMigration.h) ---
    /// Android: the libraries are in the app's own folder, which Android deletes with the app (the home screen shows
    /// a hint, and tapping it offers the move). False on the desktop.
    Q_PROPERTY(bool librariesInApp READ librariesInApp NOTIFY librariesHomeChanged)
    bool librariesInApp() const;
    /// The offer to move them is due at the start: they are in the app's folder and it was not declined.
    Q_PROPERTY(bool offerLibrariesHome READ offerLibrariesHome NOTIFY librariesHomeChanged)
    bool offerLibrariesHome() const;
    /// The libraries' home in the shared storage as the user sees it ("Documents/Xournal_Libraries").
    Q_PROPERTY(QString sharedLibrariesName READ sharedLibrariesName CONSTANT)
    QString sharedLibrariesName() const;
    /// What is in the app's folder to be moved ("12 files, 35 MB"; "" when nothing).
    Q_INVOKABLE QString librariesToMove() const;
    /// The move in progress (LibraryMove: running, step "copy" / "verify" / "clean", fraction, files, totalFiles).
    Q_PROPERTY(QObject* libraryMove READ libraryMoveObject CONSTANT)
    QObject* libraryMoveObject() const;
    /// At the start (before the library is chosen): the home from the setting "librariesHome" and "All files access";
    /// a move that was ended before its clean-up is finished. With the access and nothing to move, the phone's
    /// folder is used at once.
    void chooseLibrariesHome();
    /// "Continue" of the offer: with "All files access" (asked for first when needed) the libraries move to the
    /// phone's Documents/Xournal_Libraries, in the background (LibraryMigration.h), then the app works there.
    Q_INVOKABLE void moveLibrariesHome();
    /// "Not now": no offer at the start any more (the hint stays and asks again).
    Q_INVOKABLE void declineLibrariesHome();
    /// Cancel the move while it copies (nothing changes).
    Q_INVOKABLE void cancelLibrariesMove();
    /// In-app folder chooser (Android with "All files access": the system's picker refuses the Download folder and
    /// the storage's root): where it starts, and the folders in a folder [{ name, path }] (hidden ones left out).
    Q_PROPERTY(bool inAppFolderChooser READ inAppFolderChooser NOTIFY storageAccessChanged)
    bool inAppFolderChooser() const;
    Q_PROPERTY(QString storageRoot READ storageRoot CONSTANT)
    QString storageRoot() const;
    Q_INVOKABLE QVariantList subfolders(const QString& folder) const;

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
    // (group, ungroup, cut, delete: app.keyTarget; inserting an image: app.edit)
    Q_INVOKABLE bool copySelection();
    /// Paste elements (copied in this or another tab, or another Xournal Qt window) as a selection.
    Q_INVOKABLE bool pasteElements();
    Q_INVOKABLE void selectAllOnPage();
    Q_INVOKABLE void clearSelection();

    // --- search ---
    /// Find and replace (qt/src/canvas/FindReplace.h): the current hit of the search replaced with `with` (one undo
    /// step), and the next one current. "replaced"; "skipped": it cannot be replaced (PDF text, a plain text,
    /// handwriting), the next is current; "shown": there was no current hit, the first is current now; "": no hits.
    Q_INVOKABLE QString replaceCurrent(const QString& with);
    /// Every match replaced with `with`, as one undo step. Returns how many.
    Q_INVOKABLE int replaceAll(const QString& with);
    /// The same in the source beside the page (its TextArea's document, MarkdownPanel), as one undo step of it.
    /// `all`: every match ({count}); else the selection [from, to) replaced if it is a match ({replaced: true}),
    /// and the next match selected ({anchor, caret}; none: no "anchor").
    Q_INVOKABLE QVariantMap replaceInSource(QQuickTextDocument* document, int from, int to, const QString& with,
                                            bool all);
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
    // --- bookmarks and favourites (AppBookmarks.cpp, qt/docs/features/bookmarks.md) ---
    /// The bookmarks of the current document, in page order: [{ page (0-based), label (as shown), automatic }]
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    /// The current document can have bookmarks (not a plain text file)
    Q_PROPERTY(bool canBookmark READ canBookmark NOTIFY titleChanged)
    /// The current document is a favourite (starred; kept beside it, DocumentPlaces)
    Q_PROPERTY(bool favourite READ favourite WRITE setFavourite NOTIFY favouriteChanged)
    /// It has a file to keep a star for
    Q_PROPERTY(bool canFavourite READ canFavourite NOTIFY titleChanged)
    /// The library's Bookmarks view (LibraryBookmarksModel)
    Q_PROPERTY(QObject* libraryBookmarks READ libraryBookmarksModel CONSTANT)
    QVariantList bookmarks() const;
    bool canBookmark() const;
    /// A page's bookmark as shown ("": none).
    Q_INVOKABLE QString bookmarkOf(int page) const;
    Q_INVOKABLE bool isBookmarked(int page) const;
    /// Bookmark a page (its label: its first heading, or the PDF's table of contents entry for it, else "Page N") or
    /// remove its bookmark. One undo step.
    Q_INVOKABLE bool toggleBookmark(int page);
    /// Name a page's bookmark ("" or "Page N": the automatic label). One undo step.
    Q_INVOKABLE bool renameBookmark(int page, const QString& label);
    /// What a new bookmark of the page is called ("": the automatic label).
    QString defaultBookmarkLabel(int page) const;
    bool favourite() const;
    void setFavourite(bool on);
    bool canFavourite() const;
    /// The star of any document (a tab's file, a card's path).
    Q_INVOKABLE bool isFavouriteFile(const QString& path) const;
    Q_INVOKABLE void setFavouriteFile(const QString& path, bool on);
    /// Open a document at a page (a bookmark of the library's Bookmarks view).
    Q_INVOKABLE bool openBookmark(const QString& path, int page);
    QObject* libraryBookmarksModel() const;

    // --- to-dos (AppTodos.cpp, qt/docs/features/todos.md) ---
    // --- tags (AppTags.cpp, qt/docs/features/tags.md) ---
    /// The library's Tags view (LibraryTagsModel)
    Q_PROPERTY(QObject* libraryTags READ libraryTagsModel CONSTANT)
    QObject* libraryTagsModel() const;
    /// The main file of the current tab's document (its .xopp, PDF, Markdown or text file; "": none)
    Q_INVOKABLE QString currentDocumentPath() const;
    /// "Tags…" of a document (its main file): { name, pdf, typed (the #tags typed in it, from the index), file (a PDF's
    /// keywords as tags), suggestions (the library's tags, most used first), editable (a PDF that can be written), why
    /// (why not, for the dialog) }
    Q_INVOKABLE QVariantMap documentTags(const QString& path) const;
    /// Give a PDF these tags as its keywords (an incremental update, in the background; tabs showing it read it again).
    /// Refused with a message when a tab has unsaved changes of it.
    Q_INVOKABLE bool setDocumentTags(const QString& path, const QStringList& tags);

    /// The library's To-dos view (LibraryTodosModel)
    Q_PROPERTY(QObject* libraryTodos READ libraryTodosModel CONSTANT)
    QObject* libraryTodosModel() const;
    /// Tick or untick a to-do of the To-dos view (its text as written and its occurrence, LibraryIndex::Todo): in its
    /// document where it is open (one undo step there; saved when it had no other changes), else in its file in the
    /// background (a Markdown file through its text; a .xopp or a PDF with notes loaded, changed and saved as the app
    /// saves them). A file that cannot be changed (read-only, an archive PDF, ...) is not: a message says why. False if
    /// it was not done (or is not started).
    Q_INVOKABLE bool setTodoDone(const QString& path, const QString& rawText, int occurrence, bool done);
    /// Open a to-do's document at its page, with its line in view (`page`: where the index has it, -1: not known).
    Q_INVOKABLE bool openTodo(const QString& path, const QString& rawText, int occurrence, int page);
    /// "Add to calendar" of a to-do with a due date (a row of the To-dos view, LibraryTodosModel::get): on Android the
    /// calendar app's new event, filled in; else (or when none takes it) an .ics of it in the app's cache, opened with
    /// the system's app for it. One way (TodoCalendar.h).
    Q_INVOKABLE bool addTodoToCalendar(const QVariantMap& row);
    /// Export the open to-dos the To-dos view lists to `target`: an .ics (those with a due date, all-day events) or a
    /// Markdown list (.md, any other name: .md is added)
    Q_INVOKABLE bool exportTodos(const QUrl& target);
    /// The check-box stamp for a handwritten to-do is armed (TodoStamp.h): the next tap on a page places it
    Q_PROPERTY(bool todoStamp READ todoStampArmed NOTIFY todoStampChanged)
    bool todoStampArmed() const;
    /// Arm it (the hand tool meanwhile, so the tap writes nothing); after the stamp, the tool used before comes back
    /// (also on any tool chosen, or cancelTodoStamp)
    Q_INVOKABLE void startTodoStamp();
    Q_INVOKABLE void cancelTodoStamp();

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
    void openPaths(const QStringList& paths);
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
    std::optional<std::string> pictureLinkFor(const QString& arg);
    /// A page of the current document whose bookmark is a comment in its Markdown (a .md, a PDF text document;
    /// qt/docs/features/bookmarks.md, "Markdown"): add (+1), remove (-1) or rename (0) it as an edit of that text, one
    /// undo step as a typed one. Nothing (false, `handled` false) if the page is not such a page.
    bool editTextBookmark(int page, int change, const QString& label, bool& handled);
    /// A text file open in `s` was renamed or moved (`from` -> `to`, the library): its tab follows.
    void followTextFile(xqt::DocumentSession& s, const fs::path& from, const fs::path& to);
    /// The file a new document shows (an image written on, a file shown read-only) was renamed or moved: the tab and
    /// its pages' image backgrounds follow.
    void followShownFile(xqt::DocumentSession& s, const fs::path& from, const fs::path& to);
    /// The Markdown texts of every open document that show this picture: laid out again and drawn.
    void relayoutPictures(const std::string& link);
    void openReceived(const fs::path& folder, const std::vector<fs::path>& files, const QStringList& errors);

public:
    // --- version history (qt/docs/features/hybrid-pdf.md "Version history"; the model: `versions`) -------------------
    /// "Save with a message…" (Ctrl+Alt+S): a save that makes a milestone (a version with this message, never
    /// replaced). Like saveInBackground otherwise.
    Q_INVOKABLE bool saveWithMessage(const QString& message, const QJSValue& then = QJSValue());
    /// Give a version a message (an empty one: none) or change it; false and a message when it cannot.
    Q_INVOKABLE bool setVersionMessage(int id, const QString& message);
    /// Show version `id` beside the document, read-only (as its reference; the file of the version in VersionCache).
    Q_INVOKABLE bool viewVersion(int id);
    /// Open version `id` as a new document that is not saved yet (named after it).
    Q_INVOKABLE bool openVersionAsCopy(int id);
    /// "Compare with now": version `id` beside the document (as viewVersion), scrolled together, its changed pages
    /// marked (`compare`).
    Q_INVOKABLE bool compareWithNow(int id);
    /// "Compare two versions": the newer one shown read-only in a tab of its own, the older one beside it, as
    /// compareWithNow.
    Q_INVOKABLE bool compareVersions(int first, int second);
    QObject* compareObject() const;
    xqt::VersionCompare& versionCompare() const { return *compareMode; }
    bool viewingVersion() const;

    /// Show a file beside the current document, as its reference (opened as a tab if it is not open yet; an untouched
    /// new document stays, to write the notes in). Without a document open: opened as the document. The current
    /// document itself: a second view of it beside it.
    Q_INVOKABLE bool openAsReference(const QString& path);
    /// A document and a conflict copy of it (a sync app's, SyncConflicts.h) side by side: the document as the tab, the
    /// copy as its reference.
    Q_INVOKABLE bool compareConflict(const QString& document, const QString& copy);
    QObject* referenceObject() const;
    QObject* presenterObject() const;
    QObject* citationsObject() const;
    QObject* audioObject() const;
    QObject* timelineObject() const;
    xqt::ReferenceMode& reference() const { return *referenceMode; }
    /// Ctrl+S: while the reference has the keys and is written in, it is saved (true). When it needs a file first,
    /// its tab becomes the current one and false is returned (the window then asks for the file as for any
    /// document); false as well when the notes are meant.
    Q_INVOKABLE bool saveReferenceInHand();
    /// Close a tab without asking (QML asks about unsaved changes first). The last tab is replaced by a new one.
    Q_INVOKABLE void closeTab(int index);
    // --- renaming (AppRename.cpp, qt/docs/features/library.md "Renaming") --------------------------------------------
    /// What the name field of a tab's document shows: {name (without the extension), extension (it stays), note (what
    /// is renamed with it), unsaved (a new document: the name it is saved under), problem (read-only: why not)}.
    Q_INVOKABLE QVariantMap tabRenameInfo(int index) const;
    /// Why the tab's document cannot get `name` (without the extension): "" if it can (or it is its name).
    Q_INVOKABLE QString tabRenameProblem(int index, const QString& name) const;
    /// Rename the tab's document the way the library does (DocumentFiles::rename; the index, reading places, tabs,
    /// recent list and links follow). A new document without a file: its title and the name it is saved under. False
    /// (with a message) if it cannot.
    Q_INVOKABLE bool renameTab(int index, const QString& name);
    /// Why a card's document or folder (`path`) cannot get `name` as the library's rename takes it (a text or other
    /// file: the whole file name): "" if it can.
    Q_INVOKABLE QString renameProblem(const QString& path, const QString& name) const;
    static QString renameProblemText(int problem, const QString& name);
    Q_INVOKABLE void moveTab(int from, int to);
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();
    /// The open documents in the order they were used, the current one first (a phone's sheet of recent tabs)
    Q_INVOKABLE QList<int> tabsByUse() const;
    /// Back to the document used before the current one, like Alt+Tab (a double tap on a phone's tab count); from the
    /// home screen: the document behind it
    Q_INVOKABLE void previousUsedTab();
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
    // --- the document mode (qt/docs/features/hybrid-pdf.md, "PDF-only mode") ---
    QString documentMode() const;
    void setDocumentMode(const QString& mode);
    bool pdfOnly() const;
    /// The first start (of the main window) asks which way to work: nothing chosen yet, and XQT_DOCUMENT_MODE unset.
    Q_INVOKABLE bool askDocumentMode() const;
    // --- getting started (qt/docs/features/onboarding.md) ---
    bool introSeen() const;
    void setIntroSeen(bool seen);
    /// The first start shows the introduction, which ends in the document mode question: that question is due
    /// (askDocumentMode) and the introduction was not shown yet.
    Q_INVOKABLE bool askIntro() const;
    /// The tutorial (AppHelp.cpp): a copy to write on, a PDF text document in the app's data folder, made from the
    /// Markdown in the resources the first time and opened again with what was written on it afterwards.
    Q_INVOKABLE bool openTutorial();
    /// A fresh copy instead of the one there (its tab is closed first, without asking: the window asked).
    Q_INVOKABLE bool restartTutorial();
    bool tutorialExists() const;
    /// Where the copy is: <AppDataLocation>/Tutorial/Tutorial.pdf
    QString tutorialFile() const;
    /// The tutorial's Markdown in the resources
    static QString tutorialResource();
    /// Save as: the type the dialog starts on, "pdf" (PDF with notes) or "xopp". A hybrid PDF stays a PDF, a .xopp a
    /// .xopp; other documents (new ones, annotated PDFs, images) take the mode's: "pdf" in PDF files mode.
    Q_INVOKABLE QString saveFormat() const;
    /// The same, waiting until the file is written (tests): whether that worked.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    // Hybrid PDF (qt/docs/features/hybrid-pdf.md)
    bool isHybrid() const;
    bool protectedDocument() const;
    bool canProtect() const;
    /// The PDF waiting for its password (passwordNeeded): opened with this one. False: not opened (a wrong one asks
    /// again, passwordNeeded with `wrong`).
    Q_INVOKABLE bool openWithPassword(const QString& password);
    /// The password is not given: the PDF stays closed (the next one waiting is asked for).
    Q_INVOKABLE void cancelPassword();
    /// Why this protection cannot be written ("" when it can): for the dialog.
    Q_INVOKABLE QString checkProtection(const QString& password, const QString& ownerPassword, bool allowPrint,
                                        bool allowCopy, bool allowEdit) const;
    /// Protect the current document's PDF with a password (AES-256), or change it: unsaved changes are saved into
    /// it first, then the whole file is written anew encrypted (earlier versions go) and opened again. `ownerPassword`
    /// with restrictions only. False (and a message) when it could not be done.
    Q_INVOKABLE bool protectDocument(const QString& password, const QString& ownerPassword, bool allowPrint,
                                     bool allowCopy, bool allowEdit);
    /// Remove the password of the current document's PDF (written anew without encryption, opened again).
    Q_INVOKABLE bool removeProtection();
    /// Share → a PDF with notes protected with `password` (Share's "Protect with a password"): a copy of the document
    /// written into the app cache, encrypted (AES-256), then shown in the file manager or put on the clipboard. The
    /// document keeps its file and its own protection.
    Q_INVOKABLE bool sharePdfProtected(const QString& password, bool toClipboard);
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
    Q_INVOKABLE bool exportXopp(const QUrl& url);
    // --- Share (qt/docs/features/hybrid-pdf.md) ---
    /// What "Share → PDF with notes" does with the current document: "share" (its file as it is: a hybrid PDF without
    /// unsaved changes, a PDF without notes), "save" (saved first: a hybrid PDF with changes, notes that go into the
    /// PDF itself), "ask" (a .xopp: saved as a PDF with notes, or a PDF copy), "saveAs" (no file yet: Save as).
    Q_INVOKABLE QString shareStep() const;
    /// The steps "share" and "save": then the PDF goes to the system (SystemApps::share: the file manager on the
    /// desktop) or, `toClipboard`, onto the clipboard. False for the other steps (the window asks).
    /// A PDF with notes that keeps its versions (version history) is shared without them (a copy written anew in the
    /// app cache; the file keeps them), unless `withHistory`.
    Q_INVOKABLE bool sharePdf(bool toClipboard, bool withHistory = false);
    /// The PDF with notes that Share would send keeps its versions (`file`: a PDF of the library; "": the current
    /// document): Share then offers to send them along.
    Q_INVOKABLE bool sharedKeepsVersions(const QString& file) const;
    /// A PDF with notes as a copy at `target` (empty: in the app cache), the document keeps its file and format; then
    /// shared or copied.
    Q_INVOKABLE bool sharePdfCopy(const QUrl& target, bool toClipboard);
    /// "For Xournal++ (.xopp + PDF)": a one-time export into `folder`, never the document's own folder, as
    /// "name.xopp" + "name.xopp.bg.pdf" (upstream's attached PDF; a free name there), then shared. `file`: that PDF
    /// (a library card) instead of the current document.
    Q_INVOKABLE bool shareForXournal(const QUrl& folder, const QString& file = QString());
    /// A file as it is (a library card's PDF): shared or copied.
    /// One that keeps its versions: without them unless `withHistory` (see sharePdf).
    Q_INVOKABLE bool shareFile(const QString& path, bool toClipboard, bool withHistory = false);
    /// The text file Share… offers as it is: the current document's (a .md, a text file; "" if it is none), or for a
    /// library card's path the file itself if it is a Markdown or text file. Never a PDF with notes for those.
    Q_INVOKABLE QString sharedTextFile(const QString& path = QString()) const;
    /// Files onto the clipboard, to paste them into another app (SystemApps::copyToClipboard).
    Q_INVOKABLE bool copyToClipboard(const QStringList& files);
    /// The folder the "For Xournal++" dialog starts in: the one chosen last, else the documents folder.
    Q_INVOKABLE QUrl shareFolder() const;
    // --- Archive PDF (qt/docs/features/hybrid-pdf.md, "Archive PDF") ---
    /// "Export for the archive…": "name.archive.pdf" next to the current document, or next to `file` (a library card's
    /// PDF). Empty when the document has no file yet (the window then asks for a folder).
    Q_INVOKABLE QUrl suggestedArchiveFile(const QString& file = QString()) const;
    /// The archive PDF of the current document (or of `file`) in `folder`.
    Q_INVOKABLE QUrl archiveFileIn(const QUrl& folder, const QString& file = QString()) const;
    /// Write the archive PDF `target` in the background: from the current document as it is now (unsaved changes
    /// included; it keeps its file and state), or from `file`, loaded on a worker without opening a tab. Then
    /// archiveExported, or a message if it failed.
    Q_INVOKABLE bool exportArchive(const QUrl& target, const QString& file = QString());
    /// "Export library as archive…" (LibraryArchive: running, done, total, current; finished(summary)).
    Q_PROPERTY(QObject* libraryArchive READ libraryArchiveObject CONSTANT)
    QObject* libraryArchiveObject() const;
    /// Every document of the library (or of the current folder, with its subfolders) as an archive PDF in a new
    /// folder inside `into`, other files copied; in the background. False (and a message) if it cannot start, e.g.
    /// `into` is inside the library.
    Q_INVOKABLE bool exportLibraryArchive(const QUrl& into, bool currentFolderOnly);
    Q_INVOKABLE void cancelLibraryArchive();
    // --- Sharing a folder or the library as a zip (qt/docs/features/library.md, "Sharing a folder or the library") ---
    /// "Share folder…" / "Share library…" (LibraryShare: running, done, total, current, survey, passwordAvailable;
    /// finished(summary)).
    Q_PROPERTY(QObject* libraryShare READ libraryShareObject CONSTANT)
    QObject* libraryShareObject() const;
    /// Look at what the folder (relative to the library; "": the whole library) holds, for the dialog (survey).
    Q_INVOKABLE void surveyShare(const QString& folder);
    /// Share the folder (relative; "": the whole library) as a zip in the background. `options`: "format" ("app",
    /// "xournal", "pdf"), "readings", "pdfText", "history", "recordings" (bools), "password". False (and a message)
    /// if it cannot start.
    Q_INVOKABLE bool shareAsZip(const QString& folder, const QVariantMap& options);
    Q_INVOKABLE void cancelShareZip();
    /// The written zip to the system (the file manager on the desktop), or a copy of it into `folder` (" (2)" when the
    /// name is taken there). False (and a message) if that did not work.
    Q_INVOKABLE bool handOverZip(const QString& zip);
    Q_INVOKABLE bool saveZipCopy(const QString& zip, const QUrl& folder);
    /// "Open in library…" for a zip (opened with the app, from the file dialog, dropped): zipOpened(path) asks where.
    /// (LibraryUnzip: running, done, total, current; finished(result).)
    Q_PROPERTY(QObject* libraryUnzip READ libraryUnzipObject CONSTANT)
    QObject* libraryUnzipObject() const;
    /// What a zip holds: "ok", "error", "files", "bytes", "encrypted", "supported", "share", "name".
    Q_INVOKABLE QVariantMap inspectZip(const QString& zip) const;
    /// Unpack it into the library's folder `folder` (relative; made if needed: "Inbox" by default) in the background;
    /// the library then shows the new folder. False (and a message) if it cannot start.
    Q_INVOKABLE bool unzipIntoLibrary(const QString& zip, const QString& folder, const QString& password);
    Q_INVOKABLE void cancelUnzip();
    /// After hybridEditedElsewhere: take the other app's version of the changed annotations (or keep ours).
    Q_INVOKABLE bool importHybridChanges();
    Q_INVOKABLE void keepHybridData();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// The fits also turn the canvas upright again (qt/docs/features/canvas-rotation.md; the width: app.keyTarget)
    /// The height of the current page fills the view, or the whole page fits into it.
    Q_INVOKABLE void fitHeight();
    Q_INVOKABLE void fitPage();
    double canvasRotation() const;
    bool canRotateCanvas() const;
    /// Turn the canvas by a step (Ctrl+] clockwise: 90, Ctrl+[: -90; from a free angle to the next quarter)
    Q_INVOKABLE void rotateCanvas(double degrees);
    /// Upright again (the chip in the layout pill)
    Q_INVOKABLE void resetCanvasRotation();
    /// The current page has another size than the page before or after it (then fitting it alone helps).
    Q_PROPERTY(bool currentPageDiffers READ currentPageDiffers NOTIFY pageChanged)
    bool currentPageDiffers() const;
    /// Zoom to this (around the middle of the view), e.g. back to what it was. (In and out, 100 %: app.keyTarget)
    Q_INVOKABLE void setZoomPercent(int percent);
    Q_INVOKABLE void addPageAfterCurrent();

    // --- pages of the current document (index: 0-based page) ---
    Q_INVOKABLE void goToPage(int index);
    /// Go to a page and remember the place for "back" (links, page grid, sidebar).
    Q_INVOKABLE void jumpToPage(int index);
    /// The same, showing this part of the page (page points): an item of the Annotations panel.
    Q_INVOKABLE void jumpToPlace(int index, const QRectF& rect);
    // --- annotations of other apps made editable (AppAdopt.cpp; qt/docs/features/adopt-annotations.md) ---
    /// Make the annotations of other apps in the current document editable (converted on a worker, then taken by the
    /// document as one undo step); the snackbar says how many.
    Q_INVOKABLE void adoptAnnotations();
    /// "Not now" to the offer: it is not made again for this file until the file has more of them.
    Q_INVOKABLE void declineAdoption();
    int adoptableCount() const;
    QString adoptableApp() const;
    bool adopting() const { return adoptRunning; }
    // --- the annotations as Markdown (qt/docs/features/annotations-md.md) ---
    /// Where "Export as Markdown" writes without asking: "<name>.annotations.md" next to the document, with Xournal++
    /// files. Empty: ask with a save dialog (PDF files mode writes nothing next to files), or the document was never
    /// saved.
    Q_INVOKABLE QUrl annotationsFile() const;
    /// The file the save dialog suggests; empty: the document was never saved (exportAnnotations says so).
    Q_INVOKABLE QUrl suggestedAnnotationsFile() const;
    Q_INVOKABLE bool fileExists(const QUrl& file) const;
    /// Write the current document's annotations as Markdown (once the panel's list is up to date); then
    /// annotationsExported.
    Q_INVOKABLE void exportAnnotations(const QUrl& file);
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    Q_INVOKABLE void clearNavigation();
    /// The files of the documents shown (the tab's, the reference's): the paper of a reference is not looked for in
    /// the document the reference is in.
    Q_INVOKABLE QStringList shownDocumentFiles() const;
    /// (PDF text: selecting, its ends, copying and marking it: app.edit)
    /// Insert `count` new pages before `position` (0-based; page count: at the end): background `background` (index
    /// in the settings' pageBackgrounds), paper `paper` (index in paperFormats; -1: the size of the current page),
    /// portrait or landscape. One step to undo.
    /// `paperColor`, `textured` (-1: the settings' for new pages): the paper's color and texture (qt/docs/features/dark-pages.md)
    Q_INVOKABLE bool insertPages(int position, int background, int paper, bool landscape, int count = 1,
                                 const QColor& paperColor = QColor(), int textured = -1);
    /// Give these pages another background (index in the settings' pageBackgrounds), on paper of this color and
    /// texture (invalid, -1: the settings' for new pages); one undo step.
    Q_INVOKABLE bool changePageBackground(const QList<int>& pages, int background, const QColor& paperColor = QColor(),
                                          int textured = -1);
    /// One of the pages shows a page of the PDF (changing the background takes that away).
    Q_INVOKABLE bool pagesHavePdfBackground(const QList<int>& pages) const;
    /// The current page: { background (index in pageBackgrounds, -1: other), landscape, paper (its color; white for a
    /// page of a PDF), textured, pdf (a page of the PDF: no paper of its own) }
    Q_INVOKABLE QVariantMap currentPageFormat() const;
    /// Ask the window for the "insert pages" dialog (page menu); position as for insertPages.
    Q_INVOKABLE void requestInsertPages(int position) { Q_EMIT insertPagesRequested(position); }
    /// Ask the window for the background dialog for these pages (0-based).
    Q_INVOKABLE void requestPageBackground(const QList<int>& pages) { Q_EMIT pageBackgroundRequested(pages); }
    /// Space for notes beside slides (qt/docs/features/note-space.md). Ask the window for its dialog, for these pages
    /// (0-based; empty: the current page, `allPages`: offer all pages first).
    Q_INVOKABLE void requestNoteSpace(const QList<int>& pages, bool allPages = false) {
        Q_EMIT noteSpaceRequested(pages, allPages);
    }
    /// A page's space for notes and its slide: { left, top, right, bottom, slideWidth, slideHeight (points), pdf,
    /// possible (not an image background) }.
    Q_INVOKABLE QVariantMap noteSpaceOf(int page) const;
    /// The pages a choice of the dialog means: 0 these pages, 1 all pages, 2 all pages with a PDF background.
    Q_INVOKABLE QList<int> noteSpacePages(int scope, const QList<int>& pages) const;
    /// Give the pages of `scope` this space for notes: points, or (`relative`) fractions of the slide's width (left,
    /// right) and height (top, bottom). Replaces what they had; all 0 removes it. One undo step. Returns how many
    /// pages changed.
    Q_INVOKABLE int applyNoteSpace(int scope, const QList<int>& pages, double left, double top, double right,
                                   double bottom, bool relative);
    /// A blank page (the size of the slide) after each page of `scope` instead. One undo step. Returns how many.
    Q_INVOKABLE int insertBlankAfterPages(int scope, const QList<int>& pages);
    /// Changing the size of pages (qt/src/canvas/PageResize.h). Ask the window for its dialog, for these pages
    /// (0-based; empty: the current page; several: the selection).
    Q_INVOKABLE void requestPageSize(const QList<int>& pages) { Q_EMIT pageSizeRequested(pages); }
    /// A page's size: { width, height (points), paper (index in paperFormats, either way round; -1: none), landscape,
    /// text ("74 × 105 mm", portrait), pdf, possible (not in a text file) }.
    Q_INVOKABLE QVariantMap pageSizeOf(int page) const;
    /// All pages (0-based), for the dialog's "all pages".
    Q_INVOKABLE QList<int> allPages() const;
    /// What giving these pages width × height (points) would do: { pages (that change), pdfPages (left out: a PDF
    /// background), outside (elements that would reach beyond the smaller page) }.
    Q_INVOKABLE QVariantMap pageSizePreview(const QList<int>& pages, double width, double height) const;
    /// Give these pages width × height (points): the content stays where it is, PDF pages keep theirs, the page's
    /// text flows anew. One undo step. Returns how many pages changed.
    Q_INVOKABLE int applyPageSize(const QList<int>& pages, double width, double height);
    /// Turning pages a quarter to the left or right (qt/src/canvas/PageRotate.h, qt/docs/features/page-rotation.md).
    /// What it would do to these pages (0-based): { possible (some turn), pages (that turn), leftOut (PDF pages that
    /// stay), reason (why not, or why some stay; "": nothing to say) }. PDF pages turn only in a PDF with notes (or a
    /// document that will be saved as one); in a .xopp they stay as they are.
    Q_INVOKABLE QVariantMap rotationOf(const QList<int>& pages) const;
    /// Turn these pages (0-based) a quarter to the right (or left): one undo step. Returns how many turned; the window
    /// says what was done (pageActionDone) or why not (message).
    Q_INVOKABLE int rotatePages(const QList<int>& pages, bool right);
    /// Ask the window for the "new chapter" dialog on that page.
    Q_INVOKABLE void requestChapter(int page) { Q_EMIT chapterRequested(page); }
    /// Write a chapter heading on a page (level 0-2): the contents sidebar and overview show it. Undoable.
    Q_INVOKABLE bool addChapter(int page, const QString& title, int level);
    /// "Copy link" (qt/docs/features/links.md): a link to a page of the current document (-1: the current page) onto
    /// the clipboard, as the app's own format, Markdown and HTML (links::toMime). Pasted into a Markdown text it
    /// becomes `[title](link)` relative to that document, on a page a link marker. A document without a file yet:
    /// "#Page:12", a link within it, as before.
    Q_INVOKABLE void copyPageLink(int page);
    /// The same for a chapter of the contents (its title, its page).
    Q_INVOKABLE void copyChapterLink(int page, const QString& title);
    /// The same for a document of the library (a card, a search hit), or a page of it (0-based; -1: the document).
    Q_INVOKABLE bool copyDocumentLink(const QString& path, int page = -1);
    /// The clipboard holds a link (Copy link): its Markdown for the Markdown text being written beside the page
    /// (relative to the current document), else "".
    Q_INVOKABLE QString clipboardLinkMarkdown() const;
    /// Paste into the Markdown text being written beside the page (its TextArea's document), in place of
    /// [from, to): text whose \(…\) and \[…\] (as chat apps write formulas) become $…$ and $$…$$ where they are
    /// formulas there (md::tex::convertPasted). One undo step. False when there is nothing to convert: the TextArea
    /// pastes as always.
    Q_INVOKABLE bool pasteMarkdown(QQuickTextDocument* document, int from, int to);
    /// Ask the window for the print dialog, with these pages (0-based; empty: the whole document).
    Q_INVOKABLE void requestPrint(const QList<int>& pages) { Q_EMIT printRequested(pages); }
    Q_INVOKABLE void insertPageBefore(int index);
    Q_INVOKABLE void insertPageAfter(int index);
    Q_INVOKABLE void duplicatePage(int index);
    Q_INVOKABLE void deletePage(int index);
    Q_INVOKABLE void movePageUp(int index);

    // --- tools (shared by all tabs) ---
    /// "pen", "highlighter", "eraser", "hand"
    Q_INVOKABLE void selectTool(const QString& tool);
    /// The snip tool ("rect", "lasso"): one snip, then the tool in hand now comes back (also on any tool chosen, or
    /// cancelSnip). The picture goes to the clipboard with where it came from (AppSnip.cpp); "Copied picture" says so.
    Q_INVOKABLE void startSnip(const QString& shape);
    Q_INVOKABLE void cancelSnip();
    QString snipShape() const;
    QString snipResolution() const;
    void setSnipResolution(const QString& resolution);
    /// The link offered for a pasted snip (snipLinkOffered): a link to its source page next to the picture
    Q_INVOKABLE bool addSnipLink();
    /// Copy handwriting as text (AppInkCopy.cpp, hwr/InkCopy.h): arm the tool (one sweep, then the tool in hand now
    /// comes back, as a snip), or copy the selection's handwriting. The words' readings go to the clipboard in reading
    /// order; lines not read yet are read first (the user waits: InkRecognitionService's urgent job). inkTextCopy says
    /// how it went (also when the handwriting search is off or has no model). False: nothing to do.
    Q_INVOKABLE bool startInkCopy();
    Q_INVOKABLE bool copySelectionAsText();
    bool inkCopyArmed() const;
    bool selectionHasInk() const;

    // --- stickers (AppStickers.cpp, qt/docs/features/stickers.md) ---
    /// The picker's list (StickersModel): the library's Stickers folder or the app-wide set
    Q_PROPERTY(QObject* stickers READ stickersModel CONSTANT)
    QObject* stickersModel() const;
    /// What a sticker of the selection would be: { offered: something is selected, name: the name suggested,
    /// picture: a picture of the page behind it can go with it (a PDF page or a picture there), folders: of the set }
    Q_INVOKABLE QVariantMap stickerDraft(bool appWide = false) const;
    /// Save what is selected as a sticker named `name` in `folder` (relative to the set; "": its root; a new name
    /// makes it) of the library's set or (`appWide`) the app-wide one, with the picture of the page behind it
    /// (`withPicture`). Written off the UI thread: then it is on the clipboard too, "Saved sticker" says so
    /// (stickerSaved). False: nothing selected, or a sticker is being written.
    Q_INVOKABLE bool saveSticker(const QString& name, const QString& folder, bool withPicture, bool appWide);
    /// Paste a sticker (its file): read off the UI thread, then on the clipboard and pasted on the current page,
    /// selected (stickerPasted). False: no document to paste into, read-only, or a sticker is being read.
    Q_INVOKABLE bool pasteSticker(const QString& path);
    /// Stickers can be pasted into the current document (a page of notes, not read-only)
    Q_PROPERTY(bool canPasteSticker READ canPasteSticker NOTIFY selectionChanged)
    bool canPasteSticker() const;

    // --- page templates (AppTemplates.cpp, qt/docs/features/templates.md) ---
    /// The template picker's list (StickersModel of templates): the library's Templates folder or the app-wide set
    Q_PROPERTY(QObject* templates READ templatesModel CONSTANT)
    QObject* templatesModel() const;
    /// What a template of page `page` would be: { offered, name: the name suggested, background: what its background
    /// is ("pdf", "image", "paper"), folders: of the set, hasLibrary }
    Q_INVOKABLE QVariantMap templateDraft(int page, bool appWide = false) const;
    /// Save page `page` as a template named `name` in `folder` (relative to the set; a new name makes it) of the
    /// library's set or (`appWide`) the app-wide one: with its background (a PDF page: that PDF page) and with its
    /// content as asked. Written off the UI thread (templateSaved). False: no such page, nothing to save.
    Q_INVOKABLE bool saveTemplate(int page, const QString& name, const QString& folder, bool withBackground,
                                  bool withContent, bool appWide);
    /// Add a template's page `count` times before page `position` (-1: after the current page), as pasting a copy
    /// of that page does: one undo step. Read off the UI thread (templateInserted). False: no document to add to.
    Q_INVOKABLE bool insertTemplate(const QString& path, int position = -1, int count = 1);
    /// Pages can be added from a template to the current document (not a text file's pages, not read-only)
    Q_PROPERTY(bool canInsertTemplate READ canInsertTemplate NOTIFY selectionChanged)
    bool canInsertTemplate() const;
    /// A new document that starts with a template's page (else as createDocument); read off the UI thread, then
    /// created (templateInserted)
    Q_INVOKABLE bool createDocumentFromTemplate(const QString& name, bool inLibrary, const QString& path);
    // --- pages as files (AppPageFiles.cpp, qt/docs/features/page-files.md) ---
    /// Insert pages from a file (A6): read `file` (a PDF, a PDF with notes, a .xopp) off the UI thread and keep it
    /// until insertPagesFromFile or closePageFile; pageFileRead says what it is: { ok, name, pages, thumbnails (the URL
    /// of its pages' pictures, "/<page>" appended; "" for a protected file), protectedFile, needsPassword,
    /// wrongPassword, error }. `password`: for a protected PDF (asked when needsPassword). False: nothing to insert into.
    Q_INVOKABLE bool readPageFile(const QUrl& file, const QString& password = QString());
    /// Why a range of pages of a document of `count` pages is not one ("": it is; "1-3, 5", "8-").
    Q_INVOKABLE QString checkPageRange(const QString& range, int count) const;
    /// Insert pages of the file read before page `position` of the current document: `picked` (0-based) when not
    /// empty, else the pages of `range` ("": all). As pasting copied pages: one undo step, PDF pages into the
    /// document's merged PDF (their text stays searchable). Copied off the UI thread (pagesFromFileInserted); the
    /// file is let go then.
    Q_INVOKABLE bool insertPagesFromFile(const QString& range, const QList<int>& picked, int position);
    /// Let go of the file read (the dialog was closed)
    Q_INVOKABLE void closePageFile();
    /// Extract or split (A7): where and as what pages of the current document would go: { offered, name (suggested,
    /// for `pages`), folder (shown), asPdf (the default type), xoppAllowed (not for a protected document), count,
    /// range ("3-5") }
    Q_INVOKABLE QVariantMap extractDraft(const QList<int>& pages) const;
    /// Write `pages` as a new document `name` (a PDF with notes, or a .xopp with its PDF) next to the current one (or
    /// in the library), off the UI thread, then open it in a tab; with `remove` they leave this document (one undo
    /// step). pagesExtracted says where, or why not. A protected document gives a PDF protected the same way.
    Q_INVOKABLE bool extractPages(const QList<int>& pages, const QString& name, bool asPdf, bool remove);
    /// How the current document would be split: `mode` "every" (`every` pages each), "selected" (a part starts at
    /// each of `pages`) or "chapters" (at each chapter of its table of contents): { parts: [{ name, range, count }],
    /// error }
    Q_INVOKABLE QVariantMap splitPlan(const QString& mode, int every, const QList<int>& pages) const;
    /// Write those parts as documents next to it (the document stays as it is), off the UI thread; pagesExtracted.
    Q_INVOKABLE bool splitDocument(const QString& mode, int every, const QList<int>& pages, bool asPdf);
    /// Export pages as pictures (A8): { offered, refused (why not: a protected document), name, folder (the last one
    /// used, else the pictures folder), dpi (the last one used, else 200) }
    Q_INVOKABLE QVariantMap imageExportDraft() const;
    /// Write `pages` (empty: the current page) as "name-p003.png" (or ".jpg" for `format` "jpg") into `folder` at
    /// `dpi`, in the normal colours; `transparent`: no paper (colour and ruling) behind the ink, PNG only. Off the UI
    /// thread (pageImagesExported).
    Q_INVOKABLE bool exportPageImages(const QList<int>& pages, const QUrl& folder, int dpi, bool transparent,
                                      const QString& format);
    /// The resolution of pages as pictures (remembered, 300 at first): exported, and copied as an image
    Q_PROPERTY(int pageImageDpi READ pageImageDpi WRITE setPageImageDpi NOTIFY pageImageDpiChanged)
    int pageImageDpi() const;
    void setPageImageDpi(int dpi);
    /// "Copy page as image" (Ctrl+Shift+C): the first of `pages` (empty: the current page) as a PNG on the clipboard
    /// at pageImageDpi (less for a page so large it would be more than about 32 megapixels), on white paper, drawn off
    /// the UI thread; a toast says its size (pageImageCopied).
    Q_INVOKABLE bool copyPagesAsImage(const QList<int>& pages);
    /// Put the setsquare ("setsquare") or the compass ("compass") on the page, or take it away again.
    Q_INVOKABLE void toggleGeometryTool(const QString& which);
    /// The curtain over part of the page, or the spotlight (all black but a rectangle; qt/docs/features/curtain.md;
    /// this tab's, only on the screen): "curtain" / "spotlight" puts it out (instead of the other one; the same again
    /// takes it away), "" takes it away.
    Q_INVOKABLE void toggleCurtain(const QString& which);
    /// Which one is out ("" if none)
    Q_PROPERTY(QString curtain READ curtain NOTIFY curtainChanged)
    QString curtain() const;
    /// Its handles are shown (a tap on the black shows them, Esc hides them)
    Q_PROPERTY(bool curtainHandles READ curtainHandles WRITE setCurtainHandles NOTIFY curtainChanged)
    bool curtainHandles() const;
    void setCurtainHandles(bool shown);
    // --- sticky notes (qt/docs/features/sticky-notes.md) ---
    /// A new sticky note in the middle of the visible part of the current page, selected so that it can be moved
    /// and resized right away (a select tool is chosen, as for an image). One undo step.
    Q_INVOKABLE bool insertStickyNote(const QColor& color = QColor());
    /// The pastel colors a note can have
    Q_PROPERTY(QVariantList stickyNoteColors READ stickyNoteColors CONSTANT)
    QVariantList stickyNoteColors() const;
    /// Several notes are selected, or notes with elements of the page (the selection's pill;
    /// qt/docs/features/sticky-notes.md, "Several notes at once")
    Q_PROPERTY(bool notesSelectedTogether READ notesSelectedTogether NOTIFY selectionChanged)
    bool notesSelectedTogether() const;
    /// Ctrl+V in the page sidebar or grid pastes the copied note rather than the copied pages: a note is on the
    /// clipboard and it was copied after the pages (or a note is selected, or no pages are copied)
    Q_INVOKABLE bool pastesNoteBeforePages() const;
    /// The current page has sticky notes; they are hidden (a view state, not saved)
    Q_PROPERTY(bool pageHasNotes READ pageHasNotes NOTIFY notesChanged)
    bool pageHasNotes() const;
    Q_PROPERTY(bool pageNotesHidden READ pageNotesHidden WRITE setPageNotesHidden NOTIFY notesChanged)
    bool pageNotesHidden() const;
    void setPageNotesHidden(bool hidden);

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

    // --- links between documents (qt/docs/features/links.md; AppLinks.cpp) ---
    /// About a tapped link (a Markdown link target, "[[a wiki link]]"): { document: it leads to a document, name: the
    /// file's name ("" for this document), place: "page 12", "chapter …", found: the file is there, here: it is
    /// this document }.
    Q_INVOKABLE QVariantMap documentLink(const QString& uri) const;
    /// What the status line shows for the link under the mouse or the hovering pen (qt/docs/features/links.md, "Links
    /// with the mouse"), in the document of `view` (a CanvasView: the tab's or the reference): a web address in full; a
    /// page of that document as "Page 12 · its chapter"; a link to another document as its file name and the place ("…,
    /// page 3"), or "name (not found)". `page` / `pdfPage` as CanvasView::LinkTarget has them.
    Q_INVOKABLE QString linkPreview(QObject* view, const QString& uri, int page, int pdfPage) const;
    /// Follow a link to a document from the current one: "tab" (switches to it when it is open), "reference" (beside
    /// the current document) or "here" (in place of the current document, which closes when it has no unsaved
    /// changes; Back opens it again). A place in the current document itself: "reference" shows it in a second view
    /// of the document beside it (qt/self-reference), anything else goes there. The place is looked up (DocumentLinks::placeIn); what was not found is said.
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
    void shutdown();

    /// The settings, tools and rendering, shared by all windows of the process (AppServices::context)
    xqt::AppContext& context() const;
    /// What the window's feature objects get from it (its services, tabs and current document); made after the tabs
    xqt::WindowContext windowContext() const;
    xqt::TabManager& tabManager() const { return *tabs; }
    /// The current tab's document and view, with their signals (what follows "the current document" connects here)
    xqt::CurrentDocument& currentDocument() const { return *current; }

    // --- windows (undocked documents) ---
    /// What the windows of the process share (the window factory, start maximized, the open documents of all windows)
    xqt::AppServices& services() const { return *appServices; }
    /// The controller of the main window (this one is a window of its own if it has one).
    AppController* mainWindow() const { return primary; }
    /// The windows of undocked documents (of the main window; none for another window).
    std::vector<AppController*> documentWindows() const;
    bool isSecondary() const { return primary != nullptr; }
    /// Windows open maximized (AppServices::setStartMaximized)
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
    void handwritingLanguageChanged();
    void homeVisibleChanged();
    void titleChanged();
    void modifiedChanged();
    void savingChanged();
    void anySavingChanged();
    void undoRedoChanged();
    void toolChanged();
    /// The curtain came or went, or its handles did (also: another tab)
    void curtainChanged();
    void zoomChanged();
    void canvasRotationChanged();
    void pageChanged();
    /// Sticky notes came, went, were hidden or shown (on the current page, or it is another page now)
    void notesChanged();
    /// Messages from the core (XojMsgBox) and file errors, shown by QML.
    void message(const QString& title, const QString& text, bool error);
    /// This folder can be a library only with "All files access": the window explains why it is asked for, then
    /// calls requestStorageAccess.
    void storageAccessNeeded(const QString& folder);
    void storageAccessChanged();
    /// "All files access" was just given for opening a folder as library: show the folder picker again.
    void pickLibraryFolder();
    /// The libraries' home changed (librariesInApp, offerLibrariesHome).
    void librariesHomeChanged();
    /// The window should come to the front (e.g. another instance handed over files).
    void raiseRequested();
    void recoveryChanged();
    void documentModeChanged();
    /// The tutorial's copy was made or removed (tutorialExists)
    void tutorialChanged();
    /// exportAnnotations is done: the file written, or why not (`error`).
    void annotationsExported(const QString& file, const QString& error);
    void searchChanged();
    void searchFuzzyChanged();
    void searchOptionsChanged();
    void viewLayoutChanged();
    void presentingChanged();
    void pageUndoChanged();
    void selectionChanged();
    void snipChanged();
    void snipResolutionChanged();
    /// Copying handwriting as text (startInkCopy, copySelectionAsText): `result.state` is "reading" (the words are
    /// being read), "copied" (`text` is on the clipboard; `html` the same with the unsure words marked, `words`,
    /// `unsure`, `partial`: lines left out, no model), "nothing" (no handwriting there), "off" (the handwriting search
    /// is off) or "noModel"; `x`, `y`, `width`, `height`: where on the canvas (its item's coordinates), `reference`:
    /// on the reference's canvas.
    void inkTextCopy(const QVariantMap& result);
    /// A sticker was written (`path`) and is on the clipboard; or (`error` not empty) it could not be
    void stickerSaved(const QString& path, const QString& error);
    /// A sticker was pasted (or `error`)
    void stickerPasted(const QString& path, const QString& error);

    void todoStampChanged();
    /// A template was written (`path`); or (`error` not empty) it could not be
    void templateSaved(const QString& path, const QString& error);
    /// A template's page was added (`pages` of them), or a document made from it; or (`error`) not
    void templateInserted(const QString& path, int pages, const QString& error);
    /// readPageFile: the file read, or why not (see there)
    void pageFileRead(const QVariantMap& info);
    /// insertPagesFromFile: `pages` inserted, or why not
    void pagesFromFileInserted(int pages, const QString& error);
    /// extractPages, splitDocument: the documents written, or why not
    void pagesExtracted(const QStringList& files, const QString& error);
    /// exportPageImages: the pictures written, or why not
    void pageImagesExported(const QStringList& files, const QString& error);
    /// copyPagesAsImage (and an export of one page): the picture of page `page` on the clipboard, `size` pixels at
    /// `dpi`; or why not
    void pageImageCopied(int page, const QSize& size, int dpi, const QString& error);
    void pageImageDpiChanged();
    /// A snip from a document with a file was pasted: the window offers to add a link to its page (addSnipLink)
    void snipLinkOffered(const QString& title);
    void fontChanged();
    void darkPagesChanged();
    void navigationChanged();
    void pdfTextModeChanged();
    /// The keys act on another canvas (the reference took the focus, or gave it back)
    void keyTargetChanged();
    void titlePageChanged();
    void bookmarksChanged();
    void favouriteChanged();
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
    void colorPaletteChanged();
    void insertPagesRequested(int position);
    void pageBackgroundRequested(const QList<int>& pages);
    void noteSpaceRequested(const QList<int>& pages, bool allPages);
    void pageSizeRequested(const QList<int>& pages);
    void printRequested(const QList<int>& pages);
    void chapterRequested(int page);
    void toolbarHiddenChanged();
    void markdownChanged();
    void markdownOnPageChanged();
    void markdownFormatChanged();
    /// The text tool tapped a Markdown box: the window opens its editor.
    void markdownRequested(int page);
    /// The text tool tapped a Markdown text box, or a place for a new one: the window opens its editor.
    void markdownBoxRequested(int page, double x, double y);
    /// The hybrid PDF just opened was edited in another app: its ink differs from the Xournal data.
    void hybridEditedElsewhere(const QString& file);
    /// A PDF needs a password to be opened (`file`: its name; `wrong`: the one given was not right): the window asks
    /// (openWithPassword, cancelPassword).
    void passwordNeeded(const QString& file, bool wrong);
    /// Files were exported for Xournal++ and shown (`text` says where): the window offers to copy them.
    void sharedForXournal(const QStringList& files, const QString& text);
    /// An archive PDF was written: whether it is PDF/A-3b, else why not; what was changed in its source PDF.
    void archiveExported(const QString& path, bool pdfa, const QStringList& notPdfA, const QStringList& adjusted);
    /// A zip was opened (the app started with it, the file dialog, a drop): the window asks where in the library it
    /// goes ("Open in library…").
    void zipOpened(const QString& path);
    /// A page operation happened (e.g. "3 pages deleted"); the UI offers to undo it.
    void pageActionDone(const QString& text, bool undoable);
    void adoptableChanged();
    /// A document was opened whose PDF has annotations of other apps that can be made editable (asked once per file)
    void annotationsToAdopt(int count, const QString& app, const QString& file);
    /// A web picture's "Load image" was tapped: the window shows the address (and what networking means, when
    /// `access` is "ask") before loadWebImage fetches it.
    void webImageRequested(const QString& url, const QString& host, const QString& access);
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
    /// The process's shared parts: main()'s, or this window's own (made without them, as the tests do). First of the
    /// members: it goes last.
    std::unique_ptr<xqt::AppServices> ownServices;
    xqt::AppServices* appServices = nullptr;
    /// The window's own parts, for either constructor
    void setUp(xqt::AppServices& services);
    /// Dark pages: the roles' dark colors for the canvas, and darkPagesChanged when the setting (any window's) or the
    /// system's colors change (AppPaper.cpp)
    void setUpDarkPages();
    /// A new document's paper: the ink in hand readable on it (qt/docs/features/dark-pages.md, "Ink on dark paper")
    void inkForPaper(const QColor& paper);
    /// The pattern `background` (index in the page types) on this paper (invalid, -1: the settings' for new pages)
    PageType paperTypeOf(int background, const QColor& paperColor, int textured, Color& color) const;
    /// The search's options in effect (the replace row's while it is shown, else none)
    xqt::textmatch::Options searchOptions() const;
    /// The current search again with the options in effect
    void applySearchOptions();
    bool replaceRow = false;
    xqt::textmatch::Options replaceOptions;
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
    /// What the keys act on: the reference's canvas while it has the focus, else the current document's
    xqt::CanvasActions* keyActions() const;
    /// What undo and redo act on: the Markdown being written in the canvas with the keys (its own steps first;
    /// nullptr: none), and the document with the keys (the reference while it is written in, else the tab's).
    xqt::MarkdownEditor* undoneMarkdown() const;
    xqt::DocumentSession* undoneSession() const;
    bool savesWithoutDialog(const xqt::DocumentSession* s) const;
    /// ExportHybrid: a hybrid PDF copy (to share); ShareXopp: the export for Xournal++ with an attached PDF.
    enum class SaveWay { Save, SaveAs, Hybrid, ExportXopp, ExportHybrid, ShareXopp };
    /// Hand files to the system (share), or put them on the clipboard.
    bool handOver(const QStringList& files, bool toClipboard);
    fs::path lastShareFolder;
    std::unique_ptr<xqt::LibraryArchive> libraryArchiveTask;
    std::unique_ptr<xqt::LibraryShare> libraryShareTask;
    std::unique_ptr<xqt::LibraryUnzip> libraryUnzipTask;
    std::unique_ptr<xqt::LibraryMove> libraryMoveTask;
    /// The library shown when the move began, let go while it copies (its index would write into it)
    fs::path libraryBeforeMove;
    bool libraryLetGo = false;
    void startLibrariesMove();
    void librariesCopied(bool ok, const QString& error, const xqt::LibraryMigration::Plan& plan);
    void librariesCleanedUp(const xqt::LibraryMigration::Plan& plan, const xqt::LibraryMigration::Cleanup& result,
                            bool report);
    /// Paths kept by the controller follow moved libraries: recent files, the library of the settings, the last
    /// folders of the file dialogs, open tabs.
    void followMovedLibraries(const std::vector<std::pair<fs::path, fs::path>>& moves);
    void setLibrariesMoved();
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
    std::vector<QJSValue> whenAllSavedCalls;
    /// The text tool of the current tab (and of the reference) makes Markdown text boxes of markdownFontSize.
    void applyMarkdownText();
    // --- the snip tool (AppSnip.cpp) ---
    /// A view of `s` drew a snip's picture: onto the clipboard, the tool used before back
    void snipped(xqt::DocumentSession& s, const QImage& image, int page, const QRectF& area, bool capped);
    /// The setting "snipResolution" to the snip tool (Snip.h)
    void applySnipResolution();
    /// The snip ends: disarmed, and (`restore`) the tool used before back
    void endSnip(bool restore);
    /// Follow the snip tool: armed, the tool changing to another one ends it
    void followSnipTool();
    QString snipPreviousTool;           ///< the tool before the snip ("": none)
    QString stampPreviousTool;          ///< the tool before the check-box stamp ("": none)
    /// The stamp ends (`restore`: the tool before it back)
    void endTodoStamp(bool restore);
    /// Another tool chosen while the stamp is armed: it ends, that tool stays
    void followTodoStampTool();
    ToolType snipTool = TOOL_NONE;      ///< the select tool the snip uses
    QPointer<xqt::CanvasView> snipLinkView;  ///< the view a snip with a link was pasted into
    /// Arm the snip ("rect", "lasso") for a picture or for the handwriting as text
    void armSnip(bool lasso, int purpose);
    // --- copy handwriting as text (AppInkCopy.cpp) ---
    /// The tool swept over a page of a view
    void inkSwept(xqt::CanvasView* v, int page, const QPolygonF& path);
    /// Read the handwriting of these strokes (the page's: only the lines meeting `area`; a selection's: all) and copy
    /// the words `path` takes (empty: all of them); `box`: where it happens on the canvas (page points of `page`)
    void copyInkText(xqt::CanvasView* v, int page, std::vector<xqt::hwr::InkStroke> strokes, const QRectF& area,
                     const QPolygonF& path, const QRectF& box);
    QVariantMap inkCopyPlace(xqt::CanvasView* v, int page, const QRectF& box) const;
    /// The owner of the reading in flight (another copy cancels it)
    std::unique_ptr<QObject> inkCopyOwner;
    // --- stickers (AppStickers.cpp) ---
    mutable std::unique_ptr<xqt::StickersModel> stickers;
    /// The stickers' list follows the library
    void syncStickers() const;
    /// A new document just made: saved in the library's current folder as `name` (createDocument)
    bool saveNewDocument(xqt::DocumentSession& doc, const QString& name, bool inLibrary);
    bool saveNewDocumentAt(xqt::DocumentSession& doc, const std::filesystem::path& path);
    // --- pages as files (AppPageFiles.cpp) ---
    /// The file read to insert pages from (one at a time, let go when the dialog closes)
    std::shared_ptr<struct PageFileSource> pageFile;
    quint64 pageFileReads = 0;  ///< (a later read wins)
    /// A picture of a page on the clipboard (copyPagesAsImage, an export of one page), with the toast
    void putPageImage(const QImage& image, const QByteArray& png, int page, int dpi, int pages, bool capped,
                      bool announce = true);
    /// Where extracted and split documents go: next to the document, else the library's current folder
    std::filesystem::path pageFilesFolder(xqt::DocumentSession& s) const;
    /// The parts of a split (splitPlan) with their names
    std::vector<std::pair<std::string, std::vector<size_t>>> splitParts(const QString& mode, int every,
                                                                        const QList<int>& pages,
                                                                        QString* error) const;
    /// Write documents of pages of `s` off the UI thread; `then` gets the files written, or the error
    void writePageFiles(xqt::DocumentSession& s, std::vector<std::pair<std::filesystem::path, std::vector<size_t>>> files,
                        std::function<void(const QStringList&, const QString&)> then);
    // --- page templates (AppTemplates.cpp) ---
    mutable std::unique_ptr<xqt::StickersModel> templateList;
    void syncTemplates() const;
    /// Read a template off the UI thread; `then` gets its page copied (nullptr and the error if it could not be read)
    /// and whether it was saved without its background (a copied page does not keep the background's name)
    void readTemplate(const QString& path, std::function<void(std::shared_ptr<xqt::PageClipboard>,
                                                              bool withoutBackground, const QString& error)> then);
    /// The template's pages for `session`, `count` times: copies, which get the background new pages get there when
    /// the template has none of its own
    std::vector<PageRef> templatePagesFor(xqt::PageClipboard& copy, bool withoutBackground,
                                          xqt::DocumentSession& session, int count, bool* addedToMergedPdf);
    /// Editing beside the page: the page's text, or the text box at a point.
    QString startMarkdown(int page, std::optional<QPointF> at);
    /// After a change of the Markdown being edited: its pages and how far it goes below one.
    void markdownPagesChanged(double overflow);
    qreal markSpacing = 1.0;
    /// Files were renamed or moved (library, recent files): open documents and the recent list follow.
    void filesChanged(const xqt::DocumentFiles::Result& result);
    /// Another tab is the current one: the window follows its document (CurrentDocument), then tells
    void currentTabChanged();
    /// The tab list of this window (with its signals).
    void makeTabs();
    /// Reopens the tabs of a journal; `recovered`: tab index -> recovery file to load instead of the file.
    void reopenTabs(const std::vector<std::pair<fs::path, int>>& tabs, int current,
                    const std::map<size_t, std::pair<fs::path, fs::path>>& recovered);

    std::shared_ptr<Palette> colors;
    AppController* primary = nullptr;  ///< the main window's controller (nullptr: this is the main window)
    bool windowGone = false;           ///< its window was closed (it is on its way out)
    // The parts of `shared` this window uses most (AppServices owns them; they outlive every window)
    xqt::hwr::HandwritingSearch* handwriting = nullptr;
    xqt::HandwritingSettings* handwritingView = nullptr;
    /// The open documents of this window to the handwriting search
    void syncHandwriting();
    /// The handwriting read in a saved document, to the library's cache
    void handOverHandwriting(xqt::DocumentSession& s);
    std::unique_ptr<xqt::TabManager> tabs;
    std::unique_ptr<xqt::ReferenceMode> referenceMode;  ///< (after `tabs`, reset before it)
    std::unique_ptr<xqt::VersionCompare> compareMode;   ///< (after `referenceMode`, reset before it)
    std::unique_ptr<xqt::PresenterConsole> presenter;   ///< (after `tabs`, reset before it)
    std::unique_ptr<xqt::Citations> citations;
    std::unique_ptr<xqt::AudioControl> audioControl;  ///< (one recording per window; qt/docs/features/audio.md)
    void makeAudioControl();
    std::unique_ptr<xqt::TimelineControl> timelineControl;  ///< (qt/docs/features/timeline.md; made with the audio's)
    bool replacePristine = true;  ///< opening a file replaces an untouched new document (not for a reference)
    std::unique_ptr<xqt::PagesModel> pages;
    std::unique_ptr<xqt::PageFilterModel> filteredPages;
    std::unique_ptr<xqt::OutlineModel> outline;
    std::unique_ptr<xqt::AnnotationsModel> annotations;
    std::unique_ptr<xqt::VersionsModel> versions;
    std::string nextSaveMessage;  ///< saveWithMessage(): for the save it starts
    std::unique_ptr<xqt::LayersModel> layers;
    xqt::ShortcutsModel* shortcuts = nullptr;
    std::unique_ptr<xqt::MarkdownSession> markdown;
    xqt::DocumentSession* mdSession = nullptr;
    int mdPage = -1;
    bool mdInPanel = false;  ///< markdownInPanel() (tests only; the text tool writes Markdown on the page)
    int mdLastPage = -1;
    double mdOverflow = 0;
    /// Pages were copied after the last copy onto the clipboard (pastesNoteBeforePages)
    bool pagesCopiedLast = false;
    xqt::PageClipboard* pageClipboard = nullptr;  ///< pages can be pasted into any window
    std::vector<size_t> pageList(const QList<int>& pages) const;
    /// A saved document's entry for the library index, from memory (see LibraryIndex::documentSaved).
    void handOverToLibrary(xqt::DocumentSession& s);
    int lastTabCount = 0;  ///< tabs before the last change of their number (a document opened: handWhenOpening)
    xqt::LibraryBookmarksModel* libraryBookmarks = nullptr;
    xqt::LibraryTagsModel* libraryTags = nullptr;
    xqt::LibraryTodosModel* libraryTodos = nullptr;
    /// Documents that are not open, loaded to tick a to-do in them and saved (gone once saved)
    std::vector<std::unique_ptr<xqt::DocumentSession>> todoSaves;
    /// Set a to-do in an open document: one undo step (false: it is not there)
    bool setTodoIn(xqt::DocumentSession& s, const QString& rawText, int occurrence, bool done);
    xqt::SettingsModel* settingsView = nullptr;
    xqt::ToolboxModel* toolbox = nullptr;
    /// The color the last entry taken gave the tool (it follows a palette switch while the tool still has it)
    QColor appliedEntryColor;
    xqt::LibraryModel* library = nullptr;
    xqt::RecentFiles* recent = nullptr;
    bool home = true;
    fs::path journalFile;
    std::unique_ptr<xqt::SessionRecovery> recovery;  // after `tabs`: destroyed first
    bool recoveryPending = false;
    QString pdfMode = "highlight";
    void applyPdfTextMode();
    /// The roles of the tools' colors (setting "colorRoles": "pen=marker:warnings;highlighter=classic:keyTerms")
    QMap<QString, QString> colorRoles() const;
    void storeColorRoles(const QMap<QString, QString>& roles);
    /// The colors taken from a palette take their role's color in `paletteId` (where it has the role)
    void followColorPalette(const QString& paletteId);
    /// The current tab's document and view, their signals relayed (connectCurrentDocument: onto the window's)
    std::unique_ptr<xqt::CurrentDocument> current;
    void connectCurrentDocument();
    /// What acts on the current document's canvas (`edit`; follows the current tab)
    std::unique_ptr<xqt::CanvasActions> edits;

    // --- annotations of other apps (AppAdopt.cpp) ---
    struct AdoptScan {
        fs::path pdf;  ///< the background PDF looked at
        int count = 0;
        QString app;
        bool scanning = false;
        bool offer = false;  ///< ask about them when the scan is done
    };
    std::map<quint64, AdoptScan> adoptScans;  ///< by session serial (forgotten when the tab goes)
    bool adoptRunning = false;
    /// Look at the session's background PDF for annotations of other apps (on a worker), unless it was looked at;
    /// `offer`: ask about them (annotationsToAdopt) if not asked before for this file.
    void scanAdoptable(xqt::DocumentSession* s, bool offer);

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

    // --- encrypted PDFs (AppEncryption.cpp) ---
    /// A file waiting for its password: opened (openLoaded), or a recovered autosave (`recoverTo`: its document).
    struct PendingPassword {
        fs::path file;
        QString path;
        bool shown = false;
        fs::path recoverTo;
        bool recovering = false;
        fs::path passwordFile;  ///< the PDF that needs it (the file, or the PDF of a .xopp)
    };
    std::deque<PendingPassword> pendingPasswords;
    /// The encryption of the next save startSave starts (SaveRequest::encryption)
    std::optional<xqt::PdfEncryption::Encryption> nextSaveEncryption;
    /// Ask the window for the password of this file (after those asked for already).
    void askPassword(PendingPassword pending);
    /// The rest of openPath: the loaded file in a new tab.
    bool openLoaded(const fs::path& file, const QString& path, xqt::DocumentSession::LoadResult result, bool shown,
                    std::unique_ptr<xqt::DocumentSession> textSession = nullptr);
    /// Remove what the app made of `file` in its caches (previews, page pictures, clean copies, versions, the library's
    /// entry and handwriting, shared copies): after its password was set, changed or removed.
    void forgetDerivatives(const fs::path& file, bool locked);
    /// protectDocument, removeProtection
    bool applyProtection(xqt::DocumentSession* s, const xqt::PdfEncryption::Protection* protection);

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
