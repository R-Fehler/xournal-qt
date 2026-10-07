// xournal-qt: Settings → Pen: the toolbox (back to the first layout or tools), the pen and the
// eraser, shapes, the grid.
// Part of SettingsPage.qml, instantiated once there: it reads the sheet through `sheet` (SettingsPage.qml's
// context: `sheet.s` is app.settings, `sheet.narrow`, `sheet.win`); its rows are Settings*Row.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    contentWidth: availableWidth
    ColumnLayout {
        width: parent.width - 48
        x: 24
        spacing: 10
        // The toolbox (qt/docs/toolbox.md)
        SettingsSectionTitle { text: qsTr("Tools") }
        SettingsHint {
            text: qsTr("The toolbox holds your own tools, each with its color and width, beside the page "
                       + "(drag its dotted grip to another edge). Tap a tool to take it, tap it again to "
                       + "change it; hold it for its menu, hold and move it to sort it, onto another to "
                       + "group them, onto the top bar or away from both bars. + adds a tool or a command.")
        }
        // Both bars as at a first start, the tools kept (qt/top-bar)
        Button {
            objectName: "resetBarsButton"
            text: qsTr("Back to the first layout…")
            onClicked: resetBarsDialog.open()
        }
        Button {
            objectName: "resetToolboxButton"
            text: qsTr("Back to the first tools…")
            onClicked: resetToolboxDialog.open()
        }
        SettingsSectionTitle { text: qsTr("Pressure") }
        SettingsSwitchRow { key: "pressureSensitivity"; text: qsTr("Pressure changes the line width") }
        SettingsSliderRow {
            key: "minimumPressure"; text: qsTr("Minimum pressure")
            from: 0.01; to: 1; stepSize: 0.01
        }
        SettingsSliderRow {
            key: "pressureMultiplier"; text: qsTr("Pressure multiplier")
            from: 0.5; to: 4; stepSize: 0.05
        }
        SettingsSwitchRow {
            key: "pressureGuessing"
            text: qsTr("Guess pressure from the speed (for pens and mice without pressure)")
        }
        SettingsSectionTitle { text: qsTr("Buttons and eraser") }
        SettingsComboRow {
            key: "eraserButtonTool"; text: qsTr("Side button / eraser end")
            options: [
                { text: qsTr("Eraser"), value: "eraser" },
                { text: qsTr("Hand (scroll)"), value: "hand" },
                { text: qsTr("Highlighter"), value: "highlighter" },
                { text: qsTr("Rectangle selection"), value: "selectRect" },
                { text: qsTr("Lasso selection"), value: "selectRegion" },
                { text: qsTr("No change (keep the tool)"), value: "none" }
            ]
        }
        SettingsComboRow {
            key: "stylusButtonTool"; text: qsTr("Lower side button")
            options: [
                { text: qsTr("Eraser"), value: "eraser" },
                { text: qsTr("No change (keep the tool)"), value: "none" },
                { text: qsTr("Hand (scroll)"), value: "hand" },
                { text: qsTr("Highlighter"), value: "highlighter" },
                { text: qsTr("Rectangle selection"), value: "selectRect" },
                { text: qsTr("Lasso selection"), value: "selectRegion" }
            ]
        }
        SettingsComboRow {
            key: "stylusButton2Tool"; text: qsTr("Upper side button")
            options: [
                { text: qsTr("Eraser"), value: "eraser" },
                { text: qsTr("No change (keep the tool)"), value: "none" },
                { text: qsTr("Hand (scroll)"), value: "hand" },
                { text: qsTr("Highlighter"), value: "highlighter" },
                { text: qsTr("Rectangle selection"), value: "selectRect" },
                { text: qsTr("Lasso selection"), value: "selectRegion" }
            ]
        }
        SettingsComboRow {
            key: "eraserMode"; text: qsTr("Eraser")
            options: [
                { text: qsTr("Standard (cuts strokes)"), value: "default" },
                { text: qsTr("Whole strokes"), value: "deleteStroke" },
                { text: qsTr("Whiteout"), value: "whiteout" }
            ]
        }
        SettingsComboRow {
            objectName: "hoverPointerRow"
            key: "hoverPointer"; text: qsTr("Pointer over the page")
            options: [
                { text: qsTr("Dot"), value: "dot" },
                { text: qsTr("Crosshair"), value: "crosshair" }
            ]
        }
        SettingsHint {
            text: qsTr("What the mouse and the hovering pen show over the page: a small dot where the "
                       + "pen will write, or the crosshair. With the eraser it is the eraser itself, gray, "
                       + "as big as it erases at the zoom (dashed when it erases whole strokes).")
        }
        // The pen's gestures (qt/docs/pen-gestures.md)
        SettingsSectionTitle { text: qsTr("Gestures") }
        SettingsSwitchRow {
            objectName: "holdToStraightenSwitch"
            key: "holdToStraighten"; text: qsTr("Hold to straighten")
        }
        SettingsSliderRow {
            objectName: "holdToStraightenTimeSlider"
            visible: (sheet.s.revision, sheet.s.get("holdToStraighten"))
            key: "holdToStraightenTime"; text: qsTr("Hold for")
            from: 250; to: 2000; stepSize: 50
            factor: 0.001; decimals: 2; suffix: " s"
        }
        SettingsHint {
            text: qsTr("Finish a line, a circle, an ellipse, a rectangle or a triangle with the pen or the "
                       + "highlighter and keep the pen still on the screen for a moment: the stroke becomes "
                       + "that shape, straight and neat. Undo brings the stroke back as you drew it.")
        }
        SettingsSwitchRow {
            objectName: "scratchOutSwitch"
            key: "scratchOut"; text: qsTr("Scratch out to erase")
        }
        SettingsHint {
            text: qsTr("Scribble quickly back and forth over handwriting (at least four strokes across, "
                       + "like crossing out on paper): what the zigzag covers is erased, and the zigzag "
                       + "goes too. Undo brings it back. A zigzag over nothing stays a stroke.")
        }
        SettingsSectionTitle { text: qsTr("Laser pointer") }
        SettingsSliderRow {
            objectName: "laserFadeSlider"
            key: "laserPointerFadeOutTime"; text: qsTr("The ink fades after")
            from: 0; to: 10000; stepSize: 100
            factor: 0.001; decimals: 1; suffix: " s"
        }
        SettingsHint {
            text: qsTr("The laser pointer and the laser highlighter (in the list of the pen button, and "
                       + "in the tools of full screen and presenting) draw ink that is never kept: it "
                       + "fades this long after the pen is lifted.")
        }
        SettingsSectionTitle { text: qsTr("Presenting") }
        SettingsSwitchRow { objectName: "presenterViewSwitch"; key: "presenterView"; text: qsTr("Presenter view on a second screen") }
        SettingsSwitchRow { objectName: "presenterSwapSwitch"; key: "presenterSwapScreens"; text: qsTr("Swap the screens") }
        SettingsSwitchRow { objectName: "presenterShowNotesSwitch"; key: "presenterShowNotes"; text: qsTr("The audience sees the space for notes too") }
        SettingsSwitchRow { objectName: "presenterFollowSwitch"; key: "presenterFollowView"; text: qsTr("The audience follows the zoom of the console") }
        SettingsHint {
            text: qsTr("With a second screen (a projector), presenting%1 shows only the slide there, "
                       + "and on this screen the page with its space for notes, the next page, the clock "
                       + "and the time since the start. Write on the page here: the audience sees it at "
                       + "once. The audience's screen is the one that is not the main screen; swap them "
                       + "when it is the other way round. With the space for notes shown too, the audience "
                       + "sees the whole page and what is written beside the slide. Zooming in on the console "
                       + "shows the audience the same part of the page, as large as their screen allows; "
                       + "a frame on the console shows what they see.").arg(sheet.win.keyNote("present"))
        }
        SettingsSectionTitle { text: qsTr("Grid") }
        SettingsSwitchRow { objectName: "snapGridSwitch"; key: "snapGrid"; text: qsTr("Snap to the grid") }
        SettingsHint {
            text: qsTr("Selections that are moved and the corners of shapes jump onto the nearest point "
                       + "of a half-centimetre grid when they come close to it (as in Xournal++). Hold Alt "
                       + "to do the opposite for a moment.")
        }
        SettingsSectionTitle { text: qsTr("Colors") }
        SettingsComboRow {
            objectName: "colorPaletteRow"
            text: qsTr("Color palette")
            options: app.colorPalettes.map(function(p) { return { text: p.name, value: p.id } })
            dependsOn: app.colorPalette
            getter: function() { return app.colorPalette }
            setter: function(id) { app.colorPalette = id }
        }
        Flow {  // the chosen palette's ink colors, with their roles' names
            objectName: "colorPalettePreview"
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: {
                    const all = app.colorPalettes
                    for (let i = 0; i < all.length; ++i) if (all[i].id === app.colorPalette) return all[i].roles
                    return []
                }
                delegate: Rectangle {
                    required property var modelData
                    width: 22; height: 22; radius: 11
                    color: modelData.ink
                    border.width: 1
                    border.color: "#9e9e9e"
                    ToolTip.visible: dotHover.hovered
                    ToolTip.text: modelData.name
                    ToolTip.delay: 400
                    HoverHandler { id: dotHover }
                }
            }
        }
        // Where the chosen palette comes from (its source and license)
        SettingsHint {
            objectName: "colorPaletteSource"
            readonly property string source: {
                const all = app.colorPalettes
                for (let i = 0; i < all.length; ++i) if (all[i].id === app.colorPalette) return all[i].source || ""
                return ""
            }
            visible: source !== ""
            text: qsTr("From: %1").arg(source)
        }
        SettingsHint {
            text: qsTr("A tool's editor (a tap on the tool in hand) offers the palette's colors and this "
                       + "choice too. Each color of a palette has a meaning (warnings, key terms, headings, "
                       + "…) that stays the same in every palette: a tool with a palette's color follows "
                       + "when another palette is chosen. With the highlighter the palettes give their "
                       + "highlight colors, half see-through on light paper and stronger on dark paper.")
        }
        Item { Layout.preferredHeight: 16 }
    }

    // Both bars back to their first layout: the rail and the top bar as at a first start; the user's tools stay (on the
    // rail, in their order, out of their groups)
    AdaptiveDialog {
        id: resetBarsDialog
        objectName: "resetBarsDialog"
        kind: "question"
        preferredWidth: 420
        title: qsTr("Back to the first layout?")
        Label {
            width: resetBarsDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("The rail and the top bar are arranged as at the first start: their order, groups and dividers. "
                       + "Your tools stay on the rail, with their colors and widths.")
        }
        footer: DialogButtonBox {
            Button { objectName: "resetBarsConfirm"; text: qsTr("Reset"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: app.toolbox.resetLayout()
    }
    // The toolbox back to its first tools (its order, colors and widths are lost)
    AdaptiveDialog {
        id: resetToolboxDialog
        objectName: "resetToolboxDialog"
        kind: "question"
        preferredWidth: 420
        title: qsTr("Back to the first tools?")
        Label {
            width: resetToolboxDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Your tools, their order and their settings are replaced by the toolbox of the first start.")
        }
        footer: DialogButtonBox {
            Button { objectName: "resetToolboxConfirm"; text: qsTr("Reset"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            app.toolbox.reset()
            app.applyToolEntry(app.toolbox.active)
        }
    }
}
