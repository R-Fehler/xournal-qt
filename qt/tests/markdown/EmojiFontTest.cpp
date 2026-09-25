/*
 * xournal-qt: colour emoji in Markdown and text boxes, on screen and in PDFs (qt/src/markdown/EmojiFont.h).
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-font.h>
#include <poppler.h>

#include "model/Font.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "util/Matrix.h"
#include "view/LayerView.h"
#include "view/View.h"

#include "EmojiFont.h"
#include "MdBox.h"
#include "MdLayout.h"

using namespace xqt;

namespace {

// 😄 👩‍💻 (a ZWJ sequence) 🇩🇪 (a flag) 👍🏽 (a skin tone) ❤️ (with VS16)
const std::string SMILE = "\xf0\x9f\x98\x84";
const std::string CODER = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
const std::string FLAG = "\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa";
const std::string THUMB = "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd";
const std::string HEART = "\xe2\x9d\xa4\xef\xb8\x8f";
const std::string ALL = SMILE + CODER + FLAG + THUMB + HEART;

md::Layout lay(const std::string& src, double size = 12) {
    md::Style s;
    s.size = size;
    s.width = 400;
    return md::layout(md::parse(src), s, src);
}

const md::Item& firstText(const md::Layout& l) {
    for (const md::Item& it: l.items) {
        if (it.kind == md::Item::Kind::Text) {
            return it;
        }
    }
    throw std::runtime_error("no text");
}

/// The runs of a laid-out text: its bytes, its font's family, and how many glyphs.
struct RunInfo {
    std::string text;
    std::string family;
    bool color = false;
    int glyphs = 0;
    std::string file;  ///< the font's file
};
std::vector<RunInfo> runsOf(PangoLayout* layout) {
    std::vector<RunInfo> out;
    const char* text = pango_layout_get_text(layout);
    PangoLayoutIter* it = pango_layout_get_iter(layout);
    do {
        PangoLayoutRun* run = pango_layout_iter_get_run_readonly(it);
        if (!run) {
            continue;
        }
        PangoFontDescription* d = pango_font_describe(run->item->analysis.font);
        FcChar8* file = nullptr;
        if (PANGO_IS_FC_FONT(run->item->analysis.font)) {
            FcPatternGetString(pango_fc_font_get_pattern(PANGO_FC_FONT(run->item->analysis.font)), FC_FILE, 0, &file);
        }
        out.push_back({std::string(text + run->item->offset, static_cast<size_t>(run->item->length)),
                       pango_font_description_get_family(d), emoji::isColorFont(run->item->analysis.font),
                       run->glyphs->num_glyphs, file ? reinterpret_cast<const char*>(file) : ""});
        pango_font_description_free(d);
    } while (pango_layout_iter_next_run(it));
    pango_layout_iter_free(it);
    return out;
}

bool colourful(const unsigned char* px) {  // (ARGB32, premultiplied: B G R A)
    const int b = px[0], g = px[1], r = px[2];
    return std::max({r, g, b}) - std::min({r, g, b}) > 80;
}

/// Pixels of a clear colour (not grey) in an image surface.
int colourPixels(cairo_surface_t* s) {
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    const int stride = cairo_image_surface_get_stride(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    int n = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            n += colourful(data + y * stride + x * 4);
        }
    }
    return n;
}

std::string tempPdf(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("xqt-emoji-" + std::to_string(getpid()) + "-" + name)).string();
}

/// The images of a PDF's first page: their sizes in pixels.
std::vector<std::pair<int, int>> imagesOf(const std::string& pdf) {
    std::vector<std::pair<int, int>> out;
    gchar* uri = g_filename_to_uri(pdf.c_str(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    if (!doc) {
        return out;
    }
    PopplerPage* page = poppler_document_get_page(doc, 0);
    GList* mapping = poppler_page_get_image_mapping(page);
    for (GList* l = mapping; l; l = l->next) {
        const auto* m = static_cast<PopplerImageMapping*>(l->data);
        if (cairo_surface_t* image = poppler_page_get_image(page, m->image_id)) {
            out.emplace_back(cairo_image_surface_get_width(image), cairo_image_surface_get_height(image));
            cairo_surface_destroy(image);
        }
    }
    poppler_page_free_image_mapping(mapping);
    g_object_unref(page);
    g_object_unref(doc);
    return out;
}

/// The PDF's first page drawn by poppler (as other viewers see it), `scale` pixels per point, on white.
cairo_surface_t* renderPdf(const std::string& pdf, double scale) {
    gchar* uri = g_filename_to_uri(pdf.c_str(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    PopplerPage* page = poppler_document_get_page(doc, 0);
    double w = 0, h = 0;
    poppler_page_get_size(page, &w, &h);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(w * scale),
                                                    static_cast<int>(h * scale));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render_for_printing(page, cr);
    cairo_destroy(cr);
    g_object_unref(page);
    g_object_unref(doc);
    return s;
}

}  // namespace

TEST(EmojiFont, theAppsFontIsRegistered) { EXPECT_TRUE(emoji::fontRegistered()); }

/// Every emoji of the text is drawn from the app's colour font, a sequence as one glyph.
TEST(EmojiFont, emojiAreTakenFromTheAppsColourFont) {
    const md::Layout l = lay("a " + SMILE + " " + CODER + " " + FLAG + " " + THUMB + " " + HEART + " b");
    int emojiRuns = 0;
    for (const RunInfo& r: runsOf(firstText(l).layout.get())) {
        const bool isEmoji = static_cast<unsigned char>(r.text[0]) >= 0xe2;
        if (!isEmoji) {
            EXPECT_FALSE(r.color) << r.text;
            continue;
        }
        ++emojiRuns;
        EXPECT_EQ(r.family, emoji::FONT_FAMILY) << r.text;
        EXPECT_TRUE(r.color) << r.text;
        // (the app's file, not a system's font of the same name)
        EXPECT_EQ(std::filesystem::path(r.file), std::filesystem::path(XQT_EMOJI_FONT)) << r.text;
    }
    EXPECT_GE(emojiRuns, 1);
    // A sequence (ZWJ, flag, skin tone, VS16) is one picture
    for (const std::string& e: {SMILE, CODER, FLAG, THUMB, HEART}) {
        const auto runs = runsOf(firstText(lay(e)).layout.get());
        ASSERT_EQ(runs.size(), 1u) << e;
        EXPECT_EQ(runs[0].glyphs, 1) << e;
    }
}

/// The emoji as drawn on a page (a picture looked at; XQT_EMOJI_GOLDEN_UPDATE=1 writes it anew). Only emoji: the
/// text fonts differ between systems, the emoji font comes with the app.
TEST(EmojiFont, goldenPicture) {
    const md::Layout l = lay(ALL, 24);
    const md::Item& it = firstText(l);
    const int w = static_cast<int>(std::ceil(it.width * 2)) + 4;
    const int h = static_cast<int>(std::ceil(it.height * 2)) + 4;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, 2, 2);
    cairo_translate(cr, 1, 1);
    md::draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    EXPECT_GT(colourPixels(s), w * h / 10) << "drawn in colour";

    const std::string path = std::string(XQT_MARKDOWN_GOLDEN) + "/emoji-row.png";
    if (std::getenv("XQT_EMOJI_GOLDEN_UPDATE")) {
        cairo_surface_write_to_png(s, path.c_str());
        cairo_surface_destroy(s);
        return;
    }
    cairo_surface_t* want = cairo_image_surface_create_from_png(path.c_str());
    ASSERT_EQ(cairo_surface_status(want), CAIRO_STATUS_SUCCESS) << path;
    ASSERT_EQ(cairo_image_surface_get_width(want), w);
    ASSERT_EQ(cairo_image_surface_get_height(want), h);
    // (the same up to how a Cairo scales the font's bitmaps down)
    const unsigned char* a = cairo_image_surface_get_data(s);
    const unsigned char* b = cairo_image_surface_get_data(want);
    const int stride = cairo_image_surface_get_stride(s);
    long sum = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w * 4; ++x) {
            sum += std::abs(a[y * stride + x] - b[y * stride + x]);
        }
    }
    EXPECT_LT(static_cast<double>(sum) / (w * h * 4), 6.0);
    cairo_surface_destroy(want);
    cairo_surface_destroy(s);
}

/// In a PDF, other viewers show the emoji in colour and sharp: each is a picture of about the font's resolution
/// (Cairo alone puts one of 16 pixels for 12 pt text), and one picture for all of the same emoji.
TEST(EmojiFont, pdfHasSharpColourPictures) {
    const std::string pdf = tempPdf("markdown.pdf");
    const md::Layout l = lay("Text " + SMILE + " and " + FLAG + " and " + SMILE + " again");
    cairo_surface_t* surface = cairo_pdf_surface_create(pdf.c_str(), 300, 100);
    cairo_t* cr = cairo_create(surface);
    cairo_translate(cr, 20, 20);
    md::draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);

    const auto images = imagesOf(pdf);
    ASSERT_EQ(images.size(), 3u) << "one picture per emoji drawn";
    for (const auto& [w, h]: images) {
        EXPECT_GE(w, 100);
        EXPECT_GE(h, 100);
    }
    // As poppler draws it at 300 dpi: the emoji in colour, where the text's line is
    cairo_surface_t* page = renderPdf(pdf, 300.0 / 72);
    EXPECT_GT(colourPixels(page), 2000);
    cairo_surface_destroy(page);
    if (!std::getenv("XQT_KEEP")) std::filesystem::remove(pdf);
}

/// A text box (upstream's TextView) through the painter of the seam: the same sharp pictures.
TEST(EmojiFont, textBoxesInPdfsToo) {
    md::installRenderer();
    Layer layer;
    auto t = std::make_unique<Text>();
    t->setText("Hi " + SMILE + " " + CODER);
    t->setFont(XojFont("Sans", 12));
    t->setColor(Color(0, 0, 0));
    t->setTransformation(xoj::util::Matrix::TRANSLATION(20, 20));
    layer.addElement(std::move(t));

    const std::string pdf = tempPdf("text.pdf");
    cairo_surface_t* surface = cairo_pdf_surface_create(pdf.c_str(), 300, 100);
    cairo_t* cr = cairo_create(surface);
    xoj::view::LayerView(&layer).draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    const auto images = imagesOf(pdf);
    ASSERT_EQ(images.size(), 2u);
    for (const auto& [w, h]: images) {
        EXPECT_GE(w, 100);
    }
    cairo_surface_t* page = renderPdf(pdf, 300.0 / 72);
    EXPECT_GT(colourPixels(page), 1000);
    cairo_surface_destroy(page);
    if (!std::getenv("XQT_KEEP")) std::filesystem::remove(pdf);
}

/// On the screen (an image surface), the layout is drawn by Pango as it is: nothing changes there.
TEST(EmojiFont, rastersAreDrawnByPangoAsTheyAre) {
    const md::Layout l = lay("x " + SMILE);
    auto draw = [&](bool painter) {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 40);
        cairo_t* cr = cairo_create(s);
        cairo_move_to(cr, 2, 2);
        if (painter) {
            emoji::showLayout(cr, firstText(l).layout.get());
        } else {
            pango_cairo_show_layout(cr, firstText(l).layout.get());
        }
        cairo_destroy(cr);
        cairo_surface_flush(s);
        return s;
    };
    cairo_surface_t* a = draw(true);
    cairo_surface_t* b = draw(false);
    EXPECT_EQ(0, std::memcmp(cairo_image_surface_get_data(a), cairo_image_surface_get_data(b),
                             static_cast<size_t>(cairo_image_surface_get_stride(a) * 40)));
    EXPECT_GT(colourPixels(a), 50);
    cairo_surface_destroy(a);
    cairo_surface_destroy(b);
}
