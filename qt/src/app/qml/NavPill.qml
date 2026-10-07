// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Back / forward after jumps (links, page grid, sidebar)
Pane {
    id: navPill
    objectName: "navPill"
    visible: (app.canGoBack || app.canGoForward) && !pageGrid.visible && !win.hudHidden
    anchors.left: canvas.left
    anchors.leftMargin: (zenDot.visible ? 56 : 20) + win.canvasControlsLeft - canvas.x  // (in Zen: beside the dot)
    readonly property real clearY: win.clearOfPills(navPill, win.canvasControlsBottom - 24 - height, [viewPill])
    y: clearY
    padding: 2
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    RowLayout {
        spacing: 0
        IconButton {
            objectName: "navBack"
            iconName: "xopp-navigate-back"
            tip: qsTr("Back to where you were (Alt+Left)")
            enabled: app.canGoBack
            onClicked: app.navigateBack()
        }
        IconButton {
            objectName: "navForward"
            iconName: "xopp-navigate-forward"
            tip: qsTr("Forward (Alt+Right)")
            enabled: app.canGoForward
            onClicked: app.navigateForward()
        }
        IconButton {
            iconName: "xqt-close"
            tip: qsTr("Forget these places")
            implicitWidth: 36
            onClicked: app.clearNavigation()
        }
    }
}
