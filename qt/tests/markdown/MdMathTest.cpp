#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <cairo.h>
#include <gtest/gtest.h>

#include "MdMath.h"

using namespace xqt::md;

namespace {
/// The rows and columns with ink of a formula drawn at `size` (black on white; the image is 4 em wide, 3 em high,
/// the baseline at 2 em).
struct Ink {
    int left = -1;
    int right = -1;
    int top = -1;
    int bottom = -1;
    int pixels = 0;
};
Ink inkOf(const math::Formula& f, double size, cairo_surface_t** keep = nullptr) {
    const int w = static_cast<int>(std::ceil(std::max(4.0, f.width + 1) * size));
    const int h = static_cast<int>(3 * size);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    math::draw(cr, f, 0.5 * size, 2 * size, size);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    Ink ink;
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
            if ((px & 0xff) < 128) {
                ++ink.pixels;
                ink.left = ink.left < 0 ? x : std::min(ink.left, x);
                ink.right = std::max(ink.right, x);
                ink.top = ink.top < 0 ? y : std::min(ink.top, y);
                ink.bottom = std::max(ink.bottom, y);
            }
        }
    }
    if (keep) {
        *keep = s;
    } else {
        cairo_surface_destroy(s);
    }
    return ink;
}
}  // namespace

TEST(MdMath, LaysOutAFormula) {
    const auto f = math::formula("x^2", false);
    ASSERT_TRUE(f->ok) << f->error;
    EXPECT_GT(f->width, 0.5);
    EXPECT_LT(f->width, 2);
    EXPECT_GT(f->ascent, 0.5);  // (x and the 2 above it)
    EXPECT_LT(f->ascent, 1.2);
    EXPECT_GE(f->descent, 0);
    EXPECT_LT(f->descent, 0.3);
    EXPECT_FALSE(f->shapes.empty());
    // Drawn where it says it is: from the baseline's left end, up to its ascent
    const Ink ink = inkOf(*f, 40);
    ASSERT_GT(ink.pixels, 50);
    EXPECT_NEAR(ink.left, 20, 4);
    EXPECT_LE(ink.right, 20 + f->width * 40 + 2);  // (the advance: a little room after the ink)
    EXPECT_GE(ink.right, 20 + f->width * 40 - 8);
    EXPECT_GE(ink.top, 80 - f->ascent * 40 - 2);
    EXPECT_LE(ink.bottom, 80 + f->descent * 40 + 2);
}

TEST(MdMath, DisplayStyleIsBigger) {
    const auto inl = math::formula(R"(\sum_{i=1}^n \frac{1}{i})", false);
    const auto display = math::formula(R"(\sum_{i=1}^n \frac{1}{i})", true);
    ASSERT_TRUE(inl->ok && display->ok);
    EXPECT_GT(display->ascent + display->descent, 1.3 * (inl->ascent + inl->descent));
}

TEST(MdMath, ErrorsDoNotThrow) {
    for (const char* bad: {R"(\frac{1}{)", R"(\begin{matrix} a & b)", R"(\unknowncommand{x})", "}", "{{{", "", "   ",
                           R"(\left( x)", R"(x^^2)", R"(\sqrt[)", R"(\color{nocolor}{x})", R"(\end{matrix})"}) {
        const auto f = math::formula(bad, false);
        if (!f->ok) {
            EXPECT_FALSE(f->error.empty()) << bad;
            EXPECT_TRUE(f->shapes.empty()) << bad;
        }
    }
    EXPECT_FALSE(math::formula("", false)->ok);
    EXPECT_FALSE(math::formula("}", false)->ok);  // (MicroTeX forgives much, e.g. a missing argument)
    EXPECT_FALSE(math::formula(R"(\hline)", false)->ok);
    EXPECT_FALSE(math::formula("{{{", false)->ok);  // (nothing to draw)
    // Nested too deeply, too long: not given to MicroTeX
    EXPECT_FALSE(math::formula(std::string(200, '{') + "x" + std::string(200, '}'), false)->ok);
    EXPECT_FALSE(math::formula(std::string(20000, 'x'), false)->ok);
}

TEST(MdMath, ManyKindsOfFormulas) {
    // Fractions, sums, matrices, Greek, roots, accents, \text, colors, arrays
    for (const char* tex: {R"(\frac{a+b}{c-d})", R"(\sum_{k=0}^{\infty} \frac{x^k}{k!} = e^x)",
                           R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix})", R"(\alpha\beta\gamma\Omega\pi)",
                           R"(\sqrt[3]{x^2+1})", R"(\hat{x}\vec{v}\bar{y})", R"(\text{if } x > 0)",
                           R"(\textcolor{red}{x} + y)", R"(\int_0^1 f(x)\,dx)", R"(\lim_{n\to\infty} a_n)",
                           R"(\left( \frac{1}{2} \right))", R"(\mathbb{R}^n \times \mathcal{C})", "ä + ü"}) {
        const auto f = math::formula(tex, true);
        EXPECT_TRUE(f->ok) << tex << ": " << f->error;
        EXPECT_GT(f->width, 0.1) << tex;
        EXPECT_GT(inkOf(*f, 30).pixels, 20) << tex;
    }
    // A fraction stands over the baseline and hangs below it; a matrix is higher than a letter
    const auto frac = math::formula(R"(\frac{a}{b})", true);
    const auto letter = math::formula("a", true);
    const auto matrix = math::formula(R"(\begin{pmatrix} a \\ b \\ c \end{pmatrix})", true);
    EXPECT_GT(frac->descent, 0.3);
    EXPECT_GT(matrix->ascent + matrix->descent, 2 * (letter->ascent + letter->descent));
}

TEST(MdMath, ColorsOfTheTextAndOwnColors) {
    // Without \color: the current source (the text's color); with it: its own
    const auto plain = math::formula("x", false);
    for (const auto& s: plain->shapes) {
        EXPECT_EQ(s.color, 0u);
    }
    // (MicroTeX takes \color in arrays only; \textcolor{red}{x} colors a part)
    const auto red = math::formula(R"(\textcolor{red}{x})", false);
    bool anyRed = false;
    for (const auto& s: red->shapes) {
        anyRed = anyRed || s.color == 0xffff0000u;
    }
    EXPECT_TRUE(anyRed);
}

TEST(MdMath, Cache) {
    math::clearCache();
    const auto a = math::formula(R"(\frac{\pi}{2})", false);
    const auto b = math::formula(R"(\frac{\pi}{2})", false);
    EXPECT_EQ(a.get(), b.get());  // the same formula, laid out once
    const auto display = math::formula(R"(\frac{\pi}{2})", true);
    EXPECT_NE(a.get(), display.get());
    const auto stats = math::cacheStats();
    EXPECT_EQ(stats.entries, 2u);
    EXPECT_EQ(stats.misses, 2u);
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_GT(stats.bytes, 0u);
    EXPECT_LT(stats.bytes, math::CACHE_LIMIT);
}

TEST(MdMath, CacheHasALimit) {
    math::clearCache();
    const auto first = math::formula(R"(\sum_{k=0}^{0} \frac{x^k}{k!})", true);
    size_t n = 0;
    while (math::cacheStats().misses == math::cacheStats().entries && n < 20000) {  // (until one went)
        math::formula(R"(\sum_{k=0}^{)" + std::to_string(++n) + R"(} \frac{x^k}{k!} + \sqrt{y})", true);
    }
    const auto stats = math::cacheStats();
    EXPECT_LT(stats.entries, stats.misses);
    EXPECT_LE(stats.bytes, math::CACHE_LIMIT);
    EXPECT_GT(stats.bytes, math::CACHE_LIMIT * 9 / 10);
    // The least recently used went first; a formula still used by a layout stays valid
    EXPECT_TRUE(first->ok);
    EXPECT_FALSE(first->shapes.empty());
    math::clearCache();
}

TEST(MdMath, ManyThreads) {
    // Formulas are laid out under one lock and drawn by any thread
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < 20; ++i) {
                const auto f = math::formula("x_" + std::to_string((t * 7 + i) % 11) + R"(+\frac{1}{2})", i % 2);
                EXPECT_TRUE(f->ok);
                inkOf(*f, 12);
            }
        });
    }
    for (auto& t: threads) {
        t.join();
    }
}

TEST(MdMath, PathOnlyAddsTheOutlines) {
    const auto f = math::formula(R"(\sqrt{2})", false);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, 10, 10);
    cairo_t* cr = cairo_create(s);
    math::draw(cr, *f, 1, 5, 10, true);
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    cairo_path_extents(cr, &x1, &y1, &x2, &y2);
    EXPECT_GT(x2 - x1, 5);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// Pictures of formulas against the ones in qt/tests/markdown/golden (XQT_MATH_GOLDEN_UPDATE=1 writes them anew):
/// what MicroTeX, its font and the recording as paths draw stays as it was looked at.
TEST(MdMath, GoldenPictures) {
    const std::pair<const char*, const char*> formulas[] = {
            {"fraction", R"(\frac{a+b}{c-d} = \frac{1}{2})"},
            {"sum", R"(\sum_{k=0}^{\infty} \frac{x^k}{k!} = e^x)"},
            {"matrix", R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix} \begin{bmatrix} x \\ y \end{bmatrix})"},
            {"greek", R"(\alpha\beta\gamma\delta + \Omega\Sigma\pi \to \sqrt[3]{\varphi})"},
    };
    const std::string dir = XQT_MARKDOWN_GOLDEN;
    const bool update = std::getenv("XQT_MATH_GOLDEN_UPDATE") != nullptr;
    for (const auto& [name, tex]: formulas) {
        cairo_surface_t* s = nullptr;
        inkOf(*math::formula(tex, true), 32, &s);
        const std::string path = dir + "/math-" + name + ".png";
        if (update) {
            cairo_surface_write_to_png(s, path.c_str());
            cairo_surface_destroy(s);
            continue;
        }
        cairo_surface_t* want = cairo_image_surface_create_from_png(path.c_str());
        ASSERT_EQ(cairo_surface_status(want), CAIRO_STATUS_SUCCESS) << path;
        const int w = cairo_image_surface_get_width(s);
        const int h = cairo_image_surface_get_height(s);
        ASSERT_EQ(w, cairo_image_surface_get_width(want)) << name;
        ASSERT_EQ(h, cairo_image_surface_get_height(want)) << name;
        // (the same up to antialiasing: another Cairo may round an edge's pixels a little differently)
        long sum = 0;
        int far = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int a = cairo_image_surface_get_data(s)[y * cairo_image_surface_get_stride(s) + x * 4];
                const int b = cairo_image_surface_get_data(want)[y * cairo_image_surface_get_stride(want) + x * 4];
                sum += std::abs(a - b);
                far += std::abs(a - b) > 96;
            }
        }
        EXPECT_LT(static_cast<double>(sum) / (w * h), 2.0) << name;
        EXPECT_LT(far, w * h / 200) << name;
        cairo_surface_destroy(want);
        cairo_surface_destroy(s);
    }
}

/// XQT_MATH_SHOTS=<dir>: writes the formulas above as PNG images, to look at.
TEST(MdMath, Shots) {
    const char* dir = std::getenv("XQT_MATH_SHOTS");
    if (!dir) {
        GTEST_SKIP() << "XQT_MATH_SHOTS not set";
    }
    int n = 0;
    for (const char* tex: {R"(\frac{a+b}{c-d})", R"(\sum_{k=0}^{\infty} \frac{x^k}{k!} = e^x)",
                           R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix})", R"(\alpha\beta\gamma\Omega\pi)",
                           R"(\int_0^1 f(x)\,dx + \sqrt[3]{x})", R"(\text{if } x > 0, \textcolor{red}{y})"}) {
        cairo_surface_t* s = nullptr;
        inkOf(*math::formula(tex, true), 60, &s);
        cairo_surface_write_to_png(s, (std::string(dir) + "/math-" + std::to_string(n++) + ".png").c_str());
        cairo_surface_destroy(s);
    }
}

// --- formulas in the Markdown text --------------------------------------------------------------------------------

#include <cairo-pdf.h>
#include <poppler.h>

#include "MdBox.h"
#include "MdLayout.h"

namespace {
Layout lay(const std::string& src, double width = 400, size_t active = NO_SOURCE) {
    Style s;
    s.width = width;
    return layout(parse(src), s, src, active);
}
const Item& onlyText(const Layout& l) {
    for (const Item& it: l.items) {
        if (it.kind == Item::Kind::Text) {
            return it;
        }
    }
    throw std::runtime_error("no text");
}
std::vector<const Run*> mathRuns(const Block& b, std::vector<const Run*> out = {}) {
    for (const Run& r: b.runs) {
        if (r.flags & Math) {
            out.push_back(&r);
        }
    }
    for (const Block& c: b.children) {
        out = mathRuns(c, out);
    }
    return out;
}
}  // namespace

TEST(MdMathText, ParsedAsFormulas) {
    const std::string src = "Inline $x^2$ and\n\n$$\\frac{a}{b}$$\n\nno $ 5 and $10, \\$x\\$ or `$code$`.\n";
    const Document doc = parse(src);
    const auto runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 2u);  // (money, escaped dollars and code are no formulas)
    EXPECT_EQ(runs[0]->text, "x^2");
    EXPECT_FALSE(runs[0]->flags & DisplayMath);
    EXPECT_EQ(src.substr(runs[0]->source, runs[0]->sourceLength), "x^2");
    EXPECT_EQ(runs[1]->text, "\\frac{a}{b}");
    EXPECT_TRUE(runs[1]->flags & DisplayMath);
}

TEST(MdMathText, InlineFormulaIsDrawnInTheLine) {
    const Layout plain = lay("A line with x and more.");
    const Layout withMath = lay("A line with $x$ and more.");
    const Item& it = onlyText(withMath);
    ASSERT_EQ(it.maths.size(), 1u);
    EXPECT_TRUE(it.maths[0].error.empty());
    EXPECT_EQ(std::string(pango_layout_get_text(it.layout.get())), "A line with \xef\xbf\xbc and more.");
    // The line is (about) as high as without the formula
    EXPECT_LT(withMath.height, plain.height * 1.1);
    // A fraction makes it higher, but not beyond reason
    const Layout frac = lay("A line with $\\frac{1}{2}$ and more.");
    EXPECT_GT(frac.height, plain.height);
    EXPECT_LT(frac.height, plain.height * 2);
    // On the baseline: the formula sits where the text's x would be
    const auto rects = textRects(it, it.maths[0].start, it.maths[0].start + it.maths[0].length);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GT(rects[0].x, 20);
    EXPECT_NEAR(rects[0].width, it.maths[0].inkWidth, 0.01);
    // Lines break around formulas
    const Layout narrow = lay("some words $a+b$ some words $c+d$ some words $e+f$ some words", 80);
    EXPECT_GT(narrow.height, 3 * plain.height);
    for (const Item& i: narrow.items) {
        EXPECT_LE(i.x + i.width, 80.5);
    }
}

TEST(MdMathText, DisplayFormulaIsCentered) {
    const Layout l = lay("Before\n\n$$\\sum_{i=1}^n i = \\frac{n(n+1)}{2}$$\n\nAfter\n", 400);
    ASSERT_EQ(l.blocks.size(), 3u);
    const Item& it = l.items[static_cast<size_t>(l.blocks[1].item)];
    ASSERT_EQ(it.maths.size(), 1u);
    const MathSpan& m = it.maths[0];
    EXPECT_TRUE(m.display);
    EXPECT_NEAR(m.inkX * 2 + m.inkWidth, 400, 1);  // centered in the box's width
    const auto rects = textRects(it, m.start, m.start + m.length);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_NEAR(rects[0].x, m.inkX, 0.5);  // the formula, not the whole line
    EXPECT_NEAR(rects[0].width, m.inkWidth, 0.5);
    // Display style: taller than the same formula inline
    const Layout inl = lay("$\\sum_{i=1}^n i = \\frac{n(n+1)}{2}$", 400);
    EXPECT_GT(l.blocks[1].bottom - l.blocks[1].top, inl.height);
    // In the middle of a paragraph: a line of its own
    const Layout mid = lay("text before $$x = y$$ text after", 400);
    const Item& p = onlyText(mid);
    EXPECT_EQ(pango_layout_get_line_count(p.layout.get()), 3);
    // Too wide: made smaller to fit
    const Layout wide = lay("$$" + std::string(60, 'x') + "$$", 200);
    const MathSpan& w = onlyText(wide).maths[0];
    EXPECT_LE(w.inkWidth, 200);
    EXPECT_GE(w.inkX, -0.01);
}

TEST(MdMathText, ErrorShowsTheSource) {
    const Layout l = lay("Bad $\\hline$ formula and good $y$.");
    const Item& it = onlyText(l);
    ASSERT_EQ(it.maths.size(), 2u);
    EXPECT_FALSE(it.maths[0].error.empty());
    EXPECT_TRUE(it.maths[1].error.empty());
    const std::string laid = pango_layout_get_text(it.layout.get());
    EXPECT_NE(laid.find("\\hline"), std::string::npos);  // its source, in the text
    // Where it is, and why
    const auto rects = textRects(it, it.maths[0].start, it.maths[0].start + it.maths[0].length);
    ASSERT_FALSE(rects.empty());
    const auto hit = mathAt(l, rects[0].x + rects[0].width / 2, rects[0].y + rects[0].height / 2);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->span.tex, "\\hline");
    EXPECT_NE(hit->span.error.find("hline"), std::string::npos) << hit->span.error;
    // Drawing never throws, also with an unclosed formula being typed
    for (const char* src:
         {"$\\frac{1}{", "$$\\begin{matrix}", "$}$", "$\\left($", "$x^$", "$$ $$", "$\\raisebox{1pt}{}$"}) {
        const Layout e = lay(src);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
        cairo_t* cr = cairo_create(s);
        draw(cr, e);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
}

TEST(MdMathText, SourceIsSearched) {
    const Layout l = lay("Energy $E = mc^2$ and $$\\alpha + \\beta$$ end");
    const Item& it = onlyText(l);
    EXPECT_EQ(searchText(it), "Energy E = mc^2 and \\alpha + \\beta end");
    // A hit in a formula marks the formula
    const auto hits = findText(l, "mc^2");
    ASSERT_EQ(hits.size(), 1u);
    const auto formula = textRects(it, it.maths[0].start, it.maths[0].start + it.maths[0].length);
    ASSERT_EQ(formula.size(), 1u);
    EXPECT_NEAR(hits[0].x, formula[0].x, 0.01);
    EXPECT_NEAR(hits[0].width, formula[0].width, 0.01);
    // Text after formulas is where it is drawn
    const auto end = findText(l, "end");
    ASSERT_EQ(end.size(), 1u);
    const std::string laid = pango_layout_get_text(it.layout.get());
    const auto expected = textRects(it, static_cast<int>(laid.find("end")), static_cast<int>(laid.find("end") + 3));
    ASSERT_EQ(expected.size(), 1u);
    EXPECT_NEAR(end[0].x, expected[0].x, 0.01);
    EXPECT_NEAR(end[0].y, expected[0].y, 0.01);
    EXPECT_EQ(findText(l, "\\beta").size(), 1u);
    // layoutRange: over a formula's source and the text after it
    const std::string searched = searchText(it);
    const int from = static_cast<int>(searched.find("mc^2"));
    const auto [a, b] = layoutRange(it, from, from + 8);  // "mc^2 and"
    EXPECT_EQ(a, it.maths[0].start);
    EXPECT_EQ(laid.substr(static_cast<size_t>(a), static_cast<size_t>(b - a)), "\xef\xbf\xbc and");
}

TEST(MdMathText, WrittenBlockShowsTheSource) {
    const std::string src = "First $a^2$ here.\n\nSecond $b^2$ there.\n\n$$\n\\frac{1}{2}\n$$\n";
    // The cursor in the first paragraph: its formula is its source, the other one is drawn
    const Layout l = lay(src, 400, 8);
    ASSERT_GE(l.rawItem, 0);
    const Item& raw = l.items[static_cast<size_t>(l.rawItem)];
    EXPECT_EQ(std::string(pango_layout_get_text(raw.layout.get())).substr(0, 17), "First $a^2$ here.");
    EXPECT_TRUE(raw.maths.empty());
    const Item& second = l.items[static_cast<size_t>(l.blocks[1].item)];
    EXPECT_EQ(second.maths.size(), 1u);
    // The cursor in a formula block: its source, and below it how it looks (drawn only)
    const size_t inBlock = src.find("\\frac") + 2;
    const Layout b = lay(src, 400, inBlock);
    const Item& rawBlock = b.items[static_cast<size_t>(b.rawItem)];
    EXPECT_EQ(std::string(pango_layout_get_text(rawBlock.layout.get())), "$$\n\\frac{1}{2}\n$$");  // (the $$ lines too)
    ASSERT_GT(b.items.size(), static_cast<size_t>(b.rawItem) + 1);
    const Item& preview = b.items[static_cast<size_t>(b.rawItem) + 1];
    ASSERT_EQ(preview.maths.size(), 1u);
    EXPECT_TRUE(preview.sources.empty());
    EXPECT_GT(preview.y, rawBlock.y + rawBlock.height - 0.01);
}

TEST(MdMathText, PdfHasTheFormulaNotItsSource) {
    const Layout l = lay("Before $\\alpha + \\sum_{k} x_k$ after.\n\n$$\\int_0^1 f$$\n", 400);
    const std::string path = ::testing::TempDir() + "xqt-md-math-test.pdf";
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
    // Drawn: the formula's ink is on the page where it is laid out
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 500, 300);
    cairo_t* icr = cairo_create(img);
    cairo_set_source_rgb(icr, 1, 1, 1);
    cairo_paint(icr);
    poppler_page_render(page, icr);
    cairo_destroy(icr);
    cairo_surface_flush(img);
    const Item& it = onlyText(l);
    const auto r = textRects(it, it.maths[0].start, it.maths[0].start + it.maths[0].length)[0];
    int ink = 0;
    const unsigned char* data = cairo_image_surface_get_data(img);
    for (int y = static_cast<int>(50 + r.y); y < static_cast<int>(50 + r.y + r.height); ++y) {
        for (int x = static_cast<int>(50 + r.x); x < static_cast<int>(50 + r.x + r.width); ++x) {
            ink += (data[y * cairo_image_surface_get_stride(img) + x * 4] < 128);
        }
    }
    cairo_surface_destroy(img);
    g_object_unref(page);
    g_object_unref(doc);
    EXPECT_NE(s.find("Before"), std::string::npos) << s;
    EXPECT_NE(s.find("after."), std::string::npos) << s;
    EXPECT_EQ(s.find("alpha"), std::string::npos) << s;  // (paths, not the TeX)
    EXPECT_EQ(s.find("\\int"), std::string::npos) << s;
    EXPECT_GT(ink, 20);
}

/// A page of ~50 formulas: laid out once, then from the cache (XQT_BENCH_MATH=1 prints the times).
TEST(MdMathText, PageOfFormulas) {
    std::string src;
    for (int i = 0; i < 25; ++i) {
        src += "Line " + std::to_string(i) + " with $x_{" + std::to_string(i) + "}^2 + \\frac{a}{b_" +
               std::to_string(i) + "}$ inline.\n\n$$\\sum_{k=0}^{" + std::to_string(i) +
               "} \\binom{n}{k} = 2^n$$\n\n";
    }
    math::clearCache();
    const auto t0 = std::chrono::steady_clock::now();
    const Layout first = lay(src, 400);
    const auto t1 = std::chrono::steady_clock::now();
    EXPECT_EQ(math::cacheStats().misses, 50u);
    // Laid out again (another width: no Pango layout is taken again): the formulas come from the cache
    const Layout again = lay(src, 390);
    const auto t2 = std::chrono::steady_clock::now();
    EXPECT_EQ(math::cacheStats().misses, 50u);
    EXPECT_GE(math::cacheStats().hits, 50u);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, static_cast<int>(first.height) + 1);
    cairo_t* cr = cairo_create(s);
    const auto t3 = std::chrono::steady_clock::now();
    draw(cr, first);
    const auto t4 = std::chrono::steady_clock::now();
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    if (std::getenv("XQT_BENCH_MATH")) {
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        std::printf("50 formulas: first layout %.1f ms, again (cached formulas) %.1f ms, draw %.1f ms\n", ms(t0, t1),
                    ms(t1, t2), ms(t3, t4));
    }
}

#include "MdPaginate.h"

TEST(MdMathText, FormulaBlocksGoWithTheirMarks) {
    // "$$" on lines of their own belong to the formula's block
    const std::string src = "Text.\n\n$$\n\\frac{1}{2}\n$$\n\nMore.\n";
    const auto spans = topLevelSpans(src, parse(src));
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(src.substr(spans[1].begin, spans[1].end - spans[1].begin), "$$\n\\frac{1}{2}\n$$\n");
    // Pages split between formulas, never inside one: each page has whole formulas
    std::string text;
    for (int i = 0; i < 30; ++i) {
        text += "Paragraph " + std::to_string(i) + " with $x_" + std::to_string(i) + "$ inline.\n\n$$\n\\sum_{k=1}^{" +
                std::to_string(i) + "} k^2\n$$\n\n";
    }
    Style s;
    s.size = 10;
    const Pagination p = paginate(text, s, [](size_t) { return Frame{300, 160}; });
    ASSERT_GE(p.slices.size(), 3u);
    EXPECT_EQ(join(p.slices), text);
    size_t formulas = 0;
    for (const std::string& slice: p.slices) {
        for (const Item& it: lay(slice, 300).items) {
            for (const MathSpan& m: it.maths) {
                EXPECT_TRUE(m.error.empty()) << m.tex;
                EXPECT_EQ(m.tex.find('$'), std::string::npos) << slice;
                ++formulas;
            }
        }
    }
    EXPECT_EQ(formulas, 60u);
}

/// XQT_MATH_SHOTS=<dir>: a Markdown text with formulas as it is drawn, and while its formula block is written.
TEST(MdMathText, Shots) {
    const char* dir = std::getenv("XQT_MATH_SHOTS");
    if (!dir) {
        GTEST_SKIP() << "XQT_MATH_SHOTS not set";
    }
    const std::string src =
            "# Math in Markdown\n\nEuler's identity $e^{i\\pi} + 1 = 0$ links five constants; the area of a circle "
            "is $A = \\pi r^2$, and $\\frac{1}{2}$ is a half.\n\n$$\n\\sum_{k=0}^{n} \\binom{n}{k} = 2^n\n$$\n\n"
            "> A quote with $\\sqrt{x^2+y^2}$ in it.\n\n- a list item with $\\alpha_i \\to \\beta$\n- "
            "[a link $\\vec v$](https://example.org)\n\n$$\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix} "
            "\\begin{pmatrix} x \\\\ y \\end{pmatrix}$$\n\nA broken one: $\\frac{1}{2$ and $\\hline$.\n";
    int n = 0;
    for (size_t active: {NO_SOURCE, src.find("\\binom")}) {
        const Layout l = lay(src, 420, active);
        cairo_surface_t* s =
                cairo_image_surface_create(CAIRO_FORMAT_RGB24, 2 * 460, static_cast<int>(2 * (l.height + 40)));
        cairo_t* cr = cairo_create(s);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_scale(cr, 2, 2);
        cairo_translate(cr, 20, 20);
        draw(cr, l);
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, (std::string(dir) + "/md-math-" + std::to_string(n++) + ".png").c_str());
        cairo_surface_destroy(s);
    }
}

TEST(MdMathText, AFormulaOverLinesKnowsItsSource) {
    // md4c gives a "$$" block's line breaks as made-up spaces (no source): the formula still is its source, so a tap
    // on it puts the cursor into it and the search marks it
    const std::string src = "Before.\n\n$$\n\\sum_{k=1}^n k\n$$\n";
    const Layout l = lay(src, 400);
    const Item& it = l.items[static_cast<size_t>(l.blocks[1].item)];
    ASSERT_EQ(it.maths.size(), 1u);
    const SourceMap* formula = nullptr;
    for (const SourceMap& m: it.sources) {
        if (m.flags & Math) {
            formula = &m;
        }
    }
    ASSERT_NE(formula, nullptr);
    ASSERT_NE(formula->source, NO_SOURCE);
    EXPECT_EQ(src.substr(formula->source, formula->sourceLength), "\\sum_{k=1}^n k");
    const size_t at = src.find("k=1");
    EXPECT_FALSE(sourceRects(l, at, at + 3).empty());
}
