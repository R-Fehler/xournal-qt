// xournal-qt: part of the main window (Main.qml): the buttons of the app's items, kept out of sight and lent to
// the bars (Toolbox.qml) that hold them.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

// The window's buttons of the app's items (tools and commands, qt/docs/toolbox.md): kept here, out of sight, and
// lent to the bar that holds each of them in the arrangement (the rail or the top bar, ToolboxModel); one not placed
// stays here (⋮ and the catalog reach it). The buttons of the moment (the emoji while writing, edit as notes, open
// externally) sit at the top bar's end, before "+".
Item {
    id: toolArea
    objectName: "toolArea"
    visible: false
    Material.foreground: "#303030"
    /// The buttons by their names (the app items' names of ToolboxModel, and the buttons of the moment)
    readonly property var slots: ({
        hand: handTool, touchDrawing: touchDrawingTool, select: selectTool, snip: snipTool, write: writeButton,
        geometry: geometryTool, pdfText: pdfTextTool, emoji: emojiButton, image: imageTool, sticker: stickerTool,
        record: recordTool, addPage: addPageTool, search: searchTool,
        fullScreen: fullScreenTool, present: presentTool, read: readTool, zen: zenTool, replay: replayTool,
        settings: settingsTool,
        new: newTool, open: openTool, save: saveTool, milestone: milestoneTool, editAsNotes: editAsNotesTool,
        openExternally: openExternallyTool,
        share: shareTool, print: printTool, bookmark: bookmarkTool, favourite: favouriteTool, tags: tagsTool
    })
    /// The buttons of the moment: not items of the arrangement; at the top bar's end while they are offered
    readonly property var momentary: ["emoji", "editAsNotes", "openExternally"]
    Component.onCompleted: {
        momentary.forEach(function(n) {
            const b = slots[n]
            b.parent = topBarPane.leadingTail
            b.visible = Qt.binding(function() { return b.offered !== false })
        })
    }
    // (the buttons on neither bar, and those not offered for this document)
    Item { id: toolBank; visible: false }
    Item {
        // --- the buttons of the app's items (lent to the bar that holds them; `offered`: there at all for this
        // document; undo and redo are the heads of the rail and the top bar) ---
        // (the pens, highlighters, erasers, shapes, text boxes and sticky notes are the toolbox's entries; the buttons
        // below are its fixed tools, lent to it, and the commands)
        IconButton {
            id: handTool
            objectName: "handButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xopp-hand"
            label: qsTr("Hand")
            tip: qsTr("Hand: scroll with the pen or the mouse (A)")
            checked: app.tool === "hand"
            onClicked: app.selectTool("hand")
        }
        // Draw with the finger (one finger draws, two scroll and zoom): a switch, not a tool
        IconButton {
            id: touchDrawingTool
            objectName: "touchDrawingButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-finger-draw"
            label: qsTr("The finger draws")
            tip: checked ? qsTr("The finger draws (two fingers scroll) - tap: the finger scrolls")
                         : qsTr("Draw with the finger (two fingers scroll)")
            checked: (app.settings.revision, app.settings.get("touchDrawing"))
            onClicked: app.settings.set("touchDrawing", !checked)
        }
        ToolCycleButton { id: selectTool; objectName: "selectButton"; parent: toolBank; group: "select"; property bool offered: !win.textDoc }
        // Snip (qt/docs/snip.md): one tap away, a fixed tool of the rail (qt/copy-tools); a tap while armed: the other
        // shape
        ToolCycleButton { id: snipTool; objectName: "snipButton"; parent: toolBank; group: "snip"; property bool offered: !win.textDoc }
        // Writing on the page with the keyboard: Markdown, formatted while typing (hold: its source beside the page).
        IconButton {
            id: writeButton
            objectName: "textModeButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-page-text"
            label: qsTr("Write on the page")
            tip: qsTr("Write on the page: Markdown, shown formatted%1. Hold: its source beside the page").arg(win.keyNote("markdownMode"))
            checked: markdownPanel.visible || app.markdownOnPage
            ownHold: true
            readonly property string holdText: qsTr("Markdown source beside the page…")
            onClicked: {
                if (markdownPanel.visible) markdownPanel.close(true)
                else if (app.markdownOnPage) app.endMarkdownOnPage()
                else app.writeMarkdownOnPage()  // (formatted while typing, on the page)
            }
            onPressAndHold: Popups.openAt(writeMenu)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(writeMenu, point.position) }
            }
            AdaptiveMenu {
                id: writeMenu
                objectName: "writeModeMenu"
                title: qsTr("Write on the page")
                titleShown: true
                AdaptiveMenuItem {
                    objectName: "markdownItem"
                    text: qsTr("Markdown (shown formatted, on the page)")
                    icon.source: app.iconUrl("xqt-page-text")
                    onTriggered: app.writeMarkdownOnPage()
                }
                AdaptiveMenuItem {
                    objectName: "markdownSourceItem"
                    text: qsTr("Markdown source beside the page")
                    icon.source: app.iconUrl("xqt-markdown")
                    onTriggered: {
                        const onPage = app.takeMarkdownFromPage()
                        if (onPage.page === undefined) markdownPanel.open()
                        else if (onPage.pageText) markdownPanel.open(onPage.page)
                        else markdownPanel.openBox(onPage.page, onPage.x, onPage.y)
                    }
                }
            }
        }
        ToolCycleButton { id: geometryTool; objectName: "geometryButton"; parent: toolBank; group: "geometry"; property bool offered: !win.textDoc }
        // Text on the page (ToolGroups "text"): marking PDF text (highlight, underline, strike through, select) ↔ copying
        // handwriting as text (one sweep over ink, then the tool before; qt/copy-tools). A tap while one is in use: the
        // other; held or right-clicked: both, and how PDF text is marked
        IconButton {
            id: pdfTextTool
            objectName: "pdfTextButton"
            parent: toolBank
            property bool offered: !win.textDoc
            readonly property var icons: ({ "highlight": "xqt-mark-text", "underline": "xqt-underline",
                                            "strikethrough": "xqt-strikethrough", "select": "xopp-select-pdf-text-area" })
            readonly property string currentKey: win.toolGroups.current("text")
            readonly property bool copies: currentKey === "copyInkText"
            iconName: copies ? "xqt-copy-ink-text" : icons[app.pdfTextMode] || "xqt-mark-text"
            label: win.toolGroups.variant("text", currentKey).name
            tip: copies ? qsTr("Copy handwriting as text (sweep over the words; tap again: mark PDF text; hold: more)")
                        : qsTr("Mark PDF text (drag over the text; tap again: copy handwriting as text; hold: how it marks)")
            checked: win.toolGroups.isActive("text")
            ownHold: true
            onClicked: win.toolGroups.tap("text")
            onPressAndHold: Popups.openAt(pdfTextMenu)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(pdfTextMenu, point.position) }
            }
            // (two variants: the dots of a cycling button)
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Math.max(2, (parent.height - pdfTextTool.icon.height) / 2 - 8)
                spacing: 3
                Repeater {
                    model: 2
                    delegate: Rectangle {
                        required property int index
                        width: 4
                        height: 4
                        radius: 2
                        color: index === (pdfTextTool.copies ? 1 : 0)
                               ? (pdfTextTool.checked ? Material.accentColor : "#505050") : "#b4b8bd"
                    }
                }
            }
            AdaptiveMenu {
                id: pdfTextMenu
                objectName: "pdfTextMenu"
                title: qsTr("Mark PDF text")
                titleShown: true
                minimumWidth: 320
                component ModeItem: AdaptiveMenuItem {
                    property string mode
                    checkable: true
                    checked: app.pdfTextMode === mode
                    onTriggered: {
                        app.pdfTextMode = mode
                        if (app.tool !== "selectPdfTextRect") app.selectTool("selectPdfTextLinear")
                    }
                }
                ModeItem { text: qsTr("Highlight"); mode: "highlight"; icon.source: app.iconUrl("xqt-mark-text") }
                // The highlight color: three presets
                RowLayout {
                    objectName: "highlightColors"
                    width: parent ? parent.width : implicitWidth
                    Label { text: qsTr("Highlight color"); Layout.leftMargin: 16; Layout.fillWidth: true; color: "#5f6368" }
                    HighlightColors { Layout.rightMargin: 8 }
                }
                ModeItem { text: qsTr("Underline"); mode: "underline"; icon.source: app.iconUrl("xqt-underline") }
                ModeItem { text: qsTr("Strike through"); mode: "strikethrough"; icon.source: app.iconUrl("xqt-strikethrough") }
                ModeItem { text: qsTr("Select (then copy or mark)"); mode: "select"; icon.source: app.iconUrl("xopp-select-pdf-text-area") }
                MenuSeparator {}
                AdaptiveMenuItem {
                    text: qsTr("Select by area (columns, tables)")
                    checkable: true
                    checked: app.tool === "selectPdfTextRect"
                    onTriggered: app.selectTool(checked ? "selectPdfTextRect" : "selectPdfTextLinear")
                }
                MenuSeparator {}
                // Handwriting: its words (as the handwriting search read them) to the clipboard as text
                AdaptiveMenuItem {
                    objectName: "copyInkTextItem"
                    text: win.withKeys(qsTr("Copy handwriting as text"), "copyInkText")
                    icon.source: app.iconUrl("xqt-copy-ink-text")
                    checkable: true
                    checked: app.inkCopy
                    onTriggered: win.toolGroups.activate("text", "copyInkText")
                }
            }
        }
        // Writing on the page (a text box, Markdown, a text file): the emoji picker
        IconButton {
            id: emojiButton
            objectName: "emojiButton"
            parent: toolBank
            property bool offered: canvas.textEditing
            label: qsTr("Emoji")
            tip: qsTr("Emoji (or type : and a name, like :smile)")
            text: "\u{1F642}"
            display: AbstractButton.TextOnly
            font.family: "Xournal Qt Emoji"
            font.pixelSize: 22
            focusPolicy: Qt.NoFocus  // (the text being written keeps the keys)
            onClicked: canvasEmojiPicker.open()
            EmojiPicker {
                id: canvasEmojiPicker
                owner: emojiButton
                ownerY: emojiButton.height
                onPicked: function(emoji) { close(); canvas.insertText(emoji) }
            }
        }
        IconButton {
            id: imageTool
            objectName: "imageButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xopp-tool-image"
            label: qsTr("Image")
            tip: qsTr("Insert an image (hold: snip a picture from a page)")
            ownHold: true
            readonly property string holdText: qsTr("Snip a picture, a picture file, a check box…")
            onClicked: imageDialog.open()
            onPressAndHold: Popups.openAt(imageMenu)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(imageMenu, point.position) }
            }
            // A picture file, or a snip: the picture of a rectangle or lasso on any page (qt/docs/snip.md), to paste
            AdaptiveMenu {
                id: imageMenu
                objectName: "imageMenu"
                title: qsTr("Image")
                AdaptiveMenuItem {
                    objectName: "imageFromFileItem"
                    text: qsTr("Insert a picture file…")
                    icon.source: app.iconUrl("xopp-tool-image")
                    onTriggered: imageDialog.open()
                }
                AdaptiveMenuItem {
                    objectName: "snipItem"
                    text: qsTr("Snip from a page (copy a picture)")
                    icon.source: app.iconUrl("xqt-snip")
                    onTriggered: app.startSnip("rect")
                }
                AdaptiveMenuItem {
                    objectName: "snipLassoItem"
                    text: qsTr("Snip with the lasso")
                    icon.source: app.iconUrl("xqt-snip")
                    onTriggered: app.startSnip("lasso")
                }
                // A check box for a to-do written by hand beside it (qt/docs/todos.md): the next tap places it
                AdaptiveMenuItem {
                    objectName: "todoStampItem"
                    text: qsTr("Check box for a handwritten to-do")
                    icon.source: app.iconUrl("xqt-list-todo")
                    onTriggered: app.startTodoStamp()
                }
            }
        }
        // Stickers (qt/docs/stickers.md): saved content of the library, pasted with a tap (self-contained:
        // StickerButton.qml brings its picker and its dialog)
        StickerButton {
            id: stickerTool
            parent: toolBank
            property bool offered: !win.textDoc
        }
        // Recording (qt/docs/audio.md): self-contained, the tool bar only places it
        RecordButton { id: recordTool; parent: toolBank }
        IconButton {
            id: addPageTool
            objectName: "addPageButton"
            parent: toolBank
            property bool offered: !win.textDoc  // (a text file: no pages to add)
            iconName: "xopp-page-add"
            label: qsTr("Add a page")
            tip: qsTr("Add a page after the current one (press and hold: a template, background, size, several pages)")
            ownHold: true
            readonly property string holdText: qsTr("A template, background, size, several pages…")
            onClicked: app.addPageAfterCurrent()
            onPressAndHold: Popups.openAt(addPageMenu)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(addPageMenu, point.position) }
            }
            // Its list: the templates used last (qt/docs/templates.md), all templates, the Insert pages dialog
            AdaptiveMenu {
                id: addPageMenu
                objectName: "addPageMenu"
                title: qsTr("Add a page")
                titleShown: true
                property var recent: []
                onAboutToShow: recent = app.templates.recent(5)
                AdaptiveMenuItem {
                    objectName: "addPageInsertPagesItem"
                    text: qsTr("Background, size, several pages…")
                    icon.source: app.iconUrl("xopp-page-add")
                    onTriggered: insertPagesDialog.openAt(app.pageNumber)
                }
                MenuSeparator {}
                Instantiator {
                    model: addPageMenu.recent
                    delegate: AdaptiveMenuItem {
                        required property var modelData
                        objectName: "addPageTemplate_" + modelData.name
                        text: modelData.name
                        icon.source: app.iconUrl("xqt-file-plus")
                        onTriggered: app.insertTemplate(modelData.path, app.pageNumber, 1)
                    }
                    // (after the dialog's entry and the line)
                    onObjectAdded: function(index, object) { addPageMenu.insertItem(index + 2, object) }
                    onObjectRemoved: function(index, object) { addPageMenu.removeItem(object) }
                }
                AdaptiveMenuItem {
                    objectName: "addPageFromTemplateItem"
                    text: qsTr("From a template…")
                    icon.source: app.iconUrl("xqt-file-plus")
                    onTriggered: templatePicker.openToInsert(app.pageNumber)
                }
                AdaptiveMenuItem {
                    objectName: "addPageFromFileItem"
                    text: qsTr("Insert pages from a file…")
                    icon.source: app.iconUrl("xqt-import")
                    onTriggered: pageFiles.chooseFile(app.pageNumber - 1, true)
                }
                AdaptiveMenuItem {
                    objectName: "addPageSaveTemplateItem"
                    text: qsTr("Save this page as template…")
                    icon.source: app.iconUrl("xqt-file-plus")
                    onTriggered: templateSaveDialog.openForPage(app.pageNumber - 1)
                }
            }
        }
        IconButton {
            id: searchTool
            objectName: "searchButton"
            parent: toolBank
            iconName: "xqt-search"
            label: qsTr("Search")
            tip: qsTr("Search") + win.keyNote("find")
            checked: searchBar.visible
            onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar()
        }
        // Full screen (F11). Not inside full screen itself: the tools there end with "Leave full screen"
        IconButton {
            id: fullScreenTool
            objectName: "fullScreenButton"
            parent: toolBank
            property bool offered: !win.modes.fullScreenMode
            iconName: "xopp-fullscreen"
            label: qsTr("Full screen")
            tip: qsTr("Full screen") + win.keyNote("fullScreen")
            onClicked: win.modes.fullScreenMode = true
        }
        // Present: full screen, page by page (from the current page); held or right-clicked: only the page
        IconButton {
            id: presentTool
            objectName: "presentButton"
            parent: toolBank
            property bool offered: !win.modes.fullScreenMode
            iconName: "xopp-presentation-mode"
            label: qsTr("Present")
            tip: qsTr("Present%1. Hold: its menu with \"Present without controls\"%2").arg(win.keyNote("present")).arg(win.keyNote("presentClean"))
            ownHold: true
            /// What its long press does (in its menu on a bar)
            readonly property string holdText: qsTr("Present without controls") + win.keyNote("presentClean")
            onClicked: win.modes.startPresenting()
            onPressAndHold: win.modes.startPresenting(true)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: win.modes.startPresenting(true)
            }
        }
        IconButton {
            id: settingsTool
            objectName: "settingsButton"
            parent: toolBank
            iconName: "xqt-settings"
            label: qsTr("Settings")
            tip: qsTr("Settings") + win.keyNote("settings")
            onClicked: settingsPage.open()
        }
        // New: the tab strip's "+" where the tab strip is shown (one place for each action, qt/docs/adaptive-layout.md);
        // a button of the bar where it is not (the compact chrome's tools, the phone's sheet)
        IconButton {
            id: newTool
            objectName: "newButton"
            parent: toolBank
            property bool offered: !tabStrip.visible
            iconName: "xopp-document-new"
            label: qsTr("New document")
            tip: qsTr("New document (new tab)")
            onClicked: app.newDocument()
        }
        IconButton {
            id: openTool
            objectName: "openButton"
            parent: toolBank
            iconName: "xopp-document-open"
            label: qsTr("Open…")
            tip: qsTr("Open in a new tab%1").arg(win.keyNote("open"))
            onClicked: openDialog.open()
        }
        IconButton {
            id: saveTool
            objectName: "saveButton"
            parent: toolBank
            iconName: "xopp-document-save"
            label: qsTr("Save")
            tip: qsTr("Save") + win.keyNote("save")
            onClicked: saveOrAsk(null)
        }
        // A .md: a copy as notes (a .xopp) to write on with the pen; the .md stays as it is
        IconButton {
            id: editAsNotesTool
            objectName: "editAsNotesButton"
            parent: toolBank
            property bool offered: app.textDocument === "markdown"
            iconName: "xqt-notebook-pen"
            label: qsTr("Edit as notes")
            tip: qsTr("Edit as notes: a copy to write on with the pen (saved as a .xopp; the .md stays)")
            onClicked: app.editAsNotes()
        }
        // A .md, a text file, an image: in the app the system has for it (a code editor, …)
        IconButton {
            id: openExternallyTool
            objectName: "openExternallyButton"
            parent: toolBank
            property bool offered: app.canOpenExternally
            iconName: "xqt-external-link"
            label: qsTr("Open externally")
            tip: qsTr("Open externally (in the app the system has for this file)")
            onClicked: saveFlow.openExternally()
        }
        // Commands of the top bar's first layout that ⋮ has too (qt/docs/toolbox.md, "The top bar": ⋮ is complete)
        IconButton {
            id: shareTool
            objectName: "shareButton"
            parent: toolBank
            iconName: "xqt-share"
            label: qsTr("Share")
            tip: qsTr("Share…")
            onClicked: shareFlow.shareDialog.openFor("")
        }
        IconButton {
            id: printTool
            objectName: "printButton"
            parent: toolBank
            iconName: "xopp-document-print"
            label: qsTr("Print")
            tip: qsTr("Print…") + win.keyNote("print")
            onClicked: printDialog.open()
        }
        IconButton {
            id: bookmarkTool
            objectName: "bookmarkButton"
            parent: toolBank
            readonly property bool marked: (app.bookmarks, app.isBookmarked(app.pageNumber - 1))
            property bool offered: app.canBookmark
            iconName: marked ? "xqt-bookmark-filled" : "xqt-bookmark"
            checked: marked
            label: marked ? qsTr("Bookmarked") : qsTr("Bookmark")
            tip: marked ? qsTr("Remove the bookmark of this page") : qsTr("Bookmark this page")
            onClicked: app.toggleBookmark(app.pageNumber - 1)
        }
        IconButton {
            id: favouriteTool
            objectName: "favouriteButton"
            parent: toolBank
            property bool offered: app.canFavourite
            iconName: app.favourite ? "xqt-star-filled" : "xqt-star"
            checked: app.favourite
            label: qsTr("Favourite")
            tip: app.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
            onClicked: app.favourite = !app.favourite
        }
        // Reading, the replay of the writing, a milestone of the version history (where the document keeps versions),
        // the tags (qt/ui-rework)
        IconButton {
            id: readTool
            objectName: "readButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-book-open"
            label: qsTr("Read")
            tip: win.withKeys(qsTr("Read: Zen and read only, in full screen (the edges turn the pages)"), "read")
            onClicked: win.modes.startReading()
        }
        // Zen: only the page and a faint dot in the lower left corner (qt/docs/zen.md)
        IconButton {
            id: zenTool
            objectName: "zenButton"
            parent: toolBank
            iconName: "xqt-zen"
            label: qsTr("Zen")
            tip: win.withKeys(qsTr("Zen: only the page (the dot in the lower left corner brings the controls back)"), "zen")
            onClicked: win.modes.setZen(true)
        }
        IconButton {
            id: replayTool
            objectName: "replayButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-replay"
            label: qsTr("Replay")
            tip: qsTr("Replay the writing (how this document was written)")
            onClicked: app.timeline.start()
        }
        IconButton {
            id: milestoneTool
            objectName: "milestoneButton"
            parent: toolBank
            property bool offered: !win.textDoc && app.versions.on
            iconName: "xqt-flag"
            label: qsTr("Milestone")
            tip: win.withKeys(qsTr("Save with a message (a milestone of the version history)"), "saveWithMessage")
            onClicked: versionMessageDialog.openFor(-1)
        }
        IconButton {
            id: tagsTool
            objectName: "tagsButton"
            parent: toolBank
            iconName: "xqt-tag"
            label: qsTr("Tags")
            tip: qsTr("Tags of this document…")
            onClicked: documentTagsDialog.openFor(app.currentDocumentPath())
        }
    }
}
