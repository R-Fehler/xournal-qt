// While the setsquare or the compass is out (Shapes menu): a small pill at the top right of the canvas with what the
// tool can do. Its icon switches the tool off for a moment and back on: put aside, the tool is gone from the page
// and the pill shrinks to that icon; a tap on it brings both back where they were. The Shapes menu entry takes the
// tool away altogether.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "geometryPill"
    visible: app.geometryTool !== "" && !app.homeVisible && !win.cleanPage  // (presenting without controls: none)
    padding: 2
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    readonly property bool compass: app.geometryTool === "compass"
    readonly property bool small: app.geometryMinimized

    component PillToggle: ToolButton {
        id: toggle
        property string tip: ""
        checkable: true
        implicitHeight: 44
        font.pixelSize: 14
        font.weight: checked ? Font.DemiBold : Font.Normal
        Material.foreground: checked ? Material.accentColor : "#303030"
        ToolTip.visible: hovered && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 600
        background: Rectangle {
            radius: 10
            color: toggle.checked ? "#e0e3f5" : (toggle.pressed ? "#e8e8e8" : "transparent")
        }
    }

    RowLayout {
        spacing: 0
        // The tool itself: on (highlighted) or put aside
        IconButton {
            objectName: "geometryToggle"
            iconName: pill.compass ? "xopp-compass" : "xopp-setsquare"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            checked: !pill.small
            tip: pill.small ? (pill.compass ? qsTr("Bring the compass back") : qsTr("Bring the setsquare back"))
                            : (pill.compass ? qsTr("Put the compass aside") : qsTr("Put the setsquare aside"))
            onClicked: app.geometryMinimized = !pill.small
        }
        ToolSeparator { visible: !pill.small }
        // Its middle holds on to the nearest ink stroke and slides along it (for measuring along a line)
        IconButton {
            objectName: "geometryHold"
            visible: !pill.small
            iconName: "xqt-magnet"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            checked: app.geometryHeldToStroke
            tip: checked ? qsTr("Let go of the stroke") : qsTr("Hold on to the nearest stroke (its middle on it)")
            onClicked: app.geometryHeldToStroke = !app.geometryHeldToStroke
        }
        PillToggle {
            objectName: "geometrySteps"
            visible: !pill.small
            text: "15°"
            tip: qsTr("Turn it in steps of 15°")
            checked: app.geometryAngleSteps
            onToggled: app.geometryAngleSteps = checked
        }
        // The setsquare's marks drawn onto the page, and how far apart (a tap on the distance takes the other one)
        ToolSeparator { visible: !pill.small && !pill.compass }
        IconButton {
            objectName: "geometryMarks"
            visible: !pill.small && !pill.compass
            iconName: "xqt-ruler"
            implicitWidth: 44
            implicitHeight: 44
            icon.width: 22
            icon.height: 22
            tip: qsTr("Draw a mark every %1 along the edge").arg(spacing.text)
            onClicked: app.drawGeometryMarks()
        }
        PillToggle {
            id: spacing
            objectName: "geometryMarkSpacing"
            visible: !pill.small && !pill.compass
            checkable: false
            text: app.geometryMarkSpacing === 1 ? qsTr("1 cm") : qsTr("½ cm")
            tip: qsTr("Distance of the marks (tap for the other one)")
            onClicked: app.geometryMarkSpacing = app.geometryMarkSpacing === 1 ? 0.5 : 1
        }
    }
}
