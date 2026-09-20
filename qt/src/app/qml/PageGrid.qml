// Grid of all pages of the current document over the canvas: fling through the whole document, tap a page to go
// there. Zoom (pinch, Ctrl+wheel, −/+) changes the number of columns: bigger previews, fewer per row.
// Search hits are marked on the previews. Pages can be selected (Ctrl/Shift+click, or "Select" for touch), copied,
// pasted, deleted and dragged to another place (press and hold), with their own undo (see PageKeys).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: pageGrid
    color: "#4e5156"
    visible: false
    focus: visible

    /// Preview width the user zoomed to; the columns follow from it.
    property real cellTarget: 200
    /// Touch: taps select pages instead of opening them.
    property bool selectionMode: false
    readonly property int columns: Math.max(1, Math.min(12, Math.round(grid.width / cellTarget)))
    readonly property int spacing: 6
    readonly property int labelHeight: 18

    function open() {
        visible = true
        const row = app.filteredPages.rowOf(app.pages.currentPage)
        if (row >= 0) {
            grid.currentIndex = row
            grid.positionViewAtIndex(row, GridView.Center)
        }
        grid.forceActiveFocus()
    }
    function close() {
        visible = false
        selectionMode = false
    }
    function choose(index) {
        app.jumpToPage(index)
        close()
    }
    function setColumns(n) {
        n = Math.max(1, Math.min(12, n))
        const keep = grid.indexAt(grid.contentX + grid.width / 2, grid.contentY + grid.height / 2)
        cellTarget = grid.width / n
        if (keep >= 0) grid.positionViewAtIndex(keep, GridView.Center)
    }

    // Stepping through search hits (Enter in the search bar) moves the grid along.
    Connections {
        target: app
        enabled: pageGrid.visible
        function onSearchChanged() {
            const row = app.filteredPages.rowOf(app.pageNumber - 1)
            if (app.searchCurrent > 0 && row >= 0) grid.positionViewAtIndex(row, GridView.Contain)
        }
    }

    PageKeys { id: pageKeys }
    PageMenu { id: pageMenu }

    GridView {
        id: grid
        objectName: "pageGridView"
        anchors.fill: parent
        anchors.leftMargin: pageGrid.spacing / 2
        anchors.rightMargin: pageGrid.spacing / 2
        clip: true
        model: app.filteredPages
        keyNavigationEnabled: true
        cacheBuffer: height
        maximumFlickVelocity: 9000
        cellWidth: Math.floor(width / pageGrid.columns)
        // Room for a page of the document's usual format (A4, slides, ...) and its number; others are fitted in.
        cellHeight: Math.round((cellWidth - pageGrid.spacing) * app.pages.typicalAspect) + pageGrid.labelHeight
                    + pageGrid.spacing
        header: Item { height: pageGrid.spacing }
        // Add pages at the end, and room so the last row is not under the zoom controls
        footer: Column {
            width: grid.width
            AppendPages { width: parent.width }
            Item { width: 1; height: 80 }
        }
        ScrollBar.vertical: ScrollBar { minimumSize: 0.05 }

        Keys.onReturnPressed: if (currentItem) pageGrid.choose(currentItem.pageIndex)
        Keys.onEnterPressed: if (currentItem) pageGrid.choose(currentItem.pageIndex)
        Keys.onSpacePressed: if (currentItem) pageGrid.choose(currentItem.pageIndex)
        Keys.onShortcutOverride: function(event) { event.accepted = pageKeys.isPageKey(event) }
        Keys.onPressed: function(event) {
            if (pageKeys.handle(event)) {
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                if (app.pages.selectionCount > 0) app.pages.clearSelection()
                else pageGrid.close()
                event.accepted = true
            } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                pageGrid.setColumns(pageGrid.columns - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Minus) {
                pageGrid.setColumns(pageGrid.columns + 1)
                event.accepted = true
            }
        }

        delegate: Item {
            id: cell
            required property int index
            required property int pageIndex
            required property int pageNumber
            required property real aspect
            required property string thumbnail
            required property bool current
            required property var searchHits
            required property int currentSearchHit
            required property int searchHitCount
            required property bool selected
            width: grid.cellWidth
            height: grid.cellHeight

            readonly property real availW: width - pageGrid.spacing
            readonly property real availH: height - pageGrid.labelHeight - pageGrid.spacing
            // Fit the page (any format) into the cell.
            readonly property real frameW: Math.min(availW, availH / Math.max(0.1, aspect))
            readonly property real dpr: Screen.devicePixelRatio

            Rectangle {
                id: frame
                x: (cell.width - width) / 2
                y: pageGrid.spacing / 2 + (cell.availH - height)
                width: Math.round(cell.frameW)
                height: Math.round(cell.frameW * cell.aspect)
                color: "white"
                // The current page, and pages with search hits
                border.width: cell.current || cell.selected || cell.searchHitCount > 0 ? 3 : 0
                border.color: cell.current || cell.selected ? Material.accentColor : "#f9a825"

                // A small preview right away, a sharp one for big cells (loads on top of it).
                Image {
                    anchors.fill: parent
                    anchors.margins: frame.border.width
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    source: cell.thumbnail
                    sourceSize.width: 160
                    smooth: true
                }
                Image {
                    anchors.fill: parent
                    anchors.margins: frame.border.width
                    visible: cell.frameW * cell.dpr > 180 && status === Image.Ready
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    // In steps, so zooming does not render every size.
                    source: cell.frameW * cell.dpr > 180 ? cell.thumbnail : ""
                    sourceSize.width: Math.ceil(cell.frameW * cell.dpr / 128) * 128
                    smooth: true
                }
                // Search hits
                Repeater {
                    model: cell.searchHits
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        x: modelData.x * frame.width - 2
                        y: modelData.y * frame.height - 2
                        width: Math.max(4, modelData.width * frame.width + 4)
                        height: Math.max(4, modelData.height * frame.height + 4)
                        radius: 2
                        color: index === cell.currentSearchHit ? "#aaff7800" : "#80ffd200"
                        border.width: 1
                        border.color: index === cell.currentSearchHit ? "#ff7800" : "#e0a800"
                    }
                }
                HitBadge {
                    count: cell.searchHitCount
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                }
                SelectionMark { visible: cell.selected }
                // Keyboard position
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    visible: cell.GridView.isCurrentItem && grid.activeFocus && !cell.current
                    color: "transparent"
                    border.width: 2
                    border.color: "#c5cae9"
                }
            }
            Label {
                anchors.top: frame.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                height: pageGrid.labelHeight
                verticalAlignment: Text.AlignVCenter
                text: cell.pageNumber
                font.pixelSize: 12
                color: cell.current ? "#ffffff" : "#d0d3d8"
                font.weight: cell.current ? Font.DemiBold : Font.Normal
            }
            PageArea {
                dragOverlay: pageDrag
                pageIndex: cell.pageIndex
                delegateItem: cell
                onTapped: function(modifiers) {
                    grid.forceActiveFocus()
                    grid.currentIndex = cell.index
                    if (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) app.pages.select(cell.pageIndex, modifiers)
                    else if (pageGrid.selectionMode) app.pages.toggleSelected(cell.pageIndex)
                    else pageGrid.choose(cell.pageIndex)
                }
                onHeld: grid.forceActiveFocus()
                onMenuRequested: function(x, y) { pageMenu.openFor(cell.pageIndex, cell, x, y) }
            }
        }

        // Zoom: fewer, bigger previews when spreading the fingers.
        PinchHandler {
            id: pinch
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType
            property real startTarget: 200
            onActiveChanged: if (active) startTarget = pageGrid.cellTarget
            onActiveScaleChanged: {
                const cols = Math.max(1, Math.min(12, Math.round(grid.width / (startTarget * activeScale))))
                if (cols !== pageGrid.columns) pageGrid.setColumns(cols)
            }
        }
        TouchpadMomentum { flickable: grid }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                pageGrid.setColumns(pageGrid.columns + (event.angleDelta.y > 0 ? -1 : 1))
            }
        }
    }

    PageDragOverlay {
        id: pageDrag
        view: grid
        horizontal: true
    }

    // Actions on the selected pages (touch friendly)
    Pane {
        id: actionBar
        objectName: "pageActionBar"
        visible: pageGrid.selectionMode || app.pages.selectionCount > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 88
        padding: 2
        leftPadding: 12
        rightPadding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            Label {
                text: app.pages.selectionCount === 0 ? qsTr("Tap pages to select them")
                    : app.pages.selectionCount === 1 ? qsTr("1 page selected")
                    : qsTr("%1 pages selected").arg(app.pages.selectionCount)
                color: "#505050"
                Layout.rightMargin: 8
            }
            ToolSeparator {}
            ToolButton { text: qsTr("Copy"); enabled: app.pages.selectionCount > 0; onClicked: app.copyPages(app.pages.selectedPages()) }
            ToolButton { text: qsTr("Cut"); enabled: app.pages.selectionCount > 0; onClicked: app.cutPages(app.pages.selectedPages()) }
            ToolButton { text: qsTr("Paste"); enabled: app.copiedPages > 0; onClicked: app.pastePages(-1) }
            ToolButton { text: qsTr("Duplicate"); enabled: app.pages.selectionCount > 0; onClicked: app.duplicatePages(app.pages.selectedPages()) }
            ToolButton {
                text: qsTr("Delete")
                enabled: app.pages.selectionCount > 0 && app.pages.selectionCount < app.pages.count
                onClicked: app.deletePages(app.pages.selectedPages())
            }
            ToolSeparator {}
            IconButton {
                iconName: "xopp-edit-undo"; tip: qsTr("Undo page change (Ctrl+Z)")
                implicitWidth: 44; implicitHeight: 44
                enabled: app.canUndoPages
                onClicked: app.undoPages()
            }
            IconButton {
                iconName: "xqt-close"; tip: qsTr("Clear the selection")
                implicitWidth: 44; implicitHeight: 44
                onClicked: { app.pages.clearSelection(); pageGrid.selectionMode = false }
            }
        }
    }

    // Zoom and close
    Pane {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 24
        padding: 2
        leftPadding: 4
        rightPadding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            SearchFilterChip {
                visible: app.searchQuery !== ""
                Layout.rightMargin: 6
            }
            ToolButton {
                objectName: "selectModeButton"
                text: qsTr("Select")
                checkable: true
                checked: pageGrid.selectionMode
                onToggled: pageGrid.selectionMode = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Tap pages to select them (Ctrl/Shift+click also works)")
            }
            ToolSeparator {}
            ToolButton {
                text: "−"; font.pixelSize: 22; implicitWidth: 44
                enabled: pageGrid.columns < 12
                onClicked: pageGrid.setColumns(pageGrid.columns + 1)
                ToolTip.visible: hovered; ToolTip.text: qsTr("Smaller previews")
            }
            Label {
                objectName: "pageGridColumns"
                text: pageGrid.columns === 1 ? qsTr("1 column") : qsTr("%1 columns").arg(pageGrid.columns)
                color: "#505050"
                Layout.minimumWidth: 80
                horizontalAlignment: Text.AlignHCenter
            }
            ToolButton {
                text: "+"; font.pixelSize: 22; implicitWidth: 44
                enabled: pageGrid.columns > 1
                onClicked: pageGrid.setColumns(pageGrid.columns - 1)
                ToolTip.visible: hovered; ToolTip.text: qsTr("Bigger previews")
            }
            ToolSeparator {}
            IconButton {
                iconName: "xqt-close"; tip: qsTr("Back to the page (Esc)")
                implicitWidth: 44; implicitHeight: 44
                onClicked: pageGrid.close()
            }
        }
    }
}
