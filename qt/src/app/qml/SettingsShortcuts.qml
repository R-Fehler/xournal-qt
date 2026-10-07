// xournal-qt: Settings → Shortcuts: the keys of every action (ShortcutsModel), and the
// dialog that catches new ones.
// Part of SettingsPage.qml, instantiated once there: it reads the sheet through `sheet` (SettingsPage.qml's
// context: `sheet.s` is app.settings, `sheet.narrow`, `sheet.win`); its rows are Settings*Row.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    spacing: 0
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 16
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Tap a shortcut and press the keys. Backspace removes it, Esc keeps it as it is.")
            color: "#5f6368"
        }
        Button {
            objectName: "resetShortcutsButton"
            text: qsTr("Default keys")
            onClicked: app.shortcuts.resetAll()
        }
    }
    ListView {
        objectName: "shortcutList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: 16
        clip: true
        model: app.shortcuts
        spacing: 2
        ScrollBar.vertical: ScrollBar {}
        section.property: "group"
        section.delegate: Label {
            required property string section
            text: section
            font.weight: Font.DemiBold
            color: Material.accentColor
            topPadding: 10
            bottomPadding: 4
        }
        delegate: ItemDelegate {
            id: shortcutRow
            required property int index
            required property string actionId
            required property string name
            required property string keys
            required property bool isDefault
            required property string conflict
            width: ListView.view.width
            height: 44
            onClicked: { capture.row = shortcutRow.index; capture.actionId = shortcutRow.actionId; capture.open() }
            contentItem: RowLayout {
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: shortcutRow.name; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        visible: shortcutRow.conflict !== ""
                        text: qsTr("also used by “%1”").arg(shortcutRow.conflict)
                        color: "#c5221f"
                        font.pixelSize: 11
                    }
                }
                Rectangle {
                    radius: 5
                    color: shortcutRow.isDefault ? "#f1f3f4" : "#e0e3f5"
                    border.width: 1
                    border.color: shortcutRow.conflict !== "" ? "#c5221f" : "#d5d8dc"
                    implicitWidth: rowKeys.implicitWidth + 14
                    implicitHeight: 28
                    Label {
                        id: rowKeys
                        anchors.centerIn: parent
                        text: shortcutRow.keys === "" ? qsTr("none") : shortcutRow.keys
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // Catches the keys for one shortcut
    AdaptiveDialog {
        id: capture
        objectName: "shortcutCapture"
        kind: "card"
        property int row: -1
        property string actionId: ""
        preferredWidth: 360
        title: qsTr("Press the keys")
        standardButtons: Dialog.Cancel
        onOpened: catcher.forceActiveFocus()
        ColumnLayout {
            width: capture.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Press the key combination for this action. Backspace removes the shortcut.")
            }
            Item {
                id: catcher
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                focus: true
                Keys.onPressed: function(event) {
                    event.accepted = true
                    if (event.key === Qt.Key_Escape) { capture.close(); return }
                    if (event.key === Qt.Key_Backspace) { app.shortcuts.setKeys(capture.actionId, ""); capture.close(); return }
                    if ([Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].indexOf(event.key) >= 0) return
                    const combination = event.key | (event.modifiers & ~Qt.KeypadModifier)
                    app.shortcuts.setKeys(capture.actionId, capture.textOf(combination))
                    capture.close()
                }
            }
        }
        function textOf(combination) { return app.shortcuts.keyText(combination) }
    }
}
