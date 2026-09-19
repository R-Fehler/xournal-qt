#include "Diagnostics.h"

#include <algorithm>
#include <ctime>

#include <QAbstractEventDispatcher>
#include <QGuiApplication>
#include <QInputDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QScreen>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

double monotonicMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

QString deviceTypeName(int type) {
    switch (static_cast<QInputDevice::DeviceType>(type)) {
        case QInputDevice::DeviceType::Mouse:
            return "Mouse";
        case QInputDevice::DeviceType::TouchScreen:
            return "TouchScreen";
        case QInputDevice::DeviceType::TouchPad:
            return "TouchPad";
        case QInputDevice::DeviceType::Puck:
            return "Puck";
        case QInputDevice::DeviceType::Stylus:
            return "Stylus";
        case QInputDevice::DeviceType::Airbrush:
            return "Airbrush";
        case QInputDevice::DeviceType::Keyboard:
            return "Keyboard";
        case QInputDevice::DeviceType::Unknown:
            return "Unknown";
        default:
            return QString("Type%1").arg(type);
    }
}

QString pointerTypeName(int type) {
    switch (static_cast<QPointingDevice::PointerType>(type)) {
        case QPointingDevice::PointerType::Generic:
            return "Generic";
        case QPointingDevice::PointerType::Finger:
            return "Finger";
        case QPointingDevice::PointerType::Pen:
            return "Pen";
        case QPointingDevice::PointerType::Eraser:
            return "Eraser";
        case QPointingDevice::PointerType::Cursor:
            return "Cursor";
        case QPointingDevice::PointerType::Unknown:
            return "Unknown";
        default:
            return QString("Ptr%1").arg(type);
    }
}

QString eventTypeName(int type) {
    switch (static_cast<QEvent::Type>(type)) {
        case QEvent::TabletPress:
            return "TabletPress";
        case QEvent::TabletMove:
            return "TabletMove";
        case QEvent::TabletRelease:
            return "TabletRelease";
        case QEvent::TabletEnterProximity:
            return "TabletEnterProximity";
        case QEvent::TabletLeaveProximity:
            return "TabletLeaveProximity";
        case QEvent::TouchBegin:
            return "TouchBegin";
        case QEvent::TouchUpdate:
            return "TouchUpdate";
        case QEvent::TouchEnd:
            return "TouchEnd";
        case QEvent::TouchCancel:
            return "TouchCancel";
        case QEvent::MouseButtonPress:
            return "MousePress";
        case QEvent::MouseButtonRelease:
            return "MouseRelease";
        case QEvent::MouseButtonDblClick:
            return "MouseDblClick";
        case QEvent::MouseMove:
            return "MouseMove";
        case QEvent::Wheel:
            return "Wheel";
        case QEvent::NativeGesture:
            return "NativeGesture";
        case QEvent::Enter:
            return "Enter";
        case QEvent::Leave:
            return "Leave";
        case QEvent::HoverEnter:
            return "HoverEnter";
        case QEvent::HoverLeave:
            return "HoverLeave";
        case QEvent::HoverMove:
            return "HoverMove";
        default:
            return QString("Event%1").arg(type);
    }
}

namespace {
QJsonObject deviceJson(const QInputDevice* d) {
    QJsonObject o;
    if (!d) {
        return o;
    }
    o["name"] = d->name();
    o["type"] = deviceTypeName(static_cast<int>(d->type()));
    o["caps"] = static_cast<int>(d->capabilities());
    o["systemId"] = QString::number(d->systemId());
    o["seat"] = d->seatName();
    if (auto* p = qobject_cast<const QPointingDevice*>(d)) {
        o["pointerType"] = pointerTypeName(static_cast<int>(p->pointerType()));
        o["maxPoints"] = p->maximumPoints();
        o["buttonCount"] = p->buttonCount();
        o["uniqueId"] = QString::number(p->uniqueId().numericId());
    }
    return o;
}

QJsonArray pointJson(QPointF p) { return QJsonArray{p.x(), p.y()}; }
}  // namespace

EventLog::EventLog(const QString& path): file(path) {
    if (!path.isEmpty() && !file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("Cannot open event log %s", qPrintable(path));
    }
}

void EventLog::write(const QJsonObject& obj) {
    if (!file.isOpen()) {
        return;
    }
    std::lock_guard lock(mtx);
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.flush();  // keep the log complete even if the spike is killed
}

void EventLog::logEvent(const QEvent* e, const char* receiver) {
    if (!file.isOpen()) {
        return;
    }
    QJsonObject o;
    o["rt"] = monotonicMs();
    o["ev"] = eventTypeName(static_cast<int>(e->type()));
    o["to"] = receiver;
    if (e->isPointerEvent()) {
        const auto* pe = static_cast<const QPointerEvent*>(e);
        o["ts"] = static_cast<double>(pe->timestamp());
        o["dev"] = deviceJson(pe->device());
        o["ptr"] = pointerTypeName(static_cast<int>(pe->pointerType()));
        o["mods"] = static_cast<int>(pe->modifiers());
    }
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::TabletEnterProximity:
        case QEvent::TabletLeaveProximity: {
            const auto* t = static_cast<const QTabletEvent*>(e);
            o["pos"] = pointJson(t->position());
            o["gpos"] = pointJson(t->globalPosition());
            o["p"] = t->pressure();
            o["xt"] = t->xTilt();
            o["yt"] = t->yTilt();
            o["rot"] = t->rotation();
            o["z"] = t->z();
            o["tp"] = t->tangentialPressure();
            o["btn"] = static_cast<int>(t->button());
            o["btns"] = static_cast<int>(t->buttons());
            break;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            const auto* t = static_cast<const QTouchEvent*>(e);
            QJsonArray pts;
            for (const auto& pt: t->points()) {
                QJsonObject po;
                po["id"] = pt.id();
                po["state"] = static_cast<int>(pt.state());
                po["pos"] = pointJson(pt.scenePosition());
                po["p"] = pt.pressure();
                po["ell"] = QJsonArray{pt.ellipseDiameters().width(), pt.ellipseDiameters().height()};
                pts.append(po);
            }
            o["points"] = pts;
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove: {
            const auto* m = static_cast<const QMouseEvent*>(e);
            o["pos"] = pointJson(m->position());
            o["btn"] = static_cast<int>(m->button());
            o["btns"] = static_cast<int>(m->buttons());
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            o["src"] = static_cast<int>(m->source());  // 0 = not synthesized
            QT_WARNING_POP
            break;
        }
        case QEvent::Wheel: {
            const auto* w = static_cast<const QWheelEvent*>(e);
            o["pos"] = pointJson(w->position());
            o["pixel"] = QJsonArray{w->pixelDelta().x(), w->pixelDelta().y()};
            o["angle"] = QJsonArray{w->angleDelta().x(), w->angleDelta().y()};
            o["phase"] = static_cast<int>(w->phase());
            o["inverted"] = w->inverted();
            break;
        }
        case QEvent::NativeGesture: {
            const auto* g = static_cast<const QNativeGestureEvent*>(e);
            o["gesture"] = static_cast<int>(g->gestureType());
            o["value"] = g->value();
            o["fingers"] = g->fingerCount();
            o["delta"] = pointJson(g->delta());
            o["pos"] = pointJson(g->position());
            break;
        }
        default:
            break;
    }
    write(o);
}

void LatencyStats::add(double ms) {
    std::lock_guard lock(mtx);
    samples.push_back(ms);
    if (samples.size() > 300) {
        samples.pop_front();
    }
}

QString LatencyStats::summary() const {
    std::lock_guard lock(mtx);
    if (samples.empty()) {
        return "n/a";
    }
    std::vector<double> v(samples.begin(), samples.end());
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x: v) {
        sum += x;
    }
    const double p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
    return QString("avg %1 / p95 %2 ms").arg(sum / static_cast<double>(v.size()), 0, 'f', 1).arg(p95, 0, 'f', 1);
}

void RateCounter::tick() {
    const double now = monotonicMs();
    times.push_back(now);
    while (!times.empty() && times.front() < now - 1000.0) {
        times.pop_front();
    }
}

double RateCounter::hz() const {
    const double now = monotonicMs();
    return static_cast<double>(std::count_if(times.begin(), times.end(), [&](double t) { return t >= now - 1000.0; }));
}

QJsonObject environmentReport() {
    QJsonObject o;
    o["kind"] = "environment";
    o["qtVersion"] = qVersion();
    o["platform"] = QGuiApplication::platformName();
    auto* disp = QAbstractEventDispatcher::instance();
    o["dispatcher"] = disp ? disp->metaObject()->className() : "none";
    o["XDG_SESSION_TYPE"] = qEnvironmentVariable("XDG_SESSION_TYPE");
    o["QT_QPA_PLATFORM"] = qEnvironmentVariable("QT_QPA_PLATFORM");
    o["QT_IM_MODULE"] = qEnvironmentVariable("QT_IM_MODULE");
    o["compressHighFrequency"] = QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents);
    o["compressTablet"] = QCoreApplication::testAttribute(Qt::AA_CompressTabletEvents);
    o["synthMouseForTablet"] = QCoreApplication::testAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents);
    if (auto* screen = QGuiApplication::primaryScreen()) {
        o["screen"] = QString("%1 %2x%3 dpr %4 refresh %5Hz")
                              .arg(screen->name())
                              .arg(screen->size().width())
                              .arg(screen->size().height())
                              .arg(screen->devicePixelRatio())
                              .arg(screen->refreshRate());
    }
    QJsonArray devs;
    for (const auto* d: QInputDevice::devices()) {
        devs.append(deviceJson(d));
    }
    o["devices"] = devs;
    return o;
}

QString environmentSummary() {
    const QJsonObject o = environmentReport();
    QString s = QString("Qt %1 | %2 | dispatcher %3 | compressHF=%4 | %5")
                        .arg(o["qtVersion"].toString(), o["platform"].toString(), o["dispatcher"].toString())
                        .arg(o["compressHighFrequency"].toBool())
                        .arg(o["screen"].toString());
    return s;
}
