// The recordings of the document (qt/docs/audio.md, "In the app"): each with its pages, its length and how much ink
// was written while it ran; play it, or remove it from the document (its ink stays, without the recording; undoable;
// the file stays in the audio folder).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "recordingsDialog"
    title: qsTr("Recordings")
    kind: "form"
    preferredWidth: 460
    closeButton: true
    property var items: []
    function openList() {
        items = app.audio.recordings()
        open()
    }
    function lengthText(ms) {
        if (ms < 0) return qsTr("file missing")
        const s = Math.round(ms / 1000)
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Close"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    ColumnLayout {
        width: dlg.availableWidth
        Label {
            visible: dlg.items.length === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#80868b"
            text: qsTr("No recordings yet. The microphone button records; ink written meanwhile plays its moment.")
        }
        Repeater {
            model: dlg.items
            RowLayout {
                required property var modelData
                required property int index
                objectName: "recordingRow" + index
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: modelData.title; font.bold: true }
                    Label {
                        font.pixelSize: 12
                        color: "#80868b"
                        text: qsTr("Pages %1 · %2 · %n with ink", "", modelData.elements)
                              .arg(modelData.pages).arg(dlg.lengthText(modelData.durationMs))
                    }
                }
                IconButton {
                    objectName: "recordingPlay" + index
                    iconName: "xqt-play"
                    enabled: modelData.found
                    tip: qsTr("Play")
                    onClicked: { app.audio.play(modelData.name, 0); dlg.close() }
                }
                IconButton {
                    objectName: "recordingRemove" + index
                    iconName: "xqt-close"
                    tip: qsTr("Remove from the document (the ink stays)")
                    onClicked: {
                        app.audio.removeRecording(modelData.name)
                        dlg.items = app.audio.recordings()
                    }
                }
            }
        }
    }
}
