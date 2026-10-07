// Android's back key with nothing left to step out of (WindowShortcuts, "Esc and Back"): leaving the app is asked
// first (the author: "leaving the app should only happen after confirming it in a small dialog on android"). Leave
// closes the window the way ⋮ → Quit does (unsaved documents are asked about first); Stay, a tap outside or Back again
// keep the app.
import QtQuick
import QtQuick.Controls

AdaptiveDialog {
    id: dlg
    objectName: "leaveAppDialog"
    title: qsTr("Leave Xournal Qt?")
    kind: "card"
    preferredWidth: 360
    onAccepted: win.closeWindow()
    footer: DialogButtonBox {
        Button {
            objectName: "leaveAppStay"
            text: qsTr("Stay")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "leaveAppLeave"
            text: qsTr("Leave")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
