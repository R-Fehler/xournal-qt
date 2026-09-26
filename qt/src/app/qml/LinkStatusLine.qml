// Where the link under the mouse or the hovering pen leads, in a small line at the bottom left of the canvas, as
// browsers show it (qt/docs/links.md, "Links with the mouse"). It comes once the pointer rested on the link for a
// moment (DocumentCanvas.hoveredLink) and fades out when it leaves. It never takes the focus or a press (the canvas
// looks through it), and it moves to the bottom right when the pointer is where it would be.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Item {
    id: line
    /// The DocumentCanvas whose links it tells (a sibling over it or its child)
    property Item canvasItem
    /// "" for the notes; the reference's is named "referenceLinkStatus"
    property string namePrefix: ""
    property bool inputTransparent: true
    enabled: false  // (never a target of input or focus)
    anchors.fill: canvasItem

    readonly property var link: canvasItem ? canvasItem.hoveredLink : ({})
    readonly property bool active: !!link && link.uri !== undefined
    // (kept while it fades out)
    property string shownText: ""
    onLinkChanged: {
        if (link && link.uri !== undefined) {  // (not `active`: it may not have followed yet)
            shownText = app.linkPreview(canvasItem.view, link.uri, link.page, link.pdfPage)
        }
    }
    readonly property point pointer: canvasItem ? canvasItem.hoveredLinkPointer : Qt.point(0, 0)
    // The pointer is where the line would be (a little around it): the line goes to the other side
    readonly property bool onRight: pointer.x < box.width + 24 && pointer.y > height - box.height - 24

    Rectangle {
        id: box
        objectName: line.namePrefix === "" ? "linkStatusLine" : line.namePrefix + "LinkStatus"
        readonly property alias text: label.text
        property bool inputTransparent: true
        readonly property bool shown: line.active && line.shownText !== ""
        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: box.shown ? 80 : 150 } }
        x: line.onRight ? line.width - width - 4 : 4
        y: line.height - height - 4
        width: Math.min(label.implicitWidth + 16, Math.max(120, line.width * 0.6))
        height: label.implicitHeight + 8
        radius: 4
        // (as browsers draw it: a light grey on light pages, a dark grey in the dark theme)
        color: Material.theme === Material.Dark ? Material.color(Material.Grey, Material.Shade800)
                                                : Material.color(Material.Grey, Material.Shade100)
        border.width: 1
        border.color: Material.dividerColor
        Label {
            id: label
            objectName: box.objectName + "Text"
            property bool inputTransparent: true
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            text: line.shownText
            color: Material.primaryTextColor
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
    }
}
