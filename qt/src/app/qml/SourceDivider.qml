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
    visible: win.sourcePanel !== null && win.sourceAtBottom && !app.homeVisible
    x: win.sourcePanel ? win.sourcePanel.x : 0
    width: win.sourcePanel ? win.sourcePanel.width : 0
    height: 24
    y: (win.sourcePanel ? win.sourcePanel.y : 0) - height / 2
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
                startTop = win.sourcePanel.y
                win.sourceShareLive = win.sourcePageShare
            } else {
                const share = win.sourceShareLive
                win.sourceShareLive = -1
                const auto = win.adaptive.phone ? 0.4 : 0.5
                win.chooseLayout("sourceSplit", Math.abs(share - auto) < 0.01 ? "" : share.toFixed(3))
            }
        }
        onTranslationChanged: {
            if (!active) return
            const h = Math.max(1, sourceDivider.parent.height)
            win.sourceShareLive = Math.max(0.2, Math.min(0.8, (startTop + translation.y) / h))
        }
    }
}
