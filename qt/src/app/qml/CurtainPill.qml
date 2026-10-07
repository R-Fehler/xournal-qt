// While the curtain or the spotlight is out (qt/docs/curtain.md; B / Shift+B, the setsquare button's list, ⋮ → View): a
// small pill at the top right of the canvas, below the setsquare's. Its icon shows or hides the handles (a tap on the
// black does that too, Esc hides them); the next one puts out the other of the two instead; its × takes it away.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "curtainPill"
    visible: app.curtain !== "" && !app.homeVisible && !win.modes.hudHidden  // (presenting without controls: none)
    padding: 2
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }

    readonly property bool spotlight: app.curtain === "spotlight"

    RowLayout {
        spacing: 0
        // The handles: shown (highlighted) or hidden
        IconButton {
            objectName: "curtainHandles"
            iconName: pill.spotlight ? "xqt-spotlight" : "xqt-curtain"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            checked: app.curtainHandles
            tip: checked ? qsTr("Hide the handles (Esc)")
                         : pill.spotlight ? qsTr("Move, turn or size the spotlight") : qsTr("Move, turn or size the curtain")
            onClicked: app.curtainHandles = !app.curtainHandles
        }
        // The other one instead
        IconButton {
            objectName: "curtainShape"
            iconName: pill.spotlight ? "xqt-curtain" : "xqt-spotlight"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            tip: pill.spotlight ? qsTr("The curtain instead") : qsTr("The spotlight instead") + win.keyNote("spotlight")
            onClicked: app.toggleCurtain(pill.spotlight ? "curtain" : "spotlight")
        }
        ToolSeparator {}
        IconButton {
            objectName: "curtainClose"
            iconName: "xqt-close"
            label: pill.spotlight ? qsTr("Take the spotlight away (B)") : qsTr("Take the curtain away (B)")
            tip: label
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 20
            icon.height: 20
            onClicked: app.toggleCurtain("")
        }
    }
}
