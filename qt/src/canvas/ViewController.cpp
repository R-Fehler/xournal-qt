#include "ViewController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace xqt {

namespace {
// UIScrollView "normal" deceleration rate: the velocity decays by this factor per millisecond.
constexpr double DECELERATION_PER_MS = 0.998;
constexpr double MIN_VELOCITY = 0.02;  // px/ms
}  // namespace

ViewController::ViewController(const DocumentLayout* layout, QObject* parent): QObject(parent), layout(layout) {
    momentumTimer.setTimerType(Qt::PreciseTimer);
    momentumTimer.setInterval(8);
    connect(&momentumTimer, &ClockTimer::timeout, this, &ViewController::stepMomentum);
    settleTimer.setSingleShot(true);
    settleTimer.setInterval(300);
    connect(&settleTimer, &ClockTimer::timeout, this, &ViewController::zoomSettled);
}

void ViewController::setClock(Clock& to) {
    stopMomentum();
    clock = &to;
    momentumTimer.setClock(to);
    settleTimer.setClock(to);
}

double ViewController::minZoom() const {
    double lowest = smallest * z100;
    if (view.isEmpty()) {
        return lowest;
    }
    // The biggest page can be seen whole
    if (const QSizeF page = layout->largestPage(); !page.isEmpty()) {
        const double pad = 2 * layout->padding();
        const double whole = std::min((view.width() - pad) / page.width(), (view.height() - pad) / page.height());
        if (whole > 0) {
            lowest = std::min(lowest, whole);
        }
    }
    // And a whole row of pages (two side by side), whatever the setting
    if (const double row = layout->wholeGroupZoom(view); row > 0) {
        lowest = std::min(lowest, row);
    }
    return lowest;
}

void ViewController::setSmallestZoom(double fraction) {
    fraction = std::clamp(fraction, SMALLEST_ZOOM_MIN / 100.0, SMALLEST_ZOOM_MAX / 100.0);
    if (fraction == smallest) {
        return;
    }
    smallest = fraction;
    if (initialized && !view.isEmpty() && z < minZoom()) {
        setZoom(minZoom(), viewCentre);
    }
}

void ViewController::setViewSize(QSizeF size) {
    jumped = true;
    if (size.isEmpty()) {
        return;
    }
    if (rotated() && initialized) {
        // Turned: the document point in the middle of the screen stays there
        const Anchor middle = anchorAt(viewCentre);
        screen = size;
        applyAngle(angle);
        kept = Fit::None;
        placeAnchor(middle, viewCentre);
        Q_EMIT changed();
        return;
    }
    const Anchor keep = anchorAt(QPointF(view.width() / 2, 0));
    const size_t group = initialized && layout->horizontal() ? currentGroup() : 0;
    screen = size;
    applyAngle(angle);  // (the view: the screen's size, or the bounding box of the turned screen)
    if (!initialized) {
        initialized = true;
        if (kept == Fit::Page) {  // presenting
            fitPresentedPage(std::exchange(pendingPage, std::nullopt).value_or(0));
            return;
        }
        if (kept == Fit::Rect) {  // the audience's screen
            pendingPage.reset();
            fitPageRect(keptPage, keptRect);
            return;
        }
        fitDefault(pendingPage.value_or(0));
        if (pendingPage) {  // requested before the view had a size (e.g. a restored tab)
            const size_t page = *pendingPage;
            pendingPage.reset();
            scrollToPage(page);
        }
        return;
    }
    if (kept == Fit::Rect) {
        fitPageRect(keptPage, keptRect);
        return;
    }
    if (kept != Fit::None && layout->horizontal() && layout->pageCount() > 0) {
        // Sideways the height (presenting the page) is kept: fitted again, on the same pages
        stopMomentum();
        const double fit = kept == Fit::Page ? presentedZoom(layout->groupPages(group).first)
                                             : layout->fitHeightZoom(view.height());
        if (fit > 0 && std::clamp(fit, minZoom(), maxZoom()) != z) {
            z = std::clamp(fit, minZoom(), maxZoom());
            settleTimer.start();
            Q_EMIT zoomChanged();
        }
        placeGroup(group);
        Q_EMIT changed();
        return;
    }
    placeAnchor(keep, QPointF(view.width() / 2, 0));
    Q_EMIT changed();
}

QPointF ViewController::contentOrigin() const {
    const QSizeF content = layout->contentSize(z);
    const double x = content.width() < view.width() ? (view.width() - content.width()) / 2 : -scrollPos.x();
    const double y = content.height() < view.height() ? (view.height() - content.height()) / 2 : -scrollPos.y();
    return QPointF(x, y);
}

QPointF ViewController::scrollPosition() const {
    const QSizeF content = layout->contentSize(z);
    return QPointF(content.width() < view.width() ? 0.0 : scrollPos.x(),
                   content.height() < view.height() ? 0.0 : scrollPos.y());
}

void ViewController::setScrollPosition(QPointF pos) {
    stopMomentum();
    scrollPos = pos;
    clamp();
    Q_EMIT changed();
}

void ViewController::clamp() {
    const QSizeF content = layout->contentSize(z);
    const auto [minX, maxX] = scrollRangeX();
    scrollPos.setX(std::clamp(scrollPos.x(), minX, maxX));
    scrollPos.setY(std::clamp(scrollPos.y(), 0.0, std::max(0.0, content.height() - view.height())));
}

std::pair<double, double> ViewController::scrollRangeX() const {
    const double contentWidth = layout->contentSize(z).width();
    double lo = 0, hi = std::max(0.0, contentWidth - view.width());
    if (layout->horizontal() && layout->groupCount() > 0 && contentWidth >= view.width()) {
        // Sideways, the first and the last page may rest in the middle of the view as well
        lo = std::min(lo, restRangeUnclamped(0).first);
        hi = std::max(hi, restRangeUnclamped(layout->groupCount() - 1).second);
    }
    return {lo, hi};
}

auto ViewController::anchorAt(QPointF viewPos) const -> Anchor {
    if (layout->pageCount() == 0) {
        return {};
    }
    const QPointF c = viewToContent(viewPos);
    const size_t page = layout->nearestPage(c, z);
    const QRectF r = layout->pageRect(page, z);
    return {page, (c - r.topLeft()) / z};
}

void ViewController::placeAnchor(const Anchor& a, QPointF viewPos) {
    if (layout->pageCount() == 0) {
        return;
    }
    const QRectF r = layout->pageRect(std::min(a.page, layout->pageCount() - 1), z);
    const QPointF content = r.topLeft() + a.pagePoint * z;
    scrollPos = content - viewPos;
    clamp();
}

auto ViewController::placeAt(QPointF viewPos, std::optional<size_t> on) const -> std::optional<Place> {
    if (layout->pageCount() == 0) {
        return std::nullopt;
    }
    const QPointF c = viewToContent(viewPos);
    const size_t page = on ? std::min(*on, layout->pageCount() - 1) : layout->nearestPage(c, z);
    const QRectF r = layout->pageRect(page, z);
    if (r.width() <= 0 || r.height() <= 0) {
        return Place{page, {}};
    }
    return Place{page, QPointF((c.x() - r.x()) / r.width(), (c.y() - r.y()) / r.height())};
}

void ViewController::showPlace(const Place& place, QPointF viewPos) {
    if (layout->pageCount() == 0 || !initialized || view.isEmpty()) {
        return;
    }
    stopMomentum();
    const QRectF r = layout->pageRect(std::min(place.page, layout->pageCount() - 1), z);
    const QPointF content(r.x() + place.relative.x() * r.width(), r.y() + place.relative.y() * r.height());
    // (where the content is smaller than the view it stays centred: contentOrigin)
    const QPointF before = scrollPos;
    scrollPos = content - viewPos;
    clamp();
    if (scrollPos != before) {
        Q_EMIT changed();
    }
}

void ViewController::setZoom100(double value) {
    if (!(value > 0) || value == z100) {
        return;
    }
    z100 = value;
    Q_EMIT zoom100Changed();
}

void ViewController::setZoom(double zoom, QPointF viewAnchor) {
    zoom = std::clamp(zoom, minZoom(), maxZoom());
    if (zoom == z) {
        return;
    }
    const Anchor a = anchorAt(viewAnchor);
    z = zoom;
    kept = Fit::None;  // (zoomed by hand)
    placeAnchor(a, viewAnchor);
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

size_t ViewController::pageInView() const {
    if (layout->pageCount() == 0) {
        return 0;
    }
    return layout->nearestPage(viewToContent(QPointF(view.width() / 2, view.height() / 2)), z);
}

void ViewController::fitWidth(std::optional<size_t> page) {
    jumped = true;
    if (view.isEmpty() || layout->pageCount() == 0) {
        return;
    }
    // Upstream ZoomControl fit-to-width: (viewport width) / (page width + 20), for the page in view (its row)
    const size_t p = std::min(page.value_or(pageInView()), layout->pageCount() - 1);
    const double fit = fitWidthZoom(p);
    if (fit <= 0) {
        return;
    }
    const Anchor a = anchorAt(QPointF(view.width() / 2, 0));
    z = std::clamp(fit, minZoom(), maxZoom());
    kept = Fit::None;
    placeAnchor(a, QPointF(view.width() / 2, 0));
    // Its column may be wider (a wider page elsewhere): the row in the middle
    scrollPos.setX(layout->rowSpan(p, z).center().x() - view.width() / 2);
    clamp();
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::fitPage(size_t page, bool wholePage) {
    jumped = true;
    if (view.isEmpty() || page >= layout->pageCount()) {
        return;
    }
    const QSizeF size = layout->pageSize(page);
    if (size.isEmpty()) {
        return;
    }
    const double padding = 2 * layout->padding();
    double fit = (view.height() - padding) / size.height();
    if (wholePage) {
        fit = std::min(fit, (view.width() - padding) / size.width());
    }
    z = std::clamp(fit, minZoom(), maxZoom());
    kept = Fit::None;
    scrollToPage(page);
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::zoomToPageRect(size_t page, QRectF rectPt) {
    jumped = true;
    if (view.isEmpty() || page >= layout->pageCount() || rectPt.isEmpty()) {
        return;
    }
    const double padding = 2 * layout->padding();
    const double fit = std::min((view.width() - padding) / rectPt.width(), maxZoom());
    z = std::clamp(fit, minZoom(), maxZoom());
    kept = Fit::None;
    const QRectF p = layout->pageRect(page, z);
    scrollPos = QPointF(p.x() + (rectPt.center().x() * z) - view.width() / 2,
                        p.y() + (rectPt.top() * z) - layout->padding());
    clamp();
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::panBy(QPointF delta) {
    scrollPos -= delta;
    clamp();
    Q_EMIT changed();
}

void ViewController::scrollToPageRect(size_t page, QRectF rectPt) {
    jumped = true;
    if (page >= layout->pageCount()) {
        return;
    }
    if (!initialized) {
        pendingPage = page;
        return;
    }
    stopMomentum();
    const QRectF p = layout->pageRect(page, z);
    const QRectF r(p.x() + rectPt.x() * z, p.y() + rectPt.y() * z, rectPt.width() * z, rectPt.height() * z);
    const QRectF visible = visibleContentRect().adjusted(0, 40, 0, -40);  // not under floating bars
    if (visible.contains(r)) {
        return;
    }
    if (r.top() < visible.top() || r.bottom() > visible.bottom()) {
        scrollPos.setY(r.center().y() - view.height() / 2);
    }
    if (r.left() < visible.left() || r.right() > visible.right()) {
        scrollPos.setX(r.center().x() - view.width() / 2);
    }
    clamp();
    pageJump = page;
    Q_EMIT changed();
}

void ViewController::scrollToPage(size_t page) {
    jumped = true;
    if (page >= layout->pageCount()) {
        return;
    }
    if (!initialized) {
        pendingPage = page;
        return;
    }
    stopMomentum();
    const QRectF r = layout->pageRect(page, z);
    const QRectF visible = visibleContentRect();
    if (layout->horizontal()) {
        if (kept == Fit::Page) {
            fitPresentedPage(page);  // presenting: the page fills the screen, whatever its shape
            return;
        }
        // Sideways: to the resting place of its group (always when snapping, so that it comes to rest there)
        if (!snapping() && visible.contains(r)) {
            pageJump = page;  // (in view already: the current page all the same)
            Q_EMIT changed();
            return;
        }
        if (snapping() || r.left() < visible.left() || r.right() > visible.right()) {
            placeGroup(layout->groupOf(page));
        }
        if (r.top() < visible.top() || r.bottom() > visible.bottom()) {
            scrollPos.setY(r.top() - layout->padding());
        }
        clamp();
        pageJump = page;
        Q_EMIT changed();
        return;
    }
    if (visible.contains(r) || (r.height() > visible.height() && visible.top() <= r.top() &&
                                visible.bottom() >= r.top() + visible.height() / 2)) {
        pageJump = page;  // already (mostly) visible: the current page all the same
        Q_EMIT changed();
        return;
    }
    scrollPos.setY(r.top() - layout->padding());
    if (r.left() < visible.left() || r.right() > visible.right()) {  // other column
        scrollPos.setX(r.left() - layout->padding());
    }
    clamp();
    pageJump = page;
    Q_EMIT changed();
}

void ViewController::pinchBegin(QPointF centroid, double distance, std::optional<double> fingerDegrees) {
    stopMomentum();
    pinchAnchor = anchorAt(screenToView(centroid));
    pinchStartDistance = std::max(distance, 1.0);
    pinchStartZoom = z;
    twistActive = false;
    if (fingerDegrees) {
        twistBegin(*fingerDegrees);
    }
}

void ViewController::pinchUpdate(QPointF centroid, double distance, std::optional<double> fingerDegrees) {
    const double ratio = std::max(distance, 1.0) / pinchStartDistance;
    const double zoom = std::clamp(pinchStartZoom * ratio, minZoom(), maxZoom());
    const bool zoomed = zoom != z;
    z = zoom;
    bool turned = false;
    if (fingerDegrees && twistActive) {
        twistTotal += std::remainder(*fingerDegrees - twistLastFinger, 360.0);
        twistLastFinger = *fingerDegrees;
        if (const auto target = twistTarget(); target && *target != angle) {
            applyAngle(*target);
            turned = true;
        }
    }
    // The document point that was under the fingers stays under them, on the screen (also while it turns)
    placeAnchor(pinchAnchor, screenToView(centroid));  // also pans with the centroid
    if (zoomed) {
        kept = Fit::None;
        settleTimer.start();
        Q_EMIT zoomChanged();
    }
    if (turned) {
        kept = Fit::None;
        jumped = true;
        Q_EMIT rotationChanged();
    }
    Q_EMIT changed();
}

void ViewController::pinchEnd() {
    twistActive = false;
    zoomGestureEnded();
}

void ViewController::zoomGestureEnded() {
    // (a Ctrl+wheel has no end: its zoom is stable once it did not change for a while)
    if (settleTimer.isActive()) {
        settleTimer.stop();
        Q_EMIT zoomSettled();
    }
}

void ViewController::fling(QPointF v) {
    if (std::hypot(v.x(), v.y()) < MIN_VELOCITY * 4) {
        return;
    }
    velocity = v;
    momentumStartMs = clock->nowMs();
    lastMomentumMs = 0;
    momentumTimer.start();
}

void ViewController::stopMomentum() {
    momentumTimer.stop();
    velocity = {};
    animating = false;
    animGroup.reset();
}

void ViewController::stepMomentum() {
    if (animating) {
        stepAnimation();
        return;
    }
    const qint64 now = momentumElapsed();
    const double dt = static_cast<double>(std::max<qint64>(1, now - lastMomentumMs));
    lastMomentumMs = now;
    const double decay = std::pow(DECELERATION_PER_MS, dt);
    // Exact integral of the exponential decay over dt.
    const double travel = (1.0 - decay) / (1.0 - DECELERATION_PER_MS);
    const QPointF before = scrollPos;
    scrollPos -= velocity * travel;
    clamp();
    velocity *= decay;
    if (scrollPos == before || std::hypot(velocity.x(), velocity.y()) < MIN_VELOCITY) {
        stopMomentum();
    }
    Q_EMIT changed();
}

void ViewController::layoutChanged() {
    jumped = true;
    if (kept == Fit::Rect && !view.isEmpty() && keptPage < layout->pageCount()) {
        const double before = z;
        placeKeptRect();  // (the pages moved: the same part of the same page)
        if (z != before) {
            settleTimer.start();
            Q_EMIT zoomChanged();
        }
        Q_EMIT changed();
        return;
    }
    if (kept == Fit::Height && layout->horizontal() && !view.isEmpty()) {
        // (a page of another height: the rows fill the height again)
        const double fit = std::clamp(layout->fitHeightZoom(view.height()), minZoom(), maxZoom());
        if (fit > 0 && fit != z) {
            z = fit;
            settleTimer.start();
            Q_EMIT zoomChanged();
        }
    }
    clamp();
    Q_EMIT changed();
}

// --- the canvas turned ----------------------------------------------------------------------------------------------

namespace {
/// Degrees in [0, 360)
double normalised(double degrees) {
    double a = std::fmod(degrees, 360.0);
    if (a < 0) {
        a += 360.0;
    }
    return a >= 360.0 ? 0.0 : a;
}
}  // namespace

void ViewController::applyAngle(double degrees) {
    angle = normalised(degrees);
    if (std::fmod(angle, 90.0) == 0) {
        // Exact at right angles: the screen's pixels are the view's pixels
        static constexpr double C[4] = {1, 0, -1, 0};
        static constexpr double S[4] = {0, 1, 0, -1};
        const int q = static_cast<int>(angle / 90.0) % 4;
        cosA = C[q];
        sinA = S[q];
    } else {
        const double r = angle * M_PI / 180.0;
        cosA = std::cos(r);
        sinA = std::sin(r);
    }
    if (angle == 0) {
        view = screen;
    } else {
        const double w = screen.width(), h = screen.height();
        view = QSizeF(std::abs(w * cosA) + std::abs(h * sinA), std::abs(w * sinA) + std::abs(h * cosA));
    }
    screenCentre = QPointF(screen.width() / 2, screen.height() / 2);
    viewCentre = QPointF(view.width() / 2, view.height() / 2);
}

double ViewController::snapAngle(double degrees) {
    const double a = normalised(degrees);
    const double right = std::round(a / 90.0) * 90.0;
    return std::abs(a - right) <= SNAP_DEGREES ? normalised(right) : a;
}

void ViewController::setRotation(double degrees, std::optional<QPointF> screenAnchor) {
    const double target = normalised(degrees);
    if (target == angle) {
        return;
    }
    if (screen.isEmpty() || !initialized) {
        applyAngle(target);
        Q_EMIT rotationChanged();
        return;
    }
    stopMomentum();
    const QPointF at = screenAnchor.value_or(screenCentre);
    const Anchor a = anchorAt(screenToView(at));
    applyAngle(target);
    kept = Fit::None;
    placeAnchor(a, screenToView(at));
    jumped = true;
    Q_EMIT rotationChanged();
    Q_EMIT changed();
}

void ViewController::rotateBy(double degrees, std::optional<QPointF> screenAnchor) {
    const double step = std::abs(degrees);
    if (step == 0) {
        return;
    }
    if (std::fmod(angle, step) == 0) {
        setRotation(angle + degrees, screenAnchor);
        return;
    }
    // From a free angle: to the next multiple of the step that way
    setRotation(degrees > 0 ? std::ceil(angle / step) * step : std::floor(angle / step) * step, screenAnchor);
}

QRectF ViewController::viewToScreen(const QRectF& r) const {
    if (angle == 0 || r.isNull()) {
        return r;
    }
    const QPointF a = viewToScreen(r.topLeft()), b = viewToScreen(r.topRight()), c = viewToScreen(r.bottomRight()),
                  d = viewToScreen(r.bottomLeft());
    const double left = std::min({a.x(), b.x(), c.x(), d.x()}), right = std::max({a.x(), b.x(), c.x(), d.x()});
    const double top = std::min({a.y(), b.y(), c.y(), d.y()}), bottom = std::max({a.y(), b.y(), c.y(), d.y()});
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

std::array<QPointF, 4> ViewController::screenInView() const {
    const double w = screen.width(), h = screen.height();
    return {screenToView(QPointF(0, 0)), screenToView(QPointF(w, 0)), screenToView(QPointF(w, h)),
            screenToView(QPointF(0, h))};
}

void ViewController::twistBegin(double fingerDegrees) {
    twistActive = true;
    twistEngaged = false;
    twistStartAngle = angle;
    twistStartFinger = fingerDegrees;
    twistLastFinger = fingerDegrees;
    twistTotal = 0;
    twistOffset = 0;
}

std::optional<double> ViewController::twistTarget() {
    if (!twistEngaged) {
        if (std::abs(twistTotal) < ROTATE_START_DEGREES) {
            return std::nullopt;
        }
        // From here on it turns with the fingers, behind them by the start (so that nothing jumps)
        twistEngaged = true;
        twistOffset = std::copysign(ROTATE_START_DEGREES, twistTotal);
    }
    return snapAngle(twistStartAngle + twistTotal - twistOffset);
}

void ViewController::twistTo(double fingerDegrees, QPointF screenAnchor) {
    if (!twistActive) {
        twistBegin(fingerDegrees);
        return;
    }
    twistTotal += std::remainder(fingerDegrees - twistLastFinger, 360.0);
    twistLastFinger = fingerDegrees;
    if (const auto target = twistTarget()) {
        setRotation(*target, screenAnchor);
    }
}

void ViewController::twistBy(double deltaDegrees, QPointF screenAnchor) {
    if (!twistActive) {
        twistBegin(0);
    }
    twistTotal += deltaDegrees;
    if (const auto target = twistTarget()) {
        setRotation(*target, screenAnchor);
    }
}

// --- scrolling sideways ---------------------------------------------------------------------------------------------

namespace {
/// A flick faster than this goes on to the next page (px/ms)
constexpr double FLICK_VELOCITY = 0.25;
}  // namespace

void ViewController::fitHeight() {
    jumped = true;
    if (view.isEmpty() || layout->pageCount() == 0) {
        return;
    }
    const double fit = layout->fitHeightZoom(view.height());
    if (fit <= 0) {
        return;
    }
    const size_t group = currentGroup();
    stopMomentum();
    z = std::clamp(fit, minZoom(), maxZoom());
    kept = layout->horizontal() ? Fit::Height : Fit::None;
    if (layout->horizontal()) {
        placeGroup(group);
    }
    clamp();
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

double ViewController::presentedZoom(size_t page) const {
    if (page >= layout->pageCount() || view.isEmpty()) {
        return z;
    }
    const QSizeF size = layout->pageSize(page);
    if (size.isEmpty()) {
        return z;
    }
    const double pad = 2 * layout->padding();
    return std::clamp(std::min((view.width() - pad) / size.width(), (view.height() - pad) / size.height()), minZoom(),
                      maxZoom());
}

void ViewController::fitPresentedPage(size_t page) {
    jumped = true;
    if (!initialized || view.isEmpty() || page >= layout->pageCount()) {
        kept = Fit::Page;  // (fitted once the view has a size)
        pendingPage = page;
        return;
    }
    stopMomentum();
    const double fit = presentedZoom(page);
    kept = Fit::Page;
    if (fit != z) {
        z = fit;
        settleTimer.start();
        Q_EMIT zoomChanged();
    }
    placeGroup(layout->groupOf(page));
    pageJump = page;
    Q_EMIT changed();
}

void ViewController::fitPageRect(size_t page, QRectF rectPt) {
    jumped = true;
    kept = Fit::Rect;
    keptPage = page;
    keptRect = rectPt;
    if (!initialized || view.isEmpty() || page >= layout->pageCount() || rectPt.isEmpty()) {
        return;  // (fitted once the view has a size)
    }
    stopMomentum();
    const double before = z;
    placeKeptRect();
    pageJump = page;
    if (z != before) {
        settleTimer.start();
        Q_EMIT zoomChanged();
    }
    Q_EMIT changed();
}

void ViewController::placeKeptRect() {
    if (keptRect.isEmpty() || keptPage >= layout->pageCount()) {
        return;
    }
    z = std::clamp(std::min(view.width() / keptRect.width(), view.height() / keptRect.height()), minZoom(), maxZoom());
    const QRectF p = layout->pageRect(keptPage, z);
    const QPointF centre = p.topLeft() + keptRect.center() * z;
    scrollPos = centre - QPointF(view.width() / 2, view.height() / 2);
    clamp();
}

void ViewController::fitDefault(std::optional<size_t> page) {
    if (!layout->horizontal()) {
        fitWidth(page);
        return;
    }
    fitHeight();
    if (page && *page < layout->pageCount()) {
        placeGroup(layout->groupOf(*page));
        Q_EMIT changed();
    }
}

void ViewController::setSnapping(bool on, int maxStep) {
    snap = on;
    snapMaxStep = std::max(0, maxStep);
}

QPointF ViewController::scrollDelta(QPointF delta) const {
    if (!layout->horizontal() || layout->contentSize(z).height() > view.height() + 0.5) {
        return delta;
    }
    return QPointF(delta.x() + delta.y(), 0);  // (nothing to scroll up or down)
}

std::pair<double, double> ViewController::restRangeUnclamped(size_t group) const {
    const QRectF r = layout->groupRect(group, z);
    const double pad = layout->padding();
    if (r.width() + 2 * pad <= view.width() + 0.5) {
        // It fits: in the middle when it is the only whole one in view, else at the left edge (more whole pages);
        // presenting (no margins) always one page in the middle
        const bool several = !layout->getConfig().noMargins &&
                             2 * r.width() + DocumentLayout::PADDING_BETWEEN + 2 * pad <= view.width();
        const double x = several ? r.left() - pad : r.center().x() - view.width() / 2;
        return {x, x};
    }
    return {r.left() - pad, r.right() + pad - view.width()};
}

std::pair<double, double> ViewController::restRange(size_t group) const {
    const auto [lo, hi] = restRangeUnclamped(group);
    const auto [minX, maxX] = scrollRangeX();
    return {std::clamp(lo, minX, maxX), std::clamp(hi, minX, maxX)};
}

double ViewController::restY(size_t group) const {
    const double maxY = std::max(0.0, layout->contentSize(z).height() - view.height());
    if (kept != Fit::Page) {
        return std::clamp(scrollPos.y(), 0.0, maxY);
    }
    // Presenting: the page in the middle (a row is as high as its highest page)
    return std::clamp(layout->groupRect(group, z).center().y() - view.height() / 2, 0.0, maxY);
}

size_t ViewController::groupNear(double x) const {
    const size_t count = layout->groupCount();
    if (count == 0) {
        return 0;
    }
    const QPointF middle(x + view.width() / 2, scrollPos.y() + view.height() / 2);
    const size_t guess = layout->groupOf(layout->nearestPage(middle, z));
    size_t best = guess;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (size_t g = guess > 2 ? guess - 2 : 0; g < count && g <= guess + 2; ++g) {
        const auto [lo, hi] = restRange(g);
        const double d = x < lo ? lo - x : (x > hi ? x - hi : 0.0);
        if (d < bestDistance) {
            bestDistance = d;
            best = g;
        }
    }
    return best;
}

size_t ViewController::currentGroup() const {
    if (animating && animGroup) {
        return *animGroup;
    }
    return groupNear(scrollPos.x());
}

bool ViewController::groupFitsView() const {
    if (!layout->horizontal() || layout->pageCount() == 0) {
        return false;
    }
    const auto [lo, hi] = restRange(currentGroup());
    return hi - lo < 0.5;
}

void ViewController::placeGroup(size_t group) {
    if (layout->groupCount() == 0) {
        return;
    }
    stopMomentum();
    group = std::min(group, layout->groupCount() - 1);
    scrollPos = QPointF(restRange(group).first, restY(group));
    clamp();
}

void ViewController::endScroll(QPointF v) {
    if (!snapping() || layout->groupCount() == 0) {
        fling(v);
        return;
    }
    if (!layout->horizontal()) {
        endScrollVertically(v);
        return;
    }
    // Where the momentum would carry the view: the travel of the exponential decay is v / (1 - rate) per ms
    const double travel = 1.0 / (1.0 - DECELERATION_PER_MS);
    const QPointF projected = scrollPos - v * travel;
    const double x = scrollPos.x();
    const size_t count = layout->groupCount();
    const size_t g = groupNear(x);
    const auto [lo, hi] = restRange(g);
    size_t target = g;
    double tx = 0;
    if (std::abs(v.x()) < FLICK_VELOCITY) {
        // Let go (slowly): the closest resting place, where the view is if it is within a wide page
        tx = std::clamp(x, lo, hi);
    } else {
        const int dir = v.x() < 0 ? 1 : -1;  // (content moving left: on to the next pages)
        if (dir > 0 && x < hi - 0.5) {
            tx = std::min(projected.x(), hi);  // on within this page first, or to it
        } else if (dir < 0 && x > lo + 0.5) {
            tx = std::max(projected.x(), lo);
        } else {
            const auto next = static_cast<std::ptrdiff_t>(g) + dir;
            target = static_cast<size_t>(std::clamp<std::ptrdiff_t>(next, 0, static_cast<std::ptrdiff_t>(count) - 1));
            if (snapMaxStep == 0) {
                // A strong fling goes on as far as it carries
                const size_t carried = groupNear(projected.x());
                if ((dir > 0 && carried > target) || (dir < 0 && carried < target)) {
                    target = carried;
                }
            }
            const auto [tlo, thi] = restRange(target);
            tx = dir > 0 ? tlo : thi;
        }
    }
    const double maxY = std::max(0.0, layout->contentSize(z).height() - view.height());
    const double ty = kept == Fit::Page && target != g ? restY(target) : std::clamp(projected.y(), 0.0, maxY);
    animGroup.reset();
    animateTo(QPointF(tx, ty), -v);
    if (animating) {
        animGroup = target;
    }
}

// --- snapping up and down (reading) ---------------------------------------------------------------------------------

std::pair<double, double> ViewController::restRangeY(size_t group) const {
    const QRectF r = layout->groupRect(group, z);
    const double pad = layout->padding();
    const double maxY = std::max(0.0, layout->contentSize(z).height() - view.height());
    if (r.height() + 2 * pad <= view.height() + 0.5) {
        // It fits: in the middle of the view (the rows before and after it peek in)
        const double y = std::clamp(r.center().y() - view.height() / 2, 0.0, maxY);
        return {y, y};
    }
    return {std::clamp(r.top() - pad, 0.0, maxY), std::clamp(r.bottom() + pad - view.height(), 0.0, maxY)};
}

size_t ViewController::groupNearY(double y) const {
    const size_t count = layout->groupCount();
    if (count == 0) {
        return 0;
    }
    const QPointF middle(scrollPos.x() + view.width() / 2, y + view.height() / 2);
    const size_t guess = layout->groupOf(layout->nearestPage(middle, z));
    size_t best = guess;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (size_t g = guess > 2 ? guess - 2 : 0; g < count && g <= guess + 2; ++g) {
        const auto [lo, hi] = restRangeY(g);
        const double d = y < lo ? lo - y : (y > hi ? y - hi : 0.0);
        if (d < bestDistance) {
            bestDistance = d;
            best = g;
        }
    }
    return best;
}

void ViewController::endScrollVertically(QPointF v) {
    // As sideways (endScroll), along y: let go slowly, the closest resting place (within a tall row, where the view
    // is); a fling within a tall row goes on within it first, else on to the next row (or as far as it carries)
    const double travel = 1.0 / (1.0 - DECELERATION_PER_MS);
    const QPointF projected = scrollPos - v * travel;
    const double y = scrollPos.y();
    const size_t count = layout->groupCount();
    const size_t g = groupNearY(y);
    const auto [lo, hi] = restRangeY(g);
    size_t target = g;
    double ty = 0;
    if (std::abs(v.y()) < FLICK_VELOCITY) {
        ty = std::clamp(y, lo, hi);
    } else {
        const int dir = v.y() < 0 ? 1 : -1;  // (content moving up: on to the next rows)
        if (dir > 0 && y < hi - 0.5) {
            ty = std::min(projected.y(), hi);
        } else if (dir < 0 && y > lo + 0.5) {
            ty = std::max(projected.y(), lo);
        } else {
            const auto next = static_cast<std::ptrdiff_t>(g) + dir;
            target = static_cast<size_t>(std::clamp<std::ptrdiff_t>(next, 0, static_cast<std::ptrdiff_t>(count) - 1));
            const size_t carried = groupNearY(projected.y());
            if ((dir > 0 && carried > target) || (dir < 0 && carried < target)) {
                target = carried;
            }
            const auto [tlo, thi] = restRangeY(target);
            ty = dir > 0 ? tlo : thi;
        }
    }
    const auto [minX, maxX] = scrollRangeX();
    animGroup.reset();
    animateTo(QPointF(std::clamp(projected.x(), minX, maxX), ty), -v);
    if (animating) {
        animGroup = target;
    }
}

bool ViewController::stepPages(int delta) {
    const size_t count = layout->groupCount();
    if (!layout->horizontal() || count == 0) {
        return false;
    }
    const size_t from = currentGroup();
    const auto to = static_cast<size_t>(
            std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(from) + delta, 0, static_cast<std::ptrdiff_t>(count) - 1));
    if (kept == Fit::Page) {
        // Presenting a page of another size: fitted to it, there at once
        const size_t page = layout->groupPages(to).first;
        if (std::abs(presentedZoom(page) - z) > 1e-9) {
            fitPresentedPage(page);
            return true;
        }
    }
    const auto [lo, hi] = restRange(to);
    const QPointF v = animating ? animVelocity : QPointF();
    animateTo(QPointF(lo, restY(to)), to == from ? QPointF() : v);
    if (animating) {
        animGroup = to;
    }
    return true;
}

void ViewController::animateTo(QPointF target, QPointF v0) {
    momentumTimer.stop();
    velocity = {};
    animating = false;
    const auto [minX, maxX] = scrollRangeX();
    target.setX(std::clamp(target.x(), minX, maxX));
    target.setY(std::clamp(target.y(), 0.0, std::max(0.0, layout->contentSize(z).height() - view.height())));
    const QPointF d = target - scrollPos;
    const double distance = std::hypot(d.x(), d.y());
    if (distance < 0.5) {
        if (scrollPos != target) {
            scrollPos = target;
            Q_EMIT changed();
        }
        return;
    }
    // Only the momentum along the way is carried on (not a sideways part, nor one against it)
    const double along = (v0.x() * d.x() + v0.y() * d.y()) / distance;
    const QPointF v = along > 0 ? d / distance * along : QPointF();
    const double speed = std::max(0.0, along);
    // Long enough not to overshoot with the velocity it starts with, short enough to feel direct
    animDuration = speed > 0.05 ? std::clamp(2.0 * distance / speed, 150.0, 420.0)
                                : std::clamp(200.0 + distance * 0.15, 220.0, 380.0);
    animFrom = scrollPos;
    animTo = target;
    animVelocity = v;
    animating = true;
    momentumStartMs = clock->nowMs();
    momentumTimer.start();
}

void ViewController::stepAnimation() {
    const double t = std::min(1.0, static_cast<double>(momentumElapsed()) / animDuration);
    // Cubic Hermite: from `animFrom` with the start velocity to `animTo` at rest
    const double t2 = t * t, t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2;
    scrollPos = animFrom * h00 + animVelocity * (animDuration * h10) + animTo * h01;
    // (the current velocity: for steps that follow one another)
    const double dh00 = 6 * t2 - 6 * t, dh10 = 3 * t2 - 4 * t + 1, dh01 = -6 * t2 + 6 * t;
    const QPointF current = (animFrom * dh00 + animTo * dh01) / animDuration + animVelocity * dh10;
    if (t >= 1.0) {
        scrollPos = animTo;
        animating = false;
        animGroup.reset();
        momentumTimer.stop();
    } else {
        animVelocity = current;
        animFrom = scrollPos;
        animDuration *= (1.0 - t);
        momentumStartMs = clock->nowMs();
    }
    clamp();
    Q_EMIT changed();
}

}  // namespace xqt
