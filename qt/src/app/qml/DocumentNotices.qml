// xournal-qt: what the window is told about the document and answers (Main.qml's part): another app edited it,
// annotations to adopt, links that moved, text changed on disk, the links to it, messages and the snackbar's notices.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: documentNotices
    anchors.fill: parent
    visible: false
    readonly property alias adoptDialog: adoptDialog
    readonly property alias backlinksDialog: backlinksDialog
    readonly property alias messageDialog: messageDialog
    // A hybrid PDF whose ink another app changed: keep ours, or take theirs as plain annotations
    AdaptiveDialog {
        id: hybridEditedDialog
        objectName: "hybridEditedDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 520
        title: qsTr("Edited in another app")
        closePolicy: Popup.NoAutoClose
        Label {
            width: hybridEditedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This PDF was edited in another app: its ink differs from the Xournal data.") + "\n\n"
                  + qsTr("Keep the Xournal data: the next save writes the ink from it again, and the other app's "
                         + "changes to it are dropped. Import: the changed ink stays as the other app left it, as "
                         + "plain annotations (shown, not editable here), and the layers it stood for are emptied "
                         + "(Undo brings them back).")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "hybridKeepButton"
                text: qsTr("Keep the Xournal data")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                objectName: "hybridImportButton"
                text: qsTr("Import the other app's changes")
                DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
                onClicked: { app.importHybridChanges(); hybridEditedDialog.close() }
            }
        }
        onAccepted: app.keepHybridData()
    }
    // Annotations of another app in the PDF: make them editable? (qt/docs/adopt-annotations.md; asked once per file)
    AdaptiveDialog {
        id: adoptDialog
        objectName: "adoptDialog"
        kind: "question"
        property int count: 0
        property string appName: ""
        property bool offered: false
        function openFor(n, appName, offered) {
            adoptDialog.count = n
            adoptDialog.appName = appName
            adoptDialog.offered = offered
            open()
        }
        preferredWidth: 480
        title: qsTr("Annotations from another app")
        Label {
            width: adoptDialog.availableWidth
            wrapMode: Text.Wrap
            text: (adoptDialog.appName !== ""
                   ? qsTr("This PDF has %n annotation(s) from %1.", "", adoptDialog.count).arg(adoptDialog.appName)
                   : qsTr("This PDF has %n annotation(s) from another app.", "", adoptDialog.count))
                  + " " + qsTr("Make them editable?") + "\n\n"
                  + qsTr("Ink, highlights, text boxes, shapes and pictures go into a layer of their own on each page, "
                         + "notes become sticky notes. Their originals leave the PDF when it is saved, replaced by "
                         + "these. Undo brings them back.")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "adoptNotNowButton"
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "adoptMakeEditableButton"
                text: qsTr("Make editable")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.adoptAnnotations()
        onRejected: if (offered) app.declineAdoption()
    }
    Connections {
        target: app
        function onAnnotationsToAdopt(count, appName, file) {
            adoptDialog.openFor(count, appName, true)
        }
        function onHybridEditedElsewhere(file) {
            hybridEditedDialog.file = file
            hybridEditedDialog.open()
        }
        function onLinkTargetFound(name, folder) {
            linkFoundDialog.file = name
            linkFoundDialog.folder = folder
            linkFoundDialog.open()
        }
        function onLinkTargetMissing(name) {
            linkMissingDialog.file = name
            linkMissingDialog.open()
        }
        function onEditAnywayWarning(name) {
            editAnywayDialog.file = name
            editAnywayDialog.open()
        }
        function onTextChangedOnDisk(name) {
            textChangedDialog.file = name
            textChangedDialog.document = false
            textChangedDialog.open()
        }
        function onDocumentChangedOnDisk(name) {
            textChangedDialog.file = name
            textChangedDialog.document = true
            textChangedDialog.open()
        }
    }
    // "Edit anyway" for a code, LaTeX, JSON... file: once per file, what editing it here means
    AdaptiveDialog {
        id: editAnywayDialog
        objectName: "editAnywayDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 520
        title: qsTr("Edit %1 as plain text?").arg(file)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: editAnywayDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This file is edited as plain text; the app does not know its format. It does not check or "
                       + "complete what you write, and it writes the text back as you leave it (lines you do not touch "
                       + "stay as they are). For more, open it externally in an editor made for it.")
        }
        onAccepted: app.editAnyway(true)
    }
    // "Linked from": the documents of the library that link to this one (qt/docs/links.md)
    AdaptiveDialog {
        id: backlinksDialog
        objectName: "backlinksDialog"
        property var items: []
        function show() { items = app.backlinks(); open() }
        preferredWidth: 460
        title: qsTr("Linked from")
        standardButtons: Dialog.Close
        ColumnLayout {
            width: backlinksDialog.availableWidth
            Label {
                visible: backlinksDialog.items.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTr("No document of the library links to this one.")
            }
            Repeater {
                model: backlinksDialog.items
                delegate: ItemDelegate {
                    required property var modelData
                    objectName: "backlink"
                    Layout.fillWidth: true
                    text: modelData.folder !== "" ? modelData.name + "  —  " + modelData.folder : modelData.name
                    onClicked: { backlinksDialog.close(); app.openPath(modelData.path) }
                }
            }
        }
    }
    // A followed link's file was gone: found elsewhere (update the link?) or not at all (locate it?)
    AdaptiveDialog {
        id: linkFoundDialog
        objectName: "linkFoundDialog"
        kind: "question"
        property string file: ""
        property string folder: ""
        preferredWidth: 480
        title: qsTr("The linked document was moved")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: linkFoundDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("It was found as \u201c%1\u201d in %2 and opened. Update the link to point there?")
                  .arg(linkFoundDialog.file).arg(linkFoundDialog.folder !== "" ? linkFoundDialog.folder : qsTr("the library"))
        }
        onAccepted: app.updateFoundLink()
    }
    AdaptiveDialog {
        id: linkMissingDialog
        objectName: "linkMissingDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 480
        title: qsTr("Document not found")
        standardButtons: Dialog.Open | Dialog.Cancel
        Label {
            width: linkMissingDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\u201c%1\u201d is not where the link says, and nothing like it is in the library. Locate it? "
                       + "The link then points to the file you choose.").arg(linkMissingDialog.file)
        }
        onAccepted: locateLinkDialog.open()
    }
    FileDialog {
        id: locateLinkDialog
        title: qsTr("Locate the linked document")
        currentFolder: app.openFolder()
        onAccepted: app.relinkTo(selectedFile)
    }
    // A text file changed on disk (another program) while it has changes here: which version stays
    AdaptiveDialog {
        id: textChangedDialog
        objectName: "textChangedDialog"
        kind: "question"
        property string file: ""
        property bool document: false  // a .xopp or PDF (reloading it cannot be undone)
        preferredWidth: 520
        title: qsTr("Changed in another app")
        closePolicy: Popup.NoAutoClose
        Label {
            width: textChangedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("%1 was changed by another app, and it has changes here that are not saved.").arg(textChangedDialog.file)
                  + "\n\n" + (textChangedDialog.document
                               ? qsTr("Reload: the file as it is now is shown, and your changes here are discarded. "
                                      + "Keep mine: your version stays, and saving writes over the other app's changes.")
                               : qsTr("Reload: the file as it is now is shown (Undo brings your changes back). Keep mine: "
                                      + "your version stays, and saving writes over the other app's changes."))
        }
        footer: DialogButtonBox {
            Button {
                objectName: "textKeepButton"
                text: qsTr("Keep mine")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "textReloadButton"
                text: qsTr("Reload")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.resolveTextChange(true)
        onRejected: app.resolveTextChange(false)
    }
    AdaptiveDialog {
        id: messageDialog
        objectName: "messageDialog"
        kind: "card"
        preferredWidth: 640
        standardButtons: Dialog.Ok
        property alias text: messageLabel.text
        Label { id: messageLabel; wrapMode: Text.Wrap; width: parent.width }
    }
    Connections {
        target: app.versions
        // A version was restored (the History panel): one undo step
        function onRestored(ok, text) {
            if (ok) {
                snackbar.show(text, true)
            } else {
                messageDialog.title = qsTr("The version could not be restored")
                messageDialog.text = text
                messageDialog.open()
            }
        }
    }
    Connections {
        target: app
        function onPageActionDone(text, undoable) { snackbar.show(text, undoable) }
        // Copy handwriting as text (qt/copy-tools): the text near the words; why not, with the way to Settings
        function onInkTextCopy(result) {
            if (result.state === "reading" || result.state === "copied") {
                inkTextToast.show(result)
                return
            }
            inkTextToast.close()
            if (result.state === "nothing") {
                snackbar.show(qsTr("No handwriting there to copy"), false)
            } else {
                snackbar.show(result.state === "off"
                              ? qsTr("Copying handwriting as text needs the handwriting search (Settings → Search)")
                              : qsTr("No handwriting model to read it with (Settings → Search)"),
                              false, qsTr("Settings"), function() { settingsPage.open(); settingsPage.showSearch() })
            }
        }
        // A snip pasted from a document with a file (qt/docs/snip.md): a link to its page, if wanted
        function onSnipLinkOffered(title) {
            snackbar.show(qsTr("Add a link to the source page (%1)?").arg(title), false, qsTr("Add link"),
                          function() { app.addSnipLink() })
        }
    }
    Connections {
        target: app
        // The annotations were exported as Markdown (the Annotations panel): open the file from here
        function onAnnotationsExported(file, error) {
            if (error !== "") {
                messageDialog.title = qsTr("Export failed")
                messageDialog.text = error
                messageDialog.open()
                return
            }
            snackbar.show(qsTr("Annotations exported to %1").arg(file.split("/").pop()), false, qsTr("Open"),
                          function() { app.openPath(file) })
        }
    }

    Connections {
        target: app
        function onMessage(title, text, error) {
            messageDialog.title = title !== "" ? title : (error ? qsTr("Error") : qsTr("Information"))
            messageDialog.text = text
            messageDialog.open()
        }
    }
    Connections {
        target: app.audio
        function onMessage(text) { snackbar.show(text, false) }
    }
    Connections {
        target: app.timeline
        function onMessage(text) { snackbar.show(text, false) }
    }
}
