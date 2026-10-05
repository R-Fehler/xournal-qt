// Insert new pages with a chosen background (blank, ruled, graph, ...), paper size and orientation, before or after
// a page. By default like the current page. Or "From a template" (qt/docs/templates.md): the template's page, as many
// times as asked, as a pasted copy of it (one undo step).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "insertPagesDialog"
    title: qsTr("Insert pages")
    preferredWidth: 660
    /// The page the new ones go next to (0-based), and where
    property int page: 0
    property bool after: true
    property int bgIndex: 0
    property int paper: -1  // -1: like the page
    property bool landscape: false
    /// The paper (qt/docs/dark-pages.md): like the page, a page of the PDF: the paper of new pages
    property color paperColor: "#ffffff"
    property bool textured: false
    /// "From a template": the template's file ("": new pages)
    property bool fromTemplate: false
    property string templatePath: ""
    property string templateName: ""

    function openAt(position) {
        // position: before this page index (page count: at the end)
        after = position > 0
        page = after ? position - 1 : 0
        open()
    }
    onAboutToShow: {
        const current = app.currentPageFormat()
        bgIndex = current.background >= 0 ? current.background : Math.max(0, app.settings.get("pageBackground"))
        landscape = current.landscape === true
        paperColor = current.pdf === false ? current.paper : app.settings.get("pageColor")
        textured = current.pdf === false ? current.textured === true : app.settings.get("pageTexture") === true
        paper = -1
        countBox.value = 1
        fromTemplate = false
    }
    function insert() {
        if (fromTemplate) {
            if (templatePath === "") {
                picker.open()
                return
            }
            app.insertTemplate(templatePath, after ? page + 1 : page, countBox.value)
        } else {
            app.insertPages(after ? page + 1 : page, bgIndex, paper, landscape, countBox.value, paperColor,
                            textured ? 1 : 0)
        }
        dlg.close()
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

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 12
        // New pages, or a template's page
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ButtonGroup { id: kind }
            Button {
                objectName: "insertNewPages"
                text: qsTr("New pages")
                checkable: true
                checked: !dlg.fromTemplate
                flat: true
                ButtonGroup.group: kind
                onClicked: dlg.fromTemplate = false
            }
            Button {
                objectName: "insertFromTemplate"
                text: qsTr("From a template")
                checkable: true
                checked: dlg.fromTemplate
                flat: true
                ButtonGroup.group: kind
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
                objectName: "insertTemplateName"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: dlg.templatePath === "" ? qsTr("No template chosen") : dlg.templateName
                font.weight: Font.DemiBold
            }
            Button {
                objectName: "insertChooseTemplate"
                text: qsTr("Choose…")
                onClicked: picker.open()
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
        // Paper and orientation, then how many and where: in two rows each in a narrow window (a phone)
        GridLayout {
            visible: !dlg.fromTemplate
            Layout.fillWidth: true
            columns: dlg.availableWidth < 520 ? 1 : 2
            columnSpacing: 12
            RowLayout {
                spacing: 12
                Label { text: qsTr("Paper") }
                ComboBox {
                    objectName: "insertPaperBox"
                    Layout.preferredWidth: 210
                    model: [qsTr("Like this page")].concat(app.settings.paperFormats)
                    currentIndex: dlg.paper + 1
                    onActivated: {
                        dlg.paper = currentIndex - 1
                        if (app.settings.paperIsWide(dlg.paper)) dlg.landscape = true  // (a slide is landscape)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Item { Layout.fillWidth: true }
                ButtonGroup { id: orientation }
                Button { text: qsTr("Portrait"); checkable: true; checked: !dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = false }
                Button { text: qsTr("Landscape"); checkable: true; checked: dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = true }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: dlg.availableWidth < 520 ? 2 : 3
            columnSpacing: 12
            SpinBox {
                id: countBox
                objectName: "insertCount"
                from: 1; to: 100
                editable: true
            }
            Label { text: countBox.value === 1 ? qsTr("page") : qsTr("pages"); Layout.fillWidth: dlg.availableWidth < 520 }
            ComboBox {
                objectName: "insertWhere"
                Layout.columnSpan: dlg.availableWidth < 520 ? 2 : 1
                Layout.preferredWidth: 200
                model: [qsTr("before page %1").arg(dlg.page + 1), qsTr("after page %1").arg(dlg.page + 1)]
                currentIndex: dlg.after ? 1 : 0
                onActivated: dlg.after = currentIndex === 1
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Insert"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onAccepted: dlg.insert()
        onRejected: dlg.close()
    }
}
