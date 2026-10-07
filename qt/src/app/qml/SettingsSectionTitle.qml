// xournal-qt: the title of a group of settings.
// A row of the settings' sections (SettingsPage.qml): it reads `sheet.s` (app.settings) and `sheet.narrow`
// through the sheet's context.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Label {
    Layout.topMargin: 18
    Layout.fillWidth: true
    font.pixelSize: 15
    font.weight: Font.DemiBold
    color: Material.accentColor
}
