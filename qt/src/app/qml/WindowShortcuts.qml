// xournal-qt: the main window's keyboard shortcuts and touch gestures (part of Main.qml; the keys come from the
// shortcut settings through win.keysOf). Shortcuts act in the window whatever item holds them.
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas

Item {
    id: windowShortcuts
    // --- Esc and Android's back key: one dispatcher (qt/docs/features/zen.md, "Esc and Back") ------------------------
    // Two enabled Shortcuts of the same key are "ambiguous" to Qt, and then neither acts. So Esc and Back have ONE
    // Shortcut each, and what a press does is the first step of this list that applies (the next press: the next one):
    //   0. what is in front takes the key itself: a popup (Qt gives a modal popup, or one that closes on Esc, the key
    //      before the window's shortcuts; Back also waits while backTakers counts an open one), and an item that
    //      claims it while it has the focus (the page jump, the page grid, a search field, a text being typed)
    //   1. drawer       the page sidebar's drawer closes
    //   2. curtain      the curtain's handles hide
    //   3. selection    the selected elements, sticky note or PDF text are unselected
    //   4. snip         an armed snip is put away
    //   5. stamp        the armed to-do stamp is put away
    //   6. replay       the replay ends
    //   7. presenting   presenting ends (full screen stays: the next press leaves it)
    //   8. zen          the Zen pill closes, else Zen ends; Read ends with it (read only, and the full screen it
    //                   entered). Presenting without controls is Zen: the controls come back.
    //   9. fullScreen   full screen ends
    //  10. leave        Back only, where leaving is asked (Android): "Leave Xournal Qt?" (LeaveAppDialog)
    // Esc and Android's back key (and gesture) take the same steps (the author, 2026-10-07: "I want a consistent
    // android back behavior"); when none is left, Back on Android asks before the app is left ("leaving the app
    // should only happen after confirming it in a small dialog"); elsewhere it is the system's.
    /// Back with nothing left asks before the app is left (Android; the tests switch it on)
    property bool confirmLeave: Qt.platform.os === "android"
    /// Popups open that close on Android's back key (sheets, dialogs, the editor, the pickers count themselves through
    /// win.takeBack): Back waits for them (a second enabled Back shortcut would make the key ambiguous)
    property int backTakers: 0
    function takeBack(on) { backTakers = Math.max(0, backTakers + (on ? 1 : -1)) }
    /// The step Esc takes now ("": none, the key goes on to the item with the focus)
    readonly property string escapeStep: stepFor(false)
    /// The step Android's back key (and gesture) takes now ("": none, the system's: the app goes to the background)
    readonly property string backStep: stepFor(true)
    function stepFor(back) {
        if (back && backTakers > 0) return ""
        if (win.layout.sidebarDrawerOpen && sidebar.visible) return "drawer"
        if (docKeys && app.curtainHandles) return "curtain"
        if (docKeys && (app.edit.hasSelection || app.edit.noteSelected || app.edit.pdfTextIsSelected)) return "selection"
        if (app.snip !== "") return "snip"
        if (app.todoStamp) return "stamp"
        if (win.modes.replaying && !app.homeVisible) return "replay"
        if (app.presenting) return "presenting"
        if (win.modes.zenShown) return "zen"
        if (win.modes.fullScreenMode) return "fullScreen"
        if (back && confirmLeave) return "leave"
        return ""
    }
    function takeStep(step, back) {
        switch (step) {
        case "drawer": win.layout.showSidebar(false); break
        case "curtain": app.curtainHandles = false; break
        case "selection":
            if (app.edit.hasSelection || app.edit.noteSelected) app.clearSelection()
            if (app.edit.pdfTextIsSelected) app.edit.clearPdfTextSelection()
            break
        case "snip": app.cancelSnip(); break
        case "stamp": app.cancelTodoStamp(); break
        case "replay": app.timeline.stop(); break
        case "presenting": app.presenting = false; break
        case "zen":
            if (zenPill.visible) {
                zenPill.opened = false
                break
            }
            zenPill.opened = false
            if (win.modes.readStarted) win.modes.stopReading()
            win.modes.setZen(false)
            break
        case "fullScreen": win.modes.fullScreenMode = false; break
        case "leave": leaveAppDialog.open(); break
        }
    }
    Shortcut {
        objectName: "escapeShortcut"
        sequence: "Escape"
        enabled: windowShortcuts.escapeStep !== ""
        onActivated: windowShortcuts.takeStep(windowShortcuts.escapeStep, false)
    }
    Shortcut {
        objectName: "backShortcut"
        sequence: "Back"
        enabled: windowShortcuts.backStep !== ""
        onActivated: windowShortcuts.takeStep(windowShortcuts.backStep, true)
    }
    // Read (Zen, read only, full screen) and Zen: their keys (changeable)
    Shortcut { sequences: win.keysOf("read"); enabled: !app.homeVisible && !win.textDoc && !win.modes.replaying; onActivated: win.modes.toggleReading() }
    Shortcut { sequences: win.keysOf("zen"); enabled: !app.homeVisible; onActivated: win.modes.setZen(!win.modes.zenShown) }
    Shortcut { sequence: "Space"; enabled: win.modes.replaying && !app.homeVisible; onActivated: app.timeline.toggle() }
    Shortcut { sequence: "Left"; enabled: win.modes.replaying && !app.homeVisible; onActivated: app.timeline.skip(-5000) }
    Shortcut { sequence: "Right"; enabled: win.modes.replaying && !app.homeVisible; onActivated: app.timeline.skip(5000) }
    Shortcut { sequence: "Home"; enabled: win.modes.replaying && !app.homeVisible; onActivated: app.timeline.seek(0) }
    Shortcut { sequence: "End"; enabled: win.modes.replaying && !app.homeVisible; onActivated: app.timeline.seek(app.timeline.duration) }
    // (A is the hand's key otherwise: the tools' keys are off while replaying)
    Shortcut { objectName: "replayAudioShortcut"; sequence: "A"; enabled: win.modes.replaying && !app.homeVisible && app.timeline.hasRecordings; onActivated: app.timeline.toggleAudio() }
    // Recording (qt/docs/features/audio.md): starts or stops it for this document
    Shortcut { sequences: win.keysOf("record"); enabled: app.audio.available && !app.homeVisible; onActivated: app.audio.toggleRecording() }
    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible && !app.markdownActive && !win.modes.replaying
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
    Shortcut { sequence: "Backspace"; enabled: windowShortcuts.presentKeys && !app.edit.hasSelection; onActivated: app.previousPage() }
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
    // The curtain: out or away again (whatever is out); Esc hides its handles first (escapeStep)
    Shortcut { sequences: win.keysOf("curtain"); enabled: toolKeys && !win.textDoc; onActivated: app.toggleCurtain(app.curtain !== "" ? "" : "curtain") }
    Shortcut { sequences: win.keysOf("spotlight"); enabled: toolKeys && !win.textDoc; onActivated: app.toggleCurtain("spotlight") }
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
    // Quick note (qt/docs/features/quick-note.md): from the home screen too
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
    Shortcut { sequences: win.keysOf("searchLibrary"); onActivated: win.actions.searchLibrary() }
    Shortcut { sequences: win.keysOf("settings"); onActivated: settingsPage.open() }
    Shortcut { sequences: win.keysOf("shortcuts"); onActivated: shortcutSheet.open() }
    // (not StandardKey.FullScreen as well: it is F11 on KDE, twice the same key is ambiguous)
    Shortcut { sequences: win.keysOf("fullScreen"); enabled: !app.homeVisible; onActivated: win.modes.fullScreenMode = !win.modes.fullScreenMode }
    // Presenting: F5 starts and ends it, Esc ends it (escapeStep; full screen stays: a second Esc leaves that too)
    Shortcut {
        sequences: win.keysOf("present")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (app.presenting = false) : win.modes.startPresenting()
    }
    // Without controls: Ctrl+F5 starts presenting so, and while presenting hides or shows the controls
    Shortcut {
        sequences: win.keysOf("presentClean")
        enabled: !app.homeVisible
        onActivated: app.presenting ? (win.modes.presentClean = !win.modes.presentClean) : win.modes.startPresenting(true)
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
    Shortcut { sequences: win.keysOf("cut"); enabled: docKeys; onActivated: app.keyTarget.cutSelection() }
    Shortcut { sequences: win.keysOf("paste"); enabled: docKeys; onActivated: app.pasteElements() }
    Shortcut { sequences: win.keysOf("deleteSelection"); enabled: docKeys && (app.edit.hasSelection || app.edit.noteSelected); onActivated: app.keyTarget.deleteSelection() }
    Shortcut { sequences: win.keysOf("selectAll"); enabled: docKeys; onActivated: app.selectAllOnPage() }
    Shortcut { sequences: win.keysOf("group"); enabled: docKeys; onActivated: app.keyTarget.groupSelection() }
    Shortcut { sequences: win.keysOf("ungroup"); enabled: docKeys; onActivated: app.keyTarget.ungroupSelection() }
    // The page (the first selected page) as a high-resolution picture on the clipboard (qt/docs/features/page-files.md)
    Shortcut { sequences: win.keysOf("copyPageImage"); enabled: docKeys && !win.textDoc; onActivated: app.copyPagesAsImage(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
    Shortcut { sequences: win.keysOf("findNext"); enabled: docKeys; onActivated: app.searchNext() }
    Shortcut { sequences: win.keysOf("findPrevious"); enabled: docKeys; onActivated: app.searchPrevious() }
    Shortcut { sequences: win.keysOf("zoomIn"); enabled: docKeys; onActivated: app.keyTarget.zoomIn() }
    Shortcut { sequences: win.keysOf("zoomOut"); enabled: docKeys; onActivated: app.keyTarget.zoomOut() }
    Shortcut { sequences: win.keysOf("fitWidth"); enabled: docKeys; onActivated: app.keyTarget.fitWidth() }
    Shortcut { sequences: win.keysOf("rotateRight"); enabled: docKeys && app.canRotateCanvas; onActivated: app.rotateCanvas(90) }
    Shortcut { sequences: win.keysOf("rotateLeft"); enabled: docKeys && app.canRotateCanvas; onActivated: app.rotateCanvas(-90) }
    Shortcut { sequences: win.keysOf("realSize"); enabled: docKeys; onActivated: app.keyTarget.zoomToRealSize() }
    Shortcut { sequences: win.keysOf("quit"); onActivated: win.close() }
}
