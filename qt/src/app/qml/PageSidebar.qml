// Page sidebar: thumbnails of the current document. Tap a page to go there; Ctrl/Shift+click to select pages;
// press and hold to drag the selected pages to another place; right click or ⋮ for the page menu; Ctrl+C/X/V,
// Delete and Ctrl+Z (page undo) with the keyboard. While searching, pages with hits are framed and the list can
// be limited to them.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: sidebar
    color: "#eceef1"
    /// The table of contents instead of the pages
    property bool showContents: false
    signal contentsOverviewRequested()

    // Pages | Contents (when the document has a table of contents)
    RowLayout {
        id: switchRow
        visible: app.outline.available
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        spacing: 2
        Repeater {
            model: [qsTr("Pages"), qsTr("Contents")]
            delegate: AbstractButton {
                id: switchButton
                required property int index
                required property string modelData
                objectName: index === 0 ? "sidebarPagesButton" : "sidebarContentsButton"
                Layout.fillWidth: true
                implicitHeight: 32
                readonly property bool active: (index === 1) === sidebar.showContents
                onClicked: sidebar.showContents = index === 1
                background: Rectangle { radius: 16; color: switchButton.active ? "#ffffff" : "transparent" }
                contentItem: Label {
                    text: switchButton.modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.weight: switchButton.active ? Font.DemiBold : Font.Normal
                }
            }
        }
        IconButton {
            iconName: "xqt-toc"
            tip: qsTr("Contents overview with the pages (Ctrl+Alt+O)")
            implicitWidth: 34; implicitHeight: 34
            icon.width: 20; icon.height: 20
            onClicked: sidebar.contentsOverviewRequested()
        }
    }
    OutlineList {
        visible: sidebar.showContents && app.outline.available
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }

    SearchFilterChip {
        id: filterChip
        anchors.top: switchRow.visible ? switchRow.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        visible: app.searchQuery !== "" && !sidebar.showContents
    }

    PageKeys { id: pageKeys }
    PageMenu { id: pageMenu }

    ListView {
        id: list
        objectName: "sidebarList"
        visible: !sidebar.showContents || !app.outline.available
        anchors.top: filterChip.visible ? filterChip.bottom : (switchRow.visible ? switchRow.bottom : parent.top)
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
        onCurrentIndexChanged: if (currentIndex >= 0 && !pageDrag.active) positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: list }

        Keys.onShortcutOverride: function(event) { event.accepted = pageKeys.isPageKey(event) }
        Keys.onPressed: function(event) {
            if (pageKeys.handle(event)) event.accepted = true
            else if (event.key === Qt.Key_Escape) { app.pages.clearSelection(); event.accepted = true }
        }

        delegate: Item {
            id: entry
            required property int pageIndex
            required property int pageNumber
            required property real aspect
            required property string thumbnail
            required property bool current
            required property bool selected
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
                border.width: entry.current || entry.selected || entry.searchHitCount > 0 ? 3 : 1
                border.color: entry.selected || entry.current ? Material.accentColor
                            : (entry.searchHitCount > 0 ? "#f9a825" : "#b9bcc1")
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
                SelectionMark { visible: entry.selected }
                HitBadge { count: entry.searchHitCount; anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 4 }
                PageArea {
                    dragOverlay: pageDrag
                    pageIndex: entry.pageIndex
                    delegateItem: entry
                    onTapped: function(modifiers) {
                        list.forceActiveFocus()
                        if (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                            app.pages.select(entry.pageIndex, modifiers)
                        } else {
                            app.pages.clearSelection()
                            app.pages.setAnchor(entry.pageIndex)
                            app.jumpToPage(entry.pageIndex)
                        }
                    }
                    onHeld: list.forceActiveFocus()
                    onMenuRequested: function(x, y) { pageMenu.openFor(entry.pageIndex, frame, x, y) }
                }
                ToolButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    implicitWidth: 40
                    implicitHeight: 40
                    text: "⋮"
                    font.pixelSize: 20
                    Material.foreground: "#3c4043"
                    opacity: entry.current || entry.selected || hovered ? 1 : 0.55
                    onClicked: pageMenu.openFor(entry.pageIndex, this, 0, height)
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
        }
    }
    PageDragOverlay {
        id: pageDrag
        view: list
    }
}
