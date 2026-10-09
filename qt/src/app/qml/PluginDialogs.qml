// xournal-qt: part of the main window (Main.qml): what plugins ask the user while a command waits
// (qt/docs/features/plugins.md): the permission question on first use, a plugin's modal dialog (its fields drawn by
// PluginFields), the file dialogs of its file handles; and its notes in the snackbar (with Undo after a command that
// changed the document).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: dialogs
    readonly property var plugins: app.plugins

    // --- the permission, asked on first use --------------------------------------------------------------------------
    AdaptiveDialog {
        id: question
        objectName: "pluginPermissionDialog"
        kind: "question"
        title: qsTr("Allow %1?").arg(dialogs.plugins.question.plugin || "")
        closePolicy: Popup.CloseOnEscape
        property bool answered: false
        onAboutToShow: answered = false
        onRejected: if (!answered) { answered = true; dialogs.plugins.answerQuestion(false) }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("%1 wants to %2. The answer is kept; Settings → Plugins changes it.")
                    .arg(dialogs.plugins.question.plugin || "").arg(dialogs.plugins.question.what || "")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "pluginDenyButton"
                text: qsTr("Don't allow")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "pluginAllowButton"
                text: qsTr("Allow")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: if (!answered) { answered = true; dialogs.plugins.answerQuestion(true) }
    }
    // --- a plugin's modal dialog ---------------------------------------------------------------------------------------
    AdaptiveDialog {
        id: form
        objectName: "pluginDialog"
        kind: "form"
        readonly property var spec: dialogs.plugins.dialog
        title: spec.title || ""
        property bool answered: false
        onAboutToShow: {
            answered = false
            const v = {}
            const fs = spec.fields || []
            for (let i = 0; i < fs.length; ++i) if (fs[i].id !== undefined && fs[i].value !== undefined) v[fs[i].id] = fs[i].value
            fields.values = Object.assign(v, spec.values || {})
        }
        onRejected: if (!answered) { answered = true; dialogs.plugins.answerDialog(null) }
        PluginFields {
            id: fields
            objectName: "pluginDialogFields"
            width: parent.width
            fields: form.spec.fields || []
        }
        footer: DialogButtonBox {
            Button {
                objectName: "pluginDialogCancel"
                text: form.spec.cancel || qsTr("Cancel")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "pluginDialogOk"
                text: form.spec.ok || qsTr("OK")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: if (!answered) { answered = true; dialogs.plugins.answerDialog(fields.values) }
    }
    // --- files the user chooses for a plugin (its handles) ----------------------------------------------------------
    FileDialog {
        id: files
        objectName: "pluginFileDialog"
        property bool answered: false
        onAccepted: { answered = true; dialogs.plugins.answerFile(selectedFile) }
        onRejected: { answered = true; dialogs.plugins.answerFile("") }
    }

    Connections {
        target: dialogs.plugins
        function onQuestionChanged() {
            if (dialogs.plugins.question.plugin !== undefined) question.open()
            else if (question.visible) { question.answered = true; question.close() }
        }
        function onDialogChanged() {
            if (dialogs.plugins.dialog.title !== undefined || dialogs.plugins.dialog.fields !== undefined) form.open()
            else if (form.visible) { form.answered = true; form.close() }
        }
        function onFileRequested(save, title, name, filters) {
            files.answered = false
            files.fileMode = save ? FileDialog.SaveFile : FileDialog.OpenFile
            files.title = title !== "" ? title : (save ? qsTr("Save") : qsTr("Open"))
            files.nameFilters = filters
            files.currentFolder = app.openFolder()
            if (name !== "") files.selectedFile = files.currentFolder + "/" + name
            files.open()
        }
        function onNote(text, undoable) {
            snackbar.show(text, false, undoable ? qsTr("Undo") : "", undoable ? function() { app.undo() } : null)
        }
    }
}
