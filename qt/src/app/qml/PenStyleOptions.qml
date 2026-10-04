// The pen's options in the menu of the pen and shape buttons (ToolCycleButton; a long press or a right click): the
// line style (upstream's: plain, dashed, dash-dot, dotted). Upstream keeps it per tool, in its settings and in each
// stroke of a .xopp. Small and self-contained: qt/toolbox will carry these options into its tool presets.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

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

    /// Upstream's line styles (StrokeStyle): its name, the dashes (in widths of the line), what it is called
    readonly property var lineStyles: [
        { key: "plain", dashes: [], name: qsTr("Solid") },
        { key: "dash", dashes: [6, 3], name: qsTr("Dashed") },
        { key: "dashdot", dashes: [6, 3, 0.5, 3], name: qsTr("Dash-dot") },
        { key: "dot", dashes: [0.5, 3], name: qsTr("Dotted") }
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
                    // A sample of the line (the dashes as upstream draws them: in widths of the line, round caps)
                    Canvas {
                        id: sample
                        anchors.centerIn: parent
                        width: 32; height: 12
                        readonly property var dashes: styleButton.modelData.dashes
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const w = 2.5
                            ctx.lineWidth = w
                            ctx.lineCap = "round"
                            ctx.strokeStyle = "#303030"
                            ctx.setLineDash(dashes)  // (in widths of the sample line; upstream: points)
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
}
