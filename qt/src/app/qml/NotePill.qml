// The selected sticky note (qt/docs/sticky-notes.md), beside it: its color, cover mode (self-testing), copy, cut,
// delete. Every change is one undo step; paste (Ctrl+V, the context pill) puts a copied note on the page in view. The note itself is moved by dragging it, resized by the handle at its bottom right corner.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    property Item canvasItem
    property bool hidden: false
    visible: app.noteSelected && !hidden
    /// Above the note (below it where there is no room), going along with it; at the top of the canvas when the
    /// note is out of sight
    function refresh() {
        const c = canvasItem
        const box = app.noteBox()
        if (box.width === 0 && box.height === 0) return
        x = Math.max(c.x + 8, Math.min(c.x + box.x, c.x + c.width - width - 8))
        const above = c.y + box.y - height - 12
        const below = c.y + box.y + box.height + 16
        y = above >= c.y + 8 ? above : Math.min(below, c.y + c.height - height - 8)
        if (box.y + box.height < 0 || box.y > c.height) y = c.y + 12
    }
    onVisibleChanged: if (visible) refresh()
    onWidthChanged: refresh()
    Connections {
        target: app
        function onNoteSelectionChanged() { pill.refresh() }
    }
    Connections {
        target: pill.canvasItem
        function onViewportChanged() { if (pill.visible) pill.refresh() }
    }
    padding: 2
    leftPadding: 8
    rightPadding: 8
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    RowLayout {
        spacing: 2
        Repeater {
            model: app.stickyNoteColors
            delegate: AbstractButton {
                id: swatch
                required property color modelData
                required property int index
                objectName: "noteColor" + index
                implicitWidth: 36
                implicitHeight: 40
                onClicked: app.noteColor = modelData
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Color of the note")
                ToolTip.delay: 600
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        radius: 4
                        color: swatch.modelData
                        border.width: Qt.colorEqual(app.noteColor, swatch.modelData) ? 2 : 1
                        border.color: Qt.colorEqual(app.noteColor, swatch.modelData) ? "#303030" : "#40000000"
                    }
                }
            }
        }
        ToolSeparator {}
        ToolButton {
            objectName: "noteCoverButton"
            text: qsTr("Cover")
            checkable: true
            checked: app.noteCovers
            onClicked: app.noteCovers = checked
            ToolTip.visible: hovered
            ToolTip.text: app.noteCovers ? qsTr("Covers: the pen leaves it alone, a tap lets you peek under it. Tap to write on it again.")
                                         : qsTr("Cover what is below (self-testing): the pen leaves it alone, a tap lets you peek under it")
            ToolTip.delay: 600
        }
        ToolSeparator {}
        IconButton { objectName: "noteCopy"; iconName: "xopp-edit-copy"; tip: qsTr("Copy the note (Ctrl+C), to paste it on another page"); onClicked: app.copyStickyNote() }
        IconButton { objectName: "noteCut"; iconName: "xopp-edit-cut"; tip: qsTr("Cut the note (Ctrl+X): paste it on another page to move it there"); onClicked: app.cutStickyNote() }
        IconButton { objectName: "noteDelete"; iconName: "xqt-delete"; tip: qsTr("Delete the note (Del)"); onClicked: app.deleteStickyNote() }
        IconButton { objectName: "noteDeselect"; iconName: "xqt-close"; tip: qsTr("Deselect (Esc)"); onClicked: app.clearSelection() }
    }
}
