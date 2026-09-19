#include "PageClipboard.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <cairo.h>
#include <gio/gio.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"

namespace xqt {

namespace {
cairo_status_t appendPng(void* closure, const unsigned char* data, unsigned int length) {
    auto* buffer = static_cast<std::vector<unsigned char>*>(closure);
    buffer->insert(buffer->end(), data, data + length);
    return CAIRO_STATUS_SUCCESS;
}

/// The PDF page as an (attached) image background of the page.
bool pdfToImageBackground(XojPage& page, const XojPdfPage& pdf) {
    const double scale = PageClipboard::IMAGE_DPI / 72.0;
    const int w = std::max(1, static_cast<int>(page.getWidth() * scale));
    const int h = std::max(1, static_cast<int>(page.getHeight() * scale));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    pdf.render(cr);
    cairo_destroy(cr);
    std::vector<unsigned char> png;
    const bool ok = cairo_surface_write_to_png_stream(surface, appendPng, &png) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    if (!ok) {
        return false;
    }
    GBytes* bytes = g_bytes_new(png.data(), png.size());
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    BackgroundImage img;
    GError* error = nullptr;
    img.loadFile(stream, fs::path("pasted-pdf-page.png"), &error);
    g_object_unref(stream);
    if (error) {
        g_warning("Could not convert the PDF page: %s", error->message);
        g_error_free(error);
        return false;
    }
    img.setAttach(true);  // stored in the .xopp
    page.setBackgroundImage(img);
    page.setBackgroundType(PageType(PageTypeFormat::Image));
    return true;
}
}  // namespace

void PageClipboard::copy(Document& doc, const std::vector<size_t>& indices) {
    std::shared_lock lock(doc);
    pages.clear();
    pdfPages.clear();
    pdfFile = doc.getPdfFilepath();
    for (size_t i: indices) {
        if (i >= doc.getPageCount()) {
            continue;
        }
        const PageRef page = doc.getPage(i);
        pages.push_back(std::make_shared<XojPage>(*page));
        pdfPages.push_back(page->getBackgroundType().isPdfPage() ? doc.getPdfPage(page->getPdfPageNr()) : nullptr);
    }
}

std::vector<PageRef> PageClipboard::pagesFor(Document& target) const {
    fs::path targetPdf;
    {
        std::shared_lock lock(target);
        targetPdf = target.getPdfFilepath();
    }
    const bool samePdf = !pdfFile.empty() && targetPdf == pdfFile;
    std::vector<PageRef> result;
    for (size_t i = 0; i < pages.size(); ++i) {
        auto copy = std::make_shared<XojPage>(*pages[i]);
        if (copy->getBackgroundType().isPdfPage() && !samePdf) {
            if (!pdfPages[i] || !pdfToImageBackground(*copy, *pdfPages[i])) {
                copy->setBackgroundType(PageType(PageTypeFormat::Plain));  // the annotations at least
            }
        }
        result.push_back(std::move(copy));
    }
    return result;
}

}  // namespace xqt
