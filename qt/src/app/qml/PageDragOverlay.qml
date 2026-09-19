// Drag and drop of pages in the sidebar list or the page grid: a card with the number of pages follows the pointer,
// a bar shows where they go, and the view scrolls near its edges. Dropping moves the selected pages there
// (one step on the page undo stack).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Item {
    id: overlay
    required property var view
    /// Grid: drop left/right of a page; list: above/below.
    property bool horizontal: false
    property bool active: false
    /// Page index the pages are moved in front of (page count: at the end), -1: none.
    property int target: -1
    property int count: 0
    property point pos
    property Item source: null  // the delegate the drag started on (kept alive while scrolling)
    property int savedCacheBuffer: 0
    anchors.fill: view
    z: 10

    function start(p, sourceItem) {
        source = sourceItem
        savedCacheBuffer = view.cacheBuffer
        count = Math.max(1, app.pages.selectionCount)
        active = true
        moveTo(p)
    }
    function moveTo(p) {
        pos = p
        updateTarget()
    }
    function finish() {
        if (active && target >= 0) app.movePages(app.pages.selectedPages(), target)
        stop()
    }
    function stop() {
        active = false
        target = -1
        source = null
        view.cacheBuffer = savedCacheBuffer
    }

    function updateTarget() {
        const cx = view.contentX + pos.x, cy = view.contentY + pos.y
        let idx = view.indexAt(cx, cy)
        if (idx < 0) {
            // In a gap between pages: look a bit before.
            idx = horizontal ? view.indexAt(cx - 12, cy) : view.indexAt(cx, cy - 12)
        }
        if (idx < 0) {
            if (cy > view.contentHeight - 1 || (view.count > 0 && cy > itemBottom(view.count - 1))) {
                target = app.pages.count
                placeIndicator(view.itemAtIndex(view.count - 1), true)
            }
            return
        }
        const item = view.itemAtIndex(idx)
        if (!item) return
        const local = item.mapFromItem(overlay, pos.x, pos.y)
        const after = horizontal ? local.x > item.width / 2 : local.y > item.height / 2
        target = item.pageIndex + (after ? 1 : 0)
        placeIndicator(item, after)
    }
    function itemBottom(i) {
        const it = view.itemAtIndex(i)
        return it ? it.y + it.height : 0
    }
    function placeIndicator(item, after) {
        if (!item) return
        const p = item.mapToItem(overlay, 0, 0)
        if (horizontal) {
            indicator.width = 4
            indicator.height = item.height - 20
            indicator.x = (after ? p.x + item.width : p.x) - 2
            indicator.y = p.y + 4
        } else {
            indicator.width = item.width - 30
            indicator.height = 4
            indicator.x = p.x + 15
            indicator.y = (after ? p.y + item.height + 5 : p.y - 9)
        }
    }

    Rectangle {
        id: indicator
        visible: overlay.active && overlay.target >= 0
        radius: 2
        color: Material.accentColor
    }

    // The dragged pages
    Rectangle {
        visible: overlay.active
        x: overlay.pos.x - width / 2
        y: overlay.pos.y - height / 2
        width: 64
        height: 80
        radius: 6
        color: "#ffffff"
        border.width: 2
        border.color: Material.accentColor
        opacity: 0.9
        Rectangle {  // stack look for several pages
            visible: overlay.count > 1
            z: -1
            x: 5; y: 5
            width: parent.width; height: parent.height
            radius: 6
            color: "#e8eaf6"
            border.width: 1
            border.color: Material.accentColor
        }
        Label {
            anchors.centerIn: parent
            text: overlay.count
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Material.accentColor
        }
    }

    // Scroll near the edges while dragging.
    Timer {
        interval: 16
        repeat: true
        running: overlay.active
        onTriggered: {
            const edge = 48
            let d = 0
            if (overlay.pos.y < edge) d = -Math.min(24, (edge - overlay.pos.y) / 2)
            else if (overlay.pos.y > overlay.height - edge) d = Math.min(24, (overlay.pos.y - overlay.height + edge) / 2)
            if (d === 0) return
            const maxY = view.originY + view.contentHeight - view.height
            view.contentY = Math.max(view.originY, Math.min(maxY, view.contentY + d))
            // Keep the delegate that holds the pointer alive (views destroy delegates far outside the view).
            if (overlay.source)
                view.cacheBuffer = Math.max(overlay.savedCacheBuffer,
                                            Math.abs(overlay.source.y - view.contentY) + view.height * 2)
            overlay.updateTarget()
        }
    }
}
