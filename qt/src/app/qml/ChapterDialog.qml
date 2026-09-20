// Starts a chapter on a page: a heading is written at its top left, and the contents sidebar and overview show it
// (documents without a PDF table of contents get their chapters this way).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "chapterDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Start a chapter")
    width: Math.min(parent ? parent.width * 0.9 : 420, 420)
    standardButtons: Dialog.Cancel | Dialog.Ok

    property int page: 0
    property int level: 0

    function openFor(index) {
        page = index
        level = 0
        titleField.text = ""
        open()
    }
    onOpened: titleField.forceActiveFocus()
    onAccepted: app.addChapter(page, titleField.text, level)

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 8
        Label { text: qsTr("On page %1").arg(dlg.page + 1); color: "#5f6368" }
        TextField {
            id: titleField
            objectName: "chapterTitleField"
            Layout.fillWidth: true
            placeholderText: qsTr("Name of the chapter")
            onAccepted: dlg.accept()
        }
        RowLayout {
            Label { text: qsTr("Level"); Layout.fillWidth: true }
            Repeater {
                model: [{ text: qsTr("Chapter"), value: 0 }, { text: qsTr("Section"), value: 1 },
                        { text: qsTr("Subsection"), value: 2 }]
                delegate: RadioButton {
                    required property var modelData
                    text: modelData.text
                    checked: dlg.level === modelData.value
                    onClicked: dlg.level = modelData.value
                }
            }
        }
    }
}
