/*
 * xournal-qt: the render service must produce exactly what upstream's RenderJob produces, i.e. a direct
 * DocumentView::drawPage into a Mask at the view's zoom, both for full and for partial re-renders.
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
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
