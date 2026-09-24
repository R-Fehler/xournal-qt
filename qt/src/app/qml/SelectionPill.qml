// Actions on the selected elements (select tools) of a canvas: the notes (target: app), or the reference beside
// them (target: app.reference). A canvas for reading only: copy and deselect, nothing that changes it.
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
    RowLayout {
        spacing: 0
        IconButton { objectName: pill.named("selectionCopy"); iconName: "xopp-edit-copy"; tip: qsTr("Copy (Ctrl+C)"); onClicked: pill.target.copySelection() }
        IconButton { objectName: pill.named("selectionCut"); visible: !pill.readingOnly; iconName: "xopp-edit-cut"; tip: qsTr("Cut (Ctrl+X)"); onClicked: pill.target.cutSelection() }
        IconButton { objectName: pill.named("selectionPaste"); visible: !pill.readingOnly; iconName: "xopp-edit-paste"; tip: qsTr("Paste (Ctrl+V)"); onClicked: pill.target.pasteElements() }
        IconButton { objectName: pill.named("selectionDelete"); visible: !pill.readingOnly; iconName: "xqt-delete"; tip: qsTr("Delete (Del)"); onClicked: pill.target.deleteSelection() }
        ToolSeparator {}
        IconButton { objectName: pill.named("selectionClear"); iconName: "xqt-close"; tip: qsTr("Deselect (Esc)"); onClicked: pill.target.clearSelection() }
    }
}
