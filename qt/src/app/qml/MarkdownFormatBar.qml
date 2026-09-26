// The formatting bar of the Markdown editors (qt/docs/md-editor.md, "Formatting bar"): a row of tools shown while
// Markdown is written (a .md, Markdown on a page, its source beside the page), for writing without knowing its
// marks. Grouped as Typora's and Obsidian's: the block's kind (a menu: paragraph, headings), the marks in the text,
// the lines' marks (lists, check boxes, quotes), then blocks (code, table, and a menu with the rest). It scrolls
// sideways where the window is narrow (a phone). The buttons show what is at the cursor (`format`, as
// app.markdownFormat). The tools do not take the focus: the text keeps it, and the keyboard stays.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: bar
    objectName: named("markdownFormatBar")
    /// What is at the cursor: {bold, italic, strike, code, math, link, heading, list, quote, codeBlock, table}
    property var format: ({})
    /// A second bar (the editor beside the page): its items' names start with this
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    /// A tool was chosen: md::format's action ("bold", "heading2", "codeBlock", ...) and its argument (a language)
    signal formatRequested(string action, string arg)
    /// "Table": the table editor, for the table at the cursor or a new one
    signal tableRequested()

    implicitHeight: 44
    color: "#ffffff"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e3e5e8" }

    function act(action, arg) { bar.formatRequested(action, arg === undefined ? "" : arg) }

    // A paragraph / heading level button (the one at the cursor highlighted)
    component LevelButton: ToolButton {
        property int level
        property string tool
        readonly property bool current: (bar.format.heading || 0) === level
        implicitWidth: 36
        implicitHeight: 40
        focusPolicy: Qt.NoFocus  // (the text keeps the keys and the keyboard)
        enabled: !bar.format.codeBlock
        checkable: false
        highlighted: current
        font.bold: level > 0
        font.pixelSize: level === 0 ? 18 : level === 1 ? 16 : level === 2 ? 14 : 13
        ToolTip.visible: hovered
        ToolTip.delay: 600
        onClicked: bar.act(tool)
    }
    component FormatButton: IconButton {
        property string tool: ""
        implicitWidth: 40
        implicitHeight: 40
        icon.width: 20
        icon.height: 20
        focusPolicy: Qt.NoFocus
        onClicked: if (tool !== "") bar.act(tool)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        contentWidth: row.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        RowLayout {
            id: row
            height: flick.height
            spacing: 2

            // The block's kind: paragraph or heading 1-3, each a button of its own (the one at the cursor checked;
            // no menu: the row scrolls sideways when the window is narrow)
            LevelButton { objectName: bar.named("mdParagraph"); level: 0; tool: "paragraph"; text: "¶"; ToolTip.text: qsTr("Paragraph (Ctrl+0)") }
            LevelButton { objectName: bar.named("mdHeading1"); level: 1; tool: "heading1"; text: "H1"; ToolTip.text: qsTr("Heading 1 (Ctrl+1)") }
            LevelButton { objectName: bar.named("mdHeading2"); level: 2; tool: "heading2"; text: "H2"; ToolTip.text: qsTr("Heading 2 (Ctrl+2)") }
            LevelButton { objectName: bar.named("mdHeading3"); level: 3; tool: "heading3"; text: "H3"; ToolTip.text: qsTr("Heading 3 (Ctrl+3)") }
            ToolSeparator {}
            // Marks in the text: around the selection, or empty with the cursor between them; again: they go
            FormatButton { objectName: bar.named("mdBold"); iconName: "xqt-bold"; tool: "bold"; checked: !!bar.format.bold; tip: qsTr("Bold (Ctrl+B)") }
            FormatButton { objectName: bar.named("mdItalic"); iconName: "xqt-italic"; tool: "italic"; checked: !!bar.format.italic; tip: qsTr("Italic (Ctrl+I)") }
            FormatButton { objectName: bar.named("mdStrike"); iconName: "xqt-strikethrough"; tool: "strike"; checked: !!bar.format.strike; tip: qsTr("Strikethrough (~~text~~)") }
            FormatButton { objectName: bar.named("mdCode"); iconName: "xqt-code"; tool: "code"; checked: !!bar.format.code; tip: qsTr("Code in the text (Ctrl+E)") }
            FormatButton { objectName: bar.named("mdLink"); iconName: "xqt-link"; tool: "link"; checked: !!bar.format.link; tip: qsTr("Link (Ctrl+K); on a link: its text again") }
            FormatButton { objectName: bar.named("mdMath"); iconName: "xqt-sigma"; tool: "inlineMath"; checked: !!bar.format.math; tip: qsTr("Formula in the text ($…$)") }
            ToolSeparator {}
            // Marks of the lines (all selected lines): again, they go
            FormatButton { objectName: bar.named("mdBulletList"); iconName: "xqt-list"; tool: "bulletList"; checked: bar.format.list === "bullet"; tip: qsTr("Bullet list (- )") }
            FormatButton { objectName: bar.named("mdNumberedList"); iconName: "xqt-list-ordered"; tool: "numberedList"; checked: bar.format.list === "numbered"; tip: qsTr("Numbered list (1. )") }
            FormatButton { objectName: bar.named("mdTaskList"); iconName: "xqt-list-todo"; tool: "taskList"; checked: bar.format.list === "task"; tip: qsTr("Check boxes (- [ ] )") }
            FormatButton { objectName: bar.named("mdQuote"); iconName: "xqt-quote"; tool: "quote"; checked: !!bar.format.quote; tip: qsTr("Quote (> )") }
            ToolSeparator {}
            // Blocks, on lines of their own
            FormatButton {
                objectName: bar.named("mdCodeBlock")
                iconName: "xqt-code-block"
                checked: !!bar.format.codeBlock
                tip: qsTr("Code block (```), with its language")
                onClicked: Popups.openAt(codeMenu)
                Menu {
                    id: codeMenu
                    objectName: bar.named("mdCodeBlockMenu")
                    focus: false
                    // The languages the highlighter knows best; the word after ``` can be any other
                    Repeater {
                        model: [
                            { name: qsTr("Plain text"), lang: "" }, { name: "Python", lang: "python" },
                            { name: "C++", lang: "cpp" }, { name: "C", lang: "c" }, { name: "Java", lang: "java" },
                            { name: "JavaScript", lang: "js" }, { name: "TypeScript", lang: "ts" },
                            { name: "Rust", lang: "rust" }, { name: "Bash", lang: "bash" }, { name: "SQL", lang: "sql" },
                            { name: "JSON", lang: "json" }, { name: "YAML", lang: "yaml" }, { name: "HTML", lang: "html" },
                            { name: "LaTeX", lang: "latex" }, { name: "MATLAB", lang: "matlab" }, { name: "R", lang: "r" }
                        ]
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name
                            onTriggered: bar.act("codeBlock", modelData.lang)
                        }
                    }
                }
            }
            FormatButton {
                objectName: bar.named("mdTable")
                iconName: "xqt-table"
                checked: !!bar.format.table
                tip: bar.format.table ? qsTr("Edit the table (rows, columns, alignment)") : qsTr("Insert a table")
                onClicked: bar.tableRequested()
            }
            // Each tool is a button of its own: the row scrolls sideways when the window is too narrow, so nothing is
            // hidden behind a menu (the author, 2026-09-26)
            FormatButton { objectName: bar.named("mdMathBlock"); iconName: "xqt-sigma-block"; tool: "mathBlock"; tip: qsTr("Formula block ($$ … $$)") }
            FormatButton { objectName: bar.named("mdImage"); iconName: "xopp-tool-image"; tip: qsTr("Image… (a picture file, saved with the document)"); onClicked: imagePicker.open() }
            FormatButton { objectName: bar.named("mdRule"); iconName: "xqt-rule"; tool: "rule"; tip: qsTr("Horizontal rule (---)") }
            FormatButton { objectName: bar.named("mdPageBreak"); iconName: "xqt-page-break"; tool: "pageBreak"; tip: qsTr("Page break") }
            Item { Layout.fillWidth: true }
        }
    }

    // The image button: a picture file, saved with the document and linked at the cursor
    // (qt/docs/md-images.md)
    FileDialog {
        id: imagePicker
        objectName: bar.named("mdImageDialog")
        title: qsTr("Insert image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp *.svg *.bmp)"), qsTr("All files (*)")]
        onAccepted: bar.act("image", selectedFile.toString())
    }
}
