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
