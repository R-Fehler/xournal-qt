#include "InputLog.h"

#include <cstdio>

#include <QElapsedTimer>
#include <QEvent>
#include <QInputDevice>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStringList>
#include <QTabletEvent>
#include <QTouchEvent>

namespace xqt::inputlog {

namespace {
constexpr int MOVES_PER_STROKE = 5;  // moves logged after each press
constexpr int HOVER_MOVES = 3;       // moves without a button logged after the pen comes near

struct State {
    const QEvent* lastEvent = nullptr;
    int lastType = 0;
    quint64 lastTimestamp = 0;
    int penMoves = 0;
    int penHovers = 0;
    int mouseMoves = 0;
    int touchMoves = 0;
};
State& state() {
    static State s;
    return s;
}

qint64 ms() {
    static QElapsedTimer clock;
    if (!clock.isValid()) {
        clock.start();
    }
    return clock.elapsed();
}

const char* typeName(QEvent::Type t) {
    switch (t) {
        case QEvent::TabletPress: return "TabletPress";
        case QEvent::TabletMove: return "TabletMove";
        case QEvent::TabletRelease: return "TabletRelease";
        case QEvent::TabletEnterProximity: return "TabletEnterProximity";
        case QEvent::TabletLeaveProximity: return "TabletLeaveProximity";
        case QEvent::MouseButtonPress: return "MouseButtonPress";
        case QEvent::MouseButtonRelease: return "MouseButtonRelease";
        case QEvent::MouseMove: return "MouseMove";
        case QEvent::TouchBegin: return "TouchBegin";
        case QEvent::TouchUpdate: return "TouchUpdate";
        case QEvent::TouchEnd: return "TouchEnd";
        case QEvent::TouchCancel: return "TouchCancel";
        default: return "other";
    }
}

const char* deviceTypeName(QInputDevice::DeviceType t) {
    switch (t) {
        case QInputDevice::DeviceType::Mouse: return "Mouse";
        case QInputDevice::DeviceType::TouchScreen: return "TouchScreen";
        case QInputDevice::DeviceType::TouchPad: return "TouchPad";
        case QInputDevice::DeviceType::Puck: return "Puck";
        case QInputDevice::DeviceType::Stylus: return "Stylus";
        case QInputDevice::DeviceType::Airbrush: return "Airbrush";
        case QInputDevice::DeviceType::Keyboard: return "Keyboard";
        default: return "Unknown";
    }
}

const char* pointerTypeName(QPointingDevice::PointerType t) {
    switch (t) {
        case QPointingDevice::PointerType::Generic: return "Generic";
        case QPointingDevice::PointerType::Finger: return "Finger";
        case QPointingDevice::PointerType::Pen: return "Pen";
        case QPointingDevice::PointerType::Eraser: return "Eraser";
        case QPointingDevice::PointerType::Cursor: return "Cursor";
        default: return "Unknown";
    }
}

QString capabilities(QInputDevice::Capabilities c) {
    QStringList parts;
    const std::pair<QInputDevice::Capability, const char*> names[] = {
            {QInputDevice::Capability::Position, "Position"},     {QInputDevice::Capability::Area, "Area"},
            {QInputDevice::Capability::Pressure, "Pressure"},     {QInputDevice::Capability::Velocity, "Velocity"},
            {QInputDevice::Capability::NormalizedPosition, "Normalized"},
            {QInputDevice::Capability::MouseEmulation, "MouseEmulation"},
            {QInputDevice::Capability::PixelScroll, "PixelScroll"}, {QInputDevice::Capability::Scroll, "Scroll"},
            {QInputDevice::Capability::Hover, "Hover"},           {QInputDevice::Capability::Rotation, "Rotation"},
            {QInputDevice::Capability::XTilt, "XTilt"},           {QInputDevice::Capability::YTilt, "YTilt"},
            {QInputDevice::Capability::TangentialPressure, "TangentialPressure"},
            {QInputDevice::Capability::ZPosition, "Z"}};
    for (const auto& [flag, name]: names) {
        if (c.testFlag(flag)) {
            parts << QLatin1String(name);
        }
    }
    return parts.isEmpty() ? QStringLiteral("none") : parts.join('|');
}

QString device(const QPointingDevice* d) {
    if (!d) {
        return QStringLiteral("device=none");
    }
    return QStringLiteral("device=\"%1\" id=%2 type=%3 pointer=%4 caps=%5 uniqueId=%6")
            .arg(d->name())
            .arg(d->systemId())
            .arg(QLatin1String(deviceTypeName(d->type())))
            .arg(QLatin1String(pointerTypeName(d->pointerType())))
            .arg(capabilities(d->capabilities()))
            .arg(d->uniqueId().numericId());
}

void line(const QString& text) {
    std::fprintf(stderr, "[input %7lld ms] %s\n", static_cast<long long>(ms()), text.toUtf8().constData());
    std::fflush(stderr);
}

/// Whether this event is one to log (a press, a release, the first moves of a stroke, the first hover moves).
bool wanted(const QEvent* e) {
    State& s = state();
    switch (e->type()) {
        case QEvent::TabletPress:
            s.penMoves = 0;
            return true;
        case QEvent::TabletRelease:
        case QEvent::TabletEnterProximity:
        case QEvent::TabletLeaveProximity:
            s.penHovers = 0;
            return true;
        case QEvent::TabletMove: {
            const bool down = static_cast<const QTabletEvent*>(e)->buttons() != Qt::NoButton;
            return down ? s.penMoves++ < MOVES_PER_STROKE : s.penHovers++ < HOVER_MOVES;
        }
        case QEvent::MouseButtonPress:
            s.mouseMoves = 0;
            return true;
        case QEvent::MouseButtonRelease:
            return true;
        case QEvent::MouseMove:
            return static_cast<const QMouseEvent*>(e)->buttons() != Qt::NoButton && s.mouseMoves++ < MOVES_PER_STROKE;
        case QEvent::TouchBegin:
            s.touchMoves = 0;
            return true;
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            return true;
        case QEvent::TouchUpdate:
            return s.touchMoves++ < MOVES_PER_STROKE;
        default:
            return false;
    }
}

/// The same event seen again (by another canvas of the window)?
bool seen(const QEvent* e) {
    State& s = state();
    const auto* pe = dynamic_cast<const QPointerEvent*>(e);
    const quint64 timestamp = pe ? pe->timestamp() : 0;
    if (s.lastEvent == e && s.lastType == e->type() && s.lastTimestamp == timestamp) {
        return true;
    }
    s.lastEvent = e;
    s.lastType = e->type();
    s.lastTimestamp = timestamp;
    return false;
}
}  // namespace

bool enabled() {
    static const bool on = qEnvironmentVariableIsSet("XQT_LOG_INPUT");
    return on;
}

void event(const QEvent* e) {
    if (!enabled() || seen(e) || !wanted(e)) {
        return;
    }
    const char* type = typeName(e->type());
    if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
        const auto* t = static_cast<const QTabletEvent*>(e);
        line(QStringLiteral("%1 %2").arg(QLatin1String(type), device(t->pointingDevice())));
        return;
    }
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            const auto* t = static_cast<const QTabletEvent*>(e);
            line(QStringLiteral("%1 %2 pos=(%3,%4) pressure=%5 tilt=(%6,%7) rotation=%8 z=%9 button=%10 buttons=%11")
                         .arg(QLatin1String(type), device(t->pointingDevice()))
                         .arg(t->position().x(), 0, 'f', 1)
                         .arg(t->position().y(), 0, 'f', 1)
                         .arg(t->pressure(), 0, 'f', 3)
                         .arg(t->xTilt(), 0, 'f', 1)
                         .arg(t->yTilt(), 0, 'f', 1)
                         .arg(t->rotation(), 0, 'f', 1)
                         .arg(t->z(), 0, 'f', 1)
                         .arg(int(t->button()))
                         .arg(int(t->buttons())));
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove: {
            const auto* m = static_cast<const QMouseEvent*>(e);
            const char* source = m->source() == Qt::MouseEventNotSynthesized       ? "real"
                                 : m->source() == Qt::MouseEventSynthesizedBySystem ? "synthesized by Windows/system"
                                 : m->source() == Qt::MouseEventSynthesizedByQt     ? "synthesized by Qt"
                                                                                    : "synthesized by the app";
            line(QStringLiteral("%1 %2 pos=(%3,%4) pressure=%5 button=%6 buttons=%7 source=%8")
                         .arg(QLatin1String(type), device(m->pointingDevice()))
                         .arg(m->scenePosition().x(), 0, 'f', 1)
                         .arg(m->scenePosition().y(), 0, 'f', 1)
                         .arg(m->points().isEmpty() ? -1.0 : m->points().first().pressure(), 0, 'f', 3)
                         .arg(int(m->button()))
                         .arg(int(m->buttons()))
                         .arg(QLatin1String(source)));
            break;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            const auto* t = static_cast<const QTouchEvent*>(e);
            QStringList points;
            for (const auto& p: t->points()) {
                points << QStringLiteral("#%1(%2,%3) p=%4 %5x%6")
                                  .arg(p.id())
                                  .arg(p.scenePosition().x(), 0, 'f', 1)
                                  .arg(p.scenePosition().y(), 0, 'f', 1)
                                  .arg(p.pressure(), 0, 'f', 3)
                                  .arg(p.ellipseDiameters().width(), 0, 'f', 1)
                                  .arg(p.ellipseDiameters().height(), 0, 'f', 1);
            }
            line(QStringLiteral("%1 %2 points=%3")
                         .arg(QLatin1String(type), device(t->pointingDevice()), points.join(' ')));
            break;
        }
        default:
            break;
    }
}

void decision(const QEvent* e, bool taken, const char* as) {
    if (!enabled()) {
        return;
    }
    const auto type = e->type();
    const bool logged = type == QEvent::TabletPress || type == QEvent::TabletRelease ||
                        type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease ||
                        type == QEvent::TouchBegin || type == QEvent::TouchEnd;
    if (!logged) {
        return;  // (moves: their press says where they go)
    }
    line(QStringLiteral("  -> %1: %2").arg(taken ? QStringLiteral("canvas takes it") : QStringLiteral("passed on"),
                                           QLatin1String(as)));
}

}  // namespace xqt::inputlog
