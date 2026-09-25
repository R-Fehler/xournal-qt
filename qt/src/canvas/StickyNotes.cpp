#include "StickyNotes.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include <QCoreApplication>
#include <cairo.h>

#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "undo/InsertLayerUndoAction.h"
#include "undo/RemoveLayerUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Range.h"
#include "view/overlays/OverlayView.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "TextEditor.h"
#include "session/DocumentSession.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
std::string tr(const char* text) { return QCoreApplication::translate("StickyNotes", text).toStdString(); }

bool inside(const Rectangle<double>& r, double x, double y) {
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}

/// The outline of the selected note and its handle (bottom right), over its page
class NoteSelectionView final: public xoj::view::OverlayView {
public:
    NoteSelectionView(CanvasPage* page, const StickyNotes& notes, Color color):
            OverlayView(page), page(page), notes(notes), color(color) {}
    void draw(cairo_t* cr) const override {
        if (notes.selectedPage() != page) {
            return;
        }
        const auto look = notes.selectedLook();
        if (!look) {
            return;
        }
        const double px = 1.0 / page->getZoom();
        const Rectangle<double>& r = look->rect;
        cairo_save(cr);
        cairo_set_source_rgb(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0);
        cairo_set_line_width(cr, 2 * px);
        cairo_rectangle(cr, r.x - px, r.y - px, r.width + 2 * px, r.height + 2 * px);
        cairo_stroke(cr);
        cairo_arc(cr, r.x + r.width, r.y + r.height, StickyNotes::HANDLE_RADIUS_PX * px, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    bool isViewOf(const OverlayBase* overlay) const override { return overlay == &notes; }

private:
    CanvasPage* page;
    const StickyNotes& notes;
    Color color;
};
}  // namespace

StickyNotes::StickyNotes(CanvasView& view): view(view), lastColor(sticky::presetColors().front()) {}

StickyNotes::~StickyNotes() {
    for (const Layer* layer: peeking) {
        sticky::setPeeking(layer, false);
    }
}

std::optional<sticky::Look> StickyNotes::lookOf(const Layer* layer) const {
    if (!layer) {
        return std::nullopt;
    }
    std::shared_lock lock(*view.getSession().getDocument());
    return sticky::lookOf(*layer);
}

std::optional<sticky::Look> StickyNotes::selectedLook() const { return lookOf(selected); }

QRectF StickyNotes::selectedViewBox() const {
    const auto look = selectedLook();
    const auto idx = selectedOn ? view.indexOf(selectedOn) : std::nullopt;
    if (!look || !idx) {
        return {};
    }
    const double zoom = view.getViewController().zoom();
    const QRectF pageRect = view.pageViewRect(*idx);
    return QRectF(pageRect.topLeft() + QPointF(look->rect.x, look->rect.y) * zoom,
                  QSizeF(look->rect.width, look->rect.height) * zoom);
}

// --- placing -----------------------------------------------------------------------------------------------------

bool StickyNotes::insert() {
    DocumentSession& session = view.getSession();
    const size_t pNr = session.getCurrentPageNo();
    if (pNr >= view.pageCount() || session.isReadOnly() || view.isReadingOnly()) {
        return false;
    }
    view.endTextEditing();
    view.clearSelection();
    CanvasPage* page = view.getPage(pNr);
    PageRef ref = page->getPage();
    // In the middle of the visible part of the page (as an image is inserted), inside the page
    const double zoom = view.getViewController().zoom();
    const QRectF pageRect = view.documentLayout().pageRect(pNr, zoom);
    QRectF visible = pageRect.intersected(view.getViewController().visibleContentRect());
    if (visible.isEmpty()) {
        visible = pageRect;
    }
    const QRectF area((visible.topLeft() - pageRect.topLeft()) / zoom, visible.size() / zoom);
    sticky::Look look;
    look.color = lastColor;
    look.rect.width = std::min(sticky::DEFAULT_WIDTH, ref->getWidth() * 0.8);
    look.rect.height = std::min(sticky::DEFAULT_HEIGHT, ref->getHeight() * 0.8);
    look.rect.x = std::clamp(area.center().x() - look.rect.width / 2, 0.0, ref->getWidth() - look.rect.width);
    look.rect.y = std::clamp(area.center().y() - look.rect.height / 2, 0.0, ref->getHeight() - look.rect.height);

    Layer* layer = sticky::makeNote(look);
    LayerController* layers = session.getLayerController();
    Layer::Index position = 0;
    {
        std::shared_lock lock(*session.getDocument());
        position = ref->getLayerCount();  // (on top of the page's layers)
    }
    layers->insertLayer(ref, layer, position);  // (locks the document; the view keeps the page's own layer selected)
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertLayerUndoAction>(layers, ref, layer, position));
    sticky::leaveNoteLayer(*session.getDocument(), ref);
    select(*page, layer);
    Q_EMIT view.notesChanged();
    return true;
}

// --- the selected note -------------------------------------------------------------------------------------------

void StickyNotes::select(CanvasPage& page, Layer* note) {
    if (note == selected && &page == selectedOn) {
        return;
    }
    view.clearSelection();  // (a selection of elements, and a note selected before)
    selected = note;
    selectedOn = &page;
    selectedPageRef = page.getPage();
    page.addOverlayView(std::make_unique<NoteSelectionView>(
            &page, *this, view.getSession().getSettings()->getSelectionColor()));
    if (auto look = selectedLook()) {
        lastColor = look->color;
    }
    Q_EMIT view.noteSelectionChanged();
}

void StickyNotes::clearSelection() {
    if (!selected) {
        return;
    }
    repaintSelection(selectedLook());
    if (selectedOn) {
        selectedOn->removeOverlayViewsOf(this);
    }
    selected = nullptr;
    selectedOn = nullptr;
    selectedPageRef.reset();
    drag = Drag::None;
    Q_EMIT view.noteSelectionChanged();
}

void StickyNotes::repaintSelection(const std::optional<sticky::Look>& look) {
    if (!look || !selectedOn) {
        return;
    }
    const double pad = (HANDLE_RADIUS_PX + 4) / selectedOn->getZoom();
    selectedOn->repaintArea(look->rect.x - pad, look->rect.y - pad, look->rect.x + look->rect.width + pad,
                            look->rect.y + look->rect.height + pad);
}

void StickyNotes::show(const sticky::Look& from, const sticky::Look& to) {
    sticky::changeLook(*view.getSession().getDocument(), selectedPageRef, *selected, from, to);
    repaintSelection(from);
    repaintSelection(to);
}

void StickyNotes::change(const sticky::Look& to, const char* what) {
    const auto from = selectedLook();
    if (!from || *from == to) {
        return;
    }
    show(*from, to);
    view.getSession().getUndoRedoHandler()->addUndoAction(
            std::make_unique<sticky::NoteUndoAction>(selectedPageRef, selected, *from, to, tr(what)));
    Q_EMIT view.noteSelectionChanged();
}

void StickyNotes::setColor(Color color) {
    if (auto look = selectedLook()) {
        lastColor = color;
        look->color = color;
        change(*look, "Sticky note color");
    }
}

void StickyNotes::setCover(bool cover) {
    if (auto look = selectedLook()) {
        look->cover = cover;
        if (!cover && peeking.erase(selected)) {
            sticky::setPeeking(selected, false);
        }
        change(*look, cover ? "Sticky note covers" : "Sticky note to write on");
    }
}

void StickyNotes::deleteSelected() {
    if (!selected) {
        return;
    }
    DocumentSession& session = view.getSession();
    Layer* layer = selected;
    PageRef ref = selectedPageRef;
    clearSelection();
    Layer::Index id = 0;
    {
        std::shared_lock lock(*session.getDocument());
        id = sticky::layerIdOf(*ref, layer);
    }
    if (id == 0) {
        return;
    }
    if (peeking.erase(layer)) {
        sticky::setPeeking(layer, false);
    }
    LayerController* layers = session.getLayerController();
    layers->removeLayer(ref, layer);  // (locks the document)
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<RemoveLayerUndoAction>(layers, ref, layer, id - 1));
    sticky::leaveNoteLayer(*session.getDocument(), ref);
    Q_EMIT view.notesChanged();
}

// --- input -------------------------------------------------------------------------------------------------------

bool StickyNotes::onHandle(const CanvasPage& page, double x, double y, bool touch) const {
    if (&page != selectedOn) {
        return false;
    }
    const auto look = selectedLook();
    if (!look) {
        return false;
    }
    const double reach = (touch ? 24.0 : HANDLE_RADIUS_PX + 4) / page.getZoom();
    return std::hypot(x - (look->rect.x + look->rect.width), y - (look->rect.y + look->rect.height)) <= reach;
}

void StickyNotes::startDrag(Drag how, double x, double y) {
    const auto look = selectedLook();
    if (!look) {
        return;
    }
    drag = how;
    dragFrom = QPointF(x, y);
    dragStart = dragNow = *look;
}

bool StickyNotes::press(CanvasPage& page, double x, double y, bool selectTool, bool& deselected) {
    deselected = false;
    if (selected) {
        if (onHandle(page, x, y)) {
            startDrag(Drag::Resize, x, y);
            return true;
        }
        if (selectTool && &page == selectedOn) {
            if (const auto look = selectedLook(); look && inside(look->rect, x, y)) {
                startDrag(Drag::Move, x, y);
                return true;
            }
        }
        clearSelection();
        deselected = true;
        if (!selectTool) {
            return true;  // a press beside the selection only ends it (as for a selection of elements)
        }
    }
    if (!selectTool) {
        return false;
    }
    Layer* note = nullptr;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        note = sticky::noteAt(*page.getPage(), x, y);
    }
    if (!note) {
        return false;
    }
    select(page, note);
    startDrag(Drag::Move, x, y);  // (a drag moves it right away)
    return true;
}

bool StickyNotes::pressTouch(CanvasPage& page, double x, double y) {
    if (!selected || &page != selectedOn) {
        return false;
    }
    if (onHandle(page, x, y, true)) {
        startDrag(Drag::Resize, x, y);
        return true;
    }
    if (const auto look = selectedLook(); look && inside(look->rect, x, y)) {
        startDrag(Drag::Move, x, y);
        return true;
    }
    return false;
}

void StickyNotes::dragTo(double x, double y) {
    if (drag == Drag::None || !selected) {
        return;
    }
    const double pageWidth = selectedPageRef->getWidth();
    const double pageHeight = selectedPageRef->getHeight();
    sticky::Look to = dragStart;
    const double dx = x - dragFrom.x();
    const double dy = y - dragFrom.y();
    // The note stays on its page
    if (drag == Drag::Move) {
        to.rect.x = std::clamp(dragStart.rect.x + dx, 0.0, std::max(0.0, pageWidth - to.rect.width));
        to.rect.y = std::clamp(dragStart.rect.y + dy, 0.0, std::max(0.0, pageHeight - to.rect.height));
    } else {
        to.rect.width = std::clamp(dragStart.rect.width + dx, sticky::MIN_SIDE,
                                   std::max(sticky::MIN_SIDE, pageWidth - to.rect.x));
        to.rect.height = std::clamp(dragStart.rect.height + dy, sticky::MIN_SIDE,
                                    std::max(sticky::MIN_SIDE, pageHeight - to.rect.y));
    }
    if (to == dragNow) {
        return;
    }
    show(dragNow, to);
    dragNow = to;
    Q_EMIT view.noteSelectionChanged();  // (its pill goes along)
}

void StickyNotes::endDrag() {
    const Drag was = std::exchange(drag, Drag::None);
    if (was == Drag::None || !selected || dragNow == dragStart) {
        return;
    }
    view.getSession().getUndoRedoHandler()->addUndoAction(std::make_unique<sticky::NoteUndoAction>(
            selectedPageRef, selected, dragStart, dragNow,
            tr(was == Drag::Move ? "Move sticky note" : "Resize sticky note")));
    Q_EMIT view.noteSelectionChanged();
}

bool StickyNotes::tapCover(CanvasPage& page, double x, double y) {
    Layer* note = nullptr;
    std::optional<sticky::Look> look;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        note = sticky::noteAt(*page.getPage(), x, y);
        if (note) {
            look = sticky::lookOf(*note);
        }
    }
    if (!note || !look || !look->cover) {
        return false;
    }
    const bool peek = !peeking.count(note);
    if (peek) {
        peeking.insert(note);
    } else {
        peeking.erase(note);
    }
    sticky::setPeeking(note, peek);
    // Only the screen shows it: no page change (thumbnails, the file), the note is drawn again here
    const double pad = 2 / page.getZoom() + sticky::PAPER_WIDTH;
    page.rerenderRect(look->rect.x - pad, look->rect.y - pad, look->rect.width + 2 * pad, look->rect.height + 2 * pad);
    return true;
}

// --- the notes of a page ------------------------------------------------------------------------------------------

bool StickyNotes::pageHasNotes(size_t page) const {
    Document* doc = view.getSession().getDocument();
    std::shared_lock lock(*doc);
    return page < doc->getPageCount() && sticky::hasNotes(*doc->getPage(page));
}

bool StickyNotes::notesHidden(size_t page) const {
    Document* doc = view.getSession().getDocument();
    std::shared_lock lock(*doc);
    if (page >= doc->getPageCount()) {
        return false;
    }
    const PageRef p = doc->getPage(page);
    return sticky::hasNotes(*p) && !sticky::hasNotes(*p, true);
}

void StickyNotes::setNotesHidden(size_t page, bool hidden) {
    DocumentSession& session = view.getSession();
    Document* doc = session.getDocument();
    {
        std::unique_lock lock(*doc);
        if (page >= doc->getPageCount()) {
            return;
        }
        for (Layer* layer: doc->getPage(page)->getLayers()) {
            if (sticky::isNote(*layer)) {
                layer->setVisible(!hidden);
            }
        }
    }
    if (hidden && selectedOn && view.indexOf(selectedOn) == page) {
        clearSelection();
    }
    session.firePageChanged(page);  // (drawn again, also its thumbnail)
    Q_EMIT view.notesChanged();
}

void StickyNotes::layersChanged() {
    Document* doc = view.getSession().getDocument();
    bool gone = false;
    std::vector<const Layer*> unpeek;
    {
        std::shared_lock lock(*doc);
        gone = selected && (!selectedPageRef || sticky::layerIdOf(*selectedPageRef, selected) == 0);
        if (!peeking.empty()) {
            std::unordered_set<const Layer*> present;
            for (size_t i = 0; i < doc->getPageCount(); ++i) {
                for (const Layer* layer: doc->getPage(i)->getLayersView()) {
                    if (peeking.count(layer)) {
                        present.insert(layer);
                    }
                }
            }
            for (const Layer* layer: peeking) {
                if (!present.count(layer)) {
                    unpeek.push_back(layer);
                }
            }
        }
    }
    for (const Layer* layer: unpeek) {
        peeking.erase(layer);
        sticky::setPeeking(layer, false);
    }
    if (gone) {
        clearSelection();
    }
    Q_EMIT view.notesChanged();
}

void StickyNotes::pageGoing(const CanvasPage* page) {
    if (page == selectedOn) {
        selected = nullptr;  // (its page and its overlay go with it)
        selectedOn = nullptr;
        selectedPageRef.reset();
        drag = Drag::None;
        Q_EMIT view.noteSelectionChanged();
    }
}

}  // namespace xqt
