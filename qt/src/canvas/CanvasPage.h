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
#include <QString>

#include "gui/LegacyRedrawable.h"
#include "gui/PageView.h"
#include "gui/inputdevices/DeviceId.h"
#include "model/PageListener.h"
#include "model/Layer.h"
#include "model/PageRef.h"
#include "render/PageRaster.h"
#include "util/Range.h"
#include "util/Rectangle.h"
#include "view/Repaintable.h"

class EraseHandler;
class OverlayBase;
class Selector;
class InputHandler;
class PositionInputData;

namespace xoj::view {
class OverlayView;
class ToolView;
}  // namespace xoj::view

namespace xqt {

class CanvasView;

/// A link on a page where it is drawn (page coordinates): a PDF link, a web address in a text, a link of a Markdown
/// text or a link marker. CanvasView::hoverLinkAt finds them.
struct LinkSpot {
    QRectF rect;
    QString uri;       ///< the target (a wiki link as "[[name]]"); empty: a page of this document
    int page = -1;     ///< a page of this document (0-based; "#Page:N", a web address's page), else -1
    int pdfPage = -1;  ///< a PDF link to a page: the PDF page (0-based), resolved to a page when used
};

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
        /// A big page drawn in part (PageRaster::Placement): the buffer's top left on the page in logical pixels of
        /// its zoom (whole pages: 0, 0), and the part of the page it shows (page coordinates)
        bool whole = true;
        QPoint origin;
        QRectF area;
    };
    BufferInfo bufferInfo();
    /// Buffer + overlay views for a rectangle of buffer pixels (from the buffer's top left, BufferInfo::origin; call
    /// on the UI thread / during scene graph sync).
    QImage composeTile(const QRect& pixelRect);
    /// Dirty areas since the last call, in buffer pixels of the given buffer geometry. `all` = everything.
    std::vector<QRect> takeDirty(const BufferInfo& info, bool& all);
    bool hasOverlays() const { return !overlayViews.empty(); }
    void addOverlayView(std::unique_ptr<xoj::view::OverlayView> v);
    void removeOverlayViewsOf(const OverlayBase* o);

    /// The links on this page, found once and kept until the page changes (any change of it forgets them): the mouse
    /// looks them up on every move (CanvasView::hoverLinkAt). Null: not looked for since the last change.
    const std::vector<LinkSpot>* linkSpots() const { return links ? &*links : nullptr; }
    void setLinkSpots(std::vector<LinkSpot> spots) { links = std::move(spots); }

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

    // --- sticky notes (qt/docs/sticky-notes.md) ---
    /// A press with a tool that writes: onto the note there, if any (its layer is selected until the release). True
    /// when the press is done with (on a covering note: nothing is written, a tap lets it peek).
    bool pressOnNote(double x, double y);
    /// The page's own layer is selected again after writing on a note
    void leaveNote();
    /// The eraser stays inside the note it erases on (never on the paper's edge). False: the note is too small.
    bool eraserInNote(double& x, double& y) const;
    std::optional<Layer::Index> layerBeforeNote;  ///< the page's selected layer while writing on a note
    std::optional<xoj::util::Rectangle<double>> noteClip;  ///< the note written on: the stroke is drawn clipped to it
    std::optional<std::pair<double, double>> coverPress;  ///< a press on a covering note (a tap: it peeks)

    std::optional<std::vector<LinkSpot>> links;  ///< linkSpots()

    mutable std::vector<Range> dirtyRanges;  ///< page coordinates
    mutable bool allDirty = true;
};

}  // namespace xqt
