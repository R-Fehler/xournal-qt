#include "StickyNote.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include <cairo.h>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PageRaster.h"
#include "util/Range.h"
#include "view/LayerView.h"
#include "view/View.h"

using xoj::util::Rectangle;

namespace xqt::sticky {

const std::vector<Color>& presetColors() {
    // Pastel yellow, pink, blue, green, orange (Material 100 shades): light enough for dark ink on them
    static const std::vector<Color> colors{Color(0xff, 0xf5, 0x9d), Color(0xf8, 0xbb, 0xd0), Color(0xb3, 0xe5, 0xfc),
                                           Color(0xc8, 0xe6, 0xc9), Color(0xff, 0xe0, 0xb2)};
    return colors;
}

bool isNoteName(const std::string& name) { return name == LAYER_NAME || name == COVER_LAYER_NAME; }

const Stroke* paperOf(const Layer& layer) {
    if (!layer.hasName() || !isNoteName(layer.getName())) {
        return nullptr;
    }
    const auto elements = layer.getElementsView();
    if (elements.size() == 0 || elements.front()->getType() != ELEMENT_STROKE) {
        return nullptr;
    }
    const auto* paper = static_cast<const Stroke*>(elements.front());
    if (paper->getFill() < 0 || paper->getPointCount() < 4) {
        return nullptr;  // (not a filled shape: an ordinary layer of that name)
    }
    return paper;
}

bool isNote(const Layer& layer) { return paperOf(layer) != nullptr; }

namespace {
Rectangle<double> rectOf(const Stroke& paper) {
    double x0 = std::numeric_limits<double>::max();
    double y0 = x0;
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = x1;
    for (const Point& p: paper.getPointVector()) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

std::vector<Point> paperPoints(const Rectangle<double>& r) {
    const double x1 = r.x + r.width;
    const double y1 = r.y + r.height;
    return {Point(r.x, r.y), Point(x1, r.y), Point(x1, y1), Point(r.x, y1), Point(r.x, r.y)};
}

bool contains(const Rectangle<double>& r, double x, double y) {
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}

std::mutex peekMutex;
std::unordered_set<const Layer*> peeking;
}  // namespace

std::optional<Look> lookOf(const Layer& layer) {
    const Stroke* paper = paperOf(layer);
    if (!paper) {
        return std::nullopt;
    }
    Look look;
    look.rect = rectOf(*paper);
    look.color = paper->getColor();
    look.cover = layer.getName() == COVER_LAYER_NAME;
    return look;
}

Layer* makeNote(const Look& look) {
    auto* layer = new Layer();
    layer->setName(look.cover ? COVER_LAYER_NAME : LAYER_NAME);
    auto paper = std::make_unique<Stroke>();
    paper->setToolType(StrokeTool::PEN);
    paper->setWidth(PAPER_WIDTH);
    paper->setColor(look.color);
    paper->setFill(255);
    paper->setPointVector(paperPoints(look.rect));
    layer->addElement(std::move(paper));
    return layer;
}

void applyLook(Layer& layer, const Look& from, const Look& to) {
    auto& elements = layer.getElements();
    if (elements.empty() || elements.front()->getType() != ELEMENT_STROKE) {
        return;
    }
    auto* paper = static_cast<Stroke*>(elements.front().get());
    paper->setPointVector(paperPoints(to.rect));
    paper->setColor(to.color);
    layer.setName(to.cover ? COVER_LAYER_NAME : LAYER_NAME);
    const double dx = to.rect.x - from.rect.x;
    const double dy = to.rect.y - from.rect.y;
    if (dx != 0 || dy != 0) {
        for (size_t i = 1; i < elements.size(); ++i) {
            elements[i]->move(dx, dy);
        }
    }
}

void changeLook(Document& doc, const PageRef& page, Layer& layer, const Look& from, const Look& to) {
    {
        std::unique_lock lock(doc);
        applyLook(layer, from, to);
    }
    // Only the paper shows (the content is clipped to it): the old and the new place
    Rectangle<double> area = from.rect;
    area.unite(to.rect);
    Range range(area);
    range.addPadding(PAPER_WIDTH + 1);
    page->fireRangeChanged(range);
}

Layer* noteAt(const XojPage& page, double x, double y) {
    const auto layers = page.getLayersView();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        Layer* layer = const_cast<Layer*>(*it);
        if (!layer->isVisible()) {
            continue;
        }
        if (const Stroke* paper = paperOf(*layer); paper && contains(rectOf(*paper), x, y)) {
            return layer;
        }
    }
    return nullptr;
}

bool hasNotes(const XojPage& page, bool visibleOnly) {
    for (const Layer* layer: page.getLayersView()) {
        if ((!visibleOnly || layer->isVisible()) && isNote(*layer)) {
            return true;
        }
    }
    return false;
}

Layer::Index layerIdOf(const XojPage& page, const Layer* layer) {
    const auto layers = page.getLayersView();
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i] == layer) {
            return i + 1;
        }
    }
    return 0;
}

bool leaveNoteLayer(Document& doc, const PageRef& page) {
    if (!page) {
        return false;
    }
    std::unique_lock lock(doc);
    const Layer::Index selected = page->getSelectedLayerId();
    const auto layers = page->getLayersView();
    if (selected == 0 || selected > layers.size() || !isNote(*layers[selected - 1])) {
        return false;
    }
    for (size_t i = layers.size(); i > 0; --i) {
        if (!isNote(*layers[i - 1])) {
            page->setSelectedLayerId(i);
            return true;
        }
    }
    // Only notes on this page: a layer for the page's own ink, below them
    auto& all = page->getLayers();  // (XojPage::insertLayer is for the LayerController)
    all.insert(all.begin(), new Layer());
    page->setSelectedLayerId(1);
    return true;
}

void setPeeking(const Layer* layer, bool peek) {
    std::lock_guard lock(peekMutex);
    if (peek) {
        peeking.insert(layer);
    } else {
        peeking.erase(layer);
    }
}

bool isPeeking(const Layer* layer) {
    std::lock_guard lock(peekMutex);
    return peeking.count(layer) > 0;
}

bool draw(const Layer& layer, const xoj::view::Context& ctx) {
    const Stroke* paper = paperOf(layer);
    if (!paper) {
        return false;
    }
    cairo_t* cr = ctx.cr;
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    cairo_clip_extents(cr, &minX, &minY, &maxX, &maxY);
    if (!paper->intersectsArea(minX, minY, maxX - minX, maxY - minY)) {
        return true;  // (nothing of the note in the area drawn: nothing of its content either)
    }
    const Rectangle<double> rect = rectOf(*paper);
    const bool screen = PageRaster::drawingForScreen();
    const bool cover = layer.getName() == COVER_LAYER_NAME;
    const bool peek = screen && cover && isPeeking(&layer);

    cairo_save(cr);
    if (peek) {
        cairo_push_group(cr);
    }
    xoj::view::ElementView::createFromElement(paper)->draw(ctx);
    cairo_save(cr);
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    cairo_clip(cr);
    cairo_clip_extents(cr, &minX, &minY, &maxX, &maxY);
    const auto elements = layer.getElementsView();
    for (size_t i = 1; i < elements.size(); ++i) {
        const Element* e = elements[i];
        if (e->intersectsArea(minX, minY, maxX - minX, maxY - minY)) {
            xoj::view::ElementView::createFromElement(e)->draw(ctx);
        }
    }
    cairo_restore(cr);
    const ColorU8 c = paper->getColor();
    const double dr = c.red / 255.0 * 0.8;
    const double dg = c.green / 255.0 * 0.8;
    const double db = c.blue / 255.0 * 0.8;
    if (screen && cover) {
        // A folded corner: this note covers (the pen does not write on it, a tap lets it peek)
        const double side = std::min({16.0, rect.width / 4, rect.height / 4});
        const double x1 = rect.x + rect.width;
        cairo_move_to(cr, x1 - side, rect.y);
        cairo_line_to(cr, x1, rect.y + side);
        cairo_line_to(cr, x1 - side, rect.y + side);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, dr, dg, db);
        cairo_fill(cr);
    }
    if (peek) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.25);
        // Its edge stays, dashed: where it goes back to when tapped again
        double px = 1;
        double py = 0;
        cairo_device_to_user_distance(cr, &px, &py);
        const double pixel = std::hypot(px, py);
        const double dash[] = {4 * pixel, 3 * pixel};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_set_line_width(cr, 1.5 * pixel);
        cairo_set_source_rgb(cr, dr * 0.8, dg * 0.8, db * 0.8);
        cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    return true;
}

void installDrawer() { xoj::view::layerDrawer.store(&draw); }

NoteUndoAction::NoteUndoAction(const PageRef& page, Layer* layer, const Look& before, const Look& after,
                               std::string text):
        UndoAction("StickyNoteUndoAction"), layer(layer), before(before), after(after), text(std::move(text)) {
    this->page = page;
}

bool NoteUndoAction::undo(Control* control) {
    changeLook(*control->getDocument(), page, *layer, after, before);
    this->undone = true;
    return true;
}

bool NoteUndoAction::redo(Control* control) {
    changeLook(*control->getDocument(), page, *layer, before, after);
    this->undone = false;
    return true;
}

}  // namespace xqt::sticky
