/*
 * xournal-qt: Markdown boxes drawn through upstream's LayerView (the seam in MarkdownHook.h).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <string>

#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Font.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"
#include "view/LayerView.h"
#include "view/MarkdownHook.h"
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
    markdown.setName(std::string(xoj::view::MARKDOWN_LAYER_NAME));
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
    xoj::view::markdownTextRenderer.store(nullptr);
    const Ink upstream = drawLayer(markdown);
    EXPECT_EQ(upstream.top, raw.top);
    EXPECT_EQ(upstream.bottom, raw.bottom);
}

TEST_F(MdBoxTest, notCulledByTheBoundingBoxOfTheSource) {
    // The source is 3 short lines; drawn, the paragraph "B" is further down (below a heading 1 and its spacing)
    Layer markdown;
    markdown.setName(std::string(xoj::view::MARKDOWN_LAYER_NAME));
    auto box = makeBox("# A\n\nB", 50, 40);
    const auto rect = boxRect(*box);
    const double sourceBottom = box->getBoundingBox().y + box->getBoundingBox().height;
    ASSERT_GT(rect.y + rect.height, sourceBottom + 5) << "drawn further down than the source";
    markdown.addElement(std::move(box));

    // A tile below the source's box, where "B" is drawn
    const xoj::util::Rectangle<double> tile(0, sourceBottom + 1, 500, rect.y + rect.height - sourceBottom);
    EXPECT_GT(drawLayer(markdown, &tile).pixels, 0);
}

TEST_F(MdBoxTest, boxRectAndLayerLookup) {
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    EXPECT_EQ(markdownLayer(page), nullptr);
    auto* layer = new Layer();
    layer->setName(std::string(xoj::view::MARKDOWN_LAYER_NAME));
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
