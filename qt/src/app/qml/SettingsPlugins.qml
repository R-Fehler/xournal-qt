// xournal-qt: Settings → Plugins (qt/docs/features/plugins.md): every plugin found (bundled and the user's), on or off,
// what it was allowed (each permission: allowed, not allowed, or asked the next time it is needed), its log, and
// "Reload" for plugin authors (the folders read again, every plugin's engine made anew).
// Part of SettingsPage.qml, instantiated once there.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ScrollView {
    id: section
    objectName: "settingsPlugins"
    contentWidth: availableWidth
    clip: true
    readonly property var plugins: app.plugins
    /// The plugin whose log is shown ("": none)
    property string logOf: ""

    ColumnLayout {
        width: section.availableWidth
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 0
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#5f6368"
                text: qsTr("Plugins add commands (in ⋮ → Plugins, the toolbox's +, and on keys). They can only do what you allow them when they first need it. Your own plugins go into %1.")
                        .arg(section.plugins.userFolder)
            }
            Button {
                objectName: "reloadPluginsButton"
                text: qsTr("Reload")
                flat: true
                onClicked: section.plugins.reload()
            }
        }
        Label {
            visible: section.plugins.plugins.length === 0
            Layout.leftMargin: 16
            text: qsTr("No plugins found.")
            color: "#5f6368"
        }
        Repeater {
            model: section.plugins.plugins
            delegate: Pane {
                id: card
                required property var modelData
                required property int index
                objectName: "pluginCard_" + modelData.id
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                padding: 12
                background: Rectangle { color: "#ffffff"; radius: 10; border.width: 1; border.color: "#e0e0e0" }
                ColumnLayout {
                    width: parent.width
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: card.modelData.name + (card.modelData.version ? "  " + card.modelData.version : "")
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: [card.modelData.bundled ? qsTr("Comes with the app") : qsTr("Your plugin"),
                                       card.modelData.author].filter(function(t) { return t }).join(" · ")
                                color: "#80868b"
                                font.pixelSize: 12
                            }
                        }
                        Switch {
                            objectName: "pluginEnabled_" + card.modelData.id
                            checked: card.modelData.enabled
                            enabled: card.modelData.error === ""
                            onToggled: section.plugins.setEnabled(card.modelData.id, checked)
                        }
                    }
                    Label {
                        visible: text !== ""
                        text: card.modelData.description
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "pluginError_" + card.modelData.id
                        visible: card.modelData.error !== ""
                        text: card.modelData.error
                        color: "#c5221f"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: card.modelData.commands.length > 0
                        text: qsTr("Commands: %1").arg(card.modelData.commands.join(", "))
                        color: "#5f6368"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    // What it may do: each permission it can ask for, and the answer
                    Repeater {
                        model: card.modelData.permissions
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: modelData.text
                                wrapMode: Text.Wrap
                                font.pixelSize: 13
                            }
                            ComboBox {
                                objectName: "pluginGrant_" + card.modelData.id + "_" + modelData["class"]
                                Layout.preferredWidth: 170
                                model: [qsTr("Ask first"), qsTr("Allowed"), qsTr("Not allowed")]
                                currentIndex: modelData.answer === "allow" ? 1 : modelData.answer === "deny" ? 2 : 0
                                onActivated: function(i) {
                                    section.plugins.setGrant(card.modelData.id, modelData["class"], ["", "allow", "deny"][i])
                                }
                            }
                        }
                    }
                    RowLayout {
                        Button {
                            objectName: "pluginLogButton_" + card.modelData.id
                            text: section.logOf === card.modelData.id ? qsTr("Hide the log") : qsTr("Log")
                            flat: true
                            onClicked: section.logOf = section.logOf === card.modelData.id ? "" : card.modelData.id
                        }
                        Label {
                            text: card.modelData.folder
                            color: "#80868b"
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    TextArea {
                        objectName: "pluginLog_" + card.modelData.id
                        visible: section.logOf === card.modelData.id
                        Layout.fillWidth: true
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        font.family: "Monospace"
                        font.pixelSize: 11
                        text: visible ? (section.plugins.logRevision, section.plugins.logText(card.modelData.id)) || qsTr("(empty)") : ""
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
