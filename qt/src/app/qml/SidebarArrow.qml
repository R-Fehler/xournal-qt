// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The page sidebar's tab (qt/docs/adaptive-layout.md, "The page sidebar"): an arrow at the left edge of the canvas
// area opens it (beside the page where there is room, else as the drawer); at the sidebar's edge, "‹" closes it.
// A finger's size in the touch profile; not in the compact chrome or Zen, nor while presenting, nor while the
// tool bar is put away (unless the sidebar is open: then it closes it).
AbstractButton {
    id: sidebarArrow
    objectName: "sidebarArrow"
    readonly property bool open: win.sidebarShown && sidebar.visible
    visible: win.fullChrome && !app.homeVisible && !win.hudHidden && !app.presenting
             && (open || !app.toolbarHidden || win.phoneChrome)
    z: 50  // (over the drawer and its dimmed page)
    width: win.adaptive.touchProfile ? win.adaptive.minTarget : 24
    height: win.adaptive.touchProfile ? 64 : 56
    // (40 % down: clear of the search bar at the top and the pills at the bottom)
    x: open ? sidebar.x + sidebar.width : Math.max(referenceSplit.x, win.controlsLeft)
    y: referenceSplit.y + Math.round(referenceSplit.height * 0.4 - height / 2)
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.name: open ? qsTr("Close the page sidebar") : qsTr("Open the page sidebar")
    ToolTip.visible: hovered
    ToolTip.text: open ? qsTr("Close the pages") : qsTr("Pages, layers, contents, annotations")
    ToolTip.delay: 600
    onClicked: win.showSidebar(!open)
    background: null
    contentItem: Item {
        Rectangle {  // a slim tab against the edge
            x: -radius
            anchors.verticalCenter: parent.verticalCenter
            width: 18 + radius
            height: 48
            radius: 8
            color: sidebarArrow.pressed ? "#e8e8e8" : "#f2ffffff"
            border.width: 1
            border.color: "#c9ccd1"
            Image {
                x: parent.radius + (18 - width) / 2
                anchors.verticalCenter: parent.verticalCenter
                source: app.iconUrl(sidebarArrow.open ? "xqt-chevron-left" : "xqt-chevron-right")
                sourceSize.width: 16
                sourceSize.height: 16
            }
        }
    }
}
