// "Share folder…" / "Share library…" (qt/docs/features/library.md, "Sharing a folder or the library"): the folder or
// the whole library as one zip. The options, then the progress (Cancel), then what was done with "Show in file manager"
// and "Save a copy…". The work is AppController's (app.libraryShare: LibraryShare).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: share
    // The folder shared (relative to the library; "": the whole library)
    property string folder: ""
    readonly property var task: app.libraryShare
    readonly property var survey: task.survey

    function openFor(folder) {
        share.folder = folder
        app.surveyShare(folder)
        options.open()
    }
    function megabytes(bytes) {
        if (bytes === undefined)
            return "…"
        if (bytes < 1e6)
            return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1e3)))
        return qsTr("%1 MB").arg((bytes / 1e6).toFixed(bytes < 1e8 ? 1 : 0))
    }

    AdaptiveDialog {
        id: options
        objectName: "shareZipDialog"
        preferredWidth: 560
        title: share.folder === "" ? qsTr("Share library") : qsTr("Share folder “%1”").arg(share.folder.split("/").pop())
        onAboutToShow: {
            formatApp.checked = true
            readings.checked = true
            pdfText.checked = false
            history.checked = false
            recordings.checked = true
            protect.checked = false
            password.text = ""
        }
        ColumnLayout {
            width: options.availableWidth
            spacing: 4
            Label {
                objectName: "shareZipExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: (share.folder === "" ? qsTr("The whole library as one zip, with its folders.")
                                           : qsTr("The folder as one zip, with its subfolders."))
                      + " " + qsTr("Your files are not changed.")
                      + (share.survey.documents !== undefined
                         ? " " + qsTr("%n file(s), %1.", "", share.survey.files).arg(share.megabytes(share.survey.bytes)) : "")
            }
            ButtonGroup { id: formats }
            RadioButton {
                id: formatApp
                objectName: "shareZipFormatApp"
                Layout.fillWidth: true
                ButtonGroup.group: formats
                text: qsTr("For xournal-qt: the documents as they are")
            }
            RadioButton {
                id: formatXournal
                objectName: "shareZipFormatXournal"
                Layout.fillWidth: true
                ButtonGroup.group: formats
                text: qsTr("For Xournal++: notes as .xopp with their PDF")
            }
            RadioButton {
                id: formatPdf
                objectName: "shareZipFormatPdf"
                Layout.fillWidth: true
                ButtonGroup.group: formats
                text: qsTr("Plain PDFs: the ink drawn into the pages, for any PDF viewer")
            }
            CheckBox {
                id: readings
                objectName: "shareZipReadings"
                Layout.fillWidth: true
                visible: formatApp.checked
                text: qsTr("Handwriting readings and previews: the search works at once for the recipient")
            }
            CheckBox {
                id: pdfText
                objectName: "shareZipPdfText"
                Layout.fillWidth: true
                Layout.leftMargin: 24
                visible: formatApp.checked
                enabled: readings.checked
                text: qsTr("PDF text too: faster search, bigger file")
            }
            CheckBox {
                id: history
                objectName: "shareZipHistory"
                Layout.fillWidth: true
                visible: !formatPdf.checked
                text: qsTr("Version history of PDFs with notes (off: only their latest state)")
            }
            CheckBox {
                id: recordings
                objectName: "shareZipRecordings"
                Layout.fillWidth: true
                visible: !formatPdf.checked
                enabled: share.survey.recordings === undefined || share.survey.recordings > 0
                text: share.survey.recordings === undefined ? qsTr("Recordings (counting…)")
                      : share.survey.recordings === 0 ? qsTr("Recordings (none)")
                      : qsTr("Recordings (%n recording(s), %1)", "", share.survey.recordings)
                            .arg(share.megabytes(share.survey.recordingBytes))
            }
            CheckBox {
                id: protect
                objectName: "shareZipPassword"
                Layout.fillWidth: true
                visible: share.task.passwordAvailable
                text: qsTr("Protect the zip with a password (AES)")
            }
            TextField {
                id: password
                objectName: "shareZipPasswordField"
                Layout.fillWidth: true
                Layout.leftMargin: 24
                visible: protect.checked && protect.visible
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                visible: password.visible
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: "#6b6f75"
                text: qsTr("Windows Explorer and macOS Finder cannot open zips with a password: the recipient needs "
                           + "7-Zip (Windows) or Keka (macOS); Android and iOS file apps differ.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shareZipStart"
                text: qsTr("Share")
                enabled: !protect.checked || password.text.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            app.shareAsZip(share.folder, {
                format: formatXournal.checked ? "xournal" : formatPdf.checked ? "pdf" : "app",
                readings: readings.checked,
                pdfText: readings.checked && pdfText.checked,
                history: history.checked,
                recordings: recordings.checked,
                password: protect.checked && protect.visible ? password.text : ""
            })
            password.text = ""
        }
    }

    // While it runs: progress and Cancel
    AdaptiveDialog {
        id: progress
        objectName: "shareZipProgress"
        kind: "card"
        modal: false
        closePolicy: Popup.NoAutoClose
        preferredWidth: 460
        title: qsTr("Writing the zip…")
        visible: share.task.running
        ColumnLayout {
            width: progress.availableWidth
            spacing: 6
            ProgressBar {
                objectName: "shareZipBar"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, share.task.total)
                value: share.task.done
            }
            Label {
                objectName: "shareZipStatus"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: share.task.current
                font.pixelSize: 13
                color: "#6b6f75"
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shareZipCancel"
                text: qsTr("Cancel")
                onClicked: app.cancelShareZip()
            }
        }
    }

    // At the end: what was done
    AdaptiveDialog {
        id: summary
        objectName: "shareZipSummary"
        kind: "card"
        property var outcome: ({})
        preferredWidth: 560
        title: outcome.cancelled ? qsTr("Sharing cancelled")
               : outcome.error ? qsTr("The zip could not be written") : qsTr("Zip ready")
        ColumnLayout {
            width: summary.availableWidth
            spacing: 6
            Label {
                objectName: "shareZipSummaryText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const r = summary.outcome
                    if (r.documents === undefined)
                        return ""
                    if (r.cancelled)
                        return qsTr("Nothing was written.")
                    if (r.error)
                        return r.error
                    let t = qsTr("%1 (%2): %n document(s) and file(s)", "", r.documents).arg(r.name).arg(share.megabytes(r.bytes))
                    if (r.readings > 0)
                        t += ", " + qsTr("%n with the library's readings", "", r.readings)
                    t += "."
                    if (r.attached.length > 0)
                        t += "\n\n" + qsTr("Files from outside the folder, in _attached:") + "\n• " + r.attached.join("\n• ")
                    if (r.outsideLinks.length > 0)
                        t += "\n\n" + qsTr("Links to documents outside the folder (they will not work):") + "\n• " + r.outsideLinks.join("\n• ")
                    if (r.notes.length > 0)
                        t += "\n\n" + r.notes.join("\n")
                    if (r.failed.length > 0)
                        t += "\n\n" + qsTr("Left out:") + "\n• " + r.failed.join("\n• ")
                    return t
                }
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shareZipShow"
                text: qsTr("Show in file manager")
                visible: app.canShowInFileManager && summary.outcome.bytes > 0
                flat: true
                onClicked: { app.handOverZip(summary.outcome.zip); summary.close() }
            }
            Button {
                objectName: "shareZipSaveCopy"
                text: qsTr("Save a copy…")
                visible: summary.outcome.bytes > 0
                flat: true
                onClicked: saveCopy.open()
            }
            Button {
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    FolderDialog {
        id: saveCopy
        objectName: "shareZipSaveFolder"
        title: qsTr("Save the zip in")
        onAccepted: if (app.saveZipCopy(summary.outcome.zip, selectedFolder)) summary.close()
    }
    Connections {
        target: share.task
        function onFinished(result) {
            summary.outcome = result
            summary.open()
        }
    }
}
