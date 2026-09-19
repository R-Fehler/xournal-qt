#include "InputRouter.h"

#include <cmath>

#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "InkModel.h"
#include "Viewport.h"

namespace {
constexpr double PEN_WIDTH_PT = 1.4;
constexpr double ERASER_RADIUS_PT = 6.0;
constexpr double PALM_TIMEOUT_MS = 1000.0;  // Xournal++ HandRecognition default
constexpr double TAP_MAX_MS = 250.0;
constexpr double TAP_SLOP_PX = 16.0;  // Krita's TOUCH_SLOP
const QColor PEN_COLOR(0x1d, 0x2b, 0x8f);

bool plausibleLatency(double ms) { return ms >= 0.0 && ms < 1000.0; }

QString buttonsString(Qt::MouseButtons b) {
    QStringList parts;
    if (b & Qt::LeftButton) parts << "tip";
    if (b & Qt::MiddleButton) parts << "middle";
    if (b & Qt::RightButton) parts << "right";
    if (b & ~(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton)) parts << QString("0x%1").arg(int(b), 0, 16);
    return parts.isEmpty() ? "none" : parts.join('+');
}
}  // namespace

InputRouter::InputRouter(InkModel* model, Viewport* viewport, QObject* parent):
        QObject(parent), model(model), viewport(viewport) {}

bool InputRouter::touchBlocked() const {
    if (penMode != PenMode::None) {
        return true;
    }
    if (proximityEverSeen && penInProximity) {
        return true;
    }
    return monotonicMs() - lastPenEventMs < PALM_TIMEOUT_MS;
}

void InputRouter::cancelTouchGesture() {
    if (!touches.empty() && !touchSessionIgnored) {
        ++gesturesCancelled;
        lastGesture = "touch gesture cancelled by pen";
    }
    if (pinching) {
        viewport->pinchEnd();
    }
    pinching = false;
    panning = false;
    touchSessionIgnored = !touches.empty();  // ignore the rest of the current touch session
    velSamples.clear();
    viewport->stopMomentum();
}

void InputRouter::noteInkEvent(ulong timestamp) { pendingTs.store(static_cast<double>(timestamp)); }

void InputRouter::syncPoint() {
    const double ts = pendingTs.exchange(-1);
    if (ts >= 0) {
        inFlightTs.store(ts);
    }
}

void InputRouter::framePresented() {
    const double ts = inFlightTs.exchange(-1);
    if (ts >= 0) {
        const double d = monotonicMs() - ts;
        if (plausibleLatency(d)) {
            frameLatency.add(d);
        }
    }
}

void InputRouter::proximity(bool entered) {
    proximityEverSeen = true;
    penInProximity = entered;
    if (!entered) {
        hover.reset();
        Q_EMIT hoverChanged();
    }
}

bool InputRouter::tablet(QTabletEvent* e, QPointF local) {
    ++penEvents;
    penRate.tick();
    const double now = monotonicMs();
    lastPenEventMs = now;
    const double handlerDelay = now - static_cast<double>(e->timestamp());
    if (plausibleLatency(handlerDelay)) {
        handlerLatency.add(handlerDelay);
    }
    lastDevice = e->device() ? e->device()->name() : QString("?");
    lastPointer = pointerTypeName(static_cast<int>(e->pointerType()));
    lastButtons = buttonsString(e->buttons());
    lastPressure = e->pressure();
    lastXTilt = e->xTilt();
    lastYTilt = e->yTilt();
    lastRotation = e->rotation();

    const bool isEraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
    const QPointF page = viewport->toPage(local);

    switch (e->type()) {
        case QEvent::TabletPress: {
            cancelTouchGesture();  // pen always wins over touch
            if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
                penMode = PenMode::Pan;
                lastPanPos = local;
            } else if (isEraser) {
                penMode = PenMode::Erase;
                model->eraseAt(page, ERASER_RADIUS_PT);
                noteInkEvent(e->timestamp());
            } else {
                penMode = PenMode::Draw;
                model->beginStroke(page, e->pressure(), PEN_COLOR, PEN_WIDTH_PT);
                noteInkEvent(e->timestamp());
            }
            break;
        }
        case QEvent::TabletMove: {
            switch (penMode) {
                case PenMode::Draw:
                    // Xournal++ drops zero-pressure motion (lift-off artefacts).
                    if (e->pressure() > 0.0) {
                        model->extendStroke(page, e->pressure());
                        noteInkEvent(e->timestamp());
                    }
                    break;
                case PenMode::Erase:
                    if (model->eraseAt(page, ERASER_RADIUS_PT)) {
                        noteInkEvent(e->timestamp());
                    }
                    break;
                case PenMode::Pan:
                    viewport->panBy(local - lastPanPos);
                    lastPanPos = local;
                    break;
                case PenMode::None:
                    break;
            }
            break;
        }
        case QEvent::TabletRelease:
            if (penMode == PenMode::Draw) {
                model->endStroke();
            }
            penMode = PenMode::None;
            break;
        default:
            break;
    }
    hover = local;
    hoverIsEraser = isEraser;
    Q_EMIT hoverChanged();
    return true;
}

bool InputRouter::touch(QTouchEvent* e, const MapToLocal& sceneToLocal) {
    ++touchEvents;
    touchRate.tick();
    const double now = monotonicMs();

    if (e->type() == QEvent::TouchCancel) {
        if (pinching) {
            viewport->pinchEnd();
        }
        touches.clear();
        pinching = false;
        panning = false;
        touchSessionIgnored = false;
        lastGesture = "touch cancelled by system";
        return true;
    }

    if (touches.empty()) {
        // First finger: start a new session. Palm rejection decides for the whole session.
        touchSessionIgnored = touchBlocked();
        if (touchSessionIgnored) {
            ++palmRejected;
            lastGesture = "touch rejected (pen active)";
        } else {
            viewport->stopMomentum();
        }
        touchSessionMaxPoints = 0;
        touchSessionStartMs = now;
        touchSessionTravel = 0;
        velSamples.clear();
    }

    std::vector<int> released;
    for (const auto& pt: e->points()) {
        const QPointF pos = sceneToLocal(pt.scenePosition());
        switch (pt.state()) {
            case QEventPoint::State::Pressed:
                touches[pt.id()] = TouchPt{pos, pos};
                break;
            case QEventPoint::State::Released:
                released.push_back(pt.id());
                [[fallthrough]];
            default:
                if (auto it = touches.find(pt.id()); it != touches.end()) {
                    it->second.pos = pos;
                }
                break;
        }
    }
    touchSessionMaxPoints = std::max(touchSessionMaxPoints, static_cast<int>(touches.size()));

    if (!touchSessionIgnored) {
        const int active = static_cast<int>(touches.size() - released.size());
        QPointF centroid;
        std::vector<QPointF> pts;
        for (const auto& [id, tp]: touches) {
            if (std::find(released.begin(), released.end(), id) == released.end()) {
                pts.push_back(tp.pos);
                centroid += tp.pos;
            }
        }
        if (!pts.empty()) {
            centroid /= static_cast<double>(pts.size());
        }

        if (active >= 2) {
            const double dist = std::hypot(pts[0].x() - pts[1].x(), pts[0].y() - pts[1].y());
            if (!pinching) {
                viewport->pinchBegin(centroid, dist);
                pinching = true;
                lastGesture = "pinch";
            } else {
                touchSessionTravel += std::hypot(centroid.x() - lastCentroid.x(), centroid.y() - lastCentroid.y());
                viewport->pinchUpdate(centroid, dist);
            }
            lastCentroid = centroid;
        } else if (active == 1) {
            if (pinching) {
                // 2 -> 1 fingers: continue as a pan from the remaining finger without a jump.
                viewport->pinchEnd();
                pinching = false;
                panning = false;
                velSamples.clear();
            }
            if (!panning) {
                panning = true;
                lastCentroid = centroid;
            }
            const QPointF delta = centroid - lastCentroid;
            touchSessionTravel += std::hypot(delta.x(), delta.y());
            if (!delta.isNull()) {
                viewport->panBy(delta);
                lastGesture = "pan";
            }
            lastCentroid = centroid;
            velSamples.push_back({now, centroid});
            while (velSamples.size() > 2 && velSamples.front().t < now - 100.0) {
                velSamples.erase(velSamples.begin());
            }
        }
    }

    for (int id: released) {
        touches.erase(id);
    }

    if (static_cast<int>(touches.size()) != 1) {
        panning = false;
    }
    if (touches.empty()) {
        // Session finished.
        if (!touchSessionIgnored) {
            if (pinching) {
                viewport->pinchEnd();
                pinching = false;
            }
            const double duration = now - touchSessionStartMs;
            if (duration <= TAP_MAX_MS && touchSessionTravel <= TAP_SLOP_PX && touchSessionMaxPoints >= 2) {
                if (touchSessionMaxPoints == 2) {
                    model->undo();
                    lastGesture = "2-finger tap: undo";
                } else if (touchSessionMaxPoints == 3) {
                    model->redo();
                    lastGesture = "3-finger tap: redo";
                }
            } else if (velSamples.size() >= 2) {
                const auto& a = velSamples.front();
                const auto& b = velSamples.back();
                const double dt = b.t - a.t;
                if (dt > 5.0 && now - b.t < 50.0) {
                    viewport->fling((b.pos - a.pos) / dt);
                    lastGesture = "fling";
                }
            }
        }
        touchSessionIgnored = false;
        velSamples.clear();
    }
    return true;
}

bool InputRouter::mouse(QMouseEvent* e, QPointF local) {
    ++mouseEvents;
    const auto type = e->device() ? e->device()->type() : QInputDevice::DeviceType::Mouse;
    if (type != QInputDevice::DeviceType::Mouse && type != QInputDevice::DeviceType::TouchPad) {
        // Synthesized from touch or tablet: must not happen when those events are accepted.
        ++synthMouseIgnored;
        return true;
    }
    const QPointF page = viewport->toPage(local);
    switch (e->type()) {
        case QEvent::MouseButtonPress:
            if (e->button() == Qt::LeftButton) {
                penMode = PenMode::Draw;
                model->beginStroke(page, 0.5, PEN_COLOR, PEN_WIDTH_PT);
                noteInkEvent(e->timestamp());
            } else {
                penMode = PenMode::Pan;
                lastPanPos = local;
            }
            break;
        case QEvent::MouseMove:
            if (penMode == PenMode::Draw) {
                model->extendStroke(page, 0.5);
                noteInkEvent(e->timestamp());
            } else if (penMode == PenMode::Pan) {
                viewport->panBy(local - lastPanPos);
                lastPanPos = local;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (penMode == PenMode::Draw) {
                model->endStroke();
            }
            penMode = PenMode::None;
            break;
        default:
            break;
    }
    return true;
}

bool InputRouter::wheel(QWheelEvent* e, QPointF local) {
    viewport->stopMomentum();
    if (e->modifiers() & Qt::ControlModifier) {
        viewport->zoomAt(local, std::pow(1.0015, e->angleDelta().y()));
    } else if (!e->pixelDelta().isNull()) {
        viewport->panBy(QPointF(e->pixelDelta()));
    } else {
        viewport->panBy(QPointF(e->angleDelta()) / 120.0 * 48.0);
    }
    return true;
}

bool InputRouter::nativeGesture(QNativeGestureEvent* e, QPointF local) {
    switch (e->gestureType()) {
        case Qt::ZoomNativeGesture:
            viewport->zoomAt(local, 1.0 + e->value());
            lastGesture = "touchpad pinch";
            break;
        case Qt::EndNativeGesture:
            viewport->pinchEnd();
            break;
        default:
            break;
    }
    return true;
}

QString InputRouter::hudText() const {
    const QString prox = proximityEverSeen ? (penInProximity ? "in" : "out") : "never seen";
    return QString("pen: %1 [%2] p=%3 tilt=(%4,%5) rot=%6 btns=%7 | %8 Hz | proximity %9\n"
                   "touch: %10 pts, %11 Hz | blocked=%12 | palm-rejected=%13 | cancelled-by-pen=%14 | %15\n"
                   "latency event→handler: %16 | event→frame: %17\n"
                   "zoom %18 | strokes %19 | events pen %20 touch %21 mouse %22 (synthesized ignored %23)")
            .arg(lastDevice.isEmpty() ? "-" : lastDevice, lastPointer.isEmpty() ? "-" : lastPointer)
            .arg(lastPressure, 0, 'f', 3)
            .arg(lastXTilt, 0, 'f', 1)
            .arg(lastYTilt, 0, 'f', 1)
            .arg(lastRotation, 0, 'f', 1)
            .arg(lastButtons.isEmpty() ? "-" : lastButtons)
            .arg(penRate.hz(), 0, 'f', 0)
            .arg(prox)
            .arg(touches.size())
            .arg(touchRate.hz(), 0, 'f', 0)
            .arg(touchBlocked() ? "yes" : "no")
            .arg(palmRejected)
            .arg(gesturesCancelled)
            .arg(lastGesture.isEmpty() ? "-" : lastGesture)
            .arg(handlerLatency.summary(), frameLatency.summary())
            .arg(viewport->scale(), 0, 'f', 2)
            .arg(model->strokeCount())
            .arg(penEvents)
            .arg(touchEvents)
            .arg(mouseEvents)
            .arg(synthMouseIgnored);
}
