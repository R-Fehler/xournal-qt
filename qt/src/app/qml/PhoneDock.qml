// The phone's tool dock (qt/docs/adaptive-layout.md, "The phone chrome"; qt/docs/toolbox.md, "On a phone"): in the
// phone classes the tools are one bar at the bottom, within reach of the thumb, instead of the command bar and the view
// pill. It hosts the toolbox: the same rail as on a larger screen, at the bottom edge (undo, redo, the same items
// scrolling sideways, "My tools", the page number; qt/rail-scroll). A text
// document has no ink tools: then the dock is
//   all tools | undo | redo | the page number
// - "All tools" opens the sheet with the other tools and the commands of the command bar;
// - the page number: a tap shows all pages (the contents and the zoom are there).
// Held sideways (a phone in landscape) it is a rail at the right side, the same buttons from the top down.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: dock
    objectName: "phoneDock"
    /// A rail at the side instead of a bar at the bottom
    property bool vertical: false
    /// Room below it for the system's navigation bar
    property real safeBottom: 0
    /// Room at the sides for a camera cut-out or the navigation bar (a phone held sideways: the rail at the right
    /// takes the right one)
    property real safeLeft: 0
    property real safeRight: 0
    /// The toolbox fills the dock (qt/docs/toolbox.md, "On a phone"): its own cells give way
    property bool hostsToolbox: false
    readonly property Item toolboxSlot: toolboxHolder
    signal toolsRequested()
    signal pagesRequested()
    /// The bar's thickness
    readonly property int barSize: 56
    /// A button: a finger's size (48) where there is room
    readonly property int target: win.adaptive.touchProfile ? win.adaptive.minTarget : 48
    implicitHeight: vertical ? 0 : barSize + safeBottom
    implicitWidth: vertical ? barSize + safeRight : 0
    color: "#ffffff"


    Rectangle {  // the line towards the page
        width: dock.vertical ? 1 : parent.width
        height: dock.vertical ? parent.height : 1
        color: "#d5d8dc"
    }

    component Cell: Item {
        Layout.fillWidth: !dock.vertical
        Layout.fillHeight: dock.vertical
        Layout.minimumWidth: dock.vertical ? dock.barSize : 36
        Layout.minimumHeight: dock.vertical ? 36 : dock.barSize
        Layout.preferredWidth: dock.vertical ? dock.barSize : dock.target
        Layout.preferredHeight: dock.vertical ? dock.target : dock.barSize
        /// The button inside: at most a finger's size, less where the dock is short of room
        readonly property real side: Math.min(dock.target, width, height)
    }

    Item {
        id: toolboxHolder
        objectName: "dockToolboxSlot"
        visible: dock.hostsToolbox
        x: dock.vertical ? 0 : dock.safeLeft
        width: dock.vertical ? dock.barSize : dock.width - dock.safeLeft - dock.safeRight
        height: dock.vertical ? dock.height - dock.safeBottom : dock.barSize
    }
    GridLayout {
        id: row
        visible: !dock.hostsToolbox
        x: dock.vertical ? 0 : 4 + dock.safeLeft
        y: dock.vertical ? 4 : 0
        width: dock.vertical ? dock.barSize : dock.width - 8 - dock.safeLeft - dock.safeRight
        height: dock.vertical ? dock.height - 8 - dock.safeBottom : dock.barSize
        columns: dock.vertical ? 1 : 4
        rows: dock.vertical ? 4 : 1
        rowSpacing: 0
        columnSpacing: 0

        // Every tool, with its name
        Cell {
            IconButton {
                objectName: "dockToolsButton"
                anchors.centerIn: parent
                implicitWidth: parent.side
                implicitHeight: parent.side
                iconName: "xqt-layout-grid"
                label: qsTr("All tools")
                tip: qsTr("All tools, with their names")
                onClicked: dock.toolsRequested()
            }
        }
        Cell {
            IconButton {
                objectName: "dockUndoButton"
                anchors.centerIn: parent
                implicitWidth: parent.side
                implicitHeight: parent.side
                iconName: "xopp-edit-undo"
                label: qsTr("Undo")
                tip: win.withKeys(qsTr("Undo"), "undo")
                enabled: app.canUndo
                onClicked: app.undo()
            }
        }
        Cell {
            IconButton {
                objectName: "dockRedoButton"
                anchors.centerIn: parent
                implicitWidth: parent.side
                implicitHeight: parent.side
                iconName: "xopp-edit-redo"
                label: qsTr("Redo")
                tip: win.withKeys(qsTr("Redo"), "redo")
                enabled: app.canRedo
                onClicked: app.redo()
            }
        }
        // The page number: all pages (and there the contents and the zoom)
        Cell {
            Layout.preferredWidth: dock.vertical ? dock.barSize : Math.max(dock.target, pageButton.implicitWidth)
            ToolButton {
                id: pageButton
                objectName: "dockPageButton"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width)
                implicitHeight: parent.side
                text: dock.vertical ? app.pageNumber + "/" + app.pageCount : app.pageNumber + " / " + app.pageCount
                font.pixelSize: dock.vertical ? 12 : 14
                leftPadding: dock.vertical ? 2 : 8
                rightPadding: dock.vertical ? 2 : 8
                focusPolicy: Qt.NoFocus
                Material.foreground: "#3c4043"
                Accessible.name: qsTr("All pages")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("All pages, the contents and the zoom (Ctrl+Alt+G)")
                ToolTip.delay: 600
                onClicked: dock.pagesRequested()
            }
        }
    }
}
