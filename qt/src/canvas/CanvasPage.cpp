#include "CanvasPage.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <utility>

#include <cairo.h>

#include "control/Control.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/layer/LayerController.h"
#include "control/tools/EditSelection.h"
#include "control/tools/EraseHandler.h"
#include "control/tools/Selector.h"
#include "util/safe_casts.h"
#include "model/Layer.h"
#include "view/overlays/SelectorView.h"
#include "control/tools/InputHandler.h"
#include "control/tools/LaserPointerHandler.h"
#include "control/tools/ArrowHandler.h"
#include "control/tools/CoordinateSystemHandler.h"
#include "control/tools/EllipseHandler.h"
#include "control/tools/RectangleHandler.h"
#include "control/tools/RulerHandler.h"
#include "control/tools/StrokeHandler.h"
#include "gui/inputdevices/PositionInputData.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"
#include "util/Color.h"
#include "util/Point.h"
#include "util/Rectangle.h"
#include "view/Mask.h"
#include "view/overlays/OverlayView.h"

#include "CanvasView.h"
#include "MdBox.h"
#include "MixedSelection.h"
#include "PenGestures.h"
#include "Snip.h"
#include "StickyNotes.h"
#include "TextEditor.h"
#include "TimelineReplay.h"
#include "render/RenderService.h"
#include "session/ElementGroups.h"
#include "session/ElementTimes.h"
#include "session/DocumentSession.h"
#include "session/PenFill.h"
#include "session/StickyNote.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
template <typename Views, typename Handler>
void eraseViewsOf(Views& views, const Handler* handler) {
    views.erase(std::remove_if(views.begin(), views.end(), [&](const auto& v) { return v->isViewOf(handler); }),
                views.end());
}

/*
 * What the eraser redraws, told to every view of the page: upstream's EraseHandler redraws only the view it was
 * given (in Xournal++ a page has one view), so a second view of the same page (the self-reference view) kept the
 * erased ink until it was drawn again, and with "delete stroke" even after the release (nothing else fires then).
 * The page's listeners (this view too) redraw the range.
 */
class EraserRedraw final: public LegacyRedrawable {
public:
    explicit EraserRedraw(CanvasPage& view): view(view) {}
    void repaintArea(double x1, double y1, double x2, double y2) const override { view.repaintArea(x1, y1, x2, y2); }
    void repaintPage() const override { view.repaintPage(); }
    void rerenderPage(bool) override { view.getPage()->firePageChanged(); }
    void rerenderRect(double x, double y, double width, double height) override {
        Range range(x, y, x + width, y + height);
        view.getPage()->fireRangeChanged(range);
    }
    GdkRGBA getSelectionColor() override { return view.getSelectionColor(); }
    void deleteViewBuffer() override {}

private:
    CanvasPage& view;
};
}  // namespace

CanvasPage::CanvasPage(CanvasView& view, PageRef page):
        view(view), page(std::move(page)),
        raster(std::make_shared<PageRaster>(&view, &view.getRenderService(), this->page)),
        eraserRedraw(std::make_unique<EraserRedraw>(*this)) {
    registerToHandler(this->page);
    DocumentSession& session = view.getSession();
    // Upstream XojPageView: the eraser handler lives as long as the page view
    this->eraser = std::make_unique<EraseHandler>(session.getUndoRedoHandler(), session.getDocument(), this->page,
                                                  session.getToolHandler(), eraserRedraw.get());
}

CanvasPage::~CanvasPage() {
    // (a stroke being written, the laser pointer's ink: their views on the audience's page go with them)
    eraseViewsOfHandler(inputHandler.get());
    eraseViewsOfHandler(laserPointer.get());
    unregisterFromHandler();
    raster->detach();
    view.getRenderService().cancel(raster.get());
}

QRectF CanvasPage::viewRect() const {
    if (auto idx = view.indexOf(this)) {
        return view.pageViewRect(*idx);
    }
    return {};
}

// --- input (port of XojPageView) ---------------------------------------------------------------------------------

bool CanvasPage::onButtonPressEvent(const PositionInputData& pos) {
    if (currentSequenceDeviceId) {
        // An input sequence is already under way from another device
        return false;
    }
    currentSequenceDeviceId = pos.deviceId;

    DocumentSession& control = view.getSession();
    if (auto idx = view.indexOf(this); idx && *idx != view.currentPageNo()) {
        view.setCurrentPageNo(*idx);  // (the session's page for the primary view, else the view's own)
    }
    // Upstream code that asks for the view or the current page gets this view and this page (a second view of the
    // document: qt/self-reference)
    const auto scope = view.actingScope(view.indexOf(this));
    ToolHandler* h = control.getToolHandler();

    double x = pos.x;
    double y = pos.y;
    if (x < 0 || y < 0) {
        return false;
    }
    const double zoom = getZoom();
    x /= zoom;
    y /= zoom;

    // xournal-qt: sticky notes (qt/docs/sticky-notes.md). A select tool takes a note (the handle of the selected one
    // resizes it, with any tool); the tools that write write on the note under them.
    const ToolType toolType = h->getToolType();
    const bool selectTool = toolType == TOOL_SELECT_RECT || toolType == TOOL_SELECT_REGION ||
                            toolType == TOOL_SELECT_MULTILAYER_RECT || toolType == TOOL_SELECT_MULTILAYER_REGION ||
                            toolType == TOOL_SELECT_OBJECT;
    const bool areaTool = toolType == TOOL_SELECT_RECT || toolType == TOOL_SELECT_REGION ||
                          toolType == TOOL_SELECT_MULTILAYER_RECT || toolType == TOOL_SELECT_MULTILAYER_REGION;
    // Ctrl or Shift with a select tool adds to the selection (a note or an element there joins it or leaves it)
    const bool add = selectTool && (pos.isShiftDown() || pos.isControlDown());
    // Whole notes are selected (one, several, or notes with elements): Ctrl or Shift adds to them or takes away
    const bool together = view.notes().hasSelection() || view.mixed().active();
    // Select more (qt/touch-multiselect): the selected note moves (a tap on it takes it away: CanvasInput) and its
    // handle resizes it; anywhere else the rectangle or lasso starts, never in a note: a tap adds what is there or
    // takes it away, a drag adds what it encloses (onButtonReleaseEvent)
    const bool selectMore = areaTool && view.selectingMore();
    // xournal-qt: the snip tool (Snip.h): the rectangle or lasso of the select tool, over notes and all, for its picture
    if (areaTool && snip::isArmed()) {
        if (!selector) {
            if (toolType == TOOL_SELECT_RECT || toolType == TOOL_SELECT_MULTILAYER_RECT) {
                this->selector = std::make_unique<RectangularSelector>(x, y, false);
            } else {
                this->selector = std::make_unique<LassoSelector>(x, y, false);
            }
            this->overlayViews.emplace_back(std::make_unique<xoj::view::SelectorView>(
                    this->selector.get(), this, control.getSettings()->getSelectionColor()));
            this->selectorNote = nullptr;
            this->snipping = true;
        }
        return true;
    }
    if (!view.isReadingOnly()) {
        bool deselected = false;
        if (selectMore ? view.notes().pressTouch(*this, x, y)
                       : view.notes().press(*this, x, y, selectTool, deselected, areaTool, add)) {
            return true;
        }
    }
    sticky::leaveNoteLayer(*control.getDocument(), page);  // (the tools work on the page's own layer)
    const bool writes = ((toolType == TOOL_PEN || toolType == TOOL_HIGHLIGHTER) &&
                         h->getDrawingType() != DRAWING_TYPE_SPLINE) ||
                        (toolType == TOOL_ERASER && h->getEraserType() != ERASER_TYPE_WHITEOUT) ||
                        toolType == TOOL_TEXT;
    if (writes && !view.isReadingOnly() && pressOnNote(x, y)) {
        return true;
    }

    if (((h->getToolType() == TOOL_PEN || h->getToolType() == TOOL_HIGHLIGHTER) &&
         h->getDrawingType() != DRAWING_TYPE_SPLINE) ||
        (h->getToolType() == TOOL_ERASER && h->getEraserType() == ERASER_TYPE_WHITEOUT)) {
        if (this->inputHandler) {
            // Upstream workaround for https://github.com/xournalpp/xournalpp/issues/4377
            g_warning("InputHandler already exists upon CanvasPage::onButtonPressEvent. Deleting it (and its views)");
            eraseViewsOfHandler(this->inputHandler.get());
            this->inputHandler.reset();
        }
        switch (h->getDrawingType()) {
            case DRAWING_TYPE_LINE:
                this->inputHandler = std::make_unique<RulerHandler>(&control, getPage());
                break;
            case DRAWING_TYPE_RECTANGLE:
                this->inputHandler = std::make_unique<RectangleHandler>(&control, getPage());
                break;
            case DRAWING_TYPE_ELLIPSE:
                this->inputHandler = std::make_unique<EllipseHandler>(&control, getPage());
                break;
            case DRAWING_TYPE_ARROW:
                this->inputHandler = std::make_unique<ArrowHandler>(&control, getPage(), false);
                break;
            case DRAWING_TYPE_DOUBLE_ARROW:
                this->inputHandler = std::make_unique<ArrowHandler>(&control, getPage(), true);
                break;
            case DRAWING_TYPE_COORDINATE_SYSTEM:
                this->inputHandler = std::make_unique<CoordinateSystemHandler>(&control, getPage());
                break;
            default:  // freehand (with the shape recognizer if that drawing type is set; the pen's gestures)
                this->inputHandler = std::make_unique<GestureStrokeHandler>(&control, getPage());
        }
        this->inputHandler->onButtonPressEvent(pos, zoom);
        if (Stroke* stroke = this->inputHandler->getStroke()) {
            penfill::apply(*control.getSettings(), *h, *stroke);  // (its fill color, before its view is made)
            timeline::stampNew(*stroke);  // (when it was made: qt/docs/timeline.md)
            if (h->getToolType() == TOOL_PEN) {
                control.stampAudio(*stroke);  // (a recording runs: upstream's InputHandler::createStroke, qt/docs/audio.md)
            }
        }
        this->overlayViews.emplace_back(this->inputHandler->createView(this));
        mirrorViewOf(this->inputHandler.get(), false);  // (the audience sees it being written)
    } else if (toolType == TOOL_LASER_POINTER_PEN || toolType == TOOL_LASER_POINTER_HIGHLIGHTER) {
        // Port of XojPageView: one handler for the ink of the page until it faded out (never in the document)
        if (!this->laserPointer) {
            this->laserPointer = std::make_unique<LaserPointerHandler>(this, &control, getPage());
            this->laserPointer->onButtonPressEvent(pos, zoom);
            this->overlayViews.emplace_back(this->laserPointer->createView(this));
            drawOnce(*this->overlayViews.back());
            mirrorViewOf(this->laserPointer.get(), true);  // (the audience sees where it points)
        } else {
            this->laserPointer->onButtonPressEvent(pos, zoom);
        }
    } else if (h->getToolType() == TOOL_ERASER) {
        if (eraserInNote(x, y)) {
            this->eraser->erase(x, y);
        }
        this->inEraser = true;
    } else if (h->getToolType() == TOOL_SELECT_RECT || h->getToolType() == TOOL_SELECT_REGION ||
               h->getToolType() == TOOL_SELECT_MULTILAYER_RECT || h->getToolType() == TOOL_SELECT_MULTILAYER_REGION) {
        if (!selector) {
            const bool multiLayer =
                    h->getToolType() == TOOL_SELECT_MULTILAYER_RECT || h->getToolType() == TOOL_SELECT_MULTILAYER_REGION;
            if (h->getToolType() == TOOL_SELECT_RECT || h->getToolType() == TOOL_SELECT_MULTILAYER_RECT) {
                this->selector = std::make_unique<RectangularSelector>(x, y, multiLayer);
            } else {
                this->selector = std::make_unique<LassoSelector>(x, y, multiLayer);
            }
            this->overlayViews.emplace_back(
                    std::make_unique<xoj::view::SelectorView>(this->selector.get(), this,
                                                              control.getSettings()->getSelectionColor()));
            // xournal-qt: started on a sticky note (that can be written on): it selects in the note
            std::shared_lock lock(*control.getDocument());
            this->selectorNote = view.isReadingOnly() || selectMore ? nullptr : sticky::openNoteAt(*page, x, y);
        }
    } else if (h->getToolType() == TOOL_TEXT) {
        view.startText(*this, x, y);
        leaveNote();  // (the text editor keeps the note's layer for its text)
    } else if (h->getToolType() == TOOL_SELECT_PDF_TEXT_LINEAR || h->getToolType() == TOOL_SELECT_PDF_TEXT_RECT) {
        view.pdfTextPress(*this, x, y);
    } else if (h->getToolType() == TOOL_PLAY_OBJECT) {
        view.playAt(*this, x, y);  // (upstream's PlayObject: the ink's recording, qt/docs/audio.md)
    } else if (h->getToolType() == TOOL_SELECT_OBJECT) {
        const bool aggregate = add && (view.getSelection() || together);
        selectObjectAt(x, y, false, aggregate);
    }
    return true;
}

bool CanvasPage::selectObjectAt(double x, double y, bool multiLayer, bool aggregate) {
    // Port of SelectObject::at / atAggregate (gui/PageViewFindObjectHelper.h)
    DocumentSession& ctrl = view.getSession();
    if (aggregate && (view.notes().hasSelection() || view.mixed().active())) {
        // xournal-qt: whole sticky notes are selected: the element there joins them or leaves them (in its layer:
        // the selected one or the Markdown texts; a multi-layer tool: the topmost with an element there, no note)
        Element* found = nullptr;
        {
            std::shared_lock lock(*ctrl.getDocument());
            const Layer* selectedLayer = page->getSelectedLayer();
            const auto layers = page->getLayersView();
            for (auto l = layers.rbegin(); l != layers.rend() && !found; ++l) {
                if (sticky::isNote(**l) || !(*l)->isVisible() ||
                    (!multiLayer && *l != selectedLayer && !md::isMarkdownLayer(**l))) {
                    continue;
                }
                constexpr double RADIUS = 5.;
                double best = RADIUS;
                for (const Element* e: (*l)->getElementsView()) {
                    if (e->intersectsArea(x - RADIUS, y - RADIUS, 2 * RADIUS, 2 * RADIUS)) {
                        if (const double d = e->distanceTo(x, y); d < best || d == 0.0) {
                            best = d;
                            found = const_cast<Element*>(e);
                        }
                    }
                }
            }
        }
        if (found) {
            view.toggleSelected(*this, nullptr, found);
        }
        return found != nullptr;
    }
    EditSelection* previous = aggregate ? view.getSelection() : nullptr;
    if (!aggregate) {
        view.clearSelection();
    }
    const Element* match = nullptr;
    Element::Index matchIndex = 0;
    auto checkLayer = [&](const Layer* l) {
        constexpr double ACTION_RADIUS = 5.;
        double minDistance = ACTION_RADIUS;
        Element::Index pos = as_signed(l->getElementsView().size());
        for (auto it = l->getElementsView().rbegin(); it < l->getElementsView().rend(); ++it) {
            pos--;
            if ((*it)->intersectsArea(x - minDistance, y - minDistance, 2. * minDistance, 2. * minDistance)) {
                const double d = (*it)->distanceTo(x, y);
                if (d == 0.0) {
                    match = *it;
                    matchIndex = pos;
                    return true;
                }
                if (d < minDistance) {
                    match = *it;
                    matchIndex = pos;
                    minDistance = d;
                }
            }
        }
        return minDistance != ACTION_RADIUS;
    };
    std::optional<Layer::Index> markdownBefore;  // xournal-qt: a Markdown text (see CanvasView::selectMarkdownLayer)
    {
        std::shared_lock lock(*ctrl.getDocument());
        if (multiLayer && !aggregate) {
            const auto& layers = page->getLayers();
            size_t layerNo = layers.size();
            for (auto l = layers.rbegin(); l != layers.rend(); l++, layerNo--) {
                if (sticky::isNote(**l)) {
                    continue;  // xournal-qt: a sticky note is selected whole (StickyNotes::press)
                }
                if (checkLayer(*l)) {
                    const auto found = as_unsigned(std::distance(l, layers.rend()));
                    if (md::isMarkdownLayer(**l)) {
                        markdownBefore = page->getSelectedLayerId();
                    }
                    lock.unlock();
                    ctrl.getLayerController()->switchToLay(found);
                    break;
                }
            }
        } else if (!checkLayer(page->getSelectedLayer()) && !aggregate) {
            // Nothing in the selected layer: a Markdown text
            const Layer* mdLayer = md::markdownLayer(page);
            if (mdLayer && mdLayer->isVisible() && mdLayer != page->getSelectedLayer() && checkLayer(mdLayer)) {
                lock.unlock();
                markdownBefore = view.selectMarkdownLayer(page);
            }
        }
    }
    if (!match) {
        return false;
    }
    // xournal-qt: a member of a group selects the whole group (qt/docs/groups.md)
    InsertionOrderRef members;
    {
        std::shared_lock lock(*ctrl.getDocument());
        const Layer* layer = aggregate && previous ? previous->getSourceLayer() : page->getSelectedLayer();
        members = groups::withMembers(*layer, InsertionOrderRef{InsertionPositionRef(match, matchIndex)});
    }
    if (aggregate && previous) {
        auto sel = members.size() > 1
                           ? SelectionFactory::addElementsFromActiveLayer(&ctrl, previous, members)
                           : SelectionFactory::addElementFromActiveLayer(&ctrl, previous, match, matchIndex);
        view.setSelection(sel.release());
    } else {
        auto sel = members.size() > 1
                           ? SelectionFactory::createFromElementsOnActiveLayer(&ctrl, page, this, members)
                           : SelectionFactory::createFromElementOnActiveLayer(&ctrl, page, this, match, matchIndex);
        view.setSelection(sel.release());
        if (markdownBefore) {
            view.markdownSelectionMade(page, *markdownBefore);
        }
    }
    repaintPage();
    return true;
}

XournalView* CanvasPage::getXournal() const { return &view; }

void CanvasPage::addOverlayView(std::unique_ptr<xoj::view::OverlayView> v) {
    overlayViews.emplace_back(std::move(v));
    flagDirtyRegion(Range(0, 0, getWidth(), getHeight()));
}

void CanvasPage::removeOverlayViewsOf(const OverlayBase* o) {
    eraseViewsOf(overlayViews, o);
    flagDirtyRegion(Range(0, 0, getWidth(), getHeight()));
}

// --- the audience's screen of the presenter view (CanvasView::setMirror) ---------------------------------------------

CanvasPage* CanvasPage::mirrorPage() const {
    CanvasView* m = view.mirror();
    return m ? m->canvasPageOf(page.get()) : nullptr;
}

template <typename Handler>
void CanvasPage::mirrorViewOf(const Handler* handler, bool once) {
    // Upstream's handlers tell all their views (a DispatchPool): a second view of the stroke on the audience's page
    // follows it as the first one does, and goes when the handler finishes it (or with eraseViewsOfHandler)
    if (CanvasPage* m = mirrorPage(); m && handler) {
        m->addMirroredView(handler, handler->createView(m), once);
    }
}

void CanvasPage::addMirroredView(const OverlayBase* handler, std::unique_ptr<xoj::view::OverlayView> v, bool once) {
    if (!v) {
        return;
    }
    if (std::find(mirrored.begin(), mirrored.end(), handler) == mirrored.end()) {
        mirrored.push_back(handler);
    }
    overlayViews.emplace_back(std::move(v));
    if (once) {
        drawOnce(*overlayViews.back());
    }
    flagDirtyRegion(Range(0, 0, getWidth(), getHeight()));
}

bool CanvasPage::isMirroredView(const xoj::view::OverlayView* v) const {
    return std::any_of(mirrored.begin(), mirrored.end(), [v](const OverlayBase* h) { return v->isViewOf(h); });
}

void CanvasPage::dropMirroredViews() {
    if (mirrored.empty()) {
        return;
    }
    for (const OverlayBase* h: std::exchange(mirrored, {})) {
        eraseViewsOf(overlayViews, h);
    }
    flagDirtyRegion(Range(0, 0, getWidth(), getHeight()));
}

size_t CanvasPage::mirroredViewCount() const {
    return static_cast<size_t>(std::count_if(overlayViews.begin(), overlayViews.end(),
                                             [this](const auto& v) { return isMirroredView(v.get()); }));
}

void CanvasPage::eraseViewsOfHandler(const OverlayBase* handler) {
    if (!handler) {
        return;
    }
    eraseViewsOf(overlayViews, handler);
    if (CanvasPage* m = mirrorPage()) {
        if (const auto it = std::find(m->mirrored.begin(), m->mirrored.end(), handler); it != m->mirrored.end()) {
            m->mirrored.erase(it);
            m->removeOverlayViewsOf(handler);
        }
    }
}

xoj::util::Point<int> CanvasPage::getPixelPosition() const {
    // Content pixels (upstream: the page's position in the layout, independent of scrolling).
    if (auto idx = view.indexOf(this)) {
        const QRectF r = view.documentLayout().pageRect(*idx, view.getViewController().zoom());
        return {static_cast<int>(std::lround(r.x())), static_cast<int>(std::lround(r.y()))};
    }
    return {0, 0};
}

void CanvasPage::drawOnce(const xoj::view::OverlayView& v) {
    // Upstream's LaserPointerView makes its picture (a mask of the visible part of the page) when it is first drawn,
    // and a stroke finished before that is not in it. GTK draws the view at once; here the next frame may come after
    // a quick stroke (a dot), so it is drawn once now, on a pixel, with the device scale of the page's buffer.
    double dpiScale = 1.0;
    raster->withPlacedBuffer([&](xoj::view::Mask& buffer, const PageRaster::Placement&) {
        if (buffer.isInitialized()) {
            cairo_surface_get_device_scale(cairo_get_target(buffer.get()), &dpiScale, &dpiScale);
        }
    });
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_surface_set_device_scale(surface, dpiScale, dpiScale);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, getZoom(), getZoom());
    v.draw(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void CanvasPage::deleteLaserPointerHandler() {
    // Port of XojPageView::deleteLaserPointerHandler: its view went with the last step of the fade
    eraseViewsOfHandler(this->laserPointer.get());
    this->laserPointer.reset();
}

ZoomControl* CanvasPage::getZoomControl() const { return view.getZoomControl(); }

bool CanvasPage::onMotionNotifyEvent(const PositionInputData& pos) {
    if (currentSequenceDeviceId && currentSequenceDeviceId != pos.deviceId) {
        // This motion event is not from the device which started the sequence: reject it
        return false;
    }
    const double zoom = getZoom();
    const double x = pos.x / zoom;
    const double y = pos.y / zoom;
    ToolHandler* h = view.getSession().getToolHandler();

    if (this->inputHandler && this->inputHandler->onMotionNotifyEvent(pos, zoom)) {
        // input handler used this event
    } else if (this->laserPointer && this->laserPointer->onMotionNotifyEvent(pos, zoom)) {
        // the laser pointer's stroke
    } else if (this->selector) {
        this->selector->currentPos(x, y);
    } else if (h->getToolType() == TOOL_SELECT_PDF_TEXT_LINEAR || h->getToolType() == TOOL_SELECT_PDF_TEXT_RECT) {
        view.pdfTextMove(*this, x, y);
    } else if (CanvasTextInput* editor = view.getTextInput(); editor && &editor->getPage() == this &&
                                                                h->getToolType() == TOOL_TEXT && currentSequenceDeviceId) {
        editor->mouseMoved(x, y);  // drag: select text
    } else if (h->getToolType() == TOOL_ERASER && h->getEraserType() != ERASER_TYPE_WHITEOUT && this->inEraser) {
        double ex = x;
        double ey = y;
        if (eraserInNote(ex, ey)) {
            this->eraser->erase(ex, ey);
        }
    }
    return false;
}

bool CanvasPage::onButtonReleaseEvent(const PositionInputData& pos) {
    if (currentSequenceDeviceId != pos.deviceId) {
        // This event is not from the device which started the sequence: reject it
        return false;
    }
    currentSequenceDeviceId.reset();

    DocumentSession& control = view.getSession();
    const auto scope = view.actingScope(view.indexOf(this));
    if (this->inputHandler) {
        this->inputHandler->onButtonReleaseEvent(pos, getZoom());
        eraseViewsOfHandler(this->inputHandler.get());  // (what it did not finish itself; and on the audience's)
        this->inputHandler.reset();
    } else if (const ToolType t = control.getToolHandler()->getToolType();
               this->laserPointer && (t == TOOL_LASER_POINTER_PEN || t == TOOL_LASER_POINTER_HIGHLIGHTER)) {
        this->laserPointer->onButtonReleaseEvent(pos, getZoom());
    }
    if (this->inEraser) {
        this->inEraser = false;
        Document* doc = control.getDocument();
        doc->lock();
        this->eraser->finalize();
        doc->unlock();
    }
    leaveNote();
    if (coverPress) {
        // A tap on a covering note (not a stroke): it peeks, or covers again
        const auto [cx, cy] = *coverPress;
        coverPress.reset();
        const double zoom = getZoom();
        if (std::hypot(pos.x / zoom - cx, pos.y / zoom - cy) <= 8 / zoom) {
            view.notes().tapCover(*this, cx, cy);
        }
    }
    if (ToolType t = control.getToolHandler()->getToolType();
        t == TOOL_SELECT_PDF_TEXT_LINEAR || t == TOOL_SELECT_PDF_TEXT_RECT) {
        view.pdfTextRelease(*this);
    }
    if (this->selector && std::exchange(this->snipping, false)) {
        // xournal-qt: a snip (Snip.h): the picture of what it encloses (a tap: nothing, the tool stays armed)
        const bool tapped = this->selector->userTapped(getZoom());
        const bool rectangle = dynamic_cast<RectangularSelector*>(this->selector.get()) != nullptr;
        const auto outline = this->selector->getBoundary();  // (before finalize: it extends it at the page's edges)
        (void)this->selector->finalize(this->page, true, control.getDocument());  // (its picture goes)
        (void)this->selector->releaseElements();
        this->selector.reset();
        if (!tapped && view.snip(*this, outline, rectangle)) {
            snip::disarm();
        }
        repaintPage();
        return false;
    }
    if (this->selector && this->selectorNote) {
        // xournal-qt: a rectangle or lasso started on a sticky note: the note's elements (a tap: the note)
        selectInNote(std::exchange(this->selectorNote, nullptr), this->selector->userTapped(getZoom()));
        this->selector.reset();
    }
    if (this->selector && view.selectingMore()) {
        // Select more (qt/touch-multiselect): a tap adds what is there or takes it away (on another page: select more
        // ends, and what is there is selected alone); a drag adds what the rectangle or lasso encloses (below)
        const double zoom = getZoom();
        if (this->selector->userTapped(zoom)) {
            (void)this->selector->finalize(this->page, true, control.getDocument());  // (its picture goes)
            (void)this->selector->releaseElements();
            this->selector.reset();
            view.toggleAt(*this, pos.x / zoom, pos.y / zoom);
            repaintPage();
            return false;
        }
        if (view.selectionPage() != this) {
            view.setSelectingMore(false);  // (a rectangle on another page: as without it)
        }
    }
    if (this->selector) {
        // Port of XojPageView::onButtonReleaseEvent (selector part)
        // xournal-qt: the lasso the selection is made with (a sticker's picture is cut to it: qt/docs/stickers.md)
        const bool lassoUsed = dynamic_cast<LassoSelector*>(this->selector.get()) != nullptr &&
                               !this->selector->userTapped(getZoom());
        view.selectedWith(this->page, lassoUsed ? this->selector->getBoundary()
                                                : std::vector<xoj::util::Point<double>>());
        const bool add = pos.isShiftDown() || pos.isControlDown() || view.selectingMore();
        const bool together = view.notes().hasSelection() || view.mixed().active();
        const bool aggregate = add && (view.getSelection() || together);
        if (!this->selector->userTapped(getZoom()) && selectNotesAndElements(aggregate)) {
            // xournal-qt: whole sticky notes in it (qt/docs/sticky-notes.md, "Several notes at once")
            this->selector.reset();
            return false;
        }
        size_t layerOfFinalizedSel = this->selector->finalize(this->page, aggregate, control.getDocument());
        if (layerOfFinalizedSel) {
            // xournal-qt: a multi-layer selection never takes a sticky note apart (a tap selects the note)
            std::shared_lock lock(*control.getDocument());
            const auto layers = this->page->getLayersView();
            if (layerOfFinalizedSel <= layers.size() && sticky::isNote(*layers[layerOfFinalizedSel - 1])) {
                (void)this->selector->releaseElements();
                layerOfFinalizedSel = 0;
            }
        }
        // xournal-qt: nothing in the selected layer: Markdown texts (in the page's layer "Markdown")
        std::optional<Layer::Index> markdownBefore;
        if (!layerOfFinalizedSel && !aggregate && !selector->userTapped(getZoom())) {
            markdownBefore = view.selectMarkdownLayer(this->page);
            if (markdownBefore) {
                layerOfFinalizedSel = this->selector->finalize(this->page, true, control.getDocument());
                if (!layerOfFinalizedSel) {
                    view.restoreSelectedLayer(this->page, *markdownBefore);
                    markdownBefore.reset();
                }
            }
        } else if (layerOfFinalizedSel && !aggregate && view.isMarkdownLayer(this->page, layerOfFinalizedSel)) {
            markdownBefore = this->page->getSelectedLayerId();  // (a multi-layer selector found Markdown texts)
        }
        if (layerOfFinalizedSel) {
            // xournal-qt: a group with any member in it is selected whole (qt/docs/groups.md)
            const auto withGroups = [&](const InsertionOrderRef& found) {
                std::shared_lock lock(*control.getDocument());
                const auto layers = this->page->getLayersView();
                return layerOfFinalizedSel <= layers.size()
                               ? groups::withMembers(*layers[layerOfFinalizedSel - 1], found)
                               : found;
            };
            if (aggregate) {
                auto sel = withGroups(selector->releaseElements());
                view.setSelection(
                        SelectionFactory::addElementsFromActiveLayer(&control, view.getSelection(), sel).release());
            } else {
                // with a multi-layer selector the objects might be on another layer
                if (view.isPrimary()) {
                    control.getLayerController()->switchToLay(layerOfFinalizedSel);
                } else {
                    // (a second view: the layer controller follows the session's page, the primary view's)
                    control.clearSelectionEndText();
                    std::unique_lock lock(*control.getDocument());
                    this->page->setSelectedLayerId(layerOfFinalizedSel);
                }
                view.setSelection(SelectionFactory::createFromElementsOnActiveLayer(
                                          &control, page, this, withGroups(selector->releaseElements()))
                                          .release());
                if (markdownBefore) {
                    view.markdownSelectionMade(this->page, *markdownBefore);
                }
            }
        } else if (const double zoom = getZoom(); selector->userTapped(zoom)) {
            selectObjectAt(pos.x / zoom, pos.y / zoom, this->selector->isMultiLayerSelection(), aggregate);
        }
        this->selector.reset();
    }
    return false;
}

bool CanvasPage::straightenStroke() {
    auto* handler = dynamic_cast<GestureStrokeHandler*>(this->inputHandler.get());
    if (!handler) {
        return false;
    }
    const auto scope = view.actingScope(view.indexOf(this));
    return handler->straighten(pengestures::HOLD_SLOP_PX / getZoom());
}

void CanvasPage::onSequenceCancelEvent(DeviceId deviceId) {
    if (currentSequenceDeviceId != deviceId) {
        return;
    }
    currentSequenceDeviceId.reset();
    if (this->inputHandler) {
        this->inputHandler->onSequenceCancelEvent();
        eraseViewsOfHandler(this->inputHandler.get());
        this->inputHandler.reset();
    } else if (this->laserPointer) {
        this->laserPointer->onSequenceCancelEvent();
    }
    if (this->inEraser) {
        // xournal-qt: keep what was erased so far (the erase is undoable), like a release.
        this->inEraser = false;
        Document* doc = view.getSession().getDocument();
        doc->lock();
        this->eraser->finalize();
        doc->unlock();
    }
    leaveNote();
    coverPress.reset();
    this->snipping = false;
    this->selector.reset();  // (its view goes with it)
}

// --- sticky notes ------------------------------------------------------------------------------------------------

bool CanvasPage::pressOnNote(double x, double y) {
    Document* doc = view.getSession().getDocument();
    std::optional<sticky::Look> look;
    Layer* note = nullptr;
    {
        std::shared_lock lock(*doc);
        note = sticky::noteAt(*page, x, y);
        if (note) {
            look = sticky::lookOf(*note);
        }
    }
    if (!note || !look) {
        return false;
    }
    if (look->cover) {
        coverPress = std::make_pair(x, y);  // nothing is written on it (nor under it)
        return true;
    }
    {
        std::unique_lock lock(*doc);
        layerBeforeNote = page->getSelectedLayerId();
        page->setSelectedLayerId(sticky::layerIdOf(*page, note));
    }
    noteClip = look->rect;
    return false;
}

void CanvasPage::selectInNote(Layer* note, bool tapped) {
    DocumentSession& control = view.getSession();
    Document* doc = control.getDocument();
    {
        std::shared_lock lock(*doc);
        if (!sticky::layerIdOf(*page, note) || !sticky::isNote(*note)) {
            note = nullptr;  // (gone meanwhile)
        }
    }
    if (!note) {
        (void)this->selector->finalize(this->page, true, doc);  // (its picture goes)
        return;
    }
    const Layer::Index before = view.selectNoteLayer(this->page, note);
    (void)this->selector->finalize(this->page, true, doc);  // (the selected layer: the note's)
    InsertionOrderRef elements = this->selector->releaseElements();
    {
        // Never the paper, never the note's Markdown text: they are the note (its frame)
        std::shared_lock lock(*doc);
        const Element* paper = sticky::paperOf(*note);
        const Element* text = sticky::textOf(*note);
        elements.erase(std::remove_if(elements.begin(), elements.end(),
                                      [&](const InsertionPositionRef& r) { return r.e == paper || r.e == text; }),
                       elements.end());
    }
    if (tapped || elements.empty()) {
        view.restoreSelectedLayer(this->page, before);
        if (tapped) {
            view.notes().select(*this, note);  // a tap selects the whole note
        }
        repaintPage();
        return;
    }
    {
        std::shared_lock lock(*doc);
        elements = groups::withMembers(*note, elements);  // xournal-qt: groups whole (qt/docs/groups.md)
    }
    view.setSelection(SelectionFactory::createFromElementsOnActiveLayer(&control, this->page, this, elements).release());
    view.noteSelectionMade(this->page, before, note);
    repaintPage();
}

bool CanvasPage::selectNotesAndElements(bool add) {
    DocumentSession& control = view.getSession();
    Document* doc = control.getDocument();
    std::vector<Layer*> notes;
    {
        // The notes wholly in it (their paper's corners): a note is selected whole, never taken apart
        std::shared_lock lock(*doc);
        for (Layer* l: page->getLayers()) {
            if (const Stroke* paper = sticky::paperOf(*l); paper && l->isVisible() && paper->isInSelection(selector.get())) {
                notes.push_back(l);
            }
        }
    }
    if (notes.empty() && !(add && (view.notes().hasSelection() || view.mixed().active()))) {
        return false;  // (no note: as before)
    }
    (void)this->selector->finalize(this->page, true, doc);  // (its picture goes; what it found is looked for below)
    (void)this->selector->releaseElements();
    // The page's elements in it: in the selected layer (else the page's Markdown texts), as a selection of elements
    // takes them; a multi-layer tool: the topmost layer with elements in it that is no note
    std::vector<MixedSelection::Item> items;
    {
        std::shared_lock lock(*doc);
        const auto inLayer = [&](Layer* l) {
            for (const Element* e: l->getElementsView()) {
                if (e->isInSelection(selector.get())) {
                    items.push_back({l, const_cast<Element*>(e)});
                }
            }
            return !items.empty();
        };
        if (selector->isMultiLayerSelection()) {
            const auto layers = page->getLayers();
            for (auto l = layers.rbegin(); l != layers.rend(); ++l) {
                if (!sticky::isNote(**l) && (*l)->isVisible() && inLayer(*l)) {
                    break;
                }
            }
        } else if (Layer* selected = page->getSelectedLayer(); selected && !sticky::isNote(*selected)) {
            if (!inLayer(selected)) {
                if (Layer* mdLayer = md::markdownLayer(page); mdLayer && mdLayer->isVisible() && mdLayer != selected) {
                    inLayer(mdLayer);
                }
            }
        }
    }
    if (add) {
        // Added to what is selected (on this page)
        auto [had, hadItems] = view.takeSelected(*this);
        for (Layer* n: had) {
            if (std::find(notes.begin(), notes.end(), n) == notes.end()) {
                notes.push_back(n);
            }
        }
        for (const auto& item: hadItems) {
            if (std::none_of(items.begin(), items.end(), [&](const auto& i) { return i.element == item.element; })) {
                items.push_back(item);
            }
        }
    }
    view.selectTogether(*this, notes, items);
    repaintPage();
    return true;
}

void CanvasPage::leaveNote() {
    if (layerBeforeNote) {
        std::unique_lock lock(*view.getSession().getDocument());
        page->setSelectedLayerId(*layerBeforeNote);
    }
    layerBeforeNote.reset();
    noteClip.reset();
}

bool CanvasPage::eraserInNote(double& x, double& y) const {
    if (!noteClip) {
        return true;
    }
    const Rectangle<double>& r = *noteClip;
    const double margin = view.getSession().getToolHandler()->getThickness() + sticky::PAPER_WIDTH + 0.5;
    if (r.width < 2 * margin || r.height < 2 * margin) {
        return false;
    }
    x = std::clamp(x, r.x + margin, r.x + r.width - margin);
    y = std::clamp(y, r.y + margin, r.y + r.height - margin);
    return true;
}

// --- display -----------------------------------------------------------------------------------------------------

auto CanvasPage::bufferInfo() -> BufferInfo {
    return raster->withPlacedBuffer([](xoj::view::Mask& buffer, const PageRaster::Placement& place) {
        BufferInfo info;
        if (!buffer.isInitialized()) {
            return info;
        }
        cairo_surface_t* s = cairo_get_target(buffer.get());
        info.valid = true;
        info.zoom = buffer.getZoom();
        cairo_surface_get_device_scale(s, &info.dpiScale, &info.dpiScale);
        info.pixelSize = QSize(cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
        info.whole = place.whole;
        info.origin = QPoint(place.x, place.y);
        info.area = QRectF(place.area.x, place.area.y, place.area.width, place.area.height);
        return info;
    });
}

QImage CanvasPage::composeTile(const QRect& pixelRect) {
    QImage img(pixelRect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    const TimelineReplay* replay = view.replay();
    const std::optional<size_t> index = replay ? view.indexOf(this) : std::nullopt;
    raster->withDrawnBuffer([&](xoj::view::Mask& buffer, const PageRaster::Placement& place,
                                const render::ElementFilter* drawn) {
        if (!buffer.isInitialized()) {
            return;
        }
        const double bufferZoom = buffer.getZoom();
        double dpiScale = 1.0;
        cairo_surface_get_device_scale(cairo_get_target(buffer.get()), &dpiScale, &dpiScale);

        // CAIRO_FORMAT_ARGB32 and QImage::Format_ARGB32_Premultiplied share the memory layout.
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
                img.bits(), CAIRO_FORMAT_ARGB32, img.width(), img.height(), static_cast<int>(img.bytesPerLine()));
        cairo_surface_set_device_scale(surface, dpiScale, dpiScale);
        cairo_t* cr = cairo_create(surface);
        cairo_translate(cr, -pixelRect.x() / dpiScale - place.x, -pixelRect.y() / dpiScale - place.y);
        cairo_scale(cr, bufferZoom, bufferZoom);  // page coordinates
        buffer.paintTo(cr);
        // xournal-qt: the replay of the timeline: what the picture lacks at its moment (TimelineReplay.h)
        if (replay && index) {
            replay->drawOverlay(cr, *index, drawn);
        }
        // Upstream XojPageView::paintPage: the overlays draw in page coordinates on top of the buffer.
        for (const auto& v: this->overlayViews) {
            // xournal-qt: a stroke written on a sticky note is clipped to it while it is drawn
            const bool clip = noteClip && inputHandler && v->isViewOf(inputHandler.get());
            if (clip) {
                cairo_save(cr);
                cairo_rectangle(cr, noteClip->x, noteClip->y, noteClip->width, noteClip->height);
                cairo_clip(cr);
            }
            v->draw(cr);
            if (clip) {
                cairo_restore(cr);
            }
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    });
    return img;
}

std::vector<QRect> CanvasPage::takeDirty(const BufferInfo& info, bool& all) {
    all = std::exchange(allDirty, false);
    std::vector<QRect> rects;
    if (!all) {
        const double s = info.zoom * info.dpiScale;
        const QRect bounds(QPoint(0, 0), info.pixelSize);
        // (from the buffer's top left: a page drawn in part)
        const double ox = info.origin.x() * info.dpiScale, oy = info.origin.y() * info.dpiScale;
        for (const Range& r: dirtyRanges) {
            if (!r.isValid() || r.empty()) {
                continue;
            }
            QRect px(QPoint(static_cast<int>(std::floor(r.minX * s - ox)) - 1,
                            static_cast<int>(std::floor(r.minY * s - oy)) - 1),
                     QPoint(static_cast<int>(std::ceil(r.maxX * s - ox)) + 1,
                            static_cast<int>(std::ceil(r.maxY * s - oy)) + 1));
            px = px.intersected(bounds);
            if (!px.isEmpty()) {
                rects.push_back(px);
            }
        }
    }
    dirtyRanges.clear();
    return rects;
}

void CanvasPage::rasterUpdated(std::optional<Rectangle<double>> area) {
    if (area) {
        flagDirtyRegion(Range(*area));
    } else {
        repaintPage();
        view.pageRendered(this);
    }
}

// --- xoj::view::Repaintable --------------------------------------------------------------------------------------

Range CanvasPage::getVisiblePart() const {
    const QRectF pageView = viewRect();
    const QRectF visible = QRectF(QPointF(0, 0), view.documentLayout().pageCount() ? view.getViewController().viewSize()
                                                                               : QSizeF())
                                   .intersected(pageView);
    if (visible.isEmpty()) {
        return Range();
    }
    const double zoom = getZoom();
    const QRectF local((visible.topLeft() - pageView.topLeft()) / zoom, visible.size() / zoom);
    return Range(local.left(), local.top(), local.right(), local.bottom());
}

double CanvasPage::getZoom() const { return view.getViewController().zoom(); }
double CanvasPage::getWidth() const { return page->getWidth(); }
double CanvasPage::getHeight() const { return page->getHeight(); }

auto CanvasPage::toWidgetCoordinates(const xoj::util::Point<double>& p) const -> xoj::util::Point<double> {
    const QRectF r = viewRect();
    const double zoom = getZoom();
    return {r.x() + p.x * zoom, r.y() + p.y * zoom};
}

auto CanvasPage::toWidgetCoordinates(const Rectangle<double>& rect) const -> Rectangle<double> {
    const QRectF r = viewRect();
    const double zoom = getZoom();
    return {r.x() + rect.x * zoom, r.y() + rect.y * zoom, rect.width * zoom, rect.height * zoom};
}

void CanvasPage::flagDirtyRegion(const Range& rg) const {
    if (allDirty || rg.empty()) {
        return;
    }
    if (dirtyRanges.size() >= 64) {
        allDirty = true;  // many small updates (e.g. while the page is not shown): recompose everything once
        dirtyRanges.clear();
    } else {
        dirtyRanges.push_back(rg);
    }
    Q_EMIT view.updateRequested();
}

void CanvasPage::drawAndDeleteToolView(xoj::view::ToolView* v, const Range& rg) {
    // (a stroke finished on the presenter's page: drawn onto the audience's page as well, until it is rendered)
    if (v->isViewOf(this->inputHandler.get()) || isMirroredView(v)) {
        // Draw the inputHandler's view onto the page buffer (upstream: no re-render, no flicker).
        const bool drawn = raster->withBuffer([&](xoj::view::Mask& buffer) {
            if (auto* cr = buffer.get(); cr) {
                cairo_save(cr);
                if (noteClip) {  // (on a sticky note: clipped to it, as the note draws it)
                    cairo_rectangle(cr, noteClip->x, noteClip->y, noteClip->width, noteClip->height);
                    cairo_clip(cr);
                }
                v->drawWithoutDrawingAids(cr);
                cairo_restore(cr);
                return true;
            }
            return false;
        });
        if (!drawn) {
            rerenderPage();
        }
    }
    this->deleteOverlayView(v, rg);
}

void CanvasPage::deleteOverlayView(xoj::view::OverlayView* v, const Range& rg) {
    auto it = std::find_if(overlayViews.begin(), overlayViews.end(), [v](const auto& p) { return p.get() == v; });
    if (it != overlayViews.end()) {
        overlayViews.erase(it);
    }
    if (!rg.empty()) {
        flagDirtyRegion(rg);
    }
}

// --- LegacyRedrawable --------------------------------------------------------------------------------------------

void CanvasPage::repaintArea(double x1, double y1, double x2, double y2) const { flagDirtyRegion(Range(x1, y1, x2, y2)); }

void CanvasPage::repaintPage() const {
    allDirty = true;
    dirtyRanges.clear();
    Q_EMIT view.updateRequested();
}

void CanvasPage::rerenderPage(bool sizeChanged) {
    links.reset();  // (whatever changed, the links are looked for again)
    raster->rerenderPage(sizeChanged);
}

void CanvasPage::rerenderRect(double x, double y, double width, double height) {
    links.reset();
    raster->rerenderRect(x, y, width, height);
}

GdkRGBA CanvasPage::getSelectionColor() {
    return Util::rgb_to_GdkRGBA(view.getSession().getSettings()->getSelectionColor());
}

void CanvasPage::deleteViewBuffer() { raster->releaseBuffer(); }

// --- PageListener (port of XojPageView) ----------------------------------------------------------------------------

void CanvasPage::rectChanged(Rectangle<double>& rect) { rerenderRect(rect.x, rect.y, rect.width, rect.height); }

void CanvasPage::rangeChanged(Range& range) {
    links.reset();
    rerenderRange(range);
    if (view.notes().selectedPage() == this) {
        // (a selected sticky note changed, maybe by undo: its outline and handle too)
        Range around = range;
        around.addPadding((StickyNotes::HANDLE_RADIUS_PX + 4) / getZoom());
        flagDirtyRegion(around);
    }
}

void CanvasPage::pageChanged() { rerenderPage(); }

void CanvasPage::elementChanged(const Element* elem) {
    links.reset();
    /*
     * Upstream: the input handlers issue an elementChanged event when creating an element. There is no need to redraw
     * it: it was already painted to the buffer via drawAndDeleteToolView. Exceptions: the element is not on the
     * top-most layer, or it overflows the visible part of the page.
     */
    const bool noRerender = inputHandler && elem == inputHandler->getStroke() &&
                            page->getSelectedLayerId() == page->getLayerCount() &&
                            getVisiblePart().contains(elem->getBoundingBox());
    if (!noRerender) {
        rerenderElement(elem);
    }
}

void CanvasPage::elementsChanged(const std::vector<const Element*>&, const Range& range) {
    links.reset();
    if (!range.empty()) {
        rerenderRange(range);
    }
}

}  // namespace xqt
