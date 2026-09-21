import QtQuick

// Whether a list of pages races (flicked, wheeled or its scroll bar dragged faster than `viewports` of its height per
// second): its pages then show only their sketches, the sharp thumbnails are asked for once it slows down.
QtObject {
    id: watch
    required property Flickable flickable
    property real viewports: 3
    property bool racing: false

    property real lastY: 0
    property double lastTime: 0
    readonly property Connections moves: Connections {
        target: watch.flickable
        function onContentYChanged() {
            const now = Date.now()
            const dt = now - watch.lastTime
            if (dt > 0 && dt < 250)
                watch.racing = Math.abs(watch.flickable.contentY - watch.lastY) * 1000 / dt
                               > watch.flickable.height * watch.viewports
            watch.lastY = watch.flickable.contentY
            watch.lastTime = now
            calm.restart()
        }
    }
    readonly property Timer calm: Timer {
        id: calm
        interval: 150
        onTriggered: watch.racing = false
    }
}
