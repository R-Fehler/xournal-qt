// xournal-qt: exporting the document (Main.qml's part): as a plain PDF and as Markdown, with their file dialogs.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: exportFlow
    anchors.fill: parent
    visible: false
    function openExportDialog() {
        const suggestion = app.suggestedExportFile().toString()
        if (suggestion !== "") {
            exportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            exportDialog.selectedFile = suggestion
        }
        exportDialog.open()
    }
    // "Export as Markdown" (qt/docs/md-pdf.md): next to the document (Xournal++ files; asked before a file is
    // replaced), else where this dialog says
    function exportMarkdown() {
        const file = app.markdownExportFile()
        if (file.toString() === "") {
            const suggestion = app.suggestedMarkdownExport().toString()
            markdownExportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            markdownExportDialog.selectedFile = suggestion
            markdownExportDialog.open()
        } else if (app.fileExists(file)) {
            markdownReplaceDialog.file = file
            markdownReplaceDialog.open()
        } else {
            app.exportMarkdown(file)
        }
    }
    FileDialog {
        id: markdownExportDialog
        objectName: "markdownExportDialog"
        title: qsTr("Export as Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown (*.md)")]
        onAccepted: app.exportMarkdown(selectedFile)
    }
    AdaptiveDialog {
        id: markdownReplaceDialog
        objectName: "markdownReplaceDialog"
        kind: "question"
        property url file
        title: qsTr("Replace the Markdown file?")
        preferredWidth: 440
        Label {
            width: markdownReplaceDialog.availableWidth
            wrapMode: Text.WordWrap
            text: qsTr("%1 exists. Replace it with the Markdown of this document?")
                  .arg(decodeURIComponent(markdownReplaceDialog.file.toString().replace(/^.*\//, "")))
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Choose another place…"); DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                     onClicked: {
                         markdownReplaceDialog.close()
                         const suggestion = markdownReplaceDialog.file.toString()
                         markdownExportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
                         markdownExportDialog.selectedFile = suggestion
                         markdownExportDialog.open()
                     } }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "markdownReplaceButton"; text: qsTr("Replace")
                     DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: app.exportMarkdown(file)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export as plain PDF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: app.exportPdf(selectedFile)
    }
}
