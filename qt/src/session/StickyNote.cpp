#include "StickyNote.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include <cairo.h>

#include "config.h"
#include "control/Control.h"
#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Image.h"
#include "model/Link.h"
#include "model/TexImage.h"
#include "model/Text.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/PageRaster.h"
#include "util/Range.h"
#include "util/serializing/BinObjectEncoding.h"
#include "util/serializing/InputStreamException.h"
#include "util/serializing/ObjectInputStream.h"
#include "util/serializing/ObjectOutputStream.h"
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

// The shadow under a note (points): a little to the bottom right, soft. A blur without a bitmap: three rectangles of
// low opacity, each reaching a little further, so that the shade fades outwards (a few paths in a PDF, little to fill
// on a screen). Its cost is measured by StickyNoteTest.benchmarkTheLook (qt/docs/sticky-notes.md).
constexpr double SHADOW_DX = 0.8;
constexpr double SHADOW_DY = 1.4;
constexpr int SHADOW_RINGS = 3;
constexpr double SHADOW_STEP = 0.8;
constexpr double SHADOW_ALPHA = 0.055;  ///< each one's (black): about 16 % where all three lie
static_assert(SHADOW_DY + SHADOW_RINGS * SHADOW_STEP <= DRAWN_MARGIN && SHADOW_DX <= SHADOW_DY);

std::atomic<Finish> finishNow{Finish::Full};

void drawShadow(cairo_t* cr, const Rectangle<double>& r) {
    const double x1 = r.x + r.width;
    const double y1 = r.y + r.height;
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, SHADOW_ALPHA);
    // Each reaches a step further out at the bottom and the right and begins a step nearer the other corners (the
    // shade fades there): nothing shows above or left of the note. Only what the paper does not hide is filled: a
    // strip at the right and one at the bottom (boxes: cairo's quickest fill).
    for (int i = 1; i <= SHADOW_RINGS; ++i) {
        const double out = i * SHADOW_STEP;
        const double in = (SHADOW_RINGS + 1 - i) * SHADOW_STEP;
        const double right = x1 + SHADOW_DX + out;
        const double bottom = y1 + SHADOW_DY + out;
        cairo_rectangle(cr, x1, r.y + SHADOW_DY + in, right - x1, y1 - (r.y + SHADOW_DY + in));
        cairo_rectangle(cr, r.x + SHADOW_DX + in, y1, right - (r.x + SHADOW_DX + in), bottom - y1);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

/// The paper's outline as a path (its points: a rectangle, unless upstream Xournal++ changed it)
void paperPath(cairo_t* cr, const Stroke& paper) {
    cairo_new_path(cr);
    for (const Point& p: paper.getPointVector()) {
        cairo_line_to(cr, p.x, p.y);
    }
    cairo_close_path(cr);
}
}  // namespace

Color edgeColor(Color paper) {
    const auto shade = [](uint8_t c) { return static_cast<uint8_t>(std::lround(c * EDGE_SHADE)); };
    return Color(shade(paper.red), shade(paper.green), shade(paper.blue));
}

Rectangle<double> drawnRect(const Rectangle<double>& r) {
    return {r.x - DRAWN_MARGIN, r.y - DRAWN_MARGIN, r.width + 2 * DRAWN_MARGIN, r.height + 2 * DRAWN_MARGIN};
}

void setFinish(Finish finish) { finishNow.store(finish, std::memory_order_relaxed); }

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
    // Only the note shows (the content is clipped to it): the old and the new place
    Rectangle<double> area = from.rect;
    area.unite(to.rect);
    Range range(area);
    range.addPadding(DRAWN_MARGIN + 1);  // (its shadow)
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

namespace {
thread_local std::optional<Rectangle<double>> changingNote;

/// The clipboard's note: its layer's name, then each element in a stream of its own. Upstream's ObjectInputStream
/// copies its whole buffer for each stroke it reads (ObjectInputStream::readData), so one stream for a note with
/// hundreds of strokes took quadratic time (15 ms to read a note with 300 strokes, now 1-3 ms). The name changed
/// with the format: a note copied by an older version is not pasted (not misread).
constexpr const char* CLIPBOARD_OBJECT = "StickyNote2";
}  // namespace

NoteLayerChange::NoteLayerChange(const Layer& layer): before(changingNote) {
    if (const auto look = lookOf(layer)) {
        changingNote = drawnRect(look->rect);
    } else {
        changingNote.reset();
    }
}

NoteLayerChange::~NoteLayerChange() { changingNote = before; }

std::optional<Rectangle<double>> changingNoteArea() { return changingNote; }

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
    const Rectangle<double> rect = rectOf(*paper);
    const Rectangle<double> drawn = drawnRect(rect);
    if (drawn.x > maxX || drawn.y > maxY || drawn.x + drawn.width < minX || drawn.y + drawn.height < minY) {
        return true;  // (nothing of the note in the area drawn: nothing of its content either)
    }
    const Finish finish = finishNow.load(std::memory_order_relaxed);
    const bool screen = PageRaster::drawingForScreen();
    const bool cover = layer.getName() == COVER_LAYER_NAME;
    const bool peek = screen && cover && isPeeking(&layer);
    const ColorU8 c = paper->getColor();

    cairo_save(cr);
    if (peek) {
        cairo_push_group(cr);
    }
    // Only an area inside the paper drawn again (what is written on it): no edge, no shadow to draw
    const double inset = EDGE_WIDTH;
    const bool inside = minX >= rect.x + inset && minY >= rect.y + inset && maxX <= rect.x + rect.width - inset &&
                        maxY <= rect.y + rect.height - inset;
    if (finish == Finish::Flat) {
        xoj::view::ElementView::createFromElement(paper)->draw(ctx);
    } else {
        if (finish == Finish::Full && !inside) {
            drawShadow(cr, rect);
        }
        paperPath(cr, *paper);
        cairo_set_source_rgba(cr, c.red / 255.0, c.green / 255.0, c.blue / 255.0, paper->getFill() / 255.0);
        cairo_fill(cr);
    }
    const bool contentShows = rect.x <= maxX && rect.y <= maxY && rect.x + rect.width >= minX &&
                              rect.y + rect.height >= minY;
    if (contentShows) {
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
    }
    const Color edge = edgeColor(paper->getColor());
    const double er = edge.red / 255.0;
    const double eg = edge.green / 255.0;
    const double eb = edge.blue / 255.0;
    if (finish != Finish::Flat && !inside) {
        // The paper's edge, over what is written on it: a darker shade of its color, as a paper note's edge looks
        paperPath(cr, *paper);
        cairo_set_line_width(cr, EDGE_WIDTH);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        cairo_set_source_rgb(cr, er, eg, eb);
        cairo_stroke(cr);
    }
    if (screen && cover) {
        // A folded corner: this note covers (the pen does not write on it, a tap lets it peek)
        const double side = std::min({16.0, rect.width / 4, rect.height / 4});
        const double x1 = rect.x + rect.width;
        cairo_move_to(cr, x1 - side, rect.y);
        cairo_line_to(cr, x1, rect.y + side);
        cairo_line_to(cr, x1 - side, rect.y + side);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, c.red / 255.0 * 0.8, c.green / 255.0 * 0.8, c.blue / 255.0 * 0.8);
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
        cairo_set_source_rgb(cr, er * 0.8, eg * 0.8, eb * 0.8);
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

// --- to another page ------------------------------------------------------------------------------------------

NotePageUndoAction::NotePageUndoAction(LayerController* layers, Layer* layer, Place from, Place to, std::string text):
        UndoAction("StickyNotePageUndoAction"),
        layers(layers),
        layer(layer),
        from(std::move(from)),
        to(std::move(to)),
        text(std::move(text)) {
    this->page = this->to.page;
}

void NotePageUndoAction::move(LayerController* layers, Document& doc, Layer* layer, const Place& from,
                              const Place& to) {
    {
        NoteLayerChange change(*layer);  // (only the note's part of the page is drawn again)
        layers->removeLayer(from.page, layer);  // (locks the document)
    }
    {
        std::unique_lock lock(doc);
        applyLook(*layer, from.look, to.look);
    }
    {
        NoteLayerChange change(*layer);
        layers->insertLayer(to.page, layer, to.position);
    }
    leaveNoteLayer(doc, from.page);
    leaveNoteLayer(doc, to.page);
}

bool NotePageUndoAction::undo(Control* control) {
    move(layers, *control->getDocument(), layer, to, from);
    this->undone = true;
    return true;
}

bool NotePageUndoAction::redo(Control* control) {
    move(layers, *control->getDocument(), layer, from, to);
    this->undone = false;
    return true;
}

// --- the clipboard -------------------------------------------------------------------------------------------------

std::string serialize(const Layer& layer) {
    if (!isNote(layer)) {
        return {};
    }
    ObjectOutputStream out(new BinObjectEncoding());
    out.writeString(PROJECT_STRING);
    out.writeObject(CLIPBOARD_OBJECT);
    out.writeString(layer.getName());
    const auto elements = layer.getElementsView();
    out.writeSizeT(elements.size());
    for (const Element* e: elements) {
        // Each element in a stream of its own (see CLIPBOARD_OBJECT)
        ObjectOutputStream one(new BinObjectEncoding());
        e->serialize(one);
        GString* bytes = one.stealData();
        out.writeImage(std::string_view(bytes->str, bytes->len));
        g_string_free(bytes, TRUE);
    }
    out.endObject();
    GString* data = out.stealData();
    std::string bytes(data->str, data->len);
    g_string_free(data, TRUE);
    return bytes;
}

std::unique_ptr<Layer> deserialize(const char* data, size_t size) {
    try {
        ObjectInputStream in;
        if (!in.read(data, size)) {
            return nullptr;
        }
        in.readString();  // (the version that wrote it: elements are read the same way since upstream 1.0)
        in.readObject(CLIPBOARD_OBJECT);
        auto layer = std::make_unique<Layer>();
        const std::string name = in.readString();
        if (!isNoteName(name)) {
            return nullptr;
        }
        layer->setName(name);
        const size_t count = in.readSizeT();
        for (size_t i = 0; i < count; ++i) {
            const std::string bytes = in.readImage();
            ObjectInputStream one;
            if (!one.read(bytes.data(), bytes.size())) {
                return nullptr;
            }
            const std::string type = one.getNextObjectName();
            ElementPtr element;
            if (type == "Stroke") {
                element = std::make_unique<Stroke>();
            } else if (type == "Image") {
                element = std::make_unique<Image>();
            } else if (type == "TexImage") {
                element = std::make_unique<TexImage>();
            } else if (type == "Text") {
                element = std::make_unique<Text>();
            } else if (type == "Link") {
                element = std::make_unique<Link>();
            } else {
                return nullptr;
            }
            element->readSerialized(one);
            layer->addElement(std::move(element));
        }
        in.endObject();
        if (!isNote(*layer)) {
            return nullptr;
        }
        return layer;
    } catch (const std::exception& e) {
        g_warning("Not a sticky note on the clipboard: %s", e.what());
        return nullptr;
    }
}

Rectangle<double> pastePlace(Rectangle<double> r, double pageWidth, double pageHeight,
                             const std::vector<Rectangle<double>>& taken) {
    r.width = std::clamp(r.width, std::min(MIN_SIDE, pageWidth), std::max(MIN_SIDE, pageWidth));
    r.height = std::clamp(r.height, std::min(MIN_SIDE, pageHeight), std::max(MIN_SIDE, pageHeight));
    const auto inside = [&](double x, double y) {
        return Rectangle<double>(std::clamp(x, 0.0, std::max(0.0, pageWidth - r.width)),
                                 std::clamp(y, 0.0, std::max(0.0, pageHeight - r.height)), r.width, r.height);
    };
    r = inside(r.x, r.y);
    const auto isTaken = [&](const Rectangle<double>& c) {
        return std::any_of(taken.begin(), taken.end(), [&](const Rectangle<double>& t) {
            return std::abs(t.x - c.x) < 0.5 && std::abs(t.y - c.y) < 0.5 && std::abs(t.width - c.width) < 0.5 &&
                   std::abs(t.height - c.height) < 0.5;
        });
    };
    // A little further down and right each time (up and left where the page ends), as a stack of copies
    constexpr double STEP = 16;
    if (!isTaken(r)) {
        return r;
    }
    for (const double dir: {1.0, -1.0}) {
        for (int i = 1; i <= 64; ++i) {
            if (const auto c = inside(r.x + dir * STEP * i, r.y + dir * STEP * i); !isTaken(c)) {
                return c;
            }
        }
    }
    return r;
}

}  // namespace xqt::sticky
