// xournal-qt: saving and closing (Main.qml's part): Save and Save as with their dialog, the questions before a document
// with unsaved changes closes, closing a tab, all tabs or the window. The window keeps forwarders of the functions the
// tests and other files call (openSaveDialog, saveOrAsk, saveChosen, requestCloseTab, closeWindow, …).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: saveFlow
    anchors.fill: parent
    visible: false
    readonly property alias closeAllDialog: closeAllDialog
    /// Run once the unsaved changes are saved or discarded (the unsaved dialog)
    property var afterDiscardCheck: null
    property bool quitting: false
    function withSavedChanges(action) {
        if (!app.modified) {
            action()
            return
        }
        afterDiscardCheck = action
        unsavedDialog.open()
    }
    /// Save as, with the type: "xopp" (Xournal notes), "pdf" (a PDF with notes, editable: a hybrid PDF), or "" for
    /// the document's own (app.saveFormat(): a hybrid PDF stays a PDF, a .xopp a .xopp; new documents, annotated PDFs
    /// and images are PDFs with notes in PDF files mode, else .xopp).
    function openSaveDialog(then, format) {
        if (win.textDoc && app.textEditable) {
            app.saveInBackground(then ? then : null)  // (a text file is saved as itself: no file types)
            return
        }
        setUpSaveDialog(format || "")
        saveDialog.afterSave = then
        saveDialog.open()
    }
    function setUpSaveDialog(format) {
        const pdf = format === "pdf" || (format !== "xopp" && app.saveFormat() === "pdf")
        // .xopp: upstream Xournal++'s suggestion, next to the annotated PDF ("lecture.pdf" -> "lecture.xopp"), else
        // the document's own path, else the default name in the library / the last used folder. PDF: the document's
        // own hybrid PDF, "lecture.notes.pdf" for an annotated PDF, else the .xopp suggestion as .pdf.
        const suggestion = (pdf ? app.suggestedHybridFile() : app.suggestedSaveFile()).toString()
        saveDialog.settingUp = true
        saveDialog.selectedNameFilter.index = pdf ? 1 : 0
        if (suggestion !== "") {
            saveDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            saveDialog.selectedFile = pdf ? suggestion : app.fileForFormat(suggestion, false)
        }
        saveDialog.settingUp = false
    }
    /// The Save as dialog was accepted: as a PDF with notes or as a .xopp (the extension typed wins). A document
    /// saved as "name.xopp" asks first what happens to that .xopp (unless the choice is stored).
    function saveChosen(url, pdfChosen, then) {
        if (!app.savesAsPdf(url, pdfChosen)) {
            app.saveAsInBackground(url, then ? then : null)
            return
        }
        const old = app.oldXoppToAsk()
        if (old === "") app.saveAsHybridInBackground(url, then ? then : null)
        else oldXoppDialog.ask(old, url, then)
    }
    // "Open externally": a text file with unsaved changes is saved first (asked), so the other app sees them
    function openExternally() {
        if (app.textEditable && app.modified) {
            externalSaveDialog.open()
        } else {
            app.openExternally()
        }
    }
    function saveOrAsk(then) {
        if (app.savesWithoutDialog()) {
            // In the background: the window stays usable; `then` runs once the file is written (with its tab
            // current), not at all if that failed (a message says why)
            app.saveInBackground(then ? then : null)
        } else {
            openSaveDialog(then)
        }
    }

    // Close a tab; unsaved changes are asked about first (with that tab shown). A tab being saved waits for its save.
    function requestCloseTab(index) {
        if (app.tabSaving(index)) {
            app.whenSaved(index, function(i) { requestCloseTab(i) })
            return
        }
        if (!app.tabModified(index)) {
            app.closeTab(index)
            return
        }
        app.currentTab = index
        withSavedChanges(function() { app.closeTab(app.currentTab) })
    }
    // Close every document; unsaved changes are asked about one by one (after the saves that run).
    function closeAllTabs() {
        if (app.anySaving) {
            app.whenAllSaved(function() { closeAllTabs() })
            return
        }
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            app.closeAllTabs()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeAllTabs() })
    }
    // Quitting: the saves that run finish first (the window stays usable meanwhile), then the tabs with unsaved
    // changes are asked about one by one.
    property bool waitingToClose: false
    function closeWindow() {
        if (app.anySaving) {
            if (!waitingToClose) {
                waitingToClose = true
                app.whenAllSaved(function() { waitingToClose = false; closeWindow() })
            }
            return
        }
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            quitting = true
            win.close()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeWindow() })
    }
    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        property var afterSave: null
        property bool settingUp: false
        readonly property bool pdfChosen: selectedNameFilter.index === 1
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: pdfChosen ? "pdf" : "xopp"
        nameFilters: [qsTr("Xournal notes (*.xopp)"), qsTr("PDF with notes, editable (*.pdf)")]
        // The name follows the chosen type (native dialogs may do that themselves; then this changes nothing)
        onPdfChosenChanged: {
            if (!settingUp && selectedFile.toString() !== "")
                selectedFile = app.fileForFormat(selectedFile, pdfChosen)
        }
        onAccepted: {
            saveFlow.saveChosen(selectedFile, pdfChosen, afterSave)
            afterSave = null
        }
        onRejected: afterSave = null
    }
    // Saving a "name.xopp" as a PDF with notes: what happens to the .xopp (asked once, before it is written)
    AdaptiveDialog {
        id: oldXoppDialog
        objectName: "oldXoppDialog"
        kind: "question"
        property string file: ""
        property var url
        property var afterSave: null
        function ask(name, target, then) {
            file = name
            url = target
            afterSave = then ? then : null
            trashChoice.checked = true  // (the default)
            dontAsk.checked = false
            open()
        }
        /// "trash", "update" or "keep": the PDF is written, then that happens (Cancel: nothing is written)
        function choose(choice) {
            app.saveAsHybridInBackground(url, afterSave, choice, dontAsk.checked)
            afterSave = null
            close()
        }
        preferredWidth: 520
        title: qsTr("The PDF holds everything")
        ColumnLayout {
            width: oldXoppDialog.availableWidth
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This document was saved as %1. What happens to it?").arg(oldXoppDialog.file)
            }
            ButtonGroup { id: oldXoppChoices }
            RadioButton {
                id: trashChoice
                objectName: "oldXoppTrash"
                ButtonGroup.group: oldXoppChoices
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Move %1 to the trash (the PDF now holds everything)").arg(oldXoppDialog.file)
            }
            RadioButton {
                id: updateChoice
                objectName: "oldXoppUpdate"
                ButtonGroup.group: oldXoppChoices
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Keep it updated for Xournal++")
            }
            RadioButton {
                id: keepChoice
                objectName: "oldXoppKeep"
                ButtonGroup.group: oldXoppChoices
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Keep it as it is (not updated)")
            }
            CheckBox {
                id: dontAsk
                objectName: "oldXoppDontAsk"
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Don't ask again (Settings → Documents)")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "oldXoppSave"
                text: qsTr("Save")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: choose(updateChoice.checked ? "update" : keepChoice.checked ? "keep" : "trash")
        onRejected: afterSave = null
    }
    // Open externally with unsaved changes: save them first?
    AdaptiveDialog {
        id: externalSaveDialog
        objectName: "externalSaveDialog"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Save before opening it elsewhere?")
        Label {
            width: externalSaveDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("%1 has changes that are not saved. The other app sees the file as it is on disk.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button {
                objectName: "externalCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "externalWithoutSavingButton"
                text: qsTr("Open without saving")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: { externalSaveDialog.close(); app.openExternally() }
            }
            Button {
                objectName: "externalSaveButton"
                text: qsTr("Save and open")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: saveOrAsk(function() { app.openExternally() })
    }
    AdaptiveDialog {
        id: unsavedDialog
        objectName: "unsavedDialog"
        kind: "question"
        title: qsTr("Unsaved changes")
        preferredWidth: 480
        Label {
            width: unsavedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" has unsaved changes.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: { unsavedDialog.close(); saveOrAsk(afterDiscardCheck) }
            onRejected: { unsavedDialog.close(); afterDiscardCheck = null }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    unsavedDialog.close()
                    var action = afterDiscardCheck
                    afterDiscardCheck = null
                    if (action) action()
                }
            }
        }
    }
    AdaptiveDialog {
        id: closeAllDialog
        objectName: "closeAllDialog"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Close all documents?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        ColumnLayout {
            width: closeAllDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 documents are open. Documents with unsaved changes ask before they close.")
                        .arg(app.tabs.count)
            }
        }
        onAccepted: { tabOverview.close(); closeAllTabs() }
    }
}
