// Markdown: write the Markdown of a page, or of a Markdown text box on it, beside the pages (right). The page shows
// it formatted as you type (a box is a Xournal++ text in a layer "Markdown": Xournal++ shows the source). The
// formatting bar's tools (MarkdownFormatBar, as on the page) change the source, each one undo step of the source;
// Enter continues a list. Done keeps it (one undo step), Cancel restores the page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: panel
    objectName: "markdownPanel"
    visible: false
    padding: 0
    background: Rectangle {
        color: "#fafafa"
        Rectangle { width: 1; anchors.top: parent.top; anchors.bottom: parent.bottom; color: "#d5d8dc" }  // towards the pages
    }

    property int zoomBefore: 100
    function open(page) { start(app.beginMarkdown(page === undefined ? -1 : page)) }
    /// The Markdown text box at a point of a page (page coordinates), or a new one there.
    function openBox(page, x, y) { start(app.beginMarkdownBox(page, x, y)) }
    function start(source) {
        if (!visible) zoomBefore = app.zoomPercent
        area.text = source
        visible = true
        Qt.callLater(app.fitWidth)  // the whole page beside the panel
        area.cursorPosition = area.length
        area.forceActiveFocus()
    }
    function close(keep) {
        pageUpdate.stop()
        if (keep) app.updateMarkdown(area.text)
        app.endMarkdown(keep)
        visible = false
        Qt.callLater(app.setZoomPercent, zoomBefore)
    }
    // The page follows shortly after typing
    Timer {
        id: pageUpdate
        interval: 150
        onTriggered: app.updateMarkdown(area.text)
    }
    Connections {
        target: app
        // Another tab or the home screen: editing ended there
        function onMarkdownChanged() { if (!app.markdownActive && panel.visible) { pageUpdate.stop(); panel.visible = false } }
    }

    // --- editing helpers (on the source) ---------------------------------------------------------------------------
    function lineStart(pos) { return area.text.lastIndexOf("\n", pos - 1) + 1 }
    function lineEnd(pos) { const i = area.text.indexOf("\n", pos); return i < 0 ? area.length : i }
    readonly property var prefixPattern: /^(\s*)(#{1,6} |[-*+] \[[ xX]\] |[-*+] |\d+[.)] |> )?/

    /// What is at the cursor, for the formatting bar
    property var format: ({})
    function anchorPosition() { return area.cursorPosition === area.selectionStart ? area.selectionEnd : area.selectionStart }
    function updateFormat() { if (visible) format = app.markdownFormatOf(area.text, anchorPosition(), area.cursorPosition) }
    /// The selection after a tool ({anchor, caret})
    function select(selection) {
        if (selection && selection.anchor !== undefined) {
            area.cursorPosition = selection.anchor
            area.moveCursorSelection(selection.caret, TextEdit.SelectCharacters)
        }
        area.forceActiveFocus()
        updateFormat()
    }
    /// A formatting tool (md::format: "bold", "heading2", ...) on the source: one undo step of the source
    function applyFormat(action, arg) {
        select(app.formatMarkdownIn(area.textDocument, anchorPosition(), area.cursorPosition, action, arg || ""))
    }
    function editTable() {
        const anchor = anchorPosition(), caret = area.cursorPosition
        panelTable.openFor(app.markdownTableIn(area.text, caret), function(cells, aligns) {
            panel.select(app.writeMarkdownTableIn(area.textDocument, anchor, caret, cells, aligns))
        })
    }
    /// Enter in a list or quote: the next line gets the same mark (the next number); on an empty item, the list ends.
    function returnPressed() {
        const pos = area.cursorPosition
        const start = lineStart(pos)
        const line = area.text.substring(start, pos)
        const m = line.match(prefixPattern)
        const mark = m[2] || ""
        if (!mark || mark.startsWith("#")) return false
        if (line.trim() === mark.trim()) {  // an empty item: end the list
            area.remove(start, pos)
            return false
        }
        let next = mark
        const number = mark.match(/^(\d+)([.)]) $/)
        if (number) next = (parseInt(number[1]) + 1) + number[2] + " "
        else if (/\[[xX]\]/.test(mark)) next = mark.replace(/\[[xX]\]/, "[ ]")
        area.insert(pos, "\n" + m[1] + next)
        return true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title and Done
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Label {
                Layout.fillWidth: true
                text: !app.markdownIsPageText ? qsTr("Markdown text box on page %1").arg(app.markdownPage + 1)
                      : app.markdownLastPage > app.markdownPage
                        ? qsTr("Markdown on pages %1–%2").arg(app.markdownPage + 1).arg(app.markdownLastPage + 1)
                        : qsTr("Markdown on page %1").arg(app.markdownPage + 1)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Button { objectName: "markdownCancel"; text: qsTr("Cancel"); flat: true; onClicked: panel.close(false) }
            Button { objectName: "markdownDone"; text: qsTr("Done"); highlighted: true; onClicked: panel.close(true) }
        }

        // The formatting tools (as on the page)
        MarkdownFormatBar {
            Layout.fillWidth: true
            namePrefix: "panel"
            color: "transparent"
            format: panel.format
            onFormatRequested: function(action, arg) { panel.applyFormat(action, arg) }
            onTableRequested: panel.editTable()
        }
        // The size of the text (the text's font size: the drawing follows)
        RowLayout {
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Label { text: qsTr("Size"); color: "#5f6368" }
            SpinBox {
                objectName: "markdownSize"
                from: 4; to: 72
                value: Math.round(app.markdownBoxSize)
                editable: true
                focusPolicy: Qt.NoFocus
                onValueModified: { app.setMarkdownBoxSize(value); area.forceActiveFocus() }
            }
        }

        // The text does not fit on the page
        Label {
            objectName: "markdownOverflow"
            visible: app.markdownOverflow > 0
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: "#b3261e"
            font.pixelSize: 12
            text: (app.markdownIsPageText ? qsTr("A block is higher than a page (about %1 lines below the margin).")
                                          : qsTr("The text is longer than the page (about %1 lines below the margin)."))
                    .arg(Math.max(1, Math.round(app.markdownOverflow / (app.markdownBoxSize * 1.25))))
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            background: Rectangle { color: "#ffffff"; border.width: 1; border.color: "#d5d8dc"; radius: 4 }
            TextArea {
                id: area
                objectName: "markdownArea"
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                font.family: "monospace"
                font.pixelSize: 14
                background: null
                padding: 14
                placeholderText: qsTr("Markdown: # heading, **bold**, *italic*, - list, 1. list, > quote, ``` code")
                onTextChanged: if (panel.visible) pageUpdate.restart()
                onCursorPositionChanged: panel.updateFormat()
                onSelectionStartChanged: panel.updateFormat()
                onSelectionEndChanged: panel.updateFormat()
                Keys.onPressed: function(event) {
                    const ctrl = event.modifiers & Qt.ControlModifier
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                        event.accepted = panel.returnPressed()
                    } else if (event.key === Qt.Key_Tab) {
                        area.insert(panel.lineStart(area.cursorPosition), "  "); event.accepted = true
                    } else if (event.key === Qt.Key_Backtab) {
                        const start = panel.lineStart(area.cursorPosition)
                        const spaces = area.text.substring(start, start + 2).match(/^ {0,2}/)[0].length
                        area.remove(start, start + spaces); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_B) {
                        panel.applyFormat("bold"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_I) {
                        panel.applyFormat("italic"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_E) {
                        panel.applyFormat("code"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_K) {
                        panel.applyFormat("link"); event.accepted = true
                    } else if (ctrl && event.key >= Qt.Key_0 && event.key <= Qt.Key_3) {
                        panel.applyFormat(event.key === Qt.Key_0 ? "paragraph" : "heading" + (event.key - Qt.Key_0)); event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        panel.close(true); event.accepted = true
                    } else if (event.matches(StandardKey.Paste) && app.clipboardLinkMarkdown() !== "") {
                        // A copied link (Copy link): as a Markdown link relative to this document
                        const link = app.clipboardLinkMarkdown()
                        if (area.selectedText !== "") area.remove(area.selectionStart, area.selectionEnd)
                        area.insert(area.cursorPosition, link); event.accepted = true
                    } else if (event.matches(StandardKey.Paste)) {
                        // Formulas as chat apps write them, \( \) and \[ \]: pasted as $ $ and $$ $$ (one undo step)
                        event.accepted = app.pasteMarkdown(area.textDocument, area.selectionStart, area.selectionEnd)
                    }
                }
            }
        }
    }

    MarkdownTableEditor {
        id: panelTable
        namePrefix: "panel"
        onClosed: area.forceActiveFocus()
    }
}
