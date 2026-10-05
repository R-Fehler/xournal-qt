// The audience's screen of the presenter view (qt/docs/presenter-view.md): only the slide, full screen on the other
// screen, black around it. Its page follows the presenter's; strokes being written, the laser pointer and the curtain
// show here too (PresenterConsole, CanvasView::setMirror). It takes no input on the page; the keys that go from page
// to page work here as in the console's window (a clicker sends them to whichever window has the focus).
import QtQuick
import QtQuick.Window
import XournalQt.Canvas

Window {
    id: audience
    objectName: "audienceWindow"
    property QtObject console: app.presenter
    /// A digit typed here: the page number is typed on in the console's window (the audience does not see it)
    signal digitTyped(string digit)
    title: qsTr("Presentation") + " — " + app.title
    color: "#000000"
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint
    transientParent: null  // (a window of its own, on its own screen: not a dialog of the console)

    // Only the slide: the canvas has the slide's shape, as large as the screen allows, in the middle; the view shows
    // exactly the slide in it (ViewController::fitPageRect), so its space for notes stays out of view
    DocumentCanvas {
        id: audienceCanvas
        objectName: "audienceCanvas"
        readonly property size slide: audience.console.slideSize
        readonly property real scale: slide.width > 0 && slide.height > 0
                                      ? Math.min(audience.width / slide.width, audience.height / slide.height) : 1
        width: slide.width * scale
        height: slide.height * scale
        anchors.centerIn: parent
        clip: true
        enabled: false  // (nothing is written here: the presenter writes on the console)
        readingOnly: true
        view: audience.console.active ? audience.console.audienceView : null
    }

    Item {
        id: keys
        objectName: "audienceKeys"
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            const plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            switch (event.key) {
            case Qt.Key_Space: case Qt.Key_Right: case Qt.Key_Down: case Qt.Key_PageDown:
                app.nextPage(); break
            case Qt.Key_Left: case Qt.Key_Up: case Qt.Key_PageUp: case Qt.Key_Backspace:
                app.previousPage(); break
            case Qt.Key_Home: app.firstPage(); break
            case Qt.Key_End: app.lastPage(); break
            case Qt.Key_Escape: case Qt.Key_F5: app.presenting = false; break
            default:
                if (plain && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                    audience.digitTyped(String(event.key - Qt.Key_0))
                    break
                }
                return
            }
            event.accepted = true
        }
    }
}
