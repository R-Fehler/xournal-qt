// New document: name, page background, paper size and orientation (the settings for new pages, so the next new
// document starts with the same choice). In the library the document is saved at once in the current folder. Or it
// starts from a template (qt/docs/features/templates.md): its first page is the template's page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "newDocumentDialog"
    title: qsTr("New document")
    preferredWidth: 660
    readonly property var s: app.settings
    readonly property bool canSaveInLibrary: app.library.available
    property int bgIndex: 0
    /// Index in paperFormats; -1: the size of new pages is none of them (set in Xournal++): it stays
    property int paper: -1
    /// That size as text ("" when it is one of the formats)
    property string otherPaper: ""
    property bool landscape: false
    /// The paper (qt/docs/features/dark-pages.md)
    property color paperColor: "#ffffff"
    property bool textured: false
    /// "From a template": the template's file ("": a blank page)
    property bool fromTemplate: false
    property string templatePath: ""
    property string templateName: ""

    onAboutToShow: {
        nameField.text = ""
        bgIndex = Math.max(0, s.get("pageBackground"))
        paper = s.get("paperFormat")
        otherPaper = paper < 0 ? s.templatePaperSize() : ""
        // (after the model: a choice made before took the box's binding away)
        paperBox.currentIndex = paper < 0 ? s.paperFormats.length : paper
        landscape = s.get("landscape")
        paperColor = s.get("pageColor")
        textured = s.get("pageTexture") === true
        libraryBox.checked = canSaveInLibrary
        fromTemplate = false
        // The name field gets the keys at once, but not on a phone or tablet: there that opens the soft keyboard over
        // half of the dialog before anything is typed (a tap on the field opens it)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios")
            nameField.forceActiveFocus()
    }

    function create() {
        if (fromTemplate) {
            if (templatePath === "") {
                picker.open()
                return
            }
            app.createDocumentFromTemplate(nameField.text, libraryBox.checked, templatePath)
            dlg.close()
            return
        }
        s.set("pageBackground", bgIndex)
        if (paper >= 0)
            s.set("paperFormat", paper)
        s.set("landscape", landscape)
        s.set("pageColor", paperColor)
        s.set("pageTexture", textured)
        app.createDocument(nameField.text, libraryBox.checked)
        dlg.close()
    }

    ColumnLayout {
        id: column
        width: dlg.availableWidth
        spacing: 12

        TextField {
            id: nameField
            objectName: "newDocumentName"
            Layout.fillWidth: true
            enabled: libraryBox.checked
            placeholderText: qsTr("Name (Untitled)")
            selectByMouse: true
            Keys.onReturnPressed: dlg.create()
            Keys.onEnterPressed: dlg.create()
            EnterKey.type: Qt.EnterKeyDone  // (the soft keyboard's Enter key creates the document)
        }

        // A blank page, or a template's page
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ButtonGroup { id: start }
            Button {
                objectName: "newDocumentBlank"
                text: qsTr("Blank")
                checkable: true
                checked: !dlg.fromTemplate
                flat: true
                ButtonGroup.group: start
                onClicked: dlg.fromTemplate = false
            }
            Button {
                objectName: "newDocumentFromTemplate"
                text: qsTr("From a template")
                checkable: true
                checked: dlg.fromTemplate
                flat: true
                ButtonGroup.group: start
                onClicked: {
                    dlg.fromTemplate = true
                    if (dlg.templatePath === "") picker.open()
                }
            }
        }
        RowLayout {
            visible: dlg.fromTemplate
            Layout.fillWidth: true
            spacing: 12
            Label {
                objectName: "newDocumentTemplateName"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: dlg.templatePath === "" ? qsTr("No template chosen") : dlg.templateName
                font.weight: Font.DemiBold
            }
            Button {
                text: qsTr("Choose…")
                onClicked: picker.open()
            }
        }
        StickerPicker {
            id: picker
            mode: "templates"
            pickOnly: true
            onChosen: function(path) {
                dlg.templatePath = path
                dlg.templateName = path.replace(/^.*[\\/]/, "").replace(/\.xopp$/i, "")
                dlg.fromTemplate = true
            }
        }

        Label { visible: !dlg.fromTemplate; text: qsTr("Background"); font.weight: Font.DemiBold }
        BackgroundChooser {
            visible: !dlg.fromTemplate
            Layout.fillWidth: true
            selected: dlg.bgIndex
            landscape: dlg.landscape
            paper: dlg.paperColor
            onChosen: function(index) { dlg.bgIndex = index }
        }
        PaperSwatches {
            visible: !dlg.fromTemplate
            Layout.fillWidth: true
            paper: dlg.paperColor
            textured: dlg.textured
            onChosen: function(c) { dlg.paperColor = c }
            onTexturedToggled: function(on) { dlg.textured = on }
        }

        // Paper and orientation: one row, or two in a narrow window (a phone)
        GridLayout {
            visible: !dlg.fromTemplate
            Layout.fillWidth: true
            columns: dlg.availableWidth < 520 ? 1 : 2
            columnSpacing: 12
          RowLayout {
            spacing: 12
            Label { text: qsTr("Paper") }
            ComboBox {
                id: paperBox
                objectName: "paperBox"
                Layout.preferredWidth: 210
                // A0 (posters) to A7 (flashcards), Letter, ...; then the size of new pages if it is none of them
                model: dlg.otherPaper ? dlg.s.paperFormats.concat([qsTr("Other: %1").arg(dlg.otherPaper)])
                                      : dlg.s.paperFormats
                currentIndex: dlg.paper < 0 ? dlg.s.paperFormats.length : dlg.paper
                onActivated: {
                    dlg.paper = currentIndex < dlg.s.paperFormats.length ? currentIndex : -1
                    if (dlg.s.paperIsWide(currentIndex)) dlg.landscape = true  // (a slide is landscape)
                }
            }
          }
          RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Item { Layout.fillWidth: true }
            ButtonGroup { id: orientation }
            Button {
                objectName: "portraitButton"
                text: qsTr("Portrait")
                checkable: true
                checked: !dlg.landscape
                flat: true
                ButtonGroup.group: orientation
                onClicked: dlg.landscape = false
            }
            Button {
                objectName: "landscapeButton"
                text: qsTr("Landscape")
                checkable: true
                checked: dlg.landscape
                flat: true
                ButtonGroup.group: orientation
                onClicked: dlg.landscape = true
            }
          }
        }

        CheckBox {
            id: libraryBox
            objectName: "saveInLibrary"
            visible: dlg.canSaveInLibrary
            Layout.fillWidth: true
            text: {
                const crumbs = app.library.breadcrumbs.map(function(c) { return c.name })
                return qsTr("Save in the library: %1").arg(crumbs.join(" › "))
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Create"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onAccepted: dlg.create()
        onRejected: dlg.close()
    }
}
