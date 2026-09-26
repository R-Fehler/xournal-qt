// xournal-qt: main window (first usable version: one document, pen / highlighter / eraser / hand).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import XournalQt
import XournalQt.Canvas
import "Popups.js" as Popups

ApplicationWindow {
    id: win
    width: 1280
    height: 900
    // Maximized when the app starts (people make it smaller with the tiling of their desktop); the size above is
    // what it gets when it is not maximized, and what the tests use
    visibility: app.startMaximized ? Window.Maximized : Window.Windowed
    title: app.homeVisible ? (app.library.available ? app.library.name + " — Xournal Qt" : "Xournal Qt")
                           : (app.modified ? "• " : "") + app.title + (app.saving ? " (" + qsTr("saving…") + ")" : "")
                             + " — Xournal Qt"
    Material.theme: Material.Light
    Material.accent: Material.Indigo
    color: app.presenting ? "#000000" : "#5f6368"  // (presenting: black around the pages, like a projector)

    property var afterDiscardCheck: null
    /// The part of the window's top under the system's status bar (edge to edge on Android; set by main.cpp)
    property real safeTop: 0
    property bool sidebarShown: width >= 900
    property bool quitting: false
    readonly property string toolbarPosition: app.toolbarPosition
    readonly property bool sideToolbar: toolbarPosition === "left" || toolbarPosition === "right"
    /// Full screen: no tab strip, tool bar or page sidebar; a small square shows the current tool, a tap on it offers
    /// the tools (the same ones) and colors. The page / zoom pill stays.
    property bool fullScreenMode: false
    /// The window's state outside full screen (maximized or not), followed all the time rather than read when full
    /// screen starts: the platform may report the state late or in steps, and leaving full screen goes back to it
    property int windowedVisibility: app.startMaximized ? Window.Maximized : Window.Windowed
    property bool leavingFullScreen: false
    property bool remaximized: false
    onVisibilityChanged: {
        if (leavingFullScreen) {
            // The way back may pass through other states (the timer ends this). A compositor may also give back the
            // size from before it was maximized when full screen ends, even after it reported maximized: then ask
            // for maximized once more.
            if (visibility === Window.Windowed && windowedVisibility === Window.Maximized && !remaximized) {
                remaximized = true
                app.logWindow("maximized again (the compositor gave back the normal size)")
                showMaximized()
            }
        } else if (!fullScreenMode && (visibility === Window.Windowed || visibility === Window.Maximized)) {
            windowedVisibility = visibility
        }
    }
    Timer {  // ends the way back from full screen (a compositor may take a moment and several steps)
        id: leavingFullScreenTimer
        interval: 1500
        onTriggered: {
            win.leavingFullScreen = false
            // settled: the state it is in now is the window's state
            if (!win.fullScreenMode && (win.visibility === Window.Windowed || win.visibility === Window.Maximized))
                win.windowedVisibility = win.visibility
        }
    }
    onFullScreenModeChanged: {
        if (fullScreenMode) {
            leavingFullScreenTimer.stop()
            leavingFullScreen = false
            app.logWindow("full screen")
            showFullScreen()
        } else {
            quickTools.close()
            app.presenting = false  // (presenting is full screen)
            leavingFullScreen = true
            remaximized = false
            leavingFullScreenTimer.restart()
            app.logWindow("leave full screen to " + (windowedVisibility === Window.Maximized ? "maximized" : "normal"))
            if (windowedVisibility === Window.Maximized) showMaximized()
            else showNormal()
        }
    }
    /// No tool bar: in full screen, or when it was put away - the small tool square takes over
    readonly property bool noToolbar: fullScreenMode || app.toolbarHidden
    readonly property bool verticalTools: sideToolbar || noToolbar
    /// The document is a text file (a .md, a .txt): written with the keyboard, no ink tools (qt/docs/md-editor.md)
    readonly property bool textDoc: app.textDocument !== ""
    readonly property int toolColumns: noToolbar ? 6 : 2
    Connections {
        target: app
        function onHomeVisibleChanged() { if (app.homeVisible) win.fullScreenMode = false }
        function onPresentingChanged() { if (!app.presenting) win.presentClean = false }
    }

    /// Present from the current page: full screen, a page fills it. `clean`: without controls (below)
    function startPresenting(clean) {
        if (app.homeVisible) return
        presentClean = clean === true
        fullScreenMode = true
        app.presenting = true
    }
    /// Presenting without controls (Ctrl+F5, or holding the presentation button): only the page shows, no pen pill,
    /// no tool square, no other overlay; the faint mark in the lower left corner (presentCornerMark) or Ctrl+F5
    /// brings them back and hides them again. Every presentation starts as it is asked for: F5 with the controls.
    property bool presentClean: false
    readonly property bool cleanPage: app.presenting && presentClean
    onCleanPageChanged: if (cleanPage) quickTools.close()

    function withSavedChanges(action) {
        if (!app.modified) {
            action()
            return
        }
        afterDiscardCheck = action
        unsavedDialog.open()
    }
    /// Save as, with the type: "xopp" (Xournal notes), "pdf" (a PDF with notes, editable: a hybrid PDF), or "" for
    /// the document's own (app.saveFormat(): a hybrid PDF stays a PDF, a .xopp a .xopp; new documents, annotated PDFs
    /// and images are PDFs with notes in PDF files mode, else .xopp).
    function openSaveDialog(then, format) {
        if (win.textDoc && app.textEditable) {
            app.saveInBackground(then ? then : null)  // (a text file is saved as itself: no file types)
            return
        }
        setUpSaveDialog(format || "")
        saveDialog.afterSave = then
        saveDialog.open()
    }
    function setUpSaveDialog(format) {
        const pdf = format === "pdf" || (format !== "xopp" && app.saveFormat() === "pdf")
        // .xopp: upstream Xournal++'s suggestion, next to the annotated PDF ("lecture.pdf" -> "lecture.xopp"), else
        // the document's own path, else the default name in the library / the last used folder. PDF: the document's
        // own hybrid PDF, "lecture.notes.pdf" for an annotated PDF, else the .xopp suggestion as .pdf.
        const suggestion = (pdf ? app.suggestedHybridFile() : app.suggestedSaveFile()).toString()
        saveDialog.settingUp = true
        saveDialog.selectedNameFilter.index = pdf ? 1 : 0
        if (suggestion !== "") {
            saveDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            saveDialog.selectedFile = pdf ? suggestion : app.fileForFormat(suggestion, false)
        }
        saveDialog.settingUp = false
    }
    /// The Save as dialog was accepted: as a PDF with notes or as a .xopp (the extension typed wins). A document
    /// saved as "name.xopp" asks first what happens to that .xopp (unless the choice is stored).
    function saveChosen(url, pdfChosen, then) {
        if (!app.savesAsPdf(url, pdfChosen)) {
            app.saveAsInBackground(url, then ? then : null)
            return
        }
        const old = app.oldXoppToAsk()
        if (old === "") app.saveAsHybridInBackground(url, then ? then : null)
        else oldXoppDialog.ask(old, url, then)
    }
    function openExportDialog() {
        const suggestion = app.suggestedExportFile().toString()
        if (suggestion !== "") {
            exportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            exportDialog.selectedFile = suggestion
        }
        exportDialog.open()
    }
    /// Share → a PDF with notes of the current document (`file`: a PDF of the library instead), shown in the file
    /// manager or onto the clipboard. Saved first if needed; a .xopp is never turned into a PDF unasked.
    function sharePdfOf(file, toClipboard) {
        if (file !== "") {
            app.shareFile(file, toClipboard)
            return
        }
        const step = app.shareStep()
        if (step === "share" || step === "save") {
            app.sharePdf(toClipboard)
        } else if (step === "saveAs") {
            openSaveDialog(function() { app.sharePdf(toClipboard) }, "pdf")
        } else if (toClipboard) {
            app.sharePdfCopy("", true)  // (a .xopp: a PDF copy in the cache; the document stays as it is)
        } else {
            shareXoppDialog.open()
        }
    }
    // Share of a text file: the file itself; the open document's unsaved changes are saved first
    function shareTextFile(path, current, toClipboard) {
        if (current && app.textEditable && app.modified) {
            app.saveInBackground(function() { app.shareFile(path, toClipboard) })
        } else {
            app.shareFile(path, toClipboard)
        }
    }
    // "Open externally": a text file with unsaved changes is saved first (asked), so the other app sees them
    function openExternally() {
        if (app.textEditable && app.modified) {
            externalSaveDialog.open()
        } else {
            app.openExternally()
        }
    }
    function saveOrAsk(then) {
        if (app.savesWithoutDialog()) {
            // In the background: the window stays usable; `then` runs once the file is written (with its tab
            // current), not at all if that failed (a message says why)
            app.saveInBackground(then ? then : null)
        } else {
            openSaveDialog(then)
        }
    }

    // Close a tab; unsaved changes are asked about first (with that tab shown). A tab being saved waits for its save.
    function requestCloseTab(index) {
        if (app.tabSaving(index)) {
            app.whenSaved(index, function(i) { requestCloseTab(i) })
            return
        }
        if (!app.tabModified(index)) {
            app.closeTab(index)
            return
        }
        app.currentTab = index
        withSavedChanges(function() { app.closeTab(app.currentTab) })
    }
    // Close every document; unsaved changes are asked about one by one (after the saves that run).
    function closeAllTabs() {
        if (app.anySaving) {
            app.whenAllSaved(function() { closeAllTabs() })
            return
        }
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            app.closeAllTabs()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeAllTabs() })
    }
    // Quitting: the saves that run finish first (the window stays usable meanwhile), then the tabs with unsaved
    // changes are asked about one by one.
    property bool waitingToClose: false
    /// A web address chosen in the look-up menu (qt/docs/citations.md): asked first with the whole address, unless
    /// that was turned off (the menu showed it)
    function openWebAddress(url, purpose) {
        if (url === "") return
        if ((app.settings.revision, app.settings.get("webConfirm"))) {
            webConfirm.ask(url, purpose)
            return
        }
        if (app.citations.openWeb(url)) snackbar.show(qsTr("Opened %1 in the browser").arg(app.citations.hostOf(url)), false)
    }
    /// "Find this paper": the library searched for the title of a bibliography entry (qt/docs/citations.md)
    function findPaper(text) { findPaperSheet.openFor(text) }
    /// arXiv: a search by title, or one paper by its ID (qt/docs/citations.md)
    function arxivSearch(title) { arxivSheet.openSearch(title) }
    function arxivPaper(id) { arxivSheet.openId(id) }
    function closeWindow() {
        if (app.anySaving) {
            if (!waitingToClose) {
                waitingToClose = true
                app.whenAllSaved(function() { waitingToClose = false; closeWindow() })
            }
            return
        }
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            quitting = true
            win.close()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeWindow() })
    }

    onClosing: function(close) {
        if (!quitting && (app.modifiedTabs().length > 0 || app.anySaving)) {
            close.accepted = false
            closeWindow()
            return
        }
        if (app.secondaryWindow) {
            app.windowClosed()  // its documents (unsaved ones go back to the main window)
        }
    }

    Connections {
        target: app
        function onCloseWindowRequested() {  // its last document moved to another window
            win.quitting = true
            win.close()
        }
        function onRaiseRequested() {
            if (win.visibility === Window.Minimized) { app.logWindow("raise from minimized"); win.showNormal() }
            win.raise()
            win.requestActivate()
        }
    }

    header: Column {
      TabStrip {
        id: tabStrip
        width: parent.width
        topInset: win.safeTop
        visible: !win.fullScreenMode
        onCloseRequested: function(index) { requestCloseTab(index) }
        onOverviewRequested: tabOverview.open()
        onUndockRequested: function(index) { app.undockTab(index) }
        onDockRequested: function(index) { app.dockTab(index) }
        onShareRequested: function(index) {
            app.currentTab = index
            app.homeVisible = false
            shareDialog.openFor("")
        }
      }
      ToolBar {
        id: topTools
        width: parent.width
        visible: !app.homeVisible && !win.sideToolbar && !win.noToolbar
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: 56
      }
      // Markdown being written (a .md, Markdown on a page): its formatting tools (qt/docs/md-editor.md)
      MarkdownFormatBar {
        id: formatBar
        width: parent.width
        visible: !app.homeVisible && !app.presenting && !markdownPanel.visible
                 && (app.markdownOnPage || (app.textDocument === "markdown" && app.textEditable) || app.textNotes)
        format: app.markdownFormat
        onFormatRequested: function(action, arg) {
            app.formatMarkdown(action, arg)
            canvas.forceActiveFocus()
        }
        onTableRequested: {
            tableEditor.openFor(app.markdownTable(), function(cells, aligns) { app.writeMarkdownTable(cells, aligns) })
        }
      }
    }
    // The table editor of the formatting bar (the notes' canvas; the editor beside the page has its own)
    MarkdownTableEditor {
        id: tableEditor
        onClosed: if (formatBar.visible) canvas.forceActiveFocus()
    }

    // The tools: in the header (top), or a column at the left or right side (setting). One set of tools, moved.
    Rectangle {
        id: sideTools
        objectName: "sideTools"
        visible: !app.homeVisible && win.sideToolbar && !win.noToolbar
        width: visible ? 104 : 0
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: win.toolbarPosition === "right" ? parent.width - width : 0
        color: "#ffffff"
        Rectangle {  // the line towards the pages
            width: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: win.toolbarPosition === "right" ? 0 : parent.width - 1
            color: "#d5d8dc"
        }
    }
    Item {
        id: toolArea
        parent: win.noToolbar ? quickToolsHolder : (win.sideToolbar ? sideTools : topTools)
        anchors.fill: parent
        anchors.margins: win.verticalTools ? 4 : 0
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        Material.foreground: "#303030"
        // Scrolls when the window is too small for all tools (sideways on top, up and down at a side).
        Flickable {
            id: toolFlick
            anchors.fill: parent
            contentWidth: toolRow.width
            contentHeight: toolRow.height
            flickableDirection: win.verticalTools ? Flickable.VerticalFlick : Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: win.verticalTools ? contentHeight > height : contentWidth > width
            clip: true
        GridLayout {
            id: toolRow
            objectName: "toolRow"
            rows: win.verticalTools ? -1 : 1
            columns: win.verticalTools ? win.toolColumns : -1
            height: win.verticalTools ? Math.max(toolFlick.height, implicitHeight) : toolFlick.height
            width: win.verticalTools ? toolFlick.width : Math.max(toolFlick.width, implicitWidth)
            rowSpacing: 2
            columnSpacing: 2

            IconButton { iconName: "xopp-sidebar-page-preview"; tip: qsTr("Pages"); checked: sidebarShown; onClicked: sidebarShown = !sidebarShown }
            IconButton {
                objectName: "contentsButton"
                iconName: "xqt-toc"
                tip: qsTr("Contents with the pages of each chapter (Ctrl+Alt+O)")
                checked: contentsOverview.visible
                onClicked: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open()
            }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { iconName: "xopp-document-new"; tip: qsTr("New document (new tab)"); onClicked: app.newDocument() }
            IconButton { iconName: "xopp-document-open"; tip: qsTr("Open (in a new tab)"); onClicked: openDialog.open() }
            IconButton { iconName: "xopp-document-save"; tip: qsTr("Save"); onClicked: saveOrAsk(null) }
            // A .md, a text file, an image: in the app the system has for it (a code editor, …)
            // A .md: a copy as notes (a .xopp) to write on with the pen; the .md stays as it is
            IconButton {
                objectName: "editAsNotesButton"
                visible: app.textDocument === "markdown"
                iconName: "xqt-notebook-pen"
                tip: qsTr("Edit as notes: a copy to write on with the pen (saved as a .xopp; the .md stays)")
                onClicked: app.editAsNotes()
            }
            IconButton {
                objectName: "openExternallyButton"
                visible: app.canOpenExternally
                iconName: "xqt-external-link"
                tip: qsTr("Open externally (in the app the system has for this file)")
                onClicked: win.openExternally()
            }
            ToolSeparator { visible: !win.textDoc; orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { visible: !win.textDoc; iconName: "xopp-tool-pencil"; tip: qsTr("Pen"); checked: app.tool === "pen"; onClicked: app.selectTool("pen") }
            IconButton { visible: !win.textDoc; iconName: "xopp-tool-highlighter"; tip: qsTr("Highlighter"); checked: app.tool === "highlighter"; onClicked: app.selectTool("highlighter") }
            // The eraser: a tap takes it; tapped again, held or right-clicked, it offers how it erases
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "eraserButton"
                iconName: "xopp-tool-eraser"
                tip: qsTr("Eraser (tap again or hold: how it erases)")
                checked: app.tool === "eraser"
                onClicked: checked ? Popups.openAt(eraserMenu) : app.selectTool("eraser")
                onPressAndHold: Popups.openAt(eraserMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(eraserMenu, point.position) }
                }
                Menu {
                    id: eraserMenu
                    objectName: "eraserMenu"
                    component EraserItem: MenuItem {
                        property string mode
                        checkable: true
                        checked: (app.settings.revision, app.settings.get("eraserMode")) === mode
                        onTriggered: {
                            app.settings.set("eraserMode", mode)
                            app.selectTool("eraser")
                        }
                    }
                    EraserItem { objectName: "eraserStandard"; text: qsTr("Standard (cuts strokes)"); mode: "default" }
                    EraserItem { objectName: "eraserWholeStrokes"; text: qsTr("Whole strokes"); mode: "deleteStroke" }
                    EraserItem { objectName: "eraserWhiteout"; text: qsTr("Whiteout (paints white)"); mode: "whiteout" }
                }
            }
            IconButton { visible: !win.textDoc; iconName: "xopp-hand"; tip: qsTr("Hand"); checked: app.tool === "hand"; onClicked: app.selectTool("hand") }
            // Draw with the finger (one finger draws, two scroll and zoom): a switch, not a tool
            IconButton {
                visible: !win.textDoc
                objectName: "touchDrawingButton"
                iconName: "xopp-touch-drawing"
                tip: checked ? qsTr("The finger draws (two fingers scroll) - tap: the finger scrolls")
                             : qsTr("Draw with the finger (two fingers scroll)")
                checked: (app.settings.revision, app.settings.get("touchDrawing"))
                onClicked: app.settings.set("touchDrawing", !checked)
            }
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "textButton"
                iconName: "xopp-tool-text"
                tip: app.textMarkdown ? qsTr("Markdown text (tap to write; tap a text to edit it; hold for the font)")
                                      : qsTr("Text (tap to write; tap a text to edit it)")
                checked: app.tool === "text"
                onClicked: app.tool === "text" ? fontPopup.open() : app.selectTool("text")
                onPressAndHold: fontPopup.open()
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: fontPopup.open()
                }
                Popup {
                    id: fontPopup
                    x: win.toolbarPosition === "left" ? parent.width : win.toolbarPosition === "right" ? -width : 0
                    y: win.verticalTools ? 0 : parent.height
                    padding: 12
                    ColumnLayout {
                        spacing: 8
                        Label { text: qsTr("Font"); font.weight: Font.DemiBold }
                        ComboBox {
                            id: familyBox
                            Layout.preferredWidth: 260
                            model: fontPopup.opened ? app.fontFamilies() : []
                            currentIndex: model.indexOf(app.fontFamily)
                            onActivated: app.fontFamily = currentText
                        }
                        RowLayout {
                            Label { text: app.textMarkdown ? qsTr("Size of Markdown text") : qsTr("Size"); Layout.fillWidth: true }
                            SpinBox {
                                objectName: "fontSizeBox"
                                from: 4; to: 200
                                value: Math.round(app.textMarkdown ? app.markdownFontSize : app.fontSize)
                                editable: true
                                onValueModified: app.textMarkdown ? app.markdownFontSize = value : app.fontSize = value
                            }
                        }
                        // New text boxes are Markdown: shown formatted
                        Switch {
                            objectName: "textMarkdownSwitch"
                            text: qsTr("Markdown (shown formatted)")
                            checked: app.textMarkdown
                            onToggled: app.textMarkdown = checked
                        }
                    }
                }
            }
            // Writing on the page (a text box, Markdown, a text file): the emoji picker
            ToolButton {
                id: emojiButton
                objectName: "emojiButton"
                visible: canvas.textEditing
                text: "\u{1F642}"
                font.family: "Xournal Qt Emoji"
                font.pixelSize: 22
                implicitWidth: 48
                implicitHeight: 48
                focusPolicy: Qt.NoFocus  // (the text being written keeps the keys)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Emoji (or type : and a name, like :smile)")
                ToolTip.delay: 600
                background: Rectangle { radius: 10; color: emojiButton.pressed ? "#e8e8e8" : "transparent" }
                onClicked: canvasEmojiPicker.open()
                EmojiPicker {
                    id: canvasEmojiPicker
                    x: win.toolbarPosition === "left" ? parent.width : win.toolbarPosition === "right" ? -width : 0
                    y: win.verticalTools ? 0 : parent.height
                    onPicked: function(emoji) { close(); canvas.insertText(emoji) }
                }
            }
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "pdfTextButton"
                readonly property var icons: ({ "highlight": "xopp-select-pdf-text-ht", "underline": "xqt-underline",
                                                "strikethrough": "xqt-strikethrough", "select": "xopp-select-pdf-text-area" })
                iconName: icons[app.pdfTextMode] || "xopp-select-pdf-text-ht"
                tip: qsTr("Mark PDF text (drag over the text)")
                checked: app.tool === "selectPdfTextLinear" || app.tool === "selectPdfTextRect"
                onClicked: checked ? Popups.openAt(pdfTextMenu) : app.selectTool("selectPdfTextLinear")
                onPressAndHold: Popups.openAt(pdfTextMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(pdfTextMenu, point.position) }
                }
                Menu {
                    id: pdfTextMenu
                    objectName: "pdfTextMenu"
                    width: 320
                    component ModeItem: MenuItem {
                        property string mode
                        checkable: true
                        checked: app.pdfTextMode === mode
                        onTriggered: {
                            app.pdfTextMode = mode
                            if (app.tool !== "selectPdfTextRect") app.selectTool("selectPdfTextLinear")
                        }
                    }
                    ModeItem { text: qsTr("Highlight"); mode: "highlight" }
                    // The highlight color: three presets
                    RowLayout {
                        objectName: "highlightColors"
                        width: parent ? parent.width : implicitWidth
                        Label { text: qsTr("Highlight color"); Layout.leftMargin: 16; Layout.fillWidth: true; color: "#5f6368" }
                        HighlightColors { Layout.rightMargin: 8 }
                    }
                    ModeItem { text: qsTr("Underline"); mode: "underline" }
                    ModeItem { text: qsTr("Strike through"); mode: "strikethrough" }
                    ModeItem { text: qsTr("Select (then copy or mark)"); mode: "select" }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Select by area (columns, tables)")
                        checkable: true
                        checked: app.tool === "selectPdfTextRect"
                        onTriggered: app.selectTool(checked ? "selectPdfTextRect" : "selectPdfTextLinear")
                    }
                }
            }
            // Writing on the page with the keyboard: Markdown, formatted while typing (hold for its source beside the page).
            // DEPRECATED (2026-09-26): the text mode (TextFlowPanel, TextFlow) is no longer offered here; its code
            // stays for now (qt/docs/text-mode.md).
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                id: writeButton
                objectName: "textModeButton"
                property bool markdownMode: true  // (always; kept for the tests and QML that read it)
                iconName: "xqt-markdown"
                tip: qsTr("Markdown: write on the page, shown formatted (Ctrl+Alt+M). Hold for its source beside the page")
                checked: textFlowPanel.visible || markdownPanel.visible || app.markdownOnPage
                onClicked: {
                    if (textFlowPanel.visible) textFlowPanel.close(true)
                    else if (markdownPanel.visible) markdownPanel.close(true)
                    else if (app.markdownOnPage) app.endMarkdownOnPage()
                    else app.writeMarkdownOnPage()  // (formatted while typing, on the page)
                }
                onPressAndHold: Popups.openAt(writeMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(writeMenu, point.position) }
                }
                Menu {
                    id: writeMenu
                    objectName: "writeModeMenu"
                    width: 340
                    MenuItem {
                        objectName: "markdownItem"
                        text: qsTr("Markdown (shown formatted, on the page)")
                        checkable: true
                        checked: writeButton.markdownMode
                        onTriggered: { writeButton.markdownMode = true; app.writeMarkdownOnPage() }
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "markdownSourceItem"
                        text: qsTr("Markdown source beside the page")
                        onTriggered: {
                            const onPage = app.takeMarkdownFromPage()
                            if (onPage.page === undefined) markdownPanel.open()
                            else if (onPage.pageText) markdownPanel.open(onPage.page)
                            else markdownPanel.openBox(onPage.page, onPage.x, onPage.y)
                        }
                    }
                }
                Connections {
                    target: markdownPanel
                    function onVisibleChanged() { if (markdownPanel.visible) writeButton.markdownMode = true }
                }
            }
            IconButton { visible: !win.textDoc; objectName: "imageButton"; iconName: "xopp-tool-image"; tip: qsTr("Insert image"); onClicked: imageDialog.open() }
            IconButton { visible: !win.textDoc; objectName: "selectRectButton"; iconName: "xopp-select-rect"; tip: qsTr("Select (rectangle)"); checked: app.tool === "selectRect"; onClicked: app.selectTool("selectRect") }
            IconButton { visible: !win.textDoc; objectName: "lassoButton"; iconName: "xopp-select-lasso"; tip: qsTr("Select (lasso)"); checked: app.tool === "selectRegion"; onClicked: app.selectTool("selectRegion") }
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "shapeButton"
                readonly property var icons: ({
                    "line": "xopp-draw-line", "rectangle": "xopp-draw-rect", "ellipse": "xopp-draw-ellipse",
                    "arrow": "xopp-draw-arrow", "doubleArrow": "xopp-draw-double-arrow",
                    "drawCoordinateSystem": "xopp-draw-coordinate-system", "strokeRecognizer": "xopp-shape-recognizer"
                })
                iconName: icons[app.drawingType] || "xqt-shapes"
                tip: qsTr("Shapes")
                checked: (app.tool === "pen" || app.tool === "highlighter") && app.drawingType !== "default"
                onClicked: Popups.openAt(shapeMenu)
                Menu {
                    id: shapeMenu
                    component ShapeItem: MenuItem {
                        property string type
                        checkable: true
                        checked: app.drawingType === type
                        onTriggered: app.drawingType = type
                    }
                    ShapeItem { text: qsTr("Freehand"); type: "default" }
                    ShapeItem { text: qsTr("Recognize shapes (draw, then it straightens)"); type: "strokeRecognizer" }
                    MenuSeparator {}
                    ShapeItem { text: qsTr("Line"); type: "line" }
                    ShapeItem { text: qsTr("Rectangle"); type: "rectangle" }
                    ShapeItem { text: qsTr("Ellipse"); type: "ellipse" }
                    ShapeItem { text: qsTr("Arrow"); type: "arrow" }
                    ShapeItem { text: qsTr("Double arrow"); type: "doubleArrow" }
                    ShapeItem { text: qsTr("Coordinate system"); type: "drawCoordinateSystem" }
                    MenuSeparator {}
                    // On the page itself, not a way of drawing: drag it where it is needed, draw along its edge
                    MenuItem {
                        objectName: "setsquareItem"
                        text: qsTr("Setsquare (draw along its edge)")
                        checkable: true
                        checked: app.geometryTool === "setsquare"
                        onTriggered: app.toggleSetsquare()
                    }
                    MenuItem {
                        objectName: "compassItem"
                        text: qsTr("Compass (draw circles around it)")
                        checkable: true
                        checked: app.geometryTool === "compass"
                        onTriggered: app.toggleCompass()
                    }
                    // A paper note on the page: write on it, move it, cover answers with it (qt/docs/sticky-notes.md)
                    MenuItem {
                        objectName: "stickyNoteItem"
                        text: qsTr("Sticky note (write on it, cover with it)")
                        onTriggered: app.insertStickyNote()
                    }
                    MenuSeparator {}
                    // Corners of shapes and moved selections jump onto the half-centimetre grid (upstream's tool bar
                    // toggle; also in the settings)
                    MenuItem {
                        objectName: "snapGridItem"
                        text: qsTr("Snap to the grid")
                        checkable: true
                        checked: (app.settings.revision, app.settings.get("snapGrid"))
                        onTriggered: app.settings.set("snapGrid", checked)
                    }
                }
            }
            ToolSeparator { visible: !win.textDoc; orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            // The preset colors: tap to use; press and hold / right click to remove; + adds one.
            Repeater {
                model: app.toolbarColors
                delegate: AbstractButton {
                    visible: !win.textDoc
                    id: swatch
                    required property color modelData
                    required property int index
                    objectName: "colorSwatch"
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setColor(modelData)
                    onPressAndHold: Popups.openAt(swatchMenu)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                        onTapped: function(point) { Popups.openAt(swatchMenu, point.position) }
                    }
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: swatch.modelData
                            border.width: Qt.colorEqual(app.color, swatch.modelData) ? 3 : 1
                            border.color: Qt.colorEqual(app.color, swatch.modelData) ? Material.accentColor : "#9e9e9e"
                        }
                    }
                    Menu {
                        id: swatchMenu
                        MenuItem { text: qsTr("Remove from the tool bar"); onTriggered: app.removeToolbarColor(swatch.index) }
                        MenuItem { text: qsTr("Add a color…"); onTriggered: colorDialog.open() }
                        MenuItem { text: qsTr("Default colors"); onTriggered: app.resetToolbarColors() }
                    }
                }
            }
            AbstractButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "addColorButton"
                implicitWidth: 40
                implicitHeight: 44
                onClicked: colorDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add a color (press and hold a color to remove it)")
                ToolTip.delay: 600
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 28; height: 28; radius: 14
                        color: "transparent"
                        border.width: 1
                        border.color: "#9e9e9e"
                        Label { anchors.centerIn: parent; text: "+"; font.pixelSize: 18; color: "#5f6368" }
                    }
                }
            }
            ToolSeparator { visible: !win.textDoc; orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            Repeater {
                model: [ { size: 1, dot: 6 }, { size: 2, dot: 10 }, { size: 3, dot: 15 }, { size: 4, dot: 21 } ]
                delegate: AbstractButton {
                    visible: !win.textDoc
                    required property var modelData
                    objectName: "sizeButton" + modelData.size
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setSize(modelData.size)
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 32; height: 32; radius: 6
                            color: app.size === modelData.size ? "#e0e3f5" : "transparent"
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: modelData.dot; height: modelData.dot; radius: modelData.dot / 2
                            color: "#303030"
                        }
                    }
                }
            }
            // The fifth width: the tool's own, adjustable (tap it again or press and hold)
            AbstractButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                id: customSizeButton
                objectName: "customSizeButton"
                implicitWidth: 40
                implicitHeight: 44
                enabled: app.customWidth > 0
                opacity: enabled ? 1 : 0.4
                // As big as the width compared to the very thick size
                readonly property real dot: {
                    const ref = app.tool, thick = app.sizeWidth(4)
                    return thick > 0 ? Math.max(3, Math.min(26, Math.sqrt(app.customWidth / thick) * 21)) : 12
                }
                onClicked: app.size === 5 ? customSizePopup.open() : app.setSize(5)
                onPressAndHold: { app.setSize(5); customSizePopup.open() }
                ToolTip.visible: hovered && !customSizePopup.visible
                ToolTip.text: qsTr("Own width (%1 mm): tap again or press and hold to change it").arg(customSizePopup.mmText)
                ToolTip.delay: 600
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 32; height: 32; radius: 6
                        color: app.size === 5 ? "#e0e3f5" : "transparent"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: customSizeButton.dot; height: width; radius: width / 2
                        color: "transparent"
                        border.width: Math.min(width / 2, 2.5)
                        border.color: "#303030"
                    }
                }
                CustomWidthPopup {
                    id: customSizePopup
                    side: win.fullScreenMode ? "left" : win.toolbarPosition
                }
            }
            Item { Layout.fillWidth: !win.verticalTools; Layout.fillHeight: win.verticalTools; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1 }
            IconButton {
                visible: !win.textDoc  // (a text file: no ink, no pages to add)
                objectName: "addPageButton"
                iconName: "xopp-page-add"
                tip: qsTr("Add a page after the current one (press and hold: background, size, several pages)")
                onClicked: app.addPageAfterCurrent()
                onPressAndHold: insertPagesDialog.openAt(app.pageNumber)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: insertPagesDialog.openAt(app.pageNumber)
                }
            }
            ToolSeparator { visible: !win.textDoc; orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { objectName: "searchButton"; iconName: "xqt-search"; tip: qsTr("Search (Ctrl+F)"); checked: searchBar.visible; onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar() }
            // Full screen (F11). Not inside full screen itself: the tools there end with "Leave full screen"
            IconButton {
                objectName: "fullScreenButton"
                visible: !win.fullScreenMode
                iconName: "xopp-fullscreen"
                tip: qsTr("Full screen (F11)")
                onClicked: win.fullScreenMode = true
            }
            // Present: full screen, page by page (from the current page)
            // (held or right-clicked: without controls, only the page)
            IconButton {
                objectName: "presentButton"
                visible: !win.fullScreenMode
                iconName: "xopp-presentation-mode"
                tip: qsTr("Present (F5; hold: only the page, Ctrl+F5)")
                onClicked: win.startPresenting()
                onPressAndHold: win.startPresenting(true)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: win.startPresenting(true)
                }
            }
            IconButton { objectName: "settingsButton"; iconName: "xqt-settings"; tip: qsTr("Settings (Ctrl+,)"); onClicked: settingsPage.open() }
            IconButton {
                objectName: "moreButton"
                iconName: "xqt-more"
                tip: qsTr("More")
                onClicked: Popups.openAt(moreMenu)
                Menu {
                    id: moreMenu
                    objectName: "moreMenu"
                    MenuItem { visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Save as…"); onTriggered: openSaveDialog(null) }
                    MenuItem { objectName: "shareItem"; text: qsTr("Share…"); onTriggered: shareDialog.openFor("") }
                    MenuItem { objectName: "copyPageLinkItem"; text: qsTr("Copy link to this page"); onTriggered: app.copyPageLink(-1) }
                    MenuItem { objectName: "linkedFromItem"; text: qsTr("Linked from…"); onTriggered: backlinksDialog.show() }
                    MenuItem {
                        objectName: "editAsNotesItem"
                        visible: app.textDocument === "markdown"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Edit as notes (to write on with the pen)")
                        onTriggered: app.editAsNotes()
                    }
                    // Text documents as PDF (qt/docs/md-pdf.md): a .md as a new PDF text document; the Markdown of
                    // a document's page texts as a .md
                    MenuItem {
                        objectName: "openAsPdfDocumentItem"
                        visible: app.textDocument === "markdown"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Open as PDF document")
                        onTriggered: app.openAsPdfDocument()
                    }
                    MenuItem {
                        objectName: "unusedImagesItem"
                        visible: app.textDocument === "markdown"
                        height: visible ? implicitHeight : 0
                        text: qsTr("Remove unused images…")
                        onTriggered: unusedImagesDialog.show()
                    }
                    MenuItem {
                        objectName: "exportMarkdownItem"
                        visible: !win.textDoc && app.hasMarkdownText
                        height: visible ? implicitHeight : 0
                        text: qsTr("Export as Markdown")
                        onTriggered: win.exportMarkdown()
                    }
                    MenuItem {
                        objectName: "openExternallyItem"
                        visible: app.canOpenExternally
                        height: visible ? implicitHeight : 0
                        text: qsTr("Open externally")
                        onTriggered: win.openExternally()
                    }
                    MenuItem {
                        objectName: "editAnywayItem"
                        visible: app.canEditAnyway
                        height: visible ? implicitHeight : 0
                        text: qsTr("Edit anyway (as plain text)…")
                        onTriggered: app.editAnyway(false)
                    }
                    // A plain PDF: the notes drawn into the pages (a PDF with notes that stays editable is a type of
                    // Save as)
                    MenuItem { objectName: "exportPdfItem"; text: qsTr("Export as plain PDF…"); onTriggered: openExportDialog() }
                    // A PDF/A for keeping: the ink merged into the pages, the Xournal data inside
                    MenuItem {
                        objectName: "exportArchiveItem"
                        visible: !win.textDoc
                        height: visible ? implicitHeight : 0
                        text: qsTr("Export for the archive…")
                        onTriggered: archiveDialog.openFor("")
                    }
                    MenuItem { objectName: "printItem"; text: qsTr("Print… (Ctrl+P)"); onTriggered: printDialog.open() }
                    MenuItem { visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Start a chapter here…"); onTriggered: chapterDialog.openFor(app.pageNumber - 1) }
                    MenuSeparator {}
                    MenuItem { visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Insert image…"); onTriggered: imageDialog.open() }
                    MenuItem { objectName: "insertStickyNoteItem"; visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Insert sticky note"); onTriggered: app.insertStickyNote() }
                    MenuItem { visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Insert pages…"); onTriggered: insertPagesDialog.openAt(app.pageNumber) }
                    MenuItem { visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Background of this page…"); onTriggered: backgroundDialog.openFor([app.pageNumber - 1]) }
                    // Writing space beside the slides of all pages (qt/docs/note-space.md)
                    MenuItem { objectName: "noteSpaceItem"; visible: !win.textDoc; height: visible ? implicitHeight : 0; text: qsTr("Space for notes…"); onTriggered: noteSpaceDialog.openFor([app.pageNumber - 1], true) }
                    MenuItem { text: qsTr("All pages"); onTriggered: pageGrid.open() }
                    MenuItem { text: qsTr("All open documents"); onTriggered: tabOverview.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Settings"); onTriggered: settingsPage.open() }
                    MenuItem {
                        objectName: "hideToolbarItem"
                        text: app.toolbarHidden ? qsTr("Show the tool bar") : qsTr("Hide the tool bar")
                        onTriggered: app.toolbarHidden = !app.toolbarHidden
                    }
                    MenuItem {
                        objectName: "presentItem"
                        text: qsTr("Present (F5)")
                        onTriggered: win.startPresenting()
                    }
                    MenuItem {
                        objectName: "presentCleanItem"
                        text: qsTr("Present without controls (Ctrl+F5)")
                        onTriggered: win.startPresenting(true)
                    }
                    MenuItem {
                        objectName: "fullScreenItem"
                        text: win.fullScreenMode ? qsTr("Leave full screen (F11)") : qsTr("Full screen (F11)")
                        onTriggered: win.fullScreenMode = !win.fullScreenMode
                    }
                    Menu {
                        title: qsTr("Tool bar position")
                        component PositionItem: MenuItem {
                            property string position
                            checkable: true
                            checked: app.toolbarPosition === position
                            onTriggered: app.toolbarPosition = position
                        }
                        PositionItem { text: qsTr("Top"); position: "top" }
                        PositionItem { text: qsTr("Left"); position: "left" }
                        PositionItem { text: qsTr("Right"); position: "right" }
                    }
                }
            }
        }
        }
    }

    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: win.toolbarPosition === "left" ? sideTools.right : parent.left
        width: 210
        visible: sidebarShown && !win.fullScreenMode
    }

    DocumentCanvas {
        id: canvas
        objectName: "canvas"
        // The canvas area, or the main document's side of it when the tab shows a reference beside it
        x: referenceSplit.x + referenceSplit.mainX
        y: referenceSplit.y
        width: referenceSplit.mainWidth
        height: referenceSplit.height
        clip: true  // zoomed-in pages must not paint over the sidebar
        view: app.view

        // Picture files dropped on Markdown being written (a .md, a text document, Markdown on a page): saved with
        // the document and linked at the cursor (qt/docs/md-images.md)
        DropArea {
            objectName: "markdownDropArea"
            anchors.fill: parent
            enabled: formatBar.visible
            keys: ["text/uri-list"]
            onDropped: function(drop) {
                if (drop.hasUrls && app.insertMarkdownImages(drop.urls))
                    drop.accept(Qt.CopyAction)
            }
        }
        // The mouse rests on a formula of a Markdown text that cannot be drawn (shown as its source, in red): why
        ToolTip {
            objectName: "mathErrorTip"
            visible: canvas.mathError !== ""
            text: qsTr("This formula cannot be drawn: %1").arg(canvas.mathError)
            x: Math.max(0, Math.min(canvas.mathErrorRect.x, canvas.width - width))
            y: canvas.mathErrorRect.y + canvas.mathErrorRect.height + 4
        }
    }
    // ":smi" typed on the page: the emoji suggested (Up / Down / Enter go to the canvas's editor; a tap chooses).
    // Beside the canvas, not in it: the canvas takes the presses on its own items.
    EmojiSuggestions {
        objectName: "emojiSuggestions"
        model: canvas.emojiCompletions
        current: canvas.emojiCompletionIndex
        cursor: Qt.rect(canvas.x + canvas.emojiCompletionRect.x, canvas.y + canvas.emojiCompletionRect.y,
                        canvas.emojiCompletionRect.width, canvas.emojiCompletionRect.height)
        onChosen: function(index) { canvas.chooseEmojiCompletion(index) }
    }
    // Reference mode: another document beside this one (the canvas area is split)
    ReferenceSplit {
        id: referenceSplit
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: textFlowPanel.visible ? textFlowPanel.left
                       : markdownPanel.visible ? markdownPanel.left
                       : (win.toolbarPosition === "right" ? sideTools.left : parent.right)
        anchors.left: sidebar.visible ? sidebar.right : (win.toolbarPosition === "left" ? sideTools.right : parent.left)
    }

    // A Markdown file shown read-only for now, an image to write on: what that means (closed for this tab with ×).
    Pane {
        id: shownFileNote
        objectName: "shownFileNote"
        property string closedFor: ""
        visible: app.shownFileNote !== "" && closedFor !== app.title && !pageGrid.visible && !contentsOverview.visible
                 && !win.cleanPage
        // (bottom left: the search bar is at the top, the page and zoom pill at the bottom right)
        anchors.bottom: canvas.bottom
        anchors.left: canvas.left
        anchors.bottomMargin: 24
        anchors.leftMargin: app.presenting ? 56 : 24  // (presenting: beside the corner mark)
        width: Math.max(160, Math.min(canvas.width - viewPill.width - 80, 560))
        padding: 2
        leftPadding: 14
        background: Rectangle {
            radius: 12
            color: "#f2fff8e1"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            width: parent.width
            spacing: 4
            Label {
                objectName: "shownFileNoteText"
                Layout.fillWidth: true
                text: app.shownFileNote
                wrapMode: Text.Wrap
                color: "#4a3b00"
                font.pixelSize: 13
            }
            Button {
                objectName: "editAnywayButton"
                visible: app.canEditAnyway
                flat: true
                text: qsTr("Edit anyway")
                onClicked: app.editAnyway(false)
            }
            ToolButton {
                objectName: "shownFileNoteClose"
                text: "×"
                font.pixelSize: 18
                implicitWidth: 36
                onClicked: shownFileNote.closedFor = app.title
            }
        }
    }

    // Page and zoom status, floating over the canvas.
    Pane {
        id: viewPill
        objectName: "viewPill"
        // also in full screen; presenting only the page number, for a moment (presentPageIndicator)
        visible: !pageGrid.visible && !contentsOverview.visible && !app.presenting
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 24
        padding: 2
        leftPadding: 14
        rightPadding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            IconButton {
                objectName: "undoButton"
                iconName: "xopp-edit-undo"
                tip: qsTr("Undo (Ctrl+Z)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canUndo
                onClicked: app.undo()
            }
            IconButton {
                objectName: "redoButton"
                iconName: "xopp-edit-redo"
                tip: qsTr("Redo (Ctrl+Y)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canRedo
                onClicked: app.redo()
            }
            ToolSeparator {}
            IconButton {
                objectName: "layoutButton"
                iconName: "xqt-columns"
                tip: qsTr("Page layout")
                implicitWidth: 40; implicitHeight: 40
                // A tap switches between one page and two side by side; the rest is in the menu (press and hold)
                onClicked: {
                    if (app.pairedPages) {
                        app.pairedPages = false
                        app.viewColumns = 1
                    } else {
                        app.viewColumns = 2
                        app.pairsOffset = 0
                        app.pairedPages = true
                    }
                }
                onPressAndHold: Popups.openAt(layoutMenu)
                TapHandler {  // right click does what press and hold does
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(layoutMenu, point.position) }
                }
                Menu {
                    id: layoutMenu
                    objectName: "layoutMenu"
                    // A text file (.md, .txt): A4 pages, or one continuous page that grows with the text
                    MenuItem {
                        objectName: "textPagesItem"
                        visible: win.textDoc && app.textEditable
                        height: visible ? implicitHeight : 0
                        text: qsTr("Text on pages")
                        checkable: true
                        checked: !app.textContinuous
                        onTriggered: app.textContinuous = false
                    }
                    MenuItem {
                        objectName: "textContinuousItem"
                        visible: win.textDoc && app.textEditable
                        height: visible ? implicitHeight : 0
                        text: qsTr("Text on one continuous page")
                        checkable: true
                        checked: app.textContinuous
                        onTriggered: app.textContinuous = true
                    }
                    MenuSeparator { visible: win.textDoc && app.textEditable; height: visible ? implicitHeight : 0 }
                    MenuItem {
                        objectName: "onePageItem"
                        text: app.horizontalScrolling ? qsTr("Pages in one row") : qsTr("One page per row")
                        checkable: true
                        checked: !app.pairedPages && (app.horizontalScrolling ? app.viewRows === 1 : app.viewColumns === 1)
                        onTriggered: {
                            app.pairedPages = false
                            if (app.horizontalScrolling) app.viewRows = 1
                            else app.viewColumns = 1
                        }
                    }
                    MenuItem {
                        text: qsTr("Two pages side by side")
                        checkable: true
                        checked: app.pairedPages && app.pairsOffset === 0
                        onTriggered: { app.viewColumns = 2; app.pairsOffset = 0; app.pairedPages = true }
                    }
                    MenuItem {
                        text: qsTr("Book (cover page alone)")
                        checkable: true
                        checked: app.pairedPages && app.pairsOffset === 1
                        onTriggered: { app.viewColumns = 2; app.pairsOffset = 1; app.pairedPages = true }
                    }
                    MenuSeparator {}
                    // N columns (scrolling sideways: N rows)
                    RowLayout {
                        width: parent ? parent.width : implicitWidth
                        readonly property bool rows: app.horizontalScrolling
                        readonly property int count: rows ? app.viewRows : app.viewColumns
                        function setCount(n) {
                            if (rows) { app.viewRows = n; return }
                            app.pairedPages = false
                            app.viewColumns = n
                        }
                        Label { text: parent.rows ? qsTr("Rows") : qsTr("Columns"); Layout.leftMargin: 16; Layout.fillWidth: true }
                        ToolButton {
                            objectName: "fewerColumnsButton"
                            text: "−"; font.pixelSize: 20
                            enabled: parent.count > 1
                            onClicked: parent.setCount(parent.count - 1)
                        }
                        Label {
                            objectName: "columnsLabel"
                            text: parent.count
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            Layout.minimumWidth: 20
                        }
                        ToolButton {
                            objectName: "moreColumnsButton"
                            text: "+"; font.pixelSize: 20
                            enabled: parent.count < 8
                            onClicked: parent.setCount(parent.count + 1)
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "sidewaysItem"
                        text: qsTr("Scroll sideways")
                        checkable: true
                        checked: app.horizontalScrolling
                        onTriggered: app.horizontalScrolling = !app.horizontalScrolling
                    }
                    MenuItem {
                        objectName: "snapPagesItem"
                        text: qsTr("Stop on whole pages")
                        enabled: app.horizontalScrolling
                        checkable: true
                        checked: app.snapPages
                        onTriggered: app.snapPages = !app.snapPages
                    }
                }
            }
            ToolSeparator {}
            IconButton {
                objectName: "pageGridButton"
                iconName: "xqt-pages-grid"
                tip: qsTr("All pages (Ctrl+Alt+G)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                onClicked: pageGrid.open()
            }
            // Scrolling sideways: the previous and the next page on either side of the page number
            IconButton {
                objectName: "previousPageButton"
                visible: app.horizontalScrolling
                iconName: "xqt-chevron-left"
                tip: qsTr("Previous page (←, Page Up)")
                implicitWidth: 36; implicitHeight: 40
                icon.width: 20; icon.height: 20
                enabled: app.pageNumber > 1
                onClicked: app.previousPage()
            }
            Label {
                objectName: "pageNumberLabel"
                text: app.pageNumber + " / " + app.pageCount
                color: "#505050"
                Layout.rightMargin: app.horizontalScrolling ? 0 : 6
            }
            // Only on a page with sticky notes: hide them all (to see what they cover) and show them again
            IconButton {
                objectName: "pageNotesButton"
                visible: app.pageHasNotes && !win.textDoc
                iconName: app.pageNotesHidden ? "xqt-eye-off" : "xqt-eye"
                tip: app.pageNotesHidden ? qsTr("Show the sticky notes of this page") : qsTr("Hide the sticky notes of this page")
                implicitWidth: 36; implicitHeight: 40
                icon.width: 20; icon.height: 20
                onClicked: app.pageNotesHidden = !app.pageNotesHidden
            }
            IconButton {
                objectName: "nextPageButton"
                visible: app.horizontalScrolling
                iconName: "xqt-chevron-right"
                tip: qsTr("Next page (→, Page Down)")
                implicitWidth: 36; implicitHeight: 40
                icon.width: 20; icon.height: 20
                enabled: app.pageNumber < app.pageCount
                onClicked: app.nextPage()
            }
            ToolSeparator {}
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomOut() }
            ToolButton {
                objectName: "zoomButton"
                text: app.zoomPercent + " %"
                implicitWidth: 72
                onClicked: app.fitWidth()
                onPressAndHold: Popups.openAt(fitMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(fitMenu, point.position) }
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit the width (press and hold: more)")
                ToolTip.delay: 600
                Menu {
                    id: fitMenu
                    objectName: "fitMenu"
                    MenuItem { text: qsTr("Fit the width (Ctrl+0)"); onTriggered: app.fitWidth() }
                    MenuItem {
                        objectName: "realSizeItem"
                        text: qsTr("Real size, 100 % (Ctrl+1)")
                        onTriggered: app.zoomToRealSize()
                    }
                    MenuItem { text: qsTr("Fit the height"); onTriggered: app.fitHeight() }
                    MenuItem {
                        objectName: "fitPageItem"
                        text: app.currentPageDiffers ? qsTr("Fit this page (its size differs)") : qsTr("Fit the whole page")
                        onTriggered: app.fitPage()
                    }
                }
            }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomIn() }
        }
    }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
    }

    // While a tab is dragged off the strip: what happens when it is let go
    Rectangle {
        objectName: "tabDragHint"
        visible: tabStrip.dragIndex >= 0
        z: 60
        anchors.horizontalCenter: parent.horizontalCenter
        y: 12
        width: hintRow.implicitWidth + 28
        height: 44
        radius: 22
        readonly property bool willMove: tabStrip.dragDistance > tabStrip.undockDistance
        color: willMove ? Material.accentColor : "#e8eaed"
        border.width: 1
        border.color: willMove ? Material.accentColor : "#c9ccd1"
        opacity: 0.96
        RowLayout {
            id: hintRow
            anchors.centerIn: parent
            spacing: 8
            Label {
                text: app.secondaryWindow ? (parent.parent.willMove ? qsTr("Let go: back to the main window")
                                                                    : qsTr("Drag down: back to the main window"))
                                          : (parent.parent.willMove ? qsTr("Let go: a window of its own")
                                                                    : qsTr("Drag down: a window of its own"))
                color: parent.parent.willMove ? "#ffffff" : "#3c4043"
                font.weight: Font.DemiBold
            }
        }
    }
    // Text mode: beside the pages (right), the canvas makes room
    TextFlowPanel {
        id: textFlowPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: visible ? Math.min(Math.max(360, win.width * 0.38), 600) : 0
    }
    // Markdown box: the same place
    MarkdownPanel {
        id: markdownPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: visible ? Math.min(Math.max(360, win.width * 0.38), 600) : 0
    }
    Connections {
        target: app
        // The text tool tapped a Markdown box
        function onMarkdownRequested(page) {
            if (textFlowPanel.visible) textFlowPanel.close(true)
            if (!markdownPanel.visible || app.markdownPage !== page || !app.markdownIsPageText) markdownPanel.open(page)
        }
        // A Markdown text box (or a place for a new one)
        function onMarkdownBoxRequested(page, x, y) {
            if (textFlowPanel.visible) textFlowPanel.close(true)
            markdownPanel.openBox(page, x, y)
        }
    }
    ContentsOverview {
        id: contentsOverview
        anchors.fill: canvas
        onVisibleChanged: if (visible && pageGrid.visible) pageGrid.close()
    }
    Connections {
        target: pageGrid
        function onVisibleChanged() { if (pageGrid.visible && contentsOverview.visible) contentsOverview.close() }
    }

    // Actions on the selected elements (select tools).
    SelectionPill {
        id: selectionBar
        objectName: "selectionBar"
        canvasItem: canvas
        hidden: pageGrid.visible
    }

    // The selected sticky note: its color, cover mode, delete.
    NotePill {
        id: notePill
        objectName: "notePill"
        canvasItem: canvas
        hidden: pageGrid.visible
    }

    // Selected PDF text: mark or copy it (at the text, going along with it).
    PdfTextPill {
        id: pdfTextBar
        objectName: "pdfTextBar"
        canvasItem: canvas
        hidden: pageGrid.visible || contentsOverview.visible
    }

    // Back / forward after jumps (links, page grid, sidebar)
    Pane {
        id: navPill
        objectName: "navPill"
        visible: (app.canGoBack || app.canGoForward) && !pageGrid.visible && !win.cleanPage
        anchors.left: canvas.left
        anchors.bottom: canvas.bottom
        anchors.leftMargin: app.presenting ? 56 : 20  // (presenting: beside the corner mark)
        anchors.bottomMargin: 24
        padding: 2
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f7fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            IconButton {
                objectName: "navBack"
                iconName: "xopp-navigate-back"
                tip: qsTr("Back to where you were (Alt+Left)")
                enabled: app.canGoBack
                onClicked: app.navigateBack()
            }
            IconButton {
                objectName: "navForward"
                iconName: "xopp-navigate-forward"
                tip: qsTr("Forward (Alt+Right)")
                enabled: app.canGoForward
                onClicked: app.navigateForward()
            }
            IconButton {
                iconName: "xqt-close"
                tip: qsTr("Forget these places")
                implicitWidth: 36
                onClicked: app.clearNavigation()
            }
        }
    }

    // A tapped link: open it / go to the page (not at once: a tap can be a mistake). A link to a document
    // (qt/docs/links.md) offers a new tab, the reference or "here", unless a choice was remembered (Settings). A page
    // or a place of this document offers going there, or showing it in the reference: a second view of the document
    // beside it (qt/self-reference).
    Popup {
        id: linkPopup
        objectName: "linkPopup"
        property string uri
        property int page: -1
        property var doc: null  // app.documentLink(uri) of a link to a document, else null
        property bool inDocument: false  // a link to a place of this document (`#page=…`, a chapter)
        padding: 6
        function follow(how) {
            if (linkRemember.checked) app.settings.set("linkOpening", how)
            const uri = linkPopup.uri
            linkPopup.close()
            app.followDocumentLink(uri, how)
        }
        Connections {
            target: app
            function onLinkTapped(uri, page, rect) {
                const info = uri !== "" ? app.documentLink(uri) : null
                if (info && info.document && info.found && !info.here) {
                    const how = (app.settings.revision, app.settings.get("linkOpening"))
                    if (how !== "ask") {
                        app.followDocumentLink(uri, how)
                        return
                    }
                }
                const inDocument = !!(info && info.document && info.here)
                if (inDocument) {  // (a place in this document: there, or in the reference if that was chosen)
                    const how = (app.settings.revision, app.settings.get("linkOpening"))
                    if (how !== "ask") {
                        app.followDocumentLink(uri, how === "reference" ? "reference" : "here")
                        return
                    }
                }
                linkPopup.uri = uri
                linkPopup.page = page
                linkPopup.doc = info && info.document ? info : null
                linkPopup.inDocument = inDocument
                linkRemember.checked = false
                linkPopup.x = Math.max(8, Math.min(canvas.x + rect.x, win.width - linkPopup.width - 8))
                linkPopup.y = canvas.y + rect.y + rect.height + 6
                if (linkPopup.y + 120 > win.height) linkPopup.y = canvas.y + rect.y - (linkPopup.doc ? 120 : 60)
                linkPopup.open()
            }
        }
        ColumnLayout {
            spacing: 2
            RowLayout {
                spacing: 4
                Image { source: app.iconUrl("xqt-link"); sourceSize.width: 18; sourceSize.height: 18; Layout.leftMargin: 6 }
                Label {
                    objectName: "linkLabel"
                    visible: linkPopup.uri !== ""
                    text: !linkPopup.doc ? linkPopup.uri
                          : linkPopup.inDocument ? (linkPopup.doc.place !== "" ? linkPopup.doc.place : linkPopup.uri)
                          : !linkPopup.doc.found ? qsTr("%1 was not found").arg(linkPopup.doc.name)
                          : linkPopup.doc.place !== "" ? qsTr("%1, %2").arg(linkPopup.doc.name).arg(linkPopup.doc.place)
                                                       : linkPopup.doc.name
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 320
                    Layout.rightMargin: linkPopup.doc ? 6 : 0
                }
                Button {
                    objectName: "linkButton"
                    visible: !linkPopup.doc || linkPopup.inDocument
                    flat: true
                    text: linkPopup.inDocument ? qsTr("Go there")
                          : linkPopup.uri !== "" ? qsTr("Open") : linkPopup.page >= 0 ? qsTr("Go to page %1").arg(linkPopup.page + 1)
                                                                                    : qsTr("Page not in this document")
                    enabled: linkPopup.uri !== "" || linkPopup.page >= 0
                    onClicked: {
                        const uri = linkPopup.uri
                        linkPopup.close()
                        if (linkPopup.inDocument) app.followDocumentLink(uri, "here")
                        else if (uri !== "") app.openLink(uri)
                        else app.jumpToPage(linkPopup.page)
                    }
                }
                // A page of this document: in a second view of it beside it (qt/self-reference)
                Button {
                    objectName: "linkInReference"
                    visible: linkPopup.inDocument || (linkPopup.uri === "" && linkPopup.page >= 0)
                    flat: true
                    text: qsTr("In the reference")
                    onClicked: {
                        const uri = linkPopup.uri
                        linkPopup.close()
                        if (linkPopup.inDocument) app.followDocumentLink(uri, "reference")
                        else app.reference.showBeside(linkPopup.page)
                    }
                }
            }
            RowLayout {
                visible: !!linkPopup.doc && linkPopup.doc.found && !linkPopup.inDocument
                spacing: 0
                Button {
                    objectName: "linkNewTab"
                    flat: true
                    text: qsTr("Open in a new tab")
                    onClicked: linkPopup.follow("tab")
                }
                Button {
                    objectName: "linkAsReference"
                    flat: true
                    text: qsTr("Open as reference")
                    onClicked: linkPopup.follow("reference")
                }
                Button {
                    objectName: "linkHere"
                    flat: true
                    text: qsTr("Open here")
                    onClicked: linkPopup.follow("here")
                }
            }
            CheckBox {
                id: linkRemember
                objectName: "linkRemember"
                visible: !!linkPopup.doc && linkPopup.doc.found && !linkPopup.inDocument
                text: qsTr("Remember my choice")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Links open this way from now on (Settings → Documents)")
            }
        }
    }

    SearchBar {
        id: searchBar
        objectName: "searchBar"
        anchors.top: canvas.top
        anchors.topMargin: 12
        anchors.horizontalCenter: canvas.horizontalCenter
        width: Math.min(implicitWidth, canvas.width - 16)
    }

    // Where the link under the mouse or the hovering pen leads (qt/docs/links.md, "Links with the mouse")
    LinkStatusLine {
        canvasItem: canvas
        visible: !win.cleanPage
    }
    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    CanvasScrollBars {
        canvasItem: canvas
        // (beside the strip that brings a right tool bar back, not under it)
        rightInset: toolbarShow.visible && toolbarShow.side === "right" ? toolbarShow.width : 0
        hidden: pageGrid.visible || app.presenting  // (presenting: no scroll bars)
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open document or PDF")
        currentFolder: app.openFolder()
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf *.md *.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("Xournal++ files (*.xopp *.xoj)"), qsTr("PDF files (*.pdf)"), qsTr("Markdown files (*.md)"),
                      qsTr("Images (*.png *.jpg *.jpeg *.webp *.heic *.heif)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: app.openUrls(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        property var afterSave: null
        property bool settingUp: false
        readonly property bool pdfChosen: selectedNameFilter.index === 1
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: pdfChosen ? "pdf" : "xopp"
        nameFilters: [qsTr("Xournal notes (*.xopp)"), qsTr("PDF with notes, editable (*.pdf)")]
        // The name follows the chosen type (native dialogs may do that themselves; then this changes nothing)
        onPdfChosenChanged: {
            if (!settingUp && selectedFile.toString() !== "")
                selectedFile = app.fileForFormat(selectedFile, pdfChosen)
        }
        onAccepted: {
            win.saveChosen(selectedFile, pdfChosen, afterSave)
            afterSave = null
        }
        onRejected: afterSave = null
    }

    /// A choice of the Share dialog: a title and a line about it
    component ShareChoice: ItemDelegate {
        id: choice
        property string detail
        Layout.fillWidth: true
        contentItem: ColumnLayout {
            spacing: 2
            Label { text: choice.text; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label {
                text: choice.detail
                color: "#6b6f75"
                font.pixelSize: 13
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }
    // Share…: the PDF with notes (shown in the file manager, or copied), or a copy for Xournal++ users
    Dialog {
        id: shareDialog
        objectName: "shareDialog"
        property string file: ""  // a PDF of the library; "": the current document
        /// A Markdown or text file (the current document's, or a card's): shared as the file itself, never as a PDF
        property string textFile: ""
        function openFor(path) {
            file = path
            textFile = app.sharedTextFile(path)
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, parent ? parent.width - 32 : 460)
        title: qsTr("Share")
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: shareDialog.availableWidth
            spacing: 0
            ShareChoice {
                objectName: "shareTextFileChoice"
                visible: shareDialog.textFile !== ""
                text: qsTr("The file itself")
                detail: app.canShare ? qsTr("Shown in the file manager, to send it on.")
                                     : qsTr("Not available on this system yet.")
                enabled: app.canShare
                onClicked: { shareDialog.close(); win.shareTextFile(shareDialog.textFile, shareDialog.file === "", false) }
            }
            ShareChoice {
                objectName: "shareTextCopyChoice"
                visible: shareDialog.textFile !== ""
                text: qsTr("Copy the file")
                detail: qsTr("Paste it into another app or a chat.")
                onClicked: { shareDialog.close(); win.shareTextFile(shareDialog.textFile, shareDialog.file === "", true) }
            }
            ShareChoice {
                objectName: "sharePdfChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("PDF with notes (opens in any app)")
                detail: app.canShare ? qsTr("Shown in the file manager, to send it on.")
                                     : qsTr("Not available on this system yet.")
                enabled: app.canShare
                onClicked: { shareDialog.close(); win.sharePdfOf(shareDialog.file, false) }
            }
            ShareChoice {
                objectName: "shareCopyChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("Copy the PDF with notes")
                detail: qsTr("Paste it into another app or a chat.")
                onClicked: { shareDialog.close(); win.sharePdfOf(shareDialog.file, true) }
            }
            ShareChoice {
                objectName: "shareArchiveChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("For the archive (PDF/A)")
                detail: qsTr("A PDF made for keeping: readable for decades, the ink merged into the pages.")
                onClicked: { shareDialog.close(); archiveDialog.openFor(shareDialog.file) }
            }
            ShareChoice {
                objectName: "shareXournalChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("For Xournal++ (.xopp + PDF)")
                detail: qsTr("A copy in a folder you choose, never next to the document.")
                onClicked: {
                    shareDialog.close()
                    xournalFolderDialog.file = shareDialog.file
                    xournalFolderDialog.currentFolder = app.shareFolder()
                    xournalFolderDialog.open()
                }
            }
        }
    }
    // Share → PDF of a .xopp: saved as a PDF with notes (the document becomes it), or a PDF copy
    Dialog {
        id: shareXoppDialog
        objectName: "shareXoppDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(500, parent ? parent.width - 32 : 500)
        title: qsTr("Share as a PDF with notes")
        Label {
            width: shareXoppDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This document is saved as Xournal notes (.xopp). Other apps need a PDF with notes: save the "
                       + "document as one (it stays editable here), or write a PDF copy and keep the .xopp.")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shareSaveAsPdf"
                text: qsTr("Save as PDF with notes…")
                flat: true
                onClicked: {
                    shareXoppDialog.close()
                    openSaveDialog(function() { app.sharePdf(false) }, "pdf")
                }
            }
            Button {
                objectName: "shareSaveCopy"
                text: qsTr("Save a PDF copy…")
                flat: true
                onClicked: {
                    shareXoppDialog.close()
                    const suggestion = app.suggestedHybridFile().toString()
                    if (suggestion !== "") {
                        pdfCopyDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
                        pdfCopyDialog.selectedFile = suggestion
                    }
                    pdfCopyDialog.open()
                }
            }
            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: shareXoppDialog.close()
            }
        }
    }
    FileDialog {
        id: pdfCopyDialog
        objectName: "pdfCopyDialog"
        title: qsTr("Save a PDF copy with notes")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF with notes, editable (*.pdf)")]
        onAccepted: app.sharePdfCopy(selectedFile, false)
    }
    FolderDialog {
        id: xournalFolderDialog
        objectName: "xournalFolderDialog"
        property string file: ""
        title: qsTr("Folder for the copy for Xournal++")
        onAccepted: app.shareForXournal(selectedFolder, file)
    }
    Connections {
        target: app
        // Exported for Xournal++ and shown: the two files can be copied too
        function onSharedForXournal(files, text) {
            snackbar.show(text, false, qsTr("Copy"), function() { app.copyToClipboard(files) })
        }
    }
    // Export for the archive: what it means, where it goes; then the PDF/A report
    Dialog {
        id: archiveDialog
        objectName: "archiveDialog"
        property string file: ""  // a library card's document; "": the current document
        property url suggestion
        function openFor(path) {
            file = path
            suggestion = app.suggestedArchiveFile(path)
            if (suggestion.toString() !== "")
                archiveNextTo.checked = true
            else
                archiveInFolder.checked = true
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: qsTr("Export for the archive")
        ColumnLayout {
            width: archiveDialog.availableWidth
            spacing: 6
            Label {
                objectName: "archiveExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("A PDF made for keeping (PDF/A-3). It stays readable for decades in any PDF viewer. "
                           + "Your ink is merged into the pages, so no viewer can hide or lose it. The full Xournal "
                           + "data is embedded, so this app can still open it for editing.")
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: qsTr("Where it goes")
                font.weight: Font.DemiBold
            }
            ButtonGroup { id: archivePlaces }
            RadioButton {
                id: archiveNextTo
                objectName: "archiveNextTo"
                Layout.fillWidth: true
                ButtonGroup.group: archivePlaces
                enabled: archiveDialog.suggestion.toString() !== ""
                text: enabled ? qsTr("Next to the document, as %1")
                                    .arg(decodeURIComponent(archiveDialog.suggestion.toString().replace(/^.*\//, "")))
                              : qsTr("Next to the document (it has no file yet)")
            }
            RadioButton {
                id: archiveInFolder
                objectName: "archiveInFolder"
                Layout.fillWidth: true
                ButtonGroup.group: archivePlaces
                text: qsTr("In a folder I choose…")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "archiveExportButton"
                text: qsTr("Export")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            if (archiveNextTo.checked) {
                app.exportArchive(suggestion, file)
            } else {
                archiveFolderDialog.file = file
                archiveFolderDialog.currentFolder = app.shareFolder()
                archiveFolderDialog.open()
            }
        }
    }
    FolderDialog {
        id: archiveFolderDialog
        objectName: "archiveFolderDialog"
        property string file: ""
        title: qsTr("Folder for the archive PDF")
        onAccepted: app.exportArchive(app.archiveFileIn(selectedFolder, file), file)
    }
    Dialog {
        id: archiveReportDialog
        objectName: "archiveReportDialog"
        property string path: ""
        property bool pdfa: false
        property var problems: []
        property var adjusted: []
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: pdfa ? qsTr("Archive PDF written") : qsTr("Written, but not as PDF/A")
        ColumnLayout {
            width: archiveReportDialog.availableWidth
            spacing: 6
            Label {
                objectName: "archiveReportText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const name = archiveReportDialog.path.replace(/^.*\//, "")
                    if (archiveReportDialog.pdfa)
                        return qsTr("%1 is a PDF/A-3b file: made for keeping, with your ink in the pages and the "
                                    + "Xournal data inside.").arg(name)
                    return qsTr("%1 was written with your ink in the pages and the Xournal data inside, and it opens "
                                + "in any PDF viewer. It is not PDF/A, because:").arg(name)
                            + "\n• " + archiveReportDialog.problems.join("\n• ")
                }
            }
            Label {
                objectName: "archiveReportAdjusted"
                visible: archiveReportDialog.adjusted.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#6b6f75"
                font.pixelSize: 13
                text: qsTr("Changed to make it conform: %1.").arg(archiveReportDialog.adjusted.join("; "))
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "archiveShowButton"
                visible: app.canShare
                text: qsTr("Show in folder")
                flat: true
                onClicked: { app.shareFile(archiveReportDialog.path, false); archiveReportDialog.close() }
            }
            Button {
                objectName: "archiveOkButton"
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Connections {
        target: app
        function onArchiveExported(path, pdfa, notPdfA, adjusted) {
            archiveReportDialog.path = path
            archiveReportDialog.pdfa = pdfa
            archiveReportDialog.problems = notPdfA
            archiveReportDialog.adjusted = adjusted
            archiveReportDialog.open()
        }
    }
    // Saving a "name.xopp" as a PDF with notes: what happens to the .xopp (asked once, before it is written)
    Dialog {
        id: oldXoppDialog
        objectName: "oldXoppDialog"
        property string file: ""
        property var url
        property var afterSave: null
        function ask(name, target, then) {
            file = name
            url = target
            afterSave = then ? then : null
            trashChoice.checked = true  // (the default)
            dontAsk.checked = false
            open()
        }
        /// "trash", "update" or "keep": the PDF is written, then that happens (Cancel: nothing is written)
        function choose(choice) {
            app.saveAsHybridInBackground(url, afterSave, choice, dontAsk.checked)
            afterSave = null
            close()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: qsTr("The PDF holds everything")
        ColumnLayout {
            width: oldXoppDialog.availableWidth
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This document was saved as %1. What happens to it?").arg(oldXoppDialog.file)
            }
            ButtonGroup { id: oldXoppChoices }
            RadioButton {
                id: trashChoice
                objectName: "oldXoppTrash"
                ButtonGroup.group: oldXoppChoices
                text: qsTr("Move %1 to the trash (the PDF now holds everything)").arg(oldXoppDialog.file)
            }
            RadioButton {
                id: updateChoice
                objectName: "oldXoppUpdate"
                ButtonGroup.group: oldXoppChoices
                text: qsTr("Keep it updated for Xournal++")
            }
            RadioButton {
                id: keepChoice
                objectName: "oldXoppKeep"
                ButtonGroup.group: oldXoppChoices
                text: qsTr("Keep it as it is (not updated)")
            }
            CheckBox {
                id: dontAsk
                objectName: "oldXoppDontAsk"
                text: qsTr("Don't ask again (Settings → Documents)")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "oldXoppSave"
                text: qsTr("Save")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: choose(updateChoice.checked ? "update" : keepChoice.checked ? "keep" : "trash")
        onRejected: afterSave = null
    }
    // A hybrid PDF whose ink another app changed: keep ours, or take theirs as plain annotations
    Dialog {
        id: hybridEditedDialog
        objectName: "hybridEditedDialog"
        property string file: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: qsTr("Edited in another app")
        closePolicy: Popup.NoAutoClose
        Label {
            width: hybridEditedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This PDF was edited in another app: its ink differs from the Xournal data.") + "\n\n"
                  + qsTr("Keep the Xournal data: the next save writes the ink from it again, and the other app's "
                         + "changes to it are dropped. Import: the changed ink stays as the other app left it, as "
                         + "plain annotations (shown, not editable here), and the layers it stood for are emptied "
                         + "(Undo brings them back).")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "hybridKeepButton"
                text: qsTr("Keep the Xournal data")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                objectName: "hybridImportButton"
                text: qsTr("Import the other app's changes")
                DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
                onClicked: { app.importHybridChanges(); hybridEditedDialog.close() }
            }
        }
        onAccepted: app.keepHybridData()
    }
    Connections {
        target: app
        function onHybridEditedElsewhere(file) {
            hybridEditedDialog.file = file
            hybridEditedDialog.open()
        }
        function onLinkTargetFound(name, folder) {
            linkFoundDialog.file = name
            linkFoundDialog.folder = folder
            linkFoundDialog.open()
        }
        function onLinkTargetMissing(name) {
            linkMissingDialog.file = name
            linkMissingDialog.open()
        }
        function onEditAnywayWarning(name) {
            editAnywayDialog.file = name
            editAnywayDialog.open()
        }
        function onTextChangedOnDisk(name) {
            textChangedDialog.file = name
            textChangedDialog.document = false
            textChangedDialog.open()
        }
        function onDocumentChangedOnDisk(name) {
            textChangedDialog.file = name
            textChangedDialog.document = true
            textChangedDialog.open()
        }
    }
    // Open externally with unsaved changes: save them first?
    Dialog {
        id: externalSaveDialog
        objectName: "externalSaveDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: qsTr("Save before opening it elsewhere?")
        Label {
            width: externalSaveDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("%1 has changes that are not saved. The other app sees the file as it is on disk.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button {
                objectName: "externalCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "externalWithoutSavingButton"
                text: qsTr("Open without saving")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: { externalSaveDialog.close(); app.openExternally() }
            }
            Button {
                objectName: "externalSaveButton"
                text: qsTr("Save and open")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: saveOrAsk(function() { app.openExternally() })
    }
    // "Edit anyway" for a code, LaTeX, JSON... file: once per file, what editing it here means
    Dialog {
        id: editAnywayDialog
        objectName: "editAnywayDialog"
        property string file: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: qsTr("Edit %1 as plain text?").arg(file)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: editAnywayDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This file is edited as plain text; the app does not know its format. It does not check or "
                       + "complete what you write, and it writes the text back as you leave it (lines you do not touch "
                       + "stay as they are). For more, open it externally in an editor made for it.")
        }
        onAccepted: app.editAnyway(true)
    }
    // "Linked from": the documents of the library that link to this one (qt/docs/links.md)
    Dialog {
        id: backlinksDialog
        objectName: "backlinksDialog"
        property var items: []
        function show() { items = app.backlinks(); open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, parent ? parent.width - 32 : 460)
        title: qsTr("Linked from")
        standardButtons: Dialog.Close
        ColumnLayout {
            width: backlinksDialog.availableWidth
            Label {
                visible: backlinksDialog.items.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTr("No document of the library links to this one.")
            }
            Repeater {
                model: backlinksDialog.items
                delegate: ItemDelegate {
                    required property var modelData
                    objectName: "backlink"
                    Layout.fillWidth: true
                    text: modelData.folder !== "" ? modelData.name + "  —  " + modelData.folder : modelData.name
                    onClicked: { backlinksDialog.close(); app.openPath(modelData.path) }
                }
            }
        }
    }
    // A followed link's file was gone: found elsewhere (update the link?) or not at all (locate it?)
    Dialog {
        id: linkFoundDialog
        objectName: "linkFoundDialog"
        property string file: ""
        property string folder: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: qsTr("The linked document was moved")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: linkFoundDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("It was found as \u201c%1\u201d in %2 and opened. Update the link to point there?")
                  .arg(linkFoundDialog.file).arg(linkFoundDialog.folder !== "" ? linkFoundDialog.folder : qsTr("the library"))
        }
        onAccepted: app.updateFoundLink()
    }
    Dialog {
        id: linkMissingDialog
        objectName: "linkMissingDialog"
        property string file: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: qsTr("Document not found")
        standardButtons: Dialog.Open | Dialog.Cancel
        Label {
            width: linkMissingDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\u201c%1\u201d is not where the link says, and nothing like it is in the library. Locate it? "
                       + "The link then points to the file you choose.").arg(linkMissingDialog.file)
        }
        onAccepted: locateLinkDialog.open()
    }
    FileDialog {
        id: locateLinkDialog
        title: qsTr("Locate the linked document")
        currentFolder: app.openFolder()
        onAccepted: app.relinkTo(selectedFile)
    }
    // A text file changed on disk (another program) while it has changes here: which version stays
    Dialog {
        id: textChangedDialog
        objectName: "textChangedDialog"
        property string file: ""
        property bool document: false  // a .xopp or PDF (reloading it cannot be undone)
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - 32 : 520)
        title: qsTr("Changed in another app")
        closePolicy: Popup.NoAutoClose
        Label {
            width: textChangedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("%1 was changed by another app, and it has changes here that are not saved.").arg(textChangedDialog.file)
                  + "\n\n" + (textChangedDialog.document
                               ? qsTr("Reload: the file as it is now is shown, and your changes here are discarded. "
                                      + "Keep mine: your version stays, and saving writes over the other app's changes.")
                               : qsTr("Reload: the file as it is now is shown (Undo brings your changes back). Keep mine: "
                                      + "your version stays, and saving writes over the other app's changes."))
        }
        footer: DialogButtonBox {
            Button {
                objectName: "textKeepButton"
                text: qsTr("Keep mine")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "textReloadButton"
                text: qsTr("Reload")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.resolveTextChange(true)
        onRejected: app.resolveTextChange(false)
    }
    // "Export as Markdown" (qt/docs/md-pdf.md): next to the document (Xournal++ files; asked before a file is
    // replaced), else where this dialog says
    function exportMarkdown() {
        const file = app.markdownExportFile()
        if (file.toString() === "") {
            const suggestion = app.suggestedMarkdownExport().toString()
            markdownExportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            markdownExportDialog.selectedFile = suggestion
            markdownExportDialog.open()
        } else if (app.fileExists(file)) {
            markdownReplaceDialog.file = file
            markdownReplaceDialog.open()
        } else {
            app.exportMarkdown(file)
        }
    }
    FileDialog {
        id: markdownExportDialog
        objectName: "markdownExportDialog"
        title: qsTr("Export as Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown (*.md)")]
        onAccepted: app.exportMarkdown(selectedFile)
    }
    Dialog {
        id: markdownReplaceDialog
        objectName: "markdownReplaceDialog"
        property url file
        anchors.centerIn: parent
        modal: true
        title: qsTr("Replace the Markdown file?")
        width: Math.min(win.width * 0.9, 440)
        Label {
            width: markdownReplaceDialog.availableWidth
            wrapMode: Text.WordWrap
            text: qsTr("%1 exists. Replace it with the Markdown of this document?")
                  .arg(decodeURIComponent(markdownReplaceDialog.file.toString().replace(/^.*\//, "")))
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Choose another place…"); DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                     onClicked: {
                         markdownReplaceDialog.close()
                         const suggestion = markdownReplaceDialog.file.toString()
                         markdownExportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
                         markdownExportDialog.selectedFile = suggestion
                         markdownExportDialog.open()
                     } }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "markdownReplaceButton"; text: qsTr("Replace")
                     DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: app.exportMarkdown(file)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export as plain PDF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: app.exportPdf(selectedFile)
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("Add a color to the tool bar")
        selectedColor: app.color
        onAccepted: {
            app.addToolbarColor(selectedColor)
            app.setColor(selectedColor)
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Insert image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"), qsTr("All files (*)")]
        onAccepted: app.insertImage(selectedFile)
    }

    Dialog {
        id: unsavedDialog
        objectName: "unsavedDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Unsaved changes")
        width: Math.min(win.width * 0.9, 480)
        Label {
            width: unsavedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" has unsaved changes.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: { unsavedDialog.close(); saveOrAsk(afterDiscardCheck) }
            onRejected: { unsavedDialog.close(); afterDiscardCheck = null }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    unsavedDialog.close()
                    var action = afterDiscardCheck
                    afterDiscardCheck = null
                    if (action) action()
                }
            }
        }
    }

    // After a crash: offer the documents with unsaved changes (from emergency and autosave files).
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        onClosed: homeView.offerLibrariesHomeAtStart()
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(win.width * 0.9, 560)
        title: qsTr("Recover unsaved changes?")
        ColumnLayout {
            width: recoveryDialog.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Xournal Qt did not close properly. These documents have changes that were not saved:")
            }
            Repeater {
                model: app.recoveryItems
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Label { text: modelData.time; color: "#6b6f75" }
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Recover"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard changes"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            onAccepted: { recoveryDialog.close(); app.recover(true) }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    recoveryDialog.close()
                    app.recover(false)
                }
            }
        }
    }
    // The first start asks which way to keep documents (PDF files or Xournal++ files); then the recovery question;
    // then (Android) where the libraries are kept
    DocumentModeDialog {
        id: documentModeDialog
        onChosen: {
            if (app.recoveryItems.length > 0) recoveryDialog.open()
            else homeView.offerLibrariesHomeAtStart()
        }
    }
    Component.onCompleted: {
        if (app.askDocumentMode()) documentModeDialog.open()
        else if (app.recoveryItems.length > 0) recoveryDialog.open()
        else homeView.offerLibrariesHomeAtStart()
    }

    Dialog {
        id: closeAllDialog
        objectName: "closeAllDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width * 0.8, 480)
        title: qsTr("Close all documents?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        ColumnLayout {
            width: closeAllDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 documents are open. Documents with unsaved changes ask before they close.")
                        .arg(app.tabs.count)
            }
        }
        onAccepted: { tabOverview.close(); closeAllTabs() }
    }

    Dialog {
        id: messageDialog
        objectName: "messageDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width * 0.8, 640)
        standardButtons: Dialog.Ok
        property alias text: messageLabel.text
        Label { id: messageLabel; wrapMode: Text.Wrap; width: parent.width }
    }
    // Full screen (editing): the open documents as dots in a slim bar at the top; a tap shows them all, a swipe along
    // the bar goes to the next or previous one (only on the bar: the pages keep every touch)
    Rectangle {
        id: fullScreenTabs
        objectName: "fullScreenTabs"
        visible: win.fullScreenMode && !app.presenting && !app.homeVisible && app.tabs.count > 1
                 && !searchBar.visible
        z: 59
        // at the top, in the middle of the window (over the notes and a reference beside them alike)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        height: 26  // (thin to look at, a finger's height to touch)
        width: Math.max(120, (tabDots.visible ? tabDots.implicitWidth : tabCountLabel.implicitWidth) + 36)
        radius: 13
        color: "#b3303134"
        readonly property bool manyTabs: app.tabs.count > 12
        PageIndicator {
            id: tabDots
            objectName: "fullScreenTabDots"
            anchors.centerIn: parent
            visible: !fullScreenTabs.manyTabs
            count: app.tabs.count
            currentIndex: app.currentTab
            interactive: false
            padding: 0
            spacing: 7
            delegate: Item {
                required property int index
                implicitWidth: 9
                implicitHeight: 9
                // (read again when a document is changed or another one comes to the front)
                readonly property bool unsaved: (app.modified, app.currentTab, app.tabModified(index))
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: index === tabDots.currentIndex ? "#ffffff" : "transparent"
                    border.width: 1.5
                    border.color: "#e8eaed"
                }
                Rectangle {  // not saved: a small orange mark
                    visible: parent.unsaved
                    width: 5; height: 5; radius: 2.5
                    x: parent.width - 3; y: -2
                    color: "#ffb74d"
                }
            }
        }
        Label {
            id: tabCountLabel
            objectName: "fullScreenTabCount"
            anchors.centerIn: parent
            visible: fullScreenTabs.manyTabs
            text: (app.currentTab + 1) + " / " + app.tabs.count
            color: "#ffffff"
            font.pixelSize: 13
        }
        TapHandler { onTapped: tabOverview.open() }
        DragHandler {
            id: tabSwipe
            target: null
            yAxis.enabled: false
            onActiveChanged: {
                if (active) return
                const dx = centroid.position.x - centroid.pressPosition.x
                if (Math.abs(dx) < 30) return
                if (dx < 0) app.nextTab()
                else app.previousTab()
                tabToast.show()
            }
        }
        ToolTip.visible: tabHover.hovered
        ToolTip.text: qsTr("Open documents: tap for all of them, swipe for the next or previous one")
        ToolTip.delay: 800
        HoverHandler { id: tabHover }
    }
    // The document swiped to: its title, for a moment
    Rectangle {
        id: tabToast
        objectName: "fullScreenTabToast"
        z: 59
        anchors.horizontalCenter: parent.horizontalCenter
        y: fullScreenTabs.height + 8
        visible: opacity > 0 && win.fullScreenMode
        opacity: 0
        width: Math.min(tabToastText.implicitWidth + 28, parent.width - 160)
        height: 32
        radius: 16
        color: "#e6303134"
        Label {
            id: tabToastText
            objectName: "fullScreenTabToastText"
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 28)
            elide: Text.ElideMiddle
            text: app.title
            color: "#ffffff"
            font.pixelSize: 14
        }
        function show() {
            toastFade.stop()
            opacity = 1
            toastFade.start()
        }
        SequentialAnimation {
            id: toastFade
            PauseAnimation { duration: 1200 }
            NumberAnimation { target: tabToast; property: "opacity"; to: 0; duration: 500 }
        }
    }

    // Presenting: the page number, for a moment after each page change (and when it starts)
    Rectangle {
        id: presentIndicator
        objectName: "presentPageIndicator"
        z: 90
        visible: app.presenting && !win.presentClean && opacity > 0  // (without controls: not even the number)
        opacity: 0
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: 18
        width: indicatorText.implicitWidth + 24
        height: 30
        radius: 15
        color: "#99000000"
        Label {
            id: indicatorText
            objectName: "presentPageIndicatorText"
            anchors.centerIn: parent
            text: app.pageNumber + " / " + app.pageCount
            color: "#ffffff"
            font.pixelSize: 14
        }
        function flash() {
            if (!app.presenting) return
            fade.stop()
            opacity = 0.9
            fade.start()
        }
        SequentialAnimation {
            id: fade
            PauseAnimation { duration: 1500 }
            NumberAnimation { target: presentIndicator; property: "opacity"; to: 0; duration: 600 }
        }
        Connections {
            target: app
            function onPageChanged() { presentIndicator.flash() }
            function onPresentingChanged() { if (app.presenting) presentIndicator.flash(); else presentIndicator.opacity = 0 }
        }
    }
    // Presenting: a faint mark in the lower left corner; a tap (click, pen) hides the controls - the pen pill, the
    // tool square - or shows them again, as Ctrl+F5 does (qt/present-clean). The mark is a few pixels, barely there
    // on a projector; the target around it is a finger wide. The pointer or the pen over it makes it clearer.
    AbstractButton {
        id: presentCornerMark
        objectName: "presentCornerMark"
        visible: app.presenting
        z: 91
        anchors.left: canvas.left
        anchors.bottom: canvas.bottom
        width: 48
        height: 48
        focusPolicy: Qt.NoFocus  // (the keys stay with the page)
        hoverEnabled: true
        readonly property bool lit: hovered || markHover.hovered || pressed
        Accessible.name: win.presentClean ? qsTr("Show the controls") : qsTr("Hide the controls")
        background: null
        contentItem: Item {
            Rectangle {
                objectName: "presentCornerDot"
                x: 10
                y: parent.height - height - 10
                width: 6
                height: 6
                radius: 3
                // grey: as faint on a white slide as on the black around it
                color: "#9e9e9e"
                border.width: 1
                border.color: "#80ffffff"
                opacity: presentCornerMark.lit ? 0.6 : 0.14
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }
        HoverHandler { id: markHover; acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus }
        onClicked: win.presentClean = !win.presentClean
    }
    // Digits typed while the page is at hand: go to that page (Enter)
    PageJump {
        id: pageJump
        z: 100
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.top: canvas.top
        anchors.topMargin: Math.round(canvas.height * 0.2)
        /// The number is for the reference (it had the keys when the first digit was typed)
        property bool forReference: false
        pageCount: forReference ? app.reference.pageCount : app.pageCount
        returnFocus: forReference ? referenceSplit.referenceCanvas : canvas
        onJumpRequested: function(page) {
            if (forReference) app.reference.goToPage(page - 1)
            else app.jumpToPage(page - 1)
        }
    }
    Snackbar {
        id: snackbar
        objectName: "snackbar"
        z: 100
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: 96
    }
    Connections {
        target: app
        function onPageActionDone(text, undoable) { snackbar.show(text, undoable) }
    }
    Connections {
        target: app
        // The annotations were exported as Markdown (the Annotations panel): open the file from here
        function onAnnotationsExported(file, error) {
            if (error !== "") {
                messageDialog.title = qsTr("Export failed")
                messageDialog.text = error
                messageDialog.open()
                return
            }
            snackbar.show(qsTr("Annotations exported to %1").arg(file.split("/").pop()), false, qsTr("Open"),
                          function() { app.openPath(file) })
        }
    }

    Connections {
        target: app
        function onMessage(title, text, error) {
            messageDialog.title = title !== "" ? title : (error ? qsTr("Error") : qsTr("Information"))
            messageDialog.text = text
            messageDialog.open()
        }
    }

    // Library and recent documents: over the document area while shown.
    HomeView {
        id: homeView
        anchors.fill: parent
        z: 50
        visible: app.homeVisible
        onOpenFileRequested: openDialog.open()
        onSettingsRequested: settingsPage.open()
        // A PDF card: the file itself; a card of notes (also a PDF with its .xopp): opened, then shared as a document
        onShareRequested: function(path) {
            if (path.toLowerCase().endsWith(".pdf") || app.sharedTextFile(path) !== "") {
                shareDialog.openFor(path)  // (a PDF, a Markdown or text file: the file itself)
            } else if (app.openPath(path)) {
                shareDialog.openFor("")
            }
        }
    }

    // Putting the tool bar away and getting it back: a small tab at its end, and a slim strip while it is away.
    Rectangle {
        objectName: "toolbarToggle"
        visible: !app.homeVisible && !win.fullScreenMode && !app.toolbarHidden
        z: 58
        width: win.sideToolbar ? 18 : 42
        height: win.sideToolbar ? 42 : 18
        radius: 6
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
        // At the end of the bar, a little into the pages
        x: win.toolbarPosition === "left" ? sideTools.width - width / 2
           : win.toolbarPosition === "right" ? parent.width - sideTools.width - width / 2
           : parent.width - width - 18
        y: win.sideToolbar ? Math.round(parent.height / 2) : -height / 2
        Image {  // towards the bar it puts away: up, left or right
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-up")
            sourceSize.width: 15
            sourceSize.height: 15
            rotation: win.toolbarPosition === "left" ? -90 : win.toolbarPosition === "right" ? 90 : 0
        }
        TapHandler { onTapped: app.toolbarHidden = true }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: qsTr("Hide the tool bar")
        ToolTip.delay: 600
        HoverHandler { id: hoverHandler }
    }
    // While it is away: a slim strip at the edge where it was (top, left or right) brings it back
    Rectangle {
        id: toolbarShow
        objectName: "toolbarShow"
        visible: !app.homeVisible && !win.fullScreenMode && app.toolbarHidden
        readonly property string side: win.sideToolbar ? win.toolbarPosition : "top"
        z: 60  // over the edge of the pen pill, which sits at the right edge by default
        width: side === "top" ? 96 : 16
        height: side === "top" ? 16 : 96
        x: side === "left" ? 0 : side === "right" ? parent.width - width : Math.round((parent.width - width) / 2)
        y: side === "top" ? 0 : Math.round((parent.height - height) / 2)
        radius: 8
        color: "#f1f3f4"
        border.width: 1
        border.color: "#d5d8dc"
        opacity: showHover.hovered ? 1 : 0.75
        Image {  // where the bar comes in from: down from the top, into the pages from a side
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-down")
            sourceSize.width: 15
            sourceSize.height: 15
            rotation: toolbarShow.side === "left" ? -90 : toolbarShow.side === "right" ? 90 : 0
        }
        TapHandler { onTapped: app.toolbarHidden = false }
        HoverHandler { id: showHover }
        ToolTip.visible: showHover.hovered
        ToolTip.text: qsTr("Show the tool bar")
        ToolTip.delay: 600
    }

    // Without a tool bar and with an ink tool: colors, width and pen / highlighter at a side of the screen
    PenPill {}
    // The setsquare / compass: what it can do, and putting it aside for a moment
    GeometryPill {
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.topMargin: 12
        anchors.rightMargin: 20
        z: 57
    }
    ColorDialog {
        id: pillColorDialog
        onAccepted: app.addPenColor(selectedColor)
    }

    // Full screen: the current tool in a small square (drag it anywhere); a tap offers all tools and colors.
    Rectangle {
        id: quickToolSquare
        objectName: "quickToolSquare"
        // Only in full screen: with the bar merely put away, the arrow strip brings it back at once. Not while
        // presenting without controls.
        visible: win.fullScreenMode && !app.homeVisible && !win.cleanPage
        z: 60
        x: canvas.x + 16  // (over the main document, also when a reference is beside it)
        y: canvas.y + 16
        width: 56
        height: 56
        radius: 12
        color: "#f7ffffff"
        border.width: 2
        border.color: app.color
        readonly property var toolIcons: ({
            "pen": "xopp-tool-pencil", "highlighter": "xopp-tool-highlighter", "eraser": "xopp-tool-eraser",
            "hand": "xopp-hand", "text": "xopp-tool-text", "selectRect": "xopp-select-rect",
            "selectRegion": "xopp-select-lasso", "selectPdfTextLinear": "xopp-select-pdf-text-ht",
            "selectPdfTextRect": "xopp-select-pdf-text-area"
        })
        Image {
            anchors.centerIn: parent
            source: app.iconUrl(quickToolSquare.toolIcons[app.tool] || "xopp-tool-pencil")
            sourceSize.width: 30
            sourceSize.height: 30
        }
        Rectangle {  // the color
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 14; height: 14; radius: 7
            color: app.color
            border.width: 1
            border.color: "#80000000"
        }
        DragHandler {
            target: parent
            xAxis.minimum: 0; xAxis.maximum: win.contentItem.width - quickToolSquare.width
            yAxis.minimum: 0; yAxis.maximum: win.contentItem.height - quickToolSquare.height
        }
        TapHandler { onTapped: quickTools.visible ? quickTools.close() : quickTools.open() }
    }
    Popup {
        id: quickTools
        objectName: "quickTools"
        parent: Overlay.overlay
        // next to the square, inside the window
        x: Math.min(quickToolSquare.x + quickToolSquare.width + 8, win.contentItem.width - width - 8)
        y: Math.max(8, Math.min(quickToolSquare.y, win.contentItem.height - height - 8))
        padding: 8
        width: 6 * 50 + 30
        height: Math.min(win.contentItem.height - 16, quickToolsColumn.implicitHeight + 16)
        Column {
            id: quickToolsColumn
            width: parent.width
            spacing: 4
            Item {
                id: quickToolsHolder
                width: parent.width
                height: Math.min(toolRow.implicitHeight, win.contentItem.height - 90)
            }
            Button {
                objectName: "presentToggleButton"
                width: parent.width
                flat: true
                icon.source: app.iconUrl("xopp-presentation-mode")
                text: app.presenting ? qsTr("Stop presenting (Esc)") : qsTr("Present (F5)")
                onClicked: {
                    quickTools.close()
                    app.presenting = !app.presenting
                }
                // held: without controls (only the page)
                onPressAndHold: {
                    quickTools.close()
                    if (app.presenting) win.presentClean = true
                    else win.startPresenting(true)
                }
            }
            Button {
                objectName: "leaveFullScreenButton"
                width: parent.width
                flat: true
                text: qsTr("Leave full screen") + (app.presenting ? "" : qsTr(" (Esc)"))
                onClicked: win.fullScreenMode = false
            }
        }
        // A tool, color or size was chosen: back to writing
        Connections {
            target: app
            enabled: quickTools.opened
            function onToolChanged() { quickTools.close() }
        }
    }

    InsertPagesDialog { id: insertPagesDialog }
    PrintDialog { id: printDialog }
    ChapterDialog { id: chapterDialog }
    ContextPill { id: contextPill; onImageRequested: imageDialog.open() }
    WebConfirm { id: webConfirm }
    WebImageConfirm { id: webImageConfirm }
    UnusedImagesDialog { id: unusedImagesDialog }
    Connections {
        target: app
        function onWebImageRequested(url, host, access) { webImageConfirm.ask(url, host, access) }
    }
    FindPaperSheet { id: findPaperSheet }
    ArxivSheet { id: arxivSheet }
    PdfTextHandles { }
    Connections {
        target: app
        // On PDF text a long press (or right click) selects the word, and its actions offer paste as well; elsewhere
        // it offers what can be done here
        function onContextRequested(viewPos) {
            if (app.selectPdfTextAt(viewPos.x, viewPos.y)) {
                pdfTextBar.offerPaste(viewPos)
                return
            }
            contextPill.openAt(viewPos, app.pdfTextIsSelected)
        }
    }
    Connections {
        target: app
        function onChapterRequested(page) { chapterDialog.openFor(page) }
    }
    Connections {
        target: app
        function onPrintRequested(pages) { printDialog.openFor(pages) }
    }
    BackgroundDialog { id: backgroundDialog }
    NoteSpaceDialog { id: noteSpaceDialog }
    Connections {
        target: app
        function onNoteSpaceRequested(pages, allPages) { noteSpaceDialog.openFor(pages, allPages) }
    }
    Connections {
        target: app
        function onPageBackgroundRequested(pages) { backgroundDialog.openFor(pages) }
    }
    Connections {
        target: app
        function onInsertPagesRequested(position) { insertPagesDialog.openAt(position) }
    }

    SettingsPage {
        id: settingsPage
        objectName: "settingsPage"
        onQuitRequested: win.closeWindow()  // (asks about unsaved documents first)
    }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
        onCloseAllRequested: app.tabs.count > 1 ? closeAllDialog.open() : closeAllTabs()
        onLibrarySearchRequested: win.searchLibrary()
    }

    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible && !app.textFlowActive && !app.markdownActive
    // The keys come from the shortcut settings (app.shortcuts); reading its revision keeps the bindings fresh.
    function keysOf(id) { return (app.shortcuts.revision, app.shortcuts.keys(id)) }
    Shortcut { sequences: win.keysOf("undo"); enabled: docKeys; onActivated: app.undo() }
    // The tools on single keys: only while the page is at hand (typing into a text or a field takes its keys first;
    // the overviews and the settings search or edit what is typed)
    readonly property bool toolKeys: docKeys && !pageGrid.visible && !contentsOverview.visible && !tabOverview.visible
                                     && !settingsPage.visible
    Shortcut { sequences: win.keysOf("toolPen"); enabled: toolKeys; onActivated: app.selectTool("pen") }
    // A page number: the first digit opens the jump, which takes the following keys itself. (A field or a text on
    // the page that is typed into takes its digits first.)
    component DigitKey: Shortcut {
        property int digit
        sequence: String(digit)
        enabled: win.toolKeys && !pageJump.visible && !app.reference.pagesShown
        onActivated: {
            pageJump.forReference = app.reference.focused  // (before the jump takes the keys)
            pageJump.start(String(digit))
        }
    }
    DigitKey { digit: 0 }
    DigitKey { digit: 1 }
    DigitKey { digit: 2 }
    DigitKey { digit: 3 }
    DigitKey { digit: 4 }
    DigitKey { digit: 5 }
    DigitKey { digit: 6 }
    DigitKey { digit: 7 }
    DigitKey { digit: 8 }
    DigitKey { digit: 9 }
    // Scrolling sideways: the arrow keys and Page Up / Down go from page to page (a text being typed keeps them)
    readonly property bool sidewaysKeys: toolKeys && app.horizontalScrolling && !app.presenting
    Shortcut { sequences: ["Left", "PgUp"]; enabled: win.sidewaysKeys; onActivated: app.previousPage() }
    Shortcut { sequences: ["Right", "PgDown"]; enabled: win.sidewaysKeys; onActivated: app.nextPage() }
    Shortcut { sequence: "Home"; enabled: win.sidewaysKeys; onActivated: app.firstPage() }
    Shortcut { sequence: "End"; enabled: win.sidewaysKeys; onActivated: app.lastPage() }
    // Presenting, like PowerPoint: Space, → ↓ Page Down on, ← ↑ Page Up Backspace back (Backspace deletes what is
    // selected, if anything)
    readonly property bool presentKeys: toolKeys && app.presenting
    Shortcut { sequences: ["Space", "Right", "Down", "PgDown"]; enabled: win.presentKeys; onActivated: app.nextPage() }
    Shortcut { sequences: ["Left", "Up", "PgUp"]; enabled: win.presentKeys; onActivated: app.previousPage() }
    Shortcut { sequence: "Backspace"; enabled: win.presentKeys && !app.hasSelection; onActivated: app.previousPage() }
    Shortcut { sequence: "Home"; enabled: win.presentKeys; onActivated: app.firstPage() }
    Shortcut { sequence: "End"; enabled: win.presentKeys; onActivated: app.lastPage() }

    Shortcut { sequences: win.keysOf("toolEraser"); enabled: toolKeys; onActivated: app.selectTool("eraser") }
    Shortcut { sequences: win.keysOf("toolHighlighter"); enabled: toolKeys; onActivated: app.selectTool("highlighter") }
    Shortcut { sequences: win.keysOf("toolText"); enabled: toolKeys; onActivated: app.selectTool("text") }
    Shortcut { sequences: win.keysOf("toolSelect"); enabled: toolKeys; onActivated: app.selectTool("selectRect") }
    Shortcut { sequences: win.keysOf("toolLasso"); enabled: toolKeys; onActivated: app.selectTool("selectRegion") }
    Shortcut { sequences: win.keysOf("toolHand"); enabled: toolKeys; onActivated: app.selectTool("hand") }
    Shortcut { sequences: win.keysOf("insertImage"); enabled: toolKeys; onActivated: imageDialog.open() }
    Shortcut { sequences: win.keysOf("redo"); enabled: docKeys; onActivated: app.redo() }
    // (the reference, while it has the keys and is written in)
    Shortcut { sequences: win.keysOf("save"); enabled: docKeys; onActivated: if (!app.saveReferenceInHand()) saveOrAsk(null) }
    Shortcut { sequences: win.keysOf("saveAs"); enabled: docKeys; onActivated: openSaveDialog(null) }
    Shortcut { sequences: win.keysOf("open"); onActivated: openDialog.open() }
    // Ctrl+N adds a page (what one needs while writing), Ctrl+Shift+N a document
    Shortcut { sequences: win.keysOf("addPage"); enabled: docKeys; onActivated: app.addPageAfterCurrent() }
    Shortcut { sequences: win.keysOf("newDocument"); onActivated: app.newDocument() }
    Shortcut { sequences: win.keysOf("addPage"); enabled: app.homeVisible; onActivated: app.newDocument() }
    Shortcut { sequences: win.keysOf("closeTab"); enabled: docKeys; onActivated: requestCloseTab(app.currentTab) }
    // The home screen (library)
    Shortcut { sequences: win.keysOf("home"); onActivated: app.homeVisible = !app.homeVisible || app.tabCount() === 0 }
    // Tabs: Ctrl+Tab / Ctrl+Shift+Tab (Shift+Tab arrives as Backtab), like browsers; also Ctrl+PgDown / Ctrl+PgUp.
    Shortcut {
        // (window context: with a second window, an application shortcut would be there twice - "ambiguous",
        // and then Qt does nothing at all)
        sequences: win.keysOf("nextTab")
        onActivated: app.nextTab()
    }
    Shortcut {
        sequences: win.keysOf("previousTab")
        onActivated: app.previousTab()
    }
    // Four or five fingers on the touch screen: the pages of the document, all open documents
    // (a touch pad reports such gestures to the desktop, not to us).
    TouchGestures {
        objectName: "touchGestures"
        window: win
        function togglePages() {
            if (app.homeVisible) return
            tabOverview.visible ? tabOverview.close() : (pageGrid.visible ? pageGrid.close() : pageGrid.open())
        }
        function toggleDocuments() {
            pageGrid.visible ? pageGrid.close() : 0
            tabOverview.visible ? tabOverview.close() : tabOverview.open()
        }
        onTapped: fingers => fingers >= 5 ? toggleDocuments() : togglePages()
        onPinchedIn: fingers => fingers >= 5 ? toggleDocuments() : togglePages()  // "zoom out": step back
        onPinchedOut: {
            if (tabOverview.visible) tabOverview.close()
            else if (pageGrid.visible) pageGrid.close()
        }
    }
    Shortcut { sequences: win.keysOf("tabOverview"); onActivated: tabOverview.visible ? tabOverview.close() : tabOverview.open() }
    // Search: all open documents (in their overview), the whole library (on the home screen) - from anywhere
    Shortcut {
        sequences: win.keysOf("searchAllDocuments")
        enabled: app.tabs.count > 0  // (a property: an invokable in a binding would not be read again)
        onActivated: { pageGrid.close(); app.homeVisible = false; tabOverview.openSearch() }
    }
    function searchLibrary() {
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(homeView.focusSearch)  // (after the home screen is shown: it puts the focus on its grid)
    }
    Shortcut { sequences: win.keysOf("searchLibrary"); onActivated: win.searchLibrary() }
    Shortcut { sequences: win.keysOf("settings"); onActivated: settingsPage.open() }
    Shortcut { sequences: win.keysOf("shortcuts"); onActivated: shortcutSheet.open() }
    // (not StandardKey.FullScreen as well: it is F11 on KDE, twice the same key is ambiguous)
    Shortcut { sequences: win.keysOf("fullScreen"); enabled: !app.homeVisible; onActivated: win.fullScreenMode = !win.fullScreenMode }
    Shortcut { sequence: "Escape"; enabled: win.fullScreenMode && !app.hasSelection && !app.presenting; onActivated: win.fullScreenMode = false }
    // Presenting: F5 starts and ends it, Escape ends it (full screen stays: a second Escape leaves that too)
    Shortcut {
        sequences: win.keysOf("present")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (app.presenting = false) : win.startPresenting()
    }
    Shortcut { sequence: "Escape"; enabled: app.presenting && !app.hasSelection; onActivated: app.presenting = false }
    // Without controls: Ctrl+F5 starts presenting so, and while presenting hides or shows the controls
    Shortcut {
        sequences: win.keysOf("presentClean")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (win.presentClean = !win.presentClean) : win.startPresenting(true)
    }
    Shortcut { sequences: win.keysOf("export"); enabled: docKeys; onActivated: openExportDialog() }
    Shortcut { sequences: win.keysOf("print"); enabled: docKeys; onActivated: printDialog.open() }
    Shortcut { sequences: win.keysOf("back"); enabled: docKeys; onActivated: app.navigateBack() }
    Shortcut { sequences: win.keysOf("forward"); enabled: docKeys; onActivated: app.navigateForward() }
    Shortcut { sequences: win.keysOf("pageGrid"); enabled: docKeys; onActivated: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
    Shortcut { sequences: win.keysOf("contents"); enabled: docKeys; onActivated: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open() }
    // (DEPRECATED: the text mode's Ctrl+Alt+E is gone with it; TextFlowPanel stays, not offered)
    Shortcut {
        // Markdown on the page (formatted while typing); pressed again while writing there: its source beside the
        // page
        sequences: win.keysOf("markdownMode"); enabled: !app.homeVisible
        onActivated: {
            if (markdownPanel.visible) { markdownPanel.close(true); return }
            if (!app.markdownOnPage) { writeButton.markdownMode = true; app.writeMarkdownOnPage(); return }
            const onPage = app.takeMarkdownFromPage()
            if (onPage.page === undefined) markdownPanel.open()
            else if (onPage.pageText) markdownPanel.open(onPage.page)
            else markdownPanel.openBox(onPage.page, onPage.x, onPage.y)
        }
    }
    Shortcut { sequences: win.keysOf("find"); onActivated: app.homeVisible ? homeView.focusSearch() : searchBar.openBar() }
    // Selected elements (the page sidebar and grid handle these keys themselves when they have the focus)
    Shortcut { sequences: win.keysOf("copy"); enabled: docKeys; onActivated: app.copySelection() }
    Shortcut { sequences: win.keysOf("cut"); enabled: docKeys; onActivated: app.cutSelection() }
    Shortcut { sequences: win.keysOf("paste"); enabled: docKeys; onActivated: app.pasteElements() }
    Shortcut { sequences: win.keysOf("deleteSelection"); enabled: docKeys && (app.hasSelection || app.noteSelected); onActivated: app.deleteSelection() }
    Shortcut { sequences: win.keysOf("selectAll"); enabled: docKeys; onActivated: app.selectAllOnPage() }
    Shortcut { sequence: "Escape"; enabled: docKeys && (app.hasSelection || app.noteSelected); onActivated: app.clearSelection() }
    Shortcut { sequences: win.keysOf("findNext"); enabled: docKeys; onActivated: app.searchNext() }
    Shortcut { sequences: win.keysOf("findPrevious"); enabled: docKeys; onActivated: app.searchPrevious() }
    Shortcut { sequences: win.keysOf("zoomIn"); enabled: docKeys; onActivated: app.zoomIn() }
    Shortcut { sequences: win.keysOf("zoomOut"); enabled: docKeys; onActivated: app.zoomOut() }
    Shortcut { sequences: win.keysOf("fitWidth"); enabled: docKeys; onActivated: app.fitWidth() }
    Shortcut { sequences: win.keysOf("realSize"); enabled: docKeys; onActivated: app.zoomToRealSize() }
    Shortcut { sequences: win.keysOf("quit"); onActivated: win.close() }
    ShortcutSheet {
        id: shortcutSheet
        onChangeRequested: { settingsPage.open(); settingsPage.showShortcuts() }
    }
}
