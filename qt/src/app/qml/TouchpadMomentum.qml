// Touchpad scrolling with momentum for a Flickable (page grid, sidebar, tab overview): Qt Quick's Flickable stops
// dead when the fingers lift. Like the canvas (ViewController), the content keeps the speed of the last movement.
import QtQuick

WheelHandler {
    id: momentum
    required property var flickable  // the Flickable (GridView, ListView); its children are in its contentItem
    acceptedDevices: PointerDevice.TouchPad
    acceptedModifiers: Qt.NoModifier
    target: null
    property real vx: 0
    property real vy: 0
    property double lastTime: 0

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

    onWheel: function(event) {
        const f = flickable
        const now = Date.now()
        if (event.phase === Qt.ScrollBegin || now - lastTime > 200) {
            f.cancelFlick()
            vx = 0
            vy = 0
            lastTime = now
        }
        if (event.phase === Qt.ScrollEnd) {
            if (Math.abs(vx) > 60 || Math.abs(vy) > 60) f.flick(vx, vy)
            vx = 0
            vy = 0
            return
        }
        const dx = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0 ? event.pixelDelta.x : event.angleDelta.x / 4
        const dy = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 4
        if (dx !== 0 && f.contentWidth > f.width)
            f.contentX = clamp(f.contentX - dx, f.originX, f.originX + f.contentWidth - f.width)
        if (dy !== 0 && f.contentHeight > f.height)
            f.contentY = clamp(f.contentY - dy, f.originY, f.originY + f.contentHeight - f.height)
        const dt = Math.max(4, now - lastTime)
        lastTime = now
        // Smoothed velocity in px/s (Flickable.flick: positive moves towards the beginning, like the wheel delta).
        vx = 0.6 * (dx * 1000 / dt) + 0.4 * vx
        vy = 0.6 * (dy * 1000 / dt) + 0.4 * vy
    }
}
