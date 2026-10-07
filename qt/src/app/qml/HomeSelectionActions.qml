// xournal-qt: the actions on a selection in a bar at the bottom (a phone, or no room in the selection bar):
// Open, Copy, Move, Trash, and ⋮ for the rest.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: selectionActions
    objectName: "selectionActionBar"
    visible: home.selectionCount > 0 && home.selectionAtBottom
    Layout.fillWidth: true
    Layout.preferredHeight: 60 + home.safeBottom
    color: "#ffffff"
    Hairline { width: parent.width; color: "#dadce0" }
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        height: 60
        spacing: 0
        BarAction {
            objectName: "selectionOpenAction"
            iconName: "xopp-document-open"
            text: qsTr("Open")
            onClicked: home.openAll(home.currentModel.selectedPaths(), home.currentModel)
        }
        BarAction {
            objectName: "selectionCopyAction"
            iconName: "xopp-edit-copy"
            text: qsTr("Copy to")
            enabled: app.library.available
            onClicked: home.askTransfer(home.currentModel.selectedPaths(), true)
        }
        BarAction {
            objectName: "selectionMoveAction"
            iconName: "xqt-folder-input"
            text: qsTr("Move to")
            enabled: app.library.available
            onClicked: home.askTransfer(home.currentModel.selectedPaths(), false)
        }
        BarAction {
            objectName: "selectionTrashAction"
            iconName: "xqt-delete"
            text: qsTr("Trash")
            onClicked: home.askTrash(home.currentModel, home.currentModel.selectedPaths())
        }
        BarAction {
            id: selectionMoreButton
            objectName: "selectionMoreButton"
            iconName: "xqt-more"
            text: qsTr("More")
            onClicked: Popups.openAt(selectionMenu)
            AdaptiveMenu {
                id: selectionMenu
                objectName: "selectionMenu"
                title: qsTr("%1 selected").arg(home.selectionCount)
                AdaptiveMenuItem {
                    objectName: "selectAllItem"
                    text: qsTr("Select all")
                    onTriggered: home.currentModel.selectAll()
                }
                AdaptiveMenuItem {
                    objectName: "removeSelectedFromListItem"
                    text: qsTr("Remove from list")
                    offered: home.page === 1
                    onTriggered: app.recent.removePaths(app.recent.selectedPaths())
                }
            }
        }
    }

    // A button of the selection's bar at the bottom: its icon above its word
    component BarAction: AbstractButton {
        id: action
        property string iconName
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        implicitHeight: 56
        Accessible.name: text
        background: Rectangle {
            radius: 12
            color: action.pressed ? "#e8eaed" : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 2
            opacity: action.enabled ? 1 : 0.4
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: app.iconUrl(action.iconName)
                sourceSize.width: 24
                sourceSize.height: 24
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: action.width - 4
                text: action.text
                elide: Text.ElideRight
                font.pixelSize: 12
                color: "#3c4043"
            }
        }
    }
}
