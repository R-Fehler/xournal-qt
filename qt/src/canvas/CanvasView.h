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
#include <vector>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QTimer>

#include "gui/XournalView.h"
#include "model/DocumentListener.h"
#include "render/PageRaster.h"

#include "DocumentLayout.h"
#include "ViewController.h"

class PdfCache;

namespace xqt {

class CanvasPage;
class DocumentSession;
class RenderService;

class CanvasView final: public QObject, public XournalView, public RasterHost, public DocumentListener {
    Q_OBJECT
public:
    explicit CanvasView(DocumentSession& session, QObject* parent = nullptr);
    ~CanvasView() override;

    DocumentSession& getSession() const { return session; }
    RenderService& getRenderService() const { return renderService; }
    ViewController& getViewController() { return viewController; }
    const DocumentLayout& getLayout() const { return layout; }

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

Q_SIGNALS:
    /// Something visible changed: the canvas item should repaint.
    void updateRequested();
    /// The set or geometry of pages changed.
    void pagesChanged();

private:
    void rebuildPages();
    void refreshLayout();
    void updateVisibility();
    void releaseFarBuffers();
    void updateRenderParams();

    DocumentSession& session;
    RenderService& renderService;
    DocumentLayout layout;
    ViewController viewController;
    std::unique_ptr<PdfCache> pdfCache;
    std::vector<std::unique_ptr<CanvasPage>> pages;
    double dpr = 1.0;
    std::atomic<double> renderZoom{1.0};
    std::atomic<double> renderDpr{1.0};
    QTimer releaseTimer;
};

}  // namespace xqt
