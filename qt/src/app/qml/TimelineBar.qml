// The play bar of the document's timeline (qt/docs/timeline.md, "The play bar"): the pages as of a moment, read-only.
// A title ("Replay"; a tap: what it is), the time "12:04 · 3 Oct, 14:20" (the bar's time and the clock time of the
// moment), the previous / next session, play / pause, a slider with a large handle and its elapsed part filled, the
// sessions as ticks (their dates as tips) and the recordings as bands under it, the speed and ✕. One row on a wide bar;
// two on a narrow one (a phone: the slider on its own row). Touch-sized (48 px) in the touch profile. The first replay
// shows a hint above the bar, once (setting replayHintSeen). Self-contained: Main.qml places it and sets `touch`.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: bar
    objectName: "timelineBar"
    visible: app.timeline.active && !app.homeVisible
    /// Fingers are in use (win.adaptive.touchProfile): 48 px targets and a larger handle
    property bool touch: false
    /// The size of a control: 48 px in the touch profile (the play button a little more), 40 otherwise
    readonly property real target: touch ? 48 : 40
    readonly property real handleSize: touch ? 26 : 18
    /// Two rows (the slider on its own): a narrow bar, as on a phone
    readonly property bool twoRows: width < 640
    /// The hint of the first replay is shown (above the bar)
    property bool hintOpen: false
    /// (the gap above the bar and beside the hint are the page's, not the bar's: DocumentCanvasItem::claims)
    property bool inputTransparent: true
    /// "12:04 · 3 Oct, 14:20": the bar's time and the clock time of the moment
    readonly property string timeText: app.timeline.elapsedText
                                       + (app.timeline.momentText !== "" ? " · " + app.timeline.momentText : "")

    // Colors for both themes: the bar is opaque with a clear edge, the elapsed part and the handle in the accent
    readonly property bool dark: Material.theme === Material.Dark
    readonly property color surface: dark ? "#2b2d31" : "#ffffff"
    readonly property color ink: dark ? "#f1f3f4" : "#1f1f1f"
    readonly property color muted: dark ? "#bdc1c6" : "#5f6368"
    readonly property color edge: dark ? "#66ffffff" : "#40000000"
    readonly property color trackColor: dark ? "#6b7078" : "#c4c7cc"
    readonly property color accent: dark ? "#9fa8da" : Material.accentColor
    readonly property color onAccent: dark ? "#1a1a1a" : "#ffffff"

    implicitHeight: column.implicitHeight

    /// The first replay: what it is and how it is used, once
    Connections {
        target: app.timeline
        function onActiveChanged() {
            if (!app.timeline.active) {
                bar.hintOpen = false
            } else if (!app.settings.get("replayHintSeen")) {
                bar.hintOpen = true
                app.settings.set("replayHintSeen", true)
            }
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: 8

        // The hint (the first replay; a tap on the title shows it again)
        Pane {
            id: hint
            objectName: "timelineHint"
            visible: bar.hintOpen
            width: Math.min(parent.width, 460)
            anchors.horizontalCenter: parent.horizontalCenter
            padding: 14
            topPadding: 12
            bottomPadding: 4
            Material.foreground: bar.ink
            background: Rectangle {
                radius: 14
                color: bar.surface
                border.width: 1
                border.color: bar.edge
            }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(w) { w.accepted = true } }
            contentHeight: hintColumn.implicitHeight
            ColumnLayout {
                id: hintColumn
                width: parent.width
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Replaying how this document was written; your document isn't changed.")
                    font.pixelSize: 14
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Drag the bar or press ▶ to play. The marks on the bar are the sessions it was written in. ✕ leaves the replay.")
                    font.pixelSize: 13
                    color: bar.muted
                    wrapMode: Text.WordWrap
                }
                Button {
                    objectName: "timelineHintClose"
                    Layout.alignment: Qt.AlignRight
                    flat: true
                    focusPolicy: Qt.NoFocus
                    implicitHeight: bar.target
                    text: qsTr("Got it")
                    Material.foreground: bar.accent
                    onClicked: bar.hintOpen = false
                }
            }
        }

        Pane {
            id: pane
            objectName: "timelinePane"
            width: parent.width
            padding: 4
            leftPadding: 10
            rightPadding: 6
            Material.foreground: bar.ink
            background: Item {
                Rectangle {  // (a soft shadow: the bar stands out on any page)
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: -2
                    radius: 16
                    color: "#26000000"
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 16
                    color: bar.surface
                    border.width: 1
                    border.color: bar.edge
                }
            }
            // (presses on the bar never reach the page under it)
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(w) { w.accepted = true } }

            contentWidth: grid.implicitWidth
            contentHeight: grid.implicitHeight
            GridLayout {
                id: grid
                anchors.fill: parent
                columns: 7
                rowSpacing: 0
                columnSpacing: bar.twoRows ? 0 : 2

                // The title and the time: "Replay" over "12:04 · 3 Oct, 14:20" (two rows: the time alone, the title
                // before the slider); a tap shows the hint
                AbstractButton {
                    objectName: "timelineTitle"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 0
                    Layout.fillWidth: bar.twoRows
                    Layout.preferredWidth: bar.twoRows ? -1 : 168
                    Layout.minimumWidth: 0
                    implicitHeight: bar.target
                    focusPolicy: Qt.NoFocus
                    onClicked: bar.hintOpen = !bar.hintOpen
                    ToolTip.visible: hovered && !bar.hintOpen
                    ToolTip.text: app.timeline.positionText + "\n" + app.timeline.timeText + "\n" + qsTr("A click: what this is")
                    ToolTip.delay: 600
                    Accessible.name: qsTr("Replay: what it is")
                    // (two rows: the time alone, in the middle of the row)
                    topPadding: bar.twoRows ? Math.max(0, (bar.target - contentItem.implicitHeight) / 2) : 4
                    bottomPadding: 4
                    contentItem: Column {
                        spacing: 0
                        Label {
                            visible: !bar.twoRows
                            width: parent.width
                            text: qsTr("Replay")
                            font.pixelSize: 14
                            font.bold: true
                            color: bar.accent
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "timelineTime"
                            width: parent.width
                            text: bar.timeText
                            font.pixelSize: 13
                            font.features: { "tnum": 1 }
                            color: bar.ink
                            elide: Text.ElideRight
                        }
                    }
                }
                IconButton {
                    objectName: "timelinePreviousMark"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 1
                    iconName: "xopp-audio-seek-backwards"
                    implicitWidth: bar.target
                    implicitHeight: bar.target
                    icon.width: 22
                    icon.height: 22
                    icon.color: bar.ink
                    tip: qsTr("The start of this session, or the one before")
                    label: qsTr("Session before")
                    onClicked: app.timeline.previousMark()
                }
                IconButton {
                    id: playButton
                    objectName: "timelinePlay"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 2
                    iconName: app.timeline.playing ? "xopp-audio-playback-pause" : "xqt-play"
                    implicitWidth: bar.target + 4
                    implicitHeight: bar.target + 4
                    icon.width: 24
                    icon.height: 24
                    icon.color: bar.onAccent
                    tip: app.timeline.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                    label: app.timeline.playing ? qsTr("Pause") : qsTr("Play")
                    background: Rectangle {
                        radius: width / 2
                        color: bar.accent
                        opacity: playButton.pressed ? 0.75 : 1
                    }
                    onClicked: app.timeline.toggle()
                }
                IconButton {
                    objectName: "timelineNextMark"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 3
                    iconName: "xopp-audio-seek-forwards"
                    implicitWidth: bar.target
                    implicitHeight: bar.target
                    icon.width: 22
                    icon.height: 22
                    icon.color: bar.ink
                    tip: qsTr("The next session")
                    label: qsTr("Next session")
                    onClicked: app.timeline.nextMark()
                }

                // The slider: a visible track with its elapsed part filled and a large handle; the row's whole height
                // takes the finger (48 px in the touch profile), a press on the track goes there
                RowLayout {
                    Layout.row: 0
                    Layout.column: bar.twoRows ? 0 : 4
                    Layout.columnSpan: bar.twoRows ? 7 : 1
                    Layout.fillWidth: true
                    spacing: 0
                    // (two rows: the title before the slider)
                    AbstractButton {
                        objectName: "timelineTitleShort"
                        visible: bar.twoRows
                        implicitHeight: bar.target
                        focusPolicy: Qt.NoFocus
                        rightPadding: 6
                        onClicked: bar.hintOpen = !bar.hintOpen
                        Accessible.name: qsTr("Replay: what it is")
                        contentItem: Label {
                            text: qsTr("Replay")
                            font.pixelSize: 14
                            font.bold: true
                            color: bar.accent
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Slider {
                        id: slider
                        objectName: "timelineSlider"
                        Layout.fillWidth: true
                        Layout.leftMargin: bar.twoRows ? 0 : 6
                        Layout.rightMargin: bar.twoRows ? 2 : 6
                        implicitHeight: bar.target
                        leftPadding: bar.handleSize / 2 + 2
                        rightPadding: bar.handleSize / 2 + 2
                        topPadding: 0
                        bottomPadding: 0
                        from: 0
                        to: Math.max(1, app.timeline.duration)
                        focusPolicy: Qt.NoFocus
                        value: pressed ? value : app.timeline.position
                        onMoved: app.timeline.seek(Math.round(value))
                        onPressedChanged: app.timeline.setScrubbing(pressed)
                        Accessible.name: qsTr("Time")

                        background: Item {
                            x: slider.leftPadding
                            y: slider.topPadding + slider.availableHeight / 2 - height / 2
                            width: slider.availableWidth
                            height: bar.touch ? 8 : 6
                            Rectangle {
                                objectName: "timelineTrack"
                                anchors.fill: parent
                                radius: height / 2
                                color: bar.trackColor
                            }
                            Rectangle {
                                objectName: "timelineElapsed"
                                width: Math.max(height, slider.visualPosition * parent.width)
                                height: parent.height
                                radius: height / 2
                                color: bar.accent
                            }
                            // The recordings: a band under the track where one is heard (grey: its file is not here)
                            Repeater {
                                model: app.timeline.tracks
                                Rectangle {
                                    required property var modelData
                                    objectName: "timelineRecording"
                                    x: parent.width * modelData.at / slider.to
                                    width: Math.max(2, parent.width * modelData.length / slider.to)
                                    y: parent.height + 4
                                    height: 4
                                    radius: 2
                                    color: modelData.found ? "#e53935" : "#9e9e9e"
                                    opacity: 0.8
                                }
                            }
                            // The sessions: a tick across the track where each begins, its date as a tip
                            Repeater {
                                model: app.timeline.marks
                                Item {
                                    required property var modelData
                                    objectName: "timelineMark"
                                    x: parent.width * modelData.at / slider.to - width / 2
                                    y: parent.height / 2 - height / 2
                                    width: 12
                                    height: bar.touch ? 22 : 18
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 3
                                        height: parent.height
                                        radius: 1.5
                                        color: bar.ink
                                        opacity: 0.75
                                    }
                                    // (the mouse and the pen: a finger dragging the handle has the time above it)
                                    HoverHandler {
                                        id: markHover
                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                                    }
                                    ToolTip.visible: markHover.hovered
                                    ToolTip.text: qsTr("A session: %1").arg(modelData.label)
                                }
                            }
                        }
                        handle: Rectangle {
                            objectName: "timelineHandle"
                            readonly property real size: slider.pressed ? bar.handleSize + 6 : bar.handleSize
                            x: slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
                            y: slider.topPadding + slider.availableHeight / 2 - height / 2
                            width: size
                            height: size
                            radius: size / 2
                            color: bar.accent
                            border.width: 3
                            border.color: bar.surface
                            // While it is held (a finger covers it): the time above it
                            Rectangle {
                                objectName: "timelineBubble"
                                visible: slider.pressed
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: -height - (bar.touch ? 20 : 12)
                                width: bubbleText.implicitWidth + 16
                                height: bubbleText.implicitHeight + 8
                                radius: 8
                                color: bar.dark ? "#f1f3f4" : "#303134"
                                Label {
                                    id: bubbleText
                                    anchors.centerIn: parent
                                    text: bar.timeText
                                    font.pixelSize: 13
                                    font.features: { "tnum": 1 }
                                    color: bar.dark ? "#1f1f1f" : "#ffffff"
                                }
                            }
                        }
                    }
                }

                ToolButton {
                    objectName: "timelineSpeed"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 5
                    implicitWidth: bar.target
                    implicitHeight: bar.target
                    focusPolicy: Qt.NoFocus
                    text: (app.timeline.speed === 0.5 ? "½" : app.timeline.speed) + "×"
                    font.pixelSize: 14
                    font.bold: true
                    font.features: { "tnum": 1 }
                    Material.foreground: bar.ink
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Speed (recordings are heard at 1×)")
                    ToolTip.delay: 600
                    Accessible.name: qsTr("Speed")
                    onClicked: app.timeline.cycleSpeed()
                }
                IconButton {
                    objectName: "timelineClose"
                    Layout.row: bar.twoRows ? 1 : 0
                    Layout.column: 6
                    iconName: "xqt-close"
                    implicitWidth: bar.target
                    implicitHeight: bar.target
                    icon.width: 20
                    icon.height: 20
                    icon.color: bar.ink
                    tip: qsTr("Stop the replay: the document as it is (Esc)")
                    label: qsTr("Stop the replay")
                    onClicked: app.timeline.stop()
                }
            }
        }
    }
}
