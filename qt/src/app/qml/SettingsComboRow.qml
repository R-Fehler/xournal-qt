// xournal-qt: a setting with a choice: its label and a box of `options` ({text, value});
// a key, or a getter and a setter.
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
    property var options: []    // [{ text, value }]; value may be a string or an index
    property var dependsOn      // another value its setting follows (read again when it changes)
    // Instead of a key: where the value is read and written (functions)
    property var getter: null
    property var setter: null
    Layout.fillWidth: true
    // (narrow: the label above the box)
    columns: sheet.narrow ? 1 : 2
    rowSpacing: 0
    Label { id: label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    ComboBox {
        Layout.fillWidth: sheet.narrow
        Layout.preferredWidth: sheet.narrow ? -1 : 260
        model: row.options
        textRole: "text"
        valueRole: "value"
        // count: re-evaluate once the model is there
        currentIndex: (sheet.s.revision, row.dependsOn, count,
                       indexOfValue(row.getter ? row.getter() : sheet.s.get(row.key)))
        onActivated: row.setter ? row.setter(currentValue) : sheet.s.set(row.key, currentValue)
    }
}
