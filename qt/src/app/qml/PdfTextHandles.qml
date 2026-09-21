// The two knobs at the ends of selected PDF text: drag one and the selection follows, as on a phone. They appear
// after a long press (or a right click) on text and while the "mark PDF text" tool has something selected.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Item {
    id: handles
    objectName: "pdfTextHandles"
    anchors.fill: canvas
    visible: app.pdfTextIsSelected
    z: 55
    /// Only the knobs take presses; everything else goes to the canvas under them (see DocumentCanvasItem).
    property bool inputTransparent: true

    /// Where the selection begins and ends (canvas coordinates); read again whenever it changes
    property rect ends: Qt.rect(0, 0, 0, 0)
    function refresh() { ends = app.pdfSelectionEnds() }
    Connections {
        target: app
        function onPdfTextSelectionChanged() { handles.refresh() }
        function onPdfTextModeChanged() { handles.refresh() }
        function onPageChanged() { handles.refresh() }
    }
    // The knobs sit on the text, so they go along with it whenever the view scrolls or zooms
    Connections {
        target: canvas
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
                app.dragPdfSelection(p.x, p.y, knob.startEnd)
            }
            onActiveChanged: if (!active) handles.refresh()
        }
    }

    Knob { startEnd: true; where: Qt.point(handles.ends.x, handles.ends.y) }
    Knob { startEnd: false; where: Qt.point(handles.ends.x + handles.ends.width,
                                            handles.ends.y + handles.ends.height) }
}
