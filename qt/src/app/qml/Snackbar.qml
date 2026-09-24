// Short note at the bottom of the window ("3 pages deleted") with Undo for page operations.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    property string text
    property bool undoable: false
    /// Another action instead of Undo (e.g. "Copy"), and what it does
    property string actionText: ""
    property var action: null
    function show(message, canUndo, actionName, actionCall) {
        text = message
        undoable = canUndo
        actionText = actionName || ""
        action = actionCall || null
        visible = true
        hideTimer.restart()
    }
    visible: false
    radius: 8
    color: "#323232"
    implicitHeight: Math.max(48, row.implicitHeight + 16)
    implicitWidth: row.implicitWidth + 32

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 16
        Label {
            objectName: "snackbarText"
            text: bar.text
            color: "#ffffff"
            wrapMode: Text.Wrap  // (a longer note, e.g. where pasted PDF pages are kept)
            Layout.maximumWidth: bar.parent ? Math.max(200, bar.parent.width - 160) : 560
        }
        Button {
            objectName: "snackbarUndo"
            visible: bar.undoable
            text: qsTr("Undo")
            flat: true
            Material.foreground: "#8ab4f8"
            onClicked: {
                app.undoPages()
                bar.visible = false
            }
        }
        Button {
            objectName: "snackbarAction"
            visible: bar.actionText !== "" && !bar.undoable
            text: bar.actionText
            flat: true
            Material.foreground: "#8ab4f8"
            onClicked: {
                if (bar.action) bar.action()
                bar.visible = false
            }
        }
    }
    Timer {
        id: hideTimer
        interval: bar.text.length > 60 || bar.actionText !== "" ? 9000 : 5000  // time to read a longer note
        onTriggered: bar.visible = false
    }
}
