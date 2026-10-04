// The colors of the tool bar (qt/docs/adaptive-layout.md, "The tool bar"), in the form the room allows (ToolBarPlan.js):
// - "full": the palette (the tool bar's colors) and "+" (the color chooser);
// - "recent": the current color, the colors used last (then the palette's) and a palette button (the color chooser);
// - "single": one cycling button: a tap takes the next of the first five palette colors, a long press the chooser.
// The color chooser (ColorChooser.qml) has the tool bar's colors and "Add a color…" in its first tab, then a tab for
// each color palette.
// A color: a tap uses it; a long press (or a right click) offers removing it, adding one, the default colors.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "Popups.js" as Popups

Item {
    id: strip
    objectName: "colorStrip"
    /// "full", "recent" or "single"
    property string mode: "full"
    /// How many recent colors ("recent")
    property int recentCount: 4
    /// Cells per line (-1: one line)
    property int columns: -1
    /// The width of a cell
    property real cell: 40
    /// Where the popups open: "top" (below), "bottom" (above), "left", "right" (beside)
    property string side: "top"

    readonly property var palette: app.toolbarColors
    /// The colors to show besides the current one ("recent"): the ones used last, then the palette's
    readonly property var others: {
        const out = []
        const seen = function(c) { return out.some(function(o) { return Qt.colorEqual(o, c) }) || Qt.colorEqual(app.color, c) }
        const recent = win.toolGroups.recentColors
        for (let i = 0; i < recent.length; ++i) if (!seen(recent[i])) out.push(recent[i])
        for (let j = 0; j < palette.length; ++j) if (!seen(palette[j])) out.push(palette[j])
        return out
    }
    /// The colors a tap on the cycling button goes through
    readonly property var cycleColors: palette.slice(0, 5)
    readonly property var shown: mode === "full" ? palette : mode === "recent" ? [app.color].concat(others.slice(0, recentCount)) : []
    readonly property int cells: shown.length + 1
    readonly property int perLine: columns > 0 ? columns : cells
    implicitWidth: Math.min(cells, perLine) * cell
    implicitHeight: Math.ceil(cells / perLine) * 46 - 2
    width: implicitWidth
    height: implicitHeight

    component Swatch: AbstractButton {
        id: swatch
        property color swatchColor
        /// Its place in the palette (-1: not in it)
        property int paletteIndex: -1
        implicitWidth: 40
        implicitHeight: 44
        focusPolicy: Qt.NoFocus
        onClicked: app.setColor(swatchColor)
        onPressAndHold: Popups.openAt(swatchMenu)
        ToolTip.visible: hovered
        ToolTip.text: String(swatchColor)
        ToolTip.delay: 600
        TapHandler {
            acceptedButtons: Qt.RightButton
            acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
            onTapped: function(point) { Popups.openAt(swatchMenu, point.position) }
        }
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 28; height: 28; radius: 14
                color: swatch.swatchColor
                border.width: Qt.colorEqual(app.color, swatch.swatchColor) ? 3 : 1
                border.color: Qt.colorEqual(app.color, swatch.swatchColor) ? Material.accentColor : "#9e9e9e"
            }
        }
        AdaptiveMenu {
            id: swatchMenu
            title: qsTr("Color")
            titleShown: true
            AdaptiveMenuItem {
                offered: swatch.paletteIndex >= 0
                text: qsTr("Remove from the tool bar")
                onTriggered: app.removeToolbarColor(swatch.paletteIndex)
            }
            AdaptiveMenuItem { text: qsTr("Add a color…"); onTriggered: colorDialog.open() }
            AdaptiveMenuItem { text: qsTr("Default colors"); onTriggered: app.resetToolbarColors() }
        }
    }
    function paletteIndexOf(c) {
        for (let i = 0; i < palette.length; ++i) if (Qt.colorEqual(palette[i], c)) return i
        return -1
    }
    function cellX(i) { return (i % perLine) * cell + (cell - 40) / 2 }
    function cellY(i) { return Math.floor(i / perLine) * 46 }

    Repeater {
        model: strip.shown
        delegate: Swatch {
            required property color modelData
            required property int index
            objectName: "colorSwatch"
            x: strip.cellX(index)
            y: strip.cellY(index)
            swatchColor: modelData
            paletteIndex: strip.paletteIndexOf(modelData)
        }
    }
    // The last cell: "+" (full), the palette (recent), or the one cycling button (single)
    AbstractButton {
        id: lastCell
        objectName: strip.mode === "full" ? "addColorButton" : strip.mode === "recent" ? "paletteButton" : "colorCycleButton"
        x: strip.cellX(strip.shown.length)
        y: strip.cellY(strip.shown.length)
        implicitWidth: 40
        implicitHeight: 44
        focusPolicy: Qt.NoFocus
        readonly property int cycleIndex: {
            for (let i = 0; i < strip.cycleColors.length; ++i) if (Qt.colorEqual(strip.cycleColors[i], app.color)) return i
            return -1
        }
        onClicked: {
            if (strip.mode !== "single") strip.openPalette()  // (the palettes, and "Add a color…")
            else if (strip.cycleColors.length > 0) app.setColor(strip.cycleColors[(cycleIndex + 1) % strip.cycleColors.length])
        }
        onPressAndHold: if (strip.mode !== "full") strip.openPalette()
        ToolTip.visible: hovered
        ToolTip.text: strip.mode === "full" ? qsTr("More colors: palettes, add a color (press and hold a color to remove it)")
                      : strip.mode === "recent" ? qsTr("All colors")
                      : qsTr("Color (tap: the next one; hold: all colors)")
        ToolTip.delay: 600
        contentItem: Item {
            Rectangle {  // "+", or the current color with a sign of more
                anchors.centerIn: parent
                width: 28; height: 28; radius: 14
                color: strip.mode === "single" ? app.color : "transparent"
                border.width: strip.mode === "single" ? 3 : 1
                border.color: strip.mode === "single" ? Material.accentColor : "#9e9e9e"
                Label {
                    anchors.centerIn: parent
                    visible: strip.mode === "full"
                    text: "+"
                    font.pixelSize: 18
                    color: "#5f6368"
                }
                Image {  // the palette
                    anchors.centerIn: parent
                    visible: strip.mode === "recent"
                    source: visible ? app.iconUrl("xqt-palette") : ""
                    sourceSize.width: 18
                    sourceSize.height: 18
                }
            }
            Row {  // the cycling button: how many colors, and which one
                visible: strip.mode === "single"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 1
                spacing: 2
                Repeater {
                    model: strip.cycleColors.length
                    delegate: Rectangle {
                        required property int index
                        width: 4; height: 4; radius: 2
                        color: index === lastCell.cycleIndex ? Material.accentColor : "#b4b8bd"
                    }
                }
            }
        }
    }

    function openPalette() {
        palettePopup.open()
    }
    // All colors (ColorChooser.qml): the first tab has the tool bar's colors, the recent ones and "Add a color"; the
    // others the color palettes
    ColorChooser {
        id: palettePopup
        objectName: "colorPalette"
        anchorItem: lastCell
        side: strip.side
        Label { text: qsTr("Tool bar colors"); font.weight: Font.DemiBold; color: "#5f6368" }
        Grid {
            columns: palettePopup.columns
            columnSpacing: palettePopup.asSheet ? 8 : 0
            Repeater {
                model: palettePopup.opened ? strip.palette : []
                delegate: Swatch {
                    required property color modelData
                    required property int index
                    objectName: "paletteSwatch"
                    swatchColor: modelData
                    paletteIndex: index
                    onClicked: palettePopup.close()
                }
            }
        }
        Label {
            visible: recentGrid.count > 0
            text: qsTr("Used lately")
            color: "#5f6368"
            font.pixelSize: 13
        }
        Grid {
            columns: palettePopup.columns
            columnSpacing: palettePopup.asSheet ? 8 : 0
            Repeater {
                id: recentGrid
                model: palettePopup.opened ? win.toolGroups.recentColors.filter(function(c) { return strip.paletteIndexOf(c) < 0 }) : []
                delegate: Swatch {
                    required property color modelData
                    swatchColor: modelData
                    onClicked: palettePopup.close()
                }
            }
        }
        Button {
            objectName: "paletteAddColor"
            flat: true
            text: qsTr("Add a color…")
            onClicked: { palettePopup.close(); colorDialog.open() }
        }
    }
}
