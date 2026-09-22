#include "MdBox.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <string_view>
#include <utility>

#include <pango/pango.h>

#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/Matrix.h"
#include "view/MarkdownHook.h"

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
    return s;
}

const Layout& cachedLayout(const std::string& source, const Style& style) {
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
           std::to_string(style.width);
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->key == key) {
            cache.splice(cache.begin(), cache, it);
            return cache.front().layout;
        }
    }
    cache.push_front({std::move(key), layout(parse(source), style)});
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

void installRenderer() { xoj::view::markdownTextRenderer.store(&drawText, std::memory_order_release); }

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

std::vector<Rect> findText(const Text& text, const std::string& search) {
    const auto& shift = text.getTransformation().shift;
    auto found = findText(cachedLayout(text.getText(), styleOf(text)), search);
    for (Rect& r: found) {
        r.x += shift.x;
        r.y += shift.y;
    }
    return found;
}

bool isMarkdownLayer(const Layer& layer) {
    return layer.hasName() && layer.getName() == xoj::view::MARKDOWN_LAYER_NAME;
}

Layer* markdownLayer(const PageRef& page) {
    for (Layer* l: page->getLayers()) {
        if (isMarkdownLayer(*l)) {
            return l;
        }
    }
    return nullptr;
}

Text* boxOf(const Layer& layer) {
    for (const auto& e: layer.getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            return const_cast<Text*>(static_cast<const Text*>(e));
        }
    }
    return nullptr;
}

}  // namespace xqt::md
