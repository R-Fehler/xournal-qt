// The microphone refused (qt/docs/audio.md, "Platforms"): macOS and Android ask before the first recording, and when
// the answer is no (then or earlier) recording cannot start. This says so plainly, where the microphone is allowed,
// and opens that page of the system's settings where the app can (macOS, Windows, Android). Opened by
// app.audio.microphoneDenied; closing it or opening the settings clears that.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "microphoneDialog"
    title: qsTr("The microphone is off for Xournal Qt")
    kind: "card"
    preferredWidth: 440
    readonly property bool wanted: app.audio.microphoneDenied
    onWantedChanged: wanted ? open() : close()
    onClosed: app.audio.dismissMicrophoneNotice()
    onAccepted: app.audio.openMicrophoneSettings()
    footer: DialogButtonBox {
        Button {
            objectName: "microphoneSettingsButton"
            visible: app.audio.canOpenMicrophoneSettings
            text: qsTr("Open settings")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            objectName: "microphoneCloseButton"
            text: qsTr("Close")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
    Label {
        objectName: "microphoneDialogText"
        width: dlg.availableWidth
        wrapMode: Text.Wrap
        text: qsTr("Recording needs the microphone, and the system does not let Xournal Qt use it. Allow it in %1, "
                   + "then start the recording again.").arg(app.audio.microphoneSettingsPath)
    }
}
