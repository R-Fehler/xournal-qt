// xournal-qt: Settings → Search: the fuzzy search and the handwriting search.
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
        GridLayout {
            Layout.fillWidth: true
            columns: sheet.narrow ? 1 : 2
            rowSpacing: 0
            SettingsSectionTitle { text: qsTr("Fuzzy search") }
            Button {
                objectName: "fuzzyHelpButton"
                Layout.topMargin: sheet.narrow ? 0 : 18
                flat: true
                text: qsTr("Help: syntax and examples")
                onClicked: fuzzyHelp.open()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Fuzzy search in the library and the tab overview (the \"Fuzzy\" button "
                           + "next to their search fields)")
            }
            Switch {
                objectName: "fuzzySearchSwitch"
                checked: app.library.fuzzySearch
                onToggled: app.library.fuzzySearch = checked
            }
        }
        SettingsHint {
            text: qsTr("Names match when their letters come in this order (like fzf). In the text of "
                       + "the documents, a word of three or more letters matches when it has the letters "
                       + "close together (tbine finds \"turbine\"), or with a typo; the whole word is "
                       + "marked. Shorter words are found as they are typed.")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: sheet.narrow ? 1 : 2
            rowSpacing: 0
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Typo tolerance in text")
            }
            ComboBox {
                id: typos
                objectName: "fuzzyTyposCombo"
                Layout.fillWidth: sheet.narrow
                Layout.preferredWidth: sheet.narrow ? -1 : 360
                model: [
                    { text: qsTr("Off"), value: 0 },
                    { text: qsTr("1 letter, in words of 5+ letters"), value: 1 },
                    { text: qsTr("Up to 2 letters, in words of 8+ letters"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: (sheet.s.revision, count, indexOfValue(sheet.s.get("fuzzyTypos")))
                onActivated: sheet.s.set("fuzzyTypos", currentValue)
            }
        }
        SettingsHint {
            text: qsTr("A typo is one letter swapped with the next, left out, added or wrong: with 1, "
                       + "turbnie, trbine and turbime find \"turbine\". With 2, words of 8 or more "
                       + "letters may have two (trasnfromation finds \"transformation\"), shorter ones "
                       + "one. Documents with the word as typed come first.")
        }

        // --- Handwriting (qt/docs/features/handwriting-search.md) ---
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Search handwriting")
            }
            Switch {
                objectName: "handwritingSearchSwitch"
                checked: app.handwriting.enabled
                onToggled: app.handwriting.enabled = checked
            }
        }
        SettingsHint {
            text: qsTr("Handwritten words become searchable: in open documents, in the library, and (as "
                       + "invisible text) in PDFs with notes and archive PDFs that other PDF apps open. "
                       + "The handwriting is never turned into text. The recogniser runs on this computer "
                       + "at low priority; the library's other documents are read only on mains power.")
        }
        Label {
            objectName: "handwritingStatus"
            visible: app.handwriting.enabled
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: app.handwriting.status
            color: app.handwriting.ready ? "#1e7e34" : "#6b6f75"
        }
        // The languages read: a model each (one for both would serve both)
        RowLayout {
            visible: app.handwriting.enabled
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Handwriting languages")
            }
            ComboBox {
                objectName: "handwritingLanguagesCombo"
                Layout.fillWidth: sheet.narrow
                Layout.preferredWidth: sheet.narrow ? -1 : 260
                model: [
                    { text: qsTr("English"), value: "en" },
                    { text: qsTr("German"), value: "de" },
                    { text: qsTr("English and German"), value: "en+de" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: (count, indexOfValue(app.handwriting.languages))
                onActivated: app.handwriting.languages = currentValue
            }
        }
        SettingsHint {
            visible: app.handwriting.enabled
            text: qsTr("With both, a document's language is found from its first lines: the other "
                       + "model then reads only the lines the first one is unsure of. ⋮ → Document → "
                       + "Handwriting language sets it for one document.")
        }
        // Per language its model: downloaded only when the user asks, its address and size shown first
        Repeater {
            model: app.handwriting.enabled ? app.handwriting.models : []
            delegate: ColumnLayout {
                id: hwModel
                required property var modelData
                readonly property string lang: modelData.language
                objectName: "handwritingModel_" + lang
                visible: modelData.needed || modelData.installed
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("%1: %2").arg(hwModel.modelData.label).arg(hwModel.modelData.state)
                        font.bold: true
                    }
                    Button {
                        objectName: "handwritingChooseFolder_" + hwModel.lang
                        flat: true
                        text: hwModel.modelData.own ? qsTr("Choose a folder…") : qsTr("Use the app's own")
                        onClicked: {
                            if (hwModel.modelData.own) {
                                modelFolderDialog.language = hwModel.lang
                                modelFolderDialog.open()
                            } else {
                                app.handwriting.chooseFolder(hwModel.lang, "")
                            }
                        }
                    }
                }
                ColumnLayout {
                    objectName: "handwritingDownload_" + hwModel.lang
                    visible: hwModel.modelData.own && !hwModel.modelData.installed
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("The model (%1) is downloaded once from:").arg(hwModel.modelData.size)
                    }
                    TextEdit {
                        objectName: "handwritingModelSource_" + hwModel.lang
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        text: hwModel.modelData.source
                        font.pixelSize: 13
                        color: "#3c4043"
                    }
                    RowLayout {
                        Button {
                            objectName: "handwritingDownloadButton_" + hwModel.lang
                            text: qsTr("Download")
                            enabled: hwModel.modelData.downloadAvailable && !hwModel.modelData.downloading
                            onClicked: app.handwriting.download(hwModel.lang)
                        }
                        Button {
                            objectName: "handwritingCancelDownload_" + hwModel.lang
                            visible: hwModel.modelData.downloading
                            text: qsTr("Cancel")
                            onClicked: app.handwriting.cancelDownload(hwModel.lang)
                        }
                    }
                    ProgressBar {
                        visible: hwModel.modelData.downloading
                        Layout.fillWidth: true
                        value: hwModel.modelData.progress
                    }
                    Label {
                        visible: hwModel.modelData.error !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: hwModel.modelData.error
                        color: "#c5221f"
                    }
                    SettingsHint {
                        visible: !hwModel.modelData.downloadAvailable
                        text: hwModel.modelData.unpinned
                    }
                }
                RowLayout {
                    visible: hwModel.modelData.own && hwModel.modelData.installed
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        text: qsTr("In %1").arg(hwModel.modelData.folder)
                        font.pixelSize: 12
                        color: "#6b6f75"
                    }
                    Button {
                        objectName: "handwritingRemoveModel_" + hwModel.lang
                        text: qsTr("Remove")
                        onClicked: app.handwriting.removeModel(hwModel.lang)
                    }
                }
                SettingsHint {
                    visible: !hwModel.modelData.own
                    text: qsTr("The model is the one in %1 (chosen here, or XQT_HWR_MODEL / "
                               + "XQT_HWR_MODEL_DE): it is never downloaded over or removed here.")
                          .arg(hwModel.modelData.folder)
                }
            }
        }
        FolderDialog {
            id: modelFolderDialog
            objectName: "handwritingModelFolderDialog"
            property string language: "en"
            title: qsTr("A folder with a handwriting model (model.json)")
            onAccepted: app.handwriting.chooseFolder(language, selectedFolder)
        }
        Item { Layout.preferredHeight: 16 }
    }
}
