// xournal-qt: a setting that is on or off: its label and a switch (`key`: the settings key).
// A row of the settings' sections (SettingsPage.qml): it reads `sheet.s` (app.settings) and `sheet.narrow`
// through the sheet's context.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

RowLayout {
    id: row
    property string key
    property alias text: label.text
    Layout.fillWidth: true
    Label { id: label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Switch {
        checked: (sheet.s.revision, sheet.s.get(row.key))
        onToggled: sheet.s.set(row.key, checked)
    }
}
