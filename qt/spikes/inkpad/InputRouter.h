/*
 * xournal-qt M0 spike: host-agnostic input handling.
 *
 * Both canvas hosts (QQuickItem, QWidget) forward raw Qt events plus the canvas-local position here.
 * Implements: pressure strokes, eraser end, barrel-button pan, hover dot, 1-finger pan with momentum,
 * 2-finger anchored pinch, 2/3-finger tap = undo/redo, Krita-style palm rejection (pen near/active
 * blocks touch, pen-down cancels touch gestures), wheel / Ctrl+wheel / touchpad pinch.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <atomic>
#include <map>
#include <vector>
#include <optional>

#include <QObject>
#include <QPointF>
#include <QString>

#include "Diagnostics.h"

class InkModel;
class Viewport;
class QTabletEvent;
class QTouchEvent;
class QMouseEvent;
class QWheelEvent;
class QNativeGestureEvent;

class InputRouter: public QObject {
    Q_OBJECT
public:
    InputRouter(InkModel* model, Viewport* viewport, QObject* parent = nullptr);

    using MapToLocal = std::function<QPointF(QPointF)>;

    // All handlers return true if the event was consumed. `local` is in canvas logical coordinates.
    bool tablet(QTabletEvent* e, QPointF local);
    bool touch(QTouchEvent* e, const MapToLocal& sceneToLocal);
    bool mouse(QMouseEvent* e, QPointF local);
    bool wheel(QWheelEvent* e, QPointF local);
    bool nativeGesture(QNativeGestureEvent* e, QPointF local);
    void proximity(bool entered);

    std::optional<QPointF> hoverPos() const { return hover; }
    bool isEraserHover() const { return hoverIsEraser; }

    // Latency tracking. The host calls syncPoint() when it takes a snapshot of the buffer for display
    // and framePresented() when that frame has been swapped/flushed. Thread-safe.
    void syncPoint();
    void framePresented();

    QString hudText() const;

Q_SIGNALS:
    void hoverChanged();

private:
    enum class PenMode { None, Draw, Erase, Pan };
    bool touchBlocked() const;
    void cancelTouchGesture();
    void noteInkEvent(ulong timestamp);

    InkModel* model;
    Viewport* viewport;

    // pen state
    PenMode penMode = PenMode::None;
    bool penInProximity = false;
    bool proximityEverSeen = false;
    double lastPenEventMs = -1e9;
    QPointF lastPanPos;
    std::optional<QPointF> hover;
    bool hoverIsEraser = false;

    // touch gesture state (one session from first finger down to last finger up)
    struct TouchPt {
        QPointF start, pos;
    };
    std::map<int, TouchPt> touches;
    bool touchSessionIgnored = false;
    int touchSessionMaxPoints = 0;
    double touchSessionStartMs = 0;
    double touchSessionTravel = 0;
    bool pinching = false;
    bool panning = false;
    QPointF lastCentroid;
    struct VelSample {
        double t;
        QPointF pos;
    };
    std::vector<VelSample> velSamples;

    // stats for the HUD
    RateCounter penRate, touchRate;
    LatencyStats handlerLatency;  // event timestamp -> handled
    LatencyStats frameLatency;    // event timestamp -> frame presented
    std::atomic<double> pendingTs{-1};
    std::atomic<double> inFlightTs{-1};
    QString lastDevice, lastPointer, lastButtons;
    double lastPressure = 0, lastXTilt = 0, lastYTilt = 0, lastRotation = 0;
    int penEvents = 0, touchEvents = 0, mouseEvents = 0, synthMouseIgnored = 0;
    int palmRejected = 0, gesturesCancelled = 0;
    QString lastGesture;
};
