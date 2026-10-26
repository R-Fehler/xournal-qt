// xournal-qt: the width of the eraser a button of the pen gives (the eraser end, a side button): "Own width" (off:
// the tool bar eraser's, as before) and, on, its width in points on a log slider, shown in mm
// (qt/docs/features/pen-buttons.md).
// A row of the settings' sections (SettingsPage.qml): it reads `sheet.s` (app.settings) and `sheet.narrow`
// through the sheet's context.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: row
    /// The setting of the switch ("<what>Width" in the xournalQt part: points; 0: the tool bar eraser's)
    property string key
    property alias text: label.text
    Layout.fillWidth: true
    Layout.leftMargin: 16
    spacing: 0
    readonly property bool own: (sheet.s.revision, sheet.s.get(row.key) > 0)
    // The eraser's editor in the toolbox: 0.1 to 150 pt on a log scale (slider 0 … 1), shown in mm
    function mm(w) {
        const v = w * 25.4 / 72
        return (v < 1 ? v.toFixed(2) : v < 10 ? v.toFixed(1) : Math.round(v)) + " mm"
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            id: label
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            // (the button does not erase: nothing to size)
            enabled: row.enabled
        }
        Switch {
            objectName: row.key + "Switch"
            checked: row.own
            enabled: row.enabled
            onToggled: sheet.s.set(row.key, checked ? sheet.s.defaultButtonWidth() : 0)
        }
    }
    GridLayout {
        Layout.fillWidth: true
        visible: row.own
        enabled: row.enabled
        columns: 2
        rowSpacing: 0
        Slider {
            objectName: row.key + "Slider"
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            from: 0; to: 1; stepSize: 0.001
            snapMode: Slider.SnapAlways
            value: (sheet.s.revision, Math.log(Math.max(0.1, sheet.s.get(row.key)) / 0.1) / Math.log(1500))
            onMoved: sheet.s.set(row.key, Math.round(0.1 * Math.pow(1500, value) * 100) / 100)
        }
        Label {
            objectName: row.key + "Value"
            Layout.preferredWidth: 72
            horizontalAlignment: Text.AlignRight
            text: (sheet.s.revision, row.mm(sheet.s.get(row.key)))
        }
    }
}
