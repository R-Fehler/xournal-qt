// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Zen's only mark: a small faint dot in the lower left corner of the page (10 px; faint after 2 s, clearer while the
// mouse or the pen is near), a finger-wide target, clear of the safe area. A tap opens its pill beside it.
// (Presenting with the controls has none: Ctrl+F5 or the floating toolbox's ⋯ hide them.)
AbstractButton {
    id: zenDot
    objectName: "zenDot"
    visible: win.zenShown && !pageGrid.visible && !contentsOverview.visible
    z: 91
    x: win.canvasControlsLeft
    y: win.canvasControlsBottom - height
    width: 48
    height: 48
    focusPolicy: Qt.NoFocus  // (the keys stay with the page)
    hoverEnabled: true
    Accessible.name: qsTr("Show controls, read only, the page")
    ToolTip.visible: hovered && !pressed && !zenPill.visible
    ToolTip.text: qsTr("Show controls, read only, the page")
    ToolTip.delay: 600
    /// 2 s after it showed, or after the pointer went away from it: faint
    property bool resting: false
    /// The mouse or the pen near it (or its pill open): clearer
    readonly property bool near: hovered || zenNear.hovered || pressed || zenPill.visible
    function wake() {
        resting = false
        restTimer.restart()
    }
    onVisibleChanged: if (visible) wake()
    onNearChanged: if (!near) wake()
    Timer { id: restTimer; interval: 2000; onTriggered: zenDot.resting = true }
    background: null
    contentItem: Item {
        Rectangle {
            objectName: "zenDotMark"
            width: 10
            height: 10
            radius: 5
            x: 13 - width / 2
            y: parent.height - 13 - height / 2
            // grey with a light rim: as faint on a white page as on the dark around it
            color: "#80868b"
            border.width: 1
            border.color: "#99ffffff"
            opacity: zenDot.near ? 0.9 : zenDot.resting ? 0.2 : 0.6
            Behavior on opacity { NumberAnimation { duration: 250 } }
        }
    }
    onClicked: zenPill.opened = !zenPill.opened
}
