// "Remove unused images" of a .md (qt/docs/md-images.md, "Clean-up"): the files in its "name.assets" folder that the
// text does not link to any more, listed; "Move to trash" moves them there (nothing is deleted without this).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "unusedImagesDialog"
    preferredWidth: 480
    title: qsTr("Remove unused images")

    property var files: []

    function show() {
        files = app.unusedMarkdownImages()
        open()
    }

    onAccepted: app.trashMarkdownImages(dlg.files)

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 8
        Label {
            objectName: "unusedImagesSummary"
            text: dlg.files.length === 0 ? qsTr("Every image in the document's folder of images is used by its text.")
                                         : qsTr("%n image(s) of the document's folder are not used by its text:", "", dlg.files.length)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#50555b"
        }
        ListView {
            objectName: "unusedImagesList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 220)
            clip: true
            visible: dlg.files.length > 0
            model: dlg.files
            delegate: Label {
                width: ListView.view.width
                text: modelData
                elide: Text.ElideMiddle
                font.family: "monospace"
                font.pixelSize: 13
                topPadding: 2
                bottomPadding: 2
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "unusedImagesCancel"
            text: dlg.files.length === 0 ? qsTr("Close") : qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "unusedImagesTrash"
            visible: dlg.files.length > 0
            text: qsTr("Move to trash")
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
