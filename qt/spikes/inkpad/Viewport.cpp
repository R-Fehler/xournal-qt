#include "Viewport.h"

#include <algorithm>
#include <cmath>

#include "InkModel.h"

namespace {
constexpr double MIN_SCALE = 0.2;
constexpr double MAX_SCALE = 12.0;
// UIScrollView "normal" deceleration: velocity *= 0.998 per ms.
constexpr double DECELERATION_PER_MS = 0.998;
constexpr double MIN_VELOCITY = 0.02;  // px/ms
}  // namespace

Viewport::Viewport(QObject* parent): QObject(parent) {
    momentumTimer.setTimerType(Qt::PreciseTimer);
    momentumTimer.setInterval(8);
    connect(&momentumTimer, &QTimer::timeout, this, &Viewport::stepMomentum);
    settleTimer.setSingleShot(true);
    settleTimer.setInterval(300);
    connect(&settleTimer, &QTimer::timeout, this, [this] { Q_EMIT zoomSettled(s); });
}

void Viewport::setViewSize(QSizeF size) {
    const bool first = view.isEmpty() || (off.isNull() && s == 1.0);
    view = size;
    if (first) {
        fitWidth();
    } else {
        clampOffset();
        Q_EMIT changed();
    }
}

void Viewport::fitWidth() {
    s = std::clamp(view.width() * 0.92 / InkModel::PAGE_W, MIN_SCALE, MAX_SCALE);
    off = QPointF((view.width() - InkModel::PAGE_W * s) / 2.0, 16.0);
    Q_EMIT changed();
    settleTimer.start();
}

void Viewport::clampOffset() {
    // Keep at least a quarter of the viewport covered by the page (simple bounds, no rubber band).
    const double pw = InkModel::PAGE_W * s, ph = InkModel::PAGE_H * s;
    const double mx = view.width() * 0.75, my = view.height() * 0.75;
    off.setX(std::clamp(off.x(), -pw + view.width() - mx, mx));
    off.setY(std::clamp(off.y(), -ph + view.height() - my, my));
}

void Viewport::panBy(QPointF delta) {
    off += delta;
    clampOffset();
    Q_EMIT changed();
}

void Viewport::setScaleAround(QPointF pageAnchor, QPointF viewAnchor, double newScale) {
    newScale = std::clamp(newScale, MIN_SCALE, MAX_SCALE);
    if (newScale != s) {
        settleTimer.start();
    }
    s = newScale;
    off = viewAnchor - pageAnchor * s;
    clampOffset();
    Q_EMIT changed();
}

void Viewport::zoomAt(QPointF viewAnchor, double factor) {
    setScaleAround(toPage(viewAnchor), viewAnchor, s * factor);
}

void Viewport::pinchBegin(QPointF centroid, double distance) {
    stopMomentum();
    pinchPageAnchor = toPage(centroid);
    pinchStartDistance = std::max(distance, 1.0);
    pinchStartScale = s;
}

void Viewport::pinchUpdate(QPointF centroid, double distance) {
    const double ratio = std::max(distance, 1.0) / pinchStartDistance;
    setScaleAround(pinchPageAnchor, centroid, pinchStartScale * ratio);
}

void Viewport::pinchEnd() { settleTimer.start(); }

void Viewport::fling(QPointF v) {
    if (std::hypot(v.x(), v.y()) < MIN_VELOCITY * 4) {
        return;
    }
    velocity = v;
    momentumClock.start();
    lastMomentumMs = 0;
    momentumTimer.start();
}

void Viewport::stopMomentum() {
    momentumTimer.stop();
    velocity = {};
}

void Viewport::stepMomentum() {
    const qint64 now = momentumClock.elapsed();
    const double dt = static_cast<double>(std::max<qint64>(1, now - lastMomentumMs));
    lastMomentumMs = now;
    const double decay = std::pow(DECELERATION_PER_MS, dt);
    // Integrate the exponential decay exactly over dt.
    const double travelFactor = (1.0 - decay) / (1.0 - DECELERATION_PER_MS);
    const QPointF before = off;
    off += velocity * travelFactor;
    clampOffset();
    velocity *= decay;
    if (off == before || std::hypot(velocity.x(), velocity.y()) < MIN_VELOCITY) {
        stopMomentum();
    }
    Q_EMIT changed();
}
