// xournal-qt: part of the main window (Main.qml): the frame of a plugin's preview on the page while its live dialog is
// open (qt/docs/features/plugins.md, "Dialogs"). The canvas draws the preview itself (as the elements will look); this
// is the dashed frame around it: dragged it moves the preview, its corner sizes it (unless the plugin fixed the size,
// as the plotter's exact scale does). Over the frame the page takes no ink meanwhile.
import QtQuick
import QtQuick.Controls.Material

Item {
    id: frameItem
    objectName: "pluginFrame"
    property Item canvasItem
    readonly property var frame: app.plugins.liveFrame
    readonly property bool shown: app.plugins.liveOpen && frame.page !== undefined
    visible: shown && shownRect.width > 0
    /// Where the frame is in the canvas (follows scrolling and zooming); while dragged: where the finger has it
    property rect dragged: Qt.rect(0, 0, 0, 0)
    property bool dragging: false
    readonly property rect placed: {
        if (!shown) return Qt.rect(0, 0, 0, 0)
        // (the canvas's viewport: a binding on it moves the frame with the page)
        void (canvasItem.contentX, canvasItem.contentY, canvasItem.contentWidth, canvasItem.contentHeight)
        return canvasItem.pageRectToItem(frame.page, Qt.rect(frame.x, frame.y, frame.width, frame.height))
    }
    readonly property rect shownRect: dragging ? dragged : placed
    x: canvasItem.x + shownRect.x
    y: canvasItem.y + shownRect.y
    width: shownRect.width
    height: shownRect.height
    /// Points per item pixel (the canvas's zoom)
    readonly property real pointsPerPixel: placed.width > 0 ? frame.width / placed.width : 1

    function commit() {
        const r = dragged
        const p = canvasItem.itemToPage(frame.page, Qt.point(r.x, r.y))
        app.plugins.moveLiveFrame(p.x, p.y, r.width * pointsPerPixel, r.height * pointsPerPixel)
    }
    Timer {
        id: follow
        interval: 50
        onTriggered: frameItem.commit()
    }

    Rectangle {  // the frame: light under dark, seen on any paper
        anchors.fill: parent
        anchors.margins: -1
        color: "transparent"
        border.width: 3
        border.color: "#66ffffff"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: "#081a73e8"
            border.width: 1
            border.color: "#cc1a73e8"
        }
    }
    MouseArea {
        objectName: "pluginFrameMove"
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point start
        property rect from
        onPressed: function(mouse) {
            start = mapToItem(frameItem.canvasItem, mouse.x, mouse.y)
            from = frameItem.placed
            frameItem.dragged = from
            frameItem.dragging = true
        }
        onPositionChanged: function(mouse) {
            const p = mapToItem(frameItem.canvasItem, mouse.x, mouse.y)
            frameItem.dragged = Qt.rect(from.x + p.x - start.x, from.y + p.y - start.y, from.width, from.height)
            follow.start()
        }
        onReleased: {
            follow.stop()
            frameItem.commit()
            frameItem.dragging = false
        }
        onCanceled: frameItem.dragging = false
    }
    // The corner that sizes it
    Rectangle {
        objectName: "pluginFrameSize"
        visible: frameItem.frame.resizable !== false
        width: 22
        height: 22
        radius: 11
        x: parent.width - width / 2
        y: parent.height - height / 2
        color: "#1a73e8"
        border.width: 2
        border.color: "#ffffff"
        MouseArea {
            anchors.fill: parent
            anchors.margins: -10
            cursorShape: Qt.SizeFDiagCursor
            property point start
            property rect from
            onPressed: function(mouse) {
                start = mapToItem(frameItem.canvasItem, mouse.x, mouse.y)
                from = frameItem.placed
                frameItem.dragged = from
                frameItem.dragging = true
            }
            onPositionChanged: function(mouse) {
                const p = mapToItem(frameItem.canvasItem, mouse.x, mouse.y)
                frameItem.dragged = Qt.rect(from.x, from.y, Math.max(24, from.width + p.x - start.x),
                                            Math.max(24, from.height + p.y - start.y))
                follow.start()
            }
            onReleased: {
                follow.stop()
                frameItem.commit()
                frameItem.dragging = false
            }
            onCanceled: frameItem.dragging = false
        }
    }
}
