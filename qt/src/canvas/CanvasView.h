/*
 * xournal-qt: a document session shown in a canvas (one per tab).
 *
 * Replaces upstream's GTK XournalView (gui/XournalView.cpp) for the Qt canvas:
 *  - owns one CanvasPage per document page, the page layout and the view controller (zoom/scroll);
 *  - implements the shadow XournalView interface (what reused upstream code reaches via control->getWindow()) and
 *    DocumentListener (pages inserted, deleted, resized, changed);
 *  - is the RasterHost of the page rasters (document, PDF cache, render zoom) and keeps the rendered buffers in
 *    line with the visible area: visible pages are rendered at the current zoom (after zoom gestures settle, like
 *    upstream), buffers of pages far outside the viewport are released (upstream's preload window).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>

#include "gui/Layout.h"
#include "util/Color.h"
#include "gui/XournalView.h"
#include "model/DocumentListener.h"
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

    // --- XournalView (shadow) ---------------------------------------------------------------------------------
    size_t getCurrentPage() const override;
    void layerChanged(size_t page) override;
    void recreatePdfCache() override;

    // --- RasterHost (rasterParams is called from render threads) -----------------------------------------------
    Document* rasterDocument() const override;
    PdfCache* rasterPdfCache() const override { return pdfCache.get(); }
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
    /// A tap with the text tool at a page position (points).
    void startText(CanvasPage& page, double x, double y);
    void endTextEditing();

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
    /// A PDF link was tapped (the UI offers to follow it).
    void linkTapped(const QString& uri, int page, QRectF viewRect);
    void navigationChanged();
    /// PDF text was selected (Select mode): the UI offers marking / copying it; rect in view coordinates.
    void pdfTextSelected(QRectF viewRect);
    void pdfTextSelectionCleared();

private:
    void rebuildPages();
    void refreshLayout();
    DocumentLayout::Config layoutConfig() const;
    void updateVisibility();
    void releaseFarBuffers();
    void updateRenderParams();

    DocumentSession& session;
    RenderService& renderService;
    DocumentLayout layout;
    ZoomControl zoomControl;  ///< upstream's zoom values for reused tools (from the view controller)
    ViewController viewController;
    std::unique_ptr<PdfCache> pdfCache;
    std::vector<std::unique_ptr<CanvasPage>> pages;
    double dpr = 1.0;
    std::atomic<double> renderZoom{1.0};
    std::atomic<double> renderDpr{1.0};
    QTimer releaseTimer;
    std::unique_ptr<EditSelection> selection;
    std::unique_ptr<TextEditor> textEditor;
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
