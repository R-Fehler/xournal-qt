// xournal-qt: Settings → Help: the introduction, the tutorial and the keyboard shortcuts
// (qt/docs/features/onboarding.md).
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
        SettingsSectionTitle { text: qsTr("Getting started") }
        SettingsHint {
            text: qsTr("A few pages about what the app is for: notes and PDFs you write on, Markdown, "
                       + "libraries and search, and how documents are kept.")
        }
        Button {
            objectName: "showIntroButton"
            text: qsTr("Show the introduction")
            onClicked: { sheet.close(); sheet.introRequested() }
        }
        SettingsSectionTitle { text: qsTr("Tutorial") }
        SettingsHint {
            text: qsTr("A document that asks you to try the tools, pages, search, tabs, Markdown and the "
                       + "library, one page each. It is your copy to write on, kept in the app's own "
                       + "folder, not in your library.")
        }
        RowLayout {
            spacing: 8
            Button {
                objectName: "openTutorialButton"
                text: qsTr("Open the tutorial")
                onClicked: { sheet.close(); sheet.tutorialRequested() }
            }
            Button {
                objectName: "restartTutorialButton"
                visible: app.tutorialExists
                flat: true
                text: qsTr("Start it again…")
                onClicked: { sheet.close(); sheet.restartTutorialRequested() }
            }
        }
        SettingsSectionTitle { text: qsTr("Keyboard shortcuts") }
        SettingsHint {
            readonly property var keys: sheet.win.keysOf("shortcuts")
            text: keys.length > 0 ? qsTr("%1 shows them over the page; Shortcuts here changes them.").arg(keys[0])
                                  : qsTr("Shortcuts here changes them.")
        }
        Button {
            objectName: "helpShortcutsButton"
            text: qsTr("Show the shortcuts")
            onClicked: sheet.showShortcuts()
        }
    }
}
