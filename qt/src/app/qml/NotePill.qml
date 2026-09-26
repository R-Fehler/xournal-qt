// The selected sticky note (qt/docs/sticky-notes.md), beside it: its color, cover mode (self-testing), its Markdown
// text, an image onto it, copy, cut, delete, and "Select more" (qt/touch-multiselect). Every change is one undo step;
// paste (Ctrl+V, the context pill) puts a copied note on the page in view. The note itself is moved by dragging it,
// resized by the handle at its bottom right corner. Of the notes (target: app) or of the reference beside them
// (target: app.reference; for reading only: copy and deselect).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    property Item canvasItem
    property var target: app
    property bool hidden: false
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    readonly property bool readingOnly: canvasItem.readingOnly
    /// "Image…": the window's file dialog; the image goes onto the selected note (target.insertImage)
    signal imageRequested()
    visible: target.noteSelected && !hidden
    /// Above the note (below it where there is no room), going along with it; at the top of the canvas when the
    /// note is out of sight
    function refresh() {
        const c = canvasItem
        const box = pill.target.noteBox()
        if (box.width === 0 && box.height === 0) return
        const w = width * scale
        const h = height * scale
        x = Math.max(c.x + 8, Math.min(c.x + box.x, c.x + c.width - w - 8))
        const above = c.y + box.y - h - 12
        const below = c.y + box.y + box.height + 16
        y = above >= c.y + 8 ? above : Math.min(below, c.y + c.height - h - 8)
        if (box.y + box.height < 0 || box.y > c.height) y = c.y + 12
    }
    /// Smaller where the canvas is narrower than the pill (half of the window beside a reference, a phone)
    scale: Math.min(1, Math.max(0.5, (canvasItem.width - 16) / Math.max(1, width)))
    transformOrigin: Item.TopLeft
    onVisibleChanged: if (visible) {
        buttons.ensurePolished()  // (what changed while it was hidden is laid out before it shows)
        refresh()
    }
    onWidthChanged: refresh()
    onScaleChanged: refresh()
    Connections {
        target: pill.target
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
        id: buttons
        spacing: 2
        // (selecting more: how many are selected, one)
        Label {
            objectName: pill.named("noteCount")
            visible: pill.target.selectingMore
            text: pill.target.selectedCount
            font.weight: Font.DemiBold
            color: Material.accentColor
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 32  // (fixed: the pill does not change its width with the count)
        }
        Repeater {
            model: pill.readingOnly ? [] : app.stickyNoteColors
            delegate: AbstractButton {
                id: swatch
                required property color modelData
                required property int index
                objectName: pill.named("noteColor" + index)
                implicitWidth: 36
                implicitHeight: 40
                onClicked: pill.target.noteColor = modelData
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
                        border.width: Qt.colorEqual(pill.target.noteColor, swatch.modelData) ? 2 : 1
                        border.color: Qt.colorEqual(pill.target.noteColor, swatch.modelData) ? "#303030" : "#40000000"
                    }
                }
            }
        }
        ToolSeparator { visible: !pill.readingOnly }
        ToolButton {
            objectName: pill.named("noteCoverButton")
            visible: !pill.readingOnly
            text: qsTr("Cover")
            checkable: true
            checked: pill.target.noteCovers
            onClicked: pill.target.noteCovers = checked
            ToolTip.visible: hovered
            ToolTip.text: pill.target.noteCovers ? qsTr("Covers: the pen leaves it alone, a tap lets you peek under it. Tap to write on it again.")
                                         : qsTr("Cover what is below (self-testing): the pen leaves it alone, a tap lets you peek under it")
            ToolTip.delay: 600
        }
        ToolSeparator { visible: !pill.readingOnly }
        ToolButton {
            objectName: pill.named("noteTextButton")
            visible: !pill.readingOnly
            text: qsTr("Text")
            enabled: !pill.target.noteCovers
            onClicked: pill.target.writeNoteText()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Write on the note: its Markdown text, as wide as the note")
            ToolTip.delay: 600
        }
        ToolButton {
            objectName: pill.named("noteImageButton")
            visible: !pill.readingOnly
            text: qsTr("Image…")
            enabled: !pill.target.noteCovers
            onClicked: pill.imageRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Put an image on the note")
            ToolTip.delay: 600
        }
        ToolSeparator { visible: !pill.readingOnly }
        IconButton { objectName: pill.named("noteCopy"); iconName: "xopp-edit-copy"; tip: qsTr("Copy the note (Ctrl+C), to paste it on another page"); onClicked: pill.target.copyStickyNote() }
        IconButton { objectName: pill.named("noteCut"); visible: !pill.readingOnly; iconName: "xopp-edit-cut"; tip: qsTr("Cut the note (Ctrl+X): paste it on another page to move it there"); onClicked: pill.target.cutStickyNote() }
        IconButton { objectName: pill.named("noteDelete"); visible: !pill.readingOnly; iconName: "xqt-delete"; tip: qsTr("Delete the note (Del)"); onClicked: pill.target.deleteStickyNote() }
        // Select more (qt/touch-multiselect): taps add notes and elements to the selection or take them away
        IconButton {
            objectName: pill.named("noteSelectMore")
            visible: pill.target.selectMoreOffered  // (by the tool: the pill keeps its layout when it appears)
            enabled: pill.target.selectMoreAvailable || pill.target.selectingMore
            iconName: "xqt-select-more"
            checked: pill.target.selectingMore
            tip: pill.target.selectingMore ? qsTr("Selecting more: a tap adds a note or an element, or takes it away. Tap to stop.")
                                           : qsTr("Select more: add notes and elements by tapping them")
            onClicked: pill.target.selectingMore = !pill.target.selectingMore
        }
        IconButton { objectName: pill.named("noteDeselect"); iconName: "xqt-close"; tip: qsTr("Deselect (Esc)"); onClicked: pill.target.clearSelection() }
    }
}
