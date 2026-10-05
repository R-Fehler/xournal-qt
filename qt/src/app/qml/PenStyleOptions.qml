// The pen's options in the menu of the pen and shape buttons (ToolCycleButton; a long press or a right click): the
// line style (upstream's: plain, dashed, dash-dot, dotted) and the filling of shapes and strokes (upstream's fill and
// fill opacity; the pen also fills with a color of its own, PenFill.h). Upstream keeps them per tool, in its settings
// and in each stroke of a .xopp. Small and self-contained: qt/toolbox will carry these options into its tool presets.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "LineStyles.js" as LineStyles

Column {
    id: options
    objectName: "penStyleOptions"
    /// Shown in the menu (AdaptiveMenu and the phone's MenuSheet read `offered`)
    property bool offered: true
    visible: offered
    height: offered ? implicitHeight : 0
    width: parent ? parent.width : implicitWidth
    spacing: 2
    topPadding: 4
    bottomPadding: 4

    /// Upstream's line styles (StrokeStyle): its key, what it is called (the samples: LineStyles.js)
    readonly property var lineStyles: [
        { key: "plain", name: qsTr("Solid") },
        { key: "dash", name: qsTr("Dashed") },
        { key: "dashdot", name: qsTr("Dash-dot") },
        { key: "dot", name: qsTr("Dotted") }
    ]

    Rectangle { width: parent.width; height: 1; color: "#e3e5e8" }  // (a line above the options)

    RowLayout {
        objectName: "lineStyleRow"
        visible: app.hasLineStyle
        width: parent.width
        spacing: 2
        Label {
            text: qsTr("Line")
            Layout.leftMargin: 16
            Layout.fillWidth: true
            color: "#5f6368"
        }
        Repeater {
            model: options.lineStyles
            delegate: AbstractButton {
                id: styleButton
                required property var modelData
                objectName: "lineStyle_" + modelData.key
                implicitWidth: 44
                implicitHeight: 40
                focusPolicy: Qt.NoFocus
                readonly property bool current: app.lineStyle === modelData.key
                onClicked: app.lineStyle = modelData.key
                ToolTip.visible: hovered
                ToolTip.text: modelData.name
                ToolTip.delay: 600
                Accessible.name: modelData.name
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 40; height: 32; radius: 6
                        color: styleButton.current ? "#e0e3f5" : "transparent"
                        border.width: styleButton.current ? 1 : 0
                        border.color: Material.accentColor
                    }
                    // A sample of the line (LineStyles.js: dashes that show in a short line)
                    Canvas {
                        id: sample
                        objectName: "lineStyleSample"
                        anchors.centerIn: parent
                        width: 32; height: 12
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const w = 2.5
                            const key = styleButton.modelData.key
                            ctx.lineWidth = w
                            ctx.lineCap = LineStyles.sampleCap(key, "round")
                            ctx.strokeStyle = "#303030"
                            ctx.setLineDash(LineStyles.sampleDashes(key, w))
                            ctx.beginPath()
                            ctx.moveTo(w, height / 2)
                            ctx.lineTo(width - w, height / 2)
                            ctx.stroke()
                        }
                    }
                }
            }
        }
        Item { Layout.preferredWidth: 6 }
    }

    // The filling: on or off; its color (the line's, or another one for the pen) and its opacity
    RowLayout {
        objectName: "fillRow"
        visible: app.hasFill
        width: parent.width
        spacing: 2
        Label {
            text: qsTr("Fill")
            Layout.leftMargin: 16
            Layout.fillWidth: true
            color: "#5f6368"
        }
        Switch {
            objectName: "fillSwitch"
            checked: app.fillEnabled
            focusPolicy: Qt.NoFocus
            onToggled: app.fillEnabled = checked
            Accessible.name: qsTr("Fill")
        }
    }
    Flow {
        id: fillColors
        objectName: "fillColorRow"
        visible: app.hasFill && app.fillEnabled && app.hasFillColor
        x: 12
        width: parent.width - 24
        spacing: 2
        /// The fill color in use ("transparent": the line's color)
        readonly property color current: app.fillColor
        readonly property bool sameAsLine: current.a === 0
        component Swatch: AbstractButton {
            id: swatch
            property color shown
            property bool current: false
            implicitWidth: 34
            implicitHeight: 36
            focusPolicy: Qt.NoFocus
            ToolTip.visible: hovered && ToolTip.text !== ""
            ToolTip.delay: 600
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 24; height: 24; radius: 12
                    color: swatch.shown
                    border.width: swatch.current ? 3 : 1
                    border.color: swatch.current ? Material.accentColor : "#9e9e9e"
                }
            }
        }
        // The line's color (upstream's filling)
        Swatch {
            objectName: "fillSameColor"
            shown: app.color
            current: fillColors.sameAsLine
            onClicked: app.fillColor = "transparent"
            ToolTip.text: qsTr("The line's color")
            Accessible.name: ToolTip.text
            Rectangle {  // (a small line across it: the line's own color)
                anchors.centerIn: parent
                width: 14; height: 2; rotation: -45
                color: "#ffffff"
            }
        }
        Repeater {
            model: app.toolbarColors
            delegate: Swatch {
                required property color modelData
                objectName: "fillColor"
                shown: modelData
                current: !fillColors.sameAsLine && Qt.colorEqual(fillColors.current, modelData)
                onClicked: app.fillColor = modelData
            }
        }
        Swatch {
            objectName: "fillOtherColor"
            shown: "transparent"
            ToolTip.text: qsTr("Another color…")
            Accessible.name: ToolTip.text
            onClicked: fillColorDialog.open()
            Label { anchors.centerIn: parent; text: "+"; font.pixelSize: 16; color: "#5f6368" }
        }
    }
    RowLayout {
        objectName: "fillOpacityRow"
        visible: app.hasFill && app.fillEnabled
        width: parent.width
        spacing: 2
        Label {
            text: qsTr("Opacity")
            Layout.leftMargin: 16
            color: "#5f6368"
        }
        Slider {
            objectName: "fillOpacity"
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            from: 1; to: 255; stepSize: 1
            value: app.fillAlpha
            focusPolicy: Qt.NoFocus
            onMoved: app.fillAlpha = Math.round(value)
        }
        Label {
            Layout.preferredWidth: 44
            Layout.rightMargin: 12
            horizontalAlignment: Text.AlignRight
            text: Math.round(app.fillAlpha * 100 / 255) + " %"
        }
    }
    ColorDialog {
        id: fillColorDialog
        title: qsTr("Fill color")
        selectedColor: app.fillColor.a > 0 ? app.fillColor : app.color
        onAccepted: app.fillColor = selectedColor
    }
}
