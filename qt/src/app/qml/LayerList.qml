// The layers of the current page, top first, the background last (as in Xournal++). Tap a layer to draw on it,
// the eye hides it, ⋮ renames, duplicates, merges or deletes it. Everything is one undo step.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: panel

    ListView {
        id: list
        objectName: "layerList"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: buttons.top
        anchors.margins: 6
        clip: true
        spacing: 2
        model: app.layers
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: row
            required property int index
            required property string name
            required property bool layerVisible
            required property bool current
            required property bool isBackground
            required property int elementCount
            width: list.width
            height: 44
            onClicked: if (!isBackground) app.layers.select(index)

            background: Rectangle {
                radius: 8
                color: row.current ? "#e0e3f5" : (row.hovered ? "#f3f4f6" : "transparent")
            }
            contentItem: RowLayout {
                spacing: 4
                IconButton {
                    objectName: "layerVisibleButton"
                    iconName: row.layerVisible ? "xqt-eye" : "xqt-eye-off"
                    tip: row.layerVisible ? qsTr("Hide this layer") : qsTr("Show this layer")
                    implicitWidth: 34; implicitHeight: 34
                    icon.width: 19; icon.height: 19
                    icon.color: row.layerVisible ? "#3c4043" : "#9aa0a6"
                    enabled: !row.isBackground
                    opacity: row.isBackground ? 0.35 : 1
                    onClicked: app.layers.setVisible(row.index, !row.layerVisible)
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label {
                        text: row.name
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        color: row.layerVisible ? "#202124" : "#9aa0a6"
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                    }
                    Label {
                        visible: !row.isBackground
                        text: row.elementCount === 1 ? qsTr("1 element") : qsTr("%1 elements").arg(row.elementCount)
                        font.pixelSize: 11
                        color: "#6b6f75"
                    }
                }
                IconButton {
                    objectName: "layerMenuButton"
                    iconName: "xqt-more"
                    tip: qsTr("More")
                    implicitWidth: 34; implicitHeight: 34
                    icon.width: 18; icon.height: 18
                    visible: !row.isBackground
                    onClicked: Popups.openAt(layerMenu)
                    Menu {
                        id: layerMenu
                        objectName: "layerMenu"
                        MenuItem { text: qsTr("Rename…"); onTriggered: { renameField.text = row.name; renameDialog.row = row.index; renameDialog.open() } }
                        MenuItem { text: qsTr("Duplicate"); onTriggered: app.layers.duplicate(row.index) }
                        MenuItem { text: qsTr("Move up"); enabled: row.index > 0; onTriggered: app.layers.moveUp(row.index) }
                        MenuItem {
                            text: qsTr("Move down")
                            enabled: row.index < app.layers.count - 2
                            onTriggered: app.layers.moveDown(row.index)
                        }
                        MenuItem {
                            text: qsTr("Merge into the one below")
                            enabled: row.index < app.layers.count - 2
                            onTriggered: app.layers.mergeDown(row.index)
                        }
                        MenuItem { text: qsTr("Delete"); onTriggered: app.layers.remove(row.index) }
                    }
                }
            }
        }
    }

    RowLayout {
        id: buttons
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        spacing: 4
        Button {
            objectName: "addLayerButton"
            text: qsTr("New layer")
            Layout.fillWidth: true
            onClicked: app.layers.addLayer(false)
        }
        IconButton {
            objectName: "showAllLayersButton"
            iconName: "xqt-eye"
            tip: qsTr("Show all layers")
            implicitWidth: 38; implicitHeight: 38
            icon.width: 20; icon.height: 20
            onClicked: app.layers.showAll(true)
        }
    }

    Dialog {
        id: renameDialog
        objectName: "layerRenameDialog"
        property int row: -1
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 320
        title: qsTr("Rename the layer")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onOpened: { renameField.forceActiveFocus(); renameField.selectAll() }
        onAccepted: app.layers.rename(row, renameField.text)
        TextField {
            id: renameField
            objectName: "layerRenameField"
            width: parent ? parent.width : 280
            onAccepted: renameDialog.accept()
        }
    }
}
