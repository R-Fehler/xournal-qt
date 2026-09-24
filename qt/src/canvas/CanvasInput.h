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
    /// A place on the canvas in the coordinates of a page (points).
    QPointF pageCoordinates(CanvasPage& page, QPointF viewPos) const;
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
    bool readOnlyPress = false;  ///< a press on a read-only document: it only scrolls (and a tap follows a link)
    bool textPress = false;      ///< a press on a text file edited (CanvasView::textMode): cursor, drag selects
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
    /// The last moment the pen was near (touching, or in proximity and not higher than the setting)
    double lastNearMs = -1e9;
    bool penNear() const;
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
    /// The same for the pen with the pen, highlighter or hand in hand: pressed and held still (it may shake a little)
    /// for the time of a long press. The dot it began is taken back, and the pen does nothing more until it is
    /// lifted. A pen that moved further first is writing: resting afterwards is not a long press.
    QTimer penHoldTimer;
    bool penHoldFired = false;
    QPointF penHoldPos;
    bool penHoldTool() const;
    void startPenHold(const Event& event);
    void penHeld();
    /// The setsquare / compass is being dragged over the page
    bool draggingGeometryTool = false;
    QPointF lastGeometryPos;
    /// A finger on a selection of elements moves, resizes or rotates it (instead of scrolling the page).
    bool startTouchSelection(QPointF viewPos);
    void moveTouchSelection(QPointF viewPos);
    void endTouchSelection();
    bool touchSelection = false;
    int touchSelectionId = -1;
    /// Two fingers on the setsquare / compass turn and size it instead of zooming the page
    /// A touch that went down with two fingers on the setsquare / compass: it belongs to the tool until the last
    /// finger is up (no scrolling, zooming, tapping or undo meanwhile)
    bool toolGesture = false;
    /// The two fingers the tool follows; other ones (a finger lifted, another one down) start measuring anew
    std::pair<int, int> toolGestureFingers{-1, -1};
    /// The setsquare / compass under this place of the view (in the coordinates of its page)?
    bool onGeometryTool(QPointF viewPos) const;
    QPointF onGeometryPage(QPointF viewPos) const;
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
    /// Snapping to pages: wheel notches not yet turned into a page (120 each), and the pause after wheel scrolling
    /// within a zoomed-in page before it comes to rest
    double wheelPages = 0;
    QTimer wheelSnapTimer;
};

}  // namespace xqt
