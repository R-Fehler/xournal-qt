#include "MarkdownBoxResize.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>

#include "control/Control.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/CursorSelectionType.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Range.h"
#include "view/overlays/OverlayView.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MdBox.h"
#include "TextFlow.h"

namespace xqt {

namespace {
/// The width of a box before and after a drag of its handle (upstream's wrap width of the text).
class WidthUndoAction final: public UndoAction {
public:
    WidthUndoAction(const PageRef& p, Text* text, double before, double after):
            UndoAction("MarkdownWidthUndoAction"), text(text), before(before), after(after) {
        this->page = p;
    }
    bool undo(Control* control) override {
        set(control, before);
        return true;
    }
    bool redo(Control* control) override {
        set(control, after);
        return true;
    }
    std::string getText() override { return "Markdown box width"; }

private:
    void set(Control* control, double width) {
        Document* doc = control->getDocument();
        doc->lock();
        text->setWrap(width);
        doc->unlock();
        page->firePageChanged();
    }
    Text* text;
    double before;
    double after;
};

/// Draws the selected box while its handle is dragged, over its page.
class DragView final: public xoj::view::OverlayView {
public:
    DragView(CanvasPage* page, const MarkdownBoxResize& resize): OverlayView(page), page(page), resize(resize) {}
    void draw(cairo_t* cr) const override { resize.paint(cr, page); }
    bool isViewOf(const OverlayBase* overlay) const override { return overlay == &resize; }

private:
    const CanvasPage* page;
    const MarkdownBoxResize& resize;
};

constexpr double FRAME_MARGIN = 3.0;  // pt (as the box being written)

/// The box's transformation allows a width: moved and scaled alike, not turned or mirrored.
bool upright(const Text& t) {
    const auto& m = t.getTransformation();
    return m.xy == 0 && m.yx == 0 && m.xx > 0 && m.yy > 0;
}
}  // namespace

MarkdownBoxResize::MarkdownBoxResize(CanvasView& view): view(view) {}

MarkdownBoxResize::~MarkdownBoxResize() = default;

bool MarkdownBoxResize::isPageText(const PageRef& page, const Text& text) {
    const auto& at = text.getTransformation().shift;
    const double left = TextFlow::styleFor(page, TextFlow::Style{}).leftMargin;
    return std::abs(at.x - left) < 0.5 && std::abs(at.y - TextFlow::MARGIN) < 0.5;
}

const Text* MarkdownBoxResize::selectedBox() const {
    EditSelection* sel = view.getSelection();
    if (!sel || sel->isMoving() || std::abs(sel->getRotation()) > 1e-9 || !sel->getSourceLayer() ||
        !md::isMarkdownLayer(*sel->getSourceLayer())) {
        return nullptr;
    }
    const auto elements = sel->getElementsView();
    if (elements.size() != 1 || elements.front()->getType() != ELEMENT_TEXT) {
        return nullptr;
    }
    const auto* text = static_cast<const Text*>(elements.front());
    auto* page = static_cast<CanvasPage*>(sel->getView());
    if (!page || !upright(*text)) {
        return nullptr;
    }
    std::shared_lock lock(*view.getSession().getDocument());
    return isPageText(page->getPage(), *text) ? nullptr : text;
}

QPointF MarkdownBoxResize::onPage(const CanvasPage& page, QPointF viewPos) const {
    const QRectF r = page.viewRect();
    return (viewPos - r.topLeft()) / view.getViewController().zoom();
}

bool MarkdownBoxResize::onHandle(QPointF viewPos, bool touch) const {
    if (drag) {
        return true;
    }
    DocumentSession& session = view.getSession();
    if (session.isReadOnly() || view.isReadingOnly()) {
        return false;
    }
    if (const MarkdownEditor* editor = view.getMarkdownEditor()) {
        const auto h = editor->widthHandle();
        if (!h) {
            return false;
        }
        const double zoom = view.getViewController().zoom();
        const QPointF p = onPage(editor->getPage(), viewPos);
        const double reach = (touch ? 24.0 : HANDLE_RADIUS_PX + 4) / zoom;
        return std::hypot(p.x() - h->x(), p.y() - h->y()) <= reach;
    }
    if (!selectedBox() || session.getToolHandler()->getToolType() == TOOL_HAND) {
        return false;  // (the hand: the selection has no knobs)
    }
    EditSelection* sel = view.getSelection();
    const auto* page = static_cast<const CanvasPage*>(sel->getView());
    const QPointF px = viewPos - page->viewRect().topLeft();
    // The selection's right edge: its own right knob (and the edge around it), which scales other selections
    return sel->getSelectionTypeForPos(px.x(), px.y(), view.getViewController().zoom()) == CURSOR_SELECTION_RIGHT;
}

bool MarkdownBoxResize::press(QPointF viewPos, bool touch) {
    if (drag || !onHandle(viewPos, touch)) {
        return false;
    }
    Document* doc = view.getSession().getDocument();
    if (MarkdownEditor* editor = view.getMarkdownEditor()) {
        Drag d;
        d.page = &editor->getPage();
        d.pageRef = d.page->getPage();
        d.left = editor->boxLeft();
        d.pressX = onPage(*d.page, viewPos).x();
        d.startWidth = d.width = editor->boxWidth();
        drag = d;
        return true;
    }
    // A selected box: out of the selection (back into its layer), drawn here while it is dragged
    EditSelection* sel = view.getSelection();
    Text* box = const_cast<Text*>(selectedBox());
    auto* page = static_cast<CanvasPage*>(sel->getView());
    Drag d;
    d.selected = true;
    d.page = page;
    d.pageRef = page->getPage();
    d.pressX = onPage(*page, viewPos).x();
    view.clearSelection();
    {
        std::unique_lock lock(*doc);
        bool found = false;
        for (const Layer* l: d.pageRef->getLayersView()) {
            found = found || l->indexOf(box) != Element::InvalidIndex;
        }
        if (!found) {
            return true;  // (not where it should be: nothing to drag, the press only ended the selection)
        }
        box->setInEditing(true);
        d.box = box;
        d.scale = box->getTransformation().xx;
        d.left = box->getTransformation().shift.x;
        d.startWidth = d.width = md::styleOf(*box).width;
    }
    drag = d;
    drag->area = dragArea();
    page->addOverlayView(std::make_unique<DragView>(page, *this));
    d.pageRef->firePageChanged();  // (drawn here now)
    return true;
}

QRectF MarkdownBoxResize::dragArea() const {
    if (!drag || !drag->box) {
        return {};
    }
    std::shared_lock lock(*view.getSession().getDocument());
    const auto r = md::boxRect(*drag->box);
    return QRectF(r.x, r.y, r.width, std::max(r.height, drag->box->getFontSize() * 1.3));
}

void MarkdownBoxResize::repaint(const QRectF& area) const {
    if (!drag || area.isNull()) {
        return;
    }
    const double m = FRAME_MARGIN * 2 + handleReach(drag->page->getZoom());
    const QRectF r = area.adjusted(-m, -m, m, m);
    drag->page->flagDirtyRegion(Range(r.left(), r.top(), r.right(), r.bottom()));
}

void MarkdownBoxResize::dragTo(QPointF viewPos) {
    if (!drag) {
        return;
    }
    const double x = onPage(*drag->page, viewPos).x();
    double pageWidth = 0;
    {
        std::shared_lock lock(*view.getSession().getDocument());
        pageWidth = drag->pageRef->getWidth();
    }
    // On the page: at least MIN_WIDTH, at most to its right edge (in page points; the width is the box's own)
    const double most = std::max(MIN_WIDTH, pageWidth - drag->left);
    const double onPageWidth = std::clamp(drag->startWidth * drag->scale + x - drag->pressX, MIN_WIDTH, most);
    const double width = onPageWidth / drag->scale;
    if (std::abs(width - drag->width) < 1e-6) {
        return;
    }
    drag->width = width;
    if (!drag->selected) {
        if (MarkdownEditor* editor = view.getMarkdownEditor()) {
            editor->setBoxWidth(width);
        }
        return;
    }
    {
        std::unique_lock lock(*view.getSession().getDocument());
        drag->box->setWrap(width);
    }
    const QRectF now = dragArea();
    repaint(drag->area.united(now));
    drag->area = now;
}

void MarkdownBoxResize::endDrag() {
    if (!drag) {
        return;
    }
    Drag d = *drag;
    drag.reset();
    DocumentSession& session = view.getSession();
    if (!d.selected) {
        if (MarkdownEditor* editor = view.getMarkdownEditor()) {
            editor->recordWidthChange(d.startWidth);
        }
        return;
    }
    if (!d.box) {
        return;
    }
    d.page->removeOverlayViewsOf(this);
    {
        std::unique_lock lock(*session.getDocument());
        d.box->setInEditing(false);
    }
    if (std::abs(d.width - d.startWidth) > 1e-6) {
        session.getUndoRedoHandler()->addUndoAction(
                std::make_unique<WidthUndoAction>(d.pageRef, d.box, d.startWidth, d.width));
    }
    d.pageRef->firePageChanged();  // (drawn by the renderer again)
    if (const auto index = view.indexOf(d.page)) {
        session.firePageChanged(*index);  // thumbnails
    }
    selectAgain(d);
}

void MarkdownBoxResize::selectAgain(const Drag& d) {
    DocumentSession& session = view.getSession();
    Layer* layer = nullptr;
    Element::Index index = Element::InvalidIndex;
    {
        std::shared_lock lock(*session.getDocument());
        layer = md::markdownLayer(d.pageRef);
        index = layer ? layer->indexOf(d.box) : Element::InvalidIndex;
    }
    if (index == Element::InvalidIndex) {
        return;
    }
    const auto before = view.selectMarkdownLayer(d.pageRef);
    {
        std::shared_lock lock(*session.getDocument());
        if (d.pageRef->getSelectedLayer() != layer) {
            return;  // (a hidden Markdown layer: no selection)
        }
    }
    auto sel = SelectionFactory::createFromElementOnActiveLayer(&session, d.pageRef, d.page, d.box, index);
    view.setSelection(sel.release());
    if (before) {
        view.markdownSelectionMade(d.pageRef, *before);
    }
}

void MarkdownBoxResize::pagesGoing() {
    if (!drag) {
        return;
    }
    if (drag->selected && drag->box) {
        std::unique_lock lock(*view.getSession().getDocument());
        drag->box->setInEditing(false);
    }
    if (drag->selected && drag->box && std::abs(drag->width - drag->startWidth) > 1e-6) {
        view.getSession().getUndoRedoHandler()->addUndoAction(
                std::make_unique<WidthUndoAction>(drag->pageRef, drag->box, drag->startWidth, drag->width));
    }
    drag.reset();  // (the overlay goes with its page)
}

// --- drawing -----------------------------------------------------------------------------------------------------

void MarkdownBoxResize::drawHandle(cairo_t* cr, double x, double y, double px, Color color) {
    // As the selection's knobs (EditSelection::drawAnchorRect), with a double arrow: this one sets the width
    const double r = HANDLE_RADIUS_PX * px;
    cairo_save(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r + px, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);  // a soft edge, also on white pages
    cairo_set_line_width(cr, 2 * px);
    cairo_stroke(cr);
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
    Util::cairo_set_source_rgbi(cr, color);
    cairo_set_line_width(cr, 2.5 * px);
    cairo_stroke(cr);
    // ⟷
    const double a = r * 0.55;
    const double head = r * 0.3;
    cairo_set_line_width(cr, 1.8 * px);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_move_to(cr, x - a, y);
    cairo_line_to(cr, x + a, y);
    cairo_move_to(cr, x - a + head, y - head);
    cairo_line_to(cr, x - a, y);
    cairo_line_to(cr, x - a + head, y + head);
    cairo_move_to(cr, x + a - head, y - head);
    cairo_line_to(cr, x + a, y);
    cairo_line_to(cr, x + a - head, y + head);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void MarkdownBoxResize::paintOverSelection(cairo_t* cr, double zoom) const {
    EditSelection* sel = view.getSelection();
    if (!sel || !selectedBox() || view.getSession().getToolHandler()->getToolType() == TOOL_HAND) {
        return;
    }
    const double x = sel->getXOnView() + sel->getWidth();
    const double y = sel->getYOnView() + sel->getHeight() / 2;
    drawHandle(cr, x * zoom, y * zoom, 1, view.getSession().getSettings()->getSelectionColor());
}

void MarkdownBoxResize::paint(cairo_t* cr, const CanvasPage* on) const {
    if (!drag || !drag->box || drag->page != on) {
        return;
    }
    const double px = 1.0 / on->getZoom();
    const Color color = view.getSession().getSettings()->getSelectionColor();
    const QRectF r = drag->area;
    cairo_save(cr);
    md::drawText(*drag->box, cr);
    // Its frame, as the box being written has
    Util::cairo_set_source_rgbi(cr, color, 0.6);
    cairo_set_line_width(cr, 0.8);
    const double dash[] = {3.0, 2.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_rectangle(cr, r.x() - FRAME_MARGIN, r.y() - FRAME_MARGIN, r.width() + 2 * FRAME_MARGIN,
                    r.height() + 2 * FRAME_MARGIN);
    cairo_stroke(cr);
    cairo_restore(cr);
    drawHandle(cr, r.right() + FRAME_MARGIN + HANDLE_RADIUS_PX * px, r.center().y(), px, color);
}

}  // namespace xqt
