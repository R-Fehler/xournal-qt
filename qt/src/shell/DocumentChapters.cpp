#include "DocumentChapters.h"

#include <algorithm>
#include <shared_mutex>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "MdBox.h"

namespace xqt::DocumentChapters {

namespace {
/// "## Title" -> level 1 and "Title"; nothing when the text does not start with marks.
bool markedHeading(const std::string& text, int& level, std::string& title) {
    size_t marks = 0;
    while (marks < text.size() && text[marks] == '#') {
        ++marks;
    }
    if (marks == 0 || marks > 3 || marks >= text.size() || text[marks] != ' ') {
        return false;
    }
    const size_t end = text.find('\n');
    title = text.substr(marks + 1, end == std::string::npos ? std::string::npos : end - marks - 1);
    level = static_cast<int>(marks) - 1;
    return !title.empty();
}

/// A heading written in the text mode: bold and one of its sizes.
bool styledHeading(const Text* text, int& level) {
    const std::string& font = text->getFontName();
    if (font.find("Bold") == std::string::npos) {
        return false;
    }
    const double size = text->getFontSize();
    for (int l = 0; l < 3; ++l) {
        if (std::abs(size - headingSize(l)) < 0.01) {
            level = l;
            return true;
        }
    }
    return false;
}
}  // namespace

double headingSize(int level) {
    switch (level) {
        case 0:
            return 24;
        case 1:
            return 18;
        default:
            return 15;
    }
}

std::string headingText(const std::string& title, int level) {
    return std::string(static_cast<size_t>(std::clamp(level, 0, 2) + 1), '#') + " " + title;
}

std::vector<Chapter> find(Document& document) {
    std::vector<Chapter> chapters;
    std::shared_lock lock(document);
    for (size_t p = 0; p < document.getPageCount(); ++p) {
        const PageRef page = document.getPage(p);
        if (!page) {
            continue;
        }
        std::vector<std::pair<double, Chapter>> onThisPage;  // sorted by their place on the page
        for (const Layer* layer: page->getLayersView()) {
            if (!layer->isVisible()) {
                continue;
            }
            for (const Element* element: layer->getElementsView()) {
                if (element->getType() != ELEMENT_TEXT) {
                    continue;
                }
                const auto* text = static_cast<const Text*>(element);
                if (text->isMarkdown()) {
                    // A Markdown box: its headings 1-3, where they are drawn
                    const md::Document doc = md::parse(text->getText());
                    const md::Layout& laid = md::cachedLayout(text->getText(), md::styleOf(*text));
                    for (size_t i = 0; i < doc.root.children.size() && i < laid.blocks.size(); ++i) {
                        const md::Block& b = doc.root.children[i];
                        if (b.kind == md::BlockKind::Heading && b.level <= 3 && !md::plainText(b).empty()) {
                            Chapter chapter;
                            chapter.page = p;
                            chapter.title = md::plainText(b);
                            chapter.level = b.level - 1;
                            onThisPage.emplace_back(text->getTransformation().shift.y + laid.blocks[i].top,
                                                    std::move(chapter));
                        }
                    }
                    continue;
                }
                Chapter chapter;
                chapter.page = p;
                std::string title;
                int level = 0;
                if (markedHeading(text->getText(), level, title)) {
                    chapter.title = title;
                    chapter.level = level;
                } else if (styledHeading(text, level)) {
                    const std::string& content = text->getText();
                    const size_t end = content.find('\n');
                    chapter.title = content.substr(0, end);
                    chapter.level = level;
                } else {
                    continue;
                }
                if (!chapter.title.empty()) {
                    onThisPage.emplace_back(text->getBoundingBox().y, std::move(chapter));
                }
            }
        }
        std::stable_sort(onThisPage.begin(), onThisPage.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [y, chapter]: onThisPage) {
            chapters.push_back(std::move(chapter));
        }
    }
    return chapters;
}

}  // namespace xqt::DocumentChapters
