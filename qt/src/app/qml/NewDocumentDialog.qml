// New document: name, page background, paper size and orientation (the settings for new pages, so the next new
// document starts with the same choice). In the library the document is saved at once in the current folder.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Dialog {
    id: dlg
    objectName: "newDocumentDialog"
    parent: Overlay.overlay
    // In the middle, or above the soft keyboard while it is open: then it is as high as there is room above the
    // keyboard, and what is in it scrolls, so that Create stays in reach
    readonly property real keyboardTop: {
        const r = Qt.inputMethod.keyboardRectangle
        // (Android reports it in the screen's pixels, not in the window's units)
        return Qt.inputMethod.visible && r.height > 0 ? r.y / (Qt.platform.os === "android" ? Screen.devicePixelRatio : 1)
                                                      : Infinity
    }
    /// (the status bar over the window's top, Main.qml)
    readonly property real safeTop: ApplicationWindow.window && ApplicationWindow.window.safeTop !== undefined
                                    ? ApplicationWindow.window.safeTop : 0
    readonly property real room: parent ? Math.min(parent.height, keyboardTop) - safeTop - 16 : 600
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round(Math.max(safeTop + 8, Math.min((parent.height - height) / 2, keyboardTop - height - 8))) : 0
    height: Math.min(implicitHeight, room)
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
        // The name field gets the keys at once, but not on a phone or tablet: there that opens the soft keyboard over
        // half of the dialog before anything is typed (a tap on the field opens it)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios")
            nameField.forceActiveFocus()
    }

    function create() {
        s.set("pageBackground", bgIndex)
        s.set("paperFormat", paper)
        s.set("landscape", landscape)
        app.createDocument(nameField.text, libraryBox.checked)
        dlg.close()
    }

    Flickable {
        id: body
        anchors.fill: parent
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
    ColumnLayout {
        id: column
        width: body.width
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
            EnterKey.type: Qt.EnterKeyDone  // (the soft keyboard's Enter key creates the document)
        }

        Label { text: qsTr("Background"); font.weight: Font.DemiBold }
        BackgroundChooser {
            Layout.fillWidth: true
            selected: dlg.bgIndex
            landscape: dlg.landscape
            onChosen: function(index) { dlg.bgIndex = index }
        }

        // Paper and orientation: one row, or two in a narrow window (a phone)
        GridLayout {
            Layout.fillWidth: true
            columns: dlg.availableWidth < 520 ? 1 : 2
            columnSpacing: 12
          RowLayout {
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
          }
          RowLayout {
            Layout.fillWidth: true
            spacing: 12
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

    }

    footer: DialogButtonBox {
        Button { text: qsTr("Create"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onAccepted: dlg.create()
        onRejected: dlg.close()
    }
}
