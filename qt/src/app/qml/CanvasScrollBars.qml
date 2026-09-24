// Scroll bars over a canvas (the notes, or the reference beside them): wide enough to be dragged with a finger or
// the pen. Laid over the canvas item they belong to (a sibling of it); only the bars take presses.
import QtQuick
import QtQuick.Controls

Item {
    id: bars
    /// The DocumentCanvas they scroll
    property Item canvasItem
    /// Room kept free at the right (the strip that brings a right tool bar back)
    property real rightInset: 0
    property bool hidden: false
    /// "" for the notes; the reference's bars are named "reference…"
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    property bool inputTransparent: true
    anchors.fill: canvasItem

    ScrollBar {
        id: vbar
        objectName: bars.named("verticalScrollBar")
        orientation: Qt.Vertical
        anchors.top: parent.top
        anchors.right: parent.right
        // (beside the strip that brings a right tool bar back, not under it)
        anchors.rightMargin: bars.rightInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: hbar.visible ? hbar.height : 0
        visible: bars.canvasItem.contentHeight > bars.canvasItem.height + 1 && !bars.hidden
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: bars.canvasItem.contentHeight > 0 ? Math.min(1, bars.canvasItem.height / bars.canvasItem.contentHeight) : 1
        position: bars.canvasItem.contentHeight > 0 ? bars.canvasItem.contentY / bars.canvasItem.contentHeight : 0
        onPositionChanged: if (pressed) bars.canvasItem.scrollTo(bars.canvasItem.contentX,
                                                                 position * bars.canvasItem.contentHeight)
        contentItem: Rectangle {
            implicitWidth: vbar.pressed || vbar.hovered ? 10 : 7
            implicitHeight: 48
            radius: width / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: vbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: vbar.pressed || vbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: vbar.pressed || vbar.hovered ? "#30ffffff" : "transparent" }
    }
    ScrollBar {
        id: hbar
        objectName: bars.named("horizontalScrollBar")
        orientation: Qt.Horizontal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: vbar.visible ? vbar.width : 0
        visible: bars.canvasItem.contentWidth > bars.canvasItem.width + 1 && !bars.hidden
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: bars.canvasItem.contentWidth > 0 ? Math.min(1, bars.canvasItem.width / bars.canvasItem.contentWidth) : 1
        position: bars.canvasItem.contentWidth > 0 ? bars.canvasItem.contentX / bars.canvasItem.contentWidth : 0
        onPositionChanged: if (pressed) bars.canvasItem.scrollTo(position * bars.canvasItem.contentWidth,
                                                                 bars.canvasItem.contentY)
        contentItem: Rectangle {
            implicitWidth: 48
            implicitHeight: hbar.pressed || hbar.hovered ? 10 : 7
            radius: height / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: hbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: hbar.pressed || hbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: hbar.pressed || hbar.hovered ? "#30ffffff" : "transparent" }
    }
}
