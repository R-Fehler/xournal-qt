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
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    readonly property bool readingOnly: canvasItem.readingOnly
    visible: target.hasSelection && !hidden
    anchors.bottom: canvasItem.bottom
    anchors.bottomMargin: 24
    anchors.horizontalCenter: canvasItem.horizontalCenter
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
        IconButton { objectName: pill.named("selectionCut"); visible: !pill.readingOnly; iconName: "xopp-edit-cut"; tip: qsTr("Cut (Ctrl+X)"); onClicked: pill.target.cutSelection() }
        IconButton { objectName: pill.named("selectionPaste"); visible: !pill.readingOnly; iconName: "xopp-edit-paste"; tip: qsTr("Paste (Ctrl+V)"); onClicked: pill.target.pasteElements() }
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
