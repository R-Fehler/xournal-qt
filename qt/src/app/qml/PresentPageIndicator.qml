// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Presenting: the page number, for a moment after each page change (and when it starts)
Rectangle {
    id: presentIndicator
    objectName: "presentPageIndicator"
    z: 90
    visible: app.presenting && !win.presentClean && opacity > 0  // (without controls: not even the number)
    opacity: 0
    anchors.horizontalCenter: canvas.horizontalCenter
    anchors.bottom: canvas.bottom
    anchors.bottomMargin: 18 + canvas.y + canvas.height - win.canvasControlsBottom
    width: indicatorText.implicitWidth + 24
    height: 30
    radius: 15
    color: "#99000000"
    Label {
        id: indicatorText
        objectName: "presentPageIndicatorText"
        anchors.centerIn: parent
        text: app.pageNumber + " / " + app.pageCount
        color: "#ffffff"
        font.pixelSize: 14
    }
    function flash() {
        if (!app.presenting) return
        fade.stop()
        opacity = 0.9
        fade.start()
    }
    SequentialAnimation {
        id: fade
        PauseAnimation { duration: 1500 }
        NumberAnimation { target: presentIndicator; property: "opacity"; to: 0; duration: 600 }
    }
    Connections {
        target: app
        function onPageChanged() { presentIndicator.flash() }
        function onPresentingChanged() { if (app.presenting) presentIndicator.flash(); else presentIndicator.opacity = 0 }
    }
}
