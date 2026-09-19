// Page sidebar: thumbnails of the current document; tap to go to a page, long-press or ⋮ for page operations.
// While searching, pages with hits are framed and the list can be limited to them.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: sidebar
    color: "#eceef1"

    SearchFilterChip {
        id: filterChip
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        visible: app.searchQuery !== ""
    }

    ListView {
        id: list
        objectName: "sidebarList"
        anchors.top: filterChip.visible ? filterChip.bottom : parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 14
        clip: true
        model: app.filteredPages
        boundsBehavior: Flickable.StopAtBounds
        // (count: re-evaluated when the filter changes)
        currentIndex: (app.filteredPages.count, app.filteredPages.rowOf(app.pages.currentPage))
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: list }

        delegate: Item {
            id: entry
            required property int pageIndex
            required property int pageNumber
            required property real aspect
            required property string thumbnail
            required property bool current
            required property var searchHits
            required property int currentSearchHit
            required property int searchHitCount
            width: list.width
            height: frame.height + pageLabel.height + 4

            Rectangle {
                id: frame
                anchors.horizontalCenter: parent.horizontalCenter
                width: list.width - 36
                height: width * entry.aspect
                color: "white"
                border.width: entry.current || entry.searchHitCount > 0 ? 3 : 1
                border.color: entry.current ? Material.accentColor : (entry.searchHitCount > 0 ? "#f9a825" : "#b9bcc1")
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
                Repeater {
                    model: entry.searchHits
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        x: modelData.x * frame.width - 1
                        y: modelData.y * frame.height - 1
                        width: Math.max(3, modelData.width * frame.width + 2)
                        height: Math.max(3, modelData.height * frame.height + 2)
                        color: index === entry.currentSearchHit ? "#ccff7800" : "#a0ffd200"
                    }
                }
                HitBadge { count: entry.searchHitCount; anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 4 }
                TapHandler {
                    onTapped: app.goToPage(entry.pageIndex)
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
                MenuItem { text: qsTr("Insert page before"); onTriggered: app.insertPageBefore(entry.pageIndex) }
                MenuItem { text: qsTr("Insert page after"); onTriggered: app.insertPageAfter(entry.pageIndex) }
                MenuItem { text: qsTr("Duplicate page"); onTriggered: app.duplicatePage(entry.pageIndex) }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Move up")
                    enabled: entry.pageIndex > 0
                    onTriggered: app.movePageUp(entry.pageIndex)
                }
                MenuItem {
                    text: qsTr("Move down")
                    enabled: entry.pageIndex < app.pages.count - 1
                    onTriggered: app.movePageDown(entry.pageIndex)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Delete page")
                    enabled: app.pages.count > 1
                    onTriggered: app.deletePage(entry.pageIndex)
                }
            }
        }
    }
}
