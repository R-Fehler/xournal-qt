// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The document swiped to: its title, for a moment
Rectangle {
    id: tabToast
    objectName: "fullScreenTabToast"
    z: 59
    anchors.horizontalCenter: parent.horizontalCenter
    y: win.controlsTop + fullScreenTabs.height + 8
    visible: opacity > 0 && win.chromeMode === "compact" && !win.zenShown
    opacity: 0
    width: Math.min(tabToastText.implicitWidth + 28, parent.width - 160)
    height: 32
    radius: 16
    color: "#e6303134"
    Label {
        id: tabToastText
        objectName: "fullScreenTabToastText"
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 28)
        elide: Text.ElideMiddle
        text: app.title
        color: "#ffffff"
        font.pixelSize: 14
    }
    function show() {
        toastFade.stop()
        opacity = 1
        toastFade.start()
    }
    SequentialAnimation {
        id: toastFade
        PauseAnimation { duration: 1200 }
        NumberAnimation { target: tabToast; property: "opacity"; to: 0; duration: 500 }
    }
}
