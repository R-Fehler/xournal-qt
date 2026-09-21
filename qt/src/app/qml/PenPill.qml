// Without a tool bar (full screen, or the bar put away) and with pen or highlighter in hand: a small pill at a side
// of the screen with the colors one draws with, the width and a pen / highlighter switch. Drag it to another side.
// The width knob: a tap goes to the next of the five widths of the tool bar (the fifth is the one set there).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: pill
    objectName: "penPill"
    z: 59
    visible: win.noToolbar && !app.homeVisible && (app.tool === "pen" || app.tool === "highlighter")
    radius: 18
    color: "#f7ffffff"
    border.width: 1
    border.color: "#c9ccd1"

    readonly property string side: app.penPillSide
    readonly property bool vertical: side === "left" || side === "right"
    readonly property real gap: 12
    implicitWidth: vertical ? 52 : content.implicitWidth + 16
    implicitHeight: vertical ? content.implicitHeight + 16 : 52
    width: implicitWidth
    height: implicitHeight

    // Where it sits: at its side, moved along it by penPillOffset (dragging moves it and may change the side)
    x: dragging ? x : (side === "left" ? gap
                       : side === "right" ? parent.width - width - gap
                       : Math.round((parent.width - width) * app.penPillOffset))
    y: dragging ? y : (side === "top" ? gap
                       : side === "bottom" ? parent.height - height - gap
                       : Math.round((parent.height - height) * app.penPillOffset))
    property bool dragging: false

    Behavior on x { enabled: !pill.dragging; NumberAnimation { duration: 120 } }
    Behavior on y { enabled: !pill.dragging; NumberAnimation { duration: 120 } }

    DragHandler {
        id: pillDrag
        target: pill
        onActiveChanged: {
            pill.dragging = active
            if (!active) {
                // The side it was let go closest to, and where along it
                const cx = pill.x + pill.width / 2
                const cy = pill.y + pill.height / 2
                const left = cx, right = pill.parent.width - cx, top = cy, bottom = pill.parent.height - cy
                const nearest = Math.min(left, right, top, bottom)
                if (nearest === left) app.penPillSide = "left"
                else if (nearest === right) app.penPillSide = "right"
                else if (nearest === top) app.penPillSide = "top"
                else app.penPillSide = "bottom"
                app.penPillOffset = pill.vertical ? cy / Math.max(1, pill.parent.height)
                                                  : cx / Math.max(1, pill.parent.width)
            }
        }
    }

    GridLayout {
        id: content
        anchors.centerIn: parent
        columns: pill.vertical ? 1 : -1
        rows: pill.vertical ? -1 : 1
        rowSpacing: 2
        columnSpacing: 2

        // Pen or highlighter
        AbstractButton {
            objectName: "penPillTool"
            implicitWidth: 36
            implicitHeight: 36
            onClicked: app.selectTool(app.tool === "highlighter" ? "pen" : "highlighter")
            ToolTip.visible: hovered
            ToolTip.text: app.tool === "highlighter" ? qsTr("Pen") : qsTr("Highlighter")
            ToolTip.delay: 600
            contentItem: Image {
                source: app.iconUrl(app.tool === "highlighter" ? "xopp-tool-highlighter" : "xopp-tool-pencil")
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.Pad
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignVCenter
            }
        }
        Rectangle {  // a line between the parts
            Layout.preferredWidth: pill.vertical ? 24 : 1
            Layout.preferredHeight: pill.vertical ? 1 : 24
            Layout.alignment: Qt.AlignCenter
            color: "#d5d8dc"
        }

        Repeater {
            model: app.penColors
            delegate: AbstractButton {
                id: dot
                required property color modelData
                required property int index
                objectName: "penPillColor"
                implicitWidth: 36
                implicitHeight: 36
                onClicked: app.setColor(modelData)
                onPressAndHold: Popups.openAt(dotMenu)
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 24; height: 24; radius: 12
                        color: dot.modelData
                        border.width: Qt.colorEqual(app.color, dot.modelData) ? 3 : 1
                        border.color: Qt.colorEqual(app.color, dot.modelData) ? Material.accentColor : "#9e9e9e"
                    }
                }
                Menu {
                    id: dotMenu
                    MenuItem { text: qsTr("Remove this color"); onTriggered: app.removePenColor(dot.index) }
                    MenuItem { text: qsTr("Add a color…"); onTriggered: pillColorDialog.open() }
                    MenuItem { text: qsTr("Default colors"); onTriggered: app.resetPenColors() }
                }
            }
        }
        AbstractButton {
            objectName: "penPillAddColor"
            implicitWidth: 36
            implicitHeight: 36
            onClicked: pillColorDialog.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Add a color (press and hold one to remove it)")
            ToolTip.delay: 600
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 24; height: 24; radius: 12
                    color: "transparent"
                    border.width: 1
                    border.color: "#9e9e9e"
                    Label { anchors.centerIn: parent; text: "+"; font.pixelSize: 16; color: "#5f6368" }
                }
            }
        }
        Rectangle {
            Layout.preferredWidth: pill.vertical ? 24 : 1
            Layout.preferredHeight: pill.vertical ? 1 : 24
            Layout.alignment: Qt.AlignCenter
            color: "#d5d8dc"
        }

        // The width: a tap takes the next of the five, after the fifth the first again
        AbstractButton {
            id: widthKnob
            objectName: "penPillWidth"
            implicitWidth: 36
            implicitHeight: 36
            readonly property real dotSize: {
                const ref = app.tool, thick = app.sizeWidth(4)
                const width = app.size === 5 ? app.customWidth : app.sizeWidth(app.size)
                return thick > 0 ? Math.max(4, Math.min(26, Math.sqrt(width / thick) * 20)) : 10
            }
            onClicked: app.setSize(app.size >= 5 ? 1 : app.size + 1)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Width (tap for the next one)")
            ToolTip.delay: 600
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: widthKnob.dotSize; height: width; radius: width / 2
                    color: app.color
                    border.width: 1
                    border.color: "#80000000"
                }
            }
        }
    }
}
