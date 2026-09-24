/*
 * xournal-qt: a document session shown in a canvas (one per tab).
 *
 * Replaces upstream's GTK XournalView (gui/XournalView.cpp) for the Qt canvas:
 *  - owns one CanvasPage per document page, the page layout and the view controller (zoom/scroll);
 *  - implements the shadow XournalView interface (what reused upstream code reaches via control->getWindow()) and
 *    DocumentListener (pages inserted, deleted, resized, changed);
 *  - is the RasterHost of the page rasters (document, PDF cache, render zoom): visible pages are rendered at the
 *    current zoom (after zoom gestures settle, like upstream); which other pages keep their buffers and which are
 *    rendered in advance is planned by CanvasMemory (planCache, trimTo).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QImage>
#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>

#include "gui/Layout.h"
#include "util/Color.h"
#include "gui/XournalView.h"
#include "model/DocumentListener.h"
#include "model/Layer.h"
#include "model/PageRef.h"
#include "render/PageRaster.h"

#include "control/zoom/ZoomControl.h"

#include "DocumentLayout.h"
#include "ViewController.h"
#include "pdf/base/XojPdfPage.h"  // for XojPdfPageSelectionStyle

#include "GeometryToolLayer.h"

class EditSelection;
class PdfCache;
class PdfElemSelection;

namespace xqt {

class CanvasPage;
class DocumentSession;
class TextEditor;
class MarkdownEditor;
class CanvasTextInput;
class RenderService;

class CanvasView final: public QObject, public XournalView, public Layout, public RasterHost, public DocumentListener {
    Q_OBJECT
public:
    explicit CanvasView(DocumentSession& session, QObject* parent = nullptr);
    ~CanvasView() override;

    DocumentSession& getSession() const { return session; }
    RenderService& getRenderService() const { return renderService; }
    ViewController& getViewController() { return viewController; }
    const DocumentLayout& documentLayout() const { return layout; }

    size_t pageCount() const { return pages.size(); }
    CanvasPage* getPage(size_t index) const { return pages[index].get(); }
    std::optional<size_t> indexOf(const CanvasPage* page) const;
    QRectF pageViewRect(size_t index) const;
    /// Page under a view position (nullptr between pages).
    CanvasPage* pageAt(QPointF viewPos) const;
    /// First and last visible page (first > last if none).
    std::pair<size_t, size_t> visiblePages() const;

    void setDevicePixelRatio(double dpr);
    double devicePixelRatio() const { return dpr; }

    // --- memory (CanvasMemory) --------------------------------------------------------------------------------
    /// Shown in a window (DocumentCanvasItem): scrolling in it makes it the current view of CanvasMemory.
    void setShown(bool shown);
    bool isShown() const { return shown; }
    /// Bytes of the rendered page buffers
    qint64 bufferBytes() const;
    /// The current view: keep the visible pages and, within `share` bytes, 35 % before and 65 % after them; render
    /// those in advance; release the others. Returns the bytes planned.
    qint64 planCache(qint64 share);
    /// A view in the background: release the pages farthest from its current page until it holds at most
    /// `allowed` bytes. Returns what it holds then.
    qint64 trimTo(qint64 allowed);
    /// How often the visible pages were looked at (tests: scroll changes are collected)
    quint64 visibilityUpdates() const { return visibilityCount; }
    /// How long scroll changes are collected before the visible pages are looked at (ms; tests)
    void setVisibilityDelay(int ms) { visibilityDelay = ms; }
    /// Pages from `first` to `last` keep their buffers (the window of the last planCache; tests)
    std::pair<size_t, size_t> cacheWindow() const { return window; }

    /// A small picture of a page drawn in advance (PageSketches), shown until the page is rendered; null: none.
    /// Nothing is rendered for it: a page is rendered sharp straight away.
    void setPreviewSource(std::function<QImage(size_t page)> source) { previewSource = std::move(source); }
    QImage preview(size_t page) const { return previewSource ? previewSource(page) : QImage(); }
    /// Previews came: pages without a buffer show them
    void previewsChanged() { Q_EMIT updateRequested(); }

    // --- XournalView (shadow) ---------------------------------------------------------------------------------
    size_t getCurrentPage() const override;
    void layerChanged(size_t page) override;
    void recreatePdfCache() override;
    /// `rerender`: every page is drawn again (the PDF may look different); else the pages keep their pictures and only
    /// those that show a PDF page the old PDF did not have are drawn (pasted pages joined the merged PDF).
    void replacePdfCache(bool rerender);

    // --- RasterHost (rasterParams is called from render threads) -----------------------------------------------
    Document* rasterDocument() const override;
    PdfCache* rasterPdfCache(bool background) const override;
    RasterParams rasterParams() const override;
    void rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) override;

    // --- DocumentListener -------------------------------------------------------------------------------------
    void documentChanged(DocumentChangeType type) override;
    void pageSizeChanged(size_t page) override;
    void pageChanged(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;
    void pageSelected(size_t page) override;

    // XournalView: the selection (port of upstream XournalView's selection handling). The view owns it; while it
    // exists, the selected elements are out of their layer (upstream's EditSelection).
    EditSelection* getSelection() const override { return selection.get(); }
    void setSelection(EditSelection* selection) override;
    void clearSelection() override;
    void deleteSelection(EditSelection* sel = nullptr) override;
    void repaintSelection(bool evenWithoutSelection = false) override;
    double getZoom() const override;
    XournalppCursor* getCursor() const override;
    Control* getControl() const override;
    Layout* getLayout() const override { return const_cast<CanvasView*>(this); }
    void ensureRectIsVisible(int x, int y, int width, int height) override;
    /// Revision of the selection's look (changes, moves): the canvas item redraws it.
    quint64 selectionRevision() const { return selectionRev; }
    ZoomControl* getZoomControl() { return &zoomControl; }

    // --- selection actions (ports of upstream Control / ClipboardHandler) ---
    /// Put the selection on the clipboard (upstream's "application/xournal" data, and the text of text elements).
    bool copySelection();
    bool cutSelection();
    /// Paste elements from the clipboard as a new selection in the middle of the visible part of the current page.
    /// Paste what is in the clipboard. `viewPos`: where it goes (else the middle of the visible page).
    bool pasteElements(std::optional<QPointF> viewPos = std::nullopt);
    /// Text from the clipboard as a text element.
    bool pasteText(const QString& content, std::optional<QPointF> viewPos = std::nullopt);
    /// Select everything on the active layer of the current page (Control::selectAllOnPage).
    void selectAllOnPage();
    /// Insert an image (file contents: PNG, JPEG, ...) on the current page, in the middle of its visible part and
    /// fitted into it, as a selection to move or resize (port of ImageHandler::addImageToDocument). False if the data
    /// is not an image.
    bool insertImage(const QByteArray& data);

    // --- PDF links ---
    struct LinkTarget {
        QString uri;      ///< external link, or
        int page = -1;    ///< page of this document (-1: none)
        int pdfPage = -1; ///< the PDF page the link points to (also when the document has no page for it)
        QRectF viewRect;  ///< the link area in view coordinates
    };
    /// The PDF link under a view position, if any.
    std::optional<LinkTarget> linkAt(QPointF viewPos) const;
    /// A tap (finger, or pen/mouse with the hand or a select tool) at a view position: shows a link there.
    bool tapAt(QPointF viewPos);
    /// Two taps in the same spot: zoom in on what was tapped (the column of text, if the page has columns), or,
    /// when the page is zoomed in already, back to the whole page.
    void doubleTapAt(QPointF viewPos);
    /// A web address in a text element under this point (nothing if there is none).
    std::optional<LinkTarget> textLinkAt(QPointF viewPos) const;
    /// The column of text around a point of a PDF page (nothing when the page has none there).
    std::optional<QRectF> textColumnAt(size_t page, QPointF pagePoint) const;

    // --- PDF text (PDF text tools; port of upstream's PdfElemSelection use and PdfFloatingToolbox) ---
    enum class PdfTextMode { Highlight, Underline, Strikethrough, Select };
    void setPdfTextMode(PdfTextMode mode) { pdfTextMode = mode; }
    /// Color of highlights (none: the highlighter's color, as upstream)
    void setPdfHighlightColor(std::optional<Color> c) { pdfHighlightColor = c; }
    PdfTextMode getPdfTextMode() const { return pdfTextMode; }
    /// Input of the PDF text tools on a page (page coordinates, points).
    /// Select the word of the PDF under this point (or its whole line), as a long press does on a phone.
    /// Returns false when there is no PDF text there.
    bool selectPdfTextAt(QPointF viewPos, bool wholeLine);
    /// Drag one end of the PDF text selection to another place (the other end stays).
    bool dragPdfSelection(QPointF viewPos, bool startEnd);
    /// The two ends of the selection in view coordinates (for the handles); empty when nothing is selected.
    QRectF pdfSelectionEnds() const;
    /// Is this place (view coordinates) on the selected PDF text? (A press somewhere else unselects it.)
    bool pdfTextSelectionContains(QPointF viewPos) const;
    /// All of the selected text in view coordinates (for the actions beside it); empty when nothing is selected.
    /// It moves with the page, so it has to be read again whenever the view scrolls or zooms.
    QRectF pdfSelectionBox() const;
    /// Bring the selected text back into view (it may be far away after scrolling).
    void scrollToPdfSelection();
    /// The selected PDF text ("" if none).
    std::string selectedPdfText() const;
    /// The canvas page showing this page of the document (none: not shown).
    CanvasPage* canvasPageOf(const XojPage* page) const;
    /// Draw the marks of the setsquare's scale onto its page, every `spacingCm`, with the pen's color and width (one
    /// step to undo). False when there is no setsquare out.
    bool drawGeometryMarks(double spacingCm);
    /// The setsquare / compass on the canvas.
    GeometryToolLayer& geometryTool() { return geometry; }
    const GeometryToolLayer& geometryTool() const { return geometry; }
    void pdfTextPress(CanvasPage& page, double x, double y);
    void pdfTextMove(CanvasPage& page, double x, double y);
    void pdfTextRelease(CanvasPage& page);
    /// Finish a PDF text selection with this style (tells the UI about it, or marks it right away).
    /// `mark`: with a marking tool the text is marked right away (the tools do that; a long press only selects).
    bool finishPdfSelection(CanvasPage& page, XojPdfPageSelectionStyle style, bool mark = true);
    /// The selected PDF text: mark it (strokes over the text, one undo step) / copy it / drop the selection.
    bool markPdfText(PdfTextMode mode);
    bool copyPdfText();
    void clearPdfTextSelection();
    bool hasPdfTextSelection() const;

    // --- navigation history: jumps (links, page grid, sidebar) can be gone back and forth, like a browser ---
    /// Go to a page and remember where the view was.
    void jumpToPage(size_t page);
    bool canGoBack() const { return !backStack.empty(); }
    bool canGoForward() const { return !forwardStack.empty(); }
    bool navigateBack();
    bool navigateForward();
    void clearNavigation();

    // --- text tool (port of XojPageView::startText / XournalView::endTextAllPages): one editor per view ---
    TextEditor* getTextEditor() const { return textEditor.get(); }
    MarkdownEditor* getMarkdownEditor() const { return markdownEditor.get(); }
    /// The text being typed on the canvas: a text box or Markdown (nullptr: none).
    CanvasTextInput* getTextInput() const;
    /// Write Markdown on the page (formatted while typing): the page's text, or the text box at (x, y) (a new one
    /// there if none).
    void startMarkdown(size_t page, bool pageText, double x, double y);
    /// A tap with the text tool at a page position (points).
    void startText(CanvasPage& page, double x, double y);
    void endTextEditing();
    /// Whether the page's Markdown text (the box at its margins) is at a point (page coordinates).
    bool markdownBoxAt(CanvasPage& page, double x, double y) const;
    /// A tap on the check box of a task in a Markdown text (page coordinates): it is switched, one undo step.
    bool toggleMarkdownCheckBox(CanvasPage& page, double x, double y);
    /// New texts of the text tool: Markdown text boxes of this size, or ordinary texts. `inPanel`: Markdown text
    /// boxes are edited in the editor beside the page (markdownBoxRequested), else on the page (their source).
    void setMarkdownText(bool markdown, double size, bool inPanel);

    // --- selections of Markdown texts ------------------------------------------------------------------------------
    // Markdown texts are in the page's layer "Markdown", which is not the selected layer (the pen writes into
    // another one). A selection of them is made in that layer; it is the selected layer only while the selection
    // exists, also on pages the selection is moved to (the selection is dropped into the selected layer there).
    /// Select the page's Markdown layer for a selection of its texts. Returns the layer selected before (nothing:
    /// the page has no visible Markdown layer, or it is selected already).
    std::optional<Layer::Index> selectMarkdownLayer(const PageRef& page);
    /// Select a layer again (a selection of Markdown texts did not come about).
    void restoreSelectedLayer(const PageRef& page, Layer::Index layer);
    /// Whether the layer (1-based) of the page is its Markdown layer.
    bool isMarkdownLayer(const PageRef& page, Layer::Index layer) const;
    /// The selection just set is of Markdown texts from `page`, whose selected layer was `before`.
    void markdownSelectionMade(const PageRef& page, Layer::Index before);

    // Layout (upstream gui/Layout, content pixels)
    XojPageView* getPageViewAt(int x, int y) const override;
    int getTotalPixelWidth() const override;
    int getTotalPixelHeight() const override;
    xoj::util::Rectangle<double> getVisibleRect() override;
    void scrollRelative(double x, double y) override;

Q_SIGNALS:
    /// Something visible changed: the canvas item should repaint.
    void updateRequested();
    /// The set or geometry of pages changed.
    void pagesChanged();
    /// A selection was made or cleared.
    void selectionChanged(bool hasSelection);
    /// Text editing started or ended (keyboard / input method for the canvas).
    void textEditingChanged(bool editing);
    /// A long press with a finger, or a right click: the UI shows what can be done here (paste, ...).
    void contextRequested(QPointF viewPos);
    /// The text tool tapped the Markdown box of a page (0-based): the UI opens its editor.
    void markdownRequested(int page);
    /// The text tool tapped a Markdown text box, or a place for a new one (page coordinates), to be edited beside
    /// the page.
    void markdownBoxRequested(int page, double x, double y);
    /// A PDF link was tapped (the UI offers to follow it).
    void linkTapped(const QString& uri, int page, QRectF viewRect);
    void navigationChanged();
    /// PDF text was selected (Select mode): the UI offers marking / copying it; rect in view coordinates.
    void pdfTextSelected(QRectF viewRect);
    void pdfTextSelectionCleared();
    /// The setsquare / compass changed on its own (e.g. put aside because its page went).
    void geometryChanged();

private:
    void rebuildPages();
    /// Drop the queued renders of all pages and wait for the running ones (the pages go)
    void cancelRenders();
    void refreshLayout();
    DocumentLayout::Config layoutConfig() const;
    void updateVisibility();
    /// A scroll or zoom change: the visibility update (the current page, the models, the sidebar that follows) at
    /// most every few milliseconds - the mouse sends more moves than there are frames.
    void viewChanged();
    void updateRenderParams();
    /// What a buffer of the page at the current zoom takes (bytes), or what its buffer takes if that is more
    qint64 pageBytes(size_t index) const;

    DocumentSession& session;
    RenderService& renderService;
    DocumentLayout layout;
    ZoomControl zoomControl;  ///< upstream's zoom values for reused tools (from the view controller)
    ViewController viewController;
    /// (shared: its entries are evicted by a worker, which may still run when the view goes)
    std::shared_ptr<PdfCache> pdfCache;
    /// Evict the PDF cache but for these PDF pages, on a worker (it waits for a running PDF render)
    void evictPdfCache(std::unordered_set<size_t> keep);
    /// For pages rendered in advance: an instance of the PDF of their own (loaded on first use, by a worker)
    mutable std::mutex backgroundPdfMutex;
    mutable std::unique_ptr<PdfCache> backgroundPdfCache;
    mutable bool backgroundPdfLoaded = false;
    /// Replaced PDF caches: a render may still use them (they go with the view)
    std::vector<std::shared_ptr<PdfCache>> retiredPdfCaches;
    size_t pdfCachePages = 0;  ///< the pages of the PDF of `pdfCache`
    std::vector<std::unique_ptr<CanvasPage>> pages;
    bool shown = false;
    std::pair<size_t, size_t> window{1, 0};
    int visibilityDelay = 8;
    QElapsedTimer sinceVisibility;
    QTimer visibilityTimer;
    /// Visibility updates so far (tests)
    quint64 visibilityCount = 0;
    std::function<QImage(size_t)> previewSource;
    /// XQT_PERF: pages in view waiting for their render at the current zoom, since when (ms since the epoch)
    std::unordered_map<const CanvasPage*, qint64> sharpWanted;
    double dpr = 1.0;
    std::atomic<double> renderZoom{1.0};
    std::atomic<double> renderDpr{1.0};
    std::unique_ptr<EditSelection> selection;
    std::unique_ptr<TextEditor> textEditor;
    std::unique_ptr<MarkdownEditor> markdownEditor;
    struct MarkdownSelection {
        const EditSelection* selection = nullptr;
        /// The pages whose selected layer is their Markdown layer for now, with the layer selected before, and
        /// whether the Markdown layer was made for this (made for nothing: removed again).
        struct Page {
            PageRef page;
            Layer::Index before = 0;
            Layer* created = nullptr;
        };
        std::vector<Page> pages;
    };
    std::optional<MarkdownSelection> markdownSelection;
    void endMarkdownSelection();
    bool markdownText = false;       ///< the text tool makes Markdown text boxes
    double markdownTextSize = 10;    ///< of this font size
    bool markdownInPanel = true;     ///< Markdown text boxes are edited beside the page
    GeometryToolLayer geometry{*this};
    std::unique_ptr<PdfElemSelection> pdfSelection;
    CanvasPage* pdfSelectionPage = nullptr;
    PdfTextMode pdfTextMode = PdfTextMode::Highlight;
    std::optional<Color> pdfHighlightColor;
    /// A place in the document: a page (kept even if it is moved) and the view's top-left on it (points).
    struct NavPoint {
        PageRef page;
        QPointF offset;
    };
    NavPoint currentPlace() const;
    bool restorePlace(const NavPoint& place);
    std::vector<NavPoint> backStack, forwardStack;
    quint64 selectionRev = 0;
};

}  // namespace xqt
