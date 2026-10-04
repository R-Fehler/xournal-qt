// "Save as sticker…" (qt/docs/stickers.md): what is selected becomes a sticker of the library's Stickers folder (or,
// "In all libraries", of the app-wide set). The name (the first words of its first text, else "Sticker <date>"), the
// folder (the set itself, one of its folders, or a new one typed), "With the PDF behind it" where the page shows a PDF
// page or a picture. Written in the background; it is on the clipboard too.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dialog
    objectName: "stickerSaveDialog"
    property var draft: ({})
    /// The folders of the set chosen ("" first: the set itself)
    property var folderChoices: [""]
    /// Open it for what is selected now; false when nothing is selected
    function openForSelection() {
        draft = app.stickerDraft(false)
        if (!draft.offered) return false
        allLibraries.checked = !draft.hasLibrary
        nameField.text = draft.name
        picture.checked = false
        refreshFolders()
        folderBox.currentIndex = 0
        folderBox.editText = ""
        open()
        return true
    }
    function refreshFolders() {
        folderChoices = [""].concat(app.stickerDraft(allLibraries.checked).folders)
    }
    preferredWidth: 460
    title: qsTr("Save as sticker")
    standardButtons: Dialog.Save | Dialog.Cancel
    Component.onCompleted: standardButton(Dialog.Save).enabled = Qt.binding(function() { return nameField.text.trim() !== "" })
    onOpened: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    onAccepted: app.saveSticker(nameField.text, folderBox.editText.trim(), picture.visible && picture.checked,
                                allLibraries.checked)

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 6
        Label { text: qsTr("Name"); color: "#5f6368"; font.pixelSize: 13 }
        TextField {
            id: nameField
            objectName: "stickerNameField"
            Layout.fillWidth: true
            selectByMouse: true
            Keys.onReturnPressed: if (text.trim() !== "") dialog.accept()
            Keys.onEnterPressed: if (text.trim() !== "") dialog.accept()
        }
        Label { text: qsTr("Folder"); color: "#5f6368"; font.pixelSize: 13; Layout.topMargin: 6 }
        ComboBox {
            id: folderBox
            objectName: "stickerFolderBox"
            Layout.fillWidth: true
            editable: true
            model: dialog.folderChoices
            displayText: editText
            // (the set itself has no name of its own: an empty field; a new name typed makes that folder)
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: folderBox.width
                text: modelData === "" ? qsTr("No folder (Stickers itself)") : modelData
                highlighted: folderBox.highlightedIndex === index
            }
            onActivated: function(index) { editText = dialog.folderChoices[index] }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#6b6f75"
            text: qsTr("Type a new name to make a folder (for a lecture or a topic).")
        }
        CheckBox {
            id: picture
            objectName: "stickerPictureBox"
            visible: dialog.draft.picture === true
            text: qsTr("With the PDF behind it (a picture of the page there)")
        }
        CheckBox {
            id: allLibraries
            objectName: "stickerAllLibrariesBox"
            visible: dialog.draft.hasLibrary === true
            text: qsTr("In all libraries (not only in this library's Stickers folder)")
            onToggled: dialog.refreshFolders()
        }
    }
}
