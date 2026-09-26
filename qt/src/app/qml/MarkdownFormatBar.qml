// The formatting bar of the Markdown editors (qt/docs/md-editor.md, "Formatting bar"): a row of tools shown while
// Markdown is written (a .md, Markdown on a page, its source beside the page), for writing without knowing its
// marks. Grouped as Typora's and Obsidian's: the block's kind (a menu: paragraph, headings), the marks in the text,
// the lines' marks (lists, check boxes, quotes), then blocks (code, table, and a menu with the rest). It scrolls
// sideways where the window is narrow (a phone). The buttons show what is at the cursor (`format`, as
// app.markdownFormat). The tools do not take the focus: the text keeps it, and the keyboard stays.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

            // The block's kind: paragraph or heading (the current one shown)
            ToolButton {
                id: headingButton
                objectName: bar.named("mdHeadingButton")
                readonly property int level: bar.format.heading || 0
                implicitWidth: 56
                implicitHeight: 40
                focusPolicy: Qt.NoFocus
                enabled: !bar.format.codeBlock
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Paragraph or heading (Ctrl+0, Ctrl+1, Ctrl+2, Ctrl+3)")
                ToolTip.delay: 600
                onClicked: Popups.openAt(headingMenu)
                // "¶" or "H1"-"H6", and the arrow of a menu
                contentItem: Row {
                    spacing: 2
                    Label {
                        objectName: bar.named("mdHeadingLabel")
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        horizontalAlignment: Text.AlignHCenter
                        text: headingButton.level > 0 ? "H" + headingButton.level : "¶"
                        font.bold: headingButton.level > 0
                        font.pixelSize: headingButton.level > 0 ? 15 : 19
                        color: headingButton.enabled ? (headingButton.level > 0 ? Material.accentColor : "#303030") : "#b0b0b0"
                    }
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        source: app.iconUrl("xqt-chevron-down-small")
                        sourceSize.width: 16
                        sourceSize.height: 16
                        opacity: 0.7
                    }
                }
                Menu {
                    id: headingMenu
                    objectName: bar.named("mdHeadingMenu")
                    focus: false  // (the text keeps the keys and the keyboard)
                    component LevelItem: MenuItem {
                        property int level
                        property string tool
                        checkable: true
                        checked: headingButton.level === level
                        onTriggered: bar.act(tool)
                    }
                    LevelItem { text: qsTr("Paragraph (Ctrl+0)"); level: 0; tool: "paragraph"; objectName: bar.named("mdParagraphItem") }
                    LevelItem { text: qsTr("Heading 1 (Ctrl+1)"); level: 1; tool: "heading1"; font.pixelSize: 20; font.bold: true; objectName: bar.named("mdHeading1Item") }
                    LevelItem { text: qsTr("Heading 2 (Ctrl+2)"); level: 2; tool: "heading2"; font.pixelSize: 17; font.bold: true; objectName: bar.named("mdHeading2Item") }
                    LevelItem { text: qsTr("Heading 3 (Ctrl+3)"); level: 3; tool: "heading3"; font.bold: true; objectName: bar.named("mdHeading3Item") }
                }
            }
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
            FormatButton {
                objectName: bar.named("mdInsert")
                iconName: "xqt-plus"
                tip: qsTr("Insert: formula, rule, image, page break")
                onClicked: Popups.openAt(insertMenu)
                Menu {
                    id: insertMenu
                    objectName: bar.named("mdInsertMenu")
                    focus: false
                    MenuItem { objectName: bar.named("mdMathBlockItem"); text: qsTr("Formula block ($$ … $$)"); icon.source: app.iconUrl("xqt-sigma"); onTriggered: bar.act("mathBlock") }
                    MenuItem { objectName: bar.named("mdRuleItem"); text: qsTr("Horizontal rule (---)"); icon.source: app.iconUrl("xqt-rule"); onTriggered: bar.act("rule") }
                    MenuItem { objectName: bar.named("mdImageItem"); text: qsTr("Image (a placeholder to fill in)"); icon.source: app.iconUrl("xopp-tool-image"); onTriggered: bar.act("image") }
                    MenuItem { objectName: bar.named("mdPageBreakItem"); text: qsTr("Page break"); icon.source: app.iconUrl("xqt-page-break"); onTriggered: bar.act("pageBreak") }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
