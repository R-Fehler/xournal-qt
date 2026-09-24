// The first start: which way to keep documents (session/DocumentMode.h). Asked once, when nothing is chosen yet;
// the recommended way is chosen to start with. Only "Continue" closes it (the choice is stored then).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "documentModeDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(520, parent ? parent.width - 32 : 520)
    title: qsTr("How do you want to keep your documents?")
    /// After the choice (the window goes on with what waited: the recovery question)
    signal chosen()

    onAboutToShow: modeCards.mode = "pdf"  // (the recommendation)

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 12
        DocumentModeCards {
            id: modeCards
            Layout.fillWidth: true
            selfSelect: true
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#6b6f75"
            font.pixelSize: 13
            text: qsTr("You can change this later in Settings → Documents. Copies for Xournal++ are always available "
                       + "through Share → “For Xournal++”.")
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "documentModeContinue"
            text: qsTr("Continue")
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        onAccepted: {
            app.documentMode = modeCards.mode
            dialog.close()
            dialog.chosen()
        }
    }
}
