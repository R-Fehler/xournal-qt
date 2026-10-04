/*
 * xournal-qt: the picture of an area of a page (render/RegionRender.h, qt/snip): what is in the area (ink, the PDF
 * background) at the place it has on the page, the lasso's shape (transparent outside), the size limit.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <cairo-pdf.h>
#include <cairo.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"

#include "render/RegionRender.h"

using namespace xqt;
using xoj::util::Rectangle;

namespace {
struct Pixel {
    int a, r, g, b;
};
Pixel pixel(cairo_surface_t* s, int x, int y) {
    cairo_surface_flush(s);
    const auto* row = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s);
    const uint32_t v = reinterpret_cast<const uint32_t*>(row)[x];
    return {static_cast<int>(v >> 24), static_cast<int>((v >> 16) & 0xff), static_cast<int>((v >> 8) & 0xff),
            static_cast<int>(v & 0xff)};
}
bool isWhite(const Pixel& p) { return p.a == 255 && p.r > 245 && p.g > 245 && p.b > 245; }
bool isBlack(const Pixel& p) { return p.a == 255 && p.r < 10 && p.g < 10 && p.b < 10; }
bool isRed(const Pixel& p) { return p.a == 255 && p.r > 240 && p.g < 15 && p.b < 15; }

/// A plain white page of 200 × 200 points with a black line 10 points wide from (50, 100) to (150, 100)
struct InkPage {
    DocumentHandler handler;
    Document doc{&handler};
    PageRef page;
    InkPage() {
        page = std::make_shared<XojPage>(200.0, 200.0);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        page->setBackgroundColor(Colors::white);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(10);
        stroke->setColor(Colors::black);
        stroke->addPoint(Point(50, 100, -1));
        stroke->addPoint(Point(150, 100, -1));
        page->getSelectedLayer()->addElement(std::move(stroke));
        doc.addPage(page);
    }
};

/// A PDF of one page, 200 × 200 points: white with a red square from (20, 20) to (80, 80)
std::filesystem::path writePdf() {
    gchar* dir = g_dir_make_tmp("xqt-region-XXXXXX", nullptr);
    const auto file = std::filesystem::path(dir) / "square.pdf";
    g_free(dir);
    cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), 200, 200);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_rectangle(cr, 20, 20, 60, 60);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    return file;
}
}  // namespace

TEST(RegionRender, theInkInTheAreaIsWhereItIsOnThePage) {
    InkPage p;
    region::Request r;
    r.area = Rectangle<double>(40, 80, 120, 40);
    r.scale = 2;
    auto s = region::render(p.doc, p.page, r);
    ASSERT_TRUE(s);
    EXPECT_EQ(cairo_image_surface_get_width(s.get()), 240);
    EXPECT_EQ(cairo_image_surface_get_height(s.get()), 80);
    // page (100, 100): the line; page (100, 85) and (45, 100): the paper
    EXPECT_TRUE(isBlack(pixel(s.get(), 120, 40)));
    EXPECT_TRUE(isWhite(pixel(s.get(), 120, 10)));
    EXPECT_TRUE(isWhite(pixel(s.get(), 6, 40)));
    // the line ends at x = 150 (round caps: 155), the area at 160
    EXPECT_TRUE(isBlack(pixel(s.get(), 2 * (145 - 40), 40)));
    EXPECT_TRUE(isWhite(pixel(s.get(), 2 * (158 - 40), 40)));
}

TEST(RegionRender, onlyThePartOnThePageIsDrawn) {
    InkPage p;
    region::Request r;
    r.area = Rectangle<double>(150, 150, 100, 100);  // half of it beside the page
    r.scale = 1;
    auto s = region::render(p.doc, p.page, r);
    ASSERT_TRUE(s);
    EXPECT_EQ(cairo_image_surface_get_width(s.get()), 50);
    EXPECT_EQ(cairo_image_surface_get_height(s.get()), 50);
    r.area = Rectangle<double>(300, 300, 50, 50);
    EXPECT_FALSE(region::render(p.doc, p.page, r)) << "nothing of the page: no picture";
}

TEST(RegionRender, theBackgroundAloneHasNoInk) {
    InkPage p;
    region::Request r;
    r.area = Rectangle<double>(40, 80, 120, 40);
    r.scale = 1;
    r.layers = false;
    auto s = region::render(p.doc, p.page, r);
    ASSERT_TRUE(s);
    EXPECT_TRUE(isWhite(pixel(s.get(), 60, 20)));
}

TEST(RegionRender, thePdfBackgroundIsInThePicture) {
    const auto file = writePdf();
    DocumentHandler handler;
    Document doc(&handler);
    ASSERT_TRUE(doc.readPdf(file, /*initPages=*/true, /*attachToDocument=*/false));
    ASSERT_EQ(doc.getPageCount(), 1u);
    const PageRef page = doc.getPage(0);
    ASSERT_TRUE(page->getBackgroundType().isPdfPage());

    region::Request r;
    r.area = Rectangle<double>(10, 10, 100, 100);
    r.scale = 300.0 / 72;
    auto s = region::render(doc, page, r);
    ASSERT_TRUE(s);
    const auto at = [&](double x, double y) {
        return pixel(s.get(), static_cast<int>((x - 10) * r.scale), static_cast<int>((y - 10) * r.scale));
    };
    EXPECT_TRUE(isRed(at(50, 50))) << "the square";
    EXPECT_TRUE(isRed(at(22, 22)));
    EXPECT_TRUE(isWhite(at(15, 15))) << "the paper before it";
    EXPECT_TRUE(isWhite(at(90, 90))) << "and after it";
    std::filesystem::remove_all(file.parent_path());
}

TEST(RegionRender, aLassoCutsThePictureToItsShape) {
    InkPage p;
    region::Request r;
    r.area = Rectangle<double>(0, 0, 200, 200);
    r.scale = 1;
    // a triangle: the top left half of the page
    r.outline = {{0, 0}, {200, 0}, {0, 200}};
    auto s = region::render(p.doc, p.page, r);
    ASSERT_TRUE(s);
    EXPECT_TRUE(isWhite(pixel(s.get(), 20, 20))) << "inside: the paper";
    EXPECT_TRUE(isBlack(pixel(s.get(), 60, 100))) << "inside: the line";
    EXPECT_EQ(pixel(s.get(), 180, 180).a, 0) << "outside: transparent";
    EXPECT_EQ(pixel(s.get(), 140, 100).a, 0) << "outside: the line is not there";
}

TEST(RegionRender, theScaleIsTheScreensOrTwoHundredDpiWithinTheLimit) {
    const Rectangle<double> small(0, 0, 100, 50);
    EXPECT_DOUBLE_EQ(region::scaleFor(small, 1.0), region::MIN_DPI / 72) << "at least 200 dpi";
    EXPECT_DOUBLE_EQ(region::scaleFor(small, 6.0), 6.0) << "the screen's when it shows more";
    // A big poster area: limited to about 4 megapixels, whatever the screen shows
    const Rectangle<double> big(0, 0, 2000, 1500);
    const double scale = region::scaleFor(big, 3.0);
    const double pixels = std::ceil(big.width * scale) * std::ceil(big.height * scale);
    EXPECT_LE(pixels, region::MAX_PIXELS);
    EXPECT_GE(pixels, region::MAX_PIXELS * 0.98);

    InkPage p;
    p.page->setSize(2000, 1500);
    region::Request r;
    r.area = big;
    r.scale = scale;
    auto s = region::render(p.doc, p.page, r);
    ASSERT_TRUE(s);
    EXPECT_LE(static_cast<double>(cairo_image_surface_get_width(s.get())) * cairo_image_surface_get_height(s.get()),
              region::MAX_PIXELS);
}
