#include "TouchGestures.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>

#include <QEvent>
#include <QQuickWindow>
#include <QTouchEvent>

namespace xqt {

namespace {
constexpr int MIN_FINGERS = 4;
constexpr double TAP_MAX_MS = 500;
constexpr double TAP_SLOP_PX = 60;
constexpr double PINCH_IN = 0.75;   ///< the fingers are this much closer together
constexpr double PINCH_OUT = 1.35;  ///< ... or this much further apart
}  // namespace

// The window's touch events are watched through the application: the canvas takes the events of the pages for
// itself (an event filter of the window), and the application sees them before that.
TouchGestures::TouchGestures(QObject* parent): QObject(parent) { qApp->installEventFilter(this); }

TouchGestures::~TouchGestures() { qApp->removeEventFilter(this); }

void TouchGestures::setWindow(QQuickWindow* w) {
    if (w == watched) {
        return;
    }
    watched = w;
    reset();
    Q_EMIT windowChanged();
}

void TouchGestures::setEnabled(bool e) {
    if (e != on) {
        on = e;
        reset();
        Q_EMIT enabledChanged();
    }
}

void TouchGestures::reset() {
    points.clear();
    maxPoints = 0;
    travel = 0;
    startSpread = 0;
    fired = false;
}

double TouchGestures::spreadOf() const {
    if (points.size() < 2) {
        return 0;
    }
    QPointF centre;
    for (const auto& [id, p]: points) {
        centre += p;
    }
    centre /= static_cast<double>(points.size());
    double sum = 0;
    for (const auto& [id, p]: points) {
        sum += std::hypot(p.x() - centre.x(), p.y() - centre.y());
    }
    return sum / static_cast<double>(points.size());
}

bool TouchGestures::eventFilter(QObject* object, QEvent* event) {
    if (!on || object != watched) {
        return false;
    }
    const QEvent::Type type = event->type();
    if (type != QEvent::TouchBegin && type != QEvent::TouchUpdate && type != QEvent::TouchEnd &&
        type != QEvent::TouchCancel) {
        return false;
    }
    if (type == QEvent::TouchCancel) {
        reset();
        return false;
    }
    auto* touch = static_cast<QTouchEvent*>(event);
    if (type == QEvent::TouchBegin) {
        reset();  // a new touch: what was left of the one before is gone (e.g. an overlay took the fingers)
    }
    const double now = static_cast<double>(touch->timestamp());
    if (points.empty()) {
        startMs = now;
        travel = 0;
        fired = false;
    }

    QPointF before;
    for (const auto& [id, p]: points) {
        before += p;
    }
    if (!points.empty()) {
        before /= static_cast<double>(points.size());
    }

    const int had = static_cast<int>(points.size());
    for (const auto& pt: touch->points()) {
        if (pt.state() == QEventPoint::State::Released) {
            points.erase(pt.id());
        } else {
            points[pt.id()] = pt.scenePosition();
        }
    }
    const int count = static_cast<int>(points.size());
    if (count > maxPoints) {
        maxPoints = count;
        startSpread = spreadOf();  // measured when the last finger came down
        startCentroid = before;
    }

    if (maxPoints >= MIN_FINGERS && !points.empty()) {
        QPointF centre;
        for (const auto& [id, p]: points) {
            centre += p;
        }
        centre /= static_cast<double>(points.size());
        if (count == had && count == maxPoints) {
            // Only while no finger came down or went up: adding one moves the middle of them all
            travel += std::hypot(centre.x() - before.x(), centre.y() - before.y());
        }
        // Pinching, while all the fingers are still down
        if (!fired && count == maxPoints && startSpread > 20) {
            const double ratio = spreadOf() / startSpread;
            if (ratio < PINCH_IN) {
                fired = true;
                Q_EMIT pinchedIn(maxPoints);
            } else if (ratio > PINCH_OUT) {
                fired = true;
                Q_EMIT pinchedOut(maxPoints);
            }
        }
    }

    const bool allReleased = std::all_of(touch->points().begin(), touch->points().end(), [](const QEventPoint& p) {
        return p.state() == QEventPoint::State::Released;
    });
    if (points.empty() || (type == QEvent::TouchEnd && allReleased)) {  // all fingers up
        const int fingers = maxPoints;
        const bool tap = !fired && now - startMs <= TAP_MAX_MS && travel <= TAP_SLOP_PX;
        reset();
        if (tap && fingers >= MIN_FINGERS) {
            Q_EMIT tapped(fingers);
        }
    }
    return false;  // never take the event away
}

}  // namespace xqt
