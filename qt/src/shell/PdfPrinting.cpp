#include "PdfPrinting.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPrinter>
#include <QUrl>

#include <cairo.h>
#include <poppler.h>

namespace xqt {

namespace {
constexpr double maxDpi = 300;

QString tr(const char* text) { return QCoreApplication::translate("PdfPrinting", text); }

/// Draws one page into an image, `scale` pixels per PDF point, on white.
QImage renderPage(PopplerPage* page, double width, double height, double scale) {
    QImage image(std::max(1, static_cast<int>(std::ceil(width * scale))),
                 std::max(1, static_cast<int>(std::ceil(height * scale))), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return image;
    }
    image.fill(Qt::white);
    // Cairo's ARGB32 is QImage's ARGB32_Premultiplied on little-endian machines (every desktop the app runs on).
    cairo_surface_t* surface = cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, image.width(),
                                                                   image.height(), static_cast<int>(image.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    poppler_page_render_for_printing(page, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
    return image;
}
}  // namespace

bool printPdfAsImages(const QString& pdfFile, QPrinter& printer, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    GError* gerror = nullptr;
    const QByteArray uri = QUrl::fromLocalFile(pdfFile).toString(QUrl::FullyEncoded).toUtf8();
    PopplerDocument* doc = poppler_document_new_from_file(uri.constData(), nullptr, &gerror);
    if (!doc) {
        const QString why = gerror ? QString::fromUtf8(gerror->message) : tr("Could not read the document to print.");
        if (gerror) {
            g_error_free(gerror);
        }
        return fail(why);
    }
    const int count = poppler_document_get_n_pages(doc);
    int first = 1;
    int last = count;
    if (printer.printRange() == QPrinter::PageRange && printer.fromPage() > 0) {
        first = std::max(1, printer.fromPage());
        last = std::min(count, printer.toPage() > 0 ? printer.toPage() : count);
    }
    if (first > last) {
        g_object_unref(doc);
        return fail(tr("The pages to print are not in the document."));
    }
    // Copies: the printer driver makes them when it can, else the pages are sent again.
    const int copies = printer.supportsMultipleCopies() ? 1 : std::max(1, printer.copyCount());

    QPainter painter;
    if (!painter.begin(&printer)) {
        g_object_unref(doc);
        return fail(tr("Could not start printing on %1.").arg(printer.printerName()));
    }
    const double dpi = std::min<double>(printer.resolution(), maxDpi);
    bool firstSheet = true;
    bool ok = true;
    for (int copy = 0; copy < copies && ok; ++copy) {
        for (int n = first; n <= last; ++n) {
            PopplerPage* page = poppler_document_get_page(doc, n - 1);
            if (!page) {
                continue;
            }
            if (!firstSheet && !printer.newPage()) {
                g_object_unref(page);
                ok = false;
                break;
            }
            firstSheet = false;
            double width = 0;
            double height = 0;
            poppler_page_get_size(page, &width, &height);
            const QRect area = painter.viewport();  // the printable area, in printer pixels
            const bool turn = (width > height) != (area.width() > area.height());
            const double onPaperWidth = turn ? height : width;
            const double onPaperHeight = turn ? width : height;
            // Printer pixels per point: the page fills the printable area, keeping its proportions.
            const double fit = std::min(area.width() / onPaperWidth, area.height() / onPaperHeight);
            const QImage image = renderPage(page, width, height, std::min(fit, dpi / 72.0));
            g_object_unref(page);
            if (image.isNull()) {
                ok = false;
                break;
            }
            painter.save();
            painter.translate(area.x() + (area.width() - onPaperWidth * fit) / 2,
                              area.y() + (area.height() - onPaperHeight * fit) / 2);
            if (turn) {
                painter.translate(onPaperWidth * fit, 0);
                painter.rotate(90);
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(0, 0, width * fit, height * fit), image);
            painter.restore();
        }
    }
    painter.end();
    g_object_unref(doc);
    return ok || fail(tr("The printer stopped taking pages."));
}

}  // namespace xqt
