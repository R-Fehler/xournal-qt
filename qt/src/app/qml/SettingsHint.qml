// xournal-qt: a line of explanation under a setting.
// A row of the settings' sections (SettingsPage.qml): it reads `sheet.s` (app.settings) and `sheet.narrow`
// through the sheet's context.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Label {
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    color: "#6b6f75"
    font.pixelSize: 13
}
