// Markdown: write the Markdown of a page, or of a Markdown text box on it, beside the pages (right). The page shows
// it formatted as you type (a box is a Xournal++ text in a layer "Markdown": Xournal++ shows the source). The buttons
// insert Markdown; Enter continues a list. Done keeps it (one undo step), Cancel restores the page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import XournalQt.Canvas

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

    // --- emoji ------------------------------------------------------------------------------------------------------
    /// ":smi" before the cursor: the emoji suggested ({emoji, name} each); Escape closes them for that shortcode
    property var emojiSuggestions: []
    property int emojiCurrent: 0
    property string emojiQuery: ""
    property string emojiDismissed: ""
    function updateEmoji() {
        const pos = area.cursorPosition
        const name = area.selectedText === "" ? Emoji.typedShortcode(area.text.substring(lineStart(pos), pos) + area.preeditText) : ""
        if (name === emojiQuery) return
        emojiQuery = name
        if (emojiDismissed !== "" && !name.startsWith(emojiDismissed)) emojiDismissed = ""
        emojiSuggestions = name !== "" && emojiDismissed === "" ? Emoji.completions(name) : []
        emojiCurrent = 0
    }
    /// Suggestion `index` in place of the shortcode typed
    function chooseEmoji(index) {
        const e = emojiSuggestions[index].emoji
        Qt.inputMethod.commit()
        const pos = area.cursorPosition
        const from = pos - emojiQuery.length - 1
        area.remove(from, pos)
        area.insert(from, e)
        area.cursorPosition = from + e.length
        emojiSuggestions = []
        area.forceActiveFocus()
    }
    /// Left / Right / Backspace / Delete over a whole emoji (👩‍💻, 🇩🇪): Qt 6.7 splits flags
    function graphemeKey(event) {
        const shift = event.modifiers & Qt.ShiftModifier
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return false
        const pos = area.cursorPosition
        const hasSelection = area.selectionStart !== area.selectionEnd
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            if (hasSelection && !shift) return false
            const to = Emoji.graphemeStep(area.text, pos, event.key === Qt.Key_Right)
            if (shift) area.moveCursorSelection(to, TextEdit.SelectCharacters)
            else area.cursorPosition = to
            return true
        }
        if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && !hasSelection) {
            const forward = event.key === Qt.Key_Delete
            const to = Emoji.graphemeStep(area.text, pos, forward)
            if (to === pos) return true
            area.remove(Math.min(pos, to), Math.max(pos, to))
            return true
        }
        return false
    }

    // --- editing helpers (on the source) ---------------------------------------------------------------------------
    function lineStart(pos) { return area.text.lastIndexOf("\n", pos - 1) + 1 }
    function lineEnd(pos) { const i = area.text.indexOf("\n", pos); return i < 0 ? area.length : i }
    readonly property var prefixPattern: /^(\s*)(#{1,6} |[-*+] \[[ xX]\] |[-*+] |\d+[.)] |> )?/

    /// Put `before` / `after` around the selection (or insert both, the cursor between them).
    function wrap(before, after) {
        const s = area.selectionStart, e = area.selectionEnd
        area.insert(e, after)
        area.insert(s, before)
        area.select(s + before.length, e + before.length)
        area.forceActiveFocus()
    }
    /// Give the current line a prefix ("# ", "- ", "> ", ...) instead of the one it has; the same again removes it.
    function setPrefix(prefix) {
        const start = lineStart(area.cursorPosition)
        const line = area.text.substring(start, lineEnd(start))
        const m = line.match(prefixPattern)
        const indent = m[1].length
        const old = m[2] || ""
        const cursor = area.cursorPosition
        area.remove(start + indent, start + indent + old.length)
        const added = old === prefix ? "" : prefix
        area.insert(start + indent, added)
        area.cursorPosition = Math.max(start + indent + added.length, cursor - old.length + added.length)
        area.forceActiveFocus()
    }
    function insertBlock(text) {
        const pos = area.cursorPosition
        const start = lineStart(pos)
        const atEmptyLine = start === pos && lineEnd(pos) === pos
        const before = atEmptyLine ? "" : "\n\n"
        area.insert(lineEnd(pos), before + text)
        area.forceActiveFocus()
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

        // Insert Markdown
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2
            component MdButton: ToolButton {
                id: mb
                property string tip
                implicitWidth: Math.max(40, implicitContentWidth + 16)
                implicitHeight: 40
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered && tip !== ""
                ToolTip.text: tip
                ToolTip.delay: 600
            }
            MdButton { text: "H1"; font.bold: true; tip: qsTr("Heading 1 (# )"); onClicked: panel.setPrefix("# ") }
            MdButton { text: "H2"; font.bold: true; tip: qsTr("Heading 2 (## )"); onClicked: panel.setPrefix("## ") }
            MdButton { text: "H3"; font.bold: true; tip: qsTr("Heading 3 (### )"); onClicked: panel.setPrefix("### ") }
            IconButton {
                iconName: "xqt-bold"; tip: qsTr("Bold (Ctrl+B)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                focusPolicy: Qt.NoFocus
                onClicked: panel.wrap("**", "**")
            }
            IconButton {
                iconName: "xqt-italic"; tip: qsTr("Italic (Ctrl+I)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                focusPolicy: Qt.NoFocus
                onClicked: panel.wrap("*", "*")
            }
            MdButton { text: "S"; font.strikeout: true; tip: qsTr("Strikethrough"); onClicked: panel.wrap("~~", "~~") }
            MdButton { text: "</>"; font.family: "monospace"; tip: qsTr("Code (Ctrl+E)"); onClicked: panel.wrap("`", "`") }
            MdButton { text: qsTr("Link"); tip: qsTr("Link (Ctrl+K)"); onClicked: panel.wrap("[", "](https://)") }
            ToolSeparator {}
            IconButton {
                iconName: "xqt-list"; tip: qsTr("Bullet list (- )")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                focusPolicy: Qt.NoFocus
                onClicked: panel.setPrefix("- ")
            }
            IconButton {
                iconName: "xqt-list-ordered"; tip: qsTr("Numbered list (1. )")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                focusPolicy: Qt.NoFocus
                onClicked: panel.setPrefix("1. ")
            }
            MdButton { text: "☐"; tip: qsTr("Task list (- [ ] )"); onClicked: panel.setPrefix("- [ ] ") }
            MdButton { text: "“"; font.pixelSize: 20; tip: qsTr("Quote (> )"); onClicked: panel.setPrefix("> ") }
            MdButton { text: "{ }"; font.family: "monospace"; tip: qsTr("Code block"); onClicked: panel.insertBlock("```\n\n```\n") }
            MdButton {
                text: qsTr("Table"); tip: qsTr("Table")
                onClicked: panel.insertBlock("| Column | Column |\n|--------|--------|\n| | |\n")
            }
            MdButton { text: "―"; tip: qsTr("Horizontal rule (---)"); onClicked: panel.insertBlock("---\n") }
            ToolSeparator {}
            // The size of the text (the text's font size: the drawing follows)
            RowLayout {
                height: 40
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
                onTextChanged: { if (panel.visible) pageUpdate.restart(); panel.updateEmoji() }
                onCursorPositionChanged: panel.updateEmoji()
                onPreeditTextChanged: panel.updateEmoji()
                onSelectedTextChanged: panel.updateEmoji()

                Keys.onPressed: function(event) {
                    const ctrl = event.modifiers & Qt.ControlModifier
                    const n = panel.emojiSuggestions.length
                    if (n > 0 && !ctrl && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                        panel.emojiCurrent = (panel.emojiCurrent + (event.key === Qt.Key_Down ? 1 : n - 1)) % n
                        event.accepted = true
                    } else if (n > 0 && !ctrl && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab)) {
                        panel.chooseEmoji(panel.emojiCurrent); event.accepted = true
                    } else if (n > 0 && event.key === Qt.Key_Escape) {
                        panel.emojiDismissed = panel.emojiQuery; panel.emojiSuggestions = []; event.accepted = true
                    } else if (panel.graphemeKey(event)) {
                        event.accepted = true
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                        event.accepted = panel.returnPressed()
                    } else if (event.key === Qt.Key_Tab) {
                        area.insert(panel.lineStart(area.cursorPosition), "  "); event.accepted = true
                    } else if (event.key === Qt.Key_Backtab) {
                        const start = panel.lineStart(area.cursorPosition)
                        const spaces = area.text.substring(start, start + 2).match(/^ {0,2}/)[0].length
                        area.remove(start, start + spaces); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_B) {
                        panel.wrap("**", "**"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_I) {
                        panel.wrap("*", "*"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_E) {
                        panel.wrap("`", "`"); event.accepted = true
                    } else if (ctrl && event.key === Qt.Key_K) {
                        panel.wrap("[", "](https://)"); event.accepted = true
                    } else if (ctrl && event.key >= Qt.Key_1 && event.key <= Qt.Key_3) {
                        panel.setPrefix("#".repeat(event.key - Qt.Key_0) + " "); event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        panel.close(true); event.accepted = true
                    } else if (event.matches(StandardKey.Paste) && app.clipboardLinkMarkdown() !== "") {
                        // A copied link (Copy link): as a Markdown link relative to this document
                        const link = app.clipboardLinkMarkdown()
                        if (area.selectedText !== "") area.remove(area.selectionStart, area.selectionEnd)
                        area.insert(area.cursorPosition, link); event.accepted = true
                    }
                }
            }
        }
    }
    // ":smi" typed: the emoji suggested, below the cursor (over the panel, not clipped by the text's scroll view)
    EmojiSuggestions {
        objectName: "markdownEmojiSuggestions"
        model: panel.emojiSuggestions
        current: panel.emojiCurrent
        cursor: panel.emojiSuggestions.length > 0 ? area.mapToItem(parent, area.cursorRectangle) : Qt.rect(0, 0, 0, 0)
        onChosen: function(index) { panel.chooseEmoji(index) }
    }
}
