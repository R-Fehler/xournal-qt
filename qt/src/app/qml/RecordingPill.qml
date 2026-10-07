// While a recording runs (qt/docs/features/audio.md, "In the app"): a red pill at the top of the canvas with the time,
// the level, pause/resume and stop; a warning when the first seconds were silent (a muted or wrong microphone). In
// another tab it says which document the recording is for. Self-contained (RecordButton.qml starts the recording).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "recordingPill"
    visible: app.audio.recording && !app.homeVisible
    padding: 2
    leftPadding: 12
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#fff5f5"
        border.width: 1
        border.color: "#d32f2f"
    }

    function timeText(ms) {
        const s = Math.floor(ms / 1000)
        const two = function(n) { return (n < 10 ? "0" : "") + n }
        return s >= 3600 ? Math.floor(s / 3600) + ":" + two(Math.floor(s / 60) % 60) + ":" + two(s % 60)
                         : Math.floor(s / 60) + ":" + two(s % 60)
    }

    RowLayout {
        spacing: 6
        // The red dot (it blinks while paused)
        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: "#d32f2f"
            opacity: app.audio.recordingPaused ? (blink.on ? 1 : 0.2) : 1
            Timer {
                id: blink
                property bool on: true
                interval: 500
                repeat: true
                running: app.audio.recordingPaused && pill.visible
                onTriggered: on = !on
            }
        }
        Label {
            objectName: "recordingTime"
            text: pill.timeText(app.audio.recordedMs)
            font.features: { "tnum": 1 }
        }
        // The level: the loudest of the last moment
        Rectangle {
            objectName: "recordingLevel"
            Layout.preferredWidth: 40
            height: 6
            radius: 3
            color: "#e0e0e0"
            Rectangle {
                width: parent.width * Math.min(1, Math.sqrt(app.audio.level))
                height: parent.height
                radius: 3
                color: app.audio.level > 0.9 ? "#d32f2f" : "#43a047"
            }
        }
        Label {
            objectName: "recordingSilent"
            visible: app.audio.silent
            text: qsTr("No sound: is the microphone on?")
            color: "#d32f2f"
            font.pixelSize: 12
        }
        Label {
            objectName: "recordingElsewhere"
            visible: !app.audio.recordingHere
            text: qsTr("for %1").arg(app.audio.recordingTitle)
            elide: Text.ElideMiddle
            Layout.maximumWidth: 160
            font.pixelSize: 12
            color: "#80868b"
        }
        IconButton {
            objectName: "recordingPause"
            iconName: app.audio.recordingPaused ? "xqt-mic" : "xopp-audio-playback-pause"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 20
            icon.height: 20
            tip: app.audio.recordingPaused ? qsTr("Go on recording") : qsTr("Pause the recording")
            onClicked: app.audio.recordingPaused ? app.audio.resumeRecording() : app.audio.pauseRecording()
        }
        IconButton {
            objectName: "recordingStop"
            iconName: "xopp-audio-playback-stop"
            icon.color: "#d32f2f"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 20
            icon.height: 20
            tip: qsTr("Stop recording") + win.keyNote("record")
            onClicked: app.audio.stopRecording()
        }
    }
}
