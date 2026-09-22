/*
 * xournal-qt: CPU raster of one page as shown by one document view.
 *
 * Port of the buffer handling of upstream XojPageView (gui/PageView.cpp: rerenderPage, rerenderRect) and of
 * RenderJob (control/jobs/RenderJob.cpp), without GTK:
 *  - a full re-render builds a new buffer at the current zoom and swaps it in; the old buffer stays displayable
 *    (scaled) until then, exactly like upstream;
 *  - partial re-renders render only the (merged) dirty rectangles and paint them onto the existing buffer;
 *  - tool views may draw directly onto the buffer (drawAndDeleteToolView) under the drawing mutex.
 * Differences to upstream (see qt/docs/adr/0002-upstream-seams.md):
 *  - fractional device pixel ratios are supported (scaled template surface instead of an integer DPI factor);
 *  - the PDF background is rendered without holding the document lock (PDF pages are immutable), so that a slow
 *    PDF render never blocks the UI thread when it takes the exclusive document lock (e.g. on pen-up).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <cairo.h>

#include "model/PageRef.h"
#include "util/Rectangle.h"
#include "view/Mask.h"

class Document;
class PdfCache;
class Range;

namespace xqt {

class PageRaster;
class RenderService;

/// Rendering parameters of a view (all pages of a view share them).
struct RasterParams {
    double zoom = 1.0;      ///< device independent pixels per page point
    double dpiScale = 1.0;  ///< physical pixels per device independent pixel (may be fractional)
};

/// Provided by the owner of the rasters (a document view / tab).
class RasterHost {
public:
    virtual ~RasterHost() = default;
    virtual Document* rasterDocument() const = 0;
    /// `background`: for a page rendered in advance (a background worker); it may use an instance of the PDF of its
    /// own, so that the visible pages do not wait for poppler (which renders one page of an instance at a time).
    virtual PdfCache* rasterPdfCache(bool background = false) const = 0;
    virtual RasterParams rasterParams() const = 0;
    virtual bool rasterMarkAudioStrokes() const { return false; }
    /// Called on the UI thread when (part of) the buffer changed. `area` in page coordinates, nullopt = whole page.
    virtual void rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) = 0;
};

class PageRaster: public std::enable_shared_from_this<PageRaster> {
public:
    PageRaster(RasterHost* host, RenderService* service, PageRef page);
    ~PageRaster();
    PageRaster(const PageRaster&) = delete;
    PageRaster& operator=(const PageRaster&) = delete;

    const PageRef& getPage() const { return page; }

    // --- UI thread ---------------------------------------------------------------------------------------------
    /// Re-render the whole page (zoom or page changed). Upstream: XojPageView::rerenderPage(sizeChanged).
    void rerenderPage(bool sizeChanged = false);
    /// Have a buffer at the host's current parameters: render the whole page unless it has one or is being rendered
    /// at them (`inAdvance`: a page that is not visible, rendered by a background worker).
    void ensureRendered(bool inAdvance);
    /// Re-render a part of the page (page coordinates). Upstream: XojPageView::rerenderRect.
    void rerenderRect(double x, double y, double width, double height);
    void rerenderRange(const Range& range);
    /// Drop the buffer (memory cleanup for pages far outside the viewport).
    void releaseBuffer();
    /// The host is going away: no more notifications. Called by the host before it is destroyed.
    void detach();

    /// Access the buffer under the drawing mutex: `f(xoj::view::Mask&)`. The mask may be uninitialized (no render
    /// yet) and may have a zoom different from the current one (a re-render is then pending).
    template <typename F>
    decltype(auto) withBuffer(F&& f) {
        std::lock_guard lock(drawingMutex);
        return f(buffer);
    }

    // --- worker thread -----------------------------------------------------------------------------------------
    /// Port of RenderJob::run(). Called by the RenderService, never concurrently for the same raster.
    void run(bool background = false);

private:
    void schedule();
    void renderToBuffer(cairo_t* cr, const RasterParams& params, bool background) const;
    xoj::view::Mask createMask(const Range& range, const RasterParams& params) const;
    void rerenderRectangle(const xoj::util::Rectangle<double>& rect, const RasterParams& params);
    void notifyUpdated(std::optional<xoj::util::Rectangle<double>> area);

    RasterHost* host;  ///< guarded by hostMutex; nullptr after detach()
    mutable std::mutex hostMutex;
    RenderService* service;
    PageRef page;

    xoj::view::Mask buffer;
    std::mutex drawingMutex;

    std::mutex repaintRectMutex;
    std::vector<xoj::util::Rectangle<double>> rerenderRects;
    bool rerenderComplete = false;
    bool sizeChanged = false;
    std::optional<RasterParams> rendering;  ///< a full render at these parameters is running
};

}  // namespace xqt
