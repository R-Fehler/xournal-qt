// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Reading (qt/docs/zen.md): read only, anywhere. Big fields at the left and right edges (a fifth of the page's width each, at least a finger wide, its
// whole height, invisible) turn the pages: the previous or the next one (its top; presenting: the slide). A short
// arrow at that edge says the tap was taken. The page itself finds the taps (DocumentCanvas.edgeTapWidth,
// edgeTapped: a tap that is no link and no note), so a swipe there scrolls as anywhere; these items only show
// where the fields are and the hint (inputTransparent: the canvas takes the presses under them).
Item {
    id: readingTapFields
    objectName: "readingTapFields"
    readonly property bool inputTransparent: true
    visible: win.reading && !win.replaying && !pageGrid.visible && !contentsOverview.visible
    z: 4  // (over the page, under its pills)
    x: canvas.x
    y: canvas.y
    width: canvas.width
    height: canvas.height
    readonly property real fieldWidth: Math.round(Math.max(48, width * 0.2))
    function turn(step) {
        if (step < 0) {
            app.previousPage()
            previousFieldHint.flash()
        } else {
            app.nextPage()
            nextFieldHint.flash()
        }
    }
    component FieldHint: Rectangle {
        id: hint
        readonly property bool inputTransparent: true
        property bool next: true
        function flash() { flashAnimation.restart() }
        anchors.verticalCenter: parent.verticalCenter
        x: next ? parent.width - width - 12 : 12
        width: 48
        height: 48
        radius: 24
        color: "#f2fafafa"
        border.width: 1
        border.color: "#40000000"
        opacity: 0
        Image {
            anchors.centerIn: parent
            source: app.iconUrl(parent.next ? "xqt-chevron-right" : "xqt-chevron-left")
            sourceSize.width: 26
            sourceSize.height: 26
        }
        SequentialAnimation {
            id: flashAnimation
            NumberAnimation { target: hint; property: "opacity"; to: 0.9; duration: 90 }
            PauseAnimation { duration: 160 }
            NumberAnimation { target: hint; property: "opacity"; to: 0; duration: 350 }
        }
    }
    Item {
        objectName: "readingPreviousField"
        readonly property bool inputTransparent: true
        width: readingTapFields.fieldWidth
        height: parent.height
        FieldHint { id: previousFieldHint; objectName: "readingPreviousHint"; next: false }
    }
    Item {
        objectName: "readingNextField"
        readonly property bool inputTransparent: true
        x: parent.width - width
        width: readingTapFields.fieldWidth
        height: parent.height
        FieldHint { id: nextFieldHint; objectName: "readingNextHint"; next: true }
    }
}
