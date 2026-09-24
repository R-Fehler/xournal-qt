// Selected PDF text of a canvas (the notes: target app, or the reference beside them: target app.reference): mark
// or copy it. The pill sits at the text and goes along with it while scrolling; once the text is out of sight it
// waits at the top edge of the canvas and offers the way back to it. A canvas for reading only: copy only.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    /// The DocumentCanvas the text is on (a sibling of this pill) and what acts on it
    property Item canvasItem
    property var target: app
    property bool hidden: false
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    readonly property bool readingOnly: canvasItem.readingOnly
    visible: target.pdfTextIsSelected && !hidden
    padding: 2
    /// The selected text on the canvas; read again whenever the view moves
    property rect box: Qt.rect(0, 0, 0, 0)
    readonly property bool above: box.y + box.height < 8
    readonly property bool below: box.y > canvasItem.height - 8
    /// The text is not in view any more
    readonly property bool away: (box.width !== 0 || box.height !== 0) && (above || below)
    function refresh() {
        const c = canvasItem
        box = target.pdfSelectionBox()
        if (away) {
            x = c.x + (c.width - width) / 2
            y = c.y + 12
            return
        }
        x = Math.max(c.x + 8, Math.min(c.x + box.x, c.x + c.width - width - 8))
        y = c.y + box.y - height - 8 < c.y ? c.y + box.y + box.height + 8
                                           : c.y + box.y - height - 8
    }
    onVisibleChanged: if (visible) refresh(); else pasteOffered = false
    /// Selected by a long press (finger or pen): paste at that place is offered too, as the long press does
    /// everywhere else (only if there is something to paste, and only where something may be pasted)
    property bool pasteOffered: false
    property point pasteAt: Qt.point(0, 0)
    function offerPaste(viewPos) {
        pasteAt = viewPos
        pasteOffered = target.canPaste() && !readingOnly
        Qt.callLater(refresh)  // (wider now)
    }
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    Connections {
        target: pill.target
        function onPdfTextSelectionChanged() { pill.refresh() }
        function onPdfTextSelected(rect) { pill.refresh() }
    }
    Connections {
        target: pill.canvasItem
        function onViewportChanged() { pill.refresh() }
    }
    RowLayout {
        spacing: 0
        // Only while the text is out of sight: back to it
        IconButton {
            objectName: pill.named("pdfBackToSelection")
            iconName: pill.above ? "xqt-chevron-up" : "xqt-chevron-down"
            tip: qsTr("Back to the selected text")
            visible: pill.away
            onClicked: pill.target.showPdfSelection()
        }
        ToolSeparator { visible: pill.away }
        // (marking: not on a canvas for reading only)
        IconButton { objectName: pill.named("pdfHighlightButton"); visible: !pill.readingOnly; iconName: "xopp-select-pdf-text-ht"; tip: qsTr("Highlight"); onClicked: pill.target.markPdfText("highlight") }
        HighlightColors { namePrefix: pill.namePrefix; visible: !pill.readingOnly; onPicked: pill.target.markPdfText("highlight") }  // a color: highlight in it right away
        ToolSeparator { visible: !pill.readingOnly }
        IconButton { objectName: pill.named("pdfUnderlineButton"); visible: !pill.readingOnly; iconName: "xqt-underline"; tip: qsTr("Underline"); onClicked: pill.target.markPdfText("underline") }
        IconButton { objectName: pill.named("pdfStrikeButton"); visible: !pill.readingOnly; iconName: "xqt-strikethrough"; tip: qsTr("Strike through"); onClicked: pill.target.markPdfText("strikethrough") }
        IconButton { objectName: pill.named("pdfCopyTextButton"); iconName: "xopp-edit-copy"; tip: qsTr("Copy text"); onClicked: pill.target.copyPdfText() }
        ToolSeparator { visible: pill.pasteOffered }
        IconButton {
            objectName: pill.named("pdfTextPaste")
            iconName: "xopp-edit-paste"
            tip: qsTr("Paste here")
            visible: pill.pasteOffered
            onClicked: {
                const at = pill.pasteAt
                pill.target.clearPdfTextSelection()  // it was about pasting, not about the text
                pill.target.pasteAt(at.x, at.y)
            }
        }
    }
}
