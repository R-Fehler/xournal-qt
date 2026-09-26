#include "StickyNotes.h"

#include <algorithm>
#include <cmath>
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
#include "model/XojPage.h"
#include "undo/InsertLayerUndoAction.h"
#include "undo/RemoveLayerUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Range.h"
#include "view/View.h"
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

/// Upstream's layer undo actions under the note's own name in the undo list
/// Upstream's undo of a placed or deleted layer, named after what was done, drawing only the note's part of the page
/// again (sticky::NoteLayerChange)
class InsertNoteUndoAction final: public InsertLayerUndoAction {
public:
    InsertNoteUndoAction(LayerController* layers, const PageRef& page, Layer* layer, Layer::Index position,
                         std::string text):
            InsertLayerUndoAction(layers, page, layer, position), note(layer), text(std::move(text)) {}
    std::string getText() override { return text; }
    bool undo(Control* control) override {
        sticky::NoteLayerChange change(*note);
        return InsertLayerUndoAction::undo(control);
    }
    bool redo(Control* control) override {
        sticky::NoteLayerChange change(*note);
        return InsertLayerUndoAction::redo(control);
    }

private:
    Layer* note;
    std::string text;
};
class RemoveNoteUndoAction final: public RemoveLayerUndoAction {
public:
    RemoveNoteUndoAction(LayerController* layers, const PageRef& page, Layer* layer, Layer::Index position,
                         std::string text):
            RemoveLayerUndoAction(layers, page, layer, position), note(layer), text(std::move(text)) {}
    std::string getText() override { return text.empty() ? RemoveLayerUndoAction::getText() : text; }
    bool undo(Control* control) override {
        sticky::NoteLayerChange change(*note);
        return RemoveLayerUndoAction::undo(control);
    }
    bool redo(Control* control) override {
        sticky::NoteLayerChange change(*note);
        return RemoveLayerUndoAction::redo(control);
    }

private:
    Layer* note;
    std::string text;
};

/// A picture of a note for other apps (twice the page's resolution; the caller holds the document's lock if the note
/// is on a page)
QImage pictureOf(const Layer& layer) {
    const auto look = sticky::lookOf(layer);
    constexpr double SCALE = 2;
    if (!look) {
        return {};
    }
    const int w = static_cast<int>(std::ceil(look->rect.width * SCALE));
    const int h = static_cast<int>(std::ceil(look->rect.height * SCALE));
    if (w <= 0 || h <= 0 || static_cast<double>(w) * h > 4096.0 * 4096.0) {
        return {};
    }
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);  // (cairo's ARGB32)
    image.fill(Qt::transparent);
    cairo_surface_t* surface =
            cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, w, h, image.bytesPerLine());
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, SCALE, SCALE);
    cairo_translate(cr, -look->rect.x, -look->rect.y);
    sticky::draw(layer, xoj::view::Context::createDefault(cr));  // (as exported: no folded corner, no peeking)
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
    return image;
}

/// A copied note on the clipboard: the note (sticky::CLIPBOARD_MIME), and a picture of it for other apps that is drawn
/// only when one asks for it (drawing it at every copy took most of a copy's time: 35 ms for a note with 300 strokes)
class NoteMimeData final: public QMimeData {
public:
    explicit NoteMimeData(const std::string& note) {
        setData(sticky::CLIPBOARD_MIME, QByteArray(note.data(), static_cast<qsizetype>(note.size())));
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
            const QByteArray bytes = data(sticky::CLIPBOARD_MIME);
            if (auto note = sticky::deserialize(bytes.constData(), static_cast<size_t>(bytes.size()))) {
                picture = pictureOf(*note);  // (a copy of the note, on no page: no lock)
            }
        }
        return picture.isNull() ? QVariant() : QVariant(picture);
    }

private:
    inline static const QString IMAGE = QStringLiteral("application/x-qt-image");  // (QMimeData::imageData's)
    mutable bool drawn = false;
    mutable QImage picture;
};

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
    const size_t pNr = view.currentPageNo();  // (a second view of the document: its own page)
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
    place(*page, sticky::makeNote(look), "Insert sticky note");
    return true;
}

void StickyNotes::place(CanvasPage& page, Layer* layer, const char* what) {
    DocumentSession& session = view.getSession();
    PageRef ref = page.getPage();
    LayerController* layers = session.getLayerController();
    Layer::Index position = 0;
    {
        std::shared_lock lock(*session.getDocument());
        position = ref->getLayerCount();  // (on top of the page's layers)
    }
    {
        sticky::NoteLayerChange change(*layer);  // (only the note's part of the page is drawn again)
        layers->insertLayer(ref, layer, position);  // (locks the document; the view keeps the page's own layer selected)
    }
    session.getUndoRedoHandler()->addUndoAction(
            std::make_unique<InsertNoteUndoAction>(layers, ref, layer, position, tr(what)));
    sticky::leaveNoteLayer(*session.getDocument(), ref);
    select(page, layer);
    Q_EMIT view.notesChanged();
}

// --- the clipboard -----------------------------------------------------------------------------------------------

bool StickyNotes::copySelected() {
    if (!selected) {
        return false;
    }
    std::string bytes;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        bytes = sticky::serialize(*selected);
    }
    if (bytes.empty()) {
        return false;
    }
    // (pasted into another app: a picture of the note, drawn when that app asks for it)
    QGuiApplication::clipboard()->setMimeData(new NoteMimeData(bytes));
    return true;
}

bool StickyNotes::cutSelected() {
    if (view.getSession().isReadOnly() || view.isReadingOnly() || !copySelected()) {
        return false;
    }
    deleteSelected("Cut sticky note");
    return true;
}

bool StickyNotes::clipboardHasNote() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(sticky::CLIPBOARD_MIME);
}

bool StickyNotes::paste(size_t pNr) {
    DocumentSession& session = view.getSession();
    if (pNr >= view.pageCount() || session.isReadOnly() || view.isReadingOnly() || !clipboardHasNote()) {
        return false;
    }
    const QByteArray bytes = QGuiApplication::clipboard()->mimeData()->data(sticky::CLIPBOARD_MIME);
    std::unique_ptr<Layer> layer = sticky::deserialize(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!layer) {
        return false;
    }
    view.endTextEditing();
    view.clearSelection();
    CanvasPage* page = view.getPage(pNr);
    PageRef ref = page->getPage();
    // Where it was when it fits on this page, not exactly on another note (a copy on the page of its original)
    std::vector<Rectangle<double>> taken;
    double width = 0;
    double height = 0;
    {
        std::shared_lock lock(*session.getDocument());
        for (const Layer* l: ref->getLayersView()) {
            if (const auto look = sticky::lookOf(*l)) {
                taken.push_back(look->rect);
            }
        }
        width = ref->getWidth();
        height = ref->getHeight();
    }
    const sticky::Look copied = *sticky::lookOf(*layer);
    sticky::Look look = copied;
    look.rect = sticky::pastePlace(copied.rect, width, height, taken);
    sticky::applyLook(*layer, copied, look);  // (not on a page yet)
    lastColor = look.color;
    place(*page, layer.release(), "Paste sticky note");
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

void StickyNotes::deleteSelected(const char* what) {
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
    {
        sticky::NoteLayerChange change(*layer);  // (only the note's part of the page is drawn again)
        layers->removeLayer(ref, layer);  // (locks the document)
    }
    session.getUndoRedoHandler()->addUndoAction(
            std::make_unique<RemoveNoteUndoAction>(layers, ref, layer, id - 1, what ? tr(what) : std::string()));
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
    dragFrom = dragPointer = QPointF(x, y);
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
    dragPointer = QPointF(x, y);
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
    if (was == Drag::None || !selected) {
        return;
    }
    if (was == Drag::Move && dropOnOtherPage()) {
        return;
    }
    if (dragNow == dragStart) {
        return;
    }
    view.getSession().getUndoRedoHandler()->addUndoAction(std::make_unique<sticky::NoteUndoAction>(
            selectedPageRef, selected, dragStart, dragNow,
            tr(was == Drag::Move ? "Move sticky note" : "Resize sticky note")));
    Q_EMIT view.noteSelectionChanged();
}

bool StickyNotes::dropOnOtherPage() {
    // While it is dragged the note stays on its page (at its edge); released over another page it goes there, the
    // point it was held by under the pointer
    const auto fromIdx = selectedOn ? view.indexOf(selectedOn) : std::nullopt;
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
    const PageRef fromRef = selectedPageRef;
    Layer* layer = selected;
    const QPointF onTarget = (content - layout.pageRect(*toIdx, zoom).topLeft()) / zoom;
    sticky::Look look = dragStart;
    look.rect.x = onTarget.x() - (dragFrom.x() - dragStart.rect.x);
    look.rect.y = onTarget.y() - (dragFrom.y() - dragStart.rect.y);
    Layer::Index fromPos = 0;
    Layer::Index toPos = 0;
    {
        std::shared_lock lock(*session.getDocument());
        look.rect = sticky::pastePlace(look.rect, toRef->getWidth(), toRef->getHeight(), {});
        const Layer::Index id = sticky::layerIdOf(*fromRef, layer);
        if (id == 0) {
            return false;
        }
        fromPos = id - 1;
        toPos = toRef->getLayerCount();  // (on top of the other page's layers)
    }
    if (peeking.erase(layer)) {
        sticky::setPeeking(layer, false);
    }
    clearSelection();
    LayerController* layers = session.getLayerController();
    // The move on its page and the way over are one step: undone, it is back where the drag started
    sticky::NotePageUndoAction::move(layers, *session.getDocument(), layer, {fromRef, fromPos, dragNow},
                                     {toRef, toPos, look});
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<sticky::NotePageUndoAction>(
            layers, layer, sticky::NotePageUndoAction::Place{fromRef, fromPos, dragStart},
            sticky::NotePageUndoAction::Place{toRef, toPos, look}, tr("Move sticky note to another page")));
    select(*target, layer);
    Q_EMIT view.notesChanged();
    return true;
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
    const double pad = 2 / page.getZoom() + sticky::DRAWN_MARGIN;  // (its shadow too)
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
