#include "MarkdownFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <shared_mutex>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPaginate.h"
#include "TextFlow.h"
#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"

namespace xqt::MarkdownFile {

static_assert(TextFile::PAGE_MARGIN == TextFlow::MARGIN, "the session finds the page's text at the margins");

namespace {
/// Receives the events of the documents made here until a session owns them. It has no listeners.
DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

/// Where the text goes on a page: the page's text of MarkdownSession (at the margins of a plain page).
md::Frame frame() {
    return {PAGE_WIDTH - 2 * TextFlow::MARGIN, PAGE_HEIGHT - 2 * TextFlow::MARGIN};
}
}  // namespace

std::string read(const fs::path& file, size_t maxBytes, bool* cut) {
    if (cut) {
        *cut = false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string text(maxBytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    if (text.size() > maxBytes) {
        // Its start, up to the end of a line
        text.resize(maxBytes);
        const size_t line = text.rfind('\n');
        text.resize(line == std::string::npos ? maxBytes : line + 1);
        if (cut) {
            *cut = true;
        }
    }
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);  // a byte order mark
    }
    return text;
}

std::string plainText(const std::string& text, const std::string& language) {
    // A fence longer than every run of backticks in the text, so no line of it closes the block
    size_t longest = 0, run = 0;
    for (char c: text) {
        run = c == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const std::string fence(std::max<size_t>(3, longest + 1), '`');
    std::string source = fence + language + "\n" + text;
    if (!text.empty() && text.back() != '\n') {
        source += '\n';
    }
    return source + fence + "\n";
}

std::string readAsPlainText(const fs::path& file, size_t maxBytes, bool* cut) {
    std::string language = file.extension().string();
    if (!language.empty()) {
        language.erase(0, 1);
    } else {
        language = file.filename().string();  // "Makefile", "Dockerfile"
    }
    std::string lower = language;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower == "txt" || lower == "text" || lower == "log" || lower == "readme" || lower == "license" ||
        lower == "copying" || lower == "authors" || lower == "todo" ||
        language.find_first_of("` \t") != std::string::npos) {
        language.clear();  // plain text: no highlighting
    }
    return plainText(read(file, maxBytes, cut), language);
}

md::Style style() {
    md::Style s;
    s.family = "Sans";
    s.size = 11;
    s.color = Colors::black;
    s.width = frame().width;
    return s;
}

md::Style plainStyle() {
    md::Style s = style();
    s.family = "Monospace";  // (like a notepad: columns line up, code and LaTeX read as they are)
    s.size = 10;
    s.plain = true;
    return s;
}

md::Style style(const TextFile& file) { return file.kind() == TextFile::Kind::Plain ? plainStyle() : style(); }

namespace {
std::unique_ptr<Document> continuousDocument(const std::string& source, const md::Style& s);
}  // namespace

std::unique_ptr<Document> textDocument(const TextFile& file, bool continuous) {
    // (its pictures: the pages are laid out now, before a session registers the file's root)
    const md::images::RootHandle root(DocumentImages::markdownRoot(file.path()));
    return continuous ? continuousDocument(file.text(), style(file)) : document(file.text(), style(file));
}

double continuousHeight(double textHeight) { return std::max(PAGE_HEIGHT, textHeight + 2 * TextFlow::MARGIN); }

void relayout(DocumentSession& session, bool continuous) {
    if (!session.textFile()) {
        return;
    }
    session.clearSelectionEndText();  // (the text being written ends: its pages are made anew)
    const std::string text = session.currentText();
    session.setTextContinuous(continuous);
    const md::Style s = style(*session.textFile());
    std::unique_ptr<Document> made = continuous ? continuousDocument(text, s) : document(text, s);
    std::vector<PageRef> pages;
    for (size_t i = 0; i < made->getPageCount(); ++i) {
        pages.push_back(made->getPage(i));
    }
    session.applyPageOrder(pages, {});
    session.getUndoRedoHandler()->clearContents();  // (the steps before refer to the pages that went)
    session.textEdited();
}

void setText(DocumentSession& session, const std::string& text) {
    MarkdownSession md(session);
    md.begin(0, session.textFile() ? style(*session.textFile()) : style());
    md.update(text);
    md.finish();
}

std::unique_ptr<Document> document(const std::string& source, size_t maxPages) {
    return document(source, style(), maxPages);
}

namespace {
std::unique_ptr<Document> make(const std::string& source, const md::Style& s, size_t maxPages, bool firstBox) {
    md::installRenderer();  // (idempotent: the boxes are drawn formatted and are as big as they are drawn)
    auto doc = std::make_unique<Document>(&handler());
    const md::Frame f = frame();
    const md::Pagination pages = md::paginate(source, s, [f](size_t) { return f; });
    const size_t count = std::max<size_t>(1, std::min(maxPages, pages.slices.size()));
    for (size_t i = 0; i < count; ++i) {
        auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        page->setBackgroundColor(Colors::white);
        const bool slice = i < pages.slices.size() && !pages.slices[i].empty();
        if (slice || (i == 0 && firstBox)) {
            // The layer first: a text in a layer named "Markdown" is a Markdown text
            auto* layer = new Layer();
            layer->setName(std::string(xoj::markdown::LAYER_NAME));
            page->getLayers().insert(page->getLayers().begin(), layer);  // (the page owns it; at the bottom)
            auto box = std::make_unique<Text>();
            box->setTransformation(xoj::util::Matrix::TRANSLATION(TextFlow::MARGIN, TextFlow::MARGIN));
            box->setFont(XojFont(s.family, s.size));
            box->setColor(s.color);
            box->setWrap(f.width);
            box->setText(slice ? pages.slices[i] : std::string());
            Text* added = box.get();
            layer->addElement(std::move(box));
            added->getBoundingBox();  // (sizes are computed lazily, also by the renderers: once, here)
            page->setSelectedLayerId(2);  // the pen writes into the page's layer, above it (as after MarkdownSession)
        }
        doc->addPage(std::move(page));
    }
    return doc;
}
}  // namespace

std::unique_ptr<Document> document(const std::string& source, const md::Style& s, size_t maxPages) {
    return make(source, s, maxPages, false);
}

std::unique_ptr<Document> notesDocument(const std::string& source) {
    return make(source, style(), static_cast<size_t>(-1), true);
}

namespace {
std::unique_ptr<Document> continuousDocument(const std::string& source, const md::Style& s) {
    md::installRenderer();
    auto doc = std::make_unique<Document>(&handler());
    const md::Frame f{frame().width, CONTINUOUS_FRAME};
    const md::Pagination pages = md::onePage(source, s);
    auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    page->setBackgroundColor(Colors::white);
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().insert(page->getLayers().begin(), layer);
    auto box = std::make_unique<Text>();
    box->setTransformation(xoj::util::Matrix::TRANSLATION(TextFlow::MARGIN, TextFlow::MARGIN));
    box->setFont(XojFont(s.family, s.size));
    box->setColor(s.color);
    box->setWrap(f.width);
    box->setText(pages.slices.empty() ? std::string() : pages.slices.front());  // (all of it: one page)
    Text* added = box.get();
    layer->addElement(std::move(box));
    added->getBoundingBox();
    page->setSelectedLayerId(2);
    page->setSize(PAGE_WIDTH, continuousHeight(md::contentHeight(*added)));
    doc->addPage(std::move(page));
    return doc;
}
}  // namespace

std::vector<size_t> pageStarts(Document& doc) {
    std::vector<std::string> slices;
    {
        std::shared_lock lock(doc);
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            const Layer* layer = md::markdownLayer(doc.getPage(i));
            const Text* box = layer ? md::pageBoxOf(*layer, TextFlow::MARGIN, TextFlow::MARGIN) : nullptr;
            const std::string slice = box ? box->getText() : std::string();
            if (i > 0 && !md::continues(slice)) {
                break;  // the text ends before this page
            }
            slices.push_back(slice);
        }
    }
    std::vector<md::Part> parts;
    md::join(slices, &parts);
    std::vector<size_t> starts;
    for (const md::Part& p: parts) {
        starts.push_back(p.begin);
    }
    return starts;
}

size_t pageOf(Document& doc, size_t offset) {
    const std::vector<size_t> starts = pageStarts(doc);
    size_t page = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (starts[i] <= offset) {
            page = i;
        }
    }
    return page;
}

}  // namespace xqt::MarkdownFile
