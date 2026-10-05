// Actions on the selected elements (select tools) of a canvas: the notes (target: app), or the reference beside
// them (target: app.reference). A canvas for reading only: copy and deselect, nothing that changes it. The count of
// what is selected, and "Select more" (with the rectangle or lasso: taps add to the selection or take away).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    /// The DocumentCanvas the selection is on (a sibling of this pill) and what acts on it
    property Item canvasItem
    property var target: app
    property bool hidden: false
    /// "Save as sticker…" (qt/docs/stickers.md): the window opens its dialog
    signal stickerRequested()
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    readonly property bool readingOnly: canvasItem.readingOnly
    visible: target.hasSelection && !hidden
    /// Its distance from the canvas's bottom
    property real bottomMargin: 24
    /// A pill at the same bottom edge to keep clear of (the notes' view pill): where the two would meet, this one goes
    /// above it
    property Item avoid: null
    /// Where controls may go at the bottom (the parent's coordinates; Main.qml: above the navigation bar and the keyboard)
    property real bottomLimit: Infinity
    readonly property real lowY: Math.min(canvasItem.y + canvasItem.height, bottomLimit) - bottomMargin - height
    readonly property bool meets: avoid !== null && avoid.visible && x - 8 < avoid.x + avoid.width && x + width + 8 > avoid.x
                                  && lowY < avoid.y + avoid.height && lowY + height > avoid.y
    y: meets ? avoid.y - height - 12 : lowY
    anchors.horizontalCenter: canvasItem.horizontalCenter
    /// Smaller where the canvas is narrower than the pill (a phone, half of the window beside a reference)
    scale: Math.min(1, Math.max(0.5, (canvasItem.width - 16) / Math.max(1, width)))
    transformOrigin: Item.Bottom
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
    // (what changed while it was hidden is laid out before it shows: a tap goes to the button it lands on)
    onVisibleChanged: if (visible) buttons.ensurePolished()
    RowLayout {
        id: buttons
        spacing: 0
        // How many notes and elements are selected
        Label {
            objectName: pill.named("selectionCount")
            text: pill.target.selectedCount > 0 ? pill.target.selectedCount : ""  // (always there: the pill keeps its layout)
            font.weight: Font.DemiBold
            color: pill.target.selectingMore ? Material.accentColor : "#5f6368"
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 32  // (fixed: the pill does not change its width with the count)
            Layout.leftMargin: 4
            Layout.rightMargin: 2
            ToolTip.visible: countHover.hovered
            ToolTip.text: qsTr("%n selected", "", pill.target.selectedCount)
            ToolTip.delay: 600
            HoverHandler { id: countHover }
        }
        IconButton { objectName: pill.named("selectionCopy"); iconName: "xopp-edit-copy"; tip: qsTr("Copy (Ctrl+C)"); onClicked: pill.target.copySelection() }
        // The handwriting in it as text (qt/copy-tools; the notes only): the readings of its words to the clipboard
        IconButton {
            objectName: pill.named("selectionCopyText")
            visible: pill.target === app && app.selectionHasInk
            iconName: "xqt-copy-ink-text"
            label: qsTr("Copy as text")
            tip: qsTr("Copy as text (the handwriting's words, as the handwriting search read them)")
            onClicked: app.copySelectionAsText()
        }
        IconButton { objectName: pill.named("selectionCut"); visible: !pill.readingOnly; iconName: "xopp-edit-cut"; tip: qsTr("Cut (Ctrl+X)"); onClicked: pill.target.cutSelection() }
        IconButton { objectName: pill.named("selectionPaste"); visible: !pill.readingOnly; iconName: "xopp-edit-paste"; tip: qsTr("Paste (Ctrl+V)"); onClicked: pill.target.pasteElements() }
        IconButton {
            objectName: pill.named("selectionSticker")
            visible: pill.target === app  // (the notes; not the reference beside them)
            iconName: "xqt-sticker"
            tip: qsTr("Save as sticker… (to paste it again from the sticker button)")
            onClicked: pill.stickerRequested()
        }
        // Groups (qt/docs/groups.md): one button for each that can be done; a selection that is one group shows
        // only "Ungroup"
        IconButton {
            objectName: pill.named("selectionGroup")
            visible: !pill.readingOnly && pill.target.canGroup
            iconName: "xqt-group"
            label: qsTr("Group")
            tip: qsTr("Group (Ctrl+G): selected together from now on")
            onClicked: pill.target.groupSelection()
        }
        IconButton {
            objectName: pill.named("selectionUngroup")
            visible: !pill.readingOnly && pill.target.canUngroup
            iconName: "xqt-ungroup"
            label: qsTr("Ungroup")
            tip: qsTr("Ungroup (Ctrl+Shift+G)")
            onClicked: pill.target.ungroupSelection()
        }
        IconButton { objectName: pill.named("selectionDelete"); visible: !pill.readingOnly; iconName: "xqt-delete"; tip: qsTr("Delete (Del)"); onClicked: pill.target.deleteSelection() }
        ToolSeparator {}
        // Select more (qt/touch-multiselect): taps add notes and elements to the selection or take them away
        IconButton {
            objectName: pill.named("selectionMore")
            visible: pill.target.selectMoreOffered  // (by the tool: the pill keeps its layout when it appears)
            enabled: pill.target.selectMoreAvailable || pill.target.selectingMore
            iconName: "xqt-select-more"
            checked: pill.target.selectingMore
            tip: pill.target.selectingMore ? qsTr("Selecting more: a tap adds a note or an element, or takes it away. Tap to stop.")
                                           : qsTr("Select more: add notes and elements by tapping them")
            onClicked: pill.target.selectingMore = !pill.target.selectingMore
        }
        IconButton { objectName: pill.named("selectionClear"); iconName: "xqt-close"; tip: qsTr("Deselect (Esc)"); onClicked: pill.target.clearSelection() }
    }
}
