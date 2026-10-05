#include "ElementFilter.h"

#include <cairo.h>

#include "model/Element.h"
#include "model/Layer.h"
#include "view/LayerView.h"
#include "view/View.h"

namespace xqt::render {

namespace {
thread_local const ElementFilter* current = nullptr;
}  // namespace

const ElementFilter* currentFilter() { return current; }

FilterScope::FilterScope(const ElementFilter* filter): before(current) {
    current = filter;
    if (filter) {
        xoj::view::LayerDrawer none = nullptr;
        xoj::view::layerDrawer.compare_exchange_strong(none, &drawFiltered);
    }
}

FilterScope::~FilterScope() { current = before; }

void drawLayer(const Layer& layer, const xoj::view::Context& ctx, const ElementFilter& filter) {
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    cairo_clip_extents(ctx.cr, &minX, &minY, &maxX, &maxY);
    for (const Element* e: layer.getElementsView()) {
        if (filter.shows(e) && e->intersectsArea(minX, minY, maxX - minX, maxY - minY)) {
            xoj::view::ElementView::createFromElement(e)->draw(ctx);
        }
    }
}

bool drawFiltered(const Layer& layer, const xoj::view::Context& ctx) {
    if (const ElementFilter* f = current) {
        drawLayer(layer, ctx, *f);
        return true;
    }
    return false;
}

}  // namespace xqt::render
