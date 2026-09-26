// The handle at the top of a menu's bottom sheet (MenuSheet, PageMenu on phones): a short bar; dragged down, the sheet
// follows (`offset`), and let go far enough it closes (`dismissed`), else it springs back.
import QtQuick

Item {
    id: handle
    /// How far the sheet is dragged down now
    property real offset: 0
    /// How far it must be dragged to close
    property real closeDistance: 80
    signal dismissed()

    implicitHeight: 22
    Rectangle {
        anchors.centerIn: parent
        width: 32
        height: 4
        radius: 2
        color: "#c4c7cc"
    }
    DragHandler {
        id: drag
        target: null
        xAxis.enabled: false
        // (in the window's coordinates: the sheet moves under the finger)
        readonly property real dy: centroid.scenePosition.y - centroid.scenePressPosition.y
        onDyChanged: if (active) handle.offset = Math.max(0, dy)
        onActiveChanged: {
            if (active) return
            if (handle.offset > handle.closeDistance) handle.dismissed()
            else handle.offset = 0
        }
    }
}
