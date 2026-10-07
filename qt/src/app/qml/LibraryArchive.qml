// xournal-qt: exporting the library as an archive: what it does, the whole library or this folder, the folder
// it goes to, and what was written.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: archiveDialogs
    visible: false
    // (what the other parts use)
    readonly property alias libraryArchiveDialog: libraryArchiveDialog

    AdaptiveDialog {
        id: libraryArchiveDialog
        objectName: "libraryArchiveDialog"
        preferredWidth: 540
        title: qsTr("Export library as archive")
        onAboutToShow: archiveWholeLibrary.checked = true
        ColumnLayout {
            width: libraryArchiveDialog.availableWidth
            spacing: 6
            Label {
                objectName: "libraryArchiveExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Every document becomes a PDF made for keeping (PDF/A-3): readable for decades in any PDF "
                           + "viewer, with your ink merged into the pages and the full Xournal data inside, so this "
                           + "app can still open it for editing. Images, Markdown, text and other files are copied "
                           + "as they are, and a README.txt explains the files.") + "\n\n"
                      + qsTr("It all goes into a new folder “%1 archive” inside a folder you choose, with the folders "
                             + "of the library. The library itself is not changed.").arg(app.library.name)
            }
            ButtonGroup { id: archiveScope }
            RadioButton {
                id: archiveWholeLibrary
                objectName: "archiveWholeLibrary"
                Layout.fillWidth: true
                ButtonGroup.group: archiveScope
                text: qsTr("The whole library")
            }
            RadioButton {
                id: archiveThisFolder
                objectName: "archiveThisFolder"
                Layout.fillWidth: true
                ButtonGroup.group: archiveScope
                enabled: app.library.folder !== ""
                text: enabled ? qsTr("Only this folder: %1").arg(app.library.folder) : qsTr("Only this folder")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveChoose"
                text: qsTr("Choose a folder…")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            libraryArchiveFolderDialog.onlyFolder = archiveThisFolder.checked
            libraryArchiveFolderDialog.open()
        }
    }
    FolderDialog {
        id: libraryArchiveFolderDialog
        objectName: "libraryArchiveFolderDialog"
        property bool onlyFolder: false
        title: qsTr("Folder for the archive (outside the library)")
        onAccepted: app.exportLibraryArchive(selectedFolder, onlyFolder)
    }
    // While it runs: progress and Cancel
    AdaptiveDialog {
        id: libraryArchiveProgress
        objectName: "libraryArchiveProgress"
        kind: "card"
        modal: false
        closePolicy: Popup.NoAutoClose
        preferredWidth: 460
        title: qsTr("Writing the archive…")
        visible: app.libraryArchive.running
        ColumnLayout {
            width: libraryArchiveProgress.availableWidth
            spacing: 6
            ProgressBar {
                objectName: "libraryArchiveBar"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, app.libraryArchive.total)
                value: app.libraryArchive.done
            }
            Label {
                objectName: "libraryArchiveStatus"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: qsTr("%1 of %2").arg(app.libraryArchive.done).arg(app.libraryArchive.total)
                      + (app.libraryArchive.current !== "" ? " · " + app.libraryArchive.current : "")
                font.pixelSize: 13
                color: "#6b6f75"
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveCancel"
                text: qsTr("Cancel")
                onClicked: app.cancelLibraryArchive()
            }
        }
    }
    // At the end: what was done
    AdaptiveDialog {
        id: libraryArchiveSummary
        objectName: "libraryArchiveSummary"
        kind: "card"
        property var summary: ({})
        preferredWidth: 560
        title: summary.cancelled ? qsTr("Archive cancelled") : qsTr("Archive written")
        ColumnLayout {
            width: libraryArchiveSummary.availableWidth
            spacing: 6
            Label {
                objectName: "libraryArchiveSummaryText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const s = libraryArchiveSummary.summary
                    if (s.archived === undefined)
                        return ""
                    let t = qsTr("%1 archived (%2 of them PDF/A-3b), %3 copied, %4 not PDF/A, %5 failed.")
                                .arg(s.archived).arg(s.pdfa).arg(s.copied).arg(s.notPdfA.length).arg(s.failed.length)
                    if (s.cancelled)
                        t += "\n" + qsTr("Cancelled: the archive is incomplete.")
                    if (s.notPdfA.length > 0)
                        t += "\n\n" + qsTr("Not PDF/A (still readable everywhere):") + "\n• " + s.notPdfA.join("\n• ")
                    if (s.failed.length > 0)
                        t += "\n\n" + qsTr("Failed:") + "\n• " + s.failed.join("\n• ")
                    return t
                }
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveShow"
                text: qsTr("Show in file manager")
                visible: app.canShowInFileManager
                flat: true
                onClicked: { app.showInFileManager(libraryArchiveSummary.summary.target); libraryArchiveSummary.close() }
            }
            Button {
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Connections {
        target: app.libraryArchive
        function onFinished(summary) {
            libraryArchiveSummary.summary = summary
            libraryArchiveSummary.open()
        }
    }
}
