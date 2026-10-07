// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Putting the command bar away and getting it back: a small tab in the middle of its edge towards the pages (⋮
// keeps the end of the bar), and a slim strip while it is away. A finger gets a target of minTarget around the tab.
Item {
    objectName: "toolbarToggle"
    visible: !app.homeVisible && win.fullChrome && !app.toolbarHidden && !win.toolsInFormatBar && !win.phoneChrome
             && !win.replaying
    z: 58
    /// The target: a finger's size in the touch profile, reaching into the pages (not over the bar's buttons)
    readonly property real grip: win.adaptive.touchProfile ? win.adaptive.minTarget : 18
    width: 42
    height: grip
    // Half over the bar's edge, the rest into the pages
    x: Math.round((parent.width - width) / 2)
    y: -9
    Rectangle {
        width: 42
        height: 18
        radius: 6
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
        Image {  // up, towards the bar it puts away
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-up")
            sourceSize.width: 15
            sourceSize.height: 15
        }
    }
    TapHandler { onTapped: app.toolbarHidden = true }
    ToolTip.visible: hoverHandler.hovered
    ToolTip.text: qsTr("Hide the tool bar")
    ToolTip.delay: 600
    HoverHandler { id: hoverHandler }
}
