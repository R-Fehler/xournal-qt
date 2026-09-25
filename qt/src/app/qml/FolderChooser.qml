// Android with "All files access": "Open a folder as library…" picks the folder here. Android's own folder picker
// (ACTION_OPEN_DOCUMENT_TREE) refuses the Download folder and the storage's root by design; the app can read them
// by path, so it lists the folders itself: from the phone's storage down, one level at a time.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: chooser
    objectName: "folderChooser"
    /// Where the list starts and above which it does not go (the phone's storage)
    property string root: ""
    /// The folder shown
    property string folder: ""
    property var folders: []
    /// "Use this folder"
    signal chosen(string path)

    function openAt(start) {
        root = start
        show(start)
        open()
    }
    function show(path) {
        folder = path
        folders = app.subfolders(path)
        list.positionViewAtBeginning()
    }
    function up() {
        if (folder !== root) show(folder.substring(0, folder.lastIndexOf("/")))
    }
    /// The folder as the user sees it: "Phone storage / Download / Papers"
    function shown(path) {
        const rest = path.substring(root.length).split("/").filter(function(p) { return p !== "" })
        return [qsTr("Phone storage")].concat(rest).join(" / ")
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Open a folder as library")
    width: Math.min(parent ? parent.width - 32 : 480, 480)
    height: Math.min(parent ? parent.height - 64 : 640, 640)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            IconButton {
                objectName: "folderChooserUp"
                iconName: "xqt-arrow-up"
                tip: qsTr("Up")
                enabled: chooser.folder !== chooser.root
                onClicked: chooser.up()
            }
            Label {
                Layout.fillWidth: true
                text: chooser.shown(chooser.folder)
                elide: Text.ElideLeft
                font.weight: Font.DemiBold
            }
        }
        ListView {
            id: list
            objectName: "folderChooserList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: chooser.folders
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                objectName: "folderChooserEntry"
                required property var modelData
                width: ListView.view.width
                text: modelData.name
                icon.source: app.iconUrl("xqt-folder")
                icon.color: "#566d86"
                onClicked: chooser.show(modelData.path)
            }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No folders in here")
                color: "#6b6f75"
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "folderChooserUse"
            text: qsTr("Use this folder")
            // (not the whole storage: every file of the phone would be one library)
            enabled: chooser.folder !== chooser.root
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    onAccepted: chosen(folder)
}
