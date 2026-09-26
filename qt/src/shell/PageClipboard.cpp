#include "PageClipboard.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "session/PdfPageKeeper.h"
#include "util/Util.h"

namespace xqt {

namespace {
std::string stampOf(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) {
        return {};
    }
    const auto time = fs::last_write_time(p, ec);
    return std::to_string(size) + ":" + std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}
}  // namespace

void PageClipboard::copy(DocumentSession& session, const std::vector<size_t>& indices, bool withPdf) {
    Document& doc = *session.getDocument();
    if (withPdf) {
        // Pages pasted just now from another PDF: their PDF pages are taken along once they are in the merged PDF
        bool pending = false;
        {
            std::shared_lock lock(doc);
            for (size_t i: indices) {
                if (i < doc.getPageCount()) {
                    const PageRef page = doc.getPage(i);
                    pending = pending || (page->getBackgroundType().isPdfPage() &&
                                          page->getPdfPageNr() >= doc.getPdfPageCount());
                }
            }
        }
        if (pending) {
            session.waitForMerges();
        }
    }
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

std::vector<PageRef> PageClipboard::pagesFor(DocumentSession& target, bool* addedToMergedPdf) const {
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
        if (merged.session == target.serial() && merged.pdf == targetPdf &&
            stampOf(targetPdf) == merged.stamp) {
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
        if (first != npos && addedToMergedPdf) {
            *addedToMergedPdf = true;
        }
    }
    std::vector<PageRef> result;
    for (size_t i = 0; i < pages.size(); ++i) {
        auto copy = std::make_shared<XojPage>(*pages[i]);
        copy->setBookmark(std::nullopt);  // (a copy starts without the bookmark, qt/docs/bookmarks.md)
        if (copy->getBackgroundType().isPdfPage() && !samePdf) {
            if (first != npos && pdfIndex[i] != npos) {
                copy->setBackgroundPdfPageNr(first + pdfIndex[i]);
            } else if (!pdfPages[i] || !PdfPageKeeper::toImageBackground(*copy, *pdfPages[i], IMAGE_DPI)) {
                copy->setBackgroundType(PageType(PageTypeFormat::Plain));  // the annotations at least
            }
        }
        result.push_back(std::move(copy));
    }
    return result;
}

}  // namespace xqt
