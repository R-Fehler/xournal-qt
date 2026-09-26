// All keyboard shortcuts, by group (F1). "Change…" leads to the settings, where they can be given other keys.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: sheet
    objectName: "shortcutSheet"
    preferredWidth: 900
    closeButton: true
    title: qsTr("Keyboard shortcuts")

    signal changeRequested()

    readonly property var groups: {
        const list = app.shortcuts.sheet()
        const byGroup = []
        for (const entry of list) {
            let group = byGroup.find(g => g.name === entry.group)
            if (!group) {
                group = { name: entry.group, entries: [] }
                byGroup.push(group)
            }
            group.entries.push(entry)
        }
        return byGroup
    }

    Grid {
        id: content
        objectName: "shortcutGroups"
        width: sheet.availableWidth
        columns: width > 620 ? 2 : 1
        columnSpacing: 24
        rowSpacing: 18
        Repeater {
            model: sheet.groups
            delegate: ColumnLayout {
                required property var modelData
                width: (content.width - (content.columns - 1) * content.columnSpacing) / content.columns
                spacing: 2
                Label {
                    text: modelData.name
                    font.weight: Font.DemiBold
                    color: Material.accentColor
                    bottomPadding: 4
                }
                Repeater {
                    model: modelData.entries
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: modelData.name; elide: Text.ElideRight; Layout.fillWidth: true }
                        Rectangle {
                            radius: 5
                            color: "#f1f3f4"
                            border.width: 1
                            border.color: "#d5d8dc"
                            implicitWidth: keyLabel.implicitWidth + 14
                            implicitHeight: keyLabel.implicitHeight + 8
                            Label {
                                id: keyLabel
                                anchors.centerIn: parent
                                text: modelData.keys
                                font.family: "monospace"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "shortcutSettingsButton"
            text: qsTr("Change…")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: { sheet.close(); sheet.changeRequested() }
        }
    }
}
