// The record button (qt/docs/features/audio.md, "In the app"): a tap starts recording for this document (a voice memo
// of the page shown; ink written meanwhile plays the moment it was written), a tap again stops it. Held (or
// right-clicked): the play tool (tap ink to hear it) and the document's recordings. Self-contained, so its owner can
// put it anywhere: it is an app item of the arrangement (the top bar's at a first start; the rail, a group, ⋮ → Tools;
// qt/top-bar).
import QtQuick
import QtQuick.Controls
import "Popups.js" as Popups

IconButton {
    id: button
    objectName: "recordButton"
    /// Offered in builds that can record, in documents with pages (not a text file)
    property bool offered: app.audio.available && !win.textDoc
    readonly property bool here: app.audio.recordingHere
    iconName: here ? "xopp-audio-playback-stop" : "xqt-mic"
    icon.color: here || app.audio.recording ? "#d32f2f" : "#303030"
    checked: here
    label: here ? qsTr("Stop recording") : qsTr("Record audio")
    tip: here ? qsTr("Stop recording") + win.keyNote("record")
              : app.audio.recording ? qsTr("Recording for %1 (tap: stop it)").arg(app.audio.recordingTitle)
                                    : qsTr("Record audio: ink written meanwhile plays its moment%1. Hold: the play tool, recordings").arg(win.keyNote("record"))
    ownHold: true
    /// What its long press does (in its menu on a bar)
    readonly property string holdText: qsTr("The play tool, the recordings…")
    onClicked: app.audio.toggleRecording()
    onPressAndHold: Popups.openAt(audioMenu)
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
        onTapped: function(point) { Popups.openAt(audioMenu, point.position) }
    }
    AdaptiveMenu {
        id: audioMenu
        objectName: "audioMenu"
        title: qsTr("Audio")
        AdaptiveMenuItem {
            objectName: "playToolItem"
            text: qsTr("Play tool: tap ink to hear it")
            icon.source: app.iconUrl("xopp-object-play")
            onTriggered: app.selectTool("playObject")
        }
        AdaptiveMenuItem {
            objectName: "recordingsItem"
            text: qsTr("Recordings of this document…")
            icon.source: app.iconUrl("xqt-play")
            onTriggered: recordingsDialog.openList()
        }
    }
    RecordingsDialog { id: recordingsDialog }
}
