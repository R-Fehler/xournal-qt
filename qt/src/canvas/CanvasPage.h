/*
 * xournal-qt: one page as shown by a document view. Replaces upstream's GTK XojPageView (gui/PageView.cpp).
 *
 * - Implements upstream's toolkit-neutral view interfaces (xoj::view::Repaintable, LegacyRedrawable, PageListener),
 *   so upstream input handlers (StrokeHandler, EraseHandler) and their overlay views work unmodified.
 * - The input dispatch (onButtonPressEvent / onMotionNotifyEvent / onButtonReleaseEvent / onSequenceCancelEvent)
 *   is a port of XojPageView for the tools supported so far (pen, highlighter, eraser incl. whiteout).
 * - Rendering: a PageRaster (CPU buffer, see render/PageRaster.h) plus overlay views, composited per tile for display.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QImage>
#include <QRect>
#include <QRectF>

#include "gui/LegacyRedrawable.h"
#include "gui/PageView.h"
#include "gui/inputdevices/DeviceId.h"
#include "model/PageListener.h"
#include "model/PageRef.h"
#include "render/PageRaster.h"
#include "util/Range.h"
#include "view/Repaintable.h"

class EraseHandler;
class Selector;
class InputHandler;
class PositionInputData;

namespace xoj::view {
class OverlayView;
class ToolView;
}  // namespace xoj::view

namespace xqt {

class CanvasView;

class CanvasPage final: public XojPageView {
public:
    CanvasPage(CanvasView& view, PageRef page);
    ~CanvasPage() override;

    // --- XojPageView (shadow; for the selection) --------------------------------------------------------------
    const PageRef getPage() const override { return page; }
    XournalView* getXournal() const override;
    xoj::util::Point<int> getPixelPosition() const override;

    PageRaster& getRaster() const { return *raster; }
    /// Page rectangle in view coordinates at the current zoom.
    QRectF viewRect() const;

    // --- input: port of XojPageView -------------------------------------------------------------------------
    bool onButtonPressEvent(const PositionInputData& pos);
    bool onMotionNotifyEvent(const PositionInputData& pos);
    bool onButtonReleaseEvent(const PositionInputData& pos);
    void onSequenceCancelEvent(DeviceId deviceId);

    // --- display ----------------------------------------------------------------------------------------------
    struct BufferInfo {
        bool valid = false;
        double zoom = 1.0;      ///< zoom the buffer was rendered at
        double dpiScale = 1.0;  ///< device pixels per logical pixel of the buffer
        QSize pixelSize;
    };
    BufferInfo bufferInfo();
    /// Buffer + overlay views for a rectangle of buffer pixels (call on the UI thread / during scene graph sync).
    QImage composeTile(const QRect& pixelRect);
    /// Dirty areas since the last call, in buffer pixels of the given buffer geometry. `all` = everything.
    std::vector<QRect> takeDirty(const BufferInfo& info, bool& all);
    bool hasOverlays() const { return !overlayViews.empty(); }

    /// Called by the CanvasView (UI thread) when the raster finished rendering.
    void rasterUpdated(std::optional<xoj::util::Rectangle<double>> area);

    // --- xoj::view::Repaintable -------------------------------------------------------------------------------
    Range getVisiblePart() const override;
    double getZoom() const override;
    ZoomControl* getZoomControl() const override;
    double getWidth() const override;
    double getHeight() const override;
    xoj::util::Point<double> toWidgetCoordinates(const xoj::util::Point<double>& p) const override;
    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override;
    void flagDirtyRegion(const Range& rg) const override;
    void drawAndDeleteToolView(xoj::view::ToolView* v, const Range& rg) override;
    void deleteOverlayView(xoj::view::OverlayView* v, const Range& rg) override;

    // --- LegacyRedrawable -------------------------------------------------------------------------------------
    void repaintArea(double x1, double y1, double x2, double y2) const override;
    void repaintPage() const override;
    void rerenderPage(bool sizeChanged = false) override;
    void rerenderRect(double x, double y, double width, double height) override;
    GdkRGBA getSelectionColor() override;
    void deleteViewBuffer() override;

    // --- PageListener -----------------------------------------------------------------------------------------
    void rectChanged(xoj::util::Rectangle<double>& rect) override;
    void rangeChanged(Range& range) override;
    void pageChanged() override;
    void elementChanged(const Element* elem) override;
    void elementsChanged(const std::vector<const Element*>& elements, const Range& range) override;

private:
    CanvasView& view;
    PageRef page;
    std::shared_ptr<PageRaster> raster;

    std::vector<std::unique_ptr<xoj::view::OverlayView>> overlayViews;
    std::unique_ptr<InputHandler> inputHandler;
    std::unique_ptr<EraseHandler> eraser;
    bool inEraser = false;
    std::unique_ptr<Selector> selector;  ///< rectangle / lasso being drawn (select tools)
    /// Select the element under a tap (port of upstream's SelectObject). `aggregate`: add to the selection.
    bool selectObjectAt(double x, double y, bool multiLayer, bool aggregate);
    DeviceId currentSequenceDeviceId;

    mutable std::vector<Range> dirtyRanges;  ///< page coordinates
    mutable bool allDirty = true;
};

}  // namespace xqt
