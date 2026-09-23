// Tab strip: the home tab (library, recent documents), one tab per open document (touch sized), close buttons, "+"
// for a new document.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: strip
    signal closeRequested(int index)
    /// While a tab is dragged off the strip: which one, and how far (the window shows what will happen)
    property int dragIndex: -1
    property real dragDistance: 0
    readonly property real undockDistance: 60
    signal overviewRequested()
    /// The tab should get a window of its own (dragged off the strip), or go back to the main window.
    signal undockRequested(int index)
    signal dockRequested(int index)
    implicitHeight: 46
    color: "#dfe1e5"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        spacing: 0

        // The home screen (library, recent documents): always the first tab (not in a window of its own).
        AbstractButton {
            id: homeTab
            objectName: "homeTab"
            visible: !app.secondaryWindow
            readonly property bool current: app.homeVisible
            Layout.fillHeight: true
            Layout.rightMargin: 4
            implicitWidth: homeRow.implicitWidth + 28
            hoverEnabled: true
            onClicked: app.homeVisible = true
            ToolTip.visible: hovered
            ToolTip.text: app.library.available ? qsTr("Library “%1” and recent documents").arg(app.library.name)
                                                : qsTr("Recent documents")
            ToolTip.delay: 600
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 5
                    radius: 10
                    color: homeTab.current ? "#ffffff" : (homeTab.hovered ? "#eceef1" : "transparent")
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.radius
                        color: parent.color
                    }
                }
            }
            contentItem: Item {
                RowLayout {
                    id: homeRow
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 2
                    spacing: 6
                    Image { source: app.iconUrl("xqt-library"); sourceSize.width: 18; sourceSize.height: 18 }
                    Label {
                        text: app.library.available ? app.library.name : qsTr("Home")
                        elide: Text.ElideRight
                        Layout.maximumWidth: 160
                        color: homeTab.current ? "#202124" : "#5f6368"
                        font.weight: homeTab.current ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
        // All open documents: beside the tabs it belongs to
        IconButton {
            objectName: "overviewButton"
            iconName: "xqt-tabs-grid"
            tip: qsTr("All open documents (Ctrl+Shift+E)")
            implicitWidth: 38
            implicitHeight: 38
            icon.width: 22
            icon.height: 22
            Layout.rightMargin: 4
            Layout.alignment: Qt.AlignVCenter
            onClicked: strip.overviewRequested()
        }
        // The previous / next document, like Ctrl+PgUp / Ctrl+PgDown (round the ends); only with more than one
        IconButton {
            objectName: "previousTabButton"
            visible: app.tabs.count > 1
            iconName: "xqt-chevron-left"
            tip: qsTr("Previous document (Ctrl+PgUp)")
            implicitWidth: 38
            implicitHeight: 38
            icon.width: 22
            icon.height: 22
            Layout.alignment: Qt.AlignVCenter
            onClicked: app.previousTab()
        }
        IconButton {
            objectName: "nextTabButton"
            visible: app.tabs.count > 1
            iconName: "xqt-chevron-right"
            tip: qsTr("Next document (Ctrl+PgDown)")
            implicitWidth: 38
            implicitHeight: 38
            icon.width: 22
            icon.height: 22
            Layout.rightMargin: 4
            Layout.alignment: Qt.AlignVCenter
            onClicked: app.nextTab()
        }
        ListView {
            id: list
            objectName: "tabList"
            // As wide as the tabs, up to all the room there is (then it scrolls); the spacer behind it only gets
            // what is left (two fill items would share the room: the tabs would scroll at half the width).
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: contentWidth
            Layout.maximumWidth: contentWidth
            orientation: ListView.Horizontal
            model: app.tabs
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: app.currentTab
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: AbstractButton {
                id: tab
                required property int index
                required property string title
                required property bool modified
                required property bool current
                readonly property bool shown: current && !app.homeVisible
                width: Math.min(260, Math.max(140, titleLabel.implicitWidth + 70))
                height: list.height
                hoverEnabled: true
                onClicked: app.currentTab = index
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(tabMenu, point.position) }
                }
                TapHandler {
                    id: tabLongPress
                    acceptedButtons: Qt.LeftButton
                    onLongPressed: Popups.openAt(tabMenu, tabLongPress.point.position)
                }
                // Dragged off the strip: a window of its own. The tab follows the finger while it is held.
                // In scene coordinates: the tab moves with the finger, so its own coordinates would not grow
                readonly property real dragOffset: tabDrag.active ? tabDrag.centroid.scenePosition.y
                                                                  - tabDrag.centroid.scenePressPosition.y : 0
                property real lastDragOffset: 0  // where it was let go (the handler forgets it)
                onDragOffsetChanged: if (tabDrag.active) {
                    lastDragOffset = dragOffset
                    strip.dragDistance = Math.abs(dragOffset)
                }
                transform: Translate { y: Math.max(0, tab.dragOffset) }
                opacity: tabDrag.active ? 0.85 : 1
                DragHandler {
                    id: tabDrag
                    target: null
                    onActiveChanged: {
                        if (active) {
                            strip.dragIndex = tab.index
                        } else {
                            strip.dragIndex = -1
                            strip.dragDistance = 0
                            if (Math.abs(tab.lastDragOffset) > strip.undockDistance) {
                                app.secondaryWindow ? strip.dockRequested(tab.index)
                                                    : strip.undockRequested(tab.index)
                            }
                        }
                    }
                }
                Menu {
                    id: tabMenu
                    objectName: "tabMenu"
                    MenuItem {
                        objectName: "undockTabItem"
                        text: app.secondaryWindow ? qsTr("Move to the main window")
                                                  : qsTr("Move to a window of its own")
                        onTriggered: app.secondaryWindow ? strip.dockRequested(tab.index)
                                                         : strip.undockRequested(tab.index)
                    }
                    MenuItem { text: qsTr("Close"); onTriggered: strip.closeRequested(tab.index) }
                }

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 5
                        radius: 10
                        color: tab.shown ? "#ffffff" : (tab.hovered ? "#eceef1" : "transparent")
                        // square bottom corners, so the current tab merges into the tool bar below
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.radius
                            color: parent.color
                        }
                    }
                }
                contentItem: RowLayout {
                    spacing: 2
                    Label {
                        id: titleLabel
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.topMargin: 4
                        text: (tab.modified ? "● " : "") + tab.title
                        elide: Text.ElideMiddle
                        color: tab.shown ? "#202124" : "#5f6368"
                        font.weight: tab.shown ? Font.DemiBold : Font.Normal
                    }
                    ToolButton {
                        Layout.topMargin: 4
                        implicitWidth: 36
                        implicitHeight: 36
                        text: "✕"
                        font.pixelSize: 14
                        Material.foreground: "#5f6368"
                        onClicked: strip.closeRequested(tab.index)
                    }
                }
            }
        }
        ToolButton {
            objectName: "newTabButton"
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 44
            implicitHeight: 40
            text: "+"
            font.pixelSize: 22
            Material.foreground: "#3c4043"
            onClicked: app.newDocument()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("New document")
            ToolTip.delay: 600
        }
        Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
    }
}
