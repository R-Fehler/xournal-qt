// "Remove unused images" of a .md (qt/docs/md-images.md, "Clean-up"): the files in its "name.assets" folder that the
// text does not link to any more, listed; "Move to trash" moves them there (nothing is deleted without this).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: dlg
    objectName: "unusedImagesDialog"
    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 32 : 480, 480)
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var files: []

    function show() {
        files = app.unusedMarkdownImages()
        open()
    }

    background: Rectangle { color: "#ffffff"; radius: 12; border.width: 1; border.color: "#d5d8dc" }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8
        Label {
            text: qsTr("Remove unused images")
            font.pixelSize: 17
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
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
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "unusedImagesCancel"
                text: dlg.files.length === 0 ? qsTr("Close") : qsTr("Cancel")
                flat: true
                onClicked: dlg.close()
            }
            Button {
                objectName: "unusedImagesTrash"
                visible: dlg.files.length > 0
                text: qsTr("Move to trash")
                highlighted: true
                onClicked: {
                    app.trashMarkdownImages(dlg.files)
                    dlg.close()
                }
            }
        }
    }
}
