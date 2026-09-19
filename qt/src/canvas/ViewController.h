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
    void fitWidth();
    void panBy(QPointF deltaView);
    void scrollToPage(size_t page);
    /// Make a rectangle of a page (in page points) visible, centred if it has to scroll (e.g. a search hit).
    void scrollToPageRect(size_t page, QRectF rectPt);

    void pinchBegin(QPointF centroid, double distance);
    void pinchUpdate(QPointF centroid, double distance);
    void pinchEnd();

    void fling(QPointF velocityPxPerMs);
    void stopMomentum();

    /// The layout changed (pages inserted/deleted/resized): keep the view valid.
    void layoutChanged();

Q_SIGNALS:
    void changed();
    /// Emitted after every zoom change (the render service defers re-renders for a while).
    void zoomChanged();
    /// ~300 ms after the last zoom change (upstream: Scheduler::blockRerenderZoom).
    void zoomSettled();

private:
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
