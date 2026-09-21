#include <string>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "MdLayout.h"

using namespace xqt::md;

namespace {
Layout lay(const std::string& src, double width = 400) {
    Style s;
    s.width = width;
    return layout(parse(src), s);
}

const Item* firstText(const Layout& l, size_t block) {
    for (const Item& it: l.items) {
        if (it.block == block && it.kind == Item::Kind::Text) {
            return &it;
        }
    }
    return nullptr;
}
}  // namespace

TEST(MdLayout, BlocksGoDown) {
    const Layout l = lay("# Title\n\nA paragraph.\n\n- one\n- two\n");
    ASSERT_EQ(l.blocks.size(), 3u);
    EXPECT_DOUBLE_EQ(l.blocks[0].top, 0);  // no space above the first block
    EXPECT_GT(l.blocks[1].top, l.blocks[0].bottom);
    EXPECT_GT(l.blocks[2].top, l.blocks[1].bottom);
    EXPECT_GE(l.height, l.blocks[2].bottom - 0.01);
    // heading text is bigger than the paragraph's
    EXPECT_GT(firstText(l, 0)->height, firstText(l, 1)->height);
}

TEST(MdLayout, WrapsAtTheWidth) {
    const std::string text(400, 'x');
    const std::string words = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor ";
    const Layout wide = lay(words + words + words, 2000);
    const Layout narrow = lay(words + words + words, 150);
    EXPECT_GT(narrow.height, 3 * wide.height);
    for (const Item& it: narrow.items) {
        EXPECT_LE(it.x + it.width, 150.5);
    }
}

TEST(MdLayout, CommentsAreInvisible) {
    const Layout l = lay("<!-- xqt:cont -->\nHello\n");
    ASSERT_EQ(l.blocks.size(), 2u);
    const Item* hello = firstText(l, 1);
    ASSERT_NE(hello, nullptr);
    EXPECT_DOUBLE_EQ(hello->y, 0);  // as if the comment was not there
    EXPECT_EQ(firstText(l, 0), nullptr);
}

TEST(MdLayout, ListMarkersOnTheFirstLine) {
    const Layout l = lay("3. first\n4. second\n");
    int markers = 0;
    for (const Item& it: l.items) {
        if (it.kind == Item::Kind::Text) {
            const std::string t = pango_layout_get_text(it.layout.get());
            if (t == "3." || t == "4.") {
                ++markers;
            }
        }
    }
    EXPECT_EQ(markers, 2);
}

TEST(MdLayout, SameLayoutAtEveryCall) {
    const std::string src = "## Words\n\nSome *text* that wraps a few times at this width, with `code` in it.\n";
    const Layout a = lay(src, 180);
    const Layout b = lay(src, 180);
    ASSERT_EQ(a.items.size(), b.items.size());
    for (size_t i = 0; i < a.items.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.items[i].y, b.items[i].y);
        EXPECT_DOUBLE_EQ(a.items[i].height, b.items[i].height);
    }
}

TEST(MdLayout, DrawsInkOnlyInsideTheLayout) {
    const Layout l = lay("# Title\n\nSome text.\n", 300);
    const int w = 300;
    const int h = static_cast<int>(l.height) + 40;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    int inkInside = 0;
    int inkBelow = 0;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            const unsigned char alpha = data[yy * stride + xx * 4 + 3];
            if (alpha > 0) {
                (yy <= l.height + 1 ? inkInside : inkBelow)++;
            }
        }
    }
    cairo_surface_destroy(surface);
    EXPECT_GT(inkInside, 100);
    EXPECT_EQ(inkBelow, 0);
}

TEST(MdLayout, PdfKeepsTheTextAsText) {
    const Layout l = lay("# Title\n\nHello **bold** world.\n", 400);
    const std::string path = ::testing::TempDir() + "xqt-md-layout-test.pdf";
    cairo_surface_t* surface = cairo_pdf_surface_create(path.c_str(), 500, 300);
    cairo_t* cr = cairo_create(surface);
    cairo_translate(cr, 50, 50);
    draw(cr, l);
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
    EXPECT_NE(s.find("Title"), std::string::npos) << s;
    EXPECT_NE(s.find("Hello bold world."), std::string::npos) << s;
}
