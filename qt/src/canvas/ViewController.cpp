#include "ViewController.h"

#include <algorithm>
#include <cmath>

namespace xqt {

namespace {
// UIScrollView "normal" deceleration rate: the velocity decays by this factor per millisecond.
constexpr double DECELERATION_PER_MS = 0.998;
constexpr double MIN_VELOCITY = 0.02;  // px/ms
}  // namespace

ViewController::ViewController(const DocumentLayout* layout, QObject* parent): QObject(parent), layout(layout) {
    momentumTimer.setTimerType(Qt::PreciseTimer);
    momentumTimer.setInterval(8);
    connect(&momentumTimer, &QTimer::timeout, this, &ViewController::stepMomentum);
    settleTimer.setSingleShot(true);
    settleTimer.setInterval(300);
    connect(&settleTimer, &QTimer::timeout, this, &ViewController::zoomSettled);
}

void ViewController::setViewSize(QSizeF size) {
    if (size.isEmpty()) {
        return;
    }
    const Anchor keep = anchorAt(QPointF(view.width() / 2, 0));
    view = size;
    if (!initialized) {
        initialized = true;
        fitWidth();
        if (pendingPage) {  // requested before the view had a size (e.g. a restored tab)
            const size_t page = *pendingPage;
            pendingPage.reset();
            scrollToPage(page);
        }
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
    scrollPos.setX(std::clamp(scrollPos.x(), 0.0, std::max(0.0, content.width() - view.width())));
    scrollPos.setY(std::clamp(scrollPos.y(), 0.0, std::max(0.0, content.height() - view.height())));
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

void ViewController::setZoom(double zoom, QPointF viewAnchor) {
    zoom = std::clamp(zoom, minZoom(), maxZoom());
    if (zoom == z) {
        return;
    }
    const Anchor a = anchorAt(viewAnchor);
    z = zoom;
    placeAnchor(a, viewAnchor);
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::fitWidth() {
    // Upstream ZoomControl fit-to-width: (viewport width) / (page width + 20), for all columns
    const double fit = layout->fitWidthZoom(view.width());
    if (fit <= 0 || view.isEmpty()) {
        return;
    }
    const Anchor a = anchorAt(QPointF(view.width() / 2, 0));
    z = std::clamp(fit, minZoom(), maxZoom());
    placeAnchor(a, QPointF(view.width() / 2, 0));
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::fitPage(size_t page, bool wholePage) {
    if (view.isEmpty() || page >= layout->pageCount()) {
        return;
    }
    const QSizeF size = layout->pageSize(page);
    if (size.isEmpty()) {
        return;
    }
    const double padding = 2 * DocumentLayout::PADDING;
    double fit = (view.height() - padding) / size.height();
    if (wholePage) {
        fit = std::min(fit, (view.width() - padding) / size.width());
    }
    z = std::clamp(fit, minZoom(), maxZoom());
    scrollToPage(page);
    settleTimer.start();
    Q_EMIT zoomChanged();
    Q_EMIT changed();
}

void ViewController::zoomToPageRect(size_t page, QRectF rectPt) {
    if (view.isEmpty() || page >= layout->pageCount() || rectPt.isEmpty()) {
        return;
    }
    const double padding = 2 * DocumentLayout::PADDING;
    const double fit = std::min((view.width() - padding) / rectPt.width(), maxZoom());
    z = std::clamp(fit, minZoom(), maxZoom());
    const QRectF p = layout->pageRect(page, z);
    scrollPos = QPointF(p.x() + (rectPt.center().x() * z) - view.width() / 2,
                        p.y() + (rectPt.top() * z) - DocumentLayout::PADDING);
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
    Q_EMIT changed();
}

void ViewController::scrollToPage(size_t page) {
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
    if (visible.contains(r) || (r.height() > visible.height() && visible.top() <= r.top() &&
                                visible.bottom() >= r.top() + visible.height() / 2)) {
        return;  // already (mostly) visible
    }
    scrollPos.setY(r.top() - DocumentLayout::PADDING);
    if (r.left() < visible.left() || r.right() > visible.right()) {  // other column
        scrollPos.setX(r.left() - DocumentLayout::PADDING);
    }
    clamp();
    Q_EMIT changed();
}

void ViewController::pinchBegin(QPointF centroid, double distance) {
    stopMomentum();
    pinchAnchor = anchorAt(centroid);
    pinchStartDistance = std::max(distance, 1.0);
    pinchStartZoom = z;
}

void ViewController::pinchUpdate(QPointF centroid, double distance) {
    const double ratio = std::max(distance, 1.0) / pinchStartDistance;
    const double zoom = std::clamp(pinchStartZoom * ratio, minZoom(), maxZoom());
    const bool zoomed = zoom != z;
    z = zoom;
    placeAnchor(pinchAnchor, centroid);  // also pans with the centroid
    if (zoomed) {
        settleTimer.start();
        Q_EMIT zoomChanged();
    }
    Q_EMIT changed();
}

void ViewController::pinchEnd() { settleTimer.start(); }

void ViewController::fling(QPointF v) {
    if (std::hypot(v.x(), v.y()) < MIN_VELOCITY * 4) {
        return;
    }
    velocity = v;
    momentumClock.start();
    lastMomentumMs = 0;
    momentumTimer.start();
}

void ViewController::stopMomentum() {
    momentumTimer.stop();
    velocity = {};
}

void ViewController::stepMomentum() {
    const qint64 now = momentumClock.elapsed();
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
    clamp();
    Q_EMIT changed();
}

}  // namespace xqt
