// xournal-qt: a setting with a value in a range: its label, a slider and the value
// (narrow: the label above the slider).
// A row of the settings' sections (SettingsPage.qml): it reads `sheet.s` (app.settings) and `sheet.narrow`
// through the sheet's context.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

GridLayout {
    id: row
    property string key
    property alias text: label.text
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property int decimals: 2
    property string suffix: ""
    property real factor: 1  // shown value = stored value * factor
    Layout.fillWidth: true
    // (narrow: the label above the slider and its value)
    columns: sheet.narrow ? 2 : 3
    rowSpacing: 0
    Label {
        id: label
        Layout.columnSpan: sheet.narrow ? 2 : 1
        Layout.fillWidth: sheet.narrow
        Layout.preferredWidth: sheet.narrow ? -1 : 220
        wrapMode: Text.WordWrap
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        Layout.minimumWidth: 120
        from: row.from; to: row.to; stepSize: row.stepSize
        snapMode: Slider.SnapAlways
        value: (sheet.s.revision, sheet.s.get(row.key))
        onMoved: sheet.s.set(row.key, value)
    }
    Label {
        Layout.preferredWidth: 72
        horizontalAlignment: Text.AlignRight
        text: (slider.value * row.factor).toFixed(row.decimals) + row.suffix
    }
}
