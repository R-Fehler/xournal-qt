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
    /// Zoom that shows pages at their physical size ("100 %"), from the screen's calibration (ScreenCalibration.h;
    /// upstream zoom100Value).
    double zoom100() const { return z100; }
    /// A new 100 % (another screen, a calibration): the pages stay as large as they are shown, only what is called
    /// 100 % changes (the percentage shown, zoom steps, the zoom range).
    void setZoom100(double value);
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

    // --- scrolling sideways (the layout's horizontal mode) and presenting -------------------------------------
    /// A fit that is kept when the view changes size: sideways the rows fill the height, presenting the page
    /// fills the screen. Zooming by hand ends it.
    enum class Fit { None, Height, Page };
    Fit keptFit() const { return kept; }
    /// Zoom so that all rows fill the height of the view (sideways: "fit to the window height"), and keep it.
    void fitHeight();
    /// Presenting: zoom so that the page fills the screen (as much as its shape allows), show it, and keep that.
    void fitPresentedPage(size_t page);
    /// What a new view or layout starts with: sideways the height, else the width of the page.
    void fitDefault(std::optional<size_t> page = std::nullopt);
    /// Scrolling sideways, come to rest on whole pages (their group: a column, a pair) after a drag, a fling or a
    /// wheel. `maxStep`: a swipe goes at most this many pages on (0: as far as it flies).
    void setSnapping(bool snap, int maxStep = 0);
    bool snapping() const { return snap && layout->horizontal(); }
    /// A scroll delta (wheel, touchpad): sideways, what cannot scroll up or down scrolls left or right.
    QPointF scrollDelta(QPointF delta) const;
    /// A drag (finger, hand tool) or a touchpad scroll ended with this velocity (content px/ms): momentum, or when
    /// snapping, on to the page it comes to rest on (animated, the momentum carried along).
    void endScroll(QPointF velocity);
    /// Sideways: the previous / next group of pages (animated; several steps in a row add up). False otherwise.
    bool stepPages(int delta);
    /// The group of the current page fits into the view: a wheel notch goes a page on when snapping.
    bool groupFitsView() const;
    /// Scrolling to rest on a page (tests)
    bool isAnimating() const { return animating; }
    /// The group the view is at or on its way to (sideways)
    size_t currentGroup() const;
    /// Sideways: the scroll position a group rests at (the lowest and highest; the same when it fits the view).
    std::pair<double, double> restRange(size_t group) const;

    /// The layout changed (pages inserted/deleted/resized): keep the view valid.
    void layoutChanged();

Q_SIGNALS:
    void changed();
    /// Emitted after every zoom change (the render service defers re-renders for a while).
    void zoomChanged();
    /// What is 100 % changed (the zoom did not)
    void zoom100Changed();
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
    void stepAnimation();
    /// Move to a scroll position in a short ease-out, starting with the velocity (scroll px/ms) it had
    void animateTo(QPointF target, QPointF startVelocity = {});
    std::pair<double, double> restRangeUnclamped(size_t group) const;
    /// The scroll positions the view can take sideways (the first and the last page can rest in the middle)
    std::pair<double, double> scrollRangeX() const;
    /// Where a group rests up or down: presenting in the middle, else where the view is
    double restY(size_t group) const;
    /// The group whose resting place is closest to a scroll position
    size_t groupNear(double scrollX) const;
    /// Presenting: the zoom at which the page fills the view
    double presentedZoom(size_t page) const;
    /// Show a group at its resting place right away
    void placeGroup(size_t group);

    Fit kept = Fit::None;
    bool snap = false;
    int snapMaxStep = 0;
    bool animating = false;
    QPointF animFrom, animTo, animVelocity;
    double animDuration = 0;
    std::optional<size_t> animGroup;  ///< the group the animation goes to

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
