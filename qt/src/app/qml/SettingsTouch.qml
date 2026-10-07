// xournal-qt: Settings → Touch: drawing with the finger, the touch screen and the stylus's buttons.
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
        SettingsSectionTitle { text: qsTr("Palm rejection") }
        SettingsHint {
            text: qsTr("Touch is ignored while the pen is near the screen, so a hand resting on it while "
                       + "writing does nothing (it stays ignored until it is lifted). Once the pen is away, "
                       + "touch works again after:")
        }
        SettingsSliderRow {
            key: "palmRejectionTimeout"; text: qsTr("Touch waits after the pen")
            from: 0; to: 2000; stepSize: 50; decimals: 2; factor: 0.001; suffix: " s"
        }
        SettingsHint {
            text: qsTr("For a pen that never tells when it is near, the time counts from the last moment "
                       + "it was used.")
        }
        // Pens that tell how high they are: from which height on the pen counts as away
        SettingsSliderRow {
            objectName: "palmNearHeightRow"
            visible: app.penHover.reportsHeight
            key: "palmNearHeight"; text: qsTr("The pen counts as near up to")
            from: 5; to: 100; stepSize: 1; decimals: 0; suffix: " %"
        }
        GridLayout {
            visible: app.penHover.reportsHeight
            columns: sheet.narrow ? 1 : 2
            columnSpacing: 12
            Label {
                Layout.preferredWidth: sheet.narrow ? -1 : 220
                text: app.penHover.inProximity
                      ? qsTr("Your pen is at %1 % now").arg(Math.round(app.penHover.height * 100))
                      : qsTr("Your pen is away")
            }
            Button {
                objectName: "takePenHeight"
                text: takeHeight.running ? qsTr("Hold the pen there … %1").arg(takeHeight.left)
                                         : qsTr("Take the pen's height")
                enabled: !takeHeight.running
                onClicked: { takeHeight.left = 3; takeHeight.start() }
            }
            // Three seconds to lift the pen to the height that should count as away
            Timer {
                id: takeHeight
                property int left: 3
                interval: 1000
                repeat: true
                onTriggered: {
                    if (--left > 0) return
                    stop()
                    if (app.penHover.inProximity)
                        sheet.s.set("palmNearHeight", Math.max(5, Math.round(app.penHover.height * 100)))
                }
            }
        }
        SettingsHint {
            text: app.penHover.reportsHeight
                  ? qsTr("100 %: as far up as the pen is noticed at all. Lower: above that height it "
                         + "counts as away, so a finger can scroll right after writing while the pen stays "
                         + "close. \"Take the pen's height\": tap it, then hold the pen where it should "
                         + "start to count as away.")
                  : qsTr("Hover the pen over this page: if it tells how high it is above the screen, you can "
                         + "also choose here from which height on it counts as away.")
        }
        SettingsSectionTitle { text: qsTr("Gestures") }
        SettingsSwitchRow {
            objectName: "touchDrawingSwitch"
            key: "touchDrawing"
            text: qsTr("Draw with the finger")
        }
        SettingsHint {
            text: qsTr("One finger draws with the current tool (the hand still scrolls), two fingers scroll "
                       + "and zoom. The same as the finger button in the tool bar. The mouse always draws "
                       + "with its left button.")
        }
        SettingsSwitchRow {
            objectName: "handWhenOpeningSwitch"
            key: "handWhenOpening"
            text: qsTr("Open documents with the hand")
        }
        SettingsHint {
            text: qsTr("A document opens with the hand tool, so one finger scrolls. Choose the pen (or "
                       + "another tool) to write.")
        }
        SettingsComboRow {
            objectName: "touchProfileRow"
            key: "touchProfile"
            text: qsTr("Buttons sized for fingers")
            options: [{ text: qsTr("When a finger is used"), value: "auto" },
                      { text: qsTr("Always"), value: "on" },
                      { text: qsTr("Never"), value: "off" }]
        }
        SettingsHint {
            text: qsTr("Some buttons get bigger when the screen is touched with a finger, and smaller again "
                       + "when the mouse is used. The pen changes nothing.")
        }
        SettingsSwitchRow { key: "zoomGestures"; text: qsTr("Pinch with two fingers to zoom") }
        SettingsHint {
            text: qsTr("One finger scrolls (unless it draws), two fingers pan and zoom. Tap with two "
                       + "fingers to undo, with three fingers to redo.")
        }
        SettingsSwitchRow {
            objectName: "rotateGestureRow"
            key: "rotateGesture"
            text: qsTr("Turn the canvas with two fingers")
        }
        SettingsHint {
            text: qsTr("Twist two fingers (or turn them on the touchpad) to turn the canvas, not the pages. "
                       + "It snaps to quarter turns. Two taps on the page, a fit or the chip beside the "
                       + "page number turn it upright again; Ctrl+] and Ctrl+[ turn it by a quarter.")
        }
        Item { Layout.preferredHeight: 16 }
    }
}
