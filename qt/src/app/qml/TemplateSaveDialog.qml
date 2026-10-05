// "Save page as template…" (qt/docs/templates.md): a page becomes a template of the library's Templates folder (or,
// "In all libraries", of the app-wide set). The name (the document's name and the page number), the folder (the set
// itself, one of its folders, or a new one typed), "With the page's background" (a PDF page: that PDF page goes along,
// as when the page is copied; paper stays as it is) and "With its content" (ink, text, boxes, notes). Written in the
// background.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dialog
    objectName: "templateSaveDialog"
    property var draft: ({})
    /// The page (index) it saves
    property int page: 0
    /// The folders of the set chosen ("" first: the set itself)
    property var folderChoices: [""]
    /// Open it for page index `p`; false when there is no such page (or a text document)
    function openForPage(p) {
        page = p
        draft = app.templateDraft(p, false)
        if (!draft.offered) {
            app.pageActionDone(qsTr("This page cannot be saved as a template"), false)
            return false
        }
        allLibraries.checked = !draft.hasLibrary
        nameField.text = draft.name
        background.checked = true
        content.checked = true
        refreshFolders()
        folderBox.currentIndex = 0
        folderBox.editText = ""
        open()
        return true
    }
    function refreshFolders() {
        folderChoices = [""].concat(app.templateDraft(page, allLibraries.checked).folders)
    }
    readonly property bool canSave: nameField.text.trim() !== "" && (background.checked || content.checked)
    preferredWidth: 460
    title: qsTr("Save page as template")
    standardButtons: Dialog.Save | Dialog.Cancel
    Component.onCompleted: standardButton(Dialog.Save).enabled = Qt.binding(function() { return dialog.canSave })
    onOpened: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    onAccepted: app.saveTemplate(page, nameField.text, folderBox.editText.trim(), background.checked, content.checked,
                                 allLibraries.checked)

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 6
        Label { text: qsTr("Name"); color: "#5f6368"; font.pixelSize: 13 }
        TextField {
            id: nameField
            objectName: "templateNameField"
            Layout.fillWidth: true
            selectByMouse: true
            Keys.onReturnPressed: if (dialog.canSave) dialog.accept()
            Keys.onEnterPressed: if (dialog.canSave) dialog.accept()
        }
        Label { text: qsTr("Folder"); color: "#5f6368"; font.pixelSize: 13; Layout.topMargin: 6 }
        ComboBox {
            id: folderBox
            objectName: "templateFolderBox"
            Layout.fillWidth: true
            editable: true
            model: dialog.folderChoices
            displayText: editText
            // (the set itself has no name of its own: an empty field; a new name typed makes that folder)
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: folderBox.width
                text: modelData === "" ? qsTr("No folder (Templates itself)") : modelData
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
            id: background
            objectName: "templateBackgroundBox"
            text: dialog.draft.background === "pdf" ? qsTr("With the page's background (its PDF page)")
                  : dialog.draft.background === "image" ? qsTr("With the page's background (its picture)")
                  : qsTr("With the page's background (its paper)")
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#6b6f75"
            text: background.checked ? qsTr("Adding the template is then the same as pasting a copy of this page.")
                                     : qsTr("The page gets the background new pages get where it is added.")
        }
        CheckBox {
            id: content
            objectName: "templateContentBox"
            text: qsTr("With its content (ink, text, boxes, notes)")
        }
        CheckBox {
            id: allLibraries
            objectName: "templateAllLibrariesBox"
            visible: dialog.draft.hasLibrary === true
            text: qsTr("In all libraries (not only in this library's Templates folder)")
            onToggled: dialog.refreshFolders()
        }
    }
}
