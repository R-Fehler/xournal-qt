// While a recording plays (qt/docs/audio.md, "In the app"): play/pause, 5 s back and forward, a slider with a tick at
// each moment ink was written, the time, and ×. Self-contained.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "playbackPill"
    visible: app.audio.playing && !app.homeVisible && !app.timeline.active  // (the replay has its own bar)
    padding: 2
    leftPadding: 6
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }

    function timeText(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    RowLayout {
        spacing: 2
        IconButton {
            objectName: "playbackBack"
            iconName: "xopp-audio-seek-backwards"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 20
            icon.height: 20
            tip: qsTr("5 seconds back")
            onClicked: app.audio.skip(-5000)
        }
        IconButton {
            objectName: "playbackPlayPause"
            iconName: app.audio.playPaused ? "xqt-play" : "xopp-audio-playback-pause"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 20
            icon.height: 20
            tip: app.audio.playPaused ? qsTr("Play") : qsTr("Pause")
            onClicked: app.audio.playPaused ? app.audio.resumePlayback() : app.audio.pausePlayback()
        }
        IconButton {
            objectName: "playbackForward"
            iconName: "xopp-audio-seek-forwards"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 20
            icon.height: 20
            tip: qsTr("5 seconds forward")
            onClicked: app.audio.skip(5000)
        }
        Slider {
            id: slider
            objectName: "playbackSlider"
            Layout.preferredWidth: 180
            from: 0
            to: Math.max(1, app.audio.playDurationMs)
            value: pressed ? value : app.audio.playPositionMs
            onMoved: app.audio.seek(value)
            // A tick where ink was written
            Repeater {
                model: app.audio.playTicks
                Rectangle {
                    required property var modelData
                    x: slider.leftPadding + slider.availableWidth * modelData / slider.to - 1
                    y: slider.topPadding + slider.availableHeight / 2 + 6
                    width: 2
                    height: 6
                    color: "#3f51b5"
                }
            }
        }
        Label {
            objectName: "playbackTime"
            text: pill.timeText(app.audio.playPositionMs) + " / " + pill.timeText(app.audio.playDurationMs)
            font.features: { "tnum": 1 }
            font.pixelSize: 12
        }
        // The writing with it: the document's timeline from this moment (qt/docs/timeline.md)
        IconButton {
            objectName: "playbackReplay"
            iconName: "xqt-history"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 18
            icon.height: 18
            tip: qsTr("Replay the writing with it")
            onClicked: app.timeline.startAtRecording(app.audio.playName, app.audio.playPositionMs)
        }
        IconButton {
            objectName: "playbackClose"
            iconName: "xqt-close"
            implicitWidth: 40
            implicitHeight: 40
            icon.width: 18
            icon.height: 18
            tip: qsTr("Stop playing")
            onClicked: app.audio.stopPlayback()
        }
    }
}
