#include "PenHover.h"

#include <algorithm>

#include <QCoreApplication>
#include <QTabletEvent>

namespace xqt {

PenHover& PenHover::instance() {
    static PenHover* hover = new PenHover();  // lives with the application (its child)
    return *hover;
}

PenHover::PenHover(): QObject(QCoreApplication::instance()) {
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }
}

void PenHover::record(const QTabletEvent& e) {
    const double height = std::clamp(e.z() / MAX_Z, 0.0, 1.0);
    // Only a hovering pen tells its height (touching, it is 0 anyway); a pen that never reports one keeps z at 0
    const bool hovering = e.type() == QEvent::TabletMove && e.buttons() == Qt::NoButton;
    const bool reported = heightReported || (hovering && e.z() > 0);
    if (reported == heightReported && height == currentHeight) {
        return;
    }
    heightReported = reported;
    currentHeight = height;
    Q_EMIT changed();
}

void PenHover::setProximity(bool near) {
    if (near != proximity) {
        proximity = near;
        Q_EMIT changed();
    }
}

void PenHover::reset() {
    heightReported = false;
    currentHeight = 0;
    proximity = false;
    Q_EMIT changed();
}

bool PenHover::eventFilter(QObject* watched, QEvent* event) {
    // Every event passes the application object first; only look at what arrives at a window (not again at each
    // item it is delivered to)
    switch (event->type()) {
        case QEvent::TabletEnterProximity:
            setProximity(true);
            break;
        case QEvent::TabletLeaveProximity:
            setProximity(false);
            break;
        case QEvent::TabletMove:
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
            if (watched->isWindowType()) {
                record(*static_cast<QTabletEvent*>(event));
            }
            break;
        default:
            break;
    }
    return false;
}

}  // namespace xqt
