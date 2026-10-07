// "Tags…" of a document (a library card's menu, the document's ⋮ → Document; qt/docs/features/tags.md). A PDF (also one
// with notes, a text document, an archive PDF) gets its tags as keywords of the file, without typing into it: the chips
// with ✕, a field to add one (Enter, or a suggestion from the library's tags), Save writes them (an incremental update).
// The #tags typed in the document are listed as well; they are changed where they are written. A Xournal++ file or a
// Markdown file has only those: the dialog says how to add one.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dialog
    objectName: "tagsDialog"
    property string path: ""
    property var info: ({})
    /// The tags of the file as edited here
    property var fileTags: []
    property bool changed: false

    function openFor(p) {
        path = p
        info = app.documentTags(p)
        fileTags = info.file ? info.file.slice() : []
        changed = false
        field.text = ""
        open()
    }
    function clean(t) {
        t = t.trim()
        while (t.startsWith("#")) t = t.substring(1)
        return t
    }
    function has(t) {
        const k = t.toLowerCase()
        return fileTags.some(function(x) { return x.toLowerCase() === k })
    }
    function addTag(t) {
        t = clean(t)
        if (t === "" || has(t)) return
        fileTags = fileTags.concat([t])
        changed = true
        field.text = ""
    }
    function removeTag(t) {
        fileTags = fileTags.filter(function(x) { return x !== t })
        changed = true
    }

    preferredWidth: 520
    title: qsTr("Tags of %1").arg(info.name || "")
    standardButtons: info.editable ? Dialog.Save | Dialog.Cancel : Dialog.Close
    onAccepted: {
        if (info.editable && field.text.trim() !== "") addTag(field.text)
        if (info.editable && changed) app.setDocumentTags(path, fileTags)
    }
    onOpened: if (info.editable) field.forceActiveFocus()

    component TagChip: AbstractButton {
        id: chip
        property string tag
        property bool removable: false
        property bool suggestion: false
        implicitHeight: 32
        implicitWidth: chipRow.implicitWidth + 20
        Accessible.name: "#" + tag
        background: Rectangle {
            radius: height / 2
            color: chip.suggestion ? (chip.pressed ? "#d5d8dc" : "#f1f3f4") : "#d2e3fc"
            border.width: chip.suggestion ? 1 : 0
            border.color: "#dadce0"
        }
        contentItem: Item {
            RowLayout {
                id: chipRow
                anchors.centerIn: parent
                spacing: 6
                Label {
                    text: (chip.suggestion ? "+ #" : "#") + chip.tag
                    color: chip.suggestion ? "#3c4043" : "#174ea6"
                    font.pixelSize: 13
                }
                Label {
                    objectName: "tagsDialogRemove"
                    visible: chip.removable
                    text: "✕"
                    color: "#174ea6"
                }
            }
        }
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 10

        // --- the file's own (a PDF's keywords) ---
        Label {
            visible: dialog.info.pdf === true
            text: qsTr("In the file (keywords of the PDF, also for Zotero and other apps)")
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Flow {
            visible: dialog.info.pdf === true
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: dialog.fileTags
                delegate: TagChip {
                    required property string modelData
                    objectName: "tagsDialogChip"
                    tag: modelData
                    removable: dialog.info.editable === true
                    enabled: dialog.info.editable === true
                    onClicked: dialog.removeTag(modelData)
                }
            }
            Label {
                visible: dialog.fileTags.length === 0
                text: qsTr("None yet")
                color: "#80868b"
            }
        }
        RowLayout {
            visible: dialog.info.editable === true
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: field
                objectName: "tagsDialogField"
                Layout.fillWidth: true
                placeholderText: qsTr("Add a tag (course/math for one inside another)")
                selectByMouse: true
                inputMethodHints: Qt.ImhNoAutoUppercase
                Keys.onReturnPressed: dialog.addTag(text)
                Keys.onEnterPressed: dialog.addTag(text)
            }
            Button {
                objectName: "tagsDialogAdd"
                text: qsTr("Add")
                enabled: dialog.clean(field.text) !== ""
                onClicked: dialog.addTag(field.text)
            }
        }
        // The library's tags this file does not have yet (the most used first; those matching what is typed)
        Flow {
            visible: dialog.info.editable === true && suggestions.count > 0
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                id: suggestions
                model: (dialog.info.suggestions || []).filter(function(t) {
                    const typed = dialog.clean(field.text).toLowerCase()
                    return !dialog.has(t) && (typed === "" || t.toLowerCase().indexOf(typed) >= 0)
                }).slice(0, 12)
                delegate: TagChip {
                    required property string modelData
                    objectName: "tagsDialogSuggestion"
                    tag: modelData
                    suggestion: true
                    onClicked: dialog.addTag(modelData)
                }
            }
        }

        // --- typed in the document ---
        Label {
            text: qsTr("Typed in the document")
            font.weight: Font.DemiBold
            Layout.topMargin: 4
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: dialog.info.typed || []
                delegate: TagChip {
                    required property string modelData
                    objectName: "tagsDialogTyped"
                    tag: modelData
                    enabled: false
                }
            }
            Label {
                visible: !dialog.info.typed || dialog.info.typed.length === 0
                text: qsTr("None")
                color: "#80868b"
            }
        }
        Label {
            objectName: "tagsDialogNote"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#6b6f75"
            text: dialog.info.why ? dialog.info.why
                                  : qsTr("Typed #tags are changed in the document where they are written.")
        }
    }
}
