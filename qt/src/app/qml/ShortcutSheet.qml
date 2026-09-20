// All keyboard shortcuts, by group (F1). "Change…" leads to the settings, where they can be given other keys.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    objectName: "shortcutSheet"
    parent: Overlay.overlay
    modal: true
    focus: true
    x: Math.round((parent ? parent.width : 800) / 2 - width / 2)
    y: Math.round((parent ? parent.height : 600) / 2 - height / 2)
    width: Math.min(parent ? parent.width * 0.92 : 900, 900)
    height: Math.min(parent ? parent.height * 0.88 : 700, 760)
    padding: 0
    background: Rectangle { color: "#ffffff"; radius: 12; border.width: 1; border.color: "#d5d8dc" }

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 20; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "shortcutSettingsButton"
                text: qsTr("Change…")
                onClicked: { sheet.close(); sheet.changeRequested() }
            }
            IconButton { iconName: "xqt-close"; tip: qsTr("Close (Esc)"); onClicked: sheet.close() }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 16
            contentHeight: content.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar {}

            Grid {
                id: content
                objectName: "shortcutGroups"
                width: parent.width
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
        }
    }
}
