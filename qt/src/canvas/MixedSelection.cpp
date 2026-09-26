#include "MixedSelection.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <shared_mutex>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <cairo.h>

#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/AddUndoAction.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertLayerUndoAction.h"
#include "undo/MoveUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Range.h"
#include "view/LayerView.h"
#include "view/View.h"
#include "view/overlays/OverlayView.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "MdBox.h"
#include "StickyNotes.h"
#include "session/DocumentSession.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
std::string tr(const char* text) { return QCoreApplication::translate("StickyNotes", text).toStdString(); }

sticky::Look shifted(sticky::Look look, double dx, double dy) {
    look.rect.x += dx;
    look.rect.y += dy;
    return look;
}

Rectangle<double> padded(Rectangle<double> r, double pad) {
    return {r.x - pad, r.y - pad, r.width + 2 * pad, r.height + 2 * pad};
}

/// A picture of a copied selection for other apps (twice the page's resolution; as exported: the page's elements
/// below the notes, no folded corners)
QImage pictureOf(sticky::Group& group) {
    constexpr double SCALE = 2;
    const Rectangle<double>& b = group.bounds;
    const int w = static_cast<int>(std::ceil(b.width * SCALE));
    const int h = static_cast<int>(std::ceil(b.height * SCALE));
    if (w <= 0 || h <= 0 || static_cast<double>(w) * h > 4096.0 * 4096.0) {
        return {};
    }
    // The elements in a layer of their own (Markdown boxes in a "Markdown" one: drawn formatted), on no page
    Layer plain;
    Layer markdown;
    markdown.setName(std::string(xoj::markdown::LAYER_NAME));
    for (size_t i = 0; i < group.elements.size(); ++i) {
        (group.markdown[i] ? markdown : plain).addElement(std::move(group.elements[i]));
    }
    group.elements.clear();
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);  // (cairo's ARGB32)
    image.fill(Qt::transparent);
    cairo_surface_t* surface =
            cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, w, h, image.bytesPerLine());
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, SCALE, SCALE);
    cairo_translate(cr, -b.x, -b.y);
    const auto ctx = xoj::view::Context::createDefault(cr);
    xoj::view::LayerView(&markdown).draw(ctx);
    xoj::view::LayerView(&plain).draw(ctx);
    for (const auto& note: group.notes) {
        sticky::draw(*note, ctx);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
    return image;
}

/// A copied selection on the clipboard, with its picture for other apps drawn only when one asks for it (as a copied
/// note's, StickyNotes)
class GroupMimeData final: public QMimeData {
public:
    explicit GroupMimeData(const std::string& bytes) {
        setData(sticky::GROUP_CLIPBOARD_MIME, QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
    }
    QStringList formats() const override { return QMimeData::formats() << IMAGE; }
    bool hasFormat(const QString& mime) const override { return mime == IMAGE || QMimeData::hasFormat(mime); }

protected:
    QVariant retrieveData(const QString& mime, QMetaType type) const override {
        if (mime != IMAGE) {
            return QMimeData::retrieveData(mime, type);
        }
        if (!drawn) {
            drawn = true;
            const QByteArray bytes = data(sticky::GROUP_CLIPBOARD_MIME);
            if (auto group = sticky::deserializeGroup(bytes.constData(), static_cast<size_t>(bytes.size()))) {
                picture = pictureOf(*group);  // (copies, on no page: no lock)
            }
        }
        return picture.isNull() ? QVariant() : QVariant(picture);
    }

private:
    inline static const QString IMAGE = QStringLiteral("application/x-qt-image");
    mutable bool drawn = false;
    mutable QImage picture;
};

/// The selection over its page: an outline around each note, a thin box around each element, a dashed box around
/// everything
class MixedSelectionView final: public xoj::view::OverlayView {
public:
    MixedSelectionView(CanvasPage* page, const MixedSelection& sel, Document& doc, Color color):
            OverlayView(page), page(page), sel(sel), doc(doc), color(color) {}
    void draw(cairo_t* cr) const override {
        if (sel.selectedPage() != page) {
            return;
        }
        std::shared_lock lock(doc);
        const double px = 1.0 / page->getZoom();
        cairo_save(cr);
        cairo_set_source_rgb(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0);
        std::optional<Rectangle<double>> all;
        const auto add = [&](const Rectangle<double>& r) {
            if (all) {
                all->unite(r);
            } else {
                all = r;
            }
        };
        cairo_set_line_width(cr, 2 * px);
        for (const Layer* note: sel.notes()) {
            if (const auto look = sticky::lookOf(*note)) {
                const Rectangle<double>& r = look->rect;
                cairo_rectangle(cr, r.x - px, r.y - px, r.width + 2 * px, r.height + 2 * px);
                add(r);
            }
        }
        cairo_stroke(cr);
        cairo_set_line_width(cr, px);
        const bool boxes = sel.items().size() <= 200;  // (many strokes: only the box around everything)
        for (const auto& item: sel.items()) {
            const auto r = item.element->getBoundingBox();
            if (boxes) {
                cairo_rectangle(cr, r.x, r.y, r.width, r.height);
            }
            add(r);
        }
        cairo_set_source_rgba(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0, 0.45);
        cairo_stroke(cr);
        if (all) {
            const double pad = 4 * px;
            const double dash[] = {6 * px, 4 * px};
            cairo_set_dash(cr, dash, 2, 0);
            cairo_set_line_width(cr, 1.5 * px);
            cairo_set_source_rgb(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0);
            cairo_rectangle(cr, all->x - pad, all->y - pad, all->width + 2 * pad, all->height + 2 * pad);
            cairo_stroke(cr);
        }
        cairo_restore(cr);
    }
    bool isViewOf(const OverlayBase* overlay) const override { return overlay == &sel; }

private:
    CanvasPage* page;
    const MixedSelection& sel;
    Document& doc;
    Color color;
};

/// How far around the selection is drawn (its outline and box; notes: their shadow)
double drawnPad(const CanvasPage& page) { return 8 / page.getZoom() + sticky::DRAWN_MARGIN; }
}  // namespace

MixedSelection::MixedSelection(CanvasView& view): view(view) {}

MixedSelection::~MixedSelection() = default;

bool MixedSelection::holds(const Layer* note) const {
    return std::find(noteLayers.begin(), noteLayers.end(), note) != noteLayers.end();
}

bool MixedSelection::holds(const Element* element) const {
    return std::any_of(elements.begin(), elements.end(), [&](const Item& i) { return i.element == element; });
}

void MixedSelection::set(CanvasPage& onPage, std::vector<Layer*> notes, std::vector<Item> items) {
    clear();
    page = &onPage;
    ref = onPage.getPage();
    // The notes as they lie on the page (bottom first): so they keep their order wherever they go
    {
        std::shared_lock lock(*view.getSession().getDocument());
        std::sort(notes.begin(), notes.end(), [&](const Layer* a, const Layer* b) {
            return sticky::layerIdOf(*ref, a) < sticky::layerIdOf(*ref, b);
        });
    }
    noteLayers = std::move(notes);
    elements = std::move(items);
    onPage.addOverlayView(std::make_unique<MixedSelectionView>(&onPage, *this, *view.getSession().getDocument(),
                                                               view.getSession().getSettings()->getSelectionColor()));
}

void MixedSelection::clear() {
    if (!page) {
        return;
    }
    if (const auto b = bounds()) {
        repaint(*b);
    }
    page->removeOverlayViewsOf(this);
    page = nullptr;
    ref.reset();
    noteLayers.clear();
    elements.clear();
    isDragging = false;
}

std::optional<Rectangle<double>> MixedSelection::boundsLocked() const {
    std::optional<Rectangle<double>> all;
    const auto add = [&](const Rectangle<double>& r) {
        if (all) {
            all->unite(r);
        } else {
            all = r;
        }
    };
    for (const Layer* note: noteLayers) {
        if (const auto look = sticky::lookOf(*note)) {
            add(look->rect);
        }
    }
    for (const Item& item: elements) {
        add(item.element->getBoundingBox());
    }
    return all;
}

std::optional<Rectangle<double>> MixedSelection::bounds() const {
    if (!page) {
        return std::nullopt;
    }
    std::shared_lock lock(*view.getSession().getDocument());
    return boundsLocked();
}

QRectF MixedSelection::viewBox() const {
    const auto b = bounds();
    const auto idx = page ? view.indexOf(page) : std::nullopt;
    if (!b || !idx) {
        return {};
    }
    const double zoom = view.getViewController().zoom();
    const QRectF pageRect = view.pageViewRect(*idx);
    return QRectF(pageRect.topLeft() + QPointF(b->x, b->y) * zoom, QSizeF(b->width, b->height) * zoom);
}

bool MixedSelection::contains(const CanvasPage& onPage, double x, double y) const {
    if (&onPage != page) {
        return false;
    }
    const auto b = bounds();
    const double pad = 4 / onPage.getZoom();  // (its dashed box)
    return b && x >= b->x - pad && y >= b->y - pad && x <= b->x + b->width + pad && y <= b->y + b->height + pad;
}

void MixedSelection::repaint(const Rectangle<double>& area) const {
    if (!page) {
        return;
    }
    const auto r = padded(area, drawnPad(*page));
    page->repaintArea(r.x, r.y, r.x + r.width, r.y + r.height);
}

// --- moving ------------------------------------------------------------------------------------------------------

void MixedSelection::startDrag(double x, double y) {
    validate();
    const auto b = bounds();
    if (!b) {
        return;
    }
    isDragging = true;
    dragFrom = dragPointer = QPointF(x, y);
    applied = QPointF();
    dragBounds = *b;
    dragLooks.clear();
    std::shared_lock lock(*view.getSession().getDocument());
    for (const Layer* note: noteLayers) {
        dragLooks.push_back(sticky::lookOf(*note).value_or(sticky::Look{}));
    }
}

void MixedSelection::dragTo(double x, double y) {
    if (!isDragging || !page) {
        return;
    }
    dragPointer = QPointF(x, y);
    // Everything stays on its page while it is dragged (let go over another page, it goes there: endDrag)
    const double pageWidth = ref->getWidth();
    const double pageHeight = ref->getHeight();
    const double loX = -dragBounds.x;
    const double loY = -dragBounds.y;
    const double hiX = std::max(loX, pageWidth - dragBounds.x - dragBounds.width);
    const double hiY = std::max(loY, pageHeight - dragBounds.y - dragBounds.height);
    const QPointF to(std::clamp(x - dragFrom.x(), loX, hiX), std::clamp(y - dragFrom.y(), loY, hiY));
    const QPointF delta = to - applied;
    if (delta.isNull()) {
        return;
    }
    Document* doc = view.getSession().getDocument();
    {
        std::unique_lock lock(*doc);
        for (size_t i = 0; i < noteLayers.size(); ++i) {
            sticky::applyLook(*noteLayers[i], shifted(dragLooks[i], applied.x(), applied.y()),
                              shifted(dragLooks[i], to.x(), to.y()));
        }
        for (const Item& item: elements) {
            item.element->move(delta.x(), delta.y());
        }
    }
    // Drawn again where it was and where it is
    Rectangle<double> area(dragBounds.x + applied.x(), dragBounds.y + applied.y(), dragBounds.width,
                           dragBounds.height);
    area.unite(Rectangle<double>(dragBounds.x + to.x(), dragBounds.y + to.y(), dragBounds.width, dragBounds.height));
    applied = to;
    Range range(padded(area, sticky::DRAWN_MARGIN + 1));
    ref->fireRangeChanged(range);
    repaint(area);
}

void MixedSelection::endDrag() {
    if (!std::exchange(isDragging, false) || !page) {
        return;
    }
    if (dropOnOtherPage() || applied.isNull()) {
        return;
    }
    // One step: the notes' looks and the elements' move
    DocumentSession& session = view.getSession();
    auto steps = std::make_unique<sticky::UndoSteps>(tr("Move selection"));
    for (size_t i = 0; i < noteLayers.size(); ++i) {
        steps->add(std::make_unique<sticky::NoteUndoAction>(ref, noteLayers[i], dragLooks[i],
                                                           shifted(dragLooks[i], applied.x(), applied.y()), ""));
    }
    std::map<Layer*, std::vector<Element*>> byLayer;
    for (const Item& item: elements) {
        byLayer[item.layer].push_back(item.element);
    }
    for (auto& [layer, moved]: byLayer) {
        steps->add(std::make_unique<MoveUndoAction>(layer, ref, std::move(moved), applied.x(), applied.y(), layer,
                                                    ref));
    }
    session.getUndoRedoHandler()->addUndoAction(std::move(steps));
    Q_EMIT view.selectionChanged(true);
}

bool MixedSelection::dropOnOtherPage() {
    const auto fromIdx = view.indexOf(page);
    DocumentSession& session = view.getSession();
    if (!fromIdx || session.isReadOnly() || view.isReadingOnly()) {
        return false;
    }
    const double zoom = view.getViewController().zoom();
    const DocumentLayout& layout = view.documentLayout();
    const QPointF content = layout.pageRect(*fromIdx, zoom).topLeft() + dragPointer * zoom;
    const auto toIdx = layout.pageAt(content, zoom);
    if (!toIdx || *toIdx == *fromIdx || *toIdx >= view.pageCount()) {
        return false;
    }
    CanvasPage* target = view.getPage(*toIdx);
    const PageRef toRef = target->getPage();
    const PageRef fromRef = ref;
    Document* doc = session.getDocument();
    LayerController* layers = session.getLayerController();
    // The point it was held by goes under the pointer (everything inside that page, its layout kept)
    const QPointF onTarget = (content - layout.pageRect(*toIdx, zoom).topLeft()) / zoom;
    Rectangle<double> placed(onTarget.x() - (dragFrom.x() - dragBounds.x),
                             onTarget.y() - (dragFrom.y() - dragBounds.y), dragBounds.width, dragBounds.height);
    {
        std::shared_lock lock(*doc);
        const auto shift = sticky::groupPastePlace(placed, {}, toRef->getWidth(), toRef->getHeight(), {});
        placed.x += shift.x;
        placed.y += shift.y;
    }
    const double dx = placed.x - dragBounds.x;  // (from where the drag began)
    const double dy = placed.y - dragBounds.y;
    const std::vector<Layer*> notes = noteLayers;
    const std::vector<Item> items = elements;
    const std::vector<sticky::Look> looks = dragLooks;
    const QPointF already = applied;
    clear();
    auto steps = std::make_unique<sticky::UndoSteps>(tr("Move selection to another page"));
    // The notes, bottom first: each on top of the other page's layers (so they keep their order)
    for (size_t i = 0; i < notes.size(); ++i) {
        Layer::Index fromPos = 0;
        Layer::Index toPos = 0;
        {
            std::shared_lock lock(*doc);
            const Layer::Index id = sticky::layerIdOf(*fromRef, notes[i]);
            if (id == 0) {
                continue;
            }
            fromPos = id - 1;
            toPos = toRef->getLayerCount();
        }
        const sticky::Look to = shifted(looks[i], dx, dy);
        sticky::NotePageUndoAction::move(layers, *doc, notes[i],
                                         {fromRef, fromPos, shifted(looks[i], already.x(), already.y())},
                                         {toRef, toPos, to});
        steps->add(std::make_unique<sticky::NotePageUndoAction>(
                layers, notes[i], sticky::NotePageUndoAction::Place{fromRef, fromPos, looks[i]},
                sticky::NotePageUndoAction::Place{toRef, toPos, to}, ""));
    }
    // The elements: into that page's own layer (Markdown boxes into its Markdown layer)
    sticky::leaveNoteLayer(*doc, toRef);
    Layer* own = nullptr;
    {
        std::shared_lock lock(*doc);
        own = toRef->getSelectedLayer();
    }
    std::map<Layer*, std::vector<Element*>> byLayer;
    bool markdown = false;
    for (const Item& item: items) {
        byLayer[item.layer].push_back(item.element);
        markdown = markdown || md::isMarkdownLayer(*item.layer);
    }
    Layer* mdLayer = markdown ? markdownLayerFor(toRef, *steps) : nullptr;
    std::vector<Item> movedItems;
    for (auto& [layer, moved]: byLayer) {
        Layer* into = md::isMarkdownLayer(*layer) ? mdLayer : own;
        {
            std::unique_lock lock(*doc);
            for (Element* e: moved) {
                into->addElement(layer->removeElement(e).e);
                e->move(dx - already.x(), dy - already.y());
                movedItems.push_back({into, e});
            }
        }
        steps->add(std::make_unique<MoveUndoAction>(layer, fromRef, std::move(moved), dx, dy, into, toRef));
    }
    fromRef->firePageChanged();
    toRef->firePageChanged();
    session.getUndoRedoHandler()->addUndoAction(std::move(steps));
    view.selectTogether(*target, notes, movedItems);
    Q_EMIT view.notesChanged();
    return true;
}

Layer* MixedSelection::markdownLayerFor(const PageRef& onPage, sticky::UndoSteps& steps) {
    Document* doc = view.getSession().getDocument();
    Layer::Index selected = 0;
    {
        std::shared_lock lock(*doc);
        if (Layer* layer = md::markdownLayer(onPage)) {
            return layer;
        }
        selected = onPage->getSelectedLayerId();
    }
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    LayerController* layers = view.getSession().getLayerController();
    layers->insertLayer(onPage, layer, 0);  // (at the bottom; locks the document)
    steps.add(std::make_unique<InsertLayerUndoAction>(layers, onPage, layer, 0));
    std::unique_lock lock(*doc);
    onPage->setSelectedLayerId(selected > 0 ? selected + 1 : 0);  // (the layer selected before stays selected)
    return layer;
}

// --- the clipboard -------------------------------------------------------------------------------------------------

bool MixedSelection::copy() {
    validate();
    if (!page) {
        return false;
    }
    std::string bytes;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        const auto b = boundsLocked();
        if (!b) {
            return false;
        }
        std::vector<const Layer*> notes(noteLayers.begin(), noteLayers.end());
        std::vector<const Element*> copied;
        // (the page's elements in their order on the page: pasted, they lie over each other as they did)
        std::vector<std::pair<std::pair<Layer::Index, Element::Index>, const Element*>> ordered;
        for (const Item& item: elements) {
            ordered.push_back({{sticky::layerIdOf(*ref, item.layer), item.layer->indexOf(item.element)}, item.element});
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [where, e]: ordered) {
            copied.push_back(e);
        }
        bytes = sticky::serializeGroup(*b, notes, copied);
    }
    QGuiApplication::clipboard()->setMimeData(new GroupMimeData(bytes));
    return true;
}

bool MixedSelection::cut() {
    if (view.getSession().isReadOnly() || view.isReadingOnly() || !copy()) {
        return false;
    }
    deleteAll("Cut");
    return true;
}

bool MixedSelection::clipboardHas() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(sticky::GROUP_CLIPBOARD_MIME);
}

void MixedSelection::deleteAll(const char* what) {
    validate();
    DocumentSession& session = view.getSession();
    if (!page || session.isReadOnly() || view.isReadingOnly()) {
        return;
    }
    const PageRef onPage = ref;
    const std::vector<Layer*> notes = noteLayers;
    const std::vector<Item> items = elements;
    const auto area = bounds();
    clear();
    Document* doc = session.getDocument();
    auto steps = std::make_unique<sticky::UndoSteps>(tr(what ? what : "Delete"));
    if (!items.empty()) {
        // The elements, each at its place in its layer (the highest first: the places recorded stay the original ones)
        auto undo = std::make_unique<DeleteUndoAction>(onPage, false);
        std::unique_lock lock(*doc);
        std::vector<std::pair<Item, Element::Index>> places;
        for (const Item& item: items) {
            places.emplace_back(item, item.layer->indexOf(item.element));
        }
        std::sort(places.begin(), places.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (auto& [item, index]: places) {
            undo->addElement(item.layer, item.layer->removeElement(item.element).e, index);
        }
        steps->add(std::move(undo));
    }
    LayerController* layers = session.getLayerController();
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {  // (the top one first)
        Layer::Index id = 0;
        {
            std::shared_lock lock(*doc);
            id = sticky::layerIdOf(*onPage, *it);
        }
        if (id == 0) {
            continue;
        }
        {
            sticky::NoteLayerChange change(**it);  // (only the note's part of the page is drawn again)
            layers->removeLayer(onPage, *it);      // (locks the document)
        }
        steps->add(StickyNotes::removeUndo(layers, onPage, *it, id - 1, ""));
    }
    sticky::leaveNoteLayer(*doc, onPage);
    if (area) {
        Range range(padded(*area, sticky::DRAWN_MARGIN + 1));
        onPage->fireRangeChanged(range);
    }
    session.getUndoRedoHandler()->addUndoAction(std::move(steps));
    Q_EMIT view.selectionChanged(false);
    Q_EMIT view.notesChanged();
}

bool MixedSelection::paste(size_t pNr) {
    DocumentSession& session = view.getSession();
    if (pNr >= view.pageCount() || session.isReadOnly() || view.isReadingOnly() || !clipboardHas()) {
        return false;
    }
    const QByteArray bytes = QGuiApplication::clipboard()->mimeData()->data(sticky::GROUP_CLIPBOARD_MIME);
    auto group = sticky::deserializeGroup(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!group || (group->notes.empty() && group->elements.empty())) {
        return false;
    }
    view.endTextEditing();
    view.clearSelection();
    CanvasPage* target = view.getPage(pNr);
    const PageRef onPage = target->getPage();
    Document* doc = session.getDocument();
    // Where it was when it fits; not with a note exactly on one of the page (a paste over its original)
    std::vector<Rectangle<double>> taken;
    std::vector<Rectangle<double>> noteRects;
    for (const auto& note: group->notes) {
        noteRects.push_back(sticky::lookOf(*note)->rect);
    }
    xoj::util::Point<double> offset;
    {
        std::shared_lock lock(*doc);
        for (const Layer* l: onPage->getLayersView()) {
            if (const auto look = sticky::lookOf(*l)) {
                taken.push_back(look->rect);
            }
        }
        offset = sticky::groupPastePlace(group->bounds, noteRects, onPage->getWidth(), onPage->getHeight(), taken);
    }
    auto steps = std::make_unique<sticky::UndoSteps>(tr("Paste"));
    // The elements into the page's own layer (Markdown boxes: its Markdown layer)
    sticky::leaveNoteLayer(*doc, onPage);
    Layer* own = nullptr;
    {
        std::shared_lock lock(*doc);
        own = onPage->getSelectedLayer();
    }
    Layer* mdLayer = nullptr;
    if (std::find(group->markdown.begin(), group->markdown.end(), true) != group->markdown.end()) {
        mdLayer = markdownLayerFor(onPage, *steps);
    }
    std::vector<Item> items;
    if (!group->elements.empty()) {
        auto undo = std::make_unique<AddUndoAction>(onPage, false);
        std::unique_lock lock(*doc);
        for (size_t i = 0; i < group->elements.size(); ++i) {
            Layer* into = group->markdown[i] ? mdLayer : own;
            Element* e = group->elements[i].get();
            e->move(offset.x, offset.y);
            into->addElement(std::move(group->elements[i]));
            undo->addElement(into, e, into->indexOf(e));
            items.push_back({into, e});
        }
        steps->add(std::move(undo));
    }
    // The notes on top of the page's layers, in their order
    LayerController* layers = session.getLayerController();
    std::vector<Layer*> notes;
    for (auto& note: group->notes) {
        const sticky::Look look = *sticky::lookOf(*note);
        sticky::applyLook(*note, look, shifted(look, offset.x, offset.y));  // (not on a page yet)
        Layer::Index position = 0;
        {
            std::shared_lock lock(*doc);
            position = onPage->getLayerCount();
        }
        Layer* layer = note.release();
        {
            sticky::NoteLayerChange change(*layer);
            layers->insertLayer(onPage, layer, position);  // (locks the document)
        }
        steps->add(StickyNotes::insertUndo(layers, onPage, layer, position, ""));
        notes.push_back(layer);
    }
    sticky::leaveNoteLayer(*doc, onPage);
    Rectangle<double> area = group->bounds;
    area.x += offset.x;
    area.y += offset.y;
    Range range(padded(area, sticky::DRAWN_MARGIN + 1));
    onPage->fireRangeChanged(range);
    session.getUndoRedoHandler()->addUndoAction(std::move(steps));
    view.selectTogether(*target, notes, items);
    Q_EMIT view.notesChanged();
    return true;
}

// --- changes -----------------------------------------------------------------------------------------------------

void MixedSelection::validate() {
    if (!page || isDragging) {
        return;
    }
    bool changed = false;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        const auto layers = ref->getLayersView();
        const auto onPage = [&](const Layer* l) { return std::find(layers.begin(), layers.end(), l) != layers.end(); };
        const auto goneNote = [&](const Layer* l) { return !onPage(l) || !sticky::isNote(*l); };
        const auto goneItem = [&](const Item& i) {
            return !onPage(i.layer) || i.layer->indexOf(i.element) == Element::InvalidIndex;
        };
        const size_t before = noteLayers.size() + elements.size();
        noteLayers.erase(std::remove_if(noteLayers.begin(), noteLayers.end(), goneNote), noteLayers.end());
        elements.erase(std::remove_if(elements.begin(), elements.end(), goneItem), elements.end());
        changed = noteLayers.size() + elements.size() != before;
    }
    if (!changed) {
        return;
    }
    if (noteLayers.empty() && elements.empty()) {
        view.clearSelection();
        return;
    }
    page->repaintPage();
}

void MixedSelection::pageGoing(const CanvasPage* going) {
    if (going == page) {
        page = nullptr;  // (its overlay goes with the page)
        ref.reset();
        noteLayers.clear();
        elements.clear();
        isDragging = false;
        Q_EMIT view.selectionChanged(false);
    }
}

}  // namespace xqt
