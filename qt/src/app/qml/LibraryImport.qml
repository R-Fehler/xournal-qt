// xournal-qt: opening a folder as library and importing into it: the folder chooser, the file and folder dialogs,
// importing into Downloads, the sync conflicts of a document.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: importDialogs
    visible: false
    // (what the other parts use)
    readonly property alias folderChooser: folderChooser
    readonly property alias openLibraryDialog: openLibraryDialog
    readonly property alias importDialog: importDialog
    readonly property alias importFolderDialog: importFolderDialog
    readonly property alias temporaryImportDialog: temporaryImportDialog
    readonly property alias conflictDialog: conflictDialog

    FolderChooser {
        id: folderChooser
        onChosen: function(path) { app.openLibraryAt(path) }
    }
    FolderDialog {
        id: openLibraryDialog
        title: qsTr("Open a folder as library")
        onAccepted: app.openLibrary(selectedFolder)
    }

    FileDialog {
        id: importDialog
        objectName: "importDialog"
        title: qsTr("Import into the library")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf *.md *.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("All files (*)")]
        onAccepted: {
            const files = selectedFiles, folder = app.library.flat || home.searching ? "" : app.library.folder
            home.confirmImport(app.library.temporary, function() { app.library.importUrls(files, folder) })
        }
    }
    FolderDialog {
        id: importFolderDialog
        objectName: "importFolderDialog"
        title: qsTr("Import a folder (with its subfolders) into the library")
        onAccepted: {
            const dir = selectedFolder, folder = app.library.flat || home.searching ? "" : app.library.folder
            home.confirmImport(app.library.temporary, function() { app.library.importUrls([dir], folder) })
        }
    }

    AdaptiveDialog {
        id: temporaryImportDialog
        objectName: "temporaryImportDialog"
        kind: "question"
        property var action: null
        title: qsTr("Import into Downloads?")
        preferredWidth: 520
        ColumnLayout {
            width: temporaryImportDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This goes into your Downloads folder. Files there are often cleaned up or deleted. "
                           + "Documents you want to keep are better in one of your libraries.")
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Import anyway"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            const action = temporaryImportDialog.action
            temporaryImportDialog.action = null
            if (action) action()
        }
        onRejected: action = null
    }

    // Sync conflicts of a document (the badge on its card): compare side by side, or keep one (the other goes to the
    // trash; where there is none, deleted after asking)
    AdaptiveDialog {
        id: conflictDialog
        objectName: "conflictDialog"
        property string documentPath: ""
        property var items: []  // app.library.conflictsOf: the document first, then its conflict copies
        function show(path) {
            documentPath = path
            items = app.library.conflictsOf(path)
            if (items.length > 1) open()
        }
        title: qsTr("Sync conflict")
        preferredWidth: 560
        standardButtons: Dialog.Close
        ColumnLayout {
            id: conflictColumn
            width: conflictDialog.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("A sync app found this document changed in two places and kept both versions. Compare "
                           + "them side by side, then keep one; the other goes to the trash.")
                      + (app.library.canTrash ? "" : " " + qsTr("(There is no trash here: it is deleted.)"))
            }
            Repeater {
                model: conflictDialog.items
                delegate: Frame {
                    id: conflictRow
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            font.weight: Font.DemiBold
                            text: conflictRow.modelData.original ? qsTr("This document: %1").arg(conflictRow.modelData.name)
                                                                 : conflictRow.modelData.name
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: "#5f6368"
                            text: {
                                const d = conflictRow.modelData
                                let parts = []
                                if (!d.original && d.app) parts.push(qsTr("Conflict copy (%1)").arg(d.app))
                                else if (!d.original) parts.push(qsTr("Conflict copy"))
                                parts.push(qsTr("changed %1").arg(d.modified.toLocaleString(Qt.locale(), Locale.ShortFormat)))
                                parts.push(home.sizeText(d.size))
                                return parts.join(" · ")
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            visible: !conflictRow.modelData.original
                            spacing: 8
                            Button {
                                objectName: "conflictCompareButton"
                                text: qsTr("Compare")
                                onClicked: {
                                    conflictDialog.close()
                                    app.compareConflict(conflictDialog.documentPath, conflictRow.modelData.path)
                                }
                            }
                            Button {
                                objectName: "conflictKeepDocumentButton"
                                text: qsTr("Keep the document")
                                onClicked: conflictConfirm.ask(conflictRow.modelData.path, false,
                                                               conflictRow.modelData.name)
                            }
                            Button {
                                objectName: "conflictKeepCopyButton"
                                text: qsTr("Keep this copy")
                                onClicked: conflictConfirm.ask(conflictRow.modelData.path, true,
                                                               conflictDialog.items[0].name)
                            }
                        }
                    }
                }
            }
        }
    }
    // Keep one: without a trash (Android), what goes is deleted - asked first
    AdaptiveDialog {
        id: conflictConfirm
        objectName: "conflictConfirm"
        kind: "question"
        property string copyPath: ""
        property bool keepCopy: false
        property string goes: ""
        function ask(path, keep, goesName) {
            copyPath = path
            keepCopy = keep
            goes = goesName
            if (app.library.canTrash) resolve()
            else open()
        }
        function resolve() {
            if (app.library.resolveConflict(copyPath, keepCopy)) {
                conflictDialog.show(conflictDialog.documentPath)  // (more copies: still listed)
                if (conflictDialog.items.length < 2) conflictDialog.close()
            }
        }
        title: qsTr("Delete the other version?")
        preferredWidth: 480
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: conflictConfirm.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("There is no trash on this device: %1 is deleted and cannot be brought back.").arg(conflictConfirm.goes)
        }
        onAccepted: resolve()
    }
}
