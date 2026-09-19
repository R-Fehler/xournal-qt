#include "CanvasPage.h"

#include <algorithm>
#include <cmath>

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
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"
#include "util/Color.h"
#include "util/Point.h"
#include "util/Rectangle.h"
#include "view/Mask.h"
#include "view/overlays/OverlayView.h"

#include "CanvasView.h"
#include "TextEditor.h"
#include "render/RenderService.h"
#include "session/DocumentSession.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
template <typename Views, typename Handler>
void eraseViewsOf(Views& views, const Handler* handler) {
    views.erase(std::remove_if(views.begin(), views.end(), [&](const auto& v) { return v->isViewOf(handler); }),
                views.end());
}
}  // namespace

CanvasPage::CanvasPage(CanvasView& view, PageRef page):
        view(view), page(std::move(page)),
        raster(std::make_shared<PageRaster>(&view, &view.getRenderService(), this->page)) {
    registerToHandler(this->page);
    DocumentSession& session = view.getSession();
    // Upstream XojPageView: the eraser handler lives as long as the page view
    this->eraser = std::make_unique<EraseHandler>(session.getUndoRedoHandler(), session.getDocument(), this->page,
                                                  session.getToolHandler(), this);
}

CanvasPage::~CanvasPage() {
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
    if (auto idx = view.indexOf(this); idx && *idx != control.getCurrentPageNo()) {
        control.setCurrentPageNo(*idx);
    }
    ToolHandler* h = control.getToolHandler();

    double x = pos.x;
    double y = pos.y;
    if (x < 0 || y < 0) {
        return false;
    }
    const double zoom = getZoom();
    x /= zoom;
    y /= zoom;

    if (((h->getToolType() == TOOL_PEN || h->getToolType() == TOOL_HIGHLIGHTER) &&
         h->getDrawingType() != DRAWING_TYPE_SPLINE) ||
        (h->getToolType() == TOOL_ERASER && h->getEraserType() == ERASER_TYPE_WHITEOUT)) {
        if (this->inputHandler) {
            // Upstream workaround for https://github.com/xournalpp/xournalpp/issues/4377
            g_warning("InputHandler already exists upon CanvasPage::onButtonPressEvent. Deleting it (and its views)");
            eraseViewsOf(this->overlayViews, this->inputHandler.get());
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
            default:  // freehand (with the shape recognizer if that drawing type is set)
                this->inputHandler = std::make_unique<StrokeHandler>(&control, getPage());
        }
        this->inputHandler->onButtonPressEvent(pos, zoom);
        this->overlayViews.emplace_back(this->inputHandler->createView(this));
    } else if (h->getToolType() == TOOL_ERASER) {
        this->eraser->erase(x, y);
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
        }
    } else if (h->getToolType() == TOOL_TEXT) {
        view.startText(*this, x, y);
    } else if (h->getToolType() == TOOL_SELECT_OBJECT) {
        const bool aggregate = pos.isShiftDown() && view.getSelection();
        selectObjectAt(x, y, false, aggregate);
    }
    return true;
}

bool CanvasPage::selectObjectAt(double x, double y, bool multiLayer, bool aggregate) {
    // Port of SelectObject::at / atAggregate (gui/PageViewFindObjectHelper.h)
    DocumentSession& ctrl = view.getSession();
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
    {
        std::shared_lock lock(*ctrl.getDocument());
        if (multiLayer && !aggregate) {
            const auto& layers = page->getLayers();
            size_t layerNo = layers.size();
            for (auto l = layers.rbegin(); l != layers.rend(); l++, layerNo--) {
                if (checkLayer(*l)) {
                    lock.unlock();
                    ctrl.getLayerController()->switchToLay(as_unsigned(std::distance(l, layers.rend())));
                    break;
                }
            }
        } else {
            checkLayer(page->getSelectedLayer());
        }
    }
    if (!match) {
        return false;
    }
    if (aggregate && previous) {
        auto sel = SelectionFactory::addElementFromActiveLayer(&ctrl, previous, match, matchIndex);
        view.setSelection(sel.release());
    } else {
        auto sel = SelectionFactory::createFromElementOnActiveLayer(&ctrl, page, this, match, matchIndex);
        view.setSelection(sel.release());
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

xoj::util::Point<int> CanvasPage::getPixelPosition() const {
    // Content pixels (upstream: the page's position in the layout, independent of scrolling).
    if (auto idx = view.indexOf(this)) {
        const QRectF r = view.documentLayout().pageRect(*idx, view.getViewController().zoom());
        return {static_cast<int>(std::lround(r.x())), static_cast<int>(std::lround(r.y()))};
    }
    return {0, 0};
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
    } else if (this->selector) {
        this->selector->currentPos(x, y);
    } else if (TextEditor* editor = view.getTextEditor(); editor && &editor->getPage() == this &&
                                                            h->getToolType() == TOOL_TEXT && currentSequenceDeviceId) {
        editor->mouseMoved(x, y);  // drag: select text
    } else if (h->getToolType() == TOOL_ERASER && h->getEraserType() != ERASER_TYPE_WHITEOUT && this->inEraser) {
        this->eraser->erase(x, y);
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
    if (this->inputHandler) {
        this->inputHandler->onButtonReleaseEvent(pos, getZoom());
        this->inputHandler.reset();
    }
    if (this->inEraser) {
        this->inEraser = false;
        Document* doc = control.getDocument();
        doc->lock();
        this->eraser->finalize();
        doc->unlock();
    }
    if (this->selector) {
        // Port of XojPageView::onButtonReleaseEvent (selector part)
        const bool aggregate = pos.isShiftDown() && view.getSelection();
        const size_t layerOfFinalizedSel = this->selector->finalize(this->page, aggregate, control.getDocument());
        if (layerOfFinalizedSel) {
            if (aggregate) {
                auto sel = selector->releaseElements();
                view.setSelection(
                        SelectionFactory::addElementsFromActiveLayer(&control, view.getSelection(), sel).release());
            } else {
                // with a multi-layer selector the objects might be on another layer
                control.getLayerController()->switchToLay(layerOfFinalizedSel);
                view.setSelection(SelectionFactory::createFromElementsOnActiveLayer(&control, page, this,
                                                                                    selector->releaseElements())
                                          .release());
            }
        } else if (const double zoom = getZoom(); selector->userTapped(zoom)) {
            selectObjectAt(pos.x / zoom, pos.y / zoom, this->selector->isMultiLayerSelection(), aggregate);
        }
        this->selector.reset();
    }
    return false;
}

void CanvasPage::onSequenceCancelEvent(DeviceId deviceId) {
    if (currentSequenceDeviceId != deviceId) {
        return;
    }
    currentSequenceDeviceId.reset();
    if (this->inputHandler) {
        this->inputHandler->onSequenceCancelEvent();
        this->inputHandler.reset();
    }
    if (this->inEraser) {
        // xournal-qt: keep what was erased so far (the erase is undoable), like a release.
        this->inEraser = false;
        Document* doc = view.getSession().getDocument();
        doc->lock();
        this->eraser->finalize();
        doc->unlock();
    }
    this->selector.reset();  // (its view goes with it)
}

// --- display -----------------------------------------------------------------------------------------------------

auto CanvasPage::bufferInfo() -> BufferInfo {
    return raster->withBuffer([](xoj::view::Mask& buffer) {
        BufferInfo info;
        if (!buffer.isInitialized()) {
            return info;
        }
        cairo_surface_t* s = cairo_get_target(buffer.get());
        info.valid = true;
        info.zoom = buffer.getZoom();
        cairo_surface_get_device_scale(s, &info.dpiScale, &info.dpiScale);
        info.pixelSize = QSize(cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
        return info;
    });
}

QImage CanvasPage::composeTile(const QRect& pixelRect) {
    QImage img(pixelRect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    raster->withBuffer([&](xoj::view::Mask& buffer) {
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
        cairo_translate(cr, -pixelRect.x() / dpiScale, -pixelRect.y() / dpiScale);
        cairo_scale(cr, bufferZoom, bufferZoom);  // page coordinates
        buffer.paintTo(cr);
        // Upstream XojPageView::paintPage: the overlays draw in page coordinates on top of the buffer.
        for (const auto& v: this->overlayViews) {
            v->draw(cr);
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
        for (const Range& r: dirtyRanges) {
            if (!r.isValid() || r.empty()) {
                continue;
            }
            QRect px(QPoint(static_cast<int>(std::floor(r.minX * s)) - 1, static_cast<int>(std::floor(r.minY * s)) - 1),
                     QPoint(static_cast<int>(std::ceil(r.maxX * s)) + 1, static_cast<int>(std::ceil(r.maxY * s)) + 1));
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
    if (v->isViewOf(this->inputHandler.get())) {
        // Draw the inputHandler's view onto the page buffer (upstream: no re-render, no flicker).
        const bool drawn = raster->withBuffer([&](xoj::view::Mask& buffer) {
            if (auto* cr = buffer.get(); cr) {
                v->drawWithoutDrawingAids(cr);
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

void CanvasPage::rerenderPage(bool sizeChanged) { raster->rerenderPage(sizeChanged); }

void CanvasPage::rerenderRect(double x, double y, double width, double height) {
    raster->rerenderRect(x, y, width, height);
}

GdkRGBA CanvasPage::getSelectionColor() {
    return Util::rgb_to_GdkRGBA(view.getSession().getSettings()->getSelectionColor());
}

void CanvasPage::deleteViewBuffer() { raster->releaseBuffer(); }

// --- PageListener (port of XojPageView) ----------------------------------------------------------------------------

void CanvasPage::rectChanged(Rectangle<double>& rect) { rerenderRect(rect.x, rect.y, rect.width, rect.height); }

void CanvasPage::rangeChanged(Range& range) { rerenderRange(range); }

void CanvasPage::pageChanged() { rerenderPage(); }

void CanvasPage::elementChanged(const Element* elem) {
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
    if (!range.empty()) {
        rerenderRange(range);
    }
}

}  // namespace xqt
