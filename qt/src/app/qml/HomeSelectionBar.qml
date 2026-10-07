// xournal-qt: the bar at the top while items are selected: the count, and what to do with them where the row
// has room (else HomeSelectionActions.qml at the bottom).
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: selectionBar
    // (what the other parts use)
    readonly property alias selectionLabel: selectionLabel
    readonly property alias selectionFull: selectionFull
    objectName: "homeSelectionBar"
    visible: home.selectionCount > 0
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    Layout.topMargin: home.shortLayout ? 4 : 10
    Layout.preferredHeight: 48
    radius: 24
    color: "#e8eaf6"
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 8
        spacing: 2
        IconButton {
            objectName: "clearSelectionButton"
            label: qsTr("Clear the selection")
            iconName: "xqt-close"
            tip: qsTr("Clear the selection (Esc)")
            onClicked: home.currentModel.clearSelection()
        }
        Label {
            id: selectionLabel
            objectName: "selectionLabel"
            text: qsTr("%1 selected").arg(home.selectionCount)
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#283593"
            elide: Text.ElideRight
            Layout.leftMargin: 4
            Layout.fillWidth: home.selectionAtBottom
        }
        // (measured also while it is hidden: whether it fits decides where the actions are)
        RowLayout {
            id: selectionFull
            visible: !home.selectionAtBottom
            Layout.fillWidth: true
            spacing: 2
            Button { objectName: "selectAllButton"; text: qsTr("Select all"); flat: true; onClicked: home.currentModel.selectAll() }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "openSelectedButton"
                text: qsTr("Open")
                flat: true
                icon.source: app.iconUrl("xopp-document-open")
                onClicked: home.openAll(home.currentModel.selectedPaths(), home.currentModel)
            }
            Button {
                objectName: "copySelectedButton"
                text: qsTr("Copy to…")
                flat: true
                enabled: app.library.available
                icon.source: app.iconUrl("xopp-edit-copy")
                onClicked: home.askTransfer(home.currentModel.selectedPaths(), true)
            }
            Button {
                objectName: "moveSelectedButton"
                text: qsTr("Move to…")
                flat: true
                enabled: app.library.available
                icon.source: app.iconUrl("xqt-folder-input")
                onClicked: home.askTransfer(home.currentModel.selectedPaths(), false)
            }
            Button {
                visible: home.page === 1
                text: qsTr("Remove from list")
                flat: true
                onClicked: app.recent.removePaths(app.recent.selectedPaths())
            }
            Button {
                objectName: "trashSelectedButton"
                text: qsTr("Trash…")
                flat: true
                icon.source: app.iconUrl("xqt-delete")
                onClicked: home.askTrash(home.currentModel, home.currentModel.selectedPaths())
            }
        }
    }
}
