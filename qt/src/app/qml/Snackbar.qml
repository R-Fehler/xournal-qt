// Short note at the bottom of the window ("3 pages deleted") with Undo for page operations.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    property string text
    property bool undoable: false
    function show(message, canUndo) {
        text = message
        undoable = canUndo
        visible = true
        hideTimer.restart()
    }
    visible: false
    radius: 8
    color: "#323232"
    implicitHeight: 48
    implicitWidth: row.implicitWidth + 32

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 16
        Label { objectName: "snackbarText"; text: bar.text; color: "#ffffff" }
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
    }
    Timer {
        id: hideTimer
        interval: 5000
        onTriggered: bar.visible = false
    }
}
