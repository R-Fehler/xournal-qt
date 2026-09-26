// The Annotations panel of the page sidebar (qt/docs/annotations-md.md): the document's highlights, text and Markdown
// boxes, handwriting (a picture of each piece, and the PDF text it is on) and links, by page. A tap goes there. The list follows the document
// (read again once the writing pauses). The filter button chooses the kinds shown; "Export as Markdown" writes them
// into a .md with links back to their pages.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: panel
    readonly property var model: app.annotations
    readonly property var kinds: [
        { key: "highlight", text: qsTr("Highlights") },
        { key: "text", text: qsTr("Text boxes") },
        { key: "markdown", text: qsTr("Markdown boxes") },
        { key: "ink", text: qsTr("Handwriting") },
        { key: "link", text: qsTr("Links") },
        { key: "note", text: qsTr("Notes") }]

    function kindLabel(kind) {
        switch (kind) {
        case "highlight": return qsTr("Highlight")
        case "pdfHighlight": return qsTr("Highlight in the PDF")
        case "text": return qsTr("Text")
        case "markdown": return qsTr("Markdown")
        case "ink": return qsTr("Handwriting")
        case "link": return qsTr("Link")
        case "note": return qsTr("Note")
        }
        return ""
    }
    function toggleKind(key, on) {
        const shown = panel.model.shownKinds.filter(k => k !== key)
        if (on) shown.push(key)
        panel.model.shownKinds = shown
    }
    /// Export as Markdown: next to the document (Xournal++ files), else where the save dialog says.
    function exportMarkdown() {
        const file = app.annotationsFile()
        if (file.toString() === "") {
            const suggestion = app.suggestedAnnotationsFile().toString()
            if (suggestion === "") {
                app.exportAnnotations(suggestion)  // (never saved: it says so)
                return
            }
            exportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            exportDialog.selectedFile = suggestion
            exportDialog.open()
        } else if (app.fileExists(file)) {
            replaceDialog.file = file
            replaceDialog.open()
        } else {
            app.exportAnnotations(file)
        }
    }

    RowLayout {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 10
        anchors.rightMargin: 2
        spacing: 0
        Label {
            Layout.fillWidth: true
            text: panel.model.count === panel.model.total ? qsTr("%n item(s)", "", panel.model.total)
                                                          : qsTr("%1 of %2").arg(panel.model.count).arg(panel.model.total)
            color: "#5f6368"
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        BusyIndicator {
            running: panel.model.busy
            visible: running
            implicitWidth: 24
            implicitHeight: 24
        }
        ToolButton {
            id: filterButton
            objectName: "annotationFilter"
            implicitWidth: 36
            implicitHeight: 36
            icon.source: app.iconUrl("xqt-filter")
            icon.width: 18
            icon.height: 18
            icon.color: panel.model.count !== panel.model.total ? Material.accentColor : "#3c4043"
            display: AbstractButton.IconOnly
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Show…")
            onClicked: filterMenu.popup(filterButton, 0, filterButton.height)
        }
        ToolButton {
            objectName: "annotationExport"
            implicitWidth: 36
            implicitHeight: 36
            enabled: panel.model.available
            icon.source: app.iconUrl("xqt-download")
            icon.width: 18
            icon.height: 18
            icon.color: "#3c4043"
            display: AbstractButton.IconOnly
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Export as Markdown")
            onClicked: panel.exportMarkdown()
        }
    }
    Menu {
        id: filterMenu
        Repeater {
            model: panel.kinds
            delegate: MenuItem {
                required property var modelData
                objectName: "annotationShow_" + modelData.key
                // (notes appear once another block makes them)
                visible: modelData.key !== "note" || panel.model.countOf("note") > 0
                height: visible ? implicitHeight : 0
                checkable: true
                checked: panel.model.shownKinds.indexOf(modelData.key) >= 0
                text: modelData.text + " (" + (panel.model.total, panel.model.countOf(modelData.key)) + ")"
                onTriggered: panel.toggleKind(modelData.key, checked)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Show all")
            onTriggered: panel.model.shownKinds = panel.kinds.map(k => k.key)
        }
    }

    ListView {
        id: list
        objectName: "annotationList"
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 2
        clip: true
        model: panel.model
        boundsBehavior: Flickable.StopAtBounds
        spacing: 2
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: list }

        section.property: "page"
        section.delegate: Label {
            required property string section
            width: ListView.view.width
            leftPadding: 10
            topPadding: 8
            bottomPadding: 2
            text: qsTr("Page %1").arg(Number(section) + 1)
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: "#5f6368"
        }

        delegate: ItemDelegate {
            id: entry
            objectName: "annotationItem"
            required property int index
            required property string kind
            required property int page
            required property string itemText
            required property string comment
            required property string color
            required property rect rect
            required property string picture
            required property real aspect
            readonly property bool marked: kind === "highlight" || kind === "pdfHighlight"
            width: ListView.view.width
            leftPadding: 8
            rightPadding: 8
            topPadding: 4
            bottomPadding: 4
            Accessible.name: panel.kindLabel(kind) + (itemText === "" ? "" : kind === "ink" ? " " + qsTr("on “%1”").arg(itemText)
                                                                                           : ": " + itemText)
            onClicked: app.jumpToPlace(page, rect)
            contentItem: RowLayout {
                spacing: 6
                Rectangle {
                    Layout.fillHeight: true
                    implicitWidth: 3
                    radius: 1.5
                    color: entry.marked ? entry.color : "#c4c7c5"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        text: panel.kindLabel(entry.kind)
                        font.pixelSize: 11
                        color: "#6b6f75"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: entry.itemText !== "" && entry.kind !== "ink"
                        text: entry.itemText
                        // (a Markdown box as it reads; its links are not followed here: a tap goes to the box)
                        textFormat: entry.kind === "markdown" ? Text.MarkdownText : Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: entry.kind === "link" ? "#1a73e8" : "#202124"
                        background: Rectangle {
                            visible: entry.marked
                            color: entry.color
                            opacity: 0.25
                        }
                    }
                    Image {
                        Layout.fillWidth: true
                        visible: entry.picture !== ""
                        Layout.preferredHeight: visible ? Math.min(96, Math.max(16, width * entry.aspect)) : 0
                        source: entry.picture
                        sourceSize.width: Math.round(width * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        horizontalAlignment: Image.AlignLeft
                        asynchronous: true
                        cache: true
                    }
                    Label {
                        // Handwriting on PDF text (underlined, circled, struck through, written over): that text
                        objectName: "annotationInkCaption"
                        Layout.fillWidth: true
                        visible: entry.kind === "ink" && entry.itemText !== ""
                        text: qsTr("on “%1”").arg(entry.itemText)
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        color: "#5f6368"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: entry.comment !== ""
                        text: entry.comment
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.italic: true
                        color: "#5f6368"
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: list
        width: list.width - 32
        visible: list.count === 0 && !panel.model.busy
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: "#6b6f75"
        text: !panel.model.available ? qsTr("A text file has no annotations.")
              : panel.model.total === 0 ? qsTr("No highlights or notes yet.")
              : qsTr("None of the kinds shown. Change it with the filter button.")
    }

    FileDialog {
        id: exportDialog
        objectName: "annotationExportDialog"
        title: qsTr("Export annotations as Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown (*.md)")]
        onAccepted: app.exportAnnotations(selectedFile)
    }
    Dialog {
        id: replaceDialog
        objectName: "annotationReplaceDialog"
        property url file
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(420, parent ? parent.width - 32 : 420)
        title: qsTr("Replace the exported annotations?")
        Label {
            width: replaceDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("%1 exists already. Writing it again replaces what it holds, also what was added to it by hand.")
                  .arg(decodeURIComponent(replaceDialog.file.toString().split("/").pop()))
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Replace"); objectName: "annotationReplace"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Save as…"); DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                     onClicked: { replaceDialog.close(); exportDialog.selectedFile = replaceDialog.file; exportDialog.open() } }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: app.exportAnnotations(file)
    }
}
