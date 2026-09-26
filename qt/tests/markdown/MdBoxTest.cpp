/*
 * xournal-qt: Markdown texts drawn and sized through upstream's views (the seam in model/MarkdownText.h).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "model/Font.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"
#include "view/LayerView.h"
#include "model/MarkdownText.h"
#include "view/View.h"

#include "MdBox.h"

using namespace xqt::md;

namespace {
std::unique_ptr<Text> makeBox(const std::string& source, double x = 50, double y = 40) {
    auto t = std::make_unique<Text>();
    t->setText(source);
    t->setFont(XojFont("Sans", 12));
    t->setColor(Color(0, 0, 0));
    t->setWrap(400);
    t->setTransformation(xoj::util::Matrix::TRANSLATION(x, y));
    return t;
}

/// Rows with ink when the layer is drawn (only inside `clip`, if given).
struct Ink {
    int top = -1;
    int bottom = -1;
    int pixels = 0;
};
Ink drawLayer(const Layer& layer, const xoj::util::Rectangle<double>* clip = nullptr) {
    const int w = 500;
    const int h = 400;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    if (clip) {
        cairo_rectangle(cr, clip->x, clip->y, clip->width, clip->height);
        cairo_clip(cr);
    }
    xoj::view::LayerView(&layer).draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    Ink ink;
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (data[y * stride + x * 4 + 3] > 0) {
                ink.top = ink.top < 0 ? y : ink.top;
                ink.bottom = y;
                ++ink.pixels;
            }
        }
    }
    cairo_surface_destroy(surface);
    return ink;
}

class MdBoxTest: public ::testing::Test {
protected:
    void SetUp() override { installRenderer(); }
    void TearDown() override { installRenderer(); }
};
}  // namespace

TEST_F(MdBoxTest, styleComesFromTheText) {
    auto t = makeBox("x");
    t->setFont(XojFont("Serif Bold", 14));
    t->setColor(Color(0x12, 0x34, 0x56));
    const Style s = styleOf(*t);
    EXPECT_EQ(s.family, "Serif");
    EXPECT_EQ(s.size, 14);
    EXPECT_EQ(s.color, Color(0x12, 0x34, 0x56));
    EXPECT_EQ(s.width, 400);
    t->setWrap(Text::NO_WRAP);
    EXPECT_EQ(styleOf(*t).width, DEFAULT_WIDTH);
}

TEST_F(MdBoxTest, markdownLayerIsDrawnFormatted) {
    Layer markdown;
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));
    markdown.addElement(makeBox("# Big heading"));
    Layer plain;
    plain.addElement(makeBox("# Big heading"));

    const Ink formatted = drawLayer(markdown);
    const Ink raw = drawLayer(plain);
    ASSERT_GT(formatted.pixels, 0);
    ASSERT_GT(raw.pixels, 0);
    // A heading 1 is twice the body size and has a rule below; the raw text is one line of 12 pt
    EXPECT_GT(formatted.bottom - formatted.top, 1.8 * (raw.bottom - raw.top));

    // Without a renderer (upstream Xournal++): the source as it is
    xoj::markdown::renderer.store(nullptr);
    xoj::markdown::sizer.store(nullptr);
    markdown.getElements().front()->move(0, 0);  // (sizes again)
    const Ink upstream = drawLayer(markdown);
    EXPECT_EQ(upstream.top, raw.top);
    EXPECT_EQ(upstream.bottom, raw.bottom);
}

// A page draws its layers one after the other into one Cairo context (the canvas, the export, the hybrid PDF's
// layers). A text box drawn after a Markdown box is where it is, not where the box's last line was (Pango draws a
// layout at the current point, and drawing a box must not leave one behind).
TEST_F(MdBoxTest, aTextAfterAMarkdownBoxIsDrawnWhereItIs) {
    Layer markdown;
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));
    markdown.addElement(makeBox("Some text", 50, 40));
    Layer plain;
    plain.addElement(makeBox("Hello", 50, 300));

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 500, 400);
    cairo_t* cr = cairo_create(surface);
    xoj::view::LayerView(&markdown).draw(xoj::view::Context::createDefault(cr));
    EXPECT_FALSE(cairo_has_current_point(cr)) << "no current point left behind";
    xoj::view::LayerView(&plain).draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    int below = 0;  // (ink of the text box: rows 290 to 330)
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 290; y < 330; ++y) {
        for (int x = 0; x < 500; ++x) {
            below += data[y * stride + x * 4 + 3] > 0;
        }
    }
    cairo_surface_destroy(surface);
    EXPECT_GT(below, 50) << "the text box is drawn at its place";
}

TEST_F(MdBoxTest, theBoundingBoxIsWhatIsDrawn) {
    // The source is 3 short lines; drawn, the paragraph "B" is further down (below a heading 1 and its spacing)
    Layer markdown;
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));
    auto box = makeBox("# A\n\nB", 50, 40);
    EXPECT_FALSE(box->isMarkdown()) << "not in a Markdown layer yet";
    const double sourceBottom = box->getBoundingBox().y + box->getBoundingBox().height;
    const auto rect = boxRect(*box);
    ASSERT_GT(rect.y + rect.height, sourceBottom + 5) << "drawn further down than the source";
    Text* inLayer = box.get();
    markdown.addElement(std::move(box));
    EXPECT_TRUE(inLayer->isMarkdown()) << "the layer makes it one";
    const auto bounds = inLayer->getBoundingBox();
    EXPECT_NEAR(bounds.x, rect.x, 0.01);
    EXPECT_NEAR(bounds.y, rect.y, 0.01);
    EXPECT_NEAR(bounds.width, rect.width, 0.01);
    EXPECT_NEAR(bounds.height, rect.height, 0.01) << "selection, hit tests and repaints use the drawn box";
    markdown.setName("Other");
    EXPECT_FALSE(inLayer->isMarkdown()) << "renamed: an ordinary text again";
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));

    // A tile below the source's box, where "B" is drawn
    const xoj::util::Rectangle<double> tile(0, sourceBottom + 1, 500, rect.y + rect.height - sourceBottom);
    EXPECT_GT(drawLayer(markdown, &tile).pixels, 0);
}

TEST_F(MdBoxTest, boxRectAndLayerLookup) {
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    EXPECT_EQ(markdownLayer(page), nullptr);
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().push_back(layer);  // (the page owns it)
    EXPECT_EQ(markdownLayer(page), layer);
    EXPECT_EQ(boxOf(*layer), nullptr);
    layer->addElement(makeBox("Some text", 60, 70));
    const Text* box = boxOf(*layer);
    ASSERT_NE(box, nullptr);
    const auto r = boxRect(*box);
    EXPECT_DOUBLE_EQ(r.x, 60);
    EXPECT_DOUBLE_EQ(r.y, 70);
    EXPECT_DOUBLE_EQ(r.width, 400);
    EXPECT_NEAR(r.height, contentHeight(*box), 1e-9);
    EXPECT_GT(r.height, 10);
}

TEST_F(MdBoxTest, formulasOnPagesAndInTheirPdf) {
    // A box with formulas, drawn as the page renderer draws it (LayerView: canvas, thumbnails, PDF export, the
    // hybrid PDF's annotations), into a PDF
    Layer markdown;
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));
    markdown.addElement(makeBox("Area $A = \\pi r^2$ here.\n\n$$\\int_0^1 x\\,dx = \\frac{1}{2}$$\n", 30, 40));
    const auto* box = static_cast<const Text*>(markdown.getElements().front().get());

    const std::string path = ::testing::TempDir() + "xqt-md-box-math.pdf";
    cairo_surface_t* surface = cairo_pdf_surface_create(path.c_str(), 500, 400);
    cairo_t* cr = cairo_create(surface);
    xoj::view::LayerView(&markdown).draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    ASSERT_NE(doc, nullptr);
    PopplerPage* page = poppler_document_get_page(doc, 0);
    gchar* text = poppler_page_get_text(page);
    const std::string s = text;
    g_free(text);
    g_object_unref(page);
    g_object_unref(doc);
    EXPECT_NE(s.find("Area"), std::string::npos) << s;
    EXPECT_EQ(s.find("\\pi"), std::string::npos) << s;  // the formula, not its source
    EXPECT_EQ(s.find("frac"), std::string::npos) << s;

    // The search finds the source and marks the formula, on the page
    const auto shown = shownTexts(*box);
    ASSERT_FALSE(shown.empty());
    EXPECT_NE(shown[0].find("A = \\pi r^2"), std::string::npos);
    const auto found = findText(*box, "\\pi");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_GT(found[0].x, 30);
    EXPECT_GE(found[0].y, 40);
    const auto hit = mathAt(*box, found[0].x + found[0].width / 2, found[0].y + found[0].height / 2);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->span.tex, "A = \\pi r^2");
    const int from = static_cast<int>(shown[0].find("\\pi"));
    const auto rects = shownRects(*box, 0, from, from + 3);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_NEAR(rects[0].x, found[0].x, 0.01);
}
