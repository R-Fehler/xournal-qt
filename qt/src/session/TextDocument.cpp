#include "TextDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>

#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "DocumentImages.h"
#include "MdBox.h"
#include "MdImages.h"
#include "MdPaginate.h"
#include "PageMargins.h"
#include "TextFile.h"

namespace xqt::TextDocument {

namespace {
std::string sliceOf(const PageRef& page) {
    const Text* box = pageBoxOf(page);
    return box ? box->getText() : std::string();
}
}  // namespace

Text* pageBoxOf(const PageRef& page) {
    const Layer* layer = page ? md::markdownLayer(page) : nullptr;
    return layer ? PageMargins::pageBox(*layer, page) : nullptr;
}

bool isTextDocument(Document& doc) {
    if (doc.getPageCount() == 0) {
        return false;
    }
    const PageRef first = doc.getPage(0);
    if (pageBoxOf(first)) {
        return true;
    }
    // An empty text: its box is not saved (upstream drops empty texts), its Markdown layer is, and nothing else is in it
    const Layer* layer = md::markdownLayer(first);
    return layer && layer->getElementsView().begin() == layer->getElementsView().end();
}

bool hasMarkdownText(Document& doc) {
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        if (pageBoxOf(doc.getPage(i))) {
            return true;
        }
    }
    return false;
}

std::string flowText(Document& doc, size_t first, size_t* end) {
    std::vector<std::string> slices;
    size_t i = first;
    for (; i < doc.getPageCount(); ++i) {
        const Text* box = pageBoxOf(doc.getPage(i));
        if (!box || (i > first && !md::continues(box->getText()))) {
            break;  // (the flow ends before this page)
        }
        slices.push_back(box->getText());
    }
    if (end) {
        *end = std::max(i, first + 1);
    }
    return slices.empty() ? std::string() : md::join(slices);
}

std::string markdown(Document& doc) {
    std::string out;
    for (size_t i = 0; i < doc.getPageCount();) {
        if (!pageBoxOf(doc.getPage(i))) {
            ++i;
            continue;
        }
        size_t end = i + 1;
        std::string text = flowText(doc, i, &end);
        if (!out.empty()) {
            // (another flow: it starts on a page of its own, as it did)
            while (!out.empty() && out.back() == '\n') {
                out.pop_back();
            }
            out += std::string("\n\n") + PAGE_BREAK + "\n\n";
        }
        out += text;
        i = end;
    }
    return out;
}

std::string markdownName(const std::string& pdfName) {
    std::string stem = pdfName;
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos && dot > 0) {
        stem.erase(dot);
    }
    constexpr std::string_view archive = ".archive";
    if (stem.size() > archive.size() && stem.compare(stem.size() - archive.size(), archive.size(), archive) == 0) {
        stem.erase(stem.size() - archive.size());
    }
    return (stem.empty() ? std::string("document") : stem) + ".md";
}

std::vector<Attachment> attachments(Document& doc, const std::string& pdfName) {
    std::vector<Attachment> out;
    if (isTextDocument(doc)) {
        Attachment md;
        md.name = markdownName(pdfName);
        md.data = flowText(doc);
        md.mime = "text/markdown";
        md.description = "The text of this PDF as Markdown (xournal-qt)";
        md.relationship = "/Alternative";
        out.push_back(std::move(md));
    }
    // The pictures of its Markdown (the text's, and those of Markdown boxes of any notes), under the paths their links
    // name (qt/docs/md-images.md)
    for (auto& [carried, data]: DocumentImages::picturesData(DocumentImages::carriedPicturesOf(doc))) {
        Attachment a;
        a.name = carried;
        a.data = std::move(data);
        a.mime = pictureMime(carried);
        a.description = "A picture of the Markdown of this PDF (xournal-qt)";
        a.relationship = "/Supplement";
        a.fixed = true;
        out.push_back(std::move(a));
    }
    return out;
}

std::string pictureMime(const std::string& name) {
    std::string ext = name.substr(name.find_last_of('.') == std::string::npos ? name.size() : name.find_last_of('.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::pair<const char*, const char*> kinds[] = {
            {".png", "image/png"},   {".jpg", "image/jpeg"},    {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
            {".webp", "image/webp"}, {".svg", "image/svg+xml"}, {".bmp", "image/bmp"},
    };
    for (const auto& [e, mime]: kinds) {
        if (ext == e) {
            return mime;
        }
    }
    return "application/octet-stream";
}

}  // namespace xqt::TextDocument
