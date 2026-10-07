// xournal-qt: main window (first usable version: one document, pen / highlighter / eraser / hand).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import XournalQt
import XournalQt.Canvas
import "Popups.js" as Popups
import "DevicePixels.js" as DevicePixels

ApplicationWindow {
    id: win
    // While text is written on the page, the page keeps the focus: a closing popup gives it back to whatever had it
    // when it opened (the window itself, or a button), and on Android the on-screen keyboard closes as soon as
    // something that takes no text has the focus. Text fields (the Markdown source, a search) keep it.
    onActiveFocusItemChanged: {
        const item = activeFocusItem
        if (!item || item === canvas || !canvas.textEditing || item.cursorPosition !== undefined)
            return
        for (let p = item; p; p = p.parent) {
            if (p === Overlay.overlay)
                return  // (in a popup, a sheet or a dialog: its own)
        }
        if (item === contentItem || item === contentItem.parent || item.focusPolicy !== undefined)
            Qt.callLater(function() { if (canvas.textEditing && activeFocusItem === item) canvas.forceActiveFocus() })
    }
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

    // --- the window's state (WindowInsets.qml, ViewModes.qml, ChromeLayout.qml) -------------------------------------
    /// The safe area, where the controls over the pages may go, the bottom sheets and the soft keyboard
    readonly property WindowInsets insets: WindowInsets {
        id: windowInsets
        window: win
        rightReserve: viewModes.presenterPanelWidth
    }
    /// The chrome (full or compact), full screen and the window's state, presenting, Zen, read only and Read, the replay
    readonly property ViewModes modes: ViewModes { id: viewModes }
    /// The page sidebar, the controls' room over the canvas, the source panel, the phone chrome, the command bar and
    /// the toolbox's edge
    readonly property ChromeLayout layout: ChromeLayout { id: chromeLayout }
    // The window's test and C++ API (qt/docs/review/2026-10/qml.md §1.3): main.cpp sets the safe area and the fake
    // keyboard here, the tests read and write these. The QML reads them from the state objects (win.insets.top,
    // win.modes.zen, win.layout.toolboxEdge, …).
    property alias safeTop: windowInsets.top
    property alias safeRight: windowInsets.right
    property alias safeBottom: windowInsets.bottom
    property alias safeLeft: windowInsets.left
    property alias fakeKeyboardHeight: windowInsets.fakeKeyboardHeight
    readonly property alias keyboardTop: windowInsets.keyboardTop
    readonly property alias keyboardHeight: windowInsets.keyboardHeight
    property alias fullScreenMode: viewModes.fullScreenMode
    property alias windowFullScreen: viewModes.windowFullScreen
    property alias presentClean: viewModes.presentClean
    property alias readOnly: viewModes.readOnly
    readonly property alias readOnlyOn: viewModes.readOnlyOn
    readonly property alias zen: viewModes.zen
    readonly property alias hudHidden: viewModes.hudHidden
    readonly property alias chromeMode: viewModes.chromeMode
    readonly property alias fullChrome: viewModes.fullChrome
    readonly property alias windowedVisibility: viewModes.windowedVisibility
    readonly property alias leavingFullScreen: viewModes.leavingFullScreen
    readonly property alias sidebarShown: chromeLayout.sidebarShown
    readonly property alias drawerSlide: chromeLayout.drawerSlide
    readonly property alias phoneLayout: chromeLayout.phoneLayout
    readonly property alias phoneChrome: chromeLayout.phoneChrome
    readonly property alias sourceAtBottom: chromeLayout.sourceAtBottom
    readonly property alias toolboxEdge: chromeLayout.toolboxEdge
    readonly property alias toolboxEdgeTarget: chromeLayout.toolboxEdgeTarget
    function setZen(on) { viewModes.setZen(on) }
    function startReading() { viewModes.startReading() }
    function toggleReading() { viewModes.toggleReading() }
    function startPresenting(clean) { viewModes.startPresenting(clean) }
    function showSidebar(shown) { chromeLayout.showSidebar(shown) }
    function chooseToolboxEdge(edge) { chromeLayout.chooseToolboxEdge(edge) }
    // --- the layout for the window's size (qt/docs/adaptive-layout.md) --------------------------------------------
    /// The size class (desktopWide, desktopNarrow, tabletPortrait, phonePortrait, phoneShort, tiny), the width and
    /// height classes, the touch profile and its target size (minTarget). Everything that depends on the window's size
    /// reads it from here instead of keeping a threshold of its own.
    readonly property AdaptiveLayout adaptive: AdaptiveLayout {
        objectName: "adaptiveLayout"
        window: win
        adaptive: (app.settings.revision, app.settings.get("adaptiveLayout"))
        touchSetting: (app.settings.revision, app.settings.get("touchProfile"))
    }
    /// "Save page as template…" for page index `page` (qt/docs/templates.md; the page menus)
    function openTemplateSave(page) { templateSaveDialog.openForPage(page) }
    /// Pages as files (PageFiles.qml, qt/docs/page-files.md), from the page menus: "insert" (from a file, after page
    /// `pages[0]`), "extract", "split", "images" (the pages, or the selection)
    function openPageFiles(what, pages) {
        if (what === "insert") pageFiles.chooseFile(pages.length > 0 ? pages[pages.length - 1] : app.pageNumber - 1, true)
        else if (what === "extract") pageFiles.openExtract(pages)
        else if (what === "split") pageFiles.openSplit(pages)
        else if (what === "images") pageFiles.openImages(pages)
    }
    /// What was chosen by hand in this size class ("": the automatic choice): "sidebar", "zen", "toolbox"
    function layoutChoice(what) { return (app.settings.revision, app.settings.layoutChoice(adaptive.layoutClass, what)) }
    function chooseLayout(what, value) { app.settings.setLayoutChoice(adaptive.layoutClass, what, value) }
    /// The bottom sheet that the menus (AdaptiveMenu) become in the phone classes, one for the window
    MenuSheet { id: menuSheet }

    /// The sidebar's History panel (version history)
    function showHistory() {
        sidebar.mode = "history"
        win.layout.showSidebar(true)
    }
    /// The document is a text file (a .md, a .txt): written with the keyboard, no ink tools (qt/docs/md-editor.md)
    readonly property bool textDoc: app.textDocument !== ""
    /// The cycling buttons' groups (ToolGroups.qml): the fixed tools of the toolbox (select, snip, setsquare, text)
    readonly property ToolGroups toolGroups: ToolGroups {}
    /// A button's tip with the keys of its action as they are set ("Redo (Ctrl+Shift+Z, Ctrl+Y)")
    function withKeys(text, id) {
        const keys = keysOf(id)
        return keys.length > 0 ? text + " (" + keys.join(", ") + ")" : text
    }
    /// The first key of an action as a note for a label or a sentence (" (Ctrl+P)"; "" when it has none):
    /// qsTr("Print…") + win.keyNote("print"). Labels name the keys as they are set, never keys written into the text.
    function keyNote(id) {
        const keys = keysOf(id)
        return keys.length > 0 ? " (" + keys[0] + ")" : ""
    }

    // --- the presenter view on a second screen (qt/docs/presenter-view.md) ----------------------------------------
    /// The audience's window: the slide on the other screen while this window is the console (modes.presenterConsole)
    AudienceWindow {
        id: audienceWindow
        onDigitTyped: function(digit) {
            // (typed on in the console: the audience does not see the number)
            win.requestActivate()
            pageJump.forReference = false
            pageJump.start(digit)
        }
    }
    Connections {
        target: app.presenter
        function onActiveChanged() {
            if (app.presenter.active) app.presenter.placeWindows(audienceWindow, win)
            else audienceWindow.hide()
        }
        function onScreensChanged() {
            if (app.presenter.active) app.presenter.placeWindows(audienceWindow, win)
        }
    }

    // --- saving, closing, sharing, exporting (SaveFlow.qml, ShareFlow.qml, ExportFlow.qml) ----------------------------
    SaveFlow { id: saveFlow }
    ShareFlow { id: shareFlow }
    ExportFlow { id: exportFlow }
    // (what the tests and other files call on the window)
    function openSaveDialog(then, format) { saveFlow.openSaveDialog(then, format) }
    function setUpSaveDialog(format) { saveFlow.setUpSaveDialog(format) }
    function saveChosen(url, pdfChosen, then) { saveFlow.saveChosen(url, pdfChosen, then) }
    function saveOrAsk(then) { saveFlow.saveOrAsk(then) }
    function requestCloseTab(index) { saveFlow.requestCloseTab(index) }
    function closeWindow() { saveFlow.closeWindow() }
    function sharePdfOf(file, toClipboard, withHistory) { shareFlow.sharePdfOf(file, toClipboard, withHistory) }
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
    /// The searches of selected text (the look-up menu, qt/docs/citations.md): the document's search bar with the
    /// text, run (the bar follows a search set from elsewhere); the open tabs' search in their overview; the library's
    /// search on the home screen, in the library shown.
    function searchInDocument(text) {
        if (text === "") return
        pageGrid.close()
        tabOverview.close()
        searchBar.openBar()
        app.searchQuery = text
    }
    function searchOpenTabs(text) {
        if (text === "") return
        pageGrid.close()
        app.homeVisible = false
        tabOverview.searchFor(text)
    }
    function searchLibraryFor(text) {
        if (text === "") return
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(function() { homeView.searchFor(text) })  // (after the home screen is shown, as searchLibrary())
    }
    /// "Find this paper": the library searched for the title of a bibliography entry (qt/docs/citations.md)
    function findPaper(text) { findPaperSheet.openFor(text) }
    /// arXiv: a search by title, or one paper by its ID (qt/docs/citations.md)
    function arxivSearch(title) { arxivSheet.openSearch(title) }
    function arxivPaper(id) { arxivSheet.openId(id) }

    onClosing: function(close) {
        if (!saveFlow.quitting && (app.modifiedTabs().length > 0 || app.anySaving)) {
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
            saveFlow.quitting = true
            win.close()
        }
        function onRaiseRequested() {
            if (win.visibility === Window.Minimized) { app.logWindow("raise from minimized"); win.showNormal() }
            win.raise()
            win.requestActivate()
        }
    }

    header: Column {
      id: headerColumn
      // The phone classes: the app bar instead of the tab strip (qt/docs/adaptive-layout.md, "The phone chrome")
      PhoneAppBar {
        id: phoneAppBar
        width: parent.width
        topInset: win.insets.top
        leftInset: win.insets.left
        rightInset: win.insets.right
        visible: win.layout.appBarShown
        toolsShown: win.layout.phoneChrome && win.layout.topBarShown
        toolsHeight: topBarPane.thickness
        pageShown: win.layout.toolboxInDock && win.layout.dockVertical
        onPagesRequested: pageGrid.open()
        onOverviewRequested: tabOverview.open()
        onRecentRequested: Popups.openAt(recentTabsMenu)
        // The documents used lately, the current one first: pick one
        AdaptiveMenu {
            id: recentTabsMenu
            objectName: "recentTabsMenu"
            title: qsTr("Used lately")
            titleShown: true
            property var order: []
            onAboutToShow: order = app.tabsByUse()
            Instantiator {
                model: recentTabsMenu.order
                delegate: AdaptiveMenuItem {
                    required property int modelData
                    objectName: "recentTab_" + modelData
                    text: (recentTabsMenu.order, (app.tabModified(modelData) ? "● " : "") + app.tabTitle(modelData))
                    checkable: true
                    checked: modelData === app.currentTab && !app.homeVisible
                    onTriggered: app.currentTab = modelData
                }
                onObjectAdded: function(index, object) { recentTabsMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { recentTabsMenu.removeItem(object) }
            }
        }
      }
      TabStrip {
        id: tabStrip
        objectName: "tabStrip"
        width: parent.width
        topInset: win.insets.top
        leftInset: win.insets.left
        rightInset: win.insets.right
        // (the home screen keeps it: the way back to the documents; the phone classes have the app bar instead)
        visible: (win.modes.fullChrome || app.homeVisible) && !win.layout.phoneLayout
        /// Android and iOS have one window: no tab is dragged out into a window of its own
        undockable: !win.adaptive.mobilePlatform
        onCloseRequested: function(index) { requestCloseTab(index) }
        onOverviewRequested: tabOverview.open()
        onUndockRequested: function(index) { app.undockTab(index) }
        onDockRequested: function(index) { app.dockTab(index) }
        onShareRequested: function(index) {
            app.currentTab = index
            app.homeVisible = false
            shareFlow.shareDialog.openFor("")
        }
      }
      ToolBar {
        id: topTools
        objectName: "topTools"
        width: parent.width
        visible: !app.homeVisible && !win.layout.noToolbar && !win.layout.toolsInFormatBar && !win.layout.phoneChrome && !win.modes.replaying
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: 56
      }
      // Markdown being written (a .md, Markdown on a page): its formatting tools (qt/docs/md-editor.md). A text
      // document's top bar is merged into it (F7.2): its items at the end of the row, ⋮ pinned at its end
      MarkdownFormatBar {
        id: formatBar
        // On a phone, while the soft keyboard is open for the page's Markdown, right above the keyboard (in the
        // footer: the keyboard accessory bar of Obsidian, iA Writer and Google Docs), within reach of the thumbs
        parent: docked ? footerColumn : headerColumn
        readonly property bool docked: shown && win.layout.phoneLayout && win.insets.keyboardOpen && canvas.activeFocus
        width: parent ? parent.width : 0
        leftInset: win.insets.left
        rightInset: win.insets.right
        readonly property bool shown: !app.homeVisible && !app.presenting && !win.modes.hudHidden && !markdownPanel.visible
                                      && (app.markdownOnPage || (app.textDocument === "markdown" && app.textEditable) || app.textNotes)
        visible: shown
        // (a text document: its commands at the end of the row, the top bar; nothing folds, the row scrolls)
        holdsCommands: win.layout.toolsInFormatBar
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
    // The footer: the phone's dock, a phone's format bar above the soft keyboard
    footer: Column {
      id: footerColumn
      // Room for the soft keyboard: what is above the footer ends above it (the pages, the source, the pills)
      bottomPadding: win.insets.keyboardHeight
    }
    // The phone's tool dock: at the bottom (in the footer, above the navigation bar), or a rail at the right side when
    // the phone is held sideways (qt/docs/adaptive-layout.md, "The phone chrome")
    PhoneDock {
        id: phoneDock
        vertical: win.layout.dockVertical
        visible: win.layout.dockShown
        parent: vertical ? win.contentItem : footerColumn
        width: vertical ? implicitWidth : (parent ? parent.width : 0)
        height: vertical ? (parent ? parent.height : 0) : (visible ? implicitHeight : 0)
        x: vertical && parent ? parent.width - width : 0
        z: 3
        safeBottom: win.insets.bottom
        safeLeft: win.insets.left
        safeRight: win.insets.right
        onPagesRequested: pageGrid.open()
        hostsToolbox: win.layout.toolboxInDock
    }
    // The phone chrome with the soft keyboard open: the dock is gone, undo and redo stay one tap away at the end of the
    // format bar right above the keyboard
    Row {
        id: keyboardUndo
        objectName: "keyboardUndo"
        parent: formatBar.trailing
        readonly property bool shown: win.layout.phoneChrome && formatBar.docked
        visible: shown
        width: shown ? implicitWidth : 0
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        spacing: 0
        IconButton {
            objectName: "keyboardUndoButton"
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-undo"
            label: qsTr("Undo")
            tip: win.withKeys(qsTr("Undo"), "undo")
            enabled: app.canUndo
            onClicked: app.undo()
        }
        IconButton {
            objectName: "keyboardRedoButton"
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-redo"
            label: qsTr("Redo")
            tip: win.withKeys(qsTr("Redo"), "redo")
            enabled: app.canRedo
            onClicked: app.redo()
        }
    }
    // A text document with the toolbox: undo and redo at the start of its format bar
    Row {
        objectName: "formatUndo"
        parent: formatBar.leading
        visible: win.layout.undoPlace === "formatBar"
        width: visible ? implicitWidth : 0
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        IconButton {
            objectName: "formatUndoButton"
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-undo"
            label: qsTr("Undo")
            tip: win.withKeys(qsTr("Undo"), "undo")
            enabled: app.canUndo
            onClicked: app.undo()
        }
        IconButton {
            objectName: "formatRedoButton"
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-redo"
            label: qsTr("Redo")
            tip: win.withKeys(qsTr("Redo"), "redo")
            enabled: app.canRedo
            onClicked: app.redo()
        }
        ToolSeparator {}
    }
    // The table editor of the formatting bar (the notes' canvas; the editor beside the page has its own)
    MarkdownTableEditor {
        id: tableEditor
        onClosed: if (formatBar.visible) canvas.forceActiveFocus()
    }

    // The toolbox docked at the left or the right side (its strip; at the top or the bottom it is in toolboxRow)
    Rectangle {
        id: sideTools
        objectName: "sideTools"
        readonly property bool holdsToolbox: win.layout.toolboxDocked && win.layout.toolboxVertical
        visible: holdsToolbox
        width: !visible ? 0 : toolboxPane.thickness + (win.layout.sideEdge === "right" ? win.insets.right : win.insets.left)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: win.layout.sideEdge === "right" ? parent.width - width : 0
        color: "#ffffff"
    }
    // The toolbox docked at the top or the bottom (at a side it is in sideTools)
    Rectangle {
        id: toolboxRow
        objectName: "toolboxRow"
        readonly property bool holdsToolbox: win.layout.toolboxDocked && !win.layout.toolboxVertical
        visible: holdsToolbox
        width: parent.width
        height: visible ? toolboxPane.thickness + (win.layout.toolboxEdge === "bottom" ? win.insets.contentBottomInset : 0) : 0
        y: win.layout.toolboxEdge === "bottom" ? parent.height - height : 0
        z: 3
        color: "#ffffff"
    }
    // The toolbox (qt/docs/toolbox.md): the user's own tools; docked beside the page in the full chrome
    Toolbox {
        id: toolboxPane
        parent: win.layout.toolboxInDock ? phoneDock.toolboxSlot : win.layout.toolboxFloating ? win.contentItem
                : sideTools.holdsToolbox ? sideTools : toolboxRow
        visible: win.layout.toolboxShown
        // (the phone's dock: at the bottom, or a rail at the right held sideways)
        edge: win.layout.toolboxInDock ? (win.layout.dockVertical ? "right" : "bottom") : win.layout.toolboxEdge
        compact: win.layout.toolboxInDock
        // (the phone's dock: "+" at the end of its items, the room goes to the tools)
        addInline: win.layout.toolboxInDock
        peer: topBarPane
        floating: win.layout.toolboxFloating
        moreShown: floating
        z: floating ? 60 : 0
        // Floating: no longer than its tools, centred along its edge, 8 px off it (clear of the tab dots at the top
        // and the view pill at the bottom); docked: the whole edge
        readonly property real floatTop: win.insets.controlsTop + (fullScreenTabs.visible ? fullScreenTabs.height : 0) + 8
        // (the view pill sits 24 px above the bottom: the rail ends 8 px above it, however tall the pill is)
        readonly property real floatBottom: win.insets.controlsBottom - Math.max(72, viewPill.height + 24 + 8)
        // (as long as its items; when they do not fit, it scrolls and ends through the middle of a cell)
        readonly property real floatLength: vertical ? lengthFor(floatBottom - floatTop)
                                                     : lengthFor(win.insets.controlsRight - win.insets.controlsLeft - 16)
        x: compact ? 0 : floating ? (edge === "right" ? win.insets.controlsRight - width - 8 : edge === "left" ? win.insets.controlsLeft + 8
                                        : Math.round((win.insets.controlsLeft + win.insets.controlsRight - width) / 2))
           : !vertical ? win.insets.left : edge === "left" ? win.insets.left : 0
        y: compact ? 0 : floating ? (edge === "top" ? floatTop : edge === "bottom" ? win.insets.controlsBottom - height - 8
                                      : Math.round(Math.max(floatTop, (floatTop + floatBottom - height) / 2)))
           : 0
        width: compact ? (parent ? parent.width : 0)
               : vertical ? thickness : floating ? floatLength : (parent ? parent.width - win.insets.left - win.insets.right : 0)
        height: compact ? (parent ? parent.height : 0)
                : !vertical ? thickness : floating ? floatLength : (parent ? parent.height : 0)
        // (a rail's end clear of the navigation bar)
        endInset: vertical && !floating ? Math.max(0, win.contentItem.height - win.insets.controlsBottom) : 0
        // (the app's items on the rail are the window's buttons, lent to it while it is shown: hand, select, snip, mark
        // PDF text at a first start; the others are the top bar's, qt/rail-scroll)
        appButtons: toolArea.slots
        lending: win.layout.toolboxShown
        onPagesRequested: pageGrid.open()
        onEditRequested: function(entry, button) { toolboxMenus.toolEditor.openFor(entry, button, edge) }
        onMenuRequested: function(entry, button, pos) { toolboxMenus.toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolboxMenus.toolTypeMenu.ask("add", "", button, "rail") }
        onMoreRequested: function(button) { toolboxMenus.toolboxMoreMenu.openMenu(undefined, button) }
        onGripMoved: function(pos) { win.layout.toolboxEdgeTarget = win.layout.edgeAt(pos) }
        // (a tool carried onto another and held there: a group; the snackbar can take it back)
        onGrouped: function(groupId, before) { toolboxMenus.grouped(before) }
        onRemoved: function(entry, before) { toolboxMenus.leftTheBars(entry, before) }
        onGripDropped: function(pos) {
            const edge = win.layout.edgeAt(pos)
            win.layout.toolboxEdgeTarget = ""
            if (edge !== win.layout.toolboxEdge) win.layout.chooseToolboxEdge(edge)
        }
    }
    // The edge highlighted while the grip is dragged
    Rectangle {
        objectName: "toolboxEdgeHighlight"
        visible: win.layout.toolboxEdgeTarget !== ""
        z: 95
        readonly property string edge: win.layout.toolboxEdgeTarget
        readonly property real band: toolboxPane.thickness
        x: edge === "right" ? parent.width - band : 0
        y: edge === "bottom" ? parent.height - band : 0
        width: edge === "left" || edge === "right" ? band : parent.width
        height: edge === "top" || edge === "bottom" ? band : parent.height
        color: Qt.rgba(Material.accentColor.r, Material.accentColor.g, Material.accentColor.b, 0.22)
        border.width: 2
        border.color: Material.accentColor
    }
    ToolboxMenus { id: toolboxMenus }
    /// An entry's name for people ("Pen · Body", "Arrow", "Eraser (whiteout)")
    function toolEntryName(entry) { return toolboxPane.entryName(entry) }
    AppButtons { id: toolArea }
    // ⋮: pinned at the very end of the top bar (a text document: of its format bar; the phone chrome: of the app bar)
    MoreMenu {
        id: toolEnd
        parent: win.layout.phoneChrome ? phoneAppBar.moreSlot : win.layout.toolsInFormatBar ? formatBar.trailing : topBarPane.trailingTail
        y: win.layout.toolsInFormatBar && !win.layout.phoneChrome ? -2 : 0
    }
    // The top bar (qt/docs/toolbox.md, "The top bar"; qt/top-bar): the other list of the arrangement, in the user's order
    // with its dividers and groups; it scrolls sideways as the rail does, "+" (the catalog) and ⋮ pinned at its end. In
    // a text document it is the end of the format bar's row; on a phone, in the app bar
    Toolbox {
        id: topBarPane
        bar: "top"
        edge: "top"
        peer: toolboxPane
        /// In a text document: inside the format bar's row (that row scrolls), as long as its items
        readonly property bool inline: win.layout.toolsInFormatBar && !win.layout.phoneChrome
        parent: win.layout.phoneChrome ? phoneAppBar.toolsSlot : inline ? formatBar.commandsSlot : topTools
        visible: win.layout.topBarShown
        headShown: win.layout.undoPlace === "toolBar"
        // (in the format bar: its buttons' size)
        cell: inline ? 40 : win.adaptive.touchProfile ? win.adaptive.minTarget : 44
        x: inline || win.layout.phoneChrome ? 0 : win.insets.left + 2
        width: inline ? naturalLength : parent ? parent.width - (win.layout.phoneChrome ? 0 : win.insets.left + win.insets.right + 4) : 0
        height: parent ? parent.height : 0
        appButtons: toolArea.slots
        lending: win.layout.topBarShown
        onEditRequested: function(entry, button) { toolboxMenus.toolEditor.openFor(entry, button, "top") }
        onMenuRequested: function(entry, button, pos) { toolboxMenus.toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolboxMenus.toolTypeMenu.ask("add", "", button, "top") }
        onGrouped: function(groupId, before) { toolboxMenus.grouped(before) }
        onRemoved: function(entry, before) { toolboxMenus.leftTheBars(entry, before) }
    }
    /// A popup that closes on Android's back key opened (on) or closed: the Back dispatcher waits for it
    /// (WindowShortcuts.backTakers)
    function takeBack(on) { windowShortcuts.takeBack(on) }


    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: win.layout.toolboxTop
        anchors.bottomMargin: win.layout.toolboxBottom
        // As a drawer it slides in from the left edge (win.layout.drawerSlide); clear of a cut-out at the window's left edge
        // (sidebarLeftFill has the sidebar's color there)
        x: (win.layout.sideEdge === "left" && sideTools.width > 0 ? sideTools.width : win.insets.left)
           - (win.layout.sidebarDocked ? 0 : Math.round((1 - win.layout.drawerSlide) * (width + win.insets.left)))
        width: win.layout.sidebarDocked ? 210 : win.layout.drawerWidth
        bottomInset: Math.max(0, height - win.insets.controlsBottom)
        visible: win.modes.fullChrome && (win.layout.sidebarShown || (!win.layout.sidebarDocked && win.layout.drawerSlide > 0))
        // As a drawer (no room beside the page): over the page, below the home screen; it closes once a page is picked
        z: !win.layout.sidebarDocked ? 49 : 0
        onPagePicked: if (!win.layout.sidebarDocked) win.layout.showSidebar(false)
        onVersionMessageRequested: function(id) { versionMessageDialog.openFor(id) }
        // The drawer's pin, just outside its edge: keep the sidebar beside the page at this window size
        IconButton {
            objectName: "sidebarPin"
            visible: !win.layout.sidebarDocked
            x: parent.width + 8
            y: 8
            iconName: "xopp-sidebar-show"
            tip: qsTr("Keep the pages beside the page at this window size")
            onClicked: win.layout.dockSidebar()
            background: Rectangle { radius: 12; color: parent.pressed ? "#e8e8e8" : "#ffffff"; border.color: "#d5d8dc" }
        }
    }
    // The sidebar's color under a cut-out at the window's left edge (the sidebar itself is beside it)
    Rectangle {
        objectName: "sidebarLeftFill"
        visible: sidebar.visible && win.insets.left > 0 && !(win.layout.sideEdge === "left" && sideTools.width > 0)
        x: sidebar.x - width
        width: win.insets.left
        anchors.top: sidebar.top
        anchors.bottom: sidebar.bottom
        z: sidebar.z
        color: sidebar.color
    }
    SidebarArrow { id: sidebarArrow }
    SidebarScrim {}

    DocumentCanvas {
        id: canvas
        objectName: "canvas"
        // The canvas area, or the main document's side of it when the tab shows a reference beside it
        x: referenceSplit.x + referenceSplit.mainX
        y: referenceSplit.y + referenceSplit.mainY
        width: referenceSplit.mainWidth
        height: referenceSplit.mainHeight
        clip: true  // zoomed-in pages must not paint over the sidebar
        view: app.view
        darkPages: app.darkPagesShown  // (qt/docs/dark-pages.md)
        // (a version cut out of its file, compared or shown: read-only)
        readingOnly: win.modes.readOnlyOn || win.modes.replaying || app.viewingVersion
        snapVertically: win.modes.readOnlyOn && !app.presenting
        // Reading: the edges turn the pages (a tap the page does not take otherwise; readingTapFields); in Zen a
        // finger's tap in the middle opens the dot's pill (after the double tap's time: a double tap zooms)
        edgeTapWidth: readingTapFields.visible ? readingTapFields.fieldWidth : 0
        onEdgeTapped: function(side) { readingTapFields.turn(side) }
        onMiddleTapped: function(pos, count) {
            if (count > 1) zenPillDelay.stop()
            else if (win.modes.zenShown && zenDot.visible) zenPillDelay.restart()
        }
        // A stroke tried while read only: said once, at the pen (qt/docs/zen.md)
        onWritingRefused: function(pos) { if (win.modes.readOnlyOn) readOnlyNote.tell(pos) }

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
        // Presenting with two screens, zoomed in, the audience following (qt/docs/presenter-view.md): a thin frame
        // around what the audience sees (the screens' shapes differ, and the slide's edge cuts what they see)
        Rectangle {
            objectName: "audienceFrame"
            readonly property rect shown: app.presenter.audienceFrame
            visible: win.modes.presenterConsole && shown.width > 0 && shown.height > 0
            x: shown.x
            y: shown.y
            width: shown.width
            height: shown.height
            color: "transparent"
            border.width: 2
            border.color: "#f29900"
        }
        // The mouse rests on a formula of a Markdown text that cannot be drawn (shown as its source, in red): why
        ToolTip {
            objectName: "mathErrorTip"
            visible: canvas.mathError !== ""
            text: qsTr("This formula cannot be drawn: %1").arg(canvas.mathError)
            x: Math.max(0, Math.min(canvas.mathErrorRect.x, canvas.width - width))
            y: canvas.mathErrorRect.y + canvas.mathErrorRect.height + 4
        }
        // A sticky note's text written below the note's bottom: it is clipped there, so the window says why nothing
        // shows (a label, not a dialog: typing goes on; the note is not made bigger by itself)
        Rectangle {
            objectName: "noteTextHint"
            visible: canvas.noteTextHint.width > 0
            readonly property rect note: canvas.noteTextHint
            x: Math.max(4, Math.min(note.x, canvas.width - width - 4))
            y: Math.min(note.y + note.height + 6, canvas.height - height - 4)
            width: noteTextHintLabel.implicitWidth + 20
            height: noteTextHintLabel.implicitHeight + 10
            radius: 4
            color: "#e6505357"
            Label {
                id: noteTextHintLabel
                anchors.centerIn: parent
                text: qsTr("The text is longer than the note: make the note bigger")
                color: "#ffffff"
                font.pixelSize: 13
            }
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
    // Presenting with two screens: the console's panel beside the page (qt/docs/presenter-view.md)
    PresenterPanel {
        id: presenterPanel
        visible: win.modes.presenterConsole
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: win.insets.right
        width: win.modes.presenterPanelWidth
        z: 4
        onStopRequested: app.presenting = false
    }
    // Reference mode: another document beside this one (the canvas area is split)
    ReferenceSplit {
        id: referenceSplit
        bottomInset: y + height - win.insets.controlsBottom
        anchors.top: parent.top
        anchors.topMargin: win.layout.toolboxTop
        anchors.bottom: win.layout.sourcePanel && win.layout.sourceAtBottom ? win.layout.sourcePanel.top : parent.bottom
        anchors.bottomMargin: win.layout.sourcePanel && win.layout.sourceAtBottom ? 0 : win.layout.toolboxBottom
        anchors.right: win.modes.presenterConsole ? presenterPanel.left
                       : win.layout.sourcePanel && !win.layout.sourceAtBottom ? win.layout.sourcePanel.left
                       : win.layout.dockRail ? phoneDock.left
                       : (win.layout.sideEdge === "right" ? sideTools.left : parent.right)
        anchors.left: sidebar.visible && win.layout.sidebarDocked ? sidebar.right
                      : (win.layout.sideEdge === "left" ? sideTools.right : parent.left)
    }

    ShownFileNote { id: shownFileNote }

    ViewPill { id: viewPill }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
        // (its pill above the navigation bar and the keyboard)
        bottomInset: canvas.y + canvas.height - win.layout.canvasControlsBottom
        rightInset: canvas.x + canvas.width - win.layout.canvasControlsRight
        // The phone chrome: the contents and the zoom are here (its page number opens the grid)
        phoneTools: win.layout.phoneChrome
        onContentsRequested: contentsOverview.open()
        onZoomRequested: Popups.openAt(viewPill.fitMenu)
    }

    TabDragHint {}
    // Markdown box, or the Markdown of a page: the same place
    MarkdownPanel {
        id: markdownPanel
        atBottom: win.layout.sourceAtBottom
        // (its text and buttons clear of the navigation bar and a cut-out at the window's edges)
        bottomPadding: win.insets.contentBottomInset
        rightPadding: x + width >= win.contentItem.width - 0.5 ? win.insets.right : 0
        leftPadding: atBottom && x < 0.5 ? win.insets.left : 0
        anchors.bottom: parent.bottom
        anchors.bottomMargin: win.layout.toolboxBottom
        anchors.right: win.layout.dockRail ? phoneDock.left : win.layout.sideEdge === "right" ? sideTools.left : parent.right
        width: !visible ? 0 : atBottom ? referenceSplit.width : win.layout.sourceSideWidth
        height: atBottom ? win.layout.sourceBottomHeight : parent.height - win.layout.toolboxTop - win.layout.toolboxBottom
    }
    SourceDivider { id: sourceDivider }
    Connections {
        target: app
        // The text tool tapped a Markdown box
        function onMarkdownRequested(page) {
            if (!markdownPanel.visible || app.markdownPage !== page || !app.markdownIsPageText) markdownPanel.open(page)
        }
        // A Markdown text box (or a place for a new one)
        function onMarkdownBoxRequested(page, x, y) {
            markdownPanel.openBox(page, x, y)
        }
    }
    ContentsOverview {
        id: contentsOverview
        anchors.fill: canvas
        bottomInset: canvas.y + canvas.height - win.layout.canvasControlsBottom
        rightInset: canvas.x + canvas.width - win.layout.canvasControlsRight
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
        avoid: viewPill
        bottomLimit: win.insets.controlsBottom
        onStickerRequested: stickerSaveDialog.openForSelection()
    }

    // The selected sticky note: its color, cover mode, delete.
    NotePill {
        id: notePill
        objectName: "notePill"
        canvasItem: canvas
        avoid: viewPill
        bottomLimit: win.insets.controlsBottom
        onImageRequested: imageDialog.open()
        onStickerRequested: stickerSaveDialog.openForSelection()
        hidden: pageGrid.visible
    }
    // "Save as sticker…" of the pills (qt/docs/stickers.md)
    StickerSaveDialog { id: stickerSaveDialog }

    // Handwriting copied as text (the text tools' second tool, "Copy as text" of the selection): the text near it
    InkTextToast {
        id: inkTextToast
        objectName: "inkTextToast"
        canvasItem: canvas
        referenceItem: referenceSplit.referenceCanvas
        roomTop: win.insets.controlsTop
        roomBottom: win.insets.controlsBottom
    }

    // Selected PDF text: mark or copy it (at the text, going along with it).
    PdfTextPill {
        id: pdfTextBar
        objectName: "pdfTextBar"
        canvasItem: canvas
        hidden: pageGrid.visible || contentsOverview.visible
    }

    NavPill { id: navPill }

    LinkPopup { id: linkPopup }

    SearchBar {
        id: searchBar
        objectName: "searchBar"
        // Find and replace (qt/docs/md-editor.md): not while reading; on the source beside the page while it is open
        replaceAllowed: !win.modes.readOnlyOn
        sourcePanel: markdownPanel.visible ? markdownPanel : null
        onNotice: function(text, undo) {
            snackbar.show(text, false, undo ? qsTr("Undo") : "", undo ? function() { app.undo() } : null)
        }
        anchors.top: canvas.top
        anchors.topMargin: 12 + win.layout.canvasControlsTop - canvas.y
        anchors.horizontalCenter: canvas.horizontalCenter
        width: Math.min(implicitWidth, win.layout.canvasControlsRight - win.layout.canvasControlsLeft - 16)
    }

    // Where the link under the mouse or the hovering pen leads (qt/docs/links.md, "Links with the mouse")
    LinkStatusLine {
        canvasItem: canvas
        visible: !win.modes.hudHidden
    }
    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    CanvasScrollBars {
        canvasItem: canvas
        // (beside the strip that brings a right tool bar back, not under it; clear of the safe area's insets)
        rightInset: Math.max(toolbarShow.visible && toolbarShow.side === "right" ? toolbarShow.width : 0,
                             canvas.x + canvas.width - win.layout.canvasControlsRight)
        leftInset: win.layout.canvasControlsLeft - canvas.x
        topInset: win.layout.canvasControlsTop - canvas.y
        bottomInset: canvas.y + canvas.height - win.layout.canvasControlsBottom
        hidden: pageGrid.visible || app.presenting  // (presenting: no scroll bars)
    }

    FileDialog {
        id: openDialog
        objectName: "openDialog"
        title: qsTr("Open document or PDF")
        currentFolder: app.openFolder()
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf *.md *.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("Xournal++ files (*.xopp *.xoj)"), qsTr("PDF files (*.pdf)"), qsTr("Markdown files (*.md)"),
                      qsTr("Images (*.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("Shared folders (*.zip)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: app.openUrls(selectedFiles)
    }
    // A zip opened (here, with the app, dropped): "Open in library…" (OpenZipDialog.qml)
    OpenZipDialog { objectName: "openZip" }
    ProtectionDialogs { id: protectionDialogs }
    DocumentNotices { id: documentNotices }

    FileDialog {
        id: imageDialog
        title: qsTr("Insert image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"), qsTr("All files (*)")]
        onAccepted: app.insertImage(selectedFile)
    }
    StartupFlow { id: startupFlow }
    Component.onCompleted: startupFlow.start()
    FullScreenTabs { id: fullScreenTabs }
    TabToast { id: tabToast }

    PresentPageIndicator { id: presentIndicator }
    // --- Zen (qt/docs/zen.md) ----------------------------------------------------------------------------------------
    ZenDot { id: zenDot }
    // Where the mouse or the pen counts as near the dot (looked through by the page: it writes there as anywhere)
    Item {
        objectName: "zenNear"
        readonly property bool inputTransparent: true
        visible: zenDot.visible
        z: 90
        x: zenDot.x
        y: zenDot.y + zenDot.height - height
        width: 128
        height: 128
        HoverHandler { id: zenNear; acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus }
    }
    // While the pill is open: a tap on the page closes it (and writes nothing)
    MouseArea {
        objectName: "zenPillCatcher"
        visible: zenPill.visible
        z: 91
        x: canvas.x
        y: canvas.y
        width: canvas.width
        height: canvas.height
        onReleased: zenPill.opened = false
        onCanceled: zenPill.opened = false
    }
    /// In Zen with read only on, a finger's tap in the middle of the page opens the pill (once it is no double tap)
    Timer { id: zenPillDelay; interval: 380; onTriggered: if (zenDot.visible) zenPill.opened = true }
    ZenPill { id: zenPill }
    ReadingFields { id: readingTapFields }
    ReadOnlyNote { id: readOnlyNote }
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
        // (above the pills, the navigation bar and the keyboard)
        anchors.bottomMargin: 96 + canvas.y + canvas.height - win.layout.canvasControlsBottom
    }
    VersionMessageDialog { id: versionMessageDialog }

    // Library and recent documents: over the document area while shown.
    // The home screen's color under a cut-out or the navigation bar at a side (the home screen itself is beside it)
    Rectangle {
        anchors.fill: parent
        z: 50
        visible: homeView.visible && (win.insets.left > 0 || win.insets.right > 0)
        color: homeView.color
    }
    HomeView {
        id: homeView
        anchors.fill: parent
        anchors.leftMargin: win.insets.left
        anchors.rightMargin: win.insets.right
        z: 50
        visible: app.homeVisible
        onOpenFileRequested: openDialog.open()
        onSettingsRequested: settingsPage.open()
        // A PDF card: the file itself; a card of notes (also a PDF with its .xopp): opened, then shared as a document
        onShareRequested: function(path) {
            if (path.toLowerCase().endsWith(".pdf") || app.sharedTextFile(path) !== "") {
                shareFlow.shareDialog.openFor(path)  // (a PDF, a Markdown or text file: the file itself)
            } else if (app.openPath(path)) {
                shareFlow.shareDialog.openFor("")
            }
        }
    }

    ToolbarToggle {}
    ToolbarShow { id: toolbarShow }

    // The setsquare / compass: what it can do, and putting it aside for a moment
    GeometryPill {
        id: geometryPill
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.topMargin: 12 + win.layout.canvasControlsTop - canvas.y
        anchors.rightMargin: 20 + canvas.x + canvas.width - win.layout.canvasControlsRight
        z: 57
    }
    // The curtain: its handles, taking it away (below the setsquare's pill when that is out too)
    // A recording runs, a recording plays (qt/docs/audio.md): at the top of the canvas, in the middle
    // (below the toolbox where it floats at the top, in the middle too: full screen, presenting)
    RecordingPill {
        id: recordingPill
        anchors.top: canvas.top
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.topMargin: 12 + win.layout.audioPillsTop - canvas.y
        z: 57
    }
    PlaybackPill {
        anchors.top: recordingPill.visible ? recordingPill.bottom : canvas.top
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.topMargin: recordingPill.visible ? 8 : 12 + win.layout.audioPillsTop - canvas.y
        z: 57
    }
    // The microphone refused by the system (macOS, Android): where to allow it
    MicrophoneDialog {}
    // The replay of the timeline (qt/docs/timeline.md): its play bar at the bottom of the page, above the navigation
    // bar and clear of a cut-out (the safe area), off the side edges (where Android's back gesture starts); the view
    // pill, the tools and the phone's dock are put away meanwhile (hudHidden, dockShown)
    TimelineBar {
        id: timelineBar
        touch: win.adaptive.touchProfile
        readonly property real side: win.layout.phoneLayout ? 12 : 16
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: (win.layout.phoneLayout ? 10 : 16) + canvas.y + canvas.height - win.layout.canvasControlsBottom
        x: Math.round((win.layout.canvasControlsLeft + win.layout.canvasControlsRight - width) / 2)
        width: Math.min(win.layout.canvasControlsRight - win.layout.canvasControlsLeft - 2 * side, 960)
        z: 92
    }
    CurtainPill {
        anchors.top: geometryPill.visible ? geometryPill.bottom : canvas.top
        anchors.right: canvas.right
        anchors.topMargin: geometryPill.visible ? 8 : 12 + win.layout.canvasControlsTop - canvas.y
        anchors.rightMargin: 20 + canvas.x + canvas.width - win.layout.canvasControlsRight
        z: 57
    }
    InsertPagesDialog { id: insertPagesDialog }
    // Page templates (qt/docs/templates.md): the picker (in the middle of the window, a sheet on a phone) and saving
    StickerPicker { id: templatePicker; mode: "templates" }
    TemplateSaveDialog { id: templateSaveDialog; parent: Overlay.overlay }
    PageFiles { id: pageFiles }
    PrintDialog { id: printDialog }
    ChapterDialog { id: chapterDialog }
    RenameDialog { id: renameDocumentDialog }
    TagsDialog { id: documentTagsDialog }
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
    PageSizeDialog { id: pageSizeDialog }
    Connections {
        target: app
        function onPageSizeRequested(pages) { pageSizeDialog.openFor(pages) }
    }
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
        onIntroRequested: startupFlow.introDialog.show()
        onTutorialRequested: app.openTutorial()
        onRestartTutorialRequested: startupFlow.restartTutorialDialog.open()
    }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
        onCloseAllRequested: app.tabs.count > 1 ? saveFlow.closeAllDialog.open() : saveFlow.closeAllTabs()
        onLibrarySearchRequested: win.searchLibrary()
    }

    WindowShortcuts { id: windowShortcuts }
    // The keys come from the shortcut settings (app.shortcuts); reading its revision keeps the bindings fresh.
    function keysOf(id) { return (app.shortcuts.revision, app.shortcuts.keys(id)) }
    function searchLibrary() {
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(homeView.focusSearch)  // (after the home screen is shown: it puts the focus on its grid)
    }
    ShortcutSheet {
        id: shortcutSheet
        onChangeRequested: { settingsPage.open(); settingsPage.showShortcuts() }
    }
}
