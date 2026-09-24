/*
 * xournal-qt: zoom and scroll state of a document view, with anchored pinch zoom and iOS-like momentum.
 *
 * Replaces upstream's GtkScrolledWindow/GtkAdjustment + ZoomControl combination. As in upstream ZoomControl's zoom
 * sequences, the document point under the fingers (or the cursor) stays fixed while zooming; since the layout has
 * fixed pixel paddings, the anchor is kept as (page, point on page) rather than as a content coordinate.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <cstddef>

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTimer>

#include "DocumentLayout.h"

namespace xqt {

class ViewController: public QObject {
    Q_OBJECT
public:
    explicit ViewController(const DocumentLayout* layout, QObject* parent = nullptr);

    /// Logical pixels per page point.
    double zoom() const { return z; }
    /// Zoom that shows pages at their physical size ("100 %"), from the screen's logical DPI (upstream zoom100Value).
    double zoom100() const { return z100; }
    void setZoom100(double value) { z100 = value; }
    double minZoom() const { return 0.3 * z100; }  // upstream DEFAULT_ZOOM_MIN
    double maxZoom() const { return 7.0 * z100; }  // upstream DEFAULT_ZOOM_MAX

    QSizeF viewSize() const { return view; }
    void setViewSize(QSizeF size);
    /// Whether the last change was a jump (to a page, a fit, a new size) and not plain scrolling or zooming: those
    /// are looked at right away, the continuous ones only every few milliseconds (CanvasView::viewChanged).
    bool takeJumped() {
        const bool was = jumped;
        jumped = false;
        return was;
    }

    /// The page the view was last sent to (scrollToPage, scrollToPageRect), once: it becomes the current page while
    /// it can be seen, even if another one shows more of itself.
    std::optional<size_t> takePageJump() {
        auto page = pageJump;
        pageJump.reset();
        return page;
    }

    /// View position of the content origin (includes the centering of content smaller than the view).
    QPointF contentOrigin() const;
    QPointF contentToView(QPointF c) const { return c + contentOrigin(); }
    QPointF viewToContent(QPointF v) const { return v - contentOrigin(); }
    QRectF visibleContentRect() const { return QRectF(viewToContent(QPointF(0, 0)), view); }

    /// Content coordinate shown at the view's top-left corner (0 on an axis where the content is smaller).
    QPointF scrollPosition() const;
    /// Scroll so that the content coordinate `pos` is at the view's top-left corner (clamped).
    void setScrollPosition(QPointF pos);

    void setZoom(double zoom, QPointF viewAnchor);
    void zoomBy(double factor, QPointF viewAnchor) { setZoom(z * factor, viewAnchor); }
    /// Zoom so that the page (its row, with several columns) fills the width of the view, and centre it; without a
    /// page: the one in the middle of the view. Not kept: the zoom stays when another page comes into view.
    void fitWidth(std::optional<size_t> page = std::nullopt);
    /// The zoom at which `page` (its row) fills the width of the view (upstream's fit-to-width for that page).
    double fitWidthZoom(size_t page) const { return layout->fitWidthZoom(view.width(), page); }
    /// The page in the middle of the view (the closest one).
    size_t pageInView() const;
    /// Zoom so that the page fills the height of the view, or the whole page fits (and scroll to it).
    void fitPage(size_t page, bool wholePage);
    /// Zoom so that a part of a page (a column of text, say) fills the view, and show it.
    void zoomToPageRect(size_t page, QRectF rectPt);
    void panBy(QPointF deltaView);
    void scrollToPage(size_t page);
    /// Make a rectangle of a page (in page points) visible, centred if it has to scroll (e.g. a search hit).
    void scrollToPageRect(size_t page, QRectF rectPt);

    void pinchBegin(QPointF centroid, double distance);
    void pinchUpdate(QPointF centroid, double distance);
    /// The fingers are lifted: the zoom is stable now (zoomSettled right away, not after the delay).
    void pinchEnd();
    /// A zoom gesture ended (a pinch, a touchpad pinch): the zoom is stable now.
    void zoomGestureEnded();

    void fling(QPointF velocityPxPerMs);
    void stopMomentum();

    /// The layout changed (pages inserted/deleted/resized): keep the view valid.
    void layoutChanged();

Q_SIGNALS:
    void changed();
    /// Emitted after every zoom change (the render service defers re-renders for a while).
    void zoomChanged();
    /// ~300 ms after the last zoom change (upstream: Scheduler::blockRerenderZoom), or when a zoom gesture ended.
    void zoomSettled();

private:
    bool jumped = false;  ///< the last change went somewhere (not plain scrolling or zooming)
    std::optional<size_t> pageJump;  ///< the last change went to this page
    struct Anchor {
        size_t page = 0;
        QPointF pagePoint;  ///< in points, may lie outside the page
    };
    Anchor anchorAt(QPointF viewPos) const;
    void placeAnchor(const Anchor& a, QPointF viewPos);
    void clamp();
    void stepMomentum();

    const DocumentLayout* layout;
    double z = 1.0;
    double z100 = 96.0 / 72.0;
    QPointF scrollPos;  ///< content coordinate of the view's top-left corner (when content is larger than the view)
    QSizeF view;
    bool initialized = false;
    std::optional<size_t> pendingPage;

    Anchor pinchAnchor;
    double pinchStartDistance = 1.0;
    double pinchStartZoom = 1.0;

    QTimer momentumTimer;
    QElapsedTimer momentumClock;
    qint64 lastMomentumMs = 0;
    QPointF velocity;
    QTimer settleTimer;
};

}  // namespace xqt
