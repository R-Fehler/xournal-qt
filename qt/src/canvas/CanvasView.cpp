#include "CanvasView.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include "control/PdfCache.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/DocumentChangeType.h"
#include "render/RenderService.h"

#include "CanvasPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

namespace xqt {

CanvasView::CanvasView(DocumentSession& session, QObject* parent):
        QObject(parent),
        session(session),
        renderService(*session.getApp().getRenderService()),
        viewController(&layout) {
    pdfCache = std::make_unique<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    registerListener(&session);
    session.setXournalView(this);
    rebuildPages();

    connect(&viewController, &ViewController::zoomChanged, this, [this] {
        // Like upstream: while zooming, show the existing buffers scaled and render sharp only once the zoom is stable.
        renderService.blockRerenderZoom();
        updateRenderParams();
    });
    connect(&viewController, &ViewController::zoomSettled, this, [this] { updateVisibility(); });
    connect(&viewController, &ViewController::changed, this, [this] {
        updateVisibility();
        Q_EMIT updateRequested();
    });
    connect(&session, &DocumentSession::scrollToPageRequested, this,
            [this](qulonglong page) { viewController.scrollToPage(page); });
    connect(&session, &DocumentSession::scrollToRectRequested, this,
            [this](qulonglong page, QRectF rect) { viewController.scrollToPageRect(page, rect); });
    // Search hits are drawn by the canvas item over the pages.
    connect(&session.search(), &DocumentSearch::changed, this, &CanvasView::updateRequested);
    // Column layout changed in the settings: lay out again, keep the current page in view.
    connect(&session.getApp(), &AppContext::settingsChanged, this, [this] {
        if (layoutConfig() != layout.getConfig()) {
            const size_t page = this->session.getCurrentPageNo();
            refreshLayout();
            viewController.fitWidth();
            viewController.scrollToPage(page);
        }
    });

    releaseTimer.setSingleShot(true);
    releaseTimer.setInterval(1000);
    connect(&releaseTimer, &QTimer::timeout, this, &CanvasView::releaseFarBuffers);
    updateRenderParams();
}

CanvasView::~CanvasView() {
    session.setXournalView(nullptr);
    unregisterListener();
    pages.clear();  // detaches and cancels the rasters
}

std::optional<size_t> CanvasView::indexOf(const CanvasPage* page) const {
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].get() == page) {
            return i;
        }
    }
    return std::nullopt;
}

QRectF CanvasView::pageViewRect(size_t index) const {
    const double zoom = viewController.zoom();
    const QRectF r = layout.pageRect(index, zoom);
    return r.translated(viewController.contentOrigin());
}

CanvasPage* CanvasView::pageAt(QPointF viewPos) const {
    if (auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom())) {
        return pages[*idx].get();
    }
    return nullptr;
}

std::pair<size_t, size_t> CanvasView::visiblePages() const {
    return layout.pagesIn(viewController.visibleContentRect(), viewController.zoom());
}

void CanvasView::setDevicePixelRatio(double value) {
    if (value <= 0 || value == dpr) {
        return;
    }
    dpr = value;
    updateRenderParams();
    for (auto& p: pages) {
        if (p->getRaster().withBuffer([](xoj::view::Mask& m) { return m.isInitialized(); })) {
            p->rerenderPage();
        }
    }
}

void CanvasView::updateRenderParams() {
    renderZoom = viewController.zoom();
    renderDpr = dpr;
}

void CanvasView::rebuildPages() {
    pages.clear();
    Document* doc = session.getDocument();
    size_t n = 0;
    {
        std::shared_lock lock(*doc);
        n = doc->getPageCount();
    }
    pages.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        pages.push_back(std::make_unique<CanvasPage>(*this, doc->getPage(i)));
    }
    refreshLayout();
}

DocumentLayout::Config CanvasView::layoutConfig() const {
    // Upstream's view settings (viewColumns, showPairedPages, numPairsOffset).
    const Settings* s = session.getSettings();
    return {static_cast<size_t>(std::max(1, s->getViewColumns())), s->isShowPairedPages(),
            static_cast<size_t>(std::max(0, s->getPairsOffset()))};
}

void CanvasView::refreshLayout() {
    std::vector<PageRef> refs;
    refs.reserve(pages.size());
    for (const auto& p: pages) {
        refs.push_back(p->getPage());
    }
    layout.update(*session.getDocument(), refs, layoutConfig());
    viewController.layoutChanged();
    Q_EMIT pagesChanged();
}

void CanvasView::updateVisibility() {
    if (pages.empty()) {
        return;
    }
    const auto [first, last] = visiblePages();
    const double zoom = viewController.zoom();
    size_t mostVisible = session.getCurrentPageNo();
    double bestArea = -1;
    const QRectF visible = viewController.visibleContentRect();
    for (size_t i = first; i <= last && i < pages.size(); ++i) {
        CanvasPage* page = pages[i].get();
        const auto info = page->bufferInfo();
        // Render visible pages that have no buffer or a buffer at another zoom/resolution (the render service defers
        // this while a zoom gesture is running).
        if (!info.valid || info.zoom != zoom || info.dpiScale != dpr) {
            page->rerenderPage();
        }
        const QRectF inter = layout.pageRect(i, zoom).intersected(visible);
        if (const double area = inter.width() * inter.height(); area > bestArea) {
            bestArea = area;
            mostVisible = i;
        }
    }
    // Upstream Layout::updateVisibility: the most visible page becomes the current one.
    session.setCurrentPageNo(mostVisible);
    releaseTimer.start();
}

void CanvasView::releaseFarBuffers() {
    // Upstream XournalView::cleanupBufferCache: keep the visible pages plus a preload window.
    const auto [first, last] = visiblePages();
    const size_t before = session.getSettings()->getPreloadPagesBefore();
    const size_t after = session.getSettings()->getPreloadPagesAfter();
    const size_t keepFrom = first > before ? first - before : 0;
    const size_t keepTo = last + after;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (i < keepFrom || i > keepTo) {
            pages[i]->deleteViewBuffer();
        }
    }
}

// --- XournalView ---------------------------------------------------------------------------------------------------

size_t CanvasView::getCurrentPage() const { return session.getCurrentPageNo(); }

void CanvasView::layerChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage();
    }
}

void CanvasView::recreatePdfCache() {
    pdfCache = std::make_unique<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    for (auto& p: pages) {
        p->rerenderPage();
    }
}

// --- RasterHost ------------------------------------------------------------------------------------------------------

Document* CanvasView::rasterDocument() const { return session.getDocument(); }

RasterParams CanvasView::rasterParams() const { return RasterParams{renderZoom.load(), renderDpr.load()}; }

void CanvasView::rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) {
    for (auto& p: pages) {
        if (&p->getRaster() == raster) {
            p->rasterUpdated(area);
            return;
        }
    }
}

// --- DocumentListener ----------------------------------------------------------------------------------------------

void CanvasView::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
        recreatePdfCache();
        rebuildPages();
    }
}

void CanvasView::pageSizeChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage(true);
    }
    refreshLayout();
}

void CanvasView::pageChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage();
    }
}

void CanvasView::pageInserted(size_t page) {
    PageRef ref;
    {
        std::shared_lock lock(*session.getDocument());
        ref = session.getDocument()->getPage(page);
    }
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(std::min(page, pages.size())),
                 std::make_unique<CanvasPage>(*this, std::move(ref)));
    refreshLayout();
}

void CanvasView::pageDeleted(size_t page) {
    if (page < pages.size()) {
        pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(page));
    }
    refreshLayout();
}

void CanvasView::pageSelected(size_t) {}

}  // namespace xqt
