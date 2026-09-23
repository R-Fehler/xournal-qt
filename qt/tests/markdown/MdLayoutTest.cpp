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

TEST(MdLayout, checkBoxesKnowTheirMarks) {
    const std::string src = "Tasks:\n\n- [ ] open\n- [x] done\n- no task\n";
    Style s;
    s.width = 400;
    const Layout l = layout(parse(src), s);
    ASSERT_EQ(l.checkBoxes.size(), 2u);
    EXPECT_EQ(src[l.checkBoxes[0].mark], ' ');
    EXPECT_FALSE(l.checkBoxes[0].checked);
    EXPECT_EQ(src[l.checkBoxes[1].mark], 'x');
    EXPECT_TRUE(l.checkBoxes[1].checked);
    const auto& b = l.checkBoxes[0];
    const auto hit = checkBoxAt(l, b.x + b.size / 2, b.y + b.size / 2);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->mark, b.mark);
    EXPECT_FALSE(checkBoxAt(l, b.x + 100, b.y + b.size / 2).has_value()) << "on the text: no";
    EXPECT_EQ(toggledTask(src, b.mark), "Tasks:\n\n- [x] open\n- [x] done\n- no task\n");
    EXPECT_EQ(toggledTask(src, l.checkBoxes[1].mark), "Tasks:\n\n- [ ] open\n- [ ] done\n- no task\n");
}

// Writing after a code block that ends the text (the cursor at the end): the paragraph being written is laid out
// as its source, below the code block, whose grey background ends at its closing fence.
TEST(MdLayout, writingAfterACodeBlockAtTheEnd) {
    Style s;
    s.width = 400;
    for (const std::string code: {std::string("```py\nsome code\n#stuff\n```"),
                                  std::string("```py\n\nsome code\n\n#stuff\n\n```")}) {
        const std::string src = "Intro\n\n" + code + "\n\nafter";
        const Layout l = layout(parse(src), s, src, src.size());
        ASSERT_GE(l.rawItem, 0) << code;
        const Item& raw = l.items[static_cast<size_t>(l.rawItem)];
        EXPECT_STREQ(pango_layout_get_text(raw.layout.get()), "after") << code;
        EXPECT_EQ(l.rawBegin, src.size() - 5) << code;
        const Item* fill = nullptr;
        for (const Item& it: l.items) {
            fill = it.kind == Item::Kind::Fill ? &it : fill;
        }
        ASSERT_NE(fill, nullptr);
        EXPECT_LE(fill->y + fill->height, raw.y) << code << ": the text is below the code block";
    }
    // Just after the closing fence (Enter pressed, nothing typed yet): the code is done and drawn as code; the
    // cursor's empty line is below its grey background, where the next paragraph goes
    const std::string src = "Intro\n\n```py\nsome code\n```\n\n";
    const Layout l = layout(parse(src), s, src, src.size());
    ASSERT_GE(l.rawItem, 0);
    const Item& raw = l.items[static_cast<size_t>(l.rawItem)];
    EXPECT_STREQ(pango_layout_get_text(raw.layout.get()), "");
    EXPECT_EQ(l.rawBegin, src.size());
    const Item* fill = nullptr;
    for (const Item& it: l.items) {
        fill = it.kind == Item::Kind::Fill ? &it : fill;
    }
    ASSERT_NE(fill, nullptr);
    EXPECT_LE(fill->y + fill->height, raw.y) << "the cursor's line is not in the code's background";
    const std::string typed = src + "a";
    const Layout after = layout(parse(typed), s, typed, typed.size());
    ASSERT_GE(after.rawItem, 0);
    EXPECT_DOUBLE_EQ(after.items[static_cast<size_t>(after.rawItem)].y, raw.y) << "the text goes where the cursor was";
}
