#include "PageRaster.h"

#include <shared_mutex>
#include <utility>

#include "control/PdfCache.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "util/Range.h"
#include "util/Util.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/PdfBackgroundView.h"

#include "RenderService.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
thread_local bool forScreen = false;
struct ScreenDrawing {
    ScreenDrawing() { forScreen = true; }
    ~ScreenDrawing() { forScreen = false; }
};
}  // namespace

bool PageRaster::drawingForScreen() { return forScreen; }

PageRaster::PageRaster(RasterHost* host, RenderService* service, PageRef page):
        host(host), service(service), page(std::move(page)) {}

PageRaster::~PageRaster() = default;

void PageRaster::schedule() { service->schedule(shared_from_this()); }

void PageRaster::rerenderPage(bool sizeChanged) {
    {
        std::lock_guard lock(repaintRectMutex);
        this->rerenderComplete = true;
        this->sizeChanged = this->sizeChanged || sizeChanged;
    }
    schedule();
}

void PageRaster::ensureRendered(bool inAdvance) {
    // UI thread: `host` is only written on the UI thread (detach). Not under hostMutex: a render holds it.
    if (!host) {
        return;
    }
    const RasterParams params = host->rasterParams();
    {
        std::lock_guard lock(drawingMutex);
        if (buffer.isInitialized()) {
            double scale = 1;
            cairo_surface_get_device_scale(cairo_get_target(buffer.get()), &scale, &scale);
            if (buffer.getZoom() == params.zoom && scale == params.dpiScale) {
                return;
            }
        }
    }
    {
        std::lock_guard lock(repaintRectMutex);
        if (!this->rerenderComplete && rendering && rendering->zoom == params.zoom &&
            rendering->dpiScale == params.dpiScale) {
            return;  // being rendered so
        }
        this->rerenderComplete = true;
    }
    service->schedule(shared_from_this(), inAdvance ? RenderService::Priority::Preload : RenderService::Priority::Visible);
}

void PageRaster::rerenderRect(double x, double y, double width, double height) {
    auto rect = Rectangle<double>{x, y, width, height};
    {
        std::lock_guard lock(repaintRectMutex);
        if (this->rerenderComplete) {
            return;  // a full re-render is pending anyway
        }
        for (auto&& r: this->rerenderRects) {
            // Upstream: redrawing one rectangle is faster than repainting the same area twice, so merge
            // intersecting rectangles. A job for them is already queued.
            if (r.intersects(rect)) {
                r.unite(rect);
                return;
            }
        }
        this->rerenderRects.push_back(rect);
    }
    schedule();
}

void PageRaster::rerenderRange(const Range& range) {
    rerenderRect(range.getX(), range.getY(), range.getWidth(), range.getHeight());
}

void PageRaster::releaseBuffer() {
    std::lock_guard lock(drawingMutex);
    buffer.reset();
}

void PageRaster::detach() {
    std::lock_guard lock(hostMutex);  // waits for a running render of this raster
    host = nullptr;
}

auto PageRaster::createMask(const Range& range, const RasterParams& params) const -> xoj::view::Mask {
    // Upstream: Mask(int DPIScaling, range, zoom, CAIRO_CONTENT_COLOR_ALPHA). A template surface with a (possibly
    // fractional) device scale gives the same result for integer scales and also supports fractional ones.
    cairo_surface_t* scaleTemplate = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_surface_set_device_scale(scaleTemplate, params.dpiScale, params.dpiScale);
    xoj::view::Mask mask(scaleTemplate, range, params.zoom, CAIRO_CONTENT_COLOR_ALPHA);
    cairo_surface_destroy(scaleTemplate);
    return mask;
}

void PageRaster::renderToBuffer(cairo_t* cr, const RasterParams&, bool background) const {
    Document* doc = host->rasterDocument();
    PdfCache* pdfCache = host->rasterPdfCache(background);

    DocumentView localView;
    localView.setMarkAudioStroke(host->rasterMarkAudioStrokes());
    localView.setPdfCache(pdfCache);
    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;

    // xournal-qt: render the PDF background without holding the document lock. Same condition as
    // BackgroundView::createForPage (a hidden background layer shows a checkerboard instead).
    bool renderPdfFirst = false;
    size_t pdfPageNo = 0;
    double width = 0;
    double height = 0;
    NoteSpace space;  // (qt/docs/note-space.md)
    {
        std::shared_lock lock(*doc);
        const auto pt = page->getBackgroundType();
        renderPdfFirst = pt.isPdfPage() && page->isLayerVisible(0);
        pdfPageNo = page->getPdfPageNr();
        width = page->getWidth();
        height = page->getHeight();
        space = page->getNoteSpace();
    }
    if (renderPdfFirst) {
        if (XojPdfPageSPtr pending = host->rasterPendingPdfPage(pdfPageNo)) {
            cairo_save(cr);
            if (!space.empty()) {  // (space for notes: white paper, the PDF at its offset)
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_paint(cr);
                cairo_translate(cr, space.left, space.top);
                cairo_rectangle(cr, 0, 0, width - space.left - space.right, height - space.top - space.bottom);
                cairo_clip(cr);
            }
            pending->render(cr);  // (a pasted page: from the pasted PDF until the merged PDF is written)
            cairo_restore(cr);
        } else {
            xoj::view::PdfBackgroundView(width, height, pdfPageNo, pdfCache, space.left, space.top, !space.empty())
                    .draw(cr);
        }
        flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
    }

    std::shared_lock lock(*doc);
    ScreenDrawing screen;
    localView.drawPage(this->page, cr, false, flags);
}

void PageRaster::rerenderRectangle(const Rectangle<double>& rect, const RasterParams& params) {
    /**
     * Upstream: Padding seems to be necessary to prevent artefacts of most strokes.
     * These artefacts are most pronounced when using the stroke deletion
     * tool on ellipses, but also occur occasionally when removing regular
     * strokes.
     **/
    constexpr int RENDER_PADDING = 1;

    Range maskRange(rect);
    maskRange.addPadding(RENDER_PADDING);
    xoj::view::Mask newMask = createMask(maskRange, params);

    renderToBuffer(newMask.get(), params, false);

    std::lock_guard lock(this->drawingMutex);
    if (!this->buffer.isInitialized()) {
        return;
    }
    newMask.paintTo(this->buffer.get());
}

void PageRaster::run(bool background) {
    std::lock_guard hostLock(hostMutex);
    if (!host) {
        return;
    }
    const RasterParams params = host->rasterParams();

    bool complete = false;
    bool resized = false;
    std::vector<Rectangle<double>> rects;
    {
        std::lock_guard lock(repaintRectMutex);
        complete = std::exchange(this->rerenderComplete, false);
        resized = std::exchange(this->sizeChanged, false);
        rects = std::exchange(this->rerenderRects, {});
        if (complete) {
            rendering = params;
        }
    }

    if (complete) {
        const double width = page->getWidth(), height = page->getHeight();
        xoj::view::Mask newMask = createMask(Range(0, 0, width, height), params);
        renderToBuffer(newMask.get(), params, background);
        {
            std::lock_guard lock(this->drawingMutex);
            std::swap(this->buffer, newMask);
        }
        {
            std::lock_guard lock(repaintRectMutex);
            rendering.reset();
        }
        (void)resized;  // the host repaints the whole page in both cases
        notifyUpdated(std::nullopt);
        if (page->getWidth() != width || page->getHeight() != height) {
            rerenderPage(true);  // (its size changed while it was drawn: this picture is of the old size)
        }
    } else {
        for (const auto& rect: rects) {
            rerenderRectangle(rect, params);
            notifyUpdated(rect);
        }
    }
}

void PageRaster::notifyUpdated(std::optional<Rectangle<double>> area) {
    Util::execInUiThread([weak = weak_from_this(), area]() {
        // UI thread: `host` is only written on the UI thread (detach), so it can be read without the lock here.
        if (auto self = weak.lock(); self && self->host) {
            self->host->rasterUpdated(self.get(), area);
        }
    });
}

}  // namespace xqt
