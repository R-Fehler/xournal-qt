#include "SpikeContext.h"

#include <QCoreApplication>
#include <QTabletEvent>
#include <QWindow>

SpikeContext::SpikeContext(QString hostName, const QString& logPath, QObject* parent):
        QObject(parent), host(std::move(hostName)), eventLog(logPath), input(&ink, &view) {
    self = this;
    env = environmentSummary();
    eventLog.write(environmentReport());
    connect(&view, &Viewport::zoomSettled, this, [this](double scale) {
        // Re-render sharp at the new zoom (Xournal++ re-renders ~300 ms after the last zoom change).
        ink.setRenderScale(scale * qApp->devicePixelRatio());
    });
    hudTimer.setInterval(100);
    connect(&hudTimer, &QTimer::timeout, this, [this] {
        hudText = input.hudText();
        Q_EMIT hudChanged();
    });
    hudTimer.start();
    qApp->installEventFilter(this);
}

bool SpikeContext::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::TabletEnterProximity:
        case QEvent::TabletLeaveProximity:
            // Proximity events are only delivered to the application object.
            eventLog.logEvent(event, "app");
            input.proximity(event->type() == QEvent::TabletEnterProximity);
            break;
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::NativeGesture:
            // Log once per event: at the top-level QWindow (widget events are re-sent to child widgets).
            if (watched->isWindowType()) {
                eventLog.logEvent(event, "window");
            }
            break;
        default:
            break;
    }
    return false;
}
