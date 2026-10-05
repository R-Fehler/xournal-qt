// The play bar of the document's timeline (qt/docs/timeline.md, "Replay"): the pages as of a moment, read-only. ✕
// (back to the document as it is), the previous / next session, play / pause, a slider with the sessions' marks and the
// recordings as bands under it, the time on the bar and the clock time of the moment, the speed. Self-contained.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: bar
    objectName: "timelineBar"
    visible: app.timeline.active && !app.homeVisible
    padding: 4
    leftPadding: 8
    rightPadding: 8
    Material.foreground: "#303030"
    background: Rectangle {
        radius: 14
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    // (presses on the bar never reach the page under it)
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(w) { w.accepted = true } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            objectName: "timelineTrack"
            Layout.fillWidth: true
            implicitHeight: 34
            Slider {
                id: slider
                objectName: "timelineSlider"
                anchors.fill: parent
                from: 0
                to: Math.max(1, app.timeline.duration)
                focusPolicy: Qt.NoFocus
                value: pressed ? value : app.timeline.position
                onMoved: app.timeline.seek(Math.round(value))
                onPressedChanged: app.timeline.setScrubbing(pressed)
                Accessible.name: qsTr("Time")
            }
            // The recordings: a band under the slider where one is heard (grey: its file is not here)
            Repeater {
                model: app.timeline.tracks
                Rectangle {
                    required property var modelData
                    objectName: "timelineRecording"
                    x: slider.leftPadding + slider.availableWidth * modelData.at / slider.to
                    width: Math.max(2, slider.availableWidth * modelData.length / slider.to)
                    y: slider.topPadding + slider.availableHeight / 2 + 7
                    height: 4
                    radius: 2
                    color: modelData.found ? "#e53935" : "#9e9e9e"
                    opacity: 0.7
                }
            }
            // The sessions: a mark where each begins
            Repeater {
                model: app.timeline.marks
                Rectangle {
                    required property var modelData
                    objectName: "timelineMark"
                    x: slider.leftPadding + slider.availableWidth * modelData.at / slider.to - 1
                    y: slider.topPadding + slider.availableHeight / 2 - 11
                    width: 2
                    height: 8
                    color: "#3f51b5"
                    HoverHandler { id: markHover }
                    ToolTip.visible: markHover.hovered
                    ToolTip.text: modelData.label
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            IconButton {
                objectName: "timelineClose"
                iconName: "xqt-close"
                implicitWidth: 40
                implicitHeight: 40
                icon.width: 18
                icon.height: 18
                tip: qsTr("Stop the replay: the document as it is (Esc)")
                label: qsTr("Stop the replay")
                onClicked: app.timeline.stop()
            }
            IconButton {
                objectName: "timelinePreviousMark"
                iconName: "xopp-audio-seek-backwards"
                implicitWidth: 40
                implicitHeight: 40
                icon.width: 20
                icon.height: 20
                tip: qsTr("The start of this session, or the one before")
                label: qsTr("Session before")
                onClicked: app.timeline.previousMark()
            }
            IconButton {
                objectName: "timelinePlay"
                iconName: app.timeline.playing ? "xopp-audio-playback-pause" : "xqt-play"
                implicitWidth: 44
                implicitHeight: 44
                icon.width: 24
                icon.height: 24
                tip: app.timeline.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                label: app.timeline.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: app.timeline.toggle()
            }
            IconButton {
                objectName: "timelineNextMark"
                iconName: "xopp-audio-seek-forwards"
                implicitWidth: 40
                implicitHeight: 40
                icon.width: 20
                icon.height: 20
                tip: qsTr("The next session")
                label: qsTr("Next session")
                onClicked: app.timeline.nextMark()
            }
            ToolButton {
                objectName: "timelineSpeed"
                focusPolicy: Qt.NoFocus
                text: (app.timeline.speed === 0.5 ? "½" : app.timeline.speed) + "×"
                font.pixelSize: 13
                font.features: { "tnum": 1 }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Speed (recordings are heard at 1×)")
                ToolTip.delay: 600
                onClicked: app.timeline.cycleSpeed()
            }
            Label {
                objectName: "timelinePosition"
                text: app.timeline.positionText
                font.features: { "tnum": 1 }
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "timelineWhen"
                text: app.timeline.timeText
                elide: Text.ElideLeft
                Layout.maximumWidth: Math.max(80, bar.width - 400)
                font.pixelSize: 12
                color: "#606060"
            }
        }
    }
}
