// Page sidebar: thumbnails of the current document; tap to go to a page, long-press or ⋮ for page operations.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: sidebar
    color: "#eceef1"

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 14
        clip: true
        model: app.pages
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: app.pages.currentPage
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: entry
            required property int index
            required property int pageNumber
            required property real aspect
            required property string thumbnail
            required property bool current
            width: list.width
            height: frame.height + pageLabel.height + 4

            Rectangle {
                id: frame
                anchors.horizontalCenter: parent.horizontalCenter
                width: list.width - 36
                height: width * entry.aspect
                color: "white"
                border.width: entry.current ? 3 : 1
                border.color: entry.current ? Material.accentColor : "#b9bcc1"
                Image {
                    anchors.fill: parent
                    anchors.margins: frame.border.width
                    source: entry.thumbnail
                    asynchronous: true
                    cache: false
                    sourceSize.width: Math.round(width * Screen.devicePixelRatio)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                TapHandler {
                    onTapped: app.goToPage(entry.index)
                    onLongPressed: pageMenu.popup()
                }
                ToolButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    implicitWidth: 40
                    implicitHeight: 40
                    text: "⋮"
                    font.pixelSize: 20
                    Material.foreground: "#3c4043"
                    opacity: entry.current || hovered ? 1 : 0.55
                    onClicked: pageMenu.popup()
                }
            }
            Label {
                id: pageLabel
                anchors.top: frame.bottom
                anchors.topMargin: 3
                anchors.horizontalCenter: parent.horizontalCenter
                text: entry.pageNumber
                color: entry.current ? Material.accentColor : "#5f6368"
                font.weight: entry.current ? Font.DemiBold : Font.Normal
            }

            Menu {
                id: pageMenu
                MenuItem { text: qsTr("Insert page before"); onTriggered: app.insertPageBefore(entry.index) }
                MenuItem { text: qsTr("Insert page after"); onTriggered: app.insertPageAfter(entry.index) }
                MenuItem { text: qsTr("Duplicate page"); onTriggered: app.duplicatePage(entry.index) }
                MenuSeparator {}
                MenuItem { text: qsTr("Move up"); enabled: entry.index > 0; onTriggered: app.movePageUp(entry.index) }
                MenuItem {
                    text: qsTr("Move down")
                    enabled: entry.index < list.count - 1
                    onTriggered: app.movePageDown(entry.index)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Delete page")
                    enabled: list.count > 1
                    onTriggered: app.deletePage(entry.index)
                }
            }
        }
    }
}
