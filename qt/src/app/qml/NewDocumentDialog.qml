// New document: name, page background, paper size and orientation (the settings for new pages, so the next new
// document starts with the same choice). In the library the document is saved at once in the current folder.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "newDocumentDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("New document")
    width: Math.min(parent ? parent.width * 0.94 : 640, 660)
    readonly property var s: app.settings
    readonly property bool canSaveInLibrary: app.library.available
    property int bgIndex: 0
    property int paper: 1
    property bool landscape: false

    onAboutToShow: {
        nameField.text = ""
        bgIndex = Math.max(0, s.get("pageBackground"))
        paper = Math.max(0, s.get("paperFormat"))
        landscape = s.get("landscape")
        libraryBox.checked = canSaveInLibrary
        nameField.forceActiveFocus()
    }

    function create() {
        s.set("pageBackground", bgIndex)
        s.set("paperFormat", paper)
        s.set("landscape", landscape)
        app.createDocument(nameField.text, libraryBox.checked)
        dlg.close()
    }

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 12

        TextField {
            id: nameField
            objectName: "newDocumentName"
            Layout.fillWidth: true
            enabled: libraryBox.checked
            placeholderText: qsTr("Name (Untitled)")
            selectByMouse: true
            Keys.onReturnPressed: dlg.create()
            Keys.onEnterPressed: dlg.create()
        }

        Label { text: qsTr("Background"); font.weight: Font.DemiBold }
        BackgroundChooser {
            Layout.fillWidth: true
            selected: dlg.bgIndex
            landscape: dlg.landscape
            onChosen: function(index) { dlg.bgIndex = index }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label { text: qsTr("Paper") }
            ComboBox {
                id: paperBox
                objectName: "paperBox"
                Layout.preferredWidth: 210
                model: dlg.s.paperFormats
                currentIndex: dlg.paper
                onActivated: {
                    dlg.paper = currentIndex
                    if (dlg.s.paperIsWide(currentIndex)) dlg.landscape = true  // (a slide is landscape)
                }
            }
            Item { Layout.fillWidth: true }
            ButtonGroup { id: orientation }
            Button {
                objectName: "portraitButton"
                text: qsTr("Portrait")
                checkable: true
                checked: !dlg.landscape
                flat: true
                ButtonGroup.group: orientation
                onClicked: dlg.landscape = false
            }
            Button {
                objectName: "landscapeButton"
                text: qsTr("Landscape")
                checkable: true
                checked: dlg.landscape
                flat: true
                ButtonGroup.group: orientation
                onClicked: dlg.landscape = true
            }
        }

        CheckBox {
            id: libraryBox
            objectName: "saveInLibrary"
            visible: dlg.canSaveInLibrary
            Layout.fillWidth: true
            text: {
                const crumbs = app.library.breadcrumbs.map(function(c) { return c.name })
                return qsTr("Save in the library: %1").arg(crumbs.join(" › "))
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Create"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onAccepted: dlg.create()
        onRejected: dlg.close()
    }
}
