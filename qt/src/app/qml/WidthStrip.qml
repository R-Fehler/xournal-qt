// The widths of the tool bar (qt/docs/adaptive-layout.md, "The tool bar"), in the form the room allows:
// - "full": the four widths and the fifth, the tool's own (tap it again or press and hold: change it);
// - "single": one cycling button showing the width in use: a tap takes the next one (as the pen pill's), a long
//   press lists all five (the fifth: tap it again or press and hold to change it).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "Popups.js" as Popups

Item {
    id: strip
    objectName: "widthStrip"
    /// "full" or "single"
    property string mode: "full"
    property int columns: -1
    property real cell: 40
    /// Where the popups open: "top" (below), "bottom" (above), "left", "right" (beside)
    property string side: "top"

    readonly property int cells: mode === "full" ? 5 : 1
    readonly property int perLine: columns > 0 ? columns : cells
    implicitWidth: Math.min(cells, perLine) * cell
    implicitHeight: Math.ceil(cells / perLine) * 46 - 2
    width: implicitWidth
    height: implicitHeight
    function cellX(i) { return (i % perLine) * cell + (cell - 40) / 2 }
    function cellY(i) { return Math.floor(i / perLine) * 46 }

    /// The dot of a width: as big as the width compared to the very thick size
    function dotOf(size) {
        const ref = app.tool, thick = app.sizeWidth(4)
        const width = size === 5 ? app.customWidth : app.sizeWidth(size)
        if (size < 5) return [6, 10, 15, 21][size - 1]
        return thick > 0 ? Math.max(3, Math.min(26, Math.sqrt(width / thick) * 21)) : 12
    }
    /// The next width of the cycle (the fifth only when it has been set)
    function nextSize() {
        const last = app.customWidth > 0 ? 5 : 4
        return app.size >= last || app.size < 1 ? 1 : app.size + 1
    }

    // One width: a filled dot (1-4) or a ring (the fifth, the tool's own)
    component WidthButton: AbstractButton {
        id: wb
        property int size: 1
        implicitWidth: 40
        implicitHeight: 44
        focusPolicy: Qt.NoFocus
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 32; height: 32; radius: 6
                color: app.size === wb.size ? "#e0e3f5" : "transparent"
            }
            Rectangle {
                anchors.centerIn: parent
                width: strip.dotOf(wb.size); height: width; radius: width / 2
                color: wb.size === 5 ? "transparent" : "#303030"
                border.width: wb.size === 5 ? Math.min(width / 2, 2.5) : 0
                border.color: "#303030"
            }
        }
    }

    Repeater {
        model: strip.mode === "full" ? 4 : 0
        delegate: WidthButton {
            required property int index
            objectName: "sizeButton" + size
            size: index + 1
            x: strip.cellX(index)
            y: strip.cellY(index)
            onClicked: app.setSize(size)
        }
    }
    // The fifth width: the tool's own, adjustable (tap it again or press and hold)
    WidthButton {
        id: customSizeButton
        objectName: "customSizeButton"
        visible: strip.mode === "full"
        x: strip.cellX(4)
        y: strip.cellY(4)
        size: 5
        enabled: app.customWidth > 0
        opacity: enabled ? 1 : 0.4
        onClicked: app.size === 5 ? customSizePopup.open() : app.setSize(5)
        onPressAndHold: { app.setSize(5); customSizePopup.open() }
        ToolTip.visible: hovered && !customSizePopup.visible
        ToolTip.text: qsTr("Own width (%1 mm): tap again or press and hold to change it").arg(customSizePopup.mmText)
        ToolTip.delay: 600
    }
    // The one cycling button: the width in use, the dots say which of the five
    AbstractButton {
        id: widthButton
        objectName: "widthButton"
        visible: strip.mode === "single"
        x: 0
        y: 0
        implicitWidth: 40
        implicitHeight: 44
        focusPolicy: Qt.NoFocus
        onClicked: app.setSize(strip.nextSize())
        onPressAndHold: widthChoices.open()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Width (tap: the next one; hold: all five)")
        ToolTip.delay: 600
        TapHandler {
            acceptedButtons: Qt.RightButton
            acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
            onTapped: widthChoices.open()
        }
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -3
                width: strip.dotOf(Math.max(1, app.size)); height: width; radius: width / 2
                color: app.size === 5 ? "transparent" : "#303030"
                border.width: app.size === 5 ? Math.min(width / 2, 2.5) : 0
                border.color: "#303030"
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 1
                spacing: 2
                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        width: 4; height: 4; radius: 2
                        color: index + 1 === app.size ? Material.accentColor : "#b4b8bd"
                    }
                }
            }
        }
    }
    // The five widths, for the cycling button's long press
    Popup {
        id: widthChoices
        objectName: "widthChoices"
        focus: true  // (Esc closes it)
        background: Rectangle {
            radius: widthChoices.asSheet ? 16 : 12
            color: "#ffffff"
            border.width: widthChoices.asSheet ? 0 : 1
            border.color: "#d5d8dc"
        }
        /// In the phone classes a bottom sheet (qt/docs/adaptive-layout.md, "The phone chrome")
        readonly property bool asSheet: typeof win !== "undefined" && win !== null && win.phoneLayout
        modal: asSheet
        dim: asSheet
        width: asSheet && parent ? Math.min(parent.width, 640) : implicitWidth
        bottomPadding: asSheet && typeof win !== "undefined" && win ? 8 + 8 + win.safeBottom : 8
        parent: asSheet ? Overlay.overlay : widthButton
        x: asSheet ? Math.round((parent.width - width) / 2)
           : strip.side === "left" ? parent.width + 4 : strip.side === "right" ? -width - 4 : 0
        y: asSheet ? parent.height - height
           : strip.side === "top" ? parent.height + 4 : strip.side === "bottom" ? -height - 4 : 0
        margins: asSheet ? 0 : 8
        padding: 8
        leftPadding: asSheet ? 16 : 8
        rightPadding: asSheet ? 16 : 8
        Column {
            spacing: 4
            Shortcut { sequence: "Back"; enabled: widthChoices.opened; onActivated: widthChoices.close() }  // (Android's back key)
            Label { text: qsTr("Width"); font.weight: Font.DemiBold; color: "#5f6368"; leftPadding: 4 }
            Row {
                spacing: widthChoices.asSheet ? 8 : 0
                Repeater {
                    model: 5
                    delegate: WidthButton {
                        required property int index
                        objectName: "widthChoice" + size
                        size: index + 1
                        enabled: size < 5 || app.customWidth > 0
                        opacity: enabled ? 1 : 0.4
                        onClicked: {
                            if (size === 5 && app.size === 5) { widthChoices.close(); customSizePopup.open(); return }
                            app.setSize(size)
                            widthChoices.close()
                        }
                        onPressAndHold: if (size === 5) { app.setSize(5); widthChoices.close(); customSizePopup.open() }
                    }
                }
            }
        }
    }
    CustomWidthPopup {
        id: customSizePopup
        parent: strip.mode === "full" ? customSizeButton : widthButton
        side: strip.side
    }
}
