// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Between the page and its source below it: dragged, the page's share of the height is remembered for this size
// class (a grip in the middle, touch sized; the whole edge takes a drag)
Item {
    id: sourceDivider
    objectName: "sourceDivider"
    visible: win.layout.sourcePanel !== null && win.layout.sourceAtBottom && !app.homeVisible
    x: win.layout.sourcePanel ? win.layout.sourcePanel.x : 0
    width: win.layout.sourcePanel ? win.layout.sourcePanel.width : 0
    height: 24
    y: (win.layout.sourcePanel ? win.layout.sourcePanel.y : 0) - height / 2
    z: 3
    Rectangle {
        objectName: "sourceDividerGrip"
        anchors.centerIn: parent
        width: 72
        height: 16
        radius: 8
        color: sourceDrag.active ? Material.accentColor : "#e8eaed"
        border.width: 1
        border.color: "#80000000"
        Row {
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: 3
                Rectangle { width: 2; height: 6; radius: 1; color: sourceDrag.active ? "#ffffff" : "#5f6368" }
            }
        }
    }
    HoverHandler { cursorShape: Qt.SplitVCursor }
    DragHandler {
        id: sourceDrag
        target: null
        xAxis.enabled: false
        property real startTop: 0
        onActiveChanged: {
            if (active) {
                startTop = win.layout.sourcePanel.y
                win.layout.sourceShareLive = win.layout.sourcePageShare
            } else {
                const share = win.layout.sourceShareLive
                win.layout.sourceShareLive = -1
                const auto = win.adaptive.phone ? 0.4 : 0.5
                win.chooseLayout("sourceSplit", Math.abs(share - auto) < 0.01 ? "" : share.toFixed(3))
            }
        }
        onTranslationChanged: {
            if (!active) return
            const h = Math.max(1, sourceDivider.parent.height)
            win.layout.sourceShareLive = Math.max(0.2, Math.min(0.8, (startTop + translation.y) / h))
        }
    }
}
