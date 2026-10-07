// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Read only: the first stroke tried says so, once until read only is turned off, at the pen; it fades
Rectangle {
    id: readOnlyNote
    objectName: "readOnlyNote"
    readonly property bool inputTransparent: true
    /// Said in this read-only time (and how often it was said, for the tests)
    property bool told: false
    property int toldCount: 0
    property point at: Qt.point(0, 0)
    visible: opacity > 0
    opacity: 0
    z: 93
    x: Math.round(Math.max(win.layout.canvasControlsLeft + 8,
                           Math.min(canvas.x + at.x - width / 2, win.layout.canvasControlsRight - width - 8)))
    y: Math.round(Math.max(win.layout.canvasControlsTop + 8,
                           Math.min(canvas.y + at.y - height - 24, win.layout.canvasControlsBottom - height - 8)))
    width: Math.min(readOnlyNoteText.implicitWidth + 28, win.layout.canvasControlsRight - win.layout.canvasControlsLeft - 16)
    height: readOnlyNoteText.implicitHeight + 14
    radius: Math.min(16, height / 2)
    color: "#e6303134"
    Label {
        id: readOnlyNoteText
        objectName: "readOnlyNoteText"
        readonly property bool inputTransparent: true
        anchors.centerIn: parent
        width: Math.min(implicitWidth, readOnlyNote.width - 28)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        text: win.modes.zenShown ? qsTr("Read only — tap the dot to write")
              : win.modes.chromeMode === "compact" ? qsTr("Read only — ⋯ → Read only to write")
              : qsTr("Read only — ⋮ → View → Read only to write")
        color: "#ffffff"
        font.pixelSize: 14
    }
    function tell(pos) {
        if (told) return
        told = true
        ++toldCount
        at = pos
        noteFade.restart()
    }
    SequentialAnimation {
        id: noteFade
        NumberAnimation { target: readOnlyNote; property: "opacity"; to: 0.95; duration: 120 }
        PauseAnimation { duration: 2400 }
        NumberAnimation { target: readOnlyNote; property: "opacity"; to: 0; duration: 700 }
    }
    Connections {
        target: win
        function onReadOnlyOnChanged() { if (!win.modes.readOnlyOn) readOnlyNote.told = false }
    }
}
