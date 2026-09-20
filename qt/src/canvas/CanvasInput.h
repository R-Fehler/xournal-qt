/*
 * xournal-qt: input handling of a canvas (one per tab).
 *
 * Pen and mouse: port of upstream's PenInputHandler / StylusInputHandler / MouseInputHandler (gui/inputdevices):
 * tool switching through ButtonConfig (eraser end / side button, barrel buttons, mouse buttons), pressure filtering
 * (minimum pressure, multiplier, pressure inference), page crossing for multi-page tools and forwarding to the
 * page (CanvasPage, which runs upstream's StrokeHandler / EraseHandler).
 *
 * Touchpad: two-finger scrolling with momentum after the fingers are lifted (Wayland reports no kinetic scrolling,
 * GTK/upstream Xournal++ add it themselves), pinch zoom; Ctrl + wheel zooms.
 *
 * Touch: navigation (pan with momentum, anchored pinch zoom), 2-finger tap = undo, 3-finger tap = redo, and
 * Krita-style palm rejection: touch is ignored while the pen is in proximity, pressed or was used recently, and a
 * pen press cancels a running touch gesture. Touch drawing is not supported yet (upstream default: off).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <QObject>
#include <QTimer>
#include <QPointF>

#include "gui/inputdevices/PositionInputData.h"

class QMouseEvent;
class QNativeGestureEvent;
class QTabletEvent;
class QTouchEvent;
class QWheelEvent;

namespace xqt {

class CanvasPage;
class CanvasView;

class CanvasInput: public QObject {
    Q_OBJECT
public:
    explicit CanvasInput(CanvasView& view, QObject* parent = nullptr);

    using MapToView = std::function<QPointF(QPointF scenePos)>;
    // All positions are in view (canvas item) coordinates. Return true if the event was consumed.
    bool tabletEvent(QTabletEvent* e, QPointF viewPos);
    bool touchEvent(QTouchEvent* e, const MapToView& sceneToView);
    bool mouseEvent(QMouseEvent* e, QPointF viewPos);
    bool wheelEvent(QWheelEvent* e, QPointF viewPos);
    bool nativeGestureEvent(QNativeGestureEvent* e, QPointF viewPos);
    void proximityEvent(bool entered);

    std::optional<QPointF> hoverPosition() const { return hover; }
    bool hoverIsEraser() const { return hoverEraser; }

Q_SIGNALS:
    void hoverChanged();

private:
    enum class DeviceClass { Pen, Eraser, Mouse };
    struct Event {
        DeviceClass deviceClass = DeviceClass::Pen;
        QPointF viewPos;
        double pressure = -1.0;  // Point::NO_PRESSURE
        guint32 timestamp = 0;
        const void* device = nullptr;
        GdkModifierType state = GdkModifierType(0);
    };
    enum class PressureMode { NO_PRESSURE, DEVICE_PRESSURE, INFERRED_PRESSURE };

    // --- port of PenInputHandler ---
    bool actionStart(const Event& event);
    bool actionMotion(const Event& event);
    bool actionEnd(const Event& event);
    bool changeTool(const Event& event);
    void updateLastEvent(const Event& event);
    PositionInputData getInputDataRelativeToCurrentPage(CanvasPage* page, const Event& event) const;
    double filterPressure(const PositionInputData& pos, CanvasPage* page);
    double inferPressureValue(const PositionInputData& pos, CanvasPage* page);
    void handleScrollEvent(const Event& event);

    // --- touch ---
    bool touchBlocked() const;
    void cancelTouchGesture();
    void undo();
    void redo();

    CanvasView& view;

    // pen / mouse state (upstream PenInputHandler members)
    bool deviceClassPressed = false;
    bool modifier2 = false;  ///< first barrel button (upstream: button 2)
    bool modifier3 = false;  ///< second barrel button (upstream: button 3)
    bool inputRunning = false;
    std::optional<Event> lastEvent;
    std::optional<Event> lastHitEvent;
    CanvasPage* sequenceStartPage = nullptr;
    PressureMode pressureMode = PressureMode::NO_PRESSURE;
    double lastPressure = 0.0;
    std::optional<DeviceClass> runningDeviceClass;
    bool mousePanning = false;

    // pen proximity / hover / palm rejection
    bool penInProximity = false;
    bool proximityEverSeen = false;
    double lastPenEventMs = -1e9;
    std::optional<QPointF> hover;
    bool hoverEraser = false;

    // touch gesture state (one session from the first finger down to the last finger up)
    struct TouchPoint {
        QPointF pos;
    };
    std::map<int, TouchPoint> touches;
    bool touchSessionIgnored = false;
    int touchSessionMaxPoints = 0;
    double touchSessionStartMs = 0;
    // Two taps in the same spot zoom in (or back out). A tap that opened a PDF link is not the first of them.
    double lastTapMs = 0;
    QPointF lastTapPos;
    /// Holding one finger still shows what can be done here (like a right click)
    QTimer longPressTimer;
    bool longPressFired = false;
    double touchSessionTravel = 0;
    QPointF touchSessionStartPos;
    QPointF pressViewPos;
    double pressTimeMs = 0;
    bool pinching = false;
    double pinchStartDistance = 1;
    bool panning = false;
    QPointF lastCentroid;
    struct VelocitySample {
        double t;
        QPointF pos;
    };
    std::vector<VelocitySample> velocitySamples;

    // touchpad scrolling: recent deltas, for momentum when the fingers are lifted
    struct WheelSample {
        double t;
        QPointF delta;
    };
    std::vector<WheelSample> wheelSamples;
};

}  // namespace xqt
