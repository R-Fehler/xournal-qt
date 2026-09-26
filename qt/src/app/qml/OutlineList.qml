// The table of contents as text, for the page sidebar: indented by level, the current section highlighted; tap to
// go there, the arrows collapse and expand. The document's bookmarks come first, in a section of their own (tap: go
// there; press and hold or a right click: rename, remove; qt/docs/bookmarks.md).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ListView {
    id: list
    objectName: "outlineList"
    clip: true
    model: app.outline
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: app.outline.currentRow
    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
    ScrollBar.vertical: ScrollBar {}
    TouchpadMomentum { flickable: list }

    header: Column {
        objectName: "bookmarksSection"
        width: list.width
        visible: app.bookmarks.length > 0
        height: visible ? implicitHeight : 0
        Label {
            text: qsTr("Bookmarks")
            leftPadding: 10
            topPadding: 6
            bottomPadding: 2
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: "#5f6368"
        }
        Repeater {
            model: app.bookmarks
            delegate: ItemDelegate {
                id: mark
                required property var modelData
                objectName: "bookmarkEntry"
                width: list.width
                leftPadding: 6
                rightPadding: 6
                topPadding: 6
                bottomPadding: 6
                highlighted: modelData.page === app.pageNumber - 1
                onClicked: app.jumpToPage(modelData.page)
                onPressAndHold: markMenu.popup()
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: markMenu.popup() }
                Menu {
                    id: markMenu
                    MenuItem {
                        text: qsTr("Rename…")
                        onTriggered: renameMark.openFor(mark.modelData.page)
                    }
                    MenuItem {
                        objectName: "removeBookmarkItem"
                        text: qsTr("Remove bookmark")
                        onTriggered: app.toggleBookmark(mark.modelData.page)
                    }
                    MenuItem {
                        text: qsTr("Copy link to this page")
                        onTriggered: app.copyPageLink(mark.modelData.page)
                    }
                }
                contentItem: RowLayout {
                    spacing: 6
                    Image {
                        source: app.iconUrl("xqt-bookmark-filled")
                        sourceSize: Qt.size(14, 18)
                        Layout.leftMargin: 8
                    }
                    Label {
                        Layout.fillWidth: true
                        text: mark.modelData.label
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        color: "#202124"
                    }
                    Label {
                        text: mark.modelData.page + 1
                        font.pixelSize: 12
                        color: "#6b6f75"
                    }
                }
            }
        }
        Rectangle {
            visible: app.outline.available
            width: parent.width - 16
            x: 8
            height: 1
            color: "#d5d8dc"
        }
        Item { width: 1; height: 4; visible: app.outline.available }
    }
    BookmarkDialog { id: renameMark }

    delegate: ItemDelegate {
        id: entry
        required property int index
        required property string title
        required property int level
        required property int page
        required property bool hasChildren
        required property bool expanded
        width: ListView.view.width
        leftPadding: 4 + Math.min(level, 6) * 12
        rightPadding: 6
        topPadding: 6
        bottomPadding: 6
        highlighted: index === app.outline.currentRow
        onClicked: if (page >= 0) app.jumpToPage(page)
        // Press and hold, or a right click: a link to the chapter (qt/docs/links.md)
        onPressAndHold: if (page >= 0) chapterMenu.popup()
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: if (entry.page >= 0) chapterMenu.popup() }
        Menu {
            id: chapterMenu
            MenuItem {
                objectName: "copyChapterLink"
                text: qsTr("Copy link to this chapter")
                onTriggered: app.copyChapterLink(entry.page, entry.title)
            }
        }
        contentItem: RowLayout {
            spacing: 2
            ToolButton {
                visible: entry.hasChildren
                implicitWidth: 28; implicitHeight: 28
                icon.source: app.iconUrl(entry.expanded ? "xqt-chevron-down-small" : "xqt-chevron-right")
                icon.width: 16; icon.height: 16
                display: AbstractButton.IconOnly
                onClicked: app.outline.toggle(entry.index)
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: entry.hasChildren ? 0 : 30
                text: entry.title
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: entry.level === 0 ? 14 : 13
                font.weight: entry.level === 0 ? Font.DemiBold : Font.Normal
                color: entry.page >= 0 ? "#202124" : "#9aa0a6"
            }
            Label {
                text: entry.page >= 0 ? entry.page + 1 : ""
                font.pixelSize: 12
                color: "#6b6f75"
            }
        }
    }
}
