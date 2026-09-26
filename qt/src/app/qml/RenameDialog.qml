// "Rename…" of the ⋮ menu (qt/rename): the document's name, with its extension as fixed text beside it (it stays).
// A name that cannot be used says why, and OK waits for one that can. What is renamed with it (its PDF, its pictures)
// is said below. A new document that was never saved gets the name it is saved under (its tab's title).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "renameDocumentDialog"
    property int tab: -1
    property var info: ({})
    property string problem: ""
    function openFor(index) {
        tab = index
        info = app.tabRenameInfo(index)
        field.text = info.name || ""
        problem = info.problem || ""
        open()
    }
    function tryAccept() {
        problem = app.tabRenameProblem(tab, field.text)
        if (problem === "") accept()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(460, parent ? parent.width - 32 : 460)
    title: info.unsaved ? qsTr("Name the document") : qsTr("Rename")
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(function() { return dialog.problem === "" })
    onOpened: {
        field.forceActiveFocus()
        field.selectAll()
    }
    onAccepted: app.renameTab(tab, field.text)

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TextField {
                id: field
                objectName: "renameDocumentField"
                Layout.fillWidth: true
                selectByMouse: true
                onTextEdited: dialog.problem = app.tabRenameProblem(dialog.tab, text)
                Keys.onReturnPressed: dialog.tryAccept()
                Keys.onEnterPressed: dialog.tryAccept()
            }
            Label {
                objectName: "renameDocumentExtension"
                visible: text !== ""
                text: dialog.info.extension || ""
                color: "#5f6368"
            }
        }
        Label {
            objectName: "renameDocumentProblem"
            visible: dialog.problem !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.problem
            color: "#b3261e"
        }
        Label {
            visible: text !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#6b6f75"
            text: dialog.info.note || ""
        }
    }
}
