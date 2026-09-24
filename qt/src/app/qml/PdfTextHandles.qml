// The two knobs at the ends of selected PDF text: drag one and the selection follows, as on a phone. They appear
// after a long press (or a right click) on text and while the "mark PDF text" tool has something selected. Laid over
// the canvas they belong to (the notes: target app; the reference beside them: target app.reference).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Item {
    id: handles
    objectName: namePrefix === "" ? "pdfTextHandles" : namePrefix + "PdfTextHandles"
    /// The DocumentCanvas (a sibling of this item) and what acts on its text
    property Item canvasItem: canvas
    property var target: app
    property string namePrefix: ""
    anchors.fill: canvasItem
    visible: target.pdfTextIsSelected
    z: 55
    /// Only the knobs take presses; everything else goes to the canvas under them (see DocumentCanvasItem).
    property bool inputTransparent: true

    /// Where the selection begins and ends (canvas coordinates); read again whenever it changes
    property rect ends: Qt.rect(0, 0, 0, 0)
    function refresh() { ends = handles.target.pdfSelectionEnds() }
    Connections {
        target: handles.target
        function onPdfTextSelectionChanged() { handles.refresh() }
        function onPdfTextModeChanged() { handles.refresh() }
        function onPageChanged() { handles.refresh() }
    }
    // The knobs sit on the text, so they go along with it whenever the view scrolls or zooms
    Connections {
        target: handles.canvasItem
        function onViewportChanged() { handles.refresh() }
    }
    onVisibleChanged: if (visible) refresh()

    component Knob: Item {
        id: knob
        property bool startEnd: false
        property point where: Qt.point(0, 0)
        width: 44
        height: 44
        x: where.x - width / 2
        y: where.y - height / 2
        // Nothing of it outside the canvas: scrolled away, the knobs are not shown (the action pill takes over)
        visible: (handles.ends.width !== 0 || handles.ends.height !== 0) && where.y > -8 &&
                 where.y < handles.height + 8 && where.x > -8 && where.x < handles.width + 8
        Rectangle {
            anchors.centerIn: parent
            width: 18; height: 18; radius: 9
            color: Material.accentColor
            border.width: 2
            border.color: "#ffffff"
        }
        DragHandler {
            target: null
            onCentroidChanged: {
                if (!active) return
                const p = knob.mapToItem(handles, centroid.position.x, centroid.position.y)
                handles.target.dragPdfSelection(p.x, p.y, knob.startEnd)
            }
            onActiveChanged: if (!active) handles.refresh()
        }
    }

    Knob { startEnd: true; where: Qt.point(handles.ends.x, handles.ends.y) }
    Knob { startEnd: false; where: Qt.point(handles.ends.x + handles.ends.width,
                                            handles.ends.y + handles.ends.height) }
}
