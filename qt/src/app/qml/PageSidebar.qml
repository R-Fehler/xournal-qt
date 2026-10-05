// Page sidebar: thumbnails of the current document. Tap a page to go there; Ctrl/Shift+click to select pages; a
// finger held on a page starts the selection mode with that page selected (taps then select, until the selection is
// empty or the bar's ✕); press and hold, then move, to drag the selected pages to another place; right click or ⋮
// for the page menu; Ctrl+C/X/V,
// Delete and Ctrl+Z (the one undo of the document) with the keyboard. While searching, pages with hits are framed and the list can
// be limited to them. The last buttons show the document's annotations (AnnotationList) and its version history
// (HistoryPanel: the versions a PDF with notes keeps; while it keeps none, what that is and the switch).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import "DevicePixels.js" as DevicePixels

Rectangle {
    id: sidebar
    color: "#eceef1"
    /// "pages", "layers", "contents", "annotations" or "history"
    property string mode: "pages"
    readonly property bool showContents: mode === "contents"
    /// A table of contents, or bookmarks (listed at the top of it)
    readonly property bool hasContents: app.outline.available || app.bookmarks.length > 0
    /// A page, an entry of the contents or an annotation was tapped and is shown now (a drawer closes then)
    signal pagePicked()
    /// The History panel asks for a version's message (-1: "Save with a message…")
    signal versionMessageRequested(int id)
    /// Room at the bottom for the system's navigation bar (its lists end above it; the sidebar's color goes on below)
    property real bottomInset: 0
    /// Touch: taps select pages instead of going there (a finger held on a page turns it on; it ends when the selection
    /// is empty)
    property bool selectionMode: false
    Connections {
        target: app.pages
        function onSelectionChanged() { if (app.pages.selectionCount === 0) sidebar.selectionMode = false }
    }
    // The annotations are read only while they are shown
    Binding { target: app.annotations; property: "active"; value: sidebar.visible && sidebar.mode === "annotations" }
    // The versions too (reading them reads the whole file)
    Binding { target: app.versions; property: "active"; value: sidebar.visible && sidebar.mode === "history" }
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
                implicitHeight: win.adaptive.touchProfile ? win.adaptive.minTarget : 32  // (a finger: bigger)
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
            implicitWidth: win.adaptive.touchProfile ? win.adaptive.minTarget : 36
            implicitHeight: win.adaptive.touchProfile ? win.adaptive.minTarget : 32
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
        // History: the versions a PDF with notes keeps (an icon too)
        AbstractButton {
            id: historyButton
            objectName: "sidebarHistoryButton"
            implicitWidth: win.adaptive.touchProfile ? win.adaptive.minTarget : 36
            implicitHeight: win.adaptive.touchProfile ? win.adaptive.minTarget : 32
            readonly property bool active: sidebar.mode === "history"
            onClicked: sidebar.mode = "history"
            Accessible.name: qsTr("Version history")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Version history")
            background: Rectangle { radius: 16; color: historyButton.active ? "#ffffff" : "transparent" }
            contentItem: Item {
                Image {
                    anchors.centerIn: parent
                    source: app.iconUrl("xqt-history")
                    sourceSize.width: 18
                    sourceSize.height: 18
                    opacity: historyButton.active ? 1 : 0.75
                }
            }
        }
    }
    HistoryPanel {
        objectName: "historyPanel"
        onPicked: sidebar.pagePicked()
        onMessageRequested: function(id) { sidebar.versionMessageRequested(id) }
        visible: sidebar.mode === "history"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sidebar.bottomInset
        anchors.left: parent.left
        anchors.right: parent.right
    }
    AnnotationList {
        onPicked: sidebar.pagePicked()
        visible: sidebar.mode === "annotations"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sidebar.bottomInset
        anchors.left: parent.left
        anchors.right: parent.right
    }
    LayerList {
        visible: sidebar.mode === "layers"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sidebar.bottomInset
        anchors.left: parent.left
        anchors.right: parent.right
    }
    OutlineList {
        onPicked: sidebar.pagePicked()
        visible: sidebar.showContents && sidebar.hasContents
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sidebar.bottomInset
        anchors.left: parent.left
        anchors.right: parent.right
    }

    // The selection mode: how many pages are selected, their menu, and the way out
    Pane {
        id: selectionBar
        objectName: "sidebarSelectionBar"
        visible: sidebar.selectionMode && sidebar.mode === "pages"
        anchors.top: switchRow.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        padding: 0
        leftPadding: 10
        background: Rectangle { radius: height / 2; color: "#ffffff" }
        RowLayout {
            width: parent.width
            spacing: 0
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: app.pages.selectionCount === 1 ? qsTr("1 page selected")
                                                     : qsTr("%1 pages selected").arg(app.pages.selectionCount)
                color: "#3c4043"
            }
            IconButton {
                objectName: "sidebarSelectionMenu"
                iconName: "xqt-more"
                tip: qsTr("What to do with the selected pages")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 20; icon.height: 20
                enabled: app.pages.selectionCount > 0
                onClicked: pageMenu.openFor(app.pages.selectedPages()[0], this, 0, height)
            }
            IconButton {
                objectName: "sidebarSelectionDone"
                iconName: "xqt-close"
                tip: qsTr("Clear the selection")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 20; icon.height: 20
                onClicked: { app.pages.clearSelection(); sidebar.selectionMode = false }
            }
        }
    }

    SearchFilterChip {
        id: filterChip
        anchors.top: selectionBar.visible ? selectionBar.bottom : (switchRow.visible ? switchRow.bottom : parent.top)
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
        anchors.top: filterChip.visible ? filterChip.bottom
                   : selectionBar.visible ? selectionBar.bottom : (switchRow.visible ? switchRow.bottom : parent.top)
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 10
        anchors.bottomMargin: 10 + sidebar.bottomInset
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
            else if (event.key === Qt.Key_Escape) { app.pages.clearSelection(); sidebar.selectionMode = false; event.accepted = true }
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
            /// It differs from the version it is compared with (app.compare)
            required property bool differs
            width: list.width
            height: frame.height + pageLabel.height + 4

            Rectangle {
                id: frame
                anchors.horizontalCenter: parent.horizontalCenter
                width: list.width - 36
                height: width * entry.aspect
                color: "white"
                border.width: DevicePixels.whole(entry.current || entry.selected || entry.searchHitCount > 0 ? 3 : 1, Screen.devicePixelRatio)
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
                    sourceWidth: Math.round(frame.width)
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
                // Changed in a comparison (app.compare): a bar along its left edge
                Rectangle {
                    objectName: "sidebarDiffers"
                    visible: entry.differs
                    anchors.right: parent.left
                    anchors.rightMargin: 3
                    width: 5
                    height: parent.height
                    radius: 2
                    color: "#9334e6"
                }
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
                        } else if (sidebar.selectionMode) {
                            app.pages.toggleSelected(entry.pageIndex)
                        } else {
                            app.pages.clearSelection()
                            app.pages.setAnchor(entry.pageIndex)
                            app.jumpToPage(entry.pageIndex)
                            sidebar.pagePicked()
                        }
                    }
                    onHeld: list.forceActiveFocus()
                    // A finger held on a page: the selection mode with the page selected (moving on drags it)
                    onHeldToSelect: {
                        sidebar.selectionMode = true
                        if (!app.pages.isSelected(entry.pageIndex)) app.pages.toggleSelected(entry.pageIndex)
                    }
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
