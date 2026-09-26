/*
 * xournal-qt: pictures in the Markdown text (qt/docs/md-images.md): the image run, links to files, sizes, the cache,
 * drawing and pagination.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <fstream>
#include <string>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtest/gtest.h>

#include "MdDocument.h"
#include "MdImages.h"
#include "MdLayout.h"
#include "MdPaginate.h"
#include "filesystem.h"

using namespace xqt::md;

namespace {

/// A folder of its own for a test (removed at its end).
class TempDir {
public:
    TempDir() {
        static int n = 0;
        dir = fs::temp_directory_path() / ("xqt-md-images-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

/// A PNG of one color, w × h pixels.
void writePng(const fs::path& file, int w, int h, double r = 1, double g = 0, double b = 0) {
    fs::create_directories(file.parent_path());
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_destroy(cr);
    ASSERT_EQ(cairo_surface_write_to_png(s, file.string().c_str()), CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(s);
}

const Run* imageRun(const Block& b) {
    for (const Run& r: b.runs) {
        if (r.flags & Image) {
            return &r;
        }
    }
    for (const Block& c: b.children) {
        if (const xqt::md::Run* r = imageRun(c)) {
            return r;
        }
    }
    return nullptr;
}

Layout lay(const std::string& src, double width = 400, size_t active = NO_SOURCE) {
    Style s;
    s.width = width;
    return layout(parse(src), s, src, active);
}

std::vector<ImageHit> drawnPictures(const Layout& l) {
    std::vector<ImageHit> out;
    for (const ImageHit& h: imageRects(l)) {
        if (h.drawn) {
            out.push_back(h);
        }
    }
    return out;
}

std::string allText(const Layout& l) {
    std::string out;
    for (const Item& it: l.items) {
        if (it.kind == Item::Kind::Text) {
            out += pango_layout_get_text(it.layout.get());
            out += '\n';
        }
    }
    return out;
}

}  // namespace

// --- the image run --------------------------------------------------------------------------------------------

TEST(MdImages, ImageIsOneRunWithItsWholeSource) {
    const std::string src = "See ![a *cat*](pics/cat.png \"The title\") here.";
    const Document doc = parse(src);
    const xqt::md::Run* r = imageRun(doc.root);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->text, "a cat");
    EXPECT_EQ(src.substr(r->source, r->sourceLength), "![a *cat*](pics/cat.png \"The title\")");
    ASSERT_GE(r->link, 0);
    EXPECT_EQ(doc.links[static_cast<size_t>(r->link)], "pics/cat.png");
    // (the text around it is its own)
    EXPECT_EQ(plainText(doc.root.children[0]), "See a cat here.");
}

TEST(MdImages, EmptyAltAngleBracketsAndReferences) {
    {
        const std::string src = "![](notes.assets/image-2026-09-26-101112.png)";
        const Document doc = parse(src);
        const xqt::md::Run* r = imageRun(doc.root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->text, "");
        EXPECT_EQ(r->source, 0u);
        EXPECT_EQ(r->sourceLength, src.size());
        EXPECT_EQ(doc.links[static_cast<size_t>(r->link)], "notes.assets/image-2026-09-26-101112.png");
        // (the paragraph spans the image: pages are split around it)
        const auto spans = topLevelSpans(src, doc);
        ASSERT_EQ(spans.size(), 1u);
        EXPECT_EQ(spans[0].end, src.size());
    }
    {
        const std::string src = "![my pic](<my pictures/a b.png>) after";
        const Document doc = parse(src);
        const xqt::md::Run* r = imageRun(doc.root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(src.substr(r->source, r->sourceLength), "![my pic](<my pictures/a b.png>)");
        EXPECT_EQ(doc.links[static_cast<size_t>(r->link)], "my pictures/a b.png");
    }
    {
        const std::string src = "![alt][logo] and more\n\n[logo]: img/logo.svg";
        const Document doc = parse(src);
        const xqt::md::Run* r = imageRun(doc.root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(src.substr(r->source, r->sourceLength), "![alt][logo]");
        EXPECT_EQ(doc.links[static_cast<size_t>(r->link)], "img/logo.svg");
    }
    {
        // An image that is a link: the image's run, inside the link
        const std::string src = "[![badge](b.png)](https://example.org)";
        const Document doc = parse(src);
        const xqt::md::Run* r = imageRun(doc.root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(src.substr(r->source, r->sourceLength), "![badge](b.png)");
        EXPECT_TRUE(r->flags & Link);
    }
    {
        // Parentheses in the path, a title in parentheses
        const std::string src = "![x](a(1).png (title))";
        const Document doc = parse(src);
        const xqt::md::Run* r = imageRun(doc.root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->sourceLength, src.size());
        EXPECT_EQ(doc.links[static_cast<size_t>(r->link)], "a(1).png");
    }
}

// --- links to files -------------------------------------------------------------------------------------------

TEST(MdImages, LinkKinds) {
    using images::LinkKind;
    EXPECT_EQ(images::kindOf("a.png"), LinkKind::Local);
    EXPECT_EQ(images::kindOf("./a b.png"), LinkKind::Local);
    EXPECT_EQ(images::kindOf("/home/x/a.png"), LinkKind::Local);
    EXPECT_EQ(images::kindOf("C:\\x\\a.png"), LinkKind::Local);
    EXPECT_EQ(images::kindOf("file:///tmp/a.png"), LinkKind::Local);
    EXPECT_EQ(images::kindOf("https://example.org/a.png"), LinkKind::Web);
    EXPECT_EQ(images::kindOf("HTTP://example.org/a.png"), LinkKind::Web);
    EXPECT_EQ(images::kindOf("data:image/png;base64,AAAA"), LinkKind::Unsupported);
    EXPECT_EQ(images::percentDecoded("my%20pic%2x.png"), "my pic%2x.png");
    EXPECT_EQ(images::relativePath("./././a.png"), "a.png");
    EXPECT_EQ(images::relativePath("/abs.png"), "");
}

TEST(MdImages, RelativeLinksResolveInTheRoots) {
    TempDir t;
    const fs::path base = t.dir / "docs";
    writePng(base / "notes.assets" / "image 1.png", 4, 4);
    writePng(base / "pics" / "b.png", 4, 4);
    writePng(t.dir / "cache" / "notes.assets" / "cached.png", 4, 4);
    EXPECT_EQ(images::resolve("pics/b.png"), "");  // (no root: nothing)
    images::RootHandle root(images::Root{base.string(), "notes.assets", (base / "notes.assets").string()});
    EXPECT_EQ(images::resolve("pics/b.png"), (base / "pics" / "b.png").string());
    EXPECT_EQ(images::resolve("./pics/b.png"), (base / "pics" / "b.png").string());
    EXPECT_EQ(images::resolve("notes.assets/image%201.png"), (base / "notes.assets" / "image 1.png").string());
    EXPECT_EQ(images::resolve("notes.assets/image 1.png"), (base / "notes.assets" / "image 1.png").string());
    EXPECT_EQ(images::resolve((base / "pics" / "b.png").string()), (base / "pics" / "b.png").string());
    EXPECT_EQ(images::resolve("file://" + (base / "pics" / "b.png").string()), (base / "pics" / "b.png").string());
    EXPECT_EQ(images::resolve("pics/none.png"), "");
    {
        // A document whose assets are in the app cache (a PDF text document): its "name.assets/…" resolve there
        images::RootHandle cached(images::Root{"", "notes.assets", (t.dir / "cache" / "notes.assets").string()});
        EXPECT_EQ(images::resolve("notes.assets/cached.png"), (t.dir / "cache" / "notes.assets" / "cached.png").string());
        // (the other document's file is still found: the first root that has it)
        EXPECT_EQ(images::resolve("notes.assets/image 1.png"), (base / "notes.assets" / "image 1.png").string());
    }
    EXPECT_EQ(images::resolve("notes.assets/cached.png"), "");  // (its root is gone)
    // Web pictures: never a file unless fetched into the web cache
    EXPECT_EQ(images::resolve("https://example.org/a.png"), "");
    const uint64_t g = images::generation();
    root.reset();
    EXPECT_GT(images::generation(), g);
    EXPECT_EQ(images::resolve("pics/b.png"), "");
}

TEST(MdImages, InfoReadsTheSizeOnce) {
    TempDir t;
    writePng(t.dir / "a.png", 30, 20);
    std::ofstream(t.dir / "broken.png") << "not a picture";
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    const images::Info i = images::info("a.png");
    EXPECT_EQ(i.state, images::Info::State::Ok);
    EXPECT_EQ(i.width, 30);
    EXPECT_EQ(i.height, 20);
    EXPECT_EQ(images::info("missing.png").state, images::Info::State::Missing);
    EXPECT_EQ(images::info("broken.png").state, images::Info::State::Unreadable);
    EXPECT_EQ(images::info("https://example.org/x.png").state, images::Info::State::Web);
    EXPECT_EQ(images::info("data:image/png;base64,AA").state, images::Info::State::Unsupported);
}

// --- layout -----------------------------------------------------------------------------------------------------

TEST(MdImages, BlockImageScaledToTheColumn) {
    TempDir t;
    writePng(t.dir / "wide.png", 800, 400);   // 600 × 300 pt
    writePng(t.dir / "small.png", 80, 40);    // 60 × 30 pt
    writePng(t.dir / "tall.png", 400, 2000);  // 300 × 1500 pt
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    {
        const auto pics = drawnPictures(lay("Text\n\n![](wide.png)\n\nMore", 400));
        ASSERT_EQ(pics.size(), 1u);
        EXPECT_NEAR(pics[0].rect.width, 400, 0.1);
        EXPECT_NEAR(pics[0].rect.height, 200, 0.1);
        EXPECT_EQ(pics[0].link, "wide.png");
    }
    {
        // Never bigger than its natural size
        const auto pics = drawnPictures(lay("![](small.png)", 400));
        ASSERT_EQ(pics.size(), 1u);
        EXPECT_NEAR(pics[0].rect.width, 60, 0.01);
        EXPECT_NEAR(pics[0].rect.height, 30, 0.01);
    }
    {
        // No higher than 1.4 times the column: it fits a page
        const auto pics = drawnPictures(lay("![](tall.png)", 400));
        ASSERT_EQ(pics.size(), 1u);
        EXPECT_NEAR(pics[0].rect.height, 560, 0.1);
        EXPECT_NEAR(pics[0].rect.width, 112, 0.1);
    }
    {
        // A block image's paragraph is as high as the picture (no line spacing added)
        const Layout l = lay("![](wide.png)", 400);
        EXPECT_NEAR(l.height, 200, 1);
    }
}

TEST(MdImages, InlineImageIsAsHighAsTheLine) {
    TempDir t;
    writePng(t.dir / "icon.png", 64, 64);
    writePng(t.dir / "tiny.png", 8, 8);
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    Style s;
    s.size = 12;
    s.width = 400;
    const std::string src = "An icon ![i](icon.png) in a line, and ![t](tiny.png) a tiny one.";
    const Layout l = layout(parse(src), s, src);
    const auto pics = drawnPictures(l);
    ASSERT_EQ(pics.size(), 2u);
    EXPECT_NEAR(pics[0].rect.height, 12 * 1.2, 0.01);
    EXPECT_NEAR(pics[0].rect.width, 12 * 1.2, 0.01);
    EXPECT_NEAR(pics[1].rect.height, 6, 0.01);  // (8 px: 6 pt, not made bigger)
    // One line: the pictures are in the text
    ASSERT_EQ(l.items.size(), 1u);
    EXPECT_EQ(pango_layout_get_line_count(l.items[0].layout.get()), 1);
    // On the same baseline, a little below it (as the text's descent; less for a tiny one)
    const double bottom0 = pics[0].rect.y + pics[0].rect.height;
    const double bottom1 = pics[1].rect.y + pics[1].rect.height;
    EXPECT_NEAR(bottom0 - bottom1, 0.22 * 12 - 0.25 * 6, 0.01);
}

TEST(MdImages, MissingPictureShowsItsAltTextAndPath) {
    TempDir t;
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    const Layout l = lay("![A diagram](figures/none.png)");
    EXPECT_TRUE(drawnPictures(l).empty());
    EXPECT_NE(allText(l).find("A diagram figures/none.png"), std::string::npos) << allText(l);
    ASSERT_EQ(l.items.size(), 1u);
    ASSERT_EQ(l.items[0].images.size(), 1u);
    EXPECT_FALSE(l.items[0].images[0].drawn);
    // The file appears (written by the app): the next layout draws it
    writePng(t.dir / "figures" / "none.png", 10, 10);
    images::changed();
    EXPECT_EQ(drawnPictures(lay("![A diagram](figures/none.png)")).size(), 1u);
}

TEST(MdImages, WebPictureHasALoadButtonAndIsNotDrawn) {
    const Layout l = lay("![Logo](https://example.org/logo.png)");
    EXPECT_TRUE(drawnPictures(l).empty());
    EXPECT_NE(allText(l).find("Logo"), std::string::npos);
    EXPECT_NE(allText(l).find("Load image"), std::string::npos);
    const auto rects = imageRects(l);
    ASSERT_EQ(rects.size(), 1u);
    // The button is at the end of it
    const Item& it = l.items[0];
    const auto button = textRects(it, it.images[0].buttonStart, it.images[0].buttonEnd);
    ASSERT_EQ(button.size(), 1u);
    const auto hit = imageButtonAt(l, button[0].x + button[0].width / 2, button[0].y + button[0].height / 2);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->url, "https://example.org/logo.png");
    EXPECT_FALSE(imageButtonAt(l, 1, 1));  // (the alt text is no button)
}

TEST(MdImages, BlockBeingWrittenShowsItsSourceAndThePictureBelow) {
    TempDir t;
    writePng(t.dir / "wide.png", 800, 400);
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    const std::string src = "Text\n\n![](wide.png)\n\nMore";
    const size_t active = src.find("![");
    const Layout l = lay(src, 400, active + 3);
    ASSERT_GE(l.rawItem, 0);
    // (its text is its source, up to the blank line after it)
    EXPECT_EQ(std::string(pango_layout_get_text(l.items[static_cast<size_t>(l.rawItem)].layout.get())).rfind("![](wide.png)", 0), 0u);
    const auto pics = drawnPictures(l);
    ASSERT_EQ(pics.size(), 1u);
    EXPECT_NEAR(pics[0].rect.width, 400, 0.1);
    EXPECT_GE(pics[0].rect.y, l.items[static_cast<size_t>(l.rawItem)].y + l.items[static_cast<size_t>(l.rawItem)].height - 0.01);
}

// --- drawing and the cache --------------------------------------------------------------------------------------

TEST(MdImages, DrawnPixelsAndTheCache) {
    TempDir t;
    writePng(t.dir / "red.png", 200, 100, 1, 0, 0);
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    images::clearCache();
    const std::string src = "Before\n\n![](red.png)\n\nAfter";
    const Layout l = lay(src, 300);
    const auto pics = drawnPictures(l);
    ASSERT_EQ(pics.size(), 1u);
    const Rect r = pics[0].rect;
    EXPECT_NEAR(r.width, 150, 0.01);  // (200 px at 96 dpi)

    const auto render = [&](double scale) {
        const int w = static_cast<int>(300 * scale);
        const int h = static_cast<int>((l.height + 10) * scale);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        cairo_t* cr = cairo_create(s);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_scale(cr, scale, scale);
        draw(cr, l);
        cairo_destroy(cr);
        cairo_surface_flush(s);
        return s;
    };
    cairo_surface_t* s = render(1);
    const auto pixel = [](cairo_surface_t* s, double x, double y) {
        const unsigned char* d = cairo_image_surface_get_data(s);
        return *reinterpret_cast<const uint32_t*>(d + static_cast<int>(y) * cairo_image_surface_get_stride(s) +
                                                  static_cast<int>(x) * 4) &
               0xffffff;
    };
    EXPECT_EQ(pixel(s, r.x + r.width / 2, r.y + r.height / 2), 0xff0000u);
    EXPECT_EQ(pixel(s, r.x + 2, r.y + 2), 0xff0000u);
    EXPECT_EQ(pixel(s, r.x + r.width + 3, r.y + r.height / 2), 0xffffffu);  // (right of it: the page)
    EXPECT_EQ(pixel(s, r.x + r.width / 2, r.y + r.height + 3), 0xffffffu);  // (below it: until "After")
    cairo_surface_destroy(s);
    // At 1×, 150 pt are 150 px: the 200 px picture is decoded at its natural size (a halving would be too small)
    auto stats = images::cacheStats();
    EXPECT_EQ(stats.decoded, 1u);
    // Drawn again at the same zoom: from the cache
    cairo_surface_destroy(render(1));
    stats = images::cacheStats();
    EXPECT_EQ(stats.decoded, 1u);
    EXPECT_GE(stats.hits, 1u);
    // Zoomed out: a halving (100 × 50 px) is decoded once
    cairo_surface_destroy(render(0.5));
    cairo_surface_destroy(render(0.5));
    stats = images::cacheStats();
    EXPECT_EQ(stats.decoded, 2u);
    EXPECT_EQ(stats.entries, 2u);
    EXPECT_EQ(stats.bytes >= 200u * 100 * 4 + 100 * 50 * 4, true);
}

TEST(MdImages, CacheKeepsToItsLimit) {
    TempDir t;
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    images::clearCache();
    // 2000 × 2000 px: 16 MB decoded; five of them go over 64 MiB, four do not
    for (int i = 0; i < 5; ++i) {
        writePng(t.dir / ("big" + std::to_string(i) + ".png"), 2000, 2000);
        const images::Info info = images::info("big" + std::to_string(i) + ".png");
        ASSERT_TRUE(images::pixels(info, 2000, 2000));
    }
    const auto stats = images::cacheStats();
    EXPECT_LE(stats.bytes, images::CACHE_LIMIT);
    EXPECT_EQ(stats.entries, 4u);
}

TEST(MdImages, PdfCarriesAJpegAsItIsAndEachPictureOnce) {
    TempDir t;
    // A JPEG (gdk-pixbuf writes it)
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 120, 80);
    gdk_pixbuf_fill(pb, 0x3366ccff);
    ASSERT_TRUE(gdk_pixbuf_save(pb, (t.dir / "photo.jpg").string().c_str(), "jpeg", nullptr, nullptr));
    g_object_unref(pb);
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    const Layout l = lay("![](photo.jpg)\n\nand again ![x](photo.jpg)", 400);
    ASSERT_EQ(drawnPictures(l).size(), 2u);
    const fs::path pdf = t.dir / "out.pdf";
    cairo_surface_t* s = cairo_pdf_surface_create(pdf.string().c_str(), 500, 400);
    cairo_t* cr = cairo_create(s);
    draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_finish(s);
    cairo_surface_destroy(s);
    std::ifstream in(pdf, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::ifstream jin(t.dir / "photo.jpg", std::ios::binary);
    const std::string jpeg{std::istreambuf_iterator<char>(jin), std::istreambuf_iterator<char>()};
    if (std::getenv("XQT_KEEP_PDF")) {
        fs::copy_file(pdf, std::getenv("XQT_KEEP_PDF"), fs::copy_options::overwrite_existing);
    }
    EXPECT_NE(bytes.find("/DCTDecode"), std::string::npos);
    EXPECT_NE(bytes.find(jpeg), std::string::npos) << "the JPEG is in the PDF as it is";
    // (the inline one is the same file at another size: the same picture, once)
    EXPECT_EQ(bytes.find(jpeg), bytes.rfind(jpeg));
}

// --- pagination -------------------------------------------------------------------------------------------------

TEST(MdImages, TallImageFitsAPageAndIsNotSplit) {
    TempDir t;
    writePng(t.dir / "tall.png", 600, 3000);
    images::RootHandle root(images::Root{t.dir.string(), "", ""});
    std::string src;
    for (int i = 0; i < 12; ++i) {
        src += "A paragraph of text that takes a line or two on the page, number " + std::to_string(i) + ".\n\n";
    }
    src += "![a tall one](tall.png)\n\nThe end.\n";
    Style s;
    s.width = 481.6;
    const Pagination p = paginate(src, s, [](size_t) { return Frame{481.6, 728.6}; });
    EXPECT_EQ(p.overflow, 0);
    ASSERT_GE(p.slices.size(), 2u);
    // The picture is on a page whole: some slice has all of its Markdown
    int with = 0;
    for (const std::string& slice: p.slices) {
        with += slice.find("![a tall one](tall.png)") != std::string::npos;
        EXPECT_EQ(slice.find("![a tall one](tall.png)") == std::string::npos && slice.find("tall.png") != std::string::npos,
                  false);
    }
    EXPECT_EQ(with, 1);
    EXPECT_EQ(join(p.slices), src);
}

/// XQT_MD_IMAGES_SHOTS=<dir>: a text with pictures of every kind drawn as a PNG, to look at.
TEST(MdImages, Shots) {
    const char* dir = std::getenv("XQT_MD_IMAGES_SHOTS");
    if (!dir) {
        GTEST_SKIP() << "XQT_MD_IMAGES_SHOTS not set";
    }
    TempDir t;
    // A gradient with a frame (to see its edges and its scaling)
    {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 320);
        cairo_t* cr = cairo_create(s);
        cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 640, 320);
        cairo_pattern_add_color_stop_rgb(g, 0, 0.2, 0.4, 0.9);
        cairo_pattern_add_color_stop_rgb(g, 1, 0.9, 0.6, 0.2);
        cairo_set_source(cr, g);
        cairo_paint(cr);
        cairo_pattern_destroy(g);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, 8);
        cairo_rectangle(cr, 4, 4, 632, 312);
        cairo_stroke(cr);
        fs::create_directories(t.dir / "notes.assets");
        cairo_surface_write_to_png(s, (t.dir / "notes.assets" / "gradient.png").string().c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    writePng(t.dir / "icon.png", 48, 48, 0.1, 0.6, 0.2);
    images::RootHandle root(images::Root{t.dir.string(), "notes.assets", (t.dir / "notes.assets").string()});
    const std::string src = "# Pictures\n\nA block image:\n\n![](notes.assets/gradient.png)\n\n"
                            "An icon ![ok](icon.png) in a line of text, a missing one ![Diagram](figures/none.png), and "
                            "a web picture ![Logo](https://example.org/logo.png).\n\n- in a list ![ok](icon.png)\n";
    for (const size_t active: {NO_SOURCE, src.find("![](notes")}) {
        const Layout l = lay(src, 400, active);
        const double scale = 2;
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, static_cast<int>(440 * scale),
                                                        static_cast<int>((l.height + 40) * scale));
        cairo_t* cr = cairo_create(s);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, 20, 20);
        draw(cr, l);
        cairo_destroy(cr);
        const std::string name = active == NO_SOURCE ? "/md-images.png" : "/md-images-writing.png";
        cairo_surface_write_to_png(s, (std::string(dir) + name).c_str());
        cairo_surface_destroy(s);
    }
}
