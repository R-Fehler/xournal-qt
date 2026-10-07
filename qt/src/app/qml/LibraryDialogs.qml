// xournal-qt: the home screen's dialogs for the library's items and libraries: rename, new folder, new text file,
// copy or move to, trash, storage access, the libraries' home on Android and its move, new library,
// share as zip.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: libraryDialogs
    visible: false
    // (what the other parts use)
    readonly property alias renameDialog: renameDialog
    readonly property alias folderNameDialog: folderNameDialog
    readonly property alias textFileDialog: textFileDialog
    readonly property alias transferDialog: transferDialog
    readonly property alias trashDialog: trashDialog
    readonly property alias storageAccessDialog: storageAccessDialog
    readonly property alias librariesHomeDialog: librariesHomeDialog
    readonly property alias newLibraryDialog: newLibraryDialog
    readonly property alias shareZip: shareZip

    AdaptiveDialog {
        id: renameDialog
        objectName: "renameDialog"
        title: home.menuFolder ? qsTr("Rename folder") : qsTr("Rename document")
        preferredWidth: 440
        onAboutToShow: { renameField.text = home.menuName; renameField.selectAll(); renameField.forceActiveFocus() }
        ColumnLayout {
            width: renameDialog.availableWidth
            TextField {
                id: renameField
                objectName: "renameField"
                Layout.fillWidth: true
                selectByMouse: true
                Keys.onReturnPressed: renameDialog.accept()
                Keys.onEnterPressed: renameDialog.accept()
            }
            Label {
                visible: !home.menuFolder
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: home.menuKind === "other" || home.menuKind === "text" ? qsTr("The whole file name, with its extension.")
                                                                              : qsTr("The Xournal file and its PDF are renamed together.")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (renameField.text.trim() !== "" && home.menuModel) home.menuModel.rename(home.menuRow, renameField.text)
    }

    AdaptiveDialog {
        id: folderNameDialog
        objectName: "folderNameDialog"
        property int row: -1
        title: qsTr("New folder")
        preferredWidth: 440
        onAboutToShow: { folderField.text = ""; folderField.forceActiveFocus() }
        TextField {
            id: folderField
            objectName: "folderNameField"
            width: folderNameDialog.availableWidth
            placeholderText: qsTr("Folder name")
            selectByMouse: true
            Keys.onReturnPressed: folderNameDialog.accept()
            Keys.onEnterPressed: folderNameDialog.accept()
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (folderField.text.trim() !== "") app.library.createFolder(folderField.text)
    }

    // "New text document" (a PDF text document, ".pdf"), "New Markdown file" / "New text file": its name (made in the
    // current folder and opened to write in)
    AdaptiveDialog {
        id: textFileDialog
        objectName: "textFileDialog"
        property string extension: ".md"
        title: extension === ".pdf" ? qsTr("New text document") : extension === ".md" ? qsTr("New Markdown file")
                                                                                     : qsTr("New text file")
        preferredWidth: 440
        onAboutToShow: { textFileField.text = ""; textFileField.forceActiveFocus() }
        RowLayout {
            width: textFileDialog.availableWidth
            TextField {
                id: textFileField
                objectName: "textFileName"
                Layout.fillWidth: true
                placeholderText: qsTr("Name (Untitled)")
                selectByMouse: true
                Keys.onReturnPressed: textFileDialog.accept()
                Keys.onEnterPressed: textFileDialog.accept()
            }
            Label { text: textFileDialog.extension; color: "#5f6368" }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: extension === ".pdf" ? app.createTextDocument(textFileField.text)
                                         : app.createTextFile(textFileField.text, extension)
    }

    // Where to copy / move documents and folders: a folder of this library or of another one.
    AdaptiveDialog {
        id: transferDialog
        objectName: "transferDialog"
        property var paths: []
        property bool copy: false
        property var libraries: []
        property string targetRoot: ""
        title: (copy ? qsTr("Copy %1 to") : qsTr("Move %1 to"))
                   .arg(paths.length === 1 ? "“" + paths[0].substring(paths[0].lastIndexOf("/") + 1) + "”" : home.countText(paths.length))
        preferredWidth: 480
        fillBody: true
        preferredHeight: 580
        standardButtons: Dialog.Cancel
        onAboutToShow: {
            libraries = app.libraries()
            targetRoot = app.library.rootPath
            libraryBox.currentIndex = Math.max(0, libraryBox.indexOfValue(targetRoot))
        }
        function choose(folderPath) {
            const paths = transferDialog.paths, copy = transferDialog.copy
            transferDialog.close()
            // Into the Downloads folder from elsewhere: short-lived, ask first
            home.confirmImport(app.library.isTemporaryFolder(folderPath) && !app.library.temporary, function() {
                app.library.transferTo(paths, folderPath, copy)
                app.recent.clearSelection()
            })
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            ComboBox {
                id: libraryBox
                objectName: "transferLibraryBox"
                Layout.fillWidth: true
                model: transferDialog.libraries
                valueRole: "path"
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view ? ListView.view.width : implicitWidth
                    text: modelData.downloads ? qsTr("Downloads folder") : modelData.name
                    font.weight: modelData.current ? Font.DemiBold : Font.Normal
                    icon.source: app.iconUrl(modelData.downloads ? "xqt-download" : "xqt-library")
                    icon.color: "#566d86"
                    highlighted: libraryBox.highlightedIndex === index
                }
                displayText: {
                    const lib = transferDialog.libraries[currentIndex]
                    if (!lib) return ""
                    const name = lib.downloads ? qsTr("Downloads folder") : lib.name
                    return lib.current ? qsTr("%1 (this library)").arg(name) : qsTr("Library: %1").arg(name)
                }
                onActivated: transferDialog.targetRoot = currentValue
            }
            ListView {
                objectName: "transferFolders"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: transferDialog.opened ? app.library.foldersOf(transferDialog.targetRoot) : []
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    leftPadding: 16 + modelData.depth * 20
                    text: modelData.name
                    icon.source: app.iconUrl(modelData.depth === 0 ? "xqt-library" : "xqt-folder")
                    icon.color: "#566d86"
                    onClicked: transferDialog.choose(modelData.path)
                }
            }
        }
    }

    AdaptiveDialog {
        id: trashDialog
        objectName: "trashDialog"
        kind: "question"
        title: qsTr("Move to trash?")
        preferredWidth: 460
        ColumnLayout {
            width: trashDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: home.menuPaths.length > 1
                      ? qsTr("%1 go to the trash (documents with their PDFs, folders with everything in them).").arg(home.countText(home.menuPaths.length))
                      : qsTr("“%1” goes to the trash (a document with its PDF, a folder with everything in it).")
                            .arg(home.menuPaths.length === 1 ? home.menuPaths[0].substring(home.menuPaths[0].lastIndexOf("/") + 1) : "")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            app.library.trashPaths(home.menuPaths)
            if (home.menuModel === app.recent) app.recent.clearSelection()
        }
    }

    // Android: why "All files access" is asked for, before the system's page for it
    AdaptiveDialog {
        id: storageAccessDialog
        objectName: "storageAccessDialog"
        kind: "question"
        property string folder: ""  // then opened as library ("": the folder picker)
        function ask(path) { folder = path; open() }
        title: qsTr("Allow access to your files?")
        preferredWidth: 520
        Label {
            width: storageAccessDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("To use a folder of the phone's storage as a library (for example one that Syncthing, "
                       + "FolderSync or Autosync keeps in sync), Xournal Qt needs “All files access”. It then "
                       + "works with the folder as on a computer: its documents, previews and search, and changes "
                       + "other apps make are seen at once. It only reads and writes the folders you open as a "
                       + "library.")
                  + "\n\n" + qsTr("Android shows its settings page next: turn on the switch for Xournal Qt, then "
                                   + "come back.")
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "storageAccessContinue"
                text: qsTr("Continue")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.requestStorageAccess(folder)
    }

    // Android: the libraries belong in the phone's Documents folder (they are in the app's own folder, which Android
    // deletes with the app): explained, then "All files access" and the move (AppController::moveLibrariesHome)
    AdaptiveDialog {
        id: librariesHomeDialog
        objectName: "librariesHomeDialog"
        kind: "question"
        property string toMove: ""
        onAboutToShow: toMove = app.librariesToMove()
        title: qsTr("Keep libraries on the phone?")
        preferredWidth: 520
        Label {
            width: librariesHomeDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Your libraries are kept in %1 on the phone, where other apps (file managers, Syncthing) see "
                       + "them and they survive uninstalling the app.").arg(app.sharedLibrariesName)
                  + (librariesHomeDialog.toMove !== ""
                     ? "\n\n" + qsTr("The libraries you have now (%1) are moved there. Each file is checked when it has "
                                        + "arrived; nothing is deleted before all of them are there.").arg(librariesHomeDialog.toMove)
                     : "")
                  + (app.storageAccess ? ""
                     : "\n\n" + qsTr("For this Xournal Qt needs \u201cAll files access\u201d. Android shows its settings "
                                        + "page next: turn on the switch for Xournal Qt, then come back."))
        }
        footer: DialogButtonBox {
            Button {
                objectName: "librariesHomeNotNow"
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "librariesHomeContinue"
                text: qsTr("Continue")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.moveLibrariesHome()
        onRejected: app.declineLibrariesHome()
    }

    // The move in progress: copied, checked, then the old copies removed
    AdaptiveDialog {
        id: libraryMoveDialog
        objectName: "libraryMoveDialog"
        kind: "card"
        readonly property var move: app.libraryMove
        closePolicy: Popup.NoAutoClose
        title: qsTr("Moving the libraries")
        preferredWidth: 480
        Connections {
            target: app.libraryMove
            function onRunningChanged() {
                if (app.libraryMove.running) libraryMoveDialog.open()
                else libraryMoveDialog.close()
            }
        }
        ColumnLayout {
            width: libraryMoveDialog.availableWidth
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const m = libraryMoveDialog.move
                    if (m.step === "clean") return qsTr("Removing the old copies from the app\u2019s folder…")
                    const files = qsTr("%1 of %2 files").arg(m.files).arg(m.totalFiles)
                    return (m.step === "verify" ? qsTr("Checking the copies in %1…") : qsTr("Copying to %1…"))
                               .arg(app.sharedLibrariesName) + "\n" + files
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: libraryMoveDialog.move.fraction
                indeterminate: libraryMoveDialog.move.step === "clean"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: qsTr("Until everything is copied and checked, the libraries stay where they are.")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                enabled: libraryMoveDialog.move.step !== "clean"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole  // (closed when the move stops)
                onClicked: app.cancelLibrariesMove()
            }
        }
    }

    AdaptiveDialog {
        id: newLibraryDialog
        title: qsTr("New library")
        preferredWidth: 460
        onAboutToShow: { libraryName.text = ""; libraryName.forceActiveFocus() }
        ColumnLayout {
            width: newLibraryDialog.availableWidth
            TextField {
                id: libraryName
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                selectByMouse: true
                Keys.onReturnPressed: newLibraryDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: app.libraryWindows ? qsTr("A library is a folder in Documents/Xournal_Libraries. It opens in a new window.")
                                         : qsTr("A library is a folder in Documents/Xournal_Libraries.")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (!app.createLibrary(libraryName.text)) {
                errorDialog.text = qsTr("A library named “%1” cannot be created (the name is taken or not allowed).").arg(libraryName.text)
                errorDialog.open()
            }
        }
    }

    // Share folder… / Share library…: a zip (ShareZipDialog.qml)
    ShareZipDialog { id: shareZip; objectName: "shareZip" }
}
