// xournal-qt: sharing (Main.qml's part): the Share dialog, a PDF of a .xopp, the copy for Xournal++ users and the
// export for the archive (PDF/A) with its report.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: shareFlow
    anchors.fill: parent
    visible: false
    readonly property alias shareDialog: shareDialog
    readonly property alias archiveDialog: archiveDialog
    /// Share → a PDF with notes of the current document (`file`: a PDF of the library instead), shown in the file
    /// manager or onto the clipboard. Saved first if needed; a .xopp is never turned into a PDF unasked.
    function sharePdfOf(file, toClipboard, withHistory) {
        if (file !== "") {
            app.shareFile(file, toClipboard, !!withHistory)
            return
        }
        const step = app.shareStep()
        if (step === "share" || step === "save") {
            app.sharePdf(toClipboard, !!withHistory)
        } else if (step === "saveAs") {
            openSaveDialog(function() { app.sharePdf(toClipboard) }, "pdf")
        } else if (toClipboard) {
            app.sharePdfCopy("", true)  // (a .xopp: a PDF copy in the cache; the document stays as it is)
        } else {
            shareXoppDialog.open()
        }
    }
    // Share of a text file: the file itself; the open document's unsaved changes are saved first
    function shareTextFile(path, current, toClipboard) {
        if (current && app.textEditable && app.modified) {
            app.saveInBackground(function() { app.shareFile(path, toClipboard) })
        } else {
            app.shareFile(path, toClipboard)
        }
    }
    /// A choice of the Share dialog: a title and a line about it
    component ShareChoice: ItemDelegate {
        id: choice
        property string detail
        Layout.fillWidth: true
        contentItem: ColumnLayout {
            spacing: 2
            Label { text: choice.text; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label {
                text: choice.detail
                color: "#6b6f75"
                font.pixelSize: 13
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }
    // Share…: the PDF with notes (shown in the file manager, or copied), or a copy for Xournal++ users
    AdaptiveDialog {
        id: shareDialog
        objectName: "shareDialog"
        kind: "question"
        property string file: ""  // a PDF of the library; "": the current document
        /// A Markdown or text file (the current document's, or a card's): shared as the file itself, never as a PDF
        property string textFile: ""
        /// The PDF with notes keeps its versions (version history): they go along only when chosen
        property bool keepsVersions: false
        function openFor(path) {
            file = path
            textFile = app.sharedTextFile(path)
            keepsVersions = textFile === "" && app.sharedKeepsVersions(path)
            withHistoryBox.checked = false
            shareProtectBox.checked = app.protectedDocument
            sharePasswordField.text = ""
            open()
        }
        function share(toClipboard) {
            if (sharePasswordField.visible) {
                const password = sharePasswordField.text
                sharePasswordField.text = ""
                app.sharePdfProtected(password, toClipboard)
            } else {
                shareFlow.sharePdfOf(file, toClipboard, withHistoryBox.checked)
            }
        }
        preferredWidth: 460
        title: qsTr("Share")
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: shareDialog.availableWidth
            spacing: 0
            ShareChoice {
                objectName: "shareTextFileChoice"
                visible: shareDialog.textFile !== ""
                text: qsTr("The file itself")
                detail: app.canShare ? qsTr("Shown in the file manager, to send it on.")
                                     : qsTr("Not available on this system yet.")
                enabled: app.canShare
                onClicked: { shareDialog.close(); shareFlow.shareTextFile(shareDialog.textFile, shareDialog.file === "", false) }
            }
            ShareChoice {
                objectName: "shareTextCopyChoice"
                visible: shareDialog.textFile !== ""
                text: qsTr("Copy the file")
                detail: qsTr("Paste it into another app or a chat.")
                onClicked: { shareDialog.close(); shareFlow.shareTextFile(shareDialog.textFile, shareDialog.file === "", true) }
            }
            // Version history: without the versions unless chosen (older versions may hold ink that was deleted)
            CheckBox {
                id: withHistoryBox
                objectName: "shareWithHistory"
                Layout.fillWidth: true
                visible: shareDialog.keepsVersions
                text: qsTr("With its version history")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Off (recommended): the PDF goes as it is now. On: with every version it keeps, "
                                   + "also ink that was deleted since.")
            }
            // "Protect with a password": the PDF with notes shared as a copy that needs this password (AES-256). A
            // protected document is shared with its own password anyway.
            CheckBox {
                id: shareProtectBox
                objectName: "shareProtect"
                Layout.fillWidth: true
                visible: shareDialog.textFile === "" && shareDialog.file === ""
                enabled: !app.protectedDocument
                checked: app.protectedDocument
                text: app.protectedDocument ? qsTr("Protected with its password") : qsTr("Protect with a password")
            }
            TextField {
                id: sharePasswordField
                objectName: "sharePassword"
                Layout.fillWidth: true
                visible: shareProtectBox.visible && shareProtectBox.checked && !app.protectedDocument
                echoMode: TextInput.Password
                placeholderText: qsTr("Password to open it")
            }
            ShareChoice {
                objectName: "sharePdfChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("PDF with notes (opens in any app)")
                detail: app.canShare ? qsTr("Shown in the file manager, to send it on.")
                                     : qsTr("Not available on this system yet.")
                enabled: app.canShare && (!sharePasswordField.visible || sharePasswordField.text !== "")
                onClicked: { shareDialog.close(); shareDialog.share(false) }
            }
            ShareChoice {
                objectName: "shareCopyChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("Copy the PDF with notes")
                detail: qsTr("Paste it into another app or a chat.")
                enabled: !sharePasswordField.visible || sharePasswordField.text !== ""
                onClicked: { shareDialog.close(); shareDialog.share(true) }
            }
            ShareChoice {
                objectName: "shareArchiveChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("For the archive (PDF/A)")
                detail: qsTr("A PDF made for keeping: readable for decades, the ink merged into the pages.")
                onClicked: { shareDialog.close(); archiveDialog.openFor(shareDialog.file) }
            }
            ShareChoice {
                objectName: "shareXournalChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("For Xournal++ (.xopp + PDF)")
                detail: qsTr("A copy in a folder you choose, never next to the document.")
                onClicked: {
                    shareDialog.close()
                    xournalFolderDialog.file = shareDialog.file
                    xournalFolderDialog.currentFolder = app.shareFolder()
                    xournalFolderDialog.open()
                }
            }
        }
    }
    // Share → PDF of a .xopp: saved as a PDF with notes (the document becomes it), or a PDF copy
    AdaptiveDialog {
        id: shareXoppDialog
        objectName: "shareXoppDialog"
        kind: "question"
        preferredWidth: 500
        title: qsTr("Share as a PDF with notes")
        Label {
            width: shareXoppDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This document is saved as Xournal notes (.xopp). Other apps need a PDF with notes: save the "
                       + "document as one (it stays editable here), or write a PDF copy and keep the .xopp.")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shareSaveAsPdf"
                text: qsTr("Save as PDF with notes…")
                flat: true
                onClicked: {
                    shareXoppDialog.close()
                    openSaveDialog(function() { app.sharePdf(false) }, "pdf")
                }
            }
            Button {
                objectName: "shareSaveCopy"
                text: qsTr("Save a PDF copy…")
                flat: true
                onClicked: {
                    shareXoppDialog.close()
                    const suggestion = app.suggestedHybridFile().toString()
                    if (suggestion !== "") {
                        pdfCopyDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
                        pdfCopyDialog.selectedFile = suggestion
                    }
                    pdfCopyDialog.open()
                }
            }
            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: shareXoppDialog.close()
            }
        }
    }
    FileDialog {
        id: pdfCopyDialog
        objectName: "pdfCopyDialog"
        title: qsTr("Save a PDF copy with notes")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF with notes, editable (*.pdf)")]
        onAccepted: app.sharePdfCopy(selectedFile, false)
    }
    FolderDialog {
        id: xournalFolderDialog
        objectName: "xournalFolderDialog"
        property string file: ""
        title: qsTr("Folder for the copy for Xournal++")
        onAccepted: app.shareForXournal(selectedFolder, file)
    }
    Connections {
        target: app
        // Exported for Xournal++ and shown: the two files can be copied too
        function onSharedForXournal(files, text) {
            snackbar.show(text, false, qsTr("Copy"), function() { app.copyToClipboard(files) })
        }
    }
    // Export for the archive: what it means, where it goes; then the PDF/A report
    AdaptiveDialog {
        id: archiveDialog
        objectName: "archiveDialog"
        property string file: ""  // a library card's document; "": the current document
        property url suggestion
        function openFor(path) {
            file = path
            suggestion = app.suggestedArchiveFile(path)
            if (suggestion.toString() !== "")
                archiveNextTo.checked = true
            else
                archiveInFolder.checked = true
            open()
        }
        preferredWidth: 520
        title: qsTr("Export for the archive")
        ColumnLayout {
            width: archiveDialog.availableWidth
            spacing: 6
            Label {
                objectName: "archiveExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("A PDF made for keeping (PDF/A-3). It stays readable for decades in any PDF viewer. "
                           + "Your ink is merged into the pages, so no viewer can hide or lose it. The full Xournal "
                           + "data is embedded, so this app can still open it for editing.")
            }
            // (PDF/A allows no encryption: an archive PDF of a protected document has no password)
            Label {
                objectName: "archiveNotProtected"
                Layout.fillWidth: true
                visible: archiveDialog.file === "" && app.protectedDocument
                wrapMode: Text.Wrap
                color: "#b06000"
                text: qsTr("This document is protected with a password. An archive PDF cannot be (PDF/A does not "
                           + "allow it): it is written without a password.")
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: qsTr("Where it goes")
                font.weight: Font.DemiBold
            }
            ButtonGroup { id: archivePlaces }
            RadioButton {
                id: archiveNextTo
                objectName: "archiveNextTo"
                Layout.fillWidth: true
                ButtonGroup.group: archivePlaces
                enabled: archiveDialog.suggestion.toString() !== ""
                text: enabled ? qsTr("Next to the document, as %1")
                                    .arg(decodeURIComponent(archiveDialog.suggestion.toString().replace(/^.*\//, "")))
                              : qsTr("Next to the document (it has no file yet)")
            }
            RadioButton {
                id: archiveInFolder
                objectName: "archiveInFolder"
                Layout.fillWidth: true
                ButtonGroup.group: archivePlaces
                text: qsTr("In a folder I choose…")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "archiveExportButton"
                text: qsTr("Export")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            if (archiveNextTo.checked) {
                app.exportArchive(suggestion, file)
            } else {
                archiveFolderDialog.file = file
                archiveFolderDialog.currentFolder = app.shareFolder()
                archiveFolderDialog.open()
            }
        }
    }
    FolderDialog {
        id: archiveFolderDialog
        objectName: "archiveFolderDialog"
        property string file: ""
        title: qsTr("Folder for the archive PDF")
        onAccepted: app.exportArchive(app.archiveFileIn(selectedFolder, file), file)
    }
    AdaptiveDialog {
        id: archiveReportDialog
        objectName: "archiveReportDialog"
        kind: "card"
        property string path: ""
        property bool pdfa: false
        property var problems: []
        property var adjusted: []
        preferredWidth: 520
        title: pdfa ? qsTr("Archive PDF written") : qsTr("Written, but not as PDF/A")
        ColumnLayout {
            width: archiveReportDialog.availableWidth
            spacing: 6
            Label {
                objectName: "archiveReportText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const name = archiveReportDialog.path.replace(/^.*\//, "")
                    if (archiveReportDialog.pdfa)
                        return qsTr("%1 is a PDF/A-3b file: made for keeping, with your ink in the pages and the "
                                    + "Xournal data inside.").arg(name)
                    return qsTr("%1 was written with your ink in the pages and the Xournal data inside, and it opens "
                                + "in any PDF viewer. It is not PDF/A, because:").arg(name)
                            + "\n• " + archiveReportDialog.problems.join("\n• ")
                }
            }
            Label {
                objectName: "archiveReportAdjusted"
                visible: archiveReportDialog.adjusted.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#6b6f75"
                font.pixelSize: 13
                text: qsTr("Changed to make it conform: %1.").arg(archiveReportDialog.adjusted.join("; "))
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "archiveShowButton"
                visible: app.canShare
                text: qsTr("Show in folder")
                flat: true
                onClicked: { app.shareFile(archiveReportDialog.path, false); archiveReportDialog.close() }
            }
            Button {
                objectName: "archiveOkButton"
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Connections {
        target: app
        function onArchiveExported(path, pdfa, notPdfA, adjusted) {
            archiveReportDialog.path = path
            archiveReportDialog.pdfa = pdfa
            archiveReportDialog.problems = notPdfA
            archiveReportDialog.adjusted = adjusted
            archiveReportDialog.open()
        }
    }
}
