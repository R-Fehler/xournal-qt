// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Behind the drawer: the page dimmed; a tap there closes the drawer (and does not draw)
Rectangle {
    objectName: "sidebarScrim"
    visible: sidebar.visible && win.sidebarAsDrawer
    anchors.fill: parent
    z: 48
    color: "#4d000000"
    opacity: win.drawerSlide
    MouseArea {
        anchors.fill: parent
        enabled: win.sidebarDrawerOpen
        onClicked: win.showSidebar(false)
    }
}
