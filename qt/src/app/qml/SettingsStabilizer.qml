// xournal-qt: Settings → Stabilizer: smoothing of the strokes.
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
        SettingsSectionTitle { text: qsTr("Stroke stabilizer") }
        SettingsHint { text: qsTr("Smooths strokes while writing, as in Xournal++.") }
        SettingsComboRow {
            key: "stabilizerAveraging"; text: qsTr("Averaging")
            options: [
                { text: qsTr("None"), value: 0 },
                { text: qsTr("Arithmetic mean"), value: 1 },
                { text: qsTr("Velocity based Gaussian weights"), value: 2 }
            ]
        }
        SettingsSliderRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerAveraging")) !== 0
            key: "stabilizerBuffersize"; text: qsTr("Buffer size")
            from: 2; to: 100; stepSize: 1; decimals: 0
        }
        SettingsSliderRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerAveraging")) === 2
            key: "stabilizerSigma"; text: qsTr("Sigma")
            from: 0.05; to: 5; stepSize: 0.05
        }
        SettingsComboRow {
            key: "stabilizerPreprocessor"; text: qsTr("Preprocessor")
            options: [
                { text: qsTr("None"), value: 0 },
                { text: qsTr("Deadzone"), value: 1 },
                { text: qsTr("Inertia"), value: 2 }
            ]
        }
        SettingsSliderRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 1
            key: "stabilizerDeadzoneRadius"; text: qsTr("Deadzone radius")
            from: 0.1; to: 50; stepSize: 0.1; decimals: 1
        }
        SettingsSwitchRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 1
            key: "stabilizerCuspDetection"; text: qsTr("Cusp detection")
        }
        SettingsSliderRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 2
            key: "stabilizerDrag"; text: qsTr("Drag")
            from: 0; to: 1; stepSize: 0.05
        }
        SettingsSliderRow {
            visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 2
            key: "stabilizerMass"; text: qsTr("Mass")
            from: 1; to: 30; stepSize: 0.1; decimals: 1
        }
        SettingsSwitchRow {
            key: "stabilizerFinalizeStroke"
            text: qsTr("Finish the stroke at the pen position (no gap at the end)")
        }
        Item { Layout.preferredHeight: 16 }
    }
}
