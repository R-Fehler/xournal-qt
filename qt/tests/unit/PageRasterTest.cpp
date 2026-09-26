/*
 * xournal-qt: the render service must produce exactly what upstream's RenderJob produces, i.e. a direct
 * DocumentView::drawPage into a Mask at the view's zoom, both for full and for partial re-renders.
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstring>
#include <memory>
#include <vector>

#include <glib.h>
#include <gtest/gtest.h>

#include "control/PdfCache.h"
#include "control/settings/Settings.h"
#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "util/Range.h"
#include "view/DocumentView.h"
#include "view/Mask.h"

#include "config-test.h"
#include "render/PageRaster.h"
#include "render/RenderService.h"

using namespace xqt;

namespace {
class TestHost: public RasterHost {
public:
    TestHost(Document* doc, Settings* settings, RasterParams params):
            doc(doc), cache(std::make_unique<PdfCache>(doc->getPdfDocument(), settings)), params(params) {}
    Document* rasterDocument() const override { return doc; }
    PdfCache* rasterPdfCache(bool) const override { return cache.get(); }
    RasterParams rasterParams() const override { return params; }
    void rasterUpdated(PageRaster*, std::optional<xoj::util::Rectangle<double>> area) override {
        ++updates;
        partialUpdates += area.has_value();
    }

    Document* doc;
    std::unique_ptr<PdfCache> cache;
    RasterParams params;
    std::atomic<int> updates{0};
    std::atomic<int> partialUpdates{0};
};

/// What upstream's RenderJob renders for a whole page (with a fresh PDF cache).
xoj::view::Mask referenceRender(Document* doc, Settings* settings, const PageRef& page, RasterParams p) {
    cairo_surface_t* tmpl = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_surface_set_device_scale(tmpl, p.dpiScale, p.dpiScale);
    xoj::view::Mask mask(tmpl, Range(0, 0, page->getWidth(), page->getHeight()), p.zoom, CAIRO_CONTENT_COLOR_ALPHA);
    cairo_surface_destroy(tmpl);
    PdfCache cache(doc->getPdfDocument(), settings);
    DocumentView view;
    view.setPdfCache(&cache);
    view.drawPage(page, mask.get(), false);
    return mask;
}

/// Number of differing pixels (-1 on size mismatch).
long compareSurfaces(cairo_surface_t* a, cairo_surface_t* b) {
    cairo_surface_flush(a);
    cairo_surface_flush(b);
    const int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
    if (w != cairo_image_surface_get_width(b) || h != cairo_image_surface_get_height(b)) {
        return -1;
    }
    long diff = 0;
    for (int y = 0; y < h; ++y) {
        const auto* ra = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(a) +
                                                           y * cairo_image_surface_get_stride(a));
        const auto* rb = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(b) +
                                                           y * cairo_image_surface_get_stride(b));
        for (int x = 0; x < w; ++x) {
            diff += ra[x] != rb[x];
        }
    }
    return diff;
}

long compareWithRaster(PageRaster& raster, xoj::view::Mask& reference) {
    return raster.withBuffer([&](xoj::view::Mask& buffer) -> long {
        if (!buffer.isInitialized()) {
            return -2;
        }
        return compareSurfaces(cairo_get_target(buffer.get()), cairo_get_target(reference.get()));
    });
}

void pumpUiThread() {
    while (g_main_context_iteration(nullptr, false)) {}
}

struct Loaded {
    std::unique_ptr<Document> doc;
};

Loaded load(const char8_t* relative) {
    LoadHandler handler;
    auto doc = handler.loadDocument(GET_TESTFILE(relative));
    EXPECT_TRUE(doc) << "cannot load fixture";
    return {std::move(doc)};
}
}  // namespace

class PageRasterTest: public ::testing::TestWithParam<std::tuple<const char8_t*, double, double>> {};

TEST_P(PageRasterTest, fullRenderMatchesUpstreamRenderJob) {
    const auto [file, zoom, dpi] = GetParam();
    Settings settings(fs::path{});
    auto loaded = load(file);
    ASSERT_TRUE(loaded.doc);
    TestHost host(loaded.doc.get(), &settings, RasterParams{zoom, dpi});
    RenderService service(3);

    std::vector<std::shared_ptr<PageRaster>> rasters;
    for (size_t i = 0; i < loaded.doc->getPageCount(); ++i) {
        rasters.push_back(std::make_shared<PageRaster>(&host, &service, loaded.doc->getPage(i)));
        rasters.back()->rerenderPage();
    }
    service.waitForIdle();
    pumpUiThread();
    EXPECT_EQ(host.updates, static_cast<int>(rasters.size()));

    for (auto& raster: rasters) {
        auto reference = referenceRender(loaded.doc.get(), &settings, raster->getPage(), host.params);
        EXPECT_EQ(compareWithRaster(*raster, reference), 0) << "page differs from upstream rendering";
    }
    for (auto& raster: rasters) {
        raster->detach();
    }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, PageRasterTest,
                         ::testing::Values(std::make_tuple(u8"test1.xoj", 1.0, 1.0),
                                           std::make_tuple(u8"test1.xoj", 1.7, 1.0),
                                           std::make_tuple(u8"load/text-fileversion-5.xopp", 1.3, 1.0),
                                           std::make_tuple(u8"load/image-fileversion-5.xopp", 2.0, 1.0),
                                           std::make_tuple(u8"packaged_xopp/pdfBackground/old.xopp", 1.5, 1.0),
                                           std::make_tuple(u8"packaged_xopp/pdfBackground/old.xopp", 1.0, 1.25)));

// Space for notes (qt/docs/note-space.md): the canvas draws the PDF at the page's offset, as every export does
TEST(PageRaster, aPageWithSpaceForNotesMatchesUpstreamsDrawing) {
    Settings settings(fs::path{});
    auto loaded = load(u8"packaged_xopp/pdfBackground/old.xopp");
    ASSERT_TRUE(loaded.doc);
    const PageRef page = loaded.doc->getPage(0);
    page->setNoteSpace(NoteSpace{37, 21, 50, 10});
    page->setSize(page->getWidth() + 87, page->getHeight() + 31);
    TestHost host(loaded.doc.get(), &settings, RasterParams{1.5, 1.0});
    RenderService service(1);
    auto raster = std::make_shared<PageRaster>(&host, &service, page);
    raster->rerenderPage();
    service.waitForIdle();
    pumpUiThread();
    auto reference = referenceRender(loaded.doc.get(), &settings, page, host.params);
    EXPECT_EQ(compareWithRaster(*raster, reference), 0);
    // And the PDF is not at the top left: the corner is the white space
    raster->withBuffer([&](xoj::view::Mask& buffer) {
        cairo_surface_t* s = cairo_get_target(buffer.get());
        cairo_surface_flush(s);
        const auto* px = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s));
        EXPECT_EQ(px[0], 0xffffffffU) << "white, opaque";
        return 0;
    });
    raster->detach();
}

TEST(PageRaster, aPdfBackgroundIsRenderedOnceForTheScreenScale) {
    // A page on a 2x screen at zoom 1 has exactly the pixels of the page at zoom 2 on a 1x screen. The PDF background
    // came out different: its buffer took the screen scale twice (4x the pixels, then scaled down when painted).
    Settings settings(fs::path{});
    auto loaded = load(u8"packaged_xopp/pdfBackground/old.xopp");
    ASSERT_TRUE(loaded.doc);
    const PageRef page = loaded.doc->getPage(0);
    ASSERT_TRUE(page->getBackgroundType().isPdfPage());
    auto onHiDpi = referenceRender(loaded.doc.get(), &settings, page, RasterParams{1.0, 2.0});
    auto zoomedIn = referenceRender(loaded.doc.get(), &settings, page, RasterParams{2.0, 1.0});
    EXPECT_EQ(compareSurfaces(cairo_get_target(onHiDpi.get()), cairo_get_target(zoomedIn.get())), 0)
            << "the PDF is rendered at the pixels the screen shows";
}

TEST(PageRaster, partialRerenderMatchesFullRender) {
    Settings settings(fs::path{});
    auto loaded = load(u8"packaged_xopp/pdfBackground/old.xopp");
    ASSERT_TRUE(loaded.doc);
    TestHost host(loaded.doc.get(), &settings, RasterParams{1.5, 1.0});
    RenderService service(2);
    auto page = loaded.doc->getPage(0);
    auto raster = std::make_shared<PageRaster>(&host, &service, page);
    raster->rerenderPage();
    service.waitForIdle();

    // Modify the page like a tool would (under the document lock), then re-render only the affected area.
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2.26);
    stroke->addPoint(Point(100, 120, 1.0));
    stroke->addPoint(Point(160, 180, 1.5));
    stroke->addPoint(Point(220, 150, 0.7));
    const auto box = stroke->getBoundingBox();
    loaded.doc->lock();
    page->getSelectedLayer()->addElement(std::move(stroke));
    loaded.doc->unlock();
    raster->rerenderRect(box.x, box.y, box.width, box.height);
    service.waitForIdle();
    pumpUiThread();
    EXPECT_EQ(host.partialUpdates, 1);

    auto reference = referenceRender(loaded.doc.get(), &settings, page, host.params);
    EXPECT_EQ(compareWithRaster(*raster, reference), 0) << "partial re-render differs from a full render";
    raster->detach();
}

// Posters (qt/page-sizes): a page too big to be drawn whole at the zoom is drawn around the view, each part exactly as
// the whole page has it there; strokes in the part are drawn into it, strokes elsewhere when the view gets there.
TEST(PageRaster, aBigPageIsDrawnInPartAsTheWholePageHasIt) {
    Settings settings(fs::path{});
    auto loaded = load(u8"test1.xoj");
    ASSERT_TRUE(loaded.doc);
    const PageRef page = loaded.doc->getPage(0);
    const RasterParams params{7.5, 1.0};  // (A4 at 7.5: 28 megapixels)
    ASSERT_FALSE(PageRaster::drawnWhole(page->getWidth(), page->getHeight(), params));
    TestHost host(loaded.doc.get(), &settings, params);
    RenderService service(1);
    auto raster = std::make_shared<PageRaster>(&host, &service, page);
    const xoj::util::Rectangle<double> view(200, 300, 800 / params.zoom, 600 / params.zoom);
    raster->setView(view);
    raster->rerenderPage();
    service.waitForIdle();
    pumpUiThread();

    auto addStroke = [&](double x, double y) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(3);
        stroke->addPoint(Point(x, y, -1));
        stroke->addPoint(Point(x + 40, y + 30, -1));
        const auto box = stroke->getBoundingBox();
        loaded.doc->lock();
        page->getSelectedLayer()->addElement(std::move(stroke));
        loaded.doc->unlock();
        raster->rerenderRect(box.x, box.y, box.width, box.height);
    };
    addStroke(view.x + 20, view.y + 20);                                  // in the part
    addStroke(page->getWidth() - 100, page->getHeight() - 100);           // far from it
    service.waitForIdle();
    pumpUiThread();

    auto reference = referenceRender(loaded.doc.get(), &settings, page, params);
    auto matchesReference = [&](const xoj::util::Rectangle<double>& mustShow) {
        return raster->withPlacedBuffer([&](xoj::view::Mask& buffer, const PageRaster::Placement& place) -> long {
            EXPECT_FALSE(place.whole);
            EXPECT_TRUE(place.covers(mustShow));
            if (!buffer.isInitialized()) {
                return -2;
            }
            cairo_surface_t* part = cairo_get_target(buffer.get());
            cairo_surface_t* whole = cairo_get_target(reference.get());
            cairo_surface_flush(part);
            cairo_surface_flush(whole);
            const int w = cairo_image_surface_get_width(part), h = cairo_image_surface_get_height(part);
            EXPECT_LE(static_cast<double>(w) * h, 4.0 * 800 * 600 + 4 * (800 + 600) + 4) << "twice the view each way";
            if (place.x + w > cairo_image_surface_get_width(whole) || place.y + h > cairo_image_surface_get_height(whole)) {
                return -1;
            }
            long diff = 0;
            for (int y = 0; y < h; ++y) {
                const auto* rp = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(part) +
                                                                   y * cairo_image_surface_get_stride(part));
                const auto* rw = reinterpret_cast<const uint32_t*>(
                        cairo_image_surface_get_data(whole) + (place.y + y) * cairo_image_surface_get_stride(whole));
                for (int x = 0; x < w; ++x) {
                    diff += rp[x] != rw[place.x + x];
                }
            }
            return diff;
        });
    };
    EXPECT_EQ(matchesReference(view), 0) << "the part around the view, with the stroke drawn into it";
    EXPECT_FALSE(raster->ensureRendered(false)) << "the view is in the part: nothing to draw";

    // The view moves to the bottom right corner (partly beside the page): drawn again there
    const xoj::util::Rectangle<double> corner(page->getWidth() - 60, page->getHeight() - 50, 800 / params.zoom,
                                              600 / params.zoom);
    raster->setView(corner);
    EXPECT_TRUE(raster->ensureRendered(false));
    service.waitForIdle();
    pumpUiThread();
    EXPECT_EQ(matchesReference(corner.intersects({0, 0, page->getWidth(), page->getHeight()}).value()), 0)
            << "the corner, with the stroke drawn there before";
    raster->withPlacedBuffer([&](xoj::view::Mask&, const PageRaster::Placement& place) {
        EXPECT_NEAR(place.area.x + place.area.width, page->getWidth(), 1e-9) << "moved into the page";
        EXPECT_NEAR(place.area.y + place.area.height, page->getHeight(), 1e-9);
        return 0;
    });
    raster->detach();
}

// A PDF poster zoomed in: its part is drawn from the PDF directly (the PDF cache would draw the whole PDF page at the
// zoom first), and looks as the whole page does there.
TEST(PageRaster, aPartOfABigPdfPageLooksAsTheWholePage) {
    Settings settings(fs::path{});
    auto loaded = load(u8"packaged_xopp/pdfBackground/old.xopp");
    ASSERT_TRUE(loaded.doc);
    const PageRef page = loaded.doc->getPage(0);
    ASSERT_TRUE(page->getBackgroundType().isPdfPage());
    const RasterParams params{7.5, 1.0};
    ASSERT_FALSE(PageRaster::drawnWhole(page->getWidth(), page->getHeight(), params));
    TestHost host(loaded.doc.get(), &settings, params);
    RenderService service(1);
    auto raster = std::make_shared<PageRaster>(&host, &service, page);
    raster->setView({page->getWidth() * 0.3, page->getHeight() * 0.2, 800 / params.zoom, 600 / params.zoom});
    raster->rerenderPage();
    service.waitForIdle();
    pumpUiThread();
    auto reference = referenceRender(loaded.doc.get(), &settings, page, params);
    long differing = 0, total = 0, inked = 0;
    raster->withPlacedBuffer([&](xoj::view::Mask& buffer, const PageRaster::Placement& place) {
        ASSERT_TRUE(buffer.isInitialized());
        EXPECT_FALSE(place.whole);
        cairo_surface_t* part = cairo_get_target(buffer.get());
        cairo_surface_t* whole = cairo_get_target(reference.get());
        cairo_surface_flush(part);
        cairo_surface_flush(whole);
        const int w = cairo_image_surface_get_width(part), h = cairo_image_surface_get_height(part);
        for (int y = 0; y < h; ++y) {
            const auto* rp = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(part) +
                                                               y * cairo_image_surface_get_stride(part));
            const auto* rw = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(whole) +
                                                               (place.y + y) * cairo_image_surface_get_stride(whole));
            for (int x = 0; x < w; ++x) {
                ++total;
                differing += rp[x] != rw[place.x + x];
                inked += rw[place.x + x] != 0 && rw[place.x + x] != 0xffffffffU;
            }
        }
    });
    EXPECT_GT(inked, 1000) << "the part has something of the PDF";
    EXPECT_LE(differing, total / 1000) << differing << " of " << total << " pixels differ";
    raster->detach();
}

TEST(PageRaster, aPartStartsOnAWholeDevicePixelWithinTheLimits) {
    const xoj::util::Rectangle<double> view(123.4, 567.8, 90, 60);
    for (const double dpi: {1.0, 1.25, 1.5, 1.75, 2.0}) {
        const RasterParams p{9.0, dpi};
        const auto place = PageRaster::placementFor(595, 842, p, view);
        EXPECT_FALSE(place.whole) << dpi;
        EXPECT_NEAR(place.x * dpi, std::round(place.x * dpi), 1e-9) << dpi << ": the tiles are on device pixels";
        EXPECT_NEAR(place.y * dpi, std::round(place.y * dpi), 1e-9) << dpi;
        EXPECT_TRUE(place.covers(view)) << dpi;
        EXPECT_GE(place.area.x, 0);
        EXPECT_LE(place.area.x + place.area.width, 595 + 1e-9);
        const double s = p.zoom * p.dpiScale;
        EXPECT_LE(place.area.width * s * place.area.height * s, PageRaster::WHOLE_PAGE_PIXELS);
    }
    // A page below the one in view: its top, where the view comes to it; above it: its bottom
    const RasterParams p{9.0, 1.0};
    EXPECT_DOUBLE_EQ(PageRaster::placementFor(595, 842, p, {{0, -300, 90, 60}}).area.y, 0);
    const auto above = PageRaster::placementFor(595, 842, p, {{0, 900, 90, 60}});
    EXPECT_NEAR(above.area.y + above.area.height, 842, 1e-9);
    // A view bigger than the limit (a huge screen): its middle, within the limit
    const auto huge = PageRaster::placementFor(5000, 5000, p, {{1000, 1000, 3000, 3000}});
    EXPECT_LE(huge.area.width * 9 * huge.area.height * 9, PageRaster::WHOLE_PAGE_PIXELS);
    EXPECT_LE(huge.area.width * 9, PageRaster::MAX_SIDE);
    EXPECT_TRUE(huge.area.x > 1000 && huge.area.x + huge.area.width < 4000) << "around the middle of the view";
    // Small enough: the whole page, whatever the view
    const auto whole = PageRaster::placementFor(595, 842, RasterParams{2.0, 2.0}, {{100, 100, 50, 50}});
    EXPECT_TRUE(whole.whole);
    EXPECT_EQ(whole.x, 0);
    EXPECT_DOUBLE_EQ(whole.area.width, 595);
}

TEST(PageRaster, detachedRasterIsNotRenderedAndNotNotified) {
    Settings settings(fs::path{});
    auto loaded = load(u8"test1.xoj");
    ASSERT_TRUE(loaded.doc);
    TestHost host(loaded.doc.get(), &settings, RasterParams{1.0, 1.0});
    RenderService service(1);
    auto raster = std::make_shared<PageRaster>(&host, &service, loaded.doc->getPage(0));
    service.blockRerenderZoom(std::chrono::milliseconds(50));
    raster->rerenderPage();
    raster->detach();  // the view goes away before the job ran
    service.waitForIdle();
    pumpUiThread();
    EXPECT_EQ(host.updates, 0);
    EXPECT_FALSE(raster->withBuffer([](xoj::view::Mask& m) { return m.isInitialized(); }));
}

TEST(PageRaster, concurrentEditsAndRendersStayConsistent) {
    // UI thread edits the page (exclusive document lock) while workers render it (shared lock); partial and full
    // re-render requests interleave. Run under -fsanitize=thread to check for data races.
    Settings settings(fs::path{});
    auto loaded = load(u8"packaged_xopp/pdfBackground/old.xopp");
    ASSERT_TRUE(loaded.doc);
    TestHost host(loaded.doc.get(), &settings, RasterParams{1.2, 1.0});
    RenderService service(4);
    auto page = loaded.doc->getPage(0);
    auto raster = std::make_shared<PageRaster>(&host, &service, page);
    raster->rerenderPage();

    for (int i = 0; i < 100; ++i) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(1.0 + (i % 5));
        stroke->addPoint(Point(20 + i * 4, 30 + (i % 17) * 20, 1.0));
        stroke->addPoint(Point(60 + i * 3, 50 + (i % 13) * 25, 0.6));
        const auto box = stroke->getBoundingBox();
        loaded.doc->lock();
        page->getSelectedLayer()->addElement(std::move(stroke));
        loaded.doc->unlock();
        if (i % 25 == 24) {
            raster->rerenderPage();
        } else {
            raster->rerenderRect(box.x, box.y, box.width, box.height);
        }
        if (i % 10 == 0) {
            pumpUiThread();
        }
    }
    service.waitForIdle();
    pumpUiThread();

    auto reference = referenceRender(loaded.doc.get(), &settings, page, host.params);
    EXPECT_EQ(compareWithRaster(*raster, reference), 0) << "final buffer differs after concurrent edits";
    raster->detach();
}

namespace {
/// A host whose renders wait at their start until they are let go, and which notes when a worker started one
class GateHost: public TestHost {
public:
    using TestHost::TestHost;
    RasterParams rasterParams() const override {
        if (std::this_thread::get_id() != ui) {  // (the UI thread asks too, when it schedules)
            started = true;
            std::unique_lock lock(m);
            cv.wait(lock, [&] { return open; });
        }
        return params;
    }
    void letGo() {
        {
            std::lock_guard lock(m);
            open = true;
        }
        cv.notify_all();
    }
    const std::thread::id ui = std::this_thread::get_id();
    mutable std::atomic<bool> started{false};
    mutable std::mutex m;
    mutable std::condition_variable cv;
    bool open = false;
};
}  // namespace

TEST(RenderService, pagesInAdvanceWaitForTheVisiblePages) {
    Settings settings(fs::path{});
    auto loaded = load(u8"test1.xoj");
    ASSERT_TRUE(loaded.doc);
    GateHost visibleHost(loaded.doc.get(), &settings, RasterParams{1.0, 1.0});
    GateHost aheadHost(loaded.doc.get(), &settings, RasterParams{1.0, 1.0});
    aheadHost.letGo();
    RenderService service(1, 1);
    auto visible = std::make_shared<PageRaster>(&visibleHost, &service, loaded.doc->getPage(0));
    auto ahead = std::make_shared<PageRaster>(&aheadHost, &service, loaded.doc->getPage(0));
    EXPECT_FALSE(RenderService::visiblePagesBusy());

    visible->ensureRendered(false);
    ahead->ensureRendered(true);
    for (int i = 0; i < 100 && !visibleHost.started; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(visibleHost.started);
    EXPECT_TRUE(RenderService::visiblePagesBusy()) << "others (previews, thumbnails) can see it";
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_FALSE(aheadHost.started) << "a page in advance was started while a visible one was rendered";

    visibleHost.letGo();
    RenderService::waitForVisiblePages(std::chrono::milliseconds(2000));
    EXPECT_FALSE(RenderService::visiblePagesBusy());
    service.waitForIdle();
    EXPECT_TRUE(aheadHost.started) << "then it is rendered";
    pumpUiThread();
    visible->detach();
    ahead->detach();
}
