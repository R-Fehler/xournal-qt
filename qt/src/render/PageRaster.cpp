#include "PageRaster.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

namespace {
std::atomic<long long> statRenders{0}, statPixels{0}, statNanos{0};

/// The smallest step (device independent pixels) whose multiples are whole device pixels (1 at a scale of 1 or 2, 2
/// at 1.5, 4 at 1.25)
int pixelStep(double dpiScale) {
    for (int n = 1; n <= 8; ++n) {
        if (const double d = n * dpiScale; std::abs(d - std::round(d)) < 1e-3) {
            return n;
        }
    }
    return 1;
}
}  // namespace

auto PageRaster::stats() -> Stats { return {statRenders.load(), statPixels.load(), statNanos.load()}; }

bool PageRaster::Placement::covers(const Rectangle<double>& part) const {
    if (whole || part.width <= 0 || part.height <= 0) {
        return true;
    }
    constexpr double EPS = 0.01;
    return part.x >= area.x - EPS && part.y >= area.y - EPS && part.x + part.width <= area.x + area.width + EPS &&
           part.y + part.height <= area.y + area.height + EPS;
}

bool PageRaster::drawnWhole(double width, double height, const RasterParams& params) {
    const double s = params.zoom * params.dpiScale;
    return width * s <= MAX_SIDE && height * s <= MAX_SIDE && width * s * height * s <= WHOLE_PAGE_PIXELS;
}

auto PageRaster::placementFor(double width, double height, const RasterParams& params,
                              const std::optional<Rectangle<double>>& view) -> Placement {
    Placement out;
    if (drawnWhole(width, height, params)) {
        out.area = {0, 0, width, height};
        return out;
    }
    out.whole = false;
    const double zoom = params.zoom, s = zoom * params.dpiScale;
    // The view (none yet: a part at the top left), with half its size around it, within the limits (a view bigger
    // than them: its middle)
    const Rectangle<double> v = view.value_or(Rectangle<double>(0, 0, 1024 / zoom, 1024 / zoom));
    // (a little less: its top left goes to a whole pixel, up to 8 more pixels a side)
    const double side = MAX_SIDE - 16, pixels = WHOLE_PAGE_PIXELS * 0.98;
    double w = std::min(2 * v.width, side / s), h = std::min(2 * v.height, side / s);
    if (const double px = w * s * h * s; px > pixels) {
        const double f = std::sqrt(pixels / px);
        w *= f;
        h *= f;
    }
    w = std::min(w, width);
    h = std::min(h, height);
    const double x = std::clamp(v.x + v.width / 2 - w / 2, 0.0, width - w);
    const double y = std::clamp(v.y + v.height / 2 - h / 2, 0.0, height - h);
    // Its top left on a whole device pixel (the tiles are placed there)
    const int step = pixelStep(params.dpiScale);
    out.x = static_cast<int>(std::floor(x * zoom / step)) * step;
    out.y = static_cast<int>(std::floor(y * zoom / step)) * step;
    const double left = out.x / zoom, top = out.y / zoom;
    out.area = {left, top, std::min(width, x + w) - left, std::min(height, y + h) - top};
    return out;
}

void PageRaster::setView(const Rectangle<double>& v) {
    std::lock_guard lock(repaintRectMutex);
    view = v;
}

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

bool PageRaster::ensureRendered(bool inAdvance) {
    // UI thread: `host` is only written on the UI thread (detach). Not under hostMutex: a render holds it.
    if (!host) {
        return false;
    }
    const RasterParams params = host->rasterParams();
    // What it must show: the part of the page in view (a page drawn in part; one out of view: any part)
    Rectangle<double> inView{0, 0, 0, 0};
    {
        std::lock_guard lock(repaintRectMutex);
        if (view) {
            inView = view->intersects(Rectangle<double>(0, 0, page->getWidth(), page->getHeight()))
                             .value_or(Rectangle<double>{0, 0, 0, 0});
        }
    }
    {
        std::lock_guard lock(drawingMutex);
        if (buffer.isInitialized()) {
            double scale = 1;
            cairo_surface_get_device_scale(cairo_get_target(buffer.get()), &scale, &scale);
            if (buffer.getZoom() == params.zoom && scale == params.dpiScale && placement.covers(inView)) {
                return false;
            }
        }
    }
    {
        std::lock_guard lock(repaintRectMutex);
        if (!this->rerenderComplete && rendering && rendering->zoom == params.zoom &&
            rendering->dpiScale == params.dpiScale && renderingPlacement.covers(inView)) {
            return false;  // being rendered so
        }
        this->rerenderComplete = true;
    }
    service->schedule(shared_from_this(), inAdvance ? RenderService::Priority::Preload : RenderService::Priority::Visible);
    return true;
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

void PageRaster::renderToBuffer(cairo_t* cr, const RasterParams&, bool background, bool whole) const {
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
        XojPdfPageSPtr direct = host->rasterPendingPdfPage(pdfPageNo);
        if (!direct && !whole) {
            // A big page drawn in part: the PDF drawn into the part directly. The PDF cache would draw the whole PDF
            // page at this zoom first (a poster at 300 %: more than a GB).
            std::shared_lock lock(*doc);
            direct = doc->getPdfPage(pdfPageNo);
        }
        if (XojPdfPageSPtr pending = direct) {
            cairo_save(cr);
            if (!space.empty()) {  // (space for notes: white paper, the PDF at its offset)
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_paint(cr);
                cairo_translate(cr, space.left, space.top);
                cairo_rectangle(cr, 0, 0, width - space.left - space.right, height - space.top - space.bottom);
                cairo_clip(cr);
            }
            pending->render(cr);  // (a pasted page: from the pasted PDF until the merged PDF is written; a part)
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

    Rectangle<double> part = rect;
    bool wholeBuffer = true;
    {
        // A page drawn in part: only what its buffer shows (the rest is drawn when the view gets there)
        std::lock_guard lock(this->drawingMutex);
        if (!this->buffer.isInitialized()) {
            return;
        }
        wholeBuffer = placement.whole;
        if (!placement.whole) {
            Rectangle<double> padded = rect;
            padded.x -= RENDER_PADDING;
            padded.y -= RENDER_PADDING;
            padded.width += 2 * RENDER_PADDING;
            padded.height += 2 * RENDER_PADDING;
            const auto shown = padded.intersects(placement.area);
            if (!shown) {
                return;
            }
            part = *shown;
        }
    }
    Range maskRange(part);
    maskRange.addPadding(RENDER_PADDING);
    xoj::view::Mask newMask = createMask(maskRange, params);

    renderToBuffer(newMask.get(), params, false, wholeBuffer);

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
    const double width = page->getWidth(), height = page->getHeight();
    Placement place;
    {
        std::lock_guard lock(repaintRectMutex);
        complete = std::exchange(this->rerenderComplete, false);
        resized = std::exchange(this->sizeChanged, false);
        rects = std::exchange(this->rerenderRects, {});
        if (complete) {
            place = placementFor(width, height, params, view);  // (where the view is now)
            rendering = params;
            renderingPlacement = place;
        }
    }

    if (complete) {
        const auto start = std::chrono::steady_clock::now();
        // (a part: its top left a quarter pixel in, so that the mask starts at that whole pixel)
        xoj::view::Mask newMask =
                place.whole ? createMask(Range(0, 0, width, height), params)
                            : createMask(Range((place.x + 0.25) / params.zoom, (place.y + 0.25) / params.zoom,
                                               place.area.x + place.area.width, place.area.y + place.area.height),
                                         params);
        renderToBuffer(newMask.get(), params, background, place.whole);
        if (cairo_surface_t* target = cairo_get_target(newMask.get());
            cairo_surface_get_type(target) == CAIRO_SURFACE_TYPE_IMAGE) {
            statPixels += static_cast<long long>(cairo_image_surface_get_width(target)) *
                          cairo_image_surface_get_height(target);
        }
        ++statRenders;
        statNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                             .count();
        {
            std::lock_guard lock(this->drawingMutex);
            std::swap(this->buffer, newMask);
            this->placement = place;
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
