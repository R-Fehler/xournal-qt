#include "StickerFile.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "model/Element.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"

#include "DetachedDocument.h"
#include "DocumentSession.h"

using xoj::util::Rectangle;

namespace xqt::stickers {

namespace {
void unite(std::optional<Rectangle<double>>& all, const Rectangle<double>& r) {
    if (all) {
        all->unite(r);
    } else {
        all = r;
    }
}

bool isMarkdownText(const Element& e) {
    return e.getType() == ELEMENT_TEXT && static_cast<const Text&>(e).isMarkdown();
}

/// Trim ASCII white space
std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

/// Cut a UTF-8 text to at most `bytes`, at a space when there is one in its second half, never inside a character
std::string cutTo(const std::string& s, size_t bytes) {
    if (s.size() <= bytes) {
        return s;
    }
    size_t end = bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
        --end;  // (a continuation byte: back to the start of its character)
    }
    const size_t space = s.rfind(' ', end);
    if (space != std::string::npos && space > bytes / 2) {
        end = space;
    }
    return trimmed(s.substr(0, end));
}
}  // namespace

std::unique_ptr<Document> makeDocument(sticky::Group content, Color paper, const std::optional<Picture>& picture) {
    Rectangle<double> b = content.bounds;
    if (picture && picture->area.width > 0 && picture->area.height > 0) {
        b.unite(picture->area);
    }
    const double dx = MARGIN - b.x;
    const double dy = MARGIN - b.y;
    auto doc = newDetachedDocument();
    auto page = std::make_shared<XojPage>(std::max(1.0, b.width) + 2 * MARGIN, std::max(1.0, b.height) + 2 * MARGIN,
                                          /*suppressLayerCreation=*/true);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    page->setBackgroundColor(paper);

    // Bottom up: the picture, the Markdown boxes (the page's Markdown layer is at the bottom), the ink, the notes
    if (picture && !picture->png.empty()) {
        auto image = std::make_unique<Image>();
        image->setImage(std::string(picture->png));
        const auto& natural = image->getNaturalSize();
        if (natural.width > 0 && natural.height > 0) {
            image->setTransformation({picture->area.width / natural.width, 0, 0, picture->area.height / natural.height,
                                      {picture->area.x + dx, picture->area.y + dy}});
            auto* layer = new Layer();
            layer->setName(PICTURE_LAYER);
            layer->addElement(std::move(image));
            page->getLayers().push_back(layer);
        }
    }
    Layer* markdown = nullptr;
    Layer* ink = nullptr;
    for (size_t i = 0; i < content.elements.size(); ++i) {
        ElementPtr& e = content.elements[i];
        if (!e) {
            continue;
        }
        const bool md = i < content.markdown.size() ? content.markdown[i] : isMarkdownText(*e);
        if (md && !markdown) {
            markdown = new Layer();
            markdown->setName(std::string(xoj::markdown::LAYER_NAME));
        }
        if (!md && !ink) {
            ink = new Layer();
        }
        e->move(dx, dy);
        (md ? markdown : ink)->addElement(std::move(e));
    }
    if (markdown) {
        page->getLayers().push_back(markdown);
    }
    // (a sticker of notes alone still has its own layer: the selected one when Xournal++ opens it)
    Layer* own = ink ? ink : new Layer();
    page->getLayers().push_back(own);
    for (auto& note: content.notes) {
        if (const auto look = sticky::lookOf(*note)) {
            sticky::Look moved = *look;
            moved.rect.x += dx;
            moved.rect.y += dy;
            sticky::applyLook(*note, *look, moved);
            page->getLayers().push_back(note.release());
        }
    }
    // The layer for what is written on it in Xournal++: the ink's (layer ids count from 1)
    {
        const auto& layers = page->getLayers();
        const auto at = std::find(layers.begin(), layers.end(), own);
        page->setSelectedLayerId(at == layers.end() ? 1 : static_cast<Layer::Index>(at - layers.begin()) + 1);
    }
    doc->addPage(std::move(page));
    return doc;
}

bool write(Document& doc, const fs::path& target, std::string* error) {
    const auto result = DocumentSession::writeDocument(doc, target);
    if (!result.ok && error) {
        *error = result.error;
    }
    return result.ok;
}

std::optional<sticky::Group> read(const fs::path& file, std::string* error) {
    auto loaded = DocumentSession::loadFile(file);
    if (!loaded.document) {
        if (error) {
            *error = loaded.error;
        }
        return std::nullopt;
    }
    Document& doc = *loaded.document;
    std::shared_lock lock(doc);
    if (doc.getPageCount() == 0) {
        if (error) {
            *error = "no page";
        }
        return std::nullopt;
    }
    PageRef page = doc.getPage(0);
    sticky::Group group;
    std::optional<Rectangle<double>> bounds;
    // The elements in the order they are pasted (each lands on top of the one before): the picture, the Markdown
    // boxes, the rest bottom up
    std::vector<const Layer*> order;
    for (const Layer* l: page->getLayers()) {
        if (l->getName() == PICTURE_LAYER) {
            order.push_back(l);
        }
    }
    for (const Layer* l: page->getLayers()) {
        if (xoj::markdown::isMarkdownLayerName(l->getName())) {
            order.push_back(l);
        }
    }
    for (const Layer* l: page->getLayers()) {
        if (std::find(order.begin(), order.end(), l) == order.end() && !sticky::isNote(*l)) {
            order.push_back(l);
        }
    }
    for (const Layer* l: order) {
        const bool md = xoj::markdown::isMarkdownLayerName(l->getName());
        for (const Element* e: l->getElementsView()) {
            unite(bounds, e->getBoundingBox());
            group.elements.push_back(e->clone());
            group.markdown.push_back(md && e->getType() == ELEMENT_TEXT);
        }
    }
    for (const Layer* l: page->getLayers()) {
        if (const auto look = sticky::lookOf(*l)) {
            unite(bounds, look->rect);
            group.notes.emplace_back(l->clone());
        }
    }
    if (!bounds) {
        if (error) {
            *error = "nothing on its page";
        }
        return std::nullopt;
    }
    // xournal-qt: a sticker is pasted as a group (qt/docs/features/groups.md), one in each layer it goes into (its ink
    // and pictures; its Markdown boxes). Groups saved inside it give way to it: groups are flat. (Pasting gives them
    // new numbers.)
    for (const bool md: {false, true}) {
        const auto inLayer = static_cast<size_t>(std::count(group.markdown.begin(), group.markdown.end(), md));
        for (size_t i = 0; i < group.elements.size(); ++i) {
            if (group.markdown[i] == md) {
                group.elements[i]->setGroup(inLayer >= 2 ? (md ? 2 : 1) : 0);
            }
        }
    }
    group.bounds = *bounds;
    return group;
}

std::string clipboardBytes(const sticky::Group& content) {
    std::vector<const Layer*> notes;
    for (const auto& n: content.notes) {
        notes.push_back(n.get());
    }
    // (the Markdown flag goes by the element: a text that was a page's Markdown box is one)
    std::vector<const Element*> elements;
    for (const auto& e: content.elements) {
        elements.push_back(e.get());
    }
    return sticky::serializeGroup(content.bounds, notes, elements);
}

std::string suggestedName(const sticky::Group& content) {
    std::vector<const Element*> all;
    for (const auto& e: content.elements) {
        all.push_back(e.get());
    }
    for (const auto& n: content.notes) {
        for (const Element* e: n->getElementsView()) {
            all.push_back(e);
        }
    }
    for (const Element* e: all) {
        if (!e || e->getType() != ELEMENT_TEXT) {
            continue;
        }
        const std::string& text = static_cast<const Text*>(e)->getText();
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string line = text.substr(start, end - start);
            start = end + 1;
            // (Markdown's marks at the start of a line: headings, quotes, lists, task boxes)
            size_t i = 0;
            while (i < line.size() && std::string_view("#>*-+ \t").find(line[i]) != std::string_view::npos) {
                ++i;
            }
            if (line.compare(i, 4, "[ ] ") == 0 || line.compare(i, 4, "[x] ") == 0 || line.compare(i, 4, "[X] ") == 0) {
                i += 4;
            }
            line = line.substr(i);
            line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c == '*' || c == '_' || c == '`'; }),
                       line.end());
            line = fileNameOf(line);
            if (!line.empty()) {
                return cutTo(line, 40);
            }
        }
    }
    return {};
}

std::string fileNameOf(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    bool space = false;
    for (const char c: name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos;
        if (bad || c == ' ') {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += c;
    }
    // (not hidden: no dot at the start; no dot at the end, which Windows drops)
    while (!out.empty() && out.front() == '.') {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    return cutTo(trimmed(out), 80);
}

}  // namespace xqt::stickers
