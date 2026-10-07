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

#include <array>
#include <cmath>
#include <optional>

#include <cstddef>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include "Clock.h"
#include "DocumentLayout.h"

namespace xqt {

class ViewController: public QObject {
    Q_OBJECT
public:
    explicit ViewController(const DocumentLayout* layout, QObject* parent = nullptr);
    /// The clock of the momentum, the animations and the zoom settling (Clock.h; the steady clock by default). Stops
    /// what runs.
    void setClock(Clock& clock);

    /// Logical pixels per page point.
    double zoom() const { return z; }
    /// Zoom that shows pages at their physical size ("100 %"), from the screen's calibration (ScreenCalibration.h;
    /// upstream zoom100Value).
    double zoom100() const { return z100; }
    /// A new 100 % (another screen, a calibration): the pages stay as large as they are shown, only what is called
    /// 100 % changes (the percentage shown, zoom steps, the zoom range).
    void setZoom100(double value);
    /// The smallest zoom: the setting "Smallest zoom" (smallestZoom(), 20 % by default; upstream's DEFAULT_ZOOM_MIN
    /// was 30 %), or less when the biggest page needs it to be seen whole (an A0 poster), or the largest group of
    /// pages (two A4 pages side by side on an unfolded phone: DocumentLayout::wholeGroupZoom)
    double minZoom() const;
    /// The setting "Smallest zoom" as a part of 100 % (0.2: 20 %), within SMALLEST_ZOOM_MIN..MAX percent
    double smallestZoom() const { return smallest; }
    /// A new smallest zoom (the setting changed): a zoom below it comes up to it (the middle of the view stays)
    void setSmallestZoom(double fractionOf100);
    static constexpr int SMALLEST_ZOOM_DEFAULT = 20;  ///< percent
    static constexpr int SMALLEST_ZOOM_MIN = 5;
    static constexpr int SMALLEST_ZOOM_MAX = 50;
    double maxZoom() const { return 7.0 * z100; }  // upstream DEFAULT_ZOOM_MAX

    /// The size of the (upright) view the layout is seen through: the screen's (the canvas item's), or while the canvas
    /// is turned the bounding box of the turned screen (qt/docs/features/canvas-rotation.md). Everything of the view
    /// (scroll position, anchors, pinch, fits) works in this upright view; only input and what is shown over the canvas
    /// are mapped between it and the screen (screenToView, viewToScreen).
    QSizeF viewSize() const { return view; }
    /// The size of the canvas on the screen (item pixels)
    QSizeF screenSize() const { return screen; }
    void setViewSize(QSizeF screenSize);

    // --- the canvas turned (qt/docs/features/canvas-rotation.md) -----------------------------------------------------
    /// Degrees clockwise the canvas is turned on the screen, in [0, 360)
    double rotation() const { return angle; }
    bool rotated() const { return angle != 0; }
    /// Turned by a multiple of 90° (also not at all): the screen's pixels are the view's pixels
    bool rightAngled() const { return std::fmod(angle, 90.0) == 0; }
    /// Turn the canvas; the document point under `screenAnchor` (screen coordinates; none: the middle of the screen)
    /// stays where it is on the screen
    void setRotation(double degrees, std::optional<QPointF> screenAnchor = std::nullopt);
    /// Turn it by a step (Ctrl+[ / Ctrl+]: 90°): from a free angle to the next multiple of the step that way
    void rotateBy(double degrees, std::optional<QPointF> screenAnchor = std::nullopt);
    /// The nearest multiple of 90° when `degrees` is within SNAP_DEGREES of it, else `degrees` (normalised)
    static double snapAngle(double degrees);
    /// A turn of the fingers (or the touchpad's) smaller than this is not a rotation: pinching and scrolling do not
    /// turn the canvas by accident
    static constexpr double ROTATE_START_DEGREES = 12;
    static constexpr double SNAP_DEGREES = 6;
    /// Screen (canvas item) coordinates to the upright view and back; deltas (a wheel, a drag) only turn
    QPointF screenToView(QPointF p) const {
        if (angle == 0) {
            return p;  // (exactly: an upright canvas is not touched by rounding)
        }
        const QPointF d = p - screenCentre;
        return QPointF(d.x() * cosA + d.y() * sinA, -d.x() * sinA + d.y() * cosA) + viewCentre;
    }
    QPointF viewToScreen(QPointF p) const {
        if (angle == 0) {
            return p;
        }
        const QPointF d = p - viewCentre;
        return QPointF(d.x() * cosA - d.y() * sinA, d.x() * sinA + d.y() * cosA) + screenCentre;
    }
    QPointF screenDeltaToView(QPointF d) const { return QPointF(d.x() * cosA + d.y() * sinA, -d.x() * sinA + d.y() * cosA); }
    QPointF viewDeltaToScreen(QPointF d) const { return QPointF(d.x() * cosA - d.y() * sinA, d.x() * sinA + d.y() * cosA); }
    /// The bounding box on the screen of a rectangle of the view (itself when the canvas is upright)
    QRectF viewToScreen(const QRectF& r) const;
    /// A rectangle that stands for two points (its top left and bottom right: the ends of a PDF text selection) on the
    /// screen: the two points mapped (its width or height may become negative)
    QRectF viewToScreenEnds(const QRectF& r) const {
        return angle == 0 || r.isNull() ? r : QRectF(viewToScreen(r.topLeft()), viewToScreen(r.bottomRight()));
    }
    /// The corners of the screen in the view (top left, top right, bottom right, bottom left): what of the view's
    /// bounding box is shown
    std::array<QPointF, 4> screenInView() const;
    /// The cosine and sine of the angle (exact at multiples of 90°)
    double rotationCos() const { return cosA; }
    double rotationSin() const { return sinA; }

    /// A turn of two fingers began (they are at this angle, degrees) / they turned to this angle: the canvas turns
    /// with them once the turn exceeds ROTATE_START_DEGREES, snapping to multiples of 90° (SNAP_DEGREES). Used by
    /// pinchBegin / pinchUpdate and the touchpad's rotate gesture (twistBy).
    void twistBegin(double fingerDegrees);
    void twistTo(double fingerDegrees, QPointF screenAnchor);
    /// The touchpad turned by this many degrees (clockwise) about a screen point
    void twistBy(double deltaDegrees, QPointF screenAnchor);
    /// The fingers left (the touchpad's gesture ended)
    void twistEnd() { twistActive = false; }
    /// The canvas turns with the fingers (tests)
    bool twisting() const { return twistActive && twistEngaged; }
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

    // --- a place in the document, for keeping two views together (ScrollLock, qt/docs/features/reference-view.md) ----
    /// A page and a point on it relative to its size (0..1 across the page; outside it in the gap around it)
    struct Place {
        size_t page = 0;
        QPointF relative;
    };
    /// The place under a point of the view (none: no pages): on the page nearest to it, or on `page`
    std::optional<Place> placeAt(QPointF viewPos, std::optional<size_t> page = std::nullopt) const;
    /// Scroll so that a place (its page clamped to the pages there are) is under a point of the view. Stops momentum
    /// and an animation; nothing when the view has no size yet.
    void showPlace(const Place& place, QPointF viewPos);
    size_t pageCount() const { return layout->pageCount(); }
    /// The pages go sideways (the layout's horizontal mode)
    bool horizontal() const { return layout->horizontal(); }
    /// Make a rectangle of a page (in page points) visible, centred if it has to scroll (e.g. a search hit).
    void scrollToPageRect(size_t page, QRectF rectPt);

    /// Two fingers: `centroid` in screen coordinates (the canvas item's; the view's while it is upright). With
    /// `fingerDegrees` (the angle of the line between the fingers) the canvas also turns with them (twistBegin).
    void pinchBegin(QPointF centroid, double distance, std::optional<double> fingerDegrees = std::nullopt);
    void pinchUpdate(QPointF centroid, double distance, std::optional<double> fingerDegrees = std::nullopt);
    /// The fingers are lifted: the zoom is stable now (zoomSettled right away, not after the delay).
    void pinchEnd();
    /// A zoom gesture ended (a pinch, a touchpad pinch): the zoom is stable now.
    void zoomGestureEnded();

    void fling(QPointF velocityPxPerMs);
    void stopMomentum();

    // --- scrolling sideways (the layout's horizontal mode) and presenting -------------------------------------
    /// A fit that is kept when the view changes size: sideways the rows fill the height, presenting the page
    /// fills the screen, the audience's view of the presenter view a part of a page (Rect). Zooming by hand ends it.
    enum class Fit { None, Height, Page, Rect };
    Fit keptFit() const { return kept; }
    /// Zoom so that all rows fill the height of the view (sideways: "fit to the window height"), and keep it.
    void fitHeight();
    /// Presenting: zoom so that the page fills the screen (as much as its shape allows), show it, and keep that.
    void fitPresentedPage(size_t page);
    /// Presenting: the zoom at which the page fills the view (as much as its shape allows)
    double presentedZoom(size_t page) const;
    /// The audience's screen of the presenter view (qt/docs/features/presenter-view.md): zoom so that a part of a page
    /// (the slide without its space for notes) fills the view as far as its shape allows, centre it, and keep that
    /// (also when the view changes size or the pages move).
    void fitPageRect(size_t page, QRectF rectPt);
    /// What a new view or layout starts with: sideways the height, else the width of the page.
    void fitDefault(std::optional<size_t> page = std::nullopt);
    /// Scrolling sideways, come to rest on whole pages (their group: a column, a pair) after a drag, a fling or a
    /// wheel. `maxStep`: a swipe goes at most this many pages on (0: as far as it flies).
    void setSnapping(bool snap, int maxStep = 0);
    /// Up and down too (reading, qt/docs/features/toolbox.md): a drag or a fling comes to rest on a row of pages: its
    /// top at the view's top, or within it when it is taller than the view; a row that fits rests in the middle
    void setSnappingVertically(bool on) { snapVertical = on; }
    bool snapping() const { return snap && (layout->horizontal() || snapVertical); }
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
    /// The canvas was turned (rotation())
    void rotationChanged();
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
    /// Up and down: the scroll positions a row of pages rests at, and the row closest to a position
    std::pair<double, double> restRangeY(size_t group) const;
    size_t groupNearY(double y) const;
    /// endScroll up and down (snapping vertically)
    void endScrollVertically(QPointF velocity);
    /// The scroll positions the view can take sideways (the first and the last page can rest in the middle)
    std::pair<double, double> scrollRangeX() const;
    /// Where a group rests up or down: presenting in the middle, else where the view is
    double restY(size_t group) const;
    /// The group whose resting place is closest to a scroll position
    size_t groupNear(double scrollX) const;

    /// Show a group at its resting place right away
    void placeGroup(size_t group);

    Fit kept = Fit::None;
    /// Fit::Rect: the page and its part (points)
    size_t keptPage = 0;
    QRectF keptRect;
    /// Fit::Rect: zoom and scroll to it (no signals)
    void placeKeptRect();
    bool snap = false;
    bool snapVertical = false;
    int snapMaxStep = 0;
    bool animating = false;
    QPointF animFrom, animTo, animVelocity;
    double animDuration = 0;
    std::optional<size_t> animGroup;  ///< the group the animation goes to

    const DocumentLayout* layout;
    double z = 1.0;
    double z100 = 96.0 / 72.0;
    double smallest = SMALLEST_ZOOM_DEFAULT / 100.0;
    QPointF scrollPos;  ///< content coordinate of the view's top-left corner (when content is larger than the view)
    QSizeF view;
    QSizeF screen;
    double angle = 0;  ///< degrees clockwise, [0, 360)
    double cosA = 1, sinA = 0;
    QPointF screenCentre, viewCentre;
    /// The angle without moving anything (view size, centres); the caller keeps an anchor
    void applyAngle(double degrees);
    double twistStartAngle = 0;   ///< the canvas's angle when the twist began
    double twistStartFinger = 0;  ///< the fingers' angle then
    double twistOffset = 0;       ///< the part of the turn before it engaged (the canvas lags behind by it)
    double twistTotal = 0;        ///< the turn so far (degrees, unwrapped: more than half a turn is possible)
    double twistLastFinger = 0;   ///< the fingers' angle at the last update
    /// The angle the canvas takes for a turn of the fingers so far (none: not engaged yet)
    std::optional<double> twistTarget();
    bool twistEngaged = false;
    bool twistActive = false;
    bool initialized = false;
    std::optional<size_t> pendingPage;

    Anchor pinchAnchor;
    double pinchStartDistance = 1.0;
    double pinchStartZoom = 1.0;

    Clock* clock = &Clock::steady();
    ClockTimer momentumTimer;
    double momentumStartMs = 0;  ///< when the momentum or the step of the animation began (clock)
    /// Whole milliseconds since then
    qint64 momentumElapsed() const { return static_cast<qint64>(clock->nowMs() - momentumStartMs); }
    qint64 lastMomentumMs = 0;
    QPointF velocity;
    ClockTimer settleTimer;
};

}  // namespace xqt
