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
 *    PDF render never blocks the UI thread when it takes the exclusive document lock (e.g. on pen-up);
 *  - a page whose picture at the zoom would be too big (more than WHOLE_PAGE_PIXELS, or a side longer than
 *    MAX_SIDE: an A0 poster at 100 %, any page zoomed in far) is drawn in part: the part of it in view with half the
 *    view's size around it ("windowed buffer", ROADMAP R5). The host tells where the view is (setView); when the
 *    part in view leaves what is drawn, the part around it is drawn anew. The buffer's place on the page is its
 *    Placement.
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
#include "pdf/base/XojPdfPage.h"
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
    /// A PDF page that is not in the document's PDF yet (pasted pages being merged, see PdfPageKeeper): drawn from
    /// this one meanwhile (any thread; nullptr: none).
    virtual XojPdfPageSPtr rasterPendingPdfPage(size_t) const { return nullptr; }
    /// Called on the UI thread when (part of) the buffer changed. `area` in page coordinates, nullopt = whole page.
    virtual void rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) = 0;
};

class PageRaster: public std::enable_shared_from_this<PageRaster> {
public:
    /// Up to this many pixels (96 MB) the whole page is drawn; above, the part around the view
    static constexpr double WHOLE_PAGE_PIXELS = 24.0 * 1024 * 1024;
    /// At most this many pixels a side (cairo makes up to 32767)
    static constexpr double MAX_SIDE = 16384;

    /// Where the buffer is on the page: the whole page, or a part of it. `x`, `y`: its top left in pixels of the
    /// buffer's zoom (device independent: page coordinates times zoom), `area` in page coordinates.
    struct Placement {
        bool whole = true;
        int x = 0;
        int y = 0;
        xoj::util::Rectangle<double> area{0, 0, 0, 0};
        /// It shows all of this part of the page (page coordinates; an empty part: yes)
        bool covers(const xoj::util::Rectangle<double>& part) const;
    };
    /// Whether a page of this size is drawn whole at these parameters
    static bool drawnWhole(double width, double height, const RasterParams& params);
    /// What is drawn of a page of this size: all of it, or where the view is (`view`: the view's rectangle in page
    /// coordinates, possibly beside the page) with half the view's size around it, moved into the page and within
    /// the limits. Its top left is on a whole device pixel.
    static Placement placementFor(double width, double height, const RasterParams& params,
                                  const std::optional<xoj::util::Rectangle<double>>& view);
    /// Counters of the full renders of all rasters (tests, benchmarks): how many, their pixels, their time (ns)
    struct Stats {
        long long renders = 0;
        long long pixels = 0;
        long long nanos = 0;
    };
    static Stats stats();

    PageRaster(RasterHost* host, RenderService* service, PageRef page);
    ~PageRaster();
    PageRaster(const PageRaster&) = delete;
    PageRaster& operator=(const PageRaster&) = delete;

    const PageRef& getPage() const { return page; }
    /// Whether this thread is drawing a page for the screen right now: what only the screen shows (a sticky note that
    /// peeks, the corner of a covering one) asks this. Thumbnails, previews and exports are not for the screen.
    static bool drawingForScreen();

    // --- UI thread ---------------------------------------------------------------------------------------------
    /// Re-render the whole page (zoom or page changed). Upstream: XojPageView::rerenderPage(sizeChanged).
    void rerenderPage(bool sizeChanged = false);
    /// Have a buffer at the host's current parameters: render the page unless it has one or is being rendered at
    /// them (`inAdvance`: a page that is not visible, rendered by a background worker). A page drawn in part is drawn
    /// again when what it shows does not cover the part of it in view. Returns whether a render was asked for.
    bool ensureRendered(bool inAdvance);
    /// Where the view is (the view's rectangle in page coordinates; beside the page when the page is not in view):
    /// what a page drawn in part draws, and what it must cover while it is in view.
    void setView(const xoj::util::Rectangle<double>& view);
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
    /// The same with where the buffer is on the page: `f(xoj::view::Mask&, const Placement&)`.
    template <typename F>
    decltype(auto) withPlacedBuffer(F&& f) {
        std::lock_guard lock(drawingMutex);
        return f(buffer, placement);
    }

    // --- worker thread -----------------------------------------------------------------------------------------
    /// Port of RenderJob::run(). Called by the RenderService, never concurrently for the same raster.
    void run(bool background = false);

private:
    void schedule();
    /// `whole`: the whole page (else a part of a big page: its PDF is drawn directly, not through the PDF cache)
    void renderToBuffer(cairo_t* cr, const RasterParams& params, bool background, bool whole) const;
    xoj::view::Mask createMask(const Range& range, const RasterParams& params) const;
    void rerenderRectangle(const xoj::util::Rectangle<double>& rect, const RasterParams& params);
    void notifyUpdated(std::optional<xoj::util::Rectangle<double>> area);

    RasterHost* host;  ///< guarded by hostMutex; nullptr after detach()
    mutable std::mutex hostMutex;
    RenderService* service;
    PageRef page;

    xoj::view::Mask buffer;
    Placement placement;  ///< where the buffer is on the page (with the buffer, under drawingMutex)
    std::mutex drawingMutex;

    std::mutex repaintRectMutex;
    std::vector<xoj::util::Rectangle<double>> rerenderRects;
    bool rerenderComplete = false;
    bool sizeChanged = false;
    std::optional<RasterParams> rendering;  ///< a full render at these parameters is running
    Placement renderingPlacement;           ///< ... of this part of the page
    std::optional<xoj::util::Rectangle<double>> view;  ///< where the view is (page coordinates; setView)
};

}  // namespace xqt
