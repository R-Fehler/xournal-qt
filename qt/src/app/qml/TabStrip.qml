// Tab strip: the home tab (library, recent documents), one tab per open document (touch sized), close buttons, "+"
// for a new document.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: strip
    signal closeRequested(int index)
    implicitHeight: 46
    color: "#dfe1e5"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        spacing: 0

        // The home screen (library, recent documents): always the first tab.
        AbstractButton {
            id: homeTab
            objectName: "homeTab"
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
                // Middle click closes, like in browsers.
                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: strip.closeRequested(tab.index)
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
