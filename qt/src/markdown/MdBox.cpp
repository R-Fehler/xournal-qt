#include "MdBox.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <pango/pango.h>

#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"
#include "model/MarkdownText.h"
#include "EmojiFont.h"
#include "MdImages.h"

namespace xqt::md {

Style styleOf(const Text& text) {
    Style s;
    // (a font name may have a style, e.g. "Sans Bold": the family only)
    PangoFontDescription* d = pango_font_description_from_string(text.getFontName().c_str());
    if (const char* family = pango_font_description_get_family(d); family && *family) {
        s.family = family;
    }
    pango_font_description_free(d);
    if (text.getFontSize() > 0) {
        s.size = text.getFontSize();
    }
    s.color = text.getColor();
    s.color.alpha = 0xff;
    s.width = text.getWrap() > 0 ? text.getWrap() : DEFAULT_WIDTH;
    s.plain = isPlain(text.getText());  // (a plain text's pages go on as plain text)
    return s;
}

const Layout& cachedLayout(const std::string& source, const Style& style, size_t active) {
    struct Entry {
        std::string key;
        Layout layout;
    };
    // Per thread: layouts hold Pango objects, which must stay on one thread. A few entries: the boxes of the pages
    // being drawn (each tile of a page draws its box again).
    static thread_local std::list<Entry> cache;
    constexpr size_t SIZE = 8;

    std::string key = source;
    key += '\x1f';
    key += style.family;
    key += '\x1f';
    key += std::to_string(style.size) + '/' + std::to_string(uint32_t(style.color)) + '/' +
           std::to_string(style.width) + '@' + std::to_string(active) + '#' +
           std::to_string(images::generation());  // (a picture written or fetched: laid out again)
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->key == key) {
            cache.splice(cache.begin(), cache, it);
            return cache.front().layout;
        }
    }
    cache.push_front({std::move(key), layout(parse(source), style, source, active)});
    if (cache.size() > SIZE) {
        cache.pop_back();
    }
    return cache.front().layout;
}

void drawText(const Text& text, cairo_t* cr) {
    cairo_save(cr);
    text.getTransformation().transformCairo(cr);
    draw(cr, cachedLayout(text.getText(), styleOf(text)));
    cairo_restore(cr);
}

xoj::markdown::Size drawnSize(const Text& text) {
    const Style s = styleOf(text);
    // (at least a line high: an empty box can be tapped as well)
    return {s.width, std::max(cachedLayout(text.getText(), s).height, s.size * 1.25)};
}

void installRenderer() {
    xoj::markdown::sizer.store(&drawnSize, std::memory_order_release);
    xoj::markdown::renderer.store(&drawText, std::memory_order_release);
    xoj::markdown::layoutPainter.store(&xqt::emoji::showLayout, std::memory_order_release);
}

double contentHeight(const Text& text) { return cachedLayout(text.getText(), styleOf(text)).height; }

xoj::util::Rectangle<double> boxRect(const Text& text) {
    const Style s = styleOf(text);
    const xoj::util::Matrix& m = text.getTransformation();
    const auto a = m * xoj::util::Point<double>(0, 0);
    const auto b = m * xoj::util::Point<double>(s.width, contentHeight(text));
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

std::optional<LinkHit> linkAt(const Text& text, double x, double y) {
    // (boxes are moved, not scaled or rotated: the translation is enough)
    const auto& shift = text.getTransformation().shift;
    auto hit = linkAt(cachedLayout(text.getText(), styleOf(text)), x - shift.x, y - shift.y);
    if (hit) {
        hit->x += shift.x;
        hit->y += shift.y;
    }
    return hit;
}

std::vector<LinkHit> linkBoxes(const Text& text) {
    const auto& shift = text.getTransformation().shift;
    std::vector<LinkHit> boxes = linkBoxes(cachedLayout(text.getText(), styleOf(text)));
    for (LinkHit& b: boxes) {
        b.x += shift.x;
        b.y += shift.y;
    }
    return boxes;
}

std::optional<MathHit> mathAt(const Text& text, double x, double y) {
    const auto& shift = text.getTransformation().shift;
    auto hit = mathAt(cachedLayout(text.getText(), styleOf(text)), x - shift.x, y - shift.y);
    if (hit) {
        hit->rect.x += shift.x;
        hit->rect.y += shift.y;
    }
    return hit;
}

std::optional<std::string> imageButtonAt(const Text& text, double x, double y) {
    const auto& shift = text.getTransformation().shift;
    if (const auto hit = imageButtonAt(cachedLayout(text.getText(), styleOf(text)), x - shift.x, y - shift.y)) {
        return hit->url;
    }
    return std::nullopt;
}

std::optional<size_t> checkBoxAt(const Text& text, double x, double y) {
    const auto& shift = text.getTransformation().shift;
    if (const auto box = checkBoxAt(cachedLayout(text.getText(), styleOf(text)), x - shift.x, y - shift.y)) {
        return box->mark;
    }
    return std::nullopt;
}

namespace {
struct Writing {
    std::mutex mtx;
    std::unordered_map<const Text*, size_t> cursors;
};
Writing& writing() {
    static Writing w;
    return w;
}
}  // namespace

void setWritingCursor(const Text& text, size_t active) {
    std::lock_guard lock(writing().mtx);
    if (active == NO_SOURCE) {
        writing().cursors.erase(&text);
    } else {
        writing().cursors[&text] = active;
    }
}

namespace {
/// The layout a box is drawn with: while it is written on the page, as the editor draws it.
const Layout& shownLayout(const Text& text) {
    size_t active = NO_SOURCE;
    if (text.isInEditing()) {
        std::lock_guard lock(writing().mtx);
        if (auto it = writing().cursors.find(&text); it != writing().cursors.end()) {
            active = it->second;
        }
    }
    return cachedLayout(text.getText(), styleOf(text), active);
}
}  // namespace

std::vector<std::string> shownTexts(const Text& text) {
    std::vector<std::string> texts;
    for (const Item& it: shownLayout(text).items) {
        if (it.kind == Item::Kind::Text && it.layout) {
            texts.push_back(searchText(it));  // (with the sources of its formulas)
        }
    }
    return texts;
}

std::vector<Rect> shownRects(const Text& text, size_t index, int from, int to) {
    const auto& shift = text.getTransformation().shift;
    size_t i = 0;
    for (const Item& it: shownLayout(text).items) {
        if (it.kind != Item::Kind::Text || !it.layout) {
            continue;
        }
        if (i++ == index) {
            const auto [a, b] = layoutRange(it, from, to);
            auto rects = textRects(it, a, b);
            for (Rect& r: rects) {
                r.x += shift.x;
                r.y += shift.y;
            }
            return rects;
        }
    }
    return {};
}

std::vector<Rect> findText(const Text& text, const std::string& search) {
    const auto& shift = text.getTransformation().shift;
    auto found = findText(shownLayout(text), search);
    for (Rect& r: found) {
        r.x += shift.x;
        r.y += shift.y;
    }
    return found;
}

bool isMarkdownLayer(const Layer& layer) {
    return layer.hasName() && xoj::markdown::isMarkdownLayerName(layer.getName());
}

Layer* markdownLayer(const PageRef& page) {
    for (Layer* l: page->getLayers()) {
        if (isMarkdownLayer(*l)) {
            return l;
        }
    }
    return nullptr;
}

Text* pageBoxOf(const Layer& layer, double x, double y) {
    for (const auto& e: layer.getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            const auto& at = static_cast<const Text*>(e)->getTransformation().shift;
            if (std::abs(at.x - x) < 0.5 && std::abs(at.y - y) < 0.5) {
                return const_cast<Text*>(static_cast<const Text*>(e));
            }
        }
    }
    return nullptr;
}

Text* boxAt(const Layer& layer, double x, double y) {
    Text* found = nullptr;
    for (const auto& e: layer.getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            const auto* t = static_cast<const Text*>(e);
            const auto r = boxRect(*t);
            // (at least a line high: an empty box can be tapped as well)
            if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + std::max(r.height, t->getFontSize() * 1.5)) {
                found = const_cast<Text*>(t);  // (the last one is on top)
            }
        }
    }
    return found;
}

double defaultFontSize(double textFontSize) { return std::max(4.0, std::round(textFontSize * 0.6)); }

Text* boxOf(const Layer& layer) {
    for (const auto& e: layer.getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            return const_cast<Text*>(static_cast<const Text*>(e));
        }
    }
    return nullptr;
}

}  // namespace xqt::md
