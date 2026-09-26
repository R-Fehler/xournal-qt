#include "TextDocument.h"

#include <algorithm>
#include <string_view>

#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "MdBox.h"
#include "MdPaginate.h"
#include "TextFile.h"

namespace xqt::TextDocument {

namespace {
/// The left margin of the page's text (as TextFlow::styleFor: a ruled page with a margin line starts after it).
double leftMargin(const PageRef& page) {
    double left = TextFile::PAGE_MARGIN;
    const PageType bg = page->getBackgroundType();
    if (bg.format == PageTypeFormat::Lined) {
        double margin = 72;  // (upstream's default: 1 inch; negative: on the right)
        BackgroundConfig(bg.config).loadValue(background_config_strings::CFG_MARGIN, margin);
        if (margin >= 0) {
            left = std::max(left, margin + 10);
        }
    }
    return left;
}

std::string sliceOf(const PageRef& page) {
    const Text* box = pageBoxOf(page);
    return box ? box->getText() : std::string();
}
}  // namespace

Text* pageBoxOf(const PageRef& page) {
    const Layer* layer = page ? md::markdownLayer(page) : nullptr;
    return layer ? md::pageBoxOf(*layer, leftMargin(page), TextFile::PAGE_MARGIN) : nullptr;
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
    if (!isTextDocument(doc)) {
        return out;
    }
    Attachment md;
    md.name = markdownName(pdfName);
    md.data = flowText(doc);
    md.mime = "text/markdown";
    md.description = "The text of this PDF as Markdown (xournal-qt)";
    md.relationship = "/Alternative";
    out.push_back(std::move(md));
    // (qt/md-images: the images of the text as "name.assets/…")
    return out;
}

}  // namespace xqt::TextDocument
