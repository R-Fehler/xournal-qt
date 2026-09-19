// Overview of all open documents (tabs) as a grid of cards with the current page of each: tap a card to switch to
// it, × to close it, + for a new document. Keyboard: arrows, Enter, Delete, Escape.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: overview
    signal closeRequested(int index)

    modal: true
    focus: true
    // The whole window, including the tab strip and tool bar.
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    padding: 0
    closePolicy: Popup.CloseOnEscape
    // Thumbnails are rendered again each time the overview opens (their URLs change with this counter).
    property int generation: 0
    onAboutToShow: {
        generation++
        grid.currentIndex = app.currentTab
        grid.forceActiveFocus()
    }

    background: Rectangle { color: "#eef0f3" }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }

    function activate(index) {
        app.currentTab = index
        overview.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Label {
                text: grid.count === 1 ? qsTr("1 open document") : qsTr("%1 open documents").arg(grid.count)
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton {
                iconName: "xopp-document-new"
                tip: qsTr("New document")
                onClicked: { app.newDocument(); overview.close() }
            }
            IconButton { iconName: "xqt-close"; tip: qsTr("Back"); onClicked: overview.close() }
        }

        GridView {
            id: grid
            objectName: "tabGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            clip: true
            model: app.tabs
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(1, Math.floor(width / 280))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round(cellWidth * 1.25)
            ScrollBar.vertical: ScrollBar {}

            Keys.onReturnPressed: overview.activate(currentIndex)
            Keys.onEnterPressed: overview.activate(currentIndex)
            Keys.onSpacePressed: overview.activate(currentIndex)
            Keys.onDeletePressed: overview.closeRequested(currentIndex)

            delegate: Item {
                id: cell
                required property int index
                required property string title
                required property bool modified
                required property bool current
                required property string thumbnail
                required property int pageCount
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool highlighted: GridView.isCurrentItem && grid.activeFocus

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 10
                    radius: 12
                    color: "#ffffff"
                    border.width: cell.current || cell.highlighted ? 3 : 1
                    border.color: cell.current || cell.highlighted ? Material.accentColor : "#c9ccd1"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#f4f5f7"
                            radius: 6
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: 6
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                source: overview.visible ? cell.thumbnail + "/" + overview.generation : ""
                                sourceSize.width: Math.round(width * Screen.devicePixelRatio)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle {
                                visible: cell.modified
                                width: 8; height: 8; radius: 4
                                color: Material.accentColor
                            }
                            Label {
                                Layout.fillWidth: true
                                text: cell.title
                                elide: Text.ElideMiddle
                                font.weight: cell.current ? Font.DemiBold : Font.Normal
                            }
                            Label {
                                text: cell.pageCount === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(cell.pageCount)
                                color: "#6b6f75"
                                font.pixelSize: 12
                            }
                        }
                    }
                    TapHandler {
                        onTapped: overview.activate(cell.index)
                    }
                    ToolButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        implicitWidth: 40
                        implicitHeight: 40
                        icon.source: app.iconUrl("xqt-close")
                        icon.color: "#3c4043"
                        display: AbstractButton.IconOnly
                        onClicked: overview.closeRequested(cell.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Close")
                    }
                }
            }
        }
    }
}
