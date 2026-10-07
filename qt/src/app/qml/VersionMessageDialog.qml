// xournal-qt: Main.qml's part.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Save with a message…" (Ctrl+Alt+S, a milestone of the version history), or a version's message changed later
AdaptiveDialog {
    id: versionMessageDialog
    objectName: "versionMessageDialog"
    kind: "question"
    /// -1: the next save; else the version whose message it is
    property int versionId: -1
    function openFor(id) {
        versionId = id
        messageField.text = id >= 0 ? app.versions.messageOf(id) : ""
        keepVersionsBox.checked = true
        open()
        messageField.forceActiveFocus()
    }
    preferredWidth: 420
    title: versionId >= 0 ? qsTr("The message of the version of %1").arg(app.versions.titleOf(versionId))
                          : qsTr("Save with a message")
    ColumnLayout {
        width: versionMessageDialog.availableWidth
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: versionMessageDialog.versionId < 0
            text: qsTr("A version with a message is a milestone: it is kept as it is, whatever is saved later that day.")
        }
        TextField {
            id: messageField
            objectName: "versionMessageField"
            Layout.fillWidth: true
            placeholderText: qsTr("What did you do?")
            maximumLength: 200
            onAccepted: versionMessageDialog.accept()
        }
        CheckBox {
            id: keepVersionsBox
            objectName: "versionMessageKeepVersions"
            visible: versionMessageDialog.versionId < 0 && !app.versions.on
            text: qsTr("Keep versions of this document")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: versionMessageDialog.versionId < 0 && !app.versions.available
            text: app.versions.unavailableReason
            color: "#b06000"
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "versionMessageSave"
            text: versionMessageDialog.versionId >= 0 ? qsTr("Change") : qsTr("Save")
            enabled: versionMessageDialog.versionId >= 0 || app.versions.available
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    onAccepted: {
        if (versionId >= 0) {
            app.setVersionMessage(versionId, messageField.text)
            return
        }
        if (keepVersionsBox.visible && keepVersionsBox.checked) app.versions.on = true
        if (!app.saveWithMessage(messageField.text, null)) saveOrAsk(null)
    }
}
