// xournal-qt: the main window's keyboard shortcuts and touch gestures (part of Main.qml; the keys come from the
// shortcut settings through win.keysOf). Shortcuts act in the window whatever item holds them.
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas

Item {
    id: windowShortcuts
    // Esc and Android's back key close the drawer
    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: win.sidebarDrawerOpen && sidebar.visible
        onActivated: win.showSidebar(false)
    }
    // Esc leaves Zen: the pill first, a selection first loses its selection; Read ends with it (read only, and the
    // full screen it entered). Presenting: Esc ends presenting (below).
    Shortcut {
        sequence: "Escape"
        enabled: win.zenShown && !app.presenting && !win.replaying && !app.hasSelection && !app.noteSelected
                 && !app.pdfTextIsSelected && !win.sidebarDrawerOpen && !app.curtainHandles && app.snip === ""
                 && !app.todoStamp
        onActivated: {
            if (zenPill.visible) {
                zenPill.opened = false
                return
            }
            if (win.readStarted) win.stopReading()
            win.setZen(false)
        }
    }
    // Android's back key (and the back gesture) leaves Zen before anything else it does (the author, 2026-10-06: "the
    // back gesture or button should leave zen mode on android"): the controls come back (presenting: its controls);
    // Read ends with it. A popup open over the page (a sheet, a dialog, the editor) takes it first: it is in front.
    Shortcut {
        objectName: "zenBackShortcut"
        sequence: "Back"
        enabled: win.zenShown && win.backTakers === 0 && !win.sidebarDrawerOpen
        onActivated: {
            zenPill.opened = false
            if (win.readStarted) win.stopReading()
            win.setZen(false)
        }
    }
    // Read (Zen, read only, full screen) and Zen: their keys (changeable)
    Shortcut { sequences: win.keysOf("read"); enabled: !app.homeVisible && !win.textDoc && !win.replaying; onActivated: win.toggleReading() }
    Shortcut { sequences: win.keysOf("zen"); enabled: !app.homeVisible; onActivated: win.setZen(!win.zenShown) }
    Shortcut { sequence: "Escape"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.stop() }
    Shortcut { sequence: "Space"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.toggle() }
    Shortcut { sequence: "Left"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.skip(-5000) }
    Shortcut { sequence: "Right"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.skip(5000) }
    Shortcut { sequence: "Home"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.seek(0) }
    Shortcut { sequence: "End"; enabled: win.replaying && !app.homeVisible; onActivated: app.timeline.seek(app.timeline.duration) }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: app.audio.available && !app.homeVisible
        onActivated: app.audio.toggleRecording()
    }
    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible && !app.markdownActive && !replaying
    Shortcut { sequences: win.keysOf("undo"); enabled: docKeys; onActivated: app.undo() }
    // The tools on single keys: only while the page is at hand (typing into a text or a field takes its keys first;
    // the overviews and the settings search or edit what is typed)
    readonly property bool toolKeys: docKeys && !pageGrid.visible && !contentsOverview.visible && !tabOverview.visible
                                     && !settingsPage.visible
    // (the toolbox's entry of that type used last, with its color and width)
    Shortcut { sequences: win.keysOf("toolPen"); enabled: toolKeys; onActivated: app.takeToolOfType("pen") }
    // A page number: the first digit opens the jump, which takes the following keys itself. (A field or a text on
    // the page that is typed into takes its digits first.)
    component DigitKey: Shortcut {
        property int digit
        sequence: String(digit)
        enabled: windowShortcuts.toolKeys && !pageJump.visible && !app.reference.pagesShown
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
    Shortcut { sequences: ["Left", "PgUp"]; enabled: windowShortcuts.sidewaysKeys; onActivated: app.previousPage() }
    Shortcut { sequences: ["Right", "PgDown"]; enabled: windowShortcuts.sidewaysKeys; onActivated: app.nextPage() }
    Shortcut { sequence: "Home"; enabled: windowShortcuts.sidewaysKeys; onActivated: app.firstPage() }
    Shortcut { sequence: "End"; enabled: windowShortcuts.sidewaysKeys; onActivated: app.lastPage() }
    // Presenting, like PowerPoint: Space, → ↓ Page Down on, ← ↑ Page Up Backspace back (Backspace deletes what is
    // selected, if anything)
    readonly property bool presentKeys: toolKeys && app.presenting
    Shortcut { sequences: ["Space", "Right", "Down", "PgDown"]; enabled: windowShortcuts.presentKeys; onActivated: app.nextPage() }
    Shortcut { sequences: ["Left", "Up", "PgUp"]; enabled: windowShortcuts.presentKeys; onActivated: app.previousPage() }
    Shortcut { sequence: "Backspace"; enabled: windowShortcuts.presentKeys && !app.hasSelection; onActivated: app.previousPage() }
    Shortcut { sequence: "Home"; enabled: windowShortcuts.presentKeys; onActivated: app.firstPage() }
    Shortcut { sequence: "End"; enabled: windowShortcuts.presentKeys; onActivated: app.lastPage() }

    Shortcut { sequences: win.keysOf("toolEraser"); enabled: toolKeys; onActivated: app.takeToolOfType("eraser") }
    Shortcut { sequences: win.keysOf("toolHighlighter"); enabled: toolKeys; onActivated: app.takeToolOfType("highlighter") }
    Shortcut { sequences: win.keysOf("toolText"); enabled: toolKeys; onActivated: app.takeToolOfType("text") }
    Shortcut { sequences: win.keysOf("toolSelect"); enabled: toolKeys; onActivated: app.selectTool("selectRect") }
    Shortcut { sequences: win.keysOf("toolLasso"); enabled: toolKeys; onActivated: app.selectTool("selectRegion") }
    Shortcut { sequences: win.keysOf("snip"); enabled: toolKeys; onActivated: app.startSnip("rect") }
    Shortcut { sequences: win.keysOf("snipLasso"); enabled: toolKeys; onActivated: app.startSnip("lasso") }
    Shortcut { sequences: win.keysOf("copyInkText"); enabled: toolKeys; onActivated: win.toolGroups.activate("text", "copyInkText") }
    Shortcut { sequences: win.keysOf("toolHand"); enabled: toolKeys; onActivated: app.selectTool("hand") }
    Shortcut { sequences: win.keysOf("insertImage"); enabled: toolKeys; onActivated: imageDialog.open() }
    // The curtain: out or away again (whatever is out); Esc hides its handles first
    Shortcut { sequences: win.keysOf("curtain"); enabled: toolKeys && !win.textDoc; onActivated: app.toggleCurtain(app.curtain !== "" ? "" : "curtain") }
    Shortcut { sequences: win.keysOf("spotlight"); enabled: toolKeys && !win.textDoc; onActivated: app.toggleCurtain("spotlight") }
    Shortcut { sequence: "Escape"; enabled: docKeys && app.curtainHandles; onActivated: app.curtainHandles = false }
    Shortcut { sequence: "Escape"; enabled: app.snip !== "" && !app.curtainHandles; onActivated: app.cancelSnip() }
    Shortcut { sequence: "Escape"; enabled: app.todoStamp && app.snip === ""; onActivated: app.cancelTodoStamp() }
    Shortcut { sequences: win.keysOf("redo"); enabled: docKeys; onActivated: app.redo() }
    // (the reference, while it has the keys and is written in)
    Shortcut { sequences: win.keysOf("save"); enabled: docKeys; onActivated: if (!app.saveReferenceInHand()) saveOrAsk(null) }
    Shortcut { sequences: win.keysOf("saveAs"); enabled: docKeys; onActivated: openSaveDialog(null) }
    Shortcut { sequences: win.keysOf("saveWithMessage"); enabled: docKeys && !win.textDoc; onActivated: versionMessageDialog.openFor(-1) }
    // (the reference view: scroll both sides together; no key by default, one can be given in the shortcuts)
    Shortcut { sequences: win.keysOf("lockScroll"); enabled: app.reference.active && !app.homeVisible; onActivated: app.reference.toggleScrollLock() }
    Shortcut { sequences: win.keysOf("open"); onActivated: openDialog.open() }
    // Ctrl+N adds a page (what one needs while writing), Ctrl+Shift+N a document
    Shortcut { sequences: win.keysOf("addPage"); enabled: docKeys; onActivated: app.addPageAfterCurrent() }
    Shortcut { sequences: win.keysOf("newDocument"); onActivated: app.newDocument() }
    // Quick note (qt/docs/quick-note.md): from the home screen too
    Shortcut { sequences: win.keysOf("quickNote"); onActivated: app.quickNote() }
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
    Shortcut { sequences: win.keysOf("searchLibrary"); onActivated: win.searchLibrary() }
    Shortcut { sequences: win.keysOf("settings"); onActivated: settingsPage.open() }
    Shortcut { sequences: win.keysOf("shortcuts"); onActivated: shortcutSheet.open() }
    // (not StandardKey.FullScreen as well: it is F11 on KDE, twice the same key is ambiguous)
    Shortcut { sequences: win.keysOf("fullScreen"); enabled: !app.homeVisible; onActivated: win.fullScreenMode = !win.fullScreenMode }
    Shortcut { sequence: "Escape"; enabled: win.fullScreenMode && !win.zenShown && !app.hasSelection && !app.presenting && !app.curtainHandles; onActivated: win.fullScreenMode = false }
    // Presenting: F5 starts and ends it, Escape ends it (full screen stays: a second Escape leaves that too)
    Shortcut {
        sequences: win.keysOf("present")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (app.presenting = false) : win.startPresenting()
    }
    Shortcut { sequence: "Escape"; enabled: app.presenting && !app.hasSelection && !app.curtainHandles; onActivated: app.presenting = false }
    // Without controls: Ctrl+F5 starts presenting so, and while presenting hides or shows the controls
    Shortcut {
        sequences: win.keysOf("presentClean")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (win.presentClean = !win.presentClean) : win.startPresenting(true)
    }
    Shortcut { sequences: win.keysOf("export"); enabled: docKeys; onActivated: exportFlow.openExportDialog() }
    Shortcut { sequences: win.keysOf("print"); enabled: docKeys; onActivated: printDialog.open() }
    Shortcut { sequences: win.keysOf("back"); enabled: docKeys; onActivated: app.navigateBack() }
    Shortcut { sequences: win.keysOf("forward"); enabled: docKeys; onActivated: app.navigateForward() }
    Shortcut { sequences: win.keysOf("pageGrid"); enabled: docKeys; onActivated: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
    Shortcut { sequences: win.keysOf("contents"); enabled: docKeys; onActivated: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open() }
    Shortcut {
        // Markdown on the page (formatted while typing); pressed again while writing there: its source beside the
        // page
        sequences: win.keysOf("markdownMode"); enabled: !app.homeVisible
        onActivated: {
            if (markdownPanel.visible) { markdownPanel.close(true); return }
            if (!app.markdownOnPage) { app.writeMarkdownOnPage(); return }
            const onPage = app.takeMarkdownFromPage()
            if (onPage.page === undefined) markdownPanel.open()
            else if (onPage.pageText) markdownPanel.open(onPage.page)
            else markdownPanel.openBox(onPage.page, onPage.x, onPage.y)
        }
    }
    Shortcut { sequences: win.keysOf("find"); onActivated: app.homeVisible ? homeView.focusSearch() : searchBar.openBar() }
    // Find and replace: the search bar with its replace row (where text can be written; elsewhere the search alone)
    Shortcut { sequences: win.keysOf("replace"); onActivated: app.homeVisible ? homeView.focusSearch() : searchBar.openReplace() }
    // Selected elements (the page sidebar and grid handle these keys themselves when they have the focus)
    Shortcut { sequences: win.keysOf("copy"); enabled: docKeys; onActivated: app.copySelection() }
    Shortcut { sequences: win.keysOf("cut"); enabled: docKeys; onActivated: app.cutSelection() }
    Shortcut { sequences: win.keysOf("paste"); enabled: docKeys; onActivated: app.pasteElements() }
    Shortcut { sequences: win.keysOf("deleteSelection"); enabled: docKeys && (app.hasSelection || app.noteSelected); onActivated: app.deleteSelection() }
    Shortcut { sequences: win.keysOf("selectAll"); enabled: docKeys; onActivated: app.selectAllOnPage() }
    Shortcut { sequences: win.keysOf("group"); enabled: docKeys; onActivated: app.groupSelection() }
    Shortcut { sequences: win.keysOf("ungroup"); enabled: docKeys; onActivated: app.ungroupSelection() }
    // The page (the first selected page) as a high-resolution picture on the clipboard (qt/docs/page-files.md)
    Shortcut { sequences: win.keysOf("copyPageImage"); enabled: docKeys && !win.textDoc; onActivated: app.copyPagesAsImage(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
    Shortcut { sequence: "Escape"; enabled: docKeys && (app.hasSelection || app.noteSelected) && !win.sidebarDrawerOpen && !app.curtainHandles; onActivated: app.clearSelection() }
    Shortcut { sequences: win.keysOf("findNext"); enabled: docKeys; onActivated: app.searchNext() }
    Shortcut { sequences: win.keysOf("findPrevious"); enabled: docKeys; onActivated: app.searchPrevious() }
    Shortcut { sequences: win.keysOf("zoomIn"); enabled: docKeys; onActivated: app.zoomIn() }
    Shortcut { sequences: win.keysOf("zoomOut"); enabled: docKeys; onActivated: app.zoomOut() }
    Shortcut { sequences: win.keysOf("fitWidth"); enabled: docKeys; onActivated: app.fitWidth() }
    Shortcut { sequences: win.keysOf("rotateRight"); enabled: docKeys && app.canRotateCanvas; onActivated: app.rotateCanvas(90) }
    Shortcut { sequences: win.keysOf("rotateLeft"); enabled: docKeys && app.canRotateCanvas; onActivated: app.rotateCanvas(-90) }
    Shortcut { sequences: win.keysOf("realSize"); enabled: docKeys; onActivated: app.zoomToRealSize() }
    Shortcut { sequences: win.keysOf("quit"); onActivated: win.close() }
}
