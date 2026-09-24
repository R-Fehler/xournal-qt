/*
 * xournal-qt: printing a PDF through Qt's print engine (Windows has no lp). Checked here by printing into a PDF file.
 *
 * @license GNU GPLv2 or later
 */
#include <QPrinter>
#include <QTemporaryDir>
#include <QUrl>
#include <cairo-pdf.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "shell/PdfPrinting.h"

namespace {

/// A PDF with an A4 portrait page and an A4 landscape page, each filled black.
QString makePdf(const QString& file) {
    cairo_surface_t* surface = cairo_pdf_surface_create(file.toUtf8().constData(), 595, 842);
    cairo_t* cr = cairo_create(surface);
    cairo_paint(cr);
    cairo_show_page(cr);
    cairo_pdf_surface_set_size(surface, 842, 595);
    cairo_paint(cr);
    cairo_show_page(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return file;
}

/// The pages of a PDF file: {width, height} in points.
std::vector<std::pair<double, double>> pagesOf(const QString& file) {
    std::vector<std::pair<double, double>> pages;
    const QByteArray uri = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded).toUtf8();
    PopplerDocument* doc = poppler_document_new_from_file(uri.constData(), nullptr, nullptr);
    if (!doc) {
        return pages;
    }
    for (int i = 0; i < poppler_document_get_n_pages(doc); ++i) {
        PopplerPage* page = poppler_document_get_page(doc, i);
        double w = 0;
        double h = 0;
        poppler_page_get_size(page, &w, &h);
        pages.emplace_back(w, h);
        g_object_unref(page);
    }
    g_object_unref(doc);
    return pages;
}

/// How much of a page of a PDF file is dark, 0..1.
double darkShare(const QString& file, int pageIndex) {
    const QByteArray uri = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded).toUtf8();
    PopplerDocument* doc = poppler_document_new_from_file(uri.constData(), nullptr, nullptr);
    if (!doc) {
        return 0;
    }
    PopplerPage* page = poppler_document_get_page(doc, pageIndex);
    double w = 0;
    double h = 0;
    poppler_page_get_size(page, &w, &h);
    const double scale = 0.25;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(w * scale), int(h * scale));
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    long dark = 0;
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
        for (int x = 0; x < width; ++x) {
            dark += (row[x] & 0xff) < 128 ? 1 : 0;
        }
    }
    cairo_surface_destroy(surface);
    g_object_unref(page);
    g_object_unref(doc);
    return double(dark) / (double(width) * height);
}

}  // namespace

TEST(PdfPrinting, everyPageGoesOntoASheetOfTheSamePaper) {
    QTemporaryDir dir;
    const QString source = makePdf(dir.filePath("in.pdf"));
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(dir.filePath("out.pdf"));
    printer.setPageSize(QPageSize(QPageSize::A4));
    QString error;
    ASSERT_TRUE(xqt::printPdfAsImages(source, printer, &error)) << error.toStdString();
    const auto pages = pagesOf(dir.filePath("out.pdf"));
    ASSERT_EQ(pages.size(), 2u);
    for (const auto& [w, h]: pages) {
        EXPECT_LT(w, h);  // portrait paper, as set
    }
    // Both pages fill the sheet but for the margins: the landscape page was turned a quarter (not turned, it would
    // cover less than half of it).
    EXPECT_GT(darkShare(dir.filePath("out.pdf"), 0), 0.7);
    EXPECT_GT(darkShare(dir.filePath("out.pdf"), 1), 0.7);
}

TEST(PdfPrinting, onlyThePageRangeIsPrinted) {
    QTemporaryDir dir;
    const QString source = makePdf(dir.filePath("in.pdf"));
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(dir.filePath("out.pdf"));
    printer.setPrintRange(QPrinter::PageRange);
    printer.setFromTo(2, 2);
    ASSERT_TRUE(xqt::printPdfAsImages(source, printer));
    EXPECT_EQ(pagesOf(dir.filePath("out.pdf")).size(), 1u);

    printer.setFromTo(3, 4);  // not in the document
    QString error;
    EXPECT_FALSE(xqt::printPdfAsImages(source, printer, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(PdfPrinting, aFileThatIsNoPdfIsAnError) {
    QTemporaryDir dir;
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(dir.filePath("out.pdf"));
    QString error;
    EXPECT_FALSE(xqt::printPdfAsImages(dir.filePath("missing.pdf"), printer, &error));
    EXPECT_FALSE(error.isEmpty());
}
