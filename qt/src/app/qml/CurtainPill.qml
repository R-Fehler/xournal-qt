// While the curtain is out (qt/docs/curtain.md; B, the setsquare button's list, ⋮ → View): a small pill at the top right
// of the canvas, below the setsquare's. Its icon shows or hides the curtain's handles (a tap on the black does that too,
// Esc hides them); its × takes the curtain away.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "curtainPill"
    visible: app.curtain !== "" && !app.homeVisible && !win.hudHidden  // (presenting without controls: none)
    padding: 2
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }

    RowLayout {
        spacing: 0
        // The handles: shown (highlighted) or hidden
        IconButton {
            objectName: "curtainHandles"
            iconName: "xqt-curtain"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            checked: app.curtainHandles
            tip: checked ? qsTr("Hide the handles (Esc)") : qsTr("Move, turn or size the curtain")
            onClicked: app.curtainHandles = !app.curtainHandles
        }
        ToolSeparator {}
        IconButton {
            objectName: "curtainClose"
            iconName: "xqt-close"
            label: qsTr("Take the curtain away (B)")
            tip: label
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 20
            icon.height: 20
            onClicked: app.toggleCurtain("")
        }
    }
}
