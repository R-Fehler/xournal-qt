#include "TemplateFile.h"

#include <fstream>
#include <memory>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "util/Color.h"

#include "DocumentSession.h"
#include "StickyNote.h"

namespace xqt::templates {

namespace {
/// The document's events go nowhere (it is never open in a session)
DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

bool writeBytes(const fs::path& target, const std::string& bytes) {
    fs::path part = target;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::error_code ec;
            fs::remove(part, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(part, target, ec);
    if (ec) {
        fs::remove(part, ec);
        return false;
    }
    return true;
}
}  // namespace

fs::path attachedPdfOf(const fs::path& xopp) {
    fs::path p = xopp;
    p += ".bg.pdf";
    return p;
}

PageRef makePage(PageRef page, const Options& options) {
    page->setBookmark(std::nullopt);  // (a copy starts without the bookmark, as a pasted page)
    if (!options.background) {
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        page->setBackgroundColor(Colors::white);
        page->setNoteSpace({});
        page->setBackgroundName(NO_BACKGROUND);
    }
    if (!options.content) {
        auto& layers = page->getLayers();
        for (auto it = layers.begin(); it != layers.end();) {
            Layer* l = *it;
            if (sticky::isNote(*l) || xoj::markdown::isMarkdownLayerName(l->getName())) {
                delete l;
                it = layers.erase(it);
            } else {
                auto gone = l->clearNoFree();  // (freed with it)
                ++it;
            }
        }
        if (layers.empty()) {
            layers.push_back(new Layer());
        }
        page->setSelectedLayerId(1);
    }
    return page;
}

bool withoutBackground(const XojPage& page) {
    return page.backgroundHasName() && page.getBackgroundName() == NO_BACKGROUND;
}

bool write(PageRef page, const std::string& pdf, const fs::path& target, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    auto doc = std::make_unique<Document>(&handler());
    const fs::path attached = attachedPdfOf(target);
    std::error_code ec;
    // (upstream's save keeps an attached PDF that is there already: never one of another template of this name)
    fs::remove(attached, ec);
    const bool pdfPage = page->getBackgroundType().isPdfPage();
    if (pdfPage) {
        if (pdf.empty()) {
            return fail("its PDF page could not be copied");
        }
        if (!writeBytes(attached, pdf)) {
            return fail("could not write \"" + attached.string() + "\"");
        }
        if (!doc->readPdf(attached, /*initPages=*/false, /*attachToDocument=*/true)) {
            fs::remove(attached, ec);
            return fail(doc->getLastErrorMsg());
        }
        page->setBackgroundPdfPageNr(0);
    }
    doc->addPage(std::move(page));
    const auto result = DocumentSession::writeDocument(*doc, target);
    if (!result.ok) {
        fs::remove(target, ec);
        fs::remove(attached, ec);
        return fail(result.error);
    }
    return true;
}

}  // namespace xqt::templates
