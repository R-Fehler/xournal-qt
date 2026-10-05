/*
 * xournal-qt: dark pages, the color mapping (qt/docs/dark-pages.md): roles to their dark equivalents, other colors by a
 * lightness flip that keeps their hue, the table the shader reads, the pictures kept.
 *
 * @license GNU GPLv2 or later
 */
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <string>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "DarkPages.h"
#include "PagePictures.h"

using namespace xqt;

namespace {

int distance(QRgb a, QRgb b) {
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}

double luminance(QRgb c) {
    const auto lin = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(qRed(c)) + 0.7152 * lin(qGreen(c)) + 0.0722 * lin(qBlue(c));
}
/// WCAG contrast ratio
double contrast(QRgb a, QRgb b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::string hex(QRgb c) { return QColor(c).name().toStdString(); }

/// The palettes' pairs the tests use (as ColorPalettes::darkPairs gives them for Classic → Dark)
struct Roles {
    Roles() {
        dark::setRoles({{0xffd6342c, 0xffff6b6b, 1.0},           // warnings ink
                        {0xff1d5fd1, 0xff6aa8ff, 1.0},           // headings ink
                        {0xff2b2b2b, 0xffececec, 1.0},           // body ink
                        {0xffffe066, 0xff7a5a12, 0.8 / 0.47}});  // key terms highlight
    }
    ~Roles() { dark::setRoles({}); }
};

QRgb over(QRgb paper, QRgb ink, double a) {
    const auto mix = [a](int p, int i) { return static_cast<int>(std::lround(p + a * (i - p))); };
    return qRgb(mix(qRed(paper), qRed(ink)), mix(qGreen(paper), qGreen(ink)), mix(qBlue(paper), qBlue(ink)));
}

}  // namespace

TEST(DarkPages, whitePaperBecomesTheDarkPalettesBackgroundAndBlackItsBodyInk) {
    EXPECT_LE(distance(dark::darkPaper(), 0xff1e1f22), 1) << hex(dark::darkPaper());
    EXPECT_LE(distance(dark::lightInk(), 0xffececec), 1) << hex(dark::lightInk());
    EXPECT_LE(distance(dark::mapGeneric(0xffffffff), dark::darkPaper()), 0);
}

TEST(DarkPages, aRolesColorBecomesTheDarkPalettesColorOfTheRole) {
    Roles roles;
    EXPECT_EQ(dark::map(0xffd6342c), 0xffff6b6b) << hex(dark::map(0xffd6342c));
    EXPECT_EQ(dark::map(0xff1d5fd1), 0xff6aa8ff);
    // Antialiased: the role's color part on white paper becomes the dark color as much on the dark paper
    const QRgb edge = over(0xffffffff, 0xffd6342c, 0.5);
    EXPECT_LE(distance(dark::map(edge), over(dark::darkPaper(), 0xffff6b6b, 0.5)), 2) << hex(dark::map(edge));
    // The highlighter (upstream: 0.47 on the paper) shows as the palettes want it on dark paper: 0.8
    const QRgb highlight = over(0xffffffff, 0xffffe066, 0.47);
    EXPECT_LE(distance(dark::map(highlight), over(dark::darkPaper(), 0xff7a5a12, 0.8)), 2) << hex(dark::map(highlight));
}

TEST(DarkPages, otherColorsKeepTheirHueAndFlipTheirLightness) {
    for (const QRgb c: {0xff008000u, 0xff800080u, 0xffc06000u, 0xff3070a0u, 0xffa02040u}) {
        const QColor in(c), out(dark::mapGeneric(c));
        EXPECT_NEAR(in.hslHueF(), out.hslHueF(), 0.06) << hex(c) << " -> " << hex(out.rgb());
        EXPECT_GT(out.lightnessF(), in.lightnessF()) << hex(c) << " -> " << hex(out.rgb());
        // readable on the dark paper (WCAG AA for large text at least)
        EXPECT_GT(contrast(out.rgb(), dark::darkPaper()), 3.0) << hex(c) << " -> " << hex(out.rgb());
    }
    // Greys: darker in, lighter out (monotonic)
    int last = -1;
    for (int v = 255; v >= 0; v -= 15) {
        const int out = qGray(dark::mapGeneric(qRgb(v, v, v)));
        EXPECT_GT(out, last) << v;
        last = out;
    }
}

TEST(DarkPages, aPaleHighlightStaysAVisibleMarker) {
    // Upstream's yellow highlighter on white (0.47): a flip of its lightness alone would be a shade of the paper
    const QRgb yellow = over(0xffffffff, 0xffffff00, 0.47);
    const QColor out(dark::mapGeneric(yellow));
    // (as visible on the dark paper as the Dark palette's key terms highlight at 0.8 is)
    EXPECT_GT(contrast(out.rgb(), dark::darkPaper()),
              0.9 * contrast(over(dark::darkPaper(), 0xff7a5a12, 0.8), dark::darkPaper()))
            << hex(out.rgb());
    EXPECT_NEAR(out.hslHueF(), QColor(0xffff00).hslHueF(), 0.05) << hex(out.rgb());
}

TEST(DarkPages, theTableGivesWhatTheMappingGives) {
    Roles roles;
    for (const QRgb c: {0xffffffffu, 0xff000000u, 0xffd6342cu, 0xff1d5fd1u, 0xff808080u, 0xff3070a0u, 0xff2b2b2bu}) {
        EXPECT_LE(distance(dark::lookup(c), dark::map(c)), 6)
                << hex(c) << ": " << hex(dark::lookup(c)) << " vs " << hex(dark::map(c));
    }
    EXPECT_EQ(dark::lookup(0xffffffff), dark::darkPaper());
    // The table's image (the shader's): LUT slices of LUT x LUT
    EXPECT_EQ(dark::table().size(), QSize(dark::LUT * dark::LUT, dark::LUT));
    // A cream paper is balanced to white: the paper becomes the dark paper too
    EXPECT_LE(distance(dark::lookup(0xfff1e6cc, 0xfff1e6cc), dark::darkPaper()), 1);
}

TEST(DarkPages, applyTurnsAnImageDarkExceptWhatIsKept) {
    QImage img(40, 20, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    for (int x = 20; x < 40; ++x) {
        for (int y = 0; y < 20; ++y) {
            img.setPixel(x, y, 0xff00ff00);
        }
    }
    dark::apply(img, 0xffffffff, {QRect(20, 0, 20, 20)});
    EXPECT_EQ(img.pixel(5, 5), dark::lookup(0xffffffff));
    EXPECT_EQ(img.pixel(30, 5), 0xff00ff00u);  // (a picture: kept)
}

TEST(DarkPages, aPageWhosePaperIsDarkStaysAsItIs) {
    EXPECT_TRUE(dark::turnsDark(0xffffffff));
    EXPECT_TRUE(dark::turnsDark(0xfff1e6cc));  // illustration paper
    EXPECT_FALSE(dark::turnsDark(0xff141414));
    EXPECT_FALSE(dark::turnsDark(0xff2b2d31));
    QImage slide(100, 60, QImage::Format_RGB32);
    slide.fill(QColor("#202040"));
    EXPECT_EQ(dark::paperOfImage(slide), 0xff202040u);
    EXPECT_FALSE(dark::turnsDark(dark::paperOfImage(slide)));
}

TEST(DarkPages, aScanIsNotKeptAsAPicture) {
    const auto kept = dark::keptPictures({QRectF(0, 0, 590, 830), QRectF(50, 50, 100, 80)}, 595, 842);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0], QRectF(50, 50, 100, 80));
}

TEST(DarkPages, thePicturesOfAPdfPageAreFound) {
    QTemporaryDir tmp;
    const std::string file = tmp.filePath("pictures.pdf").toStdString();
    cairo_surface_t* pdf = cairo_pdf_surface_create(file.c_str(), 300, 400);
    cairo_t* cr = cairo_create(pdf);
    cairo_surface_t* picture = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 100, 80);
    cairo_t* pc = cairo_create(picture);
    cairo_set_source_rgb(pc, 0, 0.6, 0.2);
    cairo_paint(pc);
    cairo_destroy(pc);
    cairo_set_source_surface(cr, picture, 50, 60);
    cairo_rectangle(cr, 50, 60, 100, 80);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);  // (and vector ink: not a picture)
    cairo_rectangle(cr, 10, 300, 200, 4);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(picture);
    cairo_surface_destroy(pdf);

    const std::string uri = "file://" + file;
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    ASSERT_NE(doc, nullptr);
    PopplerPage* page = poppler_document_get_page(doc, 0);
    const auto rects = PdfPictures::read(page);
    g_object_unref(page);
    g_object_unref(doc);
    ASSERT_EQ(rects.size(), 1u);
    // From the page's top left, in points
    EXPECT_NEAR(rects[0].x(), 50, 1);
    EXPECT_NEAR(rects[0].y(), 60, 1);
    EXPECT_NEAR(rects[0].width(), 100, 1);
    EXPECT_NEAR(rects[0].height(), 80, 1);

    // Through the worker
    PdfPictures pictures;
    EXPECT_FALSE(pictures.pictures(file, 0).has_value());
    bool known = false;
    QObject::connect(&pictures, &PdfPictures::known, [&] { known = true; });
    for (int i = 0; i < 200 && !known; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    ASSERT_TRUE(known);
    const auto later = pictures.pictures(file, 0);
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(later->size(), 1u);
}
