#include "PageFiles.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <shared_mutex>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/XojPage.h"

#include "DocumentSession.h"
#include "HybridPdf.h"

namespace xqt::pagefiles {

namespace {
/// The events of a document that is never open in a session go nowhere
DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

std::string trimmed(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

/// A page number of a part ("" is none); false when it is not one
bool number(const std::string& s, size_t& out) {
    const std::string t = trimmed(s);
    if (t.empty() || t.size() > 9 || !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }
    out = static_cast<size_t>(std::stoul(t));
    return true;
}

/// A copy of a page as a save copies it (DocumentSave.cpp): its layers' visibility and its background's name too
PageRef copyOf(const PageRef& page) {
    struct Access: XojPage {
        using XojPage::setLayerVisible;  // (for the LayerController only)
    };
    constexpr auto setLayerVisible = &Access::setLayerVisible;
    auto copy = std::make_shared<XojPage>(*page);
    for (Layer::Index i = 0; i <= page->getLayerCount(); ++i) {  // (0: the background)
        ((*copy).*setLayerVisible)(i, page->isLayerVisible(i));
    }
    if (page->backgroundHasName()) {
        copy->setBackgroundName(page->getBackgroundName());
    }
    return copy;
}
}  // namespace

std::vector<size_t> parseRange(const std::string& text, size_t count, std::string* error) {
    std::vector<size_t> pages;
    auto fail = [&](const std::string& why) {
        if (error) {
            *error = why;
        }
        return std::vector<size_t>{};
    };
    std::string normal = text;
    for (char& c: normal) {
        if (c == ';') {
            c = ',';
        }
    }
    // (an en dash, as rangeText's readers may type it: a hyphen)
    for (size_t at = normal.find("\xe2\x80\x93"); at != std::string::npos; at = normal.find("\xe2\x80\x93", at)) {
        normal.replace(at, 3, "-");
    }
    size_t start = 0;
    bool any = false;
    while (start <= normal.size()) {
        size_t end = normal.find(',', start);
        if (end == std::string::npos) {
            end = normal.size();
        }
        const std::string part = trimmed(normal.substr(start, end - start));
        start = end + 1;
        if (part.empty()) {
            continue;
        }
        any = true;
        size_t from = 0;
        size_t to = 0;
        if (const size_t dash = part.find('-'); dash != std::string::npos) {
            const std::string a = trimmed(part.substr(0, dash));
            const std::string b = trimmed(part.substr(dash + 1));
            if (a.empty() && b.empty()) {
                return fail("\"" + part + "\" is not a page range");
            }
            if ((!a.empty() && !number(a, from)) || (!b.empty() && !number(b, to))) {
                return fail("\"" + part + "\" is not a page range");
            }
            if (a.empty()) {
                from = 1;
            }
            if (b.empty()) {
                to = count;
            }
        } else if (number(part, from)) {
            to = from;
        } else {
            return fail("\"" + part + "\" is not a page number");
        }
        if (from == 0 || to == 0) {
            return fail("Pages are counted from 1");
        }
        if (from > to) {
            std::swap(from, to);
        }
        for (size_t p = from; p <= std::min(to, count); ++p) {
            pages.push_back(p - 1);
        }
    }
    if (!any) {
        return fail("No pages given");
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    if (pages.empty()) {
        return fail(count == 1 ? "The document has 1 page" : "The document has " + std::to_string(count) + " pages");
    }
    return pages;
}

std::string rangeText(const std::vector<size_t>& unsorted) {
    std::vector<size_t> pages = unsorted;
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    std::string out;
    for (size_t i = 0; i < pages.size();) {
        size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) {
            ++j;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += std::to_string(pages[i] + 1);
        if (j > i) {
            out += "-" + std::to_string(pages[j] + 1);
        }
        i = j + 1;
    }
    return out;
}

std::vector<std::vector<size_t>> splitEvery(size_t count, size_t every) {
    std::vector<std::vector<size_t>> parts;
    if (every == 0) {
        return parts;
    }
    for (size_t p = 0; p < count; ++p) {
        if (p % every == 0) {
            parts.emplace_back();
        }
        parts.back().push_back(p);
    }
    return parts;
}

std::vector<std::vector<size_t>> splitAt(size_t count, std::vector<size_t> starts) {
    starts.push_back(0);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    std::vector<std::vector<size_t>> parts;
    for (size_t i = 0; i < starts.size() && starts[i] < count; ++i) {
        const size_t end = i + 1 < starts.size() ? std::min(starts[i + 1], count) : count;
        parts.emplace_back();
        for (size_t p = starts[i]; p < end; ++p) {
            parts.back().push_back(p);
        }
    }
    return parts;
}

std::unique_ptr<Document> subset(Document& doc, const std::vector<size_t>& pages) {
    auto copy = std::make_unique<Document>(&handler());
    std::shared_lock lock(doc);
    copy->setFilepath(doc.getFilepath());
    copy->setPdfAttributes(doc.getPdfFilepath(), false);
    copy->setPathStorageMode(doc.getPathStorageMode());
    std::vector<PageRef> copies;
    for (size_t i: pages) {
        if (i < doc.getPageCount()) {
            copies.push_back(copyOf(doc.getPage(i)));
        }
    }
    copy->addPages(copies.begin(), copies.end());
    return copy;
}

Result write(Document& doc, size_t pdfPageCount, const fs::path& target, const PdfEncryption::Encryption& encryption) {
    Result r;
    std::string ext = target.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".pdf") {
        HybridPdf::WriteOptions options;
        options.compact = true;  // (a new file, in one piece)
        options.encryption = encryption;
        const auto w = HybridPdf::write(doc, target, {}, pdfPageCount, {}, options);
        r.ok = w.ok;
        r.error = w.error;
        return r;
    }
    bool anyPdfPage = false;
    {
        std::shared_lock lock(doc);
        for (size_t i = 0; i < doc.getPageCount() && !anyPdfPage; ++i) {
            anyPdfPage = doc.getPage(i)->getBackgroundType().isPdfPage();
        }
    }
    // Its PDF pages in "name.pdf" next to it (no PDF at all when no page shows one)
    const fs::path pdf = DocumentSession::exportPdfFor(target);
    std::error_code ec;
    const bool pdfThere = fs::exists(pdf, ec);
    const auto w = HybridPdf::exportXopp(doc, target, anyPdfPage ? pdf : fs::path(target.string() + ".bg.pdf"),
                                         pdfPageCount, /*attached=*/!anyPdfPage);
    r.ok = w.ok;
    r.error = w.error;
    if (!r.ok && anyPdfPage && !pdfThere) {
        fs::remove(pdf, ec);  // (the .xopp is written last, renamed into place: not there)
    }
    return r;
}

std::string imageName(const std::string& stem, size_t page, size_t pageCount, const std::string& extension) {
    const int digits = std::max(3, static_cast<int>(std::to_string(std::max<size_t>(pageCount, 1)).size()));
    char number[32];
    std::snprintf(number, sizeof number, "%0*zu", digits, page + 1);
    return stem + "-p" + number + extension;
}

}  // namespace xqt::pagefiles
