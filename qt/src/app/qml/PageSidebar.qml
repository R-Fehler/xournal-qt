// Page sidebar: thumbnails of the current document. Tap a page to go there; Ctrl/Shift+click to select pages;
// press and hold to drag the selected pages to another place; right click or ⋮ for the page menu; Ctrl+C/X/V,
// Delete and Ctrl+Z (the one undo of the document) with the keyboard. While searching, pages with hits are framed and the list can
// be limited to them. The last button shows the document's annotations (AnnotationList).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: sidebar
    color: "#eceef1"
    /// "pages", "layers", "contents" or "annotations"
    property string mode: "pages"
    readonly property bool showContents: mode === "contents"
    /// A table of contents, or bookmarks (listed at the top of it)
    readonly property bool hasContents: app.outline.available || app.bookmarks.length > 0
    // The annotations are read only while they are shown
    Binding { target: app.annotations; property: "active"; value: sidebar.visible && sidebar.mode === "annotations" }
    onModeChanged: if (mode === "contents" && !hasContents) mode = "pages"

    // Pages | Layers | Contents (the last one when the document has a table of contents)
    RowLayout {
        id: switchRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        spacing: 2
        Repeater {
            model: [{ key: "pages", text: qsTr("Pages") },
                    { key: "layers", text: qsTr("Layers") },
                    { key: "contents", text: qsTr("Contents") }]
            delegate: AbstractButton {
                id: switchButton
                required property int index
                required property var modelData
                objectName: "sidebar" + modelData.key.charAt(0).toUpperCase() + modelData.key.slice(1) + "Button"
                visible: modelData.key !== "contents" || sidebar.hasContents
                Layout.fillWidth: true
                implicitHeight: 32
                readonly property bool active: sidebar.mode === modelData.key
                onClicked: sidebar.mode = modelData.key
                background: Rectangle { radius: 16; color: switchButton.active ? "#ffffff" : "transparent" }
                contentItem: Label {
                    text: switchButton.modelData.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.weight: switchButton.active ? Font.DemiBold : Font.Normal
                }
            }
        }
        // Annotations: an icon (the words do not fit beside the others)
        AbstractButton {
            id: annotationsButton
            objectName: "sidebarAnnotationsButton"
            implicitWidth: 36
            implicitHeight: 32
            readonly property bool active: sidebar.mode === "annotations"
            onClicked: sidebar.mode = "annotations"
            Accessible.name: qsTr("Annotations")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Annotations")
            background: Rectangle { radius: 16; color: annotationsButton.active ? "#ffffff" : "transparent" }
            contentItem: Item {
                Image {
                    anchors.centerIn: parent
                    source: app.iconUrl("xopp-tool-highlighter")
                    sourceSize.width: 18
                    sourceSize.height: 18
                    opacity: annotationsButton.active ? 1 : 0.75
                }
            }
        }
    }
    AnnotationList {
        visible: sidebar.mode === "annotations"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
    LayerList {
        visible: sidebar.mode === "layers"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
    OutlineList {
        visible: sidebar.showContents && sidebar.hasContents
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
        visible: app.searchQuery !== "" && sidebar.mode === "pages"
    }

    PageKeys { id: pageKeys }
    PageMenu { id: pageMenu }

    ListView {
        id: list
        objectName: "sidebarList"
        visible: sidebar.mode === "pages"
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
        property RaceWatch race: RaceWatch { flickable: list }
        // At the end: add pages
        footer: AppendPages { width: list.width; visible: !app.homeVisible }

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
            required property string sketch
            required property bool current
            required property bool selected
            required property var searchHits
            required property int currentSearchHit
            required property int searchHitCount
            /// Its bookmark as shown ("": none)
            required property string bookmark
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
                PagePicture {
                    anchors.fill: parent
                    anchors.margins: frame.border.width
                    objectName: "sidebarThumbnail"
                    sketch: entry.sketch
                    thumbnail: entry.thumbnail
                    racing: list.race.racing
                    // By the frame, not by this image: the frame's border is thicker on the current page, and a new
                    // size would draw the page again - each page that is scrolled past blinked
                    sourceWidth: Math.round(frame.width * Screen.devicePixelRatio)
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
                // A bookmarked page: the ribbon (qt/docs/bookmarks.md)
                Image {
                    objectName: "sidebarRibbon"
                    visible: entry.bookmark !== ""
                    anchors.right: parent.right
                    anchors.rightMargin: 42
                    y: -3
                    source: app.iconUrl("xqt-bookmark-filled")
                    sourceSize: Qt.size(16, 20)
                }
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
                    icon.source: app.iconUrl("xqt-more")
                    icon.width: 20
                    icon.height: 20
                    icon.color: "#3c4043"
                    display: AbstractButton.IconOnly
                    opacity: entry.current || entry.selected || hovered ? 1 : 0.55
                    onClicked: pageMenu.openFor(entry.pageIndex, this, 0, height)
                }
            }
            Label {
                id: pageLabel
                anchors.top: frame.bottom
                anchors.topMargin: 3
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, list.width - 16)
                elide: Text.ElideRight
                text: entry.bookmark !== "" ? entry.pageNumber + " · " + entry.bookmark : entry.pageNumber
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
