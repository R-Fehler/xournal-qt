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
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "util/Util.h"

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

namespace {
std::string stampOf(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) {
        return {};
    }
    const auto time = fs::last_write_time(p, ec);
    return std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
}
}  // namespace

void PageClipboard::copy(DocumentSession& session, const std::vector<size_t>& indices, bool withPdf) {
    Document& doc = *session.getDocument();
    std::vector<size_t> numbers;  // the PDF pages to take along
    {
        std::shared_lock lock(doc);
        pages.clear();
        pdfPages.clear();
        pdfIndex.clear();
        pdfData.clear();
        merged = {};
        pdfFile = doc.getPdfFilepath();
        sourceSession = session.serial();
        sourceNumbering = session.pdfNumbering();
        for (size_t i: indices) {
            if (i >= doc.getPageCount()) {
                continue;
            }
            const PageRef page = doc.getPage(i);
            pages.push_back(std::make_shared<XojPage>(*page));
            const bool pdf = page->getBackgroundType().isPdfPage();
            pdfPages.push_back(pdf ? doc.getPdfPage(page->getPdfPageNr()) : nullptr);
            size_t index = npos;
            if (pdf && pdfPages.back()) {
                const auto it = std::find(numbers.begin(), numbers.end(), page->getPdfPageNr());
                index = static_cast<size_t>(it - numbers.begin());
                if (it == numbers.end()) {
                    numbers.push_back(page->getPdfPageNr());
                }
            }
            pdfIndex.push_back(index);
        }
    }
    pdfStamp = stampOf(pdfFile);
    if (withPdf && !numbers.empty()) {
        // Now: the PDF may change before the paste (a save drops the unused pages of a merged PDF)
        if (const auto r = MergedPdf::extract(pdfFile, numbers, pdfData); !r.ok) {
            g_warning("Could not copy the PDF pages: %s", r.error.c_str());
            pdfData.clear();
        }
    }
}

std::vector<PageRef> PageClipboard::pagesFor(DocumentSession& target, fs::path* keptIn) const {
    Document& doc = *target.getDocument();
    fs::path targetPdf;
    {
        std::shared_lock lock(doc);
        targetPdf = doc.getPdfFilepath();
    }
    // The same PDF: the pages refer to the same page numbers. In the source document itself they stay valid until a
    // save drops PDF pages (its merged PDF only grows before); another document must use the same file, unchanged
    // since the copy.
    const bool samePdf = (target.serial() == sourceSession && target.pdfNumbering() == sourceNumbering &&
                          !pdfFile.empty()) ||
                         (!pdfFile.empty() && targetPdf == pdfFile && stampOf(pdfFile) == pdfStamp);
    size_t first = npos;
    if (!samePdf && !pdfData.empty()) {
        if (merged.session == target.serial() && merged.pdf == targetPdf && stampOf(targetPdf) == merged.stamp) {
            first = merged.first;  // pasted into it before: the pages are there
        } else {
            std::string error;
            first = target.addPdfPages(pdfData, error);
            if (first == npos) {
                g_warning("Could not add the PDF pages to the document's PDF: %s", error.c_str());
            } else {
                std::shared_lock lock(doc);
                merged.session = target.serial();
                merged.pdf = doc.getPdfFilepath();
                merged.stamp = stampOf(merged.pdf);
                merged.first = first;
            }
        }
        if (first != npos && keptIn) {
            std::shared_lock lock(doc);
            *keptIn = doc.getPdfFilepath();
        }
    }
    std::vector<PageRef> result;
    for (size_t i = 0; i < pages.size(); ++i) {
        auto copy = std::make_shared<XojPage>(*pages[i]);
        if (copy->getBackgroundType().isPdfPage() && !samePdf) {
            if (first != npos && pdfIndex[i] != npos) {
                copy->setBackgroundPdfPageNr(first + pdfIndex[i]);
            } else if (!pdfPages[i] || !pdfToImageBackground(*copy, *pdfPages[i])) {
                copy->setBackgroundType(PageType(PageTypeFormat::Plain));  // the annotations at least
            }
        }
        result.push_back(std::move(copy));
    }
    return result;
}

}  // namespace xqt
