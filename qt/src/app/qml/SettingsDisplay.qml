// xournal-qt: Settings → Display: the screen's calibration (100 % is the size of the paper),
// zoom, dark pages, the window size and the layout.
// Part of SettingsPage.qml, instantiated once there: it reads the sheet through `sheet` (SettingsPage.qml's
// context: `sheet.s` is app.settings, `sheet.narrow`, `sheet.win`); its rows are Settings*Row.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    id: displayPage
    contentWidth: availableWidth
    // The screen this window is on, read again when the window moves to another screen or its scaling
    // changes. DPIs in logical pixels per inch (what the ruler is drawn in).
    readonly property var info: (sheet.s.revision, Screen.name, Screen.devicePixelRatio,
                                 sheet.s.screenCalibration(Window.window))
    /// What the ruler shows now (saved with "Save for this screen")
    property real dpi: 96
    readonly property real minDpi: 40
    readonly property real maxDpi: 400
    onInfoChanged: dpi = info.dpi
    Component.onCompleted: dpi = info.dpi
    function setDpi(value) { dpi = Math.max(minDpi, Math.min(maxDpi, value)) }

    ColumnLayout {
        width: parent.width - 48
        x: 24
        spacing: 10
        SettingsSectionTitle { text: qsTr("Real size") }
        SettingsHint {
            text: qsTr("Hold a ruler against the screen and move the slider, or drag the ruler on the "
                       + "screen, until its marks match the real ones. Pages at 100 % (Ctrl+1) are then "
                       + "as large as the paper, and the set square and compass measure real "
                       + "centimetres. Each screen keeps its own.")
        }
        Label {
            objectName: "calibrationScreen"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("This screen: %1, scaled %2 %.").arg(displayPage.info.name || qsTr("unnamed"))
                                                      .arg(Math.round(displayPage.info.dpr * 100))
                  + " "
                  + (displayPage.info.reportedPlausible
                     ? qsTr("It says it has %1 dpi.").arg(displayPage.info.reportedDpi.toFixed(1))
                     : qsTr("It does not tell its size: 96 dpi until it is calibrated."))
        }
        // The ruler: centimetres above, inches below, from the 0 mark on the left. A drag stretches it
        // (the 0 mark stays), with the mouse or a finger.
        Canvas {
            id: ruler
            objectName: "calibrationRuler"
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            readonly property real zeroX: 16
            readonly property real dpi: displayPage.dpi
            readonly property real pixelsPerCm: dpi / 2.54
            onDpiChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#fff8d6"
                ctx.fillRect(0, 0, width, height)
                ctx.strokeStyle = "#202124"
                ctx.fillStyle = "#202124"
                ctx.lineWidth = 1
                ctx.font = "12px sans-serif"
                ctx.beginPath()
                // centimetres, in millimetres
                const mm = pixelsPerCm / 10
                for (let i = 0; zeroX + i * mm <= width - 2; ++i) {
                    const x = zeroX + i * mm
                    const h = i % 10 === 0 ? 30 : (i % 5 === 0 ? 20 : 11)
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x, h)
                    if (i % 10 === 0)
                        ctx.fillText(String(i / 10) + (i === 0 ? " cm" : ""), x + 3, 44)
                }
                // inches, in eighths
                const eighth = dpi / 8
                for (let j = 0; zeroX + j * eighth <= width - 2; ++j) {
                    const x = zeroX + j * eighth
                    const h = j % 8 === 0 ? 30 : (j % 4 === 0 ? 22 : (j % 2 === 0 ? 15 : 9))
                    ctx.moveTo(x, height)
                    ctx.lineTo(x, height - h)
                    if (j % 8 === 0)
                        ctx.fillText(String(j / 8) + (j === 0 ? " in" : ""), x + 3, height - 34)
                }
                ctx.stroke()
            }
            MouseArea {
                objectName: "calibrationRulerDrag"
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                property real startX: 0
                property real startDpi: 0
                onPressed: function(mouse) {
                    startX = mouse.x - ruler.zeroX
                    startDpi = displayPage.dpi
                }
                onPositionChanged: function(mouse) {
                    if (startX > 24)  // (too close to the 0 mark to stretch from)
                        displayPage.setDpi(startDpi * (mouse.x - ruler.zeroX) / startX)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                text: "−"
                font.pixelSize: 22
                implicitWidth: 44
                ToolTip.visible: hovered
                ToolTip.text: qsTr("A little shorter")
                ToolTip.delay: 600
                autoRepeat: true
                onClicked: displayPage.setDpi(displayPage.dpi - 0.1)
            }
            Slider {
                objectName: "calibrationSlider"
                Layout.fillWidth: true
                from: displayPage.minDpi; to: displayPage.maxDpi; stepSize: 0.1
                value: displayPage.dpi
                onMoved: displayPage.setDpi(value)
            }
            ToolButton {
                text: "+"
                font.pixelSize: 22
                implicitWidth: 44
                ToolTip.visible: hovered
                ToolTip.text: qsTr("A little longer")
                ToolTip.delay: 600
                autoRepeat: true
                onClicked: displayPage.setDpi(displayPage.dpi + 0.1)
            }
            Label {
                objectName: "calibrationValue"
                Layout.preferredWidth: 90
                horizontalAlignment: Text.AlignRight
                text: qsTr("%1 dpi").arg(displayPage.dpi.toFixed(1))
            }
        }
        // (a Flow: on a phone the two buttons go on two lines)
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button {
                objectName: "calibrationSave"
                text: qsTr("Save for this screen")
                highlighted: true
                enabled: displayPage.info.key !== ""
                         && (!displayPage.info.calibrated
                             || Math.abs(displayPage.dpi - displayPage.info.dpi) > 0.05)
                onClicked: sheet.s.calibrateScreen(Window.window, displayPage.dpi)
            }
            Button {
                objectName: "calibrationReset"
                text: qsTr("Back to what the screen says")
                flat: true
                enabled: displayPage.info.calibrated
                         || Math.abs(displayPage.dpi - displayPage.info.defaultDpi) > 0.05
                onClicked: {
                    sheet.s.resetScreenCalibration(Window.window)
                    displayPage.dpi = displayPage.info.defaultDpi
                }
            }
        }
        SettingsHint {
            objectName: "calibrationState"
            text: displayPage.info.calibrated
                  ? qsTr("Calibrated: 100 % is the real size on this screen. Another screen keeps its "
                         + "own calibration; the window takes it when it is moved there.")
                  : qsTr("Not calibrated yet: 100 % follows what the screen says.")
        }

        // How far out the pages zoom (ViewController::minZoom)
        SettingsSectionTitle { text: qsTr("Zoom") }
        SettingsSliderRow {
            objectName: "smallestZoomRow"
            key: "smallestZoom"
            text: qsTr("Smallest zoom")
            from: 5; to: 50; stepSize: 5
            decimals: 0
            suffix: " %"
        }
        SettingsHint {
            text: qsTr("How far pinching, Ctrl+wheel and Ctrl+minus zoom out. A page, or a row of pages "
                       + "side by side, that would not fit whole at this zoom can always be zoomed out "
                       + "until it does.")
        }

        // The layout for the window's size (qt/docs/adaptive-layout.md)
        SettingsSectionTitle { text: qsTr("Window size") }
        SettingsSwitchRow {
            objectName: "adaptiveLayoutSwitch"
            key: "adaptiveLayout"
            text: qsTr("Adapt the layout to the window size")
        }
        SettingsHint {
            objectName: "sizeClassHint"
            readonly property var names: ({
                desktopWide: qsTr("a wide desktop window"),
                desktopNarrow: qsTr("a narrow desktop window or a tablet in landscape"),
                tabletPortrait: qsTr("a tablet in portrait"),
                phonePortrait: qsTr("a phone in portrait"),
                phoneShort: qsTr("a phone in landscape or a low window"),
                tiny: qsTr("a tiny window")
            })
            readonly property var adaptive: win.adaptive
            text: qsTr("This window is %1 (%2 × %3).").arg(names[adaptive.sizeClass])
                                                .arg(Math.round(adaptive.classWidth))
                                                .arg(Math.round(adaptive.classHeight))
                  + " " + ((sheet.s.revision, sheet.s.get("adaptiveLayout"))
                             ? qsTr("The page sidebar shows beside the page when there is room for it. "
                                    + "What you show or hide by hand is kept for each kind of window.")
                             : qsTr("The window keeps the desktop layout at every size."))
        }
        Button {
            objectName: "resetLayoutButton"
            text: qsTr("Reset the layout choices")
            flat: true
            enabled: (sheet.s.revision, sheet.s.hasLayoutChoices())
            onClicked: sheet.s.resetLayoutChoices()
        }
        Item { Layout.preferredHeight: 16 }
    }
}
