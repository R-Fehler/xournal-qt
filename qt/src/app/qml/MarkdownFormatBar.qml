// The formatting bar of the Markdown editors (qt/docs/md-editor.md, "Formatting bar"): a row of tools shown while
// Markdown is written (a .md, Markdown on a page, its source beside the page), for writing without knowing its
// marks. Grouped as Typora's and Obsidian's: the block's kind (paragraph, headings), the marks in the text, the lines'
// marks (lists, check boxes, quotes), then what is inserted on lines of its own (code block, table, formula block,
// image, rule, page break). The buttons show what is at the cursor (`format`, as app.markdownFormat). The tools do not
// take the focus: the text keeps it, and the keyboard stays.
//
// Where the room is short (qt/docs/adaptive-layout.md, "The format bar"): on a desktop or a tablet the bar takes the
// richest form that fits, as the tool bar does - everything as buttons; then the inserts in an "Insert" menu; then the
// block's kind as one button with a menu too. The block's kind, the marks and the lists stay in the row. Only when
// even that does not fit does the row scroll. On a phone the row scrolls sideways (the norm in mobile editors), with
// fading edges that show there is more.
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
    /// The color the fading edges fade into (the bar's own, or the panel's behind a transparent bar)
    property color fadeColor: color

    implicitHeight: 44
    color: "#ffffff"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e3e5e8" }

    function act(action, arg) { bar.formatRequested(action, arg === undefined ? "" : arg) }

    /// The window's layout (Main.qml's win.adaptive), if there is one
    readonly property var adaptiveLayout: typeof win !== "undefined" && win ? win.adaptive : null
    /// A phone: the row scrolls, nothing goes into menus
    readonly property bool phone: adaptiveLayout !== null && adaptiveLayout.phone
    readonly property bool touch: adaptiveLayout !== null && adaptiveLayout.touchProfile

    // --- the form that fits (priority + overflow) ---
    readonly property real spacing: 2
    readonly property real levelWidth: touch ? 40 : 36
    readonly property real buttonWidth: 40
    readonly property real separatorWidth: separatorProbe.implicitWidth
    /// The room of the row (the bar less its trailing buttons)
    readonly property real room: flick.width
    function groupWidth(n, w) { return n * w + (n - 1) * spacing }
    readonly property real marksWidth: groupWidth(6, buttonWidth) + separatorWidth + groupWidth(4, buttonWidth)
                                       + 4 * spacing
    readonly property real levelsWidth: groupWidth(4, levelWidth) + separatorWidth + spacing
    readonly property real levelButtonWidth: levelMenuButton.implicitWidth + separatorWidth + spacing
    readonly property real insertsWidth: separatorWidth + spacing + groupWidth(6, buttonWidth) + spacing
    readonly property real insertLabelWidth: separatorWidth + spacing + insertProbe.implicitWidth + spacing
    readonly property real insertIconWidth: separatorWidth + spacing + buttonWidth + spacing
    /// 1. The inserts go into the "Insert" menu (not on a phone: its row scrolls)
    readonly property bool insertsInMenu: !phone && room < levelsWidth + marksWidth + insertsWidth + 4
    /// 3. The block's kind as one button with a menu, when even the row with a plain "+" for the inserts does not fit
    readonly property bool levelsInMenu: insertsInMenu && room < levelsWidth + marksWidth + insertIconWidth + 4
    /// 2. "Insert" without its word (a "+"), where the word does not fit
    readonly property bool insertIconOnly: insertsInMenu
                                           && room < (levelsInMenu ? levelButtonWidth : levelsWidth) + marksWidth + insertLabelWidth + 4
    /// Nothing more to fold: the row scrolls (a phone, or a very narrow window)
    readonly property bool scrolls: flick.contentWidth > flick.width + 0.5

    ToolSeparator { id: separatorProbe; visible: false }
    // (the "Insert" button with its word, measured)
    ToolButton {
        id: insertProbe
        visible: false
        text: insertButton.text
        icon.source: insertButton.icon.source
        icon.width: 18
        icon.height: 18
        display: AbstractButton.TextBesideIcon
        font.pixelSize: 14
        leftPadding: 8
        rightPadding: 10
    }

    // A paragraph / heading level button (the one at the cursor highlighted); a finger held on it shows its name
    component LevelButton: IconButton {
        property int level
        property string tool
        readonly property bool current: (bar.format.heading || 0) === level
        implicitWidth: bar.levelWidth
        implicitHeight: 40
        display: AbstractButton.TextOnly
        focusPolicy: Qt.NoFocus  // (the text keeps the keys and the keyboard)
        enabled: !bar.format.codeBlock
        checkable: false
        highlighted: current
        font.bold: level > 0
        font.pixelSize: level === 0 ? 18 : level === 1 ? 16 : level === 2 ? 14 : 13
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

    /// The languages of the code block (the word after ``` can be any other)
    readonly property var languages: [
        { name: qsTr("Plain text"), lang: "" }, { name: "Python", lang: "python" },
        { name: "C++", lang: "cpp" }, { name: "C", lang: "c" }, { name: "Java", lang: "java" },
        { name: "JavaScript", lang: "js" }, { name: "TypeScript", lang: "ts" },
        { name: "Rust", lang: "rust" }, { name: "Bash", lang: "bash" }, { name: "SQL", lang: "sql" },
        { name: "JSON", lang: "json" }, { name: "YAML", lang: "yaml" }, { name: "HTML", lang: "html" },
        { name: "LaTeX", lang: "latex" }, { name: "MATLAB", lang: "matlab" }, { name: "R", lang: "r" }
    ]
    /// The block's kinds: [paragraph, heading 1-3]
    readonly property var levels: [
        { tool: "paragraph", text: "¶", name: qsTr("Paragraph (Ctrl+0)") },
        { tool: "heading1", text: "H1", name: qsTr("Heading 1 (Ctrl+1)") },
        { tool: "heading2", text: "H2", name: qsTr("Heading 2 (Ctrl+2)") },
        { tool: "heading3", text: "H3", name: qsTr("Heading 3 (Ctrl+3)") }
    ]

    /// At the end of the bar, outside what scrolls: a text document's ⋮ and "more tools" (Main.qml puts them here)
    property alias trailing: trailingSlot
    Item {
        id: trailingSlot
        objectName: bar.named("formatBarTrailing")
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: parent.height
    }

    Flickable {
        id: flick
        objectName: bar.named("formatBarFlick")
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: trailingSlot.left
        anchors.leftMargin: 6
        anchors.rightMargin: trailingSlot.width > 0 ? 4 : 0
        contentWidth: row.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        RowLayout {
            id: row
            height: flick.height
            spacing: bar.spacing

            // The block's kind: paragraph or heading 1-3, each a button of its own (the one at the cursor highlighted)
            LevelButton { objectName: bar.named("mdParagraph"); visible: !bar.levelsInMenu; level: 0; tool: "paragraph"; text: "¶"; tip: bar.levels[0].name; label: qsTr("Paragraph") }
            LevelButton { objectName: bar.named("mdHeading1"); visible: !bar.levelsInMenu; level: 1; tool: "heading1"; text: "H1"; tip: bar.levels[1].name; label: qsTr("Heading 1") }
            LevelButton { objectName: bar.named("mdHeading2"); visible: !bar.levelsInMenu; level: 2; tool: "heading2"; text: "H2"; tip: bar.levels[2].name; label: qsTr("Heading 2") }
            LevelButton { objectName: bar.named("mdHeading3"); visible: !bar.levelsInMenu; level: 3; tool: "heading3"; text: "H3"; tip: bar.levels[3].name; label: qsTr("Heading 3") }
            // ... or, where the room is short, one button: the kind at the cursor, a tap lists them
            IconButton {
                id: levelMenuButton
                objectName: bar.named("mdBlockButton")
                visible: bar.levelsInMenu
                readonly property int level: Math.max(0, Math.min(3, bar.format.heading || 0))
                text: bar.levels[level].text
                display: AbstractButton.TextOnly
                font.bold: level > 0
                font.pixelSize: level === 0 ? 18 : 15
                implicitWidth: 52
                implicitHeight: 40
                rightPadding: 16
                enabled: !bar.format.codeBlock
                focusPolicy: Qt.NoFocus
                label: qsTr("Paragraph or heading")
                tip: qsTr("Paragraph or heading (Ctrl+0 … Ctrl+3)")
                onClicked: Popups.openAt(levelMenu)
                Image {  // (a small arrow: a tap opens a list)
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    source: app.iconUrl("xqt-chevron-down-small")
                    sourceSize.width: 14
                    sourceSize.height: 14
                    opacity: 0.7
                }
                AdaptiveMenu {
                    id: levelMenu
                    objectName: bar.named("mdBlockMenu")
                    title: qsTr("Paragraph or heading")
                    focus: false
                    Repeater {
                        model: bar.levels
                        delegate: AdaptiveMenuItem {
                            required property var modelData
                            required property int index
                            objectName: bar.named("mdBlockItem") + index
                            text: modelData.name
                            checkable: true
                            checked: levelMenuButton.level === index
                            onTriggered: bar.act(modelData.tool)
                        }
                    }
                }
            }
            ToolSeparator {}
            // Marks in the text: around the selection, or empty with the cursor between them; again: they go
            FormatButton { objectName: bar.named("mdBold"); iconName: "xqt-bold"; tool: "bold"; checked: !!bar.format.bold; tip: qsTr("Bold (Ctrl+B)"); label: qsTr("Bold") }
            FormatButton { objectName: bar.named("mdItalic"); iconName: "xqt-italic"; tool: "italic"; checked: !!bar.format.italic; tip: qsTr("Italic (Ctrl+I)"); label: qsTr("Italic") }
            FormatButton { objectName: bar.named("mdStrike"); iconName: "xqt-strikethrough"; tool: "strike"; checked: !!bar.format.strike; tip: qsTr("Strikethrough (~~text~~)"); label: qsTr("Strikethrough") }
            FormatButton { objectName: bar.named("mdCode"); iconName: "xqt-code"; tool: "code"; checked: !!bar.format.code; tip: qsTr("Code in the text (Ctrl+E)"); label: qsTr("Code in the text") }
            FormatButton { objectName: bar.named("mdLink"); iconName: "xqt-link"; tool: "link"; checked: !!bar.format.link; tip: qsTr("Link (Ctrl+K); on a link: its text again"); label: qsTr("Link") }
            FormatButton { objectName: bar.named("mdMath"); iconName: "xqt-sigma"; tool: "inlineMath"; checked: !!bar.format.math; tip: qsTr("Formula in the text ($…$)"); label: qsTr("Formula in the text") }
            ToolSeparator {}
            // Marks of the lines (all selected lines): again, they go
            FormatButton { objectName: bar.named("mdBulletList"); iconName: "xqt-list"; tool: "bulletList"; checked: bar.format.list === "bullet"; tip: qsTr("Bullet list (- )"); label: qsTr("Bullet list") }
            FormatButton { objectName: bar.named("mdNumberedList"); iconName: "xqt-list-ordered"; tool: "numberedList"; checked: bar.format.list === "numbered"; tip: qsTr("Numbered list (1. )"); label: qsTr("Numbered list") }
            FormatButton { objectName: bar.named("mdTaskList"); iconName: "xqt-list-todo"; tool: "taskList"; checked: bar.format.list === "task"; tip: qsTr("Check boxes (- [ ] )"); label: qsTr("Check boxes") }
            FormatButton { objectName: bar.named("mdQuote"); iconName: "xqt-quote"; tool: "quote"; checked: !!bar.format.quote; tip: qsTr("Quote (> )"); label: qsTr("Quote") }
            ToolSeparator {}
            // Blocks, on lines of their own: buttons where there is room, else the "Insert" menu below
            FormatButton {
                objectName: bar.named("mdCodeBlock")
                visible: !bar.insertsInMenu
                iconName: "xqt-code-block"
                checked: !!bar.format.codeBlock
                tip: qsTr("Code block (```), with its language")
                label: qsTr("Code block")
                onClicked: Popups.openAt(codeMenu)
                AdaptiveMenu {
                    id: codeMenu
                    objectName: bar.named("mdCodeBlockMenu")
                    title: qsTr("Code block")
                    focus: false
                    Repeater {
                        model: bar.languages
                        delegate: AdaptiveMenuItem {
                            required property var modelData
                            text: modelData.name
                            onTriggered: bar.act("codeBlock", modelData.lang)
                        }
                    }
                }
            }
            FormatButton {
                objectName: bar.named("mdTable")
                visible: !bar.insertsInMenu
                iconName: "xqt-table"
                checked: !!bar.format.table
                tip: bar.format.table ? qsTr("Edit the table (rows, columns, alignment)") : qsTr("Insert a table")
                label: bar.format.table ? qsTr("Edit the table") : qsTr("Table")
                onClicked: bar.tableRequested()
            }
            FormatButton { objectName: bar.named("mdMathBlock"); visible: !bar.insertsInMenu; iconName: "xqt-sigma-block"; tool: "mathBlock"; tip: qsTr("Formula block ($$ … $$)"); label: qsTr("Formula block") }
            FormatButton { objectName: bar.named("mdImage"); visible: !bar.insertsInMenu; iconName: "xopp-tool-image"; tip: qsTr("Image… (a picture file, saved with the document)"); label: qsTr("Image…"); onClicked: imagePicker.open() }
            FormatButton { objectName: bar.named("mdRule"); visible: !bar.insertsInMenu; iconName: "xqt-rule"; tool: "rule"; tip: qsTr("Horizontal rule (---)"); label: qsTr("Horizontal rule") }
            FormatButton { objectName: bar.named("mdPageBreak"); visible: !bar.insertsInMenu; iconName: "xqt-page-break"; tool: "pageBreak"; tip: qsTr("Page break"); label: qsTr("Page break") }
            // "Insert": the same, as a menu, where the row has no room for them
            IconButton {
                id: insertButton
                objectName: bar.named("mdInsertButton")
                visible: bar.insertsInMenu
                text: qsTr("Insert")
                iconName: "xqt-plus"
                icon.width: 18
                icon.height: 18
                display: bar.insertIconOnly ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                font.pixelSize: 14
                implicitWidth: bar.insertIconOnly ? bar.buttonWidth : implicitContentWidth + leftPadding + rightPadding
                implicitHeight: 40
                leftPadding: bar.insertIconOnly ? 0 : 8
                rightPadding: bar.insertIconOnly ? 0 : 10
                focusPolicy: Qt.NoFocus
                checked: !!bar.format.codeBlock || !!bar.format.table
                label: qsTr("Insert")
                tip: qsTr("Insert: code block, table, formula block, image, rule, page break")
                onClicked: Popups.openAt(insertMenu)
                AdaptiveMenu {
                    id: insertMenu
                    objectName: bar.named("mdInsertMenu")
                    title: qsTr("Insert")
                    focus: false
                    AdaptiveMenu {
                        id: insertCodeMenu
                        objectName: bar.named("mdInsertCodeMenu")
                        title: qsTr("Code block")
                        iconName: "xqt-code-block"
                        focus: false
                        Repeater {
                            model: bar.languages
                            delegate: AdaptiveMenuItem {
                                required property var modelData
                                text: modelData.name
                                onTriggered: bar.act("codeBlock", modelData.lang)
                            }
                        }
                    }
                    AdaptiveMenuItem {
                        objectName: bar.named("mdInsertTable")
                        text: bar.format.table ? qsTr("Edit the table…") : qsTr("Table…")
                        icon.source: app.iconUrl("xqt-table")
                        onTriggered: bar.tableRequested()
                    }
                    AdaptiveMenuItem {
                        objectName: bar.named("mdInsertMathBlock")
                        text: qsTr("Formula block ($$ … $$)")
                        icon.source: app.iconUrl("xqt-sigma-block")
                        onTriggered: bar.act("mathBlock")
                    }
                    AdaptiveMenuItem {
                        objectName: bar.named("mdInsertImage")
                        text: qsTr("Image…")
                        icon.source: app.iconUrl("xopp-tool-image")
                        onTriggered: imagePicker.open()
                    }
                    AdaptiveMenuItem {
                        objectName: bar.named("mdInsertRule")
                        text: qsTr("Horizontal rule (---)")
                        icon.source: app.iconUrl("xqt-rule")
                        onTriggered: bar.act("rule")
                    }
                    AdaptiveMenuItem {
                        objectName: bar.named("mdInsertPageBreak")
                        text: qsTr("Page break")
                        icon.source: app.iconUrl("xqt-page-break")
                        onTriggered: bar.act("pageBreak")
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
    // The row scrolls: its edges fade where there is more (a phone; a very narrow window)
    Rectangle {
        objectName: bar.named("formatBarFadeLeft")
        visible: bar.scrolls && flick.contentX > 1
        x: flick.x
        y: flick.y
        width: 36
        height: flick.height - 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: bar.fadeColor }
            GradientStop { position: 1; color: Qt.rgba(bar.fadeColor.r, bar.fadeColor.g, bar.fadeColor.b, 0) }
        }
    }
    Rectangle {
        objectName: bar.named("formatBarFadeRight")
        visible: bar.scrolls && flick.contentX < flick.contentWidth - flick.width - 1
        x: flick.x + flick.width - width
        y: flick.y
        width: 36
        height: flick.height - 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(bar.fadeColor.r, bar.fadeColor.g, bar.fadeColor.b, 0) }
            GradientStop { position: 1; color: bar.fadeColor }
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
