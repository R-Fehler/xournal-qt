// The phone's tool dock (qt/docs/adaptive-layout.md, "The phone chrome"): in the phone classes the tools are one bar at
// the bottom, within reach of the thumb, instead of the tool bar, the pen pill and the view pill:
//   the tool in use | all tools | the color | the width | undo | redo | the page number
// - the tool in use is a cycling button (a tap: its next variant; a long press: its variants); a tool without variants
//   is its tool bar button (the text box: the font; mark PDF text: how it marks);
// - "All tools" opens the sheet with every tool and its variants, and the other buttons of the tool bar;
// - the color and the width are the tool bar's cycling buttons (a tap: the next one; a long press: all of them, as a
//   sheet): Main.qml puts them into `colorSlot` and `widthSlot`;
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
    readonly property Item colorSlot: colorHolder
    readonly property Item widthSlot: widthHolder
    signal toolsRequested()
    signal pagesRequested()
    /// The bar's thickness
    readonly property int barSize: 56
    /// A button: a finger's size (48) where there is room
    readonly property int target: win.adaptive.touchProfile ? win.adaptive.minTarget : 48
    implicitHeight: vertical ? 0 : barSize + safeBottom
    implicitWidth: vertical ? barSize : 0
    color: "#ffffff"

    /// The group of the tool in use (a cycling button), or ""
    readonly property string currentGroup: {
        const g = win.toolGroups
        const names = ["pen", "shape", "select", "eraser"]
        for (let i = 0; i < names.length; ++i) if (g.isActive(names[i])) return names[i]
        return ""
    }
    /// The tool bar's button of a tool without variants
    readonly property var currentSlot: {
        const s = toolArea.slots, t = app.tool
        if (t === "hand") return s.hand
        if (t === "text") return s.text
        if (t === "selectPdfTextLinear" || t === "selectPdfTextRect") return s.pdfText
        return s.pen
    }

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

    GridLayout {
        id: row
        x: dock.vertical ? 0 : 4
        y: dock.vertical ? 4 : 0
        width: dock.vertical ? dock.barSize : dock.width - 8
        height: dock.vertical ? dock.height - 8 - dock.safeBottom : dock.barSize
        columns: dock.vertical ? 1 : 7
        rows: dock.vertical ? 7 : 1
        rowSpacing: 0
        columnSpacing: 0

        // The tool in use (a text document has no ink tools)
        Cell {
            id: toolCell
            visible: !win.textDoc
            Loader {
                id: toolLoader
                anchors.centerIn: parent
                readonly property string group: dock.currentGroup
                sourceComponent: group !== "" ? cycleButton : plainButton
            }
            Component {
                id: cycleButton
                ToolCycleButton {
                    objectName: "dockToolButton"
                    group: toolLoader.group
                    implicitWidth: toolCell.side
                    implicitHeight: toolCell.side
                }
            }
            Component {
                id: plainButton
                IconButton {
                    objectName: "dockToolButton"
                    readonly property var slot: dock.currentSlot
                    implicitWidth: toolCell.side
                    implicitHeight: toolCell.side
                    iconName: slot ? slot.iconName : ""
                    label: slot ? slot.label : ""
                    tip: slot ? slot.tip : ""
                    checked: true
                    ownHold: slot ? slot.ownHold : false
                    onClicked: if (slot) slot.clicked()
                    onPressAndHold: if (slot && slot.ownHold) slot.pressAndHold()
                }
            }
        }
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
        // The color and the width: the tool bar's cycling buttons (Main.qml puts them here)
        Cell {
            visible: !win.textDoc
            Item {
                id: colorHolder
                objectName: "dockColorSlot"
                anchors.centerIn: parent
                width: 40
                height: 44
            }
        }
        Cell {
            visible: !win.textDoc
            Item {
                id: widthHolder
                objectName: "dockWidthSlot"
                anchors.centerIn: parent
                width: 40
                height: 44
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
                tip: qsTr("Undo (Ctrl+Z)")
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
                tip: qsTr("Redo (Ctrl+Y)")
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
