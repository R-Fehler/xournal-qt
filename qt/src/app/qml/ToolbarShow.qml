// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// While it is away: a slim strip at the top edge brings it back
Rectangle {
    id: toolbarShow
    objectName: "toolbarShow"
    visible: !app.homeVisible && win.modes.fullChrome && app.toolbarHidden && !win.layout.phoneChrome && !win.modes.replaying
    z: 60
    width: 96
    height: 16
    x: Math.round((parent.width - width) / 2)
    y: win.insets.controlsTop
    radius: 8
    color: "#f1f3f4"
    border.width: 1
    border.color: "#d5d8dc"
    opacity: showHover.hovered ? 1 : 0.75
    Image {  // where the bar comes in from: down from the top
        anchors.centerIn: parent
        source: app.iconUrl("xqt-chevron-down")
        sourceSize.width: 15
        sourceSize.height: 15
    }
    TapHandler { onTapped: app.toolbarHidden = false }
    HoverHandler { id: showHover }
    ToolTip.visible: showHover.hovered
    ToolTip.text: qsTr("Show the tool bar")
    ToolTip.delay: 600
}
