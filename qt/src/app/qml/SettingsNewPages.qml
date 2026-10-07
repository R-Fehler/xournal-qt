// xournal-qt: Settings → New pages: the paper of new documents and pages.
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
        SettingsSectionTitle { text: qsTr("New pages") }
        SettingsSwitchRow {
            key: "copyLastPageSettings"
            text: qsTr("Use the background of the current page")
        }
        SettingsSwitchRow {
            key: "copyLastPageSize"
            text: qsTr("Use the size of the current page")
        }
        SettingsComboRow {
            key: "pageBackground"; text: qsTr("Background")
            enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSettings"))
            options: sheet.s.pageBackgrounds.map(function(name, i) { return { text: name, value: i } })
        }
        SettingsComboRow {
            key: "paperFormat"; text: qsTr("Paper size")
            enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSize"))
            // (a size that is none of them, set in Xournal++: shown as it is)
            options: sheet.s.paperFormats.map(function(name, i) { return { text: name, value: i } })
                     .concat((sheet.s.revision, sheet.s.get("paperFormat")) < 0
                             ? [{ text: qsTr("Other: %1").arg(sheet.s.templatePaperSize()), value: -1 }] : [])
        }
        SettingsSwitchRow {
            key: "landscape"; text: qsTr("Landscape")
            enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSize"))
        }
        // The paper of new pages (qt/docs/features/dark-pages.md, "Page colors")
        Label { text: qsTr("Paper"); Layout.fillWidth: true }
        PaperSwatches {
            Layout.fillWidth: true
            enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSettings"))
            paper: (sheet.s.revision, sheet.s.get("pageColor"))
            textured: (sheet.s.revision, sheet.s.get("pageTexture")) === true
            onChosen: function(c) { sheet.s.set("pageColor", c) }
            onTexturedToggled: function(on) { sheet.s.set("pageTexture", on) }
        }
        Item { Layout.preferredHeight: 16 }
    }
}
