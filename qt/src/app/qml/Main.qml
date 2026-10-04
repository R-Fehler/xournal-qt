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
import "ToolBarPlan.js" as ToolBarPlan

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

    property var afterDiscardCheck: null

    // --- safe areas and the soft keyboard (qt/docs/adaptive-layout.md, "Safe areas", "The soft keyboard") ----------
    /// The parts of the window under the system's bars and a camera cut-out (edge to edge on Android and iOS; set by
    /// main.cpp from the window's safe area margins on Qt 6.9+, 0 elsewhere; the tests set them by hand): the status
    /// bar at the top, the navigation or gesture bar at the bottom, a cut-out at a side when the phone is held
    /// sideways. The controls stay clear of them; the pages are drawn under them (edge to edge).
    QtObject {
        id: safeInsetsObject
        objectName: "safeInsets"
        property real top: 0
        property real right: 0
        property real bottom: 0
        property real left: 0
    }
    property alias safeInsets: safeInsetsObject
    property alias safeTop: safeInsetsObject.top
    property alias safeRight: safeInsetsObject.right
    property alias safeBottom: safeInsetsObject.bottom
    property alias safeLeft: safeInsetsObject.left
    /// Where the controls over the pages may go, in the content item's coordinates (the pages under the header and
    /// above the footer): clear of the safe area's insets, and above the soft keyboard
    readonly property real controlsLeft: safeLeft
    readonly property real controlsRight: contentItem.width - safeRight
    readonly property real controlsTop: Math.max(0, safeTop - contentItem.y)
    readonly property real controlsBottom: Math.min(contentItem.height, height - safeBottom - contentItem.y,
                                                    keyboardTop - contentItem.y)
    /// How much of the content item's bottom lies under the bottom inset (0 where the footer took it: the dock, the
    /// tool bar at the bottom, the room for the keyboard)
    readonly property real contentBottomInset: Math.max(0, contentItem.y + contentItem.height - (height - safeBottom))
    /// A bottom sheet of the phone classes (the overlay's coordinates): as wide as the safe area (at most 640 px) and
    /// centred in it, resting on the soft keyboard while it is open, else on the window's edge with room for the
    /// navigation bar below its content (MenuSheet, BottomSheet, the page menu, the palette and the widths)
    readonly property real sheetWidth: Math.min(width - safeLeft - safeRight, 640)
    readonly property real sheetX: safeLeft + Math.round((width - safeLeft - safeRight - sheetWidth) / 2)
    readonly property real sheetBottom: keyboardTop
    readonly property real sheetBottomPadding: keyboardOpen ? 0 : safeBottom
    /// A soft keyboard of this height at the window's bottom instead of the real one (the tests; XQT_FAKE_KEYBOARD)
    property real fakeKeyboardHeight: 0
    /// The top of the soft keyboard in the window, while it is open (else the window's height). Android reports the
    /// keyboard in the screen's pixels. Where the platform makes the window smaller instead, it is the window's height.
    readonly property real keyboardTop: {
        if (fakeKeyboardHeight > 0) return height - fakeKeyboardHeight
        const r = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || r.height <= 0) return height
        return Math.max(0, Math.min(height, r.y / (Qt.platform.os === "android" ? Screen.devicePixelRatio : 1)))
    }
    /// How much of the window the keyboard covers from below (0: none). The footer makes room for it: the pages, the
    /// Markdown source and the pills end above it (as Android's adjustResize would), the dock goes while it is open,
    /// and on a phone the format bar sits right above it.
    readonly property real keyboardHeight: Math.max(0, height - keyboardTop)
    readonly property bool keyboardOpen: keyboardHeight > 0
    property bool quitting: false

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
    /// What was chosen by hand in this size class ("": the automatic choice): "sidebar", "chrome", later "toolbar"
    function layoutChoice(what) { return (app.settings.revision, app.settings.layoutChoice(adaptive.layoutClass, what)) }
    function chooseLayout(what, value) { app.settings.setLayoutChoice(adaptive.layoutClass, what, value) }
    /// The bottom sheet that the menus (AdaptiveMenu) become in the phone classes, one for the window
    MenuSheet { id: menuSheet }

    // The page sidebar: beside the page when there is room for it (the page keeps ~900 px; not in portrait or on a
    // phone), unless it was hidden or shown by hand in this size class. Without room, its button opens it as a
    // drawer over the page, which closes when a page is picked or the page is tapped.
    readonly property string sidebarChoice: layoutChoice("sidebar")
    readonly property bool sidebarDocked: sidebarChoice === "shown" || (sidebarChoice === "" && adaptive.roomForSidebar)
    property bool sidebarDrawerOpen: false
    readonly property bool sidebarShown: sidebarDocked || sidebarDrawerOpen
    readonly property bool sidebarAsDrawer: !sidebarDocked
    onSidebarDockedChanged: closeDrawerNow()
    readonly property string layoutClass: adaptive.layoutClass
    onLayoutClassChanged: closeDrawerNow()  // (a drawer is for the moment, in the size it was opened in)
    /// The drawer slides in from the left and out again (0: out of sight, 1: in place); beside the page there is no
    /// slide. Only a tap (the arrow, the dimmed page, a page picked, Esc, the back key) slides it; a change of the size
    /// class takes it away at once.
    property real drawerSlide: 0
    NumberAnimation {
        id: drawerSlideAnimation
        target: win
        property: "drawerSlide"
        duration: 180
        easing.type: Easing.OutCubic
    }
    function slideDrawer(open) {
        drawerSlideAnimation.stop()
        drawerSlideAnimation.to = open ? 1 : 0
        drawerSlideAnimation.start()
    }
    function closeDrawerNow() {
        sidebarDrawerOpen = false
        drawerSlideAnimation.stop()
        drawerSlide = 0
    }
    /// The drawer's width: the sidebar's 210 px; on a phone up to 85 % of the window (larger thumbnails), 260 px when
    /// the phone is held sideways (a page's thumbnail stays shorter than the window)
    readonly property real drawerWidth: !adaptive.phone ? 210
                                        : layoutClass === "phoneShort" ? 260 : Math.min(360, Math.round(width * 0.85))
    /// The Pages button: hides the sidebar (remembered for this size class), or shows it again - beside the page where
    /// there is room, else as a drawer (for the moment, not remembered)
    function showSidebar(shown) {
        if (shown) {
            if (sidebarDocked) return
            if (adaptive.roomForSidebar) {
                chooseLayout("sidebar", "")  // (it was hidden by hand: automatic again)
            } else {
                sidebarDrawerOpen = true
                slideDrawer(true)
            }
        } else {
            if (sidebarDrawerOpen) slideDrawer(false)
            sidebarDrawerOpen = false
            if (sidebarDocked) chooseLayout("sidebar", adaptive.roomForSidebar ? "hidden" : "")
        }
    }
    /// The drawer's pin: keep the sidebar beside the page in this size class, although room is short
    function dockSidebar() {
        closeDrawerNow()
        chooseLayout("sidebar", adaptive.roomForSidebar ? "" : "shown")
    }

    /// The bottom of the canvas where controls may go over it (above the navigation bar and the keyboard), and its
    /// sides (clear of a cut-out): the pills over the page keep inside them
    readonly property real canvasControlsBottom: Math.min(canvas.y + canvas.height, controlsBottom)
    readonly property real canvasControlsLeft: Math.max(canvas.x, controlsLeft)
    readonly property real canvasControlsRight: Math.min(canvas.x + canvas.width, controlsRight)
    readonly property real canvasControlsTop: Math.max(canvas.y, controlsTop)
    /// A pill at the canvas's bottom edge (`lowY`: where it would sit) goes above the pills it would meet there
    /// (`others`, from the bottom up): the view pill keeps the lower right corner (audit F6)
    function clearOfPills(item, lowY, others) {
        let y = lowY
        for (let i = 0; i < others.length; ++i) {
            const o = others[i]
            if (o && o.visible && item.x - 8 < o.x + o.width && item.x + item.width + 8 > o.x
                    && y - 8 < o.y + o.height && y + item.height + 8 > o.y)
                y = o.y - item.height - 12
        }
        return y
    }

    // --- the source panels: the Markdown source (and the deprecated text flow) beside or below the page ------------
    // (qt/docs/adaptive-layout.md, "Panels")
    /// The panel open now, or null
    readonly property Item sourcePanel: markdownPanel.visible ? markdownPanel : textFlowPanel.visible ? textFlowPanel : null
    /// Below the page (the page above, its source below, a divider between them) in a portrait tablet or phone, and in
    /// any portrait window too narrow for a panel beside the page; else beside it, at the right (a desktop, narrow or
    /// wide, a phone held sideways). "Adapt the layout" off: beside it, as before.
    readonly property bool sourceAtBottom: {
        const c = adaptive.layoutClass
        if (c === "tabletPortrait" || c === "phonePortrait") return true
        if (c !== "desktopNarrow" && c !== "tiny") return false
        return contentItem.height > width - sideTools.width
    }
    /// Beside the page: 38 % of the window, 360 to 600 px, never more than half of it (a small window keeps its page)
    readonly property real sourceSideWidth: {
        const room = width - sideTools.width - (dockRail ? phoneDock.width : 0)
        return Math.round(Math.min(600, Math.max(Math.min(360, room * 0.5), width * 0.38)))
    }
    /// Below the page: the page's share of the height, dragged at the divider and remembered per size class
    /// (layout/<class>/sourceSplit); half on a tablet, 40 % on a phone (the source gets the bottom 60 %)
    readonly property real sourcePageShare: {
        const v = parseFloat(layoutChoice("sourceSplit"))
        return v >= 0.2 && v <= 0.8 ? v : (adaptive.phone ? 0.4 : 0.5)
    }
    /// While the divider is dragged: the share under the finger (-1: not dragged)
    property real sourceShareLive: -1
    readonly property real sourceBottomHeight: Math.round(contentItem.height
                                                          * (1 - (sourceShareLive >= 0 ? sourceShareLive : sourcePageShare)))

    // The chrome: "full" (tab strip, tool bar, sidebar; in the phone classes the app bar and the tool dock), "compact"
    // (full screen's: the tab dots, the tool square, the pen pill, the view pill) or "reader" (no HUD; the corner mark
    // brings it back). Separate from the window's state (windowFullScreen) and from presenting (black, page by page).
    // Full screen (F11, fullScreenMode) is the compact chrome in a full-screen window; otherwise the chrome is what was
    // chosen for this size class, else the automatic one: the reader in a tiny window, everywhere else "full".
    readonly property string chromeChoice: layoutChoice("chrome")
    /// The automatic chrome of this size class: the reader only in a tiny window (under 360 px either way: split
    /// screen, a pop-up window); elsewhere "Read" is chosen by hand (⋮ → View)
    readonly property string chromeAuto: adaptive.layoutClass === "tiny" ? "reader" : "full"
    /// The chrome of this size class: chosen by hand, else the automatic one
    readonly property string chromeSetting: ["full", "compact", "reader"].indexOf(chromeChoice) >= 0 ? chromeChoice : chromeAuto
    readonly property string chromeMode: chromeSetting === "reader" && !app.presenting ? "reader"
                                         : (fullScreenMode || chromeSetting === "compact") ? "compact" : "full"
    readonly property bool fullChrome: chromeMode === "full"
    /// Nothing over the page but the page: presenting without controls, or the reader chrome
    readonly property bool hudHidden: cleanPage || (chromeMode === "reader" && !app.homeVisible)
    onHudHiddenChanged: if (hudHidden) { quickTools.close(); phoneToolSheet.close() }
    function chooseChrome(mode) { chooseLayout("chrome", mode === chromeAuto ? "" : mode) }

    // --- the phone chrome (qt/docs/adaptive-layout.md, "The phone chrome") -------------------------------------------
    /// A phone class (by the layout class: "Adapt the layout" off keeps the desktop layout at every size)
    readonly property bool phoneLayout: ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptive.layoutClass) >= 0
    /// The full chrome of a phone: the app bar at the top (the library, the title, the tab count, ⋮) instead of the tab
    /// strip, and the tool dock at the bottom instead of the tool bar, the pen pill and the view pill
    readonly property bool phoneChrome: phoneLayout && fullChrome
    /// The app bar: the phone chrome, and the home screen of a phone class in any chrome (the way back to the documents)
    readonly property bool appBarShown: phoneLayout && (fullChrome || app.homeVisible)
    /// The dock is a rail at the right side where the window is held sideways (a phone in landscape, or a tiny window
    /// in landscape); in phone portrait always at the bottom
    readonly property bool dockVertical: adaptive.orientation === "landscape" && adaptive.layoutClass !== "phonePortrait"
    /// (not while the soft keyboard is open: the format bar takes its place above the keyboard)
    readonly property bool dockShown: phoneChrome && !app.homeVisible && !keyboardOpen
    /// The dock beside the page (a rail): what sits at the window's right edge ends at it
    readonly property bool dockRail: dockShown && dockVertical

    // --- the tool bar's place (qt/docs/adaptive-layout.md, "The tool bar") ------------------------------------------
    /// Chosen by hand in this size class (⋮ → View → Tool bar position): "top", "twoRowsTop", "twoRowsBottom",
    /// "railLeft", "railRight"; "": the automatic place
    readonly property string toolbarChoice: {
        const c = layoutChoice("toolbar")
        return ["top", "twoRowsTop", "twoRowsBottom", "railLeft", "railRight"].indexOf(c) >= 0 ? c : ""
    }
    /// The automatic place: two rows at the top on a portrait tablet (a single A4 page keeps the whole width), else
    /// the place of the older setting (top, left, right)
    readonly property string toolbarAuto: adaptive.layoutClass === "tabletPortrait" ? "twoRowsTop"
                                          : app.toolbarPosition === "left" ? "railLeft"
                                          : app.toolbarPosition === "right" ? "railRight" : "top"
    /// A window too narrow for the tools that are never hidden in one row (not a phone): two rows automatically
    readonly property string toolbarAutoLayout: toolbarAuto === "top" && toolArea.autoTwoRows ? "twoRowsTop" : toolbarAuto
    readonly property string toolbarLayout: toolbarChoice !== "" ? toolbarChoice : toolbarAutoLayout
    function chooseToolbar(layout) { chooseLayout("toolbar", layout === toolbarAutoLayout ? "" : layout) }
    /// Its edge: "top", "bottom", "left", "right"
    readonly property string toolbarPosition: toolbarLayout === "railLeft" ? "left" : toolbarLayout === "railRight" ? "right"
                                              : toolbarLayout === "twoRowsBottom" ? "bottom" : "top"
    readonly property bool sideToolbar: toolbarPosition === "left" || toolbarPosition === "right"
    readonly property bool twoToolRows: toolbarLayout === "twoRowsTop" || toolbarLayout === "twoRowsBottom"
    /// A text document's tool bar is merged into its format bar: one row, ⋮ at its end (F7.2)
    readonly property bool toolsInFormatBar: textDoc && formatBar.shown && !sideToolbar && fullChrome && !app.toolbarHidden
                                             && !phoneChrome
    /// The cycling buttons' groups (ToolGroups.qml): the tool bar, the compact chrome's tools and the pen pill
    readonly property ToolGroups toolGroups: ToolGroups {}
    /// Opens a menu from an entry of another one: on a phone once the sheet of that one has gone
    function openAfterMenus(menu) {
        if (!menuSheet.visible) {
            Popups.openAt(menu)
            return
        }
        const then = function() {
            menuSheet.closed.disconnect(then)
            Popups.openAt(menu)
        }
        menuSheet.closed.connect(then)
    }
    /// The text box tool: a Markdown text box (the bar offers no plain one; old plain texts are edited as they are)
    function takeTextBox() {
        app.textMarkdown = true
        app.selectTool("text")
    }
    /// Full screen (F11): the compact chrome - no tab strip, tool bar or page sidebar; a small square shows the current
    /// tool, a tap on it offers the tools (the same ones) and colors. The page / zoom pill stays - in a full-screen
    /// window (windowFullScreen).
    property bool fullScreenMode: false
    onFullScreenModeChanged: {
        if (!fullScreenMode) {
            quickTools.close()
            app.presenting = false  // (presenting is full screen)
        }
        windowFullScreen = fullScreenMode
    }
    /// The window's state: full screen or not (showFullScreen). Full screen (fullScreenMode) sets it; on its own it
    /// changes nothing else.
    property bool windowFullScreen: false
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
        } else if (!windowFullScreen && (visibility === Window.Windowed || visibility === Window.Maximized)) {
            windowedVisibility = visibility
        }
    }
    Timer {  // ends the way back from full screen (a compositor may take a moment and several steps)
        id: leavingFullScreenTimer
        interval: 1500
        onTriggered: {
            win.leavingFullScreen = false
            // settled: the state it is in now is the window's state
            if (!win.windowFullScreen && (win.visibility === Window.Windowed || win.visibility === Window.Maximized))
                win.windowedVisibility = win.visibility
        }
    }
    onWindowFullScreenChanged: {
        if (windowFullScreen) {
            leavingFullScreenTimer.stop()
            leavingFullScreen = false
            app.logWindow("full screen")
            showFullScreen()
        } else {
            leavingFullScreen = true
            remaximized = false
            leavingFullScreenTimer.restart()
            app.logWindow("leave full screen to " + (windowedVisibility === Window.Maximized ? "maximized" : "normal"))
            if (windowedVisibility === Window.Maximized) showMaximized()
            else showNormal()
        }
    }
    /// No tool bar: in the compact or reader chrome, or when it was put away - the small tool square takes over. (The
    /// phone chrome has its dock instead, and nothing to put away.)
    readonly property bool noToolbar: !fullChrome || (app.toolbarHidden && !phoneChrome)
    /// The document is a text file (a .md, a .txt): written with the keyboard, no ink tools (qt/docs/md-editor.md)
    readonly property bool textDoc: app.textDocument !== ""
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
      id: headerColumn
      // The phone classes: the app bar instead of the tab strip (qt/docs/adaptive-layout.md, "The phone chrome")
      PhoneAppBar {
        id: phoneAppBar
        width: parent.width
        topInset: win.safeTop
        leftInset: win.safeLeft
        rightInset: win.safeRight
        visible: win.appBarShown
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
        topInset: win.safeTop
        leftInset: win.safeLeft
        rightInset: win.safeRight
        // (the home screen keeps it: the way back to the documents; the phone classes have the app bar instead)
        visible: (win.fullChrome || app.homeVisible) && !win.phoneLayout
        /// Android and iOS have one window: no tab is dragged out into a window of its own
        undockable: !win.adaptive.mobilePlatform
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
        objectName: "topTools"
        width: parent.width
        visible: !app.homeVisible && win.toolbarPosition === "top" && !win.noToolbar && !win.toolsInFormatBar
                 && !win.phoneChrome
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: win.twoToolRows ? 106 : 56
      }
      // Markdown being written (a .md, Markdown on a page): its formatting tools (qt/docs/md-editor.md). A text
      // document's tool bar is merged into it: ⋮ at its end, its other buttons in "more tools" (F7.2)
      MarkdownFormatBar {
        id: formatBar
        // On a phone, while the soft keyboard is open for the page's Markdown, right above the keyboard (in the
        // footer: the keyboard accessory bar of Obsidian, iA Writer and Google Docs), within reach of the thumbs
        parent: docked ? footerColumn : headerColumn
        readonly property bool docked: shown && win.phoneLayout && win.keyboardOpen && canvas.activeFocus
        width: parent ? parent.width : 0
        leftInset: win.safeLeft
        rightInset: win.safeRight
        readonly property bool shown: !app.homeVisible && !app.presenting && !win.hudHidden && !markdownPanel.visible
                                      && (app.markdownOnPage || (app.textDocument === "markdown" && app.textEditable) || app.textNotes)
        visible: shown
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
    // Two rows at the bottom (the class's choice; closer to the fingertips, like the address bar of a phone's browser)
    footer: Column {
      id: footerColumn
      // Room for the soft keyboard: what is above the footer ends above it (the pages, the source, the pills)
      bottomPadding: win.keyboardHeight
      ToolBar {
        id: bottomTools
        objectName: "bottomTools"
        width: parent.width
        visible: !app.homeVisible && win.toolbarPosition === "bottom" && !win.noToolbar && !win.toolsInFormatBar
                 && !win.phoneChrome
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: visible ? 106 + (win.keyboardOpen ? 0 : win.safeBottom) : 0
        Rectangle { width: parent.width; height: 1; color: "#d5d8dc" }  // (the line towards the pages)
      }
    }
    // The phone's tool dock: at the bottom (in the footer, above the navigation bar), or a rail at the right side when
    // the phone is held sideways (qt/docs/adaptive-layout.md, "The phone chrome")
    PhoneDock {
        id: phoneDock
        vertical: win.dockVertical
        visible: win.dockShown
        parent: vertical ? win.contentItem : footerColumn
        width: vertical ? implicitWidth : (parent ? parent.width : 0)
        height: vertical ? (parent ? parent.height : 0) : (visible ? implicitHeight : 0)
        x: vertical && parent ? parent.width - width : 0
        z: 3
        safeBottom: win.safeBottom
        safeLeft: win.safeLeft
        safeRight: win.safeRight
        onToolsRequested: phoneToolSheet.open()
        onPagesRequested: pageGrid.open()
    }
    PhoneToolSheet { id: phoneToolSheet }
    // The table editor of the formatting bar (the notes' canvas; the editor beside the page has its own)
    MarkdownTableEditor {
        id: tableEditor
        onClosed: if (formatBar.visible) canvas.forceActiveFocus()
    }

    // The tools: a row or two at the top, two rows at the bottom, or a column at the left or right side (the layout
    // for this size class, qt/docs/adaptive-layout.md "The tool bar"). One set of buttons, placed by ToolBarPlan.js.
    Rectangle {
        id: sideTools
        objectName: "sideTools"
        visible: !app.homeVisible && win.sideToolbar && !win.noToolbar && !win.phoneChrome
        width: visible ? 104 + (win.toolbarPosition === "right" ? win.safeRight : win.safeLeft) : 0
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
        objectName: "toolArea"
        parent: win.noToolbar ? quickToolsHolder
                : win.sideToolbar ? sideTools : win.toolbarPosition === "bottom" ? bottomTools : topTools
        anchors.fill: parent
        anchors.topMargin: win.noToolbar ? 0 : 4
        anchors.bottomMargin: win.toolbarPosition === "bottom" ? 4 + (win.keyboardOpen ? 0 : win.safeBottom)
                              : win.noToolbar ? 0
                              : win.sideToolbar ? 4 + Math.max(0, win.contentItem.height - win.controlsBottom)  // (a rail's ⋮ above the navigation bar)
                              : 4
        // (clear of a cut-out or a navigation bar at the side: the bar's own edge, or a rail's)
        anchors.leftMargin: win.noToolbar ? 0 : (win.sideToolbar ? 3 : 6) + (win.toolbarPosition === "right" ? 0 : win.safeLeft)
        anchors.rightMargin: win.noToolbar ? 0 : (win.sideToolbar ? 3 : 6) + (win.toolbarPosition === "left" ? 0 : win.safeRight)
        Material.foreground: "#303030"

        /// The layout of the plan: "row", "twoRows", "rail", "grid" (the compact chrome's tools) or "merged"
        readonly property string planLayout: win.noToolbar ? "grid" : win.toolsInFormatBar ? "merged"
                                             : win.sideToolbar ? "rail" : win.twoToolRows ? "twoRows" : "row"
        /// Where the popups of the buttons open: below a top bar, above a bottom one, beside a rail
        readonly property string popupSide: win.noToolbar ? "left" : win.toolbarPosition
        /// The plan in effect (ToolBarPlan.plan)
        property var plan: null
        property var lastInput: null
        /// The buttons in "more tools", in their order
        property var overflowNames: []
        /// The buttons by their names in the plan, in their order (tools, insert, view, file)
        readonly property var slots: ({
            pen: penTool, eraser: eraserTool, hand: handTool, touchDrawing: touchDrawingTool, select: selectTool,
            text: textTool, write: writeButton, sticky: stickyTool, shape: shapeTool, geometry: geometryTool,
            pdfText: pdfTextTool, emoji: emojiButton, image: imageTool, addPage: addPageTool, search: searchTool,
            fullScreen: fullScreenTool, present: presentTool, settings: settingsTool, new: newTool, open: openTool,
            save: saveTool, editAsNotes: editAsNotesTool, openExternally: openExternallyTool
        })
        readonly property var order: ["pen", "eraser", "hand", "touchDrawing", "select", "text", "write", "sticky",
                                      "shape", "geometry", "pdfText", "emoji", "image", "addPage", "search",
                                      "fullScreen", "present", "settings", "new", "open", "save", "editAsNotes",
                                      "openExternally"]
        /// What the plan depends on: a change lays the bar out again (once, after the bindings settle)
        readonly property var planKey: [planLayout, win.phoneChrome, width, height, win.textDoc, app.toolbarColors.length,
                                        colorStrip.others.length, order.map(function(n) { return slots[n].offered !== false })]
        onPlanKeyChanged: Qt.callLater(relayout)
        Connections {
            target: win.adaptive
            function onHeldChanged() { if (!win.adaptive.held) Qt.callLater(toolArea.relayout) }
        }
        Component.onCompleted: {
            order.forEach(function(n) {
                const item = slots[n]
                item.clicked.connect(function() { toolArea.slotUsed(item) })
            })
            relayout()
        }

        function offeredNames() { return order.filter(function(n) { return slots[n].offered !== false }) }
        /// One row at the top would hide a tool that is never hidden (not on a phone): two rows then, as long as the
        /// place is the automatic one (with 32 px to spare before it goes back to one row)
        property bool autoTwoRows: false
        /// Lays the bar out for the room it has (not while a pointer is held: no change under a stroke)
        function relayout() {
            if (win.adaptive.held || width <= 0) return
            const input = {
                layout: planLayout, width: width, height: height, items: offeredNames(), colors: !win.textDoc,
                widths: !win.textDoc, presets: app.toolbarColors.length, recents: colorStrip.others.length
            }
            // (the automatic top bar: one row, or two when one would hide the important tools)
            const autoTop = win.toolbarChoice === "" && win.toolbarAuto === "top" && !win.adaptive.phone
                            && !win.noToolbar && !win.toolsInFormatBar
            let two = false
            if (autoTop) {
                const room = win.width - 12 - (autoTwoRows ? 32 : 0)
                two = ToolBarPlan.hidesImportant(ToolBarPlan.plan(Object.assign({}, input, { layout: "row", width: room })))
            }
            if (two !== autoTwoRows) {
                autoTwoRows = two  // (the bar changes its layout; the plan follows)
                return
            }
            let p = ToolBarPlan.plan(input)
            // The hysteresis: a bar that grows takes a richer plan only once there are 24 px to spare (the same
            // buttons and layout otherwise: no flicker at an edge)
            const last = lastInput
            if (plan && last && last.layout === input.layout && last.items.join() === input.items.join()
                    && last.presets === input.presets && last.recents === input.recents && last.height === input.height
                    && input.width > last.width && input.width - last.width < 64) {
                const slack = Object.assign({}, input, { width: input.width - 24 })
                const lean = ToolBarPlan.plan(slack)
                if (ToolBarPlan.richness(lean) <= ToolBarPlan.richness(plan)) {
                    p = ToolBarPlan.plan(Object.assign({}, input, { width: last.width }))
                    p = relocate(p, input)
                    input.width = last.width  // (the plan stays the one of that width)
                }
            }
            lastInput = input
            apply(p)
        }
        /// The same plan, with the end at the bar's end again
        function relocate(p, input) {
            if (p.layout === "row" || p.layout === "twoRows") p.end.x += width - input.width
            return p
        }
        function apply(p) {
            plan = p
            const shown = []
            order.forEach(function(n) {
                const item = slots[n]
                const at = p.placed[n]
                if (item.offered === false) {
                    item.parent = toolBank
                } else if (at) {
                    item.parent = barContent
                    item.x = at.x
                    item.y = at.y
                } else if (p.overflow.indexOf(n) >= 0) {
                    shown.push(n)
                } else {
                    item.parent = toolBank
                }
            })
            // "More tools": its buttons in their order, one under the other (two columns when there are many)
            const columns = shown.length > 8 ? 2 : 1
            overflowContent.columns = columns
            shown.forEach(function(n, i) {
                const item = slots[n]
                item.parent = overflowContent
                item.x = (i % columns) * overflowContent.cellWidth
                item.y = Math.floor(i / columns) * 52
            })
            overflowNames = shown
            // The strips (the phone chrome: its dock's cycling buttons)
            const dock = win.phoneChrome
            colorStrip.parent = dock ? phoneDock.colorSlot : p.placed.colors ? barContent : toolBank
            widthStrip.parent = dock ? phoneDock.widthSlot : p.placed.widths ? barContent : toolBank
            colorStrip.mode = dock ? "single" : p.colors
            colorStrip.recentCount = p.recent
            widthStrip.mode = dock ? "single" : p.widths
            colorStrip.columns = p.stripColumns
            widthStrip.columns = p.stripColumns
            colorStrip.cell = p.layout === "rail" || p.layout === "grid" ? p.cell : 40
            widthStrip.cell = colorStrip.cell
            if (dock) { colorStrip.x = 0; colorStrip.y = 0; widthStrip.x = 0; widthStrip.y = 0 }
            if (p.placed.colors && !dock) { colorStrip.x = p.placed.colors.x; colorStrip.y = p.placed.colors.y }
            if (p.placed.widths && !dock) { widthStrip.x = p.placed.widths.x; widthStrip.y = p.placed.widths.y }
            dividerRepeater.model = p.dividers
            barContent.width = p.contentWidth
            barContent.height = p.layout === "grid" ? p.contentHeight : Math.min(p.contentHeight, toolFlick.height)
            barContent.implicitHeight = p.contentHeight
            // The end: ⋮ and "more tools", at the end of the first row, the bottom of a rail, the format bar's end
            // for a text document, one more cell of the grid; the phone chrome: ⋮ at the end of the app bar (its
            // "All tools" holds what does not fit)
            toolEnd.parent = dock ? phoneAppBar.moreSlot : p.layout === "merged" ? formatBar.trailing
                             : p.layout === "grid" ? barContent : toolArea
            toolEnd.x = dock || p.layout === "merged" ? 0 : p.end.x
            toolEnd.y = dock ? 0 : p.layout === "merged" ? -2 : p.layout === "rail" ? toolArea.height - toolEnd.height : p.end.y
            moreToolsButton.offered = p.overflow.length > 0 && p.layout !== "grid" && !dock
        }
        /// A button of "more tools" was used: it closes, unless the button opened a menu of its own
        function slotUsed(item) {
            if (item.parent !== overflowContent) return
            Qt.callLater(function() {
                if (!Popups.hasOpenPopup(item)) moreToolsPopup.close()
            })
        }

        // (the buttons not shown: those not offered for this document)
        Item { id: toolBank; visible: false }

        Flickable {
            id: toolFlick
            anchors.fill: parent
            // (a rail keeps ⋮ at its bottom, outside what could scroll)
            anchors.bottomMargin: toolArea.planLayout === "rail" ? toolEnd.height + 2 : 0
            contentWidth: barContent.width
            contentHeight: barContent.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            // Only the compact chrome's tools scroll (a small window); the bars fit what they show (the rest is in
            // "more tools")
            interactive: toolArea.planLayout === "grid" && contentHeight > height
            clip: true
            Item {
                id: barContent
                objectName: "toolRow"
                Repeater {
                    id: dividerRepeater
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.x
                        y: modelData.y
                        width: modelData.w
                        height: modelData.h
                        color: "#d5d8dc"
                    }
                }
            }
        }

        // ⋮ and "more tools": pinned at the end, never scrolled away
        Row {
            id: toolEnd
            objectName: "toolEnd"
            spacing: 2
            layoutDirection: Qt.RightToLeft  // (⋮ last, at the very end)
            IconButton {
                objectName: "moreButton"
                iconName: "xqt-more"
                label: qsTr("More")
                tip: qsTr("More")
                onClicked: Popups.openAt(moreMenu)
                // The ⋮ menu (qt/docs/adaptive-layout.md, "Menus" and "One place for each action"): what has no button
                // of its own. Every action with a button in the tool bar (or its "more tools"), the view pill or the
                // sidebar is not here too (keyboard shortcuts stay); a sheet with drill-in on phones.
                AdaptiveMenu {
                    id: moreMenu
                    objectName: "moreMenu"
                    AdaptiveMenuItem { objectName: "saveAsItem"; offered: !win.textDoc; text: qsTr("Save as…"); icon.source: app.iconUrl("xopp-document-save"); onTriggered: openSaveDialog(null) }
                    AdaptiveMenuItem { objectName: "shareItem"; text: qsTr("Share…"); icon.source: app.iconUrl("xqt-share"); onTriggered: shareDialog.openFor("") }
                    AdaptiveMenuItem { objectName: "printItem"; text: qsTr("Print… (Ctrl+P)"); icon.source: app.iconUrl("xopp-document-print"); onTriggered: printDialog.open() }
                    AdaptiveMenuItem {
                        objectName: "bookmarkPageItem"
                        readonly property bool marked: (app.bookmarks, app.isBookmarked(app.pageNumber - 1))
                        offered: app.canBookmark
                        text: marked ? qsTr("Remove the bookmark of this page") : qsTr("Bookmark this page")
                        icon.source: app.iconUrl(marked ? "xqt-bookmark-filled" : "xqt-bookmark")
                        icon.color: "transparent"
                        onTriggered: app.toggleBookmark(app.pageNumber - 1)
                    }
                    // A favourite: a star kept beside the file, never in it (qt/docs/bookmarks.md)
                    AdaptiveMenuItem {
                        objectName: "favouriteDocumentItem"
                        offered: app.canFavourite
                        text: app.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
                        icon.source: app.iconUrl(app.favourite ? "xqt-star-filled" : "xqt-star")
                        icon.color: "transparent"
                        onTriggered: app.favourite = !app.favourite
                    }
                    MenuSeparator {}
                    // The document as a file: its name, other ways of editing it, links (Open externally and Edit as
                    // notes are buttons of the tool bar)
                    AdaptiveMenu {
                        objectName: "moreDocumentMenu"
                        title: qsTr("Document")
                        iconName: "xqt-file-text"
                        // Its name (qt/rename): the file, and what belongs to it, as the library renames it
                        AdaptiveMenuItem { objectName: "renameDocumentItem"; text: qsTr("Rename…"); icon.source: app.iconUrl("xqt-pencil"); onTriggered: renameDocumentDialog.openFor(app.currentTab) }
                        AdaptiveMenuItem {
                            objectName: "editAnywayItem"
                            offered: app.canEditAnyway
                            text: qsTr("Edit anyway (as plain text)…")
                            icon.source: app.iconUrl("xqt-file-pen")
                            onTriggered: app.editAnyway(false)
                        }
                        // Text documents as PDF (qt/docs/md-pdf.md): a .md as a new PDF text document
                        AdaptiveMenuItem {
                            objectName: "openAsPdfDocumentItem"
                            offered: app.textDocument === "markdown"
                            text: qsTr("Open as PDF document")
                            icon.source: app.iconUrl("xopp-document-export-pdf")
                            onTriggered: app.openAsPdfDocument()
                        }
                        AdaptiveMenuItem {
                            objectName: "unusedImagesItem"
                            offered: app.textDocument === "markdown"
                            text: qsTr("Remove unused images…")
                            icon.source: app.iconUrl("xqt-image-off")
                            onTriggered: unusedImagesDialog.show()
                        }
                        AdaptiveMenuItem { objectName: "linkedFromItem"; text: qsTr("Linked from…"); icon.source: app.iconUrl("xqt-link"); onTriggered: backlinksDialog.show() }
                        AdaptiveMenuItem { objectName: "copyPageLinkItem"; text: qsTr("Copy link to this page"); icon.source: app.iconUrl("xqt-copy"); onTriggered: app.copyPageLink(-1) }
                    }
                    AdaptiveMenu {
                        objectName: "moreExportMenu"
                        title: qsTr("Export")
                        iconName: "xqt-file-output"
                        // A plain PDF: the notes drawn into the pages (a PDF with notes that stays editable is a type
                        // of Save as)
                        AdaptiveMenuItem { objectName: "exportPdfItem"; text: qsTr("Export as plain PDF…"); icon.source: app.iconUrl("xopp-document-export-pdf"); onTriggered: openExportDialog() }
                        // A PDF/A for keeping: the ink merged into the pages, the Xournal data inside
                        AdaptiveMenuItem {
                            objectName: "exportArchiveItem"
                            offered: !win.textDoc
                            text: qsTr("Export for the archive…")
                            icon.source: app.iconUrl("xqt-archive")
                            onTriggered: archiveDialog.openFor("")
                        }
                        // The Markdown of the document's page texts as a .md (qt/docs/md-pdf.md)
                        AdaptiveMenuItem {
                            objectName: "exportMarkdownItem"
                            offered: !win.textDoc && app.hasMarkdownText
                            text: qsTr("Export as Markdown")
                            icon.source: app.iconUrl("xqt-markdown")
                            onTriggered: win.exportMarkdown()
                        }
                    }
                    // The pages (a text file has none to add; an image and a sticky note are buttons of the tool bar)
                    AdaptiveMenu {
                        objectName: "morePageMenu"
                        title: qsTr("Page")
                        iconName: "xqt-file"
                        offered: !win.textDoc
                        AdaptiveMenuItem { objectName: "insertPagesItem"; text: qsTr("Insert pages…"); icon.source: app.iconUrl("xopp-page-add"); onTriggered: insertPagesDialog.openAt(app.pageNumber) }
                        AdaptiveMenuItem { objectName: "pageBackgroundItem"; text: qsTr("Background of this page…"); icon.source: app.iconUrl("xqt-palette"); onTriggered: backgroundDialog.openFor([app.pageNumber - 1]) }
                        // Another paper size for this page, the selected pages or all of them (PageSizeDialog)
                        AdaptiveMenuItem { objectName: "pageSizeItem"; text: qsTr("Page size…"); icon.source: app.iconUrl("xqt-scaling"); onTriggered: pageSizeDialog.openFor([app.pageNumber - 1]) }
                        // Writing space beside the slides of all pages (qt/docs/note-space.md)
                        AdaptiveMenuItem { objectName: "noteSpaceItem"; text: qsTr("Space for notes…"); icon.source: app.iconUrl("xqt-note-space"); onTriggered: noteSpaceDialog.openFor([app.pageNumber - 1], true) }
                        AdaptiveMenuItem { objectName: "chapterItem"; text: qsTr("Start a chapter here…"); icon.source: app.iconUrl("xqt-toc"); onTriggered: chapterDialog.openFor(app.pageNumber - 1) }
                    }
                    // How the document is shown (all pages, full screen and presenting are buttons of the view pill and
                    // the tool bar; hiding the tool bar is the tab on its edge)
                    AdaptiveMenu {
                        objectName: "moreViewMenu"
                        title: qsTr("View")
                        iconName: "xqt-eye"
                        // (the phone chrome: its tab count)
                        AdaptiveMenuItem { objectName: "allDocumentsItem"; offered: !win.phoneChrome; text: qsTr("All open documents"); icon.source: app.iconUrl("xqt-tabs-grid"); onTriggered: tabOverview.open() }
                        // Phones: the view pill has no room for the page layout button
                        AdaptiveMenuItem {
                            objectName: "pageLayoutItem"
                            offered: !viewPill.layoutShown
                            text: qsTr("Page layout…")
                            icon.source: app.iconUrl("xqt-book-open")
                            onTriggered: win.openAfterMenus(layoutMenu)
                        }
                        AdaptiveMenuItem { objectName: "presentCleanItem"; text: qsTr("Present without controls (Ctrl+F5)"); icon.source: app.iconUrl("xopp-presentation-mode"); onTriggered: win.startPresenting(true) }
                        // The reader chrome of this window size: only the page; the mark in the lower left corner
                        // brings the controls back (qt/docs/adaptive-layout.md)
                        AdaptiveMenuItem { objectName: "readItem"; text: qsTr("Read (only the page)"); icon.source: app.iconUrl("xqt-book-open"); onTriggered: win.chooseChrome("reader") }
                        MenuSeparator {}
                        // Where the tool bar is, in this size class (the automatic place: "Automatic"); the phone
                        // classes have their dock instead
                        AdaptiveMenu {
                            objectName: "toolbarPositionMenu"
                            offered: !win.phoneLayout
                            title: qsTr("Tool bar position")
                            iconName: "xqt-panel-top"
                            component PositionItem: AdaptiveMenuItem {
                                property string position
                                checkable: true
                                checked: win.toolbarLayout === position
                                onTriggered: win.chooseToolbar(position)
                            }
                            PositionItem { objectName: "toolbarTopItem"; text: qsTr("Top"); position: "top" }
                            PositionItem { objectName: "toolbarTwoRowsTopItem"; text: qsTr("Two rows at the top"); position: "twoRowsTop" }
                            PositionItem { objectName: "toolbarTwoRowsBottomItem"; text: qsTr("Two rows at the bottom"); position: "twoRowsBottom" }
                            PositionItem { objectName: "toolbarLeftItem"; text: qsTr("Left"); position: "railLeft" }
                            PositionItem { objectName: "toolbarRightItem"; text: qsTr("Right"); position: "railRight" }
                            MenuSeparator {}
                            AdaptiveMenuItem {
                                objectName: "toolbarAutoItem"
                                text: qsTr("Automatic for this window size")
                                checkable: true
                                checked: win.toolbarChoice === ""
                                onTriggered: win.chooseLayout("toolbar", "")
                            }
                        }
                    }
                }
            }
            // What does not fit into the bar: its buttons, with their names
            IconButton {
                id: moreToolsButton
                objectName: "moreToolsButton"
                property bool offered: false
                visible: offered
                iconName: "xqt-tools-more"
                label: qsTr("More tools")
                tip: qsTr("More tools (what does not fit into the bar)")
                checked: moreToolsPopup.visible
                onClicked: moreToolsPopup.visible ? moreToolsPopup.close() : moreToolsPopup.open()
                Popup {
                    id: moreToolsPopup
                    objectName: "moreToolsPopup"
                    // below the bar (above it at the bottom, beside a rail), at the button's end, inside the window
                    // (margins)
                    x: win.toolbarPosition === "left" ? parent.width + 4
                       : win.toolbarPosition === "right" ? -width - 4 : parent.width - width
                    y: 0
                    onAboutToShow: {
                        const bar = toolArea.mapToItem(moreToolsButton, 0, 0)
                        y = win.toolbarPosition === "bottom" ? bar.y - height - 8
                            : win.sideToolbar ? moreToolsButton.height - height
                            : bar.y + toolArea.height + 8
                    }
                    margins: 8
                    padding: 6
                    modal: false
                    focus: true  // (Esc closes it)
                    background: Rectangle {
                        radius: 12
                        color: "#ffffff"
                        border.width: 1
                        border.color: "#d5d8dc"
                    }
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    // (a tool chosen in it: back to writing)
                    Connections {
                        target: app
                        enabled: moreToolsPopup.opened
                        function onToolChanged() { if (!Popups.hasOpenPopupIn(overflowContent)) moreToolsPopup.close() }
                    }
                    contentItem: Flickable {
                        implicitWidth: overflowContent.width
                        implicitHeight: overflowContent.height
                        contentWidth: overflowContent.width
                        contentHeight: overflowContent.height
                        interactive: contentHeight > height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        Item {
                            id: overflowContent
                            objectName: "overflowContent"
                            property int columns: 1
                            readonly property real cellWidth: Math.min(220, (win.width - 32) / columns)
                            width: columns * cellWidth
                            height: Math.ceil(toolArea.overflowNames.length / columns) * 52
                            // Each button's name beside it; a tap on the name is a tap on the button
                            Repeater {
                                model: toolArea.overflowNames
                                delegate: Label {
                                    required property string modelData
                                    required property int index
                                    readonly property var button: toolArea.slots[modelData]
                                    objectName: "overflowLabel_" + modelData
                                    x: (index % overflowContent.columns) * overflowContent.cellWidth + 56
                                    y: Math.floor(index / overflowContent.columns) * 52
                                    width: overflowContent.cellWidth - 60
                                    height: 48
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    text: button ? button.name : ""
                                    color: button && button.enabled ? "#303030" : "#9e9e9e"
                                    TapHandler { onTapped: if (parent.button && parent.button.enabled) parent.button.clicked() }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- the buttons (placed by relayout(); `offered`: there at all for this document) ---
        ToolCycleButton { id: penTool; objectName: "penButton"; parent: toolBank; group: "pen"; property bool offered: !win.textDoc }
        ToolCycleButton { id: eraserTool; objectName: "eraserButton"; parent: toolBank; group: "eraser"; property bool offered: !win.textDoc }
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
        // A text box, written in Markdown and shown formatted (the only text box the bar offers; old plain texts are
        // still edited as they are). Tapped again, held or right-clicked: the font.
        IconButton {
            id: textTool
            objectName: "textButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-text-box"
            label: qsTr("Text box")
            tip: qsTr("Text box (T): tap to write, tap a text to edit it; tap again or hold: the font")
            checked: app.tool === "text"
            ownHold: true
            onClicked: checked ? fontPopup.open() : win.takeTextBox()
            onPressAndHold: fontPopup.open()
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: fontPopup.open()
            }
            Popup {
                id: fontPopup
                objectName: "fontPopup"
                // (the phone chrome: its button is not shown; in the middle, above the dock)
                parent: win.phoneChrome ? Overlay.overlay : textTool
                x: win.phoneChrome ? Math.round((parent.width - width) / 2)
                   : toolArea.popupSide === "left" ? parent.width : toolArea.popupSide === "right" ? -width : 0
                y: win.phoneChrome ? parent.height - height - phoneDock.height - 8
                   : toolArea.popupSide === "bottom" ? -height : toolArea.popupSide === "top" ? parent.height : 0
                margins: 8
                padding: 12
                ColumnLayout {
                    spacing: 8
                    Label { text: qsTr("Text box: the font"); font.weight: Font.DemiBold }
                    ComboBox {
                        id: familyBox
                        Layout.preferredWidth: 260
                        model: fontPopup.opened ? app.fontFamilies() : []
                        currentIndex: model.indexOf(app.fontFamily)
                        onActivated: app.fontFamily = currentText
                    }
                    RowLayout {
                        Label { text: qsTr("Size"); Layout.fillWidth: true }
                        SpinBox {
                            objectName: "fontSizeBox"
                            from: 4; to: 200
                            value: Math.round(app.textMarkdown ? app.markdownFontSize : app.fontSize)
                            editable: true
                            onValueModified: app.textMarkdown ? app.markdownFontSize = value : app.fontSize = value
                        }
                    }
                }
            }
        }
        // Writing on the page with the keyboard: Markdown, formatted while typing (hold: its source beside the page).
        // DEPRECATED (2026-09-26): the text mode (TextFlowPanel, TextFlow) is no longer offered here; its code stays
        // for now (qt/docs/text-mode.md).
        IconButton {
            id: writeButton
            objectName: "textModeButton"
            parent: toolBank
            property bool offered: !win.textDoc
            property bool markdownMode: true  // (always; kept for the tests and QML that read it)
            iconName: "xqt-page-text"
            label: qsTr("Write on the page")
            tip: qsTr("Write on the page: Markdown, shown formatted (Ctrl+Alt+M). Hold: its source beside the page")
            checked: textFlowPanel.visible || markdownPanel.visible || app.markdownOnPage
            ownHold: true
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
            AdaptiveMenu {
                id: writeMenu
                objectName: "writeModeMenu"
                title: qsTr("Write on the page")
                titleShown: true
                AdaptiveMenuItem {
                    objectName: "markdownItem"
                    text: qsTr("Markdown (shown formatted, on the page)")
                    icon.source: app.iconUrl("xqt-page-text")
                    checkable: true
                    checked: writeButton.markdownMode
                    onTriggered: { writeButton.markdownMode = true; app.writeMarkdownOnPage() }
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
            Connections {
                target: markdownPanel
                function onVisibleChanged() { if (markdownPanel.visible) writeButton.markdownMode = true }
            }
        }
        // A sticky note in the middle of the visible page, selected
        IconButton {
            id: stickyTool
            objectName: "stickyNoteButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xqt-sticky-note"
            label: qsTr("Sticky note")
            tip: qsTr("Sticky note (write on it, cover with it)")
            onClicked: app.insertStickyNote()
        }
        ToolCycleButton { id: shapeTool; objectName: "shapeButton"; parent: toolBank; group: "shape"; property bool offered: !win.textDoc }
        ToolCycleButton { id: geometryTool; objectName: "geometryButton"; parent: toolBank; group: "geometry"; property bool offered: !win.textDoc }
        // Marking PDF text: highlight, underline, strike through, select; tapped again or held: how it marks
        IconButton {
            id: pdfTextTool
            objectName: "pdfTextButton"
            parent: toolBank
            property bool offered: !win.textDoc
            readonly property var icons: ({ "highlight": "xqt-mark-text", "underline": "xqt-underline",
                                            "strikethrough": "xqt-strikethrough", "select": "xopp-select-pdf-text-area" })
            iconName: icons[app.pdfTextMode] || "xqt-mark-text"
            label: qsTr("Mark PDF text")
            tip: qsTr("Mark PDF text (drag over the text; tap again or hold: how it marks)")
            checked: app.tool === "selectPdfTextLinear" || app.tool === "selectPdfTextRect"
            ownHold: true
            onClicked: checked ? Popups.openAt(pdfTextMenu) : app.selectTool("selectPdfTextLinear")
            onPressAndHold: Popups.openAt(pdfTextMenu)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(pdfTextMenu, point.position) }
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
                ownerX: toolArea.popupSide === "left" ? emojiButton.width : toolArea.popupSide === "right" ? -width : 0
                ownerY: toolArea.popupSide === "bottom" ? -height : toolArea.popupSide === "top" ? emojiButton.height : 0
                onPicked: function(emoji) { close(); canvas.insertText(emoji) }
            }
        }
        ColorStrip { id: colorStrip; parent: toolBank; side: toolArea.popupSide }
        WidthStrip { id: widthStrip; parent: toolBank; side: toolArea.popupSide }
        IconButton {
            id: imageTool
            objectName: "imageButton"
            parent: toolBank
            property bool offered: !win.textDoc
            iconName: "xopp-tool-image"
            label: qsTr("Image")
            tip: qsTr("Insert an image")
            onClicked: imageDialog.open()
        }
        IconButton {
            id: addPageTool
            objectName: "addPageButton"
            parent: toolBank
            property bool offered: !win.textDoc  // (a text file: no pages to add)
            iconName: "xopp-page-add"
            label: qsTr("Add a page")
            tip: qsTr("Add a page after the current one (press and hold: background, size, several pages)")
            ownHold: true
            onClicked: app.addPageAfterCurrent()
            onPressAndHold: insertPagesDialog.openAt(app.pageNumber)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: insertPagesDialog.openAt(app.pageNumber)
            }
        }
        IconButton {
            id: searchTool
            objectName: "searchButton"
            parent: toolBank
            iconName: "xqt-search"
            label: qsTr("Search")
            tip: qsTr("Search (Ctrl+F)")
            checked: searchBar.visible
            onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar()
        }
        // Full screen (F11). Not inside full screen itself: the tools there end with "Leave full screen"
        IconButton {
            id: fullScreenTool
            objectName: "fullScreenButton"
            parent: toolBank
            property bool offered: !win.fullScreenMode
            iconName: "xopp-fullscreen"
            label: qsTr("Full screen")
            tip: qsTr("Full screen (F11)")
            onClicked: win.fullScreenMode = true
        }
        // Present: full screen, page by page (from the current page); held or right-clicked: only the page
        IconButton {
            id: presentTool
            objectName: "presentButton"
            parent: toolBank
            property bool offered: !win.fullScreenMode
            iconName: "xopp-presentation-mode"
            label: qsTr("Present")
            tip: qsTr("Present (F5; hold: only the page, Ctrl+F5)")
            ownHold: true
            onClicked: win.startPresenting()
            onPressAndHold: win.startPresenting(true)
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: win.startPresenting(true)
            }
        }
        IconButton {
            id: settingsTool
            objectName: "settingsButton"
            parent: toolBank
            iconName: "xqt-settings"
            label: qsTr("Settings")
            tip: qsTr("Settings (Ctrl+,)")
            onClicked: settingsPage.open()
        }
        IconButton {
            id: newTool
            objectName: "newButton"
            parent: toolBank
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
            tip: qsTr("Open (in a new tab; Ctrl+O)")
            onClicked: openDialog.open()
        }
        IconButton {
            id: saveTool
            objectName: "saveButton"
            parent: toolBank
            iconName: "xopp-document-save"
            label: qsTr("Save")
            tip: qsTr("Save (Ctrl+S)")
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
            onClicked: win.openExternally()
        }
    }

    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // As a drawer it slides in from the left edge (win.drawerSlide); clear of a cut-out at the window's left edge
        // (sidebarLeftFill has the sidebar's color there)
        x: (win.toolbarPosition === "left" && sideTools.width > 0 ? sideTools.width : win.safeLeft)
           - (win.sidebarDocked ? 0 : Math.round((1 - win.drawerSlide) * (width + win.safeLeft)))
        width: win.sidebarDocked ? 210 : win.drawerWidth
        bottomInset: Math.max(0, height - win.controlsBottom)
        visible: win.fullChrome && (win.sidebarShown || (win.sidebarAsDrawer && win.drawerSlide > 0))
        // As a drawer (no room beside the page): over the page, below the home screen; it closes once a page is picked
        z: win.sidebarAsDrawer ? 49 : 0
        onPagePicked: if (win.sidebarAsDrawer) win.showSidebar(false)
        // The drawer's pin, just outside its edge: keep the sidebar beside the page at this window size
        IconButton {
            objectName: "sidebarPin"
            visible: win.sidebarAsDrawer
            x: parent.width + 8
            y: 8
            iconName: "xopp-sidebar-show"
            tip: qsTr("Keep the pages beside the page at this window size")
            onClicked: win.dockSidebar()
            background: Rectangle { radius: 12; color: parent.pressed ? "#e8e8e8" : "#ffffff"; border.color: "#d5d8dc" }
        }
    }
    // The sidebar's color under a cut-out at the window's left edge (the sidebar itself is beside it)
    Rectangle {
        objectName: "sidebarLeftFill"
        visible: sidebar.visible && win.safeLeft > 0 && !(win.toolbarPosition === "left" && sideTools.width > 0)
        x: sidebar.x - width
        width: win.safeLeft
        anchors.top: sidebar.top
        anchors.bottom: sidebar.bottom
        z: sidebar.z
        color: sidebar.color
    }
    // The page sidebar's tab (qt/docs/adaptive-layout.md, "The page sidebar"): an arrow at the left edge of the canvas
    // area opens it (beside the page where there is room, else as the drawer); at the sidebar's edge, "‹" closes it.
    // A finger's size in the touch profile; not in the compact or reader chrome, nor while presenting, nor while the
    // tool bar is put away (unless the sidebar is open: then it closes it).
    AbstractButton {
        id: sidebarArrow
        objectName: "sidebarArrow"
        readonly property bool open: win.sidebarShown && sidebar.visible
        visible: win.fullChrome && !app.homeVisible && !win.hudHidden && !app.presenting
                 && (open || !app.toolbarHidden || win.phoneChrome)
        z: 50  // (over the drawer and its dimmed page)
        width: win.adaptive.touchProfile ? win.adaptive.minTarget : 24
        height: win.adaptive.touchProfile ? 64 : 56
        // (40 % down: clear of the search bar at the top and the pills at the bottom)
        x: open ? sidebar.x + sidebar.width : Math.max(referenceSplit.x, win.controlsLeft)
        y: referenceSplit.y + Math.round(referenceSplit.height * 0.4 - height / 2)
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.name: open ? qsTr("Close the page sidebar") : qsTr("Open the page sidebar")
        ToolTip.visible: hovered
        ToolTip.text: open ? qsTr("Close the pages") : qsTr("Pages, layers, contents, annotations")
        ToolTip.delay: 600
        onClicked: win.showSidebar(!open)
        background: null
        contentItem: Item {
            Rectangle {  // a slim tab against the edge
                x: -radius
                anchors.verticalCenter: parent.verticalCenter
                width: 18 + radius
                height: 48
                radius: 8
                color: sidebarArrow.pressed ? "#e8e8e8" : "#f2ffffff"
                border.width: 1
                border.color: "#c9ccd1"
                Image {
                    x: parent.radius + (18 - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    source: app.iconUrl(sidebarArrow.open ? "xqt-chevron-left" : "xqt-chevron-right")
                    sourceSize.width: 16
                    sourceSize.height: 16
                }
            }
        }
    }
    // Behind the drawer: the page dimmed; a tap there closes the drawer (and does not draw)
    Rectangle {
        objectName: "sidebarScrim"
        visible: sidebar.visible && win.sidebarAsDrawer
        anchors.fill: parent
        z: 48
        color: "#4d000000"
        opacity: win.drawerSlide
        MouseArea {
            anchors.fill: parent
            enabled: win.sidebarDrawerOpen
            onClicked: win.showSidebar(false)
        }
    }
    // Esc and Android's back key close the drawer
    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: win.sidebarDrawerOpen && sidebar.visible
        onActivated: win.showSidebar(false)
    }

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
    // Reference mode: another document beside this one (the canvas area is split)
    ReferenceSplit {
        id: referenceSplit
        bottomInset: y + height - win.controlsBottom
        anchors.top: parent.top
        anchors.bottom: win.sourcePanel && win.sourceAtBottom ? win.sourcePanel.top : parent.bottom
        anchors.right: win.sourcePanel && !win.sourceAtBottom ? win.sourcePanel.left
                       : win.dockRail ? phoneDock.left
                       : (win.toolbarPosition === "right" ? sideTools.left : parent.right)
        anchors.left: sidebar.visible && !win.sidebarAsDrawer ? sidebar.right
                      : (win.toolbarPosition === "left" ? sideTools.right : parent.left)
    }

    // A Markdown file shown read-only for now, an image to write on: what that means (closed for this tab with ×).
    Pane {
        id: shownFileNote
        objectName: "shownFileNote"
        property string closedFor: ""
        visible: app.shownFileNote !== "" && closedFor !== app.title && !pageGrid.visible && !contentsOverview.visible
                 && !win.hudHidden
        // (bottom left: the search bar is at the top, the page and zoom pill at the bottom right; above them where it
        // would meet them in a narrow canvas)
        anchors.left: canvas.left
        anchors.leftMargin: (presentCornerMark.visible ? 56 : 24) + win.canvasControlsLeft - canvas.x  // (presenting, reading: beside the corner mark)
        // (through a property of its own: a binding of y that reads the geometry itself crashes Qt 6.7)
        readonly property real clearY: win.clearOfPills(shownFileNote, win.canvasControlsBottom - 24 - height, [viewPill, navPill])
        y: clearY
        width: Math.min(canvas.width - anchors.leftMargin - 16,
                        Math.max(160, Math.min(canvas.width - viewPill.width - 80, 560)))
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
        // also in full screen; presenting only the page number, for a moment (presentPageIndicator); not in the
        // reader chrome
        visible: !pageGrid.visible && !contentsOverview.visible && !app.presenting && !win.hudHidden && !win.phoneChrome
        /// The compact pill, in a canvas under 520 px wide (a phone, a half beside the reference or the source): undo,
        /// redo, the page number (a tap: all pages), the contents and the zoom; the page layout is in ⋮ → View then
        readonly property bool compact: canvas.width < 520
        /// Narrower than the compact pill (a very small window): no redo (Ctrl+Y) and no separators
        readonly property bool tight: canvas.width < 360
        /// The page layout button: not in a phone's portrait nor in the compact pill (it is in ⋮ → View there)
        readonly property bool layoutShown: ["phonePortrait", "tiny"].indexOf(win.adaptive.layoutClass) < 0 && !compact
                                            && !win.phoneChrome
        // At the canvas's lower right corner, always inside the canvas (8 px from its edges where 28 is too much), and
        // above the reference's pill where the two would meet
        readonly property rect refPill: Qt.rect(referenceSplit.x + referenceSplit.pillRect.x,
                                                referenceSplit.y + referenceSplit.pillRect.y,
                                                referenceSplit.pillRect.width, referenceSplit.pillRect.height)
        x: Math.max(win.canvasControlsLeft + 8, win.canvasControlsRight - width
                    - (win.canvasControlsRight - win.canvasControlsLeft - width >= 56 ? 28 : 8))
        readonly property real lowY: win.canvasControlsBottom - 24 - height
        readonly property bool meetsReference: refPill.width > 0 && x < refPill.x + refPill.width && x + width > refPill.x
                                               && lowY < refPill.y + refPill.height && lowY + height > refPill.y
        y: meetsReference ? refPill.y - height - 12 : lowY
        padding: 2
        leftPadding: 10
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
                visible: !viewPill.tight
                iconName: "xopp-edit-redo"
                tip: qsTr("Redo (Ctrl+Y)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canRedo
                onClicked: app.redo()
            }
            ToolSeparator { visible: !viewPill.tight }
            IconButton {
                objectName: "layoutButton"
                visible: viewPill.layoutShown
                iconName: app.pairedPages ? "xqt-book-open" : "xqt-page-single"
                label: qsTr("Page layout")
                tip: app.pairedPages ? qsTr("Two pages side by side - tap: one page (hold: the page layout)")
                                     : qsTr("One page - tap: two side by side (hold: the page layout)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                ownHold: true
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
                AdaptiveMenu {
                    id: layoutMenu
                    objectName: "layoutMenu"
                    title: qsTr("Page layout")
                    titleShown: true
                    // A text file (.md, .txt): A4 pages, or one continuous page that grows with the text
                    AdaptiveMenuItem {
                        objectName: "textPagesItem"
                        offered: win.textDoc && app.textEditable
                        text: qsTr("Text on pages")
                        checkable: true
                        checked: !app.textContinuous
                        onTriggered: app.textContinuous = false
                    }
                    AdaptiveMenuItem {
                        objectName: "textContinuousItem"
                        offered: win.textDoc && app.textEditable
                        text: qsTr("Text on one continuous page")
                        checkable: true
                        checked: app.textContinuous
                        onTriggered: app.textContinuous = true
                    }
                    MenuSeparator { property bool offered: win.textDoc && app.textEditable; visible: offered; height: offered ? implicitHeight : 0 }
                    AdaptiveMenuItem {
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
                    AdaptiveMenuItem {
                        text: qsTr("Two pages side by side")
                        checkable: true
                        checked: app.pairedPages && app.pairsOffset === 0
                        onTriggered: { app.viewColumns = 2; app.pairsOffset = 0; app.pairedPages = true }
                    }
                    AdaptiveMenuItem {
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
                    AdaptiveMenuItem {
                        objectName: "sidewaysItem"
                        text: qsTr("Scroll sideways")
                        checkable: true
                        checked: app.horizontalScrolling
                        onTriggered: app.horizontalScrolling = !app.horizontalScrolling
                    }
                    AdaptiveMenuItem {
                        objectName: "snapPagesItem"
                        text: qsTr("Stop on whole pages")
                        enabled: app.horizontalScrolling
                        checkable: true
                        checked: app.snapPages
                        onTriggered: app.snapPages = !app.snapPages
                    }
                }
            }
            ToolSeparator { visible: viewPill.layoutShown }
            IconButton {
                objectName: "pageGridButton"
                visible: !viewPill.compact  // (the compact pill: its page number opens them)
                iconName: "xqt-pages-grid"
                label: qsTr("All pages")
                tip: qsTr("All pages (Ctrl+Alt+G)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                onClicked: pageGrid.open()
            }
            // The table of contents with the pages of each chapter (ContentsOverview)
            IconButton {
                objectName: "contentsButton"
                iconName: "xqt-toc"
                label: qsTr("Contents")
                tip: qsTr("Contents with the pages of each chapter (Ctrl+Alt+O)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                checked: contentsOverview.visible
                onClicked: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open()
            }
            // Scrolling sideways: the previous and the next page on either side of the page number
            IconButton {
                objectName: "previousPageButton"
                visible: app.horizontalScrolling && !viewPill.compact  // (compact: a swipe turns the page)
                iconName: "xqt-chevron-left"
                tip: qsTr("Previous page (←, Page Up)")
                implicitWidth: 36; implicitHeight: 40
                icon.width: 20; icon.height: 20
                enabled: app.pageNumber > 1
                onClicked: app.previousPage()
            }
            Label {
                objectName: "pageNumberLabel"
                visible: !viewPill.compact
                text: app.pageNumber + " / " + app.pageCount
                color: "#505050"
                Layout.leftMargin: 2
                Layout.rightMargin: app.horizontalScrolling ? 0 : 4
            }
            // The compact pill: the page number opens all pages (the grid button's place)
            ToolButton {
                objectName: "pageNumberButton"
                visible: viewPill.compact
                text: app.pageNumber + " / " + app.pageCount
                font.pixelSize: 14
                implicitHeight: 40
                leftPadding: 8
                rightPadding: 8
                focusPolicy: Qt.NoFocus
                Material.foreground: "#505050"
                Accessible.name: qsTr("All pages")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("All pages (Ctrl+Alt+G)")
                ToolTip.delay: 600
                onClicked: pageGrid.open()
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
                visible: app.horizontalScrolling && !viewPill.compact
                iconName: "xqt-chevron-right"
                tip: qsTr("Next page (→, Page Down)")
                implicitWidth: 36; implicitHeight: 40
                icon.width: 20; icon.height: 20
                enabled: app.pageNumber < app.pageCount
                onClicked: app.nextPage()
            }
            ToolSeparator { visible: !viewPill.tight }
            // The zoom, small. A tap: the fits (after the double-tap time, so a double tap does not flash the menu);
            // a double tap or a long press: the whole page. Pinch, Ctrl+wheel, Ctrl+plus / minus / 0 and the middle
            // button zoom as before.
            ToolButton {
                id: zoomButton
                objectName: "zoomButton"
                text: app.zoomPercent + " %"
                font.pixelSize: 13
                implicitWidth: Math.max(48, implicitContentWidth + 16)
                implicitHeight: 40
                focusPolicy: Qt.NoFocus
                /// A tap waits for a second one before the menu opens
                property bool menuPending: false
                function fitWhole() {
                    menuTimer.stop()
                    app.fitPage()
                }
                Timer {
                    id: menuTimer
                    interval: Qt.styleHints.mouseDoubleClickInterval
                    onTriggered: Popups.openAt(fitMenu)
                }
                TapHandler {
                    id: zoomTaps
                    objectName: "zoomTaps"
                    acceptedButtons: Qt.LeftButton
                    onTapped: function(point, button) {
                        if (tapCount >= 2) zoomButton.fitWhole()
                        else menuTimer.restart()
                    }
                    onLongPressed: zoomButton.fitWhole()
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { menuTimer.stop(); Popups.openAt(fitMenu, point.position) }
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom: tap for the fits; double tap or hold: the whole page")
                ToolTip.delay: 600
                AdaptiveMenu {
                    id: fitMenu
                    objectName: "fitMenu"
                    title: qsTr("Zoom")
                    /// A fit chosen in the page grid of the phone chrome: back to the page, to see it
                    function done() { if (pageGrid.visible && pageGrid.phoneTools) pageGrid.close() }
                    AdaptiveMenuItem { objectName: "fitWidthItem"; text: qsTr("Fit the width (Ctrl+0)"); icon.source: app.iconUrl("xqt-fit-width"); onTriggered: { app.fitWidth(); fitMenu.done() } }
                    AdaptiveMenuItem {
                        objectName: "realSizeItem"
                        text: qsTr("Real size, 100 % (Ctrl+1)")
                        onTriggered: { app.zoomToRealSize(); fitMenu.done() }
                    }
                    AdaptiveMenuItem { objectName: "fitHeightItem"; text: qsTr("Fit the height"); onTriggered: { app.fitHeight(); fitMenu.done() } }
                    AdaptiveMenuItem {
                        objectName: "fitPageItem"
                        text: app.currentPageDiffers ? qsTr("Fit this page (its size differs)") : qsTr("Fit the whole page (double tap)")
                        onTriggered: { app.fitPage(); fitMenu.done() }
                    }
                }
            }
        }
    }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
        // (its pill above the navigation bar and the keyboard)
        bottomInset: canvas.y + canvas.height - win.canvasControlsBottom
        rightInset: canvas.x + canvas.width - win.canvasControlsRight
        // The phone chrome: the contents and the zoom are here (its page number opens the grid)
        phoneTools: win.phoneChrome
        onContentsRequested: contentsOverview.open()
        onZoomRequested: Popups.openAt(fitMenu)
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
    // Text mode (deprecated): beside the pages (right), or below them in portrait; the canvas makes room
    TextFlowPanel {
        id: textFlowPanel
        atBottom: win.sourceAtBottom
        anchors.bottom: parent.bottom
        anchors.right: win.dockRail ? phoneDock.left : win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: !visible ? 0 : atBottom ? referenceSplit.width : win.sourceSideWidth
        height: atBottom ? win.sourceBottomHeight : parent.height
    }
    // Markdown box, or the Markdown of a page: the same place
    MarkdownPanel {
        id: markdownPanel
        atBottom: win.sourceAtBottom
        // (its text and buttons clear of the navigation bar and a cut-out at the window's edges)
        bottomPadding: win.contentBottomInset
        rightPadding: x + width >= win.contentItem.width - 0.5 ? win.safeRight : 0
        leftPadding: atBottom && x < 0.5 ? win.safeLeft : 0
        anchors.bottom: parent.bottom
        anchors.right: win.dockRail ? phoneDock.left : win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: !visible ? 0 : atBottom ? referenceSplit.width : win.sourceSideWidth
        height: atBottom ? win.sourceBottomHeight : parent.height
    }
    // Between the page and its source below it: dragged, the page's share of the height is remembered for this size
    // class (a grip in the middle, touch sized; the whole edge takes a drag)
    Item {
        id: sourceDivider
        objectName: "sourceDivider"
        visible: win.sourcePanel !== null && win.sourceAtBottom && !app.homeVisible
        x: win.sourcePanel ? win.sourcePanel.x : 0
        width: win.sourcePanel ? win.sourcePanel.width : 0
        height: 24
        y: (win.sourcePanel ? win.sourcePanel.y : 0) - height / 2
        z: 3
        Rectangle {
            objectName: "sourceDividerGrip"
            anchors.centerIn: parent
            width: 72
            height: 16
            radius: 8
            color: sourceDrag.active ? Material.accentColor : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 3
                    Rectangle { width: 2; height: 6; radius: 1; color: sourceDrag.active ? "#ffffff" : "#5f6368" }
                }
            }
        }
        HoverHandler { cursorShape: Qt.SplitVCursor }
        DragHandler {
            id: sourceDrag
            target: null
            xAxis.enabled: false
            property real startTop: 0
            onActiveChanged: {
                if (active) {
                    startTop = win.sourcePanel.y
                    win.sourceShareLive = win.sourcePageShare
                } else {
                    const share = win.sourceShareLive
                    win.sourceShareLive = -1
                    const auto = win.adaptive.phone ? 0.4 : 0.5
                    win.chooseLayout("sourceSplit", Math.abs(share - auto) < 0.01 ? "" : share.toFixed(3))
                }
            }
            onTranslationChanged: {
                if (!active) return
                const h = Math.max(1, sourceDivider.parent.height)
                win.sourceShareLive = Math.max(0.2, Math.min(0.8, (startTop + translation.y) / h))
            }
        }
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
        bottomInset: canvas.y + canvas.height - win.canvasControlsBottom
        rightInset: canvas.x + canvas.width - win.canvasControlsRight
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
        bottomLimit: win.controlsBottom
    }

    // The selected sticky note: its color, cover mode, delete.
    NotePill {
        id: notePill
        objectName: "notePill"
        canvasItem: canvas
        avoid: viewPill
        bottomLimit: win.controlsBottom
        onImageRequested: imageDialog.open()
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
        visible: (app.canGoBack || app.canGoForward) && !pageGrid.visible && !win.hudHidden
        anchors.left: canvas.left
        anchors.leftMargin: (presentCornerMark.visible ? 56 : 20) + win.canvasControlsLeft - canvas.x  // (presenting, reading: beside the corner mark)
        readonly property real clearY: win.clearOfPills(navPill, win.canvasControlsBottom - 24 - height, [viewPill])
        y: clearY
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
        anchors.topMargin: 12 + win.canvasControlsTop - canvas.y
        anchors.horizontalCenter: canvas.horizontalCenter
        width: Math.min(implicitWidth, win.canvasControlsRight - win.canvasControlsLeft - 16)
    }

    // Where the link under the mouse or the hovering pen leads (qt/docs/links.md, "Links with the mouse")
    LinkStatusLine {
        canvasItem: canvas
        visible: !win.hudHidden
    }
    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    CanvasScrollBars {
        canvasItem: canvas
        // (beside the strip that brings a right tool bar back, not under it; clear of the safe area's insets)
        rightInset: Math.max(toolbarShow.visible && toolbarShow.side === "right" ? toolbarShow.width : 0,
                             canvas.x + canvas.width - win.canvasControlsRight)
        leftInset: win.canvasControlsLeft - canvas.x
        topInset: win.canvasControlsTop - canvas.y
        bottomInset: canvas.y + canvas.height - win.canvasControlsBottom
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
    AdaptiveDialog {
        id: shareDialog
        objectName: "shareDialog"
        kind: "question"
        property string file: ""  // a PDF of the library; "": the current document
        /// A Markdown or text file (the current document's, or a card's): shared as the file itself, never as a PDF
        property string textFile: ""
        function openFor(path) {
            file = path
            textFile = app.sharedTextFile(path)
            open()
        }
        preferredWidth: 460
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
    AdaptiveDialog {
        id: shareXoppDialog
        objectName: "shareXoppDialog"
        kind: "question"
        preferredWidth: 500
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
    AdaptiveDialog {
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
        preferredWidth: 520
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
    AdaptiveDialog {
        id: archiveReportDialog
        objectName: "archiveReportDialog"
        kind: "card"
        property string path: ""
        property bool pdfa: false
        property var problems: []
        property var adjusted: []
        preferredWidth: 520
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
    AdaptiveDialog {
        id: oldXoppDialog
        objectName: "oldXoppDialog"
        kind: "question"
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
        preferredWidth: 520
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
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Move %1 to the trash (the PDF now holds everything)").arg(oldXoppDialog.file)
            }
            RadioButton {
                id: updateChoice
                objectName: "oldXoppUpdate"
                ButtonGroup.group: oldXoppChoices
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Keep it updated for Xournal++")
            }
            RadioButton {
                id: keepChoice
                objectName: "oldXoppKeep"
                ButtonGroup.group: oldXoppChoices
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
                text: qsTr("Keep it as it is (not updated)")
            }
            CheckBox {
                id: dontAsk
                objectName: "oldXoppDontAsk"
                Layout.fillWidth: true
                Component.onCompleted: contentItem.wrapMode = Text.Wrap
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
    AdaptiveDialog {
        id: hybridEditedDialog
        objectName: "hybridEditedDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 520
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
    AdaptiveDialog {
        id: externalSaveDialog
        objectName: "externalSaveDialog"
        kind: "question"
        preferredWidth: 480
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
    AdaptiveDialog {
        id: editAnywayDialog
        objectName: "editAnywayDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 520
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
    AdaptiveDialog {
        id: backlinksDialog
        objectName: "backlinksDialog"
        property var items: []
        function show() { items = app.backlinks(); open() }
        preferredWidth: 460
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
    AdaptiveDialog {
        id: linkFoundDialog
        objectName: "linkFoundDialog"
        kind: "question"
        property string file: ""
        property string folder: ""
        preferredWidth: 480
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
    AdaptiveDialog {
        id: linkMissingDialog
        objectName: "linkMissingDialog"
        kind: "question"
        property string file: ""
        preferredWidth: 480
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
    AdaptiveDialog {
        id: textChangedDialog
        objectName: "textChangedDialog"
        kind: "question"
        property string file: ""
        property bool document: false  // a .xopp or PDF (reloading it cannot be undone)
        preferredWidth: 520
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
    AdaptiveDialog {
        id: markdownReplaceDialog
        objectName: "markdownReplaceDialog"
        kind: "question"
        property url file
        title: qsTr("Replace the Markdown file?")
        preferredWidth: 440
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

    AdaptiveDialog {
        id: unsavedDialog
        objectName: "unsavedDialog"
        kind: "question"
        title: qsTr("Unsaved changes")
        preferredWidth: 480
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
    AdaptiveDialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        kind: "question"
        onClosed: homeView.offerLibrariesHomeAtStart()
        closePolicy: Popup.NoAutoClose
        preferredWidth: 560
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

    AdaptiveDialog {
        id: closeAllDialog
        objectName: "closeAllDialog"
        kind: "question"
        preferredWidth: 480
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

    AdaptiveDialog {
        id: messageDialog
        objectName: "messageDialog"
        kind: "card"
        preferredWidth: 640
        standardButtons: Dialog.Ok
        property alias text: messageLabel.text
        Label { id: messageLabel; wrapMode: Text.Wrap; width: parent.width }
    }
    // Full screen (editing): the open documents as dots in a slim bar at the top; a tap shows them all, a swipe along
    // the bar or its small arrows at both ends go to the next or previous one (only on the bar: the pages keep every
    // touch)
    Rectangle {
        id: fullScreenTabs
        objectName: "fullScreenTabs"
        visible: win.chromeMode === "compact" && !app.presenting && !app.homeVisible && app.tabs.count > 1
                 && !searchBar.visible
        z: 59
        // at the top, in the middle of the window (over the notes and a reference beside them alike), below the status bar
        anchors.horizontalCenter: parent.horizontalCenter
        y: win.controlsTop
        // (thin to look at; for fingers (the touch profile) taller, with arrows as wide as a finger)
        height: win.adaptive.touchProfile ? 36 : 26
        width: Math.max(120, (tabDots.visible ? tabDots.implicitWidth : tabCountLabel.implicitWidth) + 36) + 2 * arrowWidth
        readonly property int arrowWidth: win.adaptive.touchProfile ? win.adaptive.minTarget : 26
        radius: height / 2
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
        // The previous / next document: small arrows at the ends of the bar
        component TabArrow: Item {
            id: arrow
            property bool forward
            readonly property bool atEnd: forward ? app.currentTab >= app.tabs.count - 1 : app.currentTab <= 0
            width: fullScreenTabs.arrowWidth
            height: fullScreenTabs.height
            Label {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                text: arrow.forward ? "›" : "‹"
                color: "#ffffff"
                opacity: arrow.atEnd ? 0.35 : (arrowHover.hovered ? 1 : 0.8)
                font.pixelSize: 20
                font.bold: true
            }
            HoverHandler { id: arrowHover }
            TapHandler {
                enabled: !arrow.atEnd
                onTapped: {
                    if (arrow.forward) app.nextTab()
                    else app.previousTab()
                    tabToast.show()
                }
            }
        }
        TabArrow { objectName: "fullScreenTabPrevious"; forward: false; anchors.left: parent.left }
        TabArrow { objectName: "fullScreenTabNext"; forward: true; anchors.right: parent.right }
        // Between the arrows, a tap: the overview of all of them
        Item {
            anchors.fill: parent
            anchors.leftMargin: fullScreenTabs.arrowWidth
            anchors.rightMargin: fullScreenTabs.arrowWidth
            TapHandler { onTapped: tabOverview.open() }
        }
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
        ToolTip.text: qsTr("Open documents: tap for all of them; the arrows or a swipe for the next or previous one")
        ToolTip.delay: 800
        HoverHandler { id: tabHover }
    }
    // The document swiped to: its title, for a moment
    Rectangle {
        id: tabToast
        objectName: "fullScreenTabToast"
        z: 59
        anchors.horizontalCenter: parent.horizontalCenter
        y: win.controlsTop + fullScreenTabs.height + 8
        visible: opacity > 0 && win.chromeMode === "compact"
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
        anchors.bottomMargin: 18 + canvas.y + canvas.height - win.canvasControlsBottom
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
    // Presenting: a mark in the lower left corner; a tap (click, pen, finger) hides the controls - the pen pill, the
    // tool square - or shows them again, as Ctrl+F5 does (qt/present-clean). While the controls show it is clearly there
    // (a dot of the accent color in a ring; it pulses once when presenting starts), while they are hidden it is faint,
    // barely there on a projector. Its name ("Hide the tools" / "Show the tools") on hover and while a finger is held
    // on it (letting go then does not tap it). The target around it is a finger wide.
    // In the reader chrome (no HUD) it is the way back to the chrome, the same field.
    AbstractButton {
        id: presentCornerMark
        objectName: "presentCornerMark"
        readonly property bool reading: !app.presenting && win.chromeMode === "reader" && !app.homeVisible
        visible: app.presenting || reading
        z: 91
        anchors.left: canvas.left
        anchors.bottom: canvas.bottom
        anchors.leftMargin: win.canvasControlsLeft - canvas.x
        anchors.bottomMargin: canvas.y + canvas.height - win.canvasControlsBottom
        width: 48
        height: 48
        focusPolicy: Qt.NoFocus  // (the keys stay with the page)
        hoverEnabled: true
        /// The controls are shown: the field is clearly visible
        readonly property bool highlighted: app.presenting && !win.presentClean
        readonly property bool lit: hovered || markHover.hovered || pressed
        /// Its name (hover, a finger held on it)
        readonly property string labelText: highlighted ? qsTr("Hide the tools") : qsTr("Show the tools")
        property bool heldLabel: false
        property bool heldPointer: false
        Accessible.name: labelText
        ToolTip.visible: heldLabel || ((hovered || markHover.hovered) && !pressed)
        ToolTip.text: labelText
        ToolTip.delay: heldLabel ? 0 : 600
        /// 0 → 1 → 0 once: when presenting (or reading) starts, so the field is found
        property real pulse: 0
        SequentialAnimation {
            id: pulseAnimation
            NumberAnimation { target: presentCornerMark; property: "pulse"; from: 0; to: 1; duration: 450; easing.type: Easing.OutQuad }
            NumberAnimation { target: presentCornerMark; property: "pulse"; to: 0; duration: 650; easing.type: Easing.InQuad }
        }
        function pulseOnce() { pulseAnimation.restart() }
        onReadingChanged: if (reading) pulseOnce()
        Connections {
            target: app
            function onPresentingChanged() { if (app.presenting) presentCornerMark.pulseOnce() }
        }
        background: null
        contentItem: Item {
            Rectangle {  // the ring (the controls are shown), wider while it pulses
                objectName: "presentCornerRing"
                visible: presentCornerMark.highlighted || presentCornerMark.pulse > 0
                readonly property real size: 22 + presentCornerMark.pulse * 18
                x: 13 - size / 2
                y: parent.height - 13 - size / 2
                width: size
                height: size
                radius: size / 2
                color: "transparent"
                border.width: 2
                border.color: Material.accentColor
                opacity: (presentCornerMark.highlighted ? 0.55 : 0) + presentCornerMark.pulse * 0.45
            }
            Rectangle {
                objectName: "presentCornerDot"
                readonly property real size: presentCornerMark.highlighted ? 12 : 6
                x: 13 - size / 2
                y: parent.height - 13 - size / 2
                width: size
                height: size
                radius: size / 2
                // shown: the accent color; hidden: grey, as faint on a white slide as on the black around it
                color: presentCornerMark.highlighted ? Material.accentColor : "#9e9e9e"
                border.width: 1
                border.color: "#80ffffff"
                opacity: presentCornerMark.highlighted ? 0.95
                         : Math.max(presentCornerMark.pulse, presentCornerMark.lit ? 0.6 : 0.14)
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }
        HoverHandler { id: markHover; acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus }
        // A finger held on it shows its name; a mouse or a pen held long still taps it
        PointHandler { id: markFinger; acceptedDevices: PointerDevice.TouchScreen }
        onPressAndHold: {
            if (markFinger.active) heldLabel = true
            else heldPointer = true
        }
        onReleased: {
            if (heldLabel) {
                heldLabel = false
            } else if (heldPointer) {
                heldPointer = false
                switchTools()
            }
        }
        onCanceled: { heldLabel = false; heldPointer = false }
        onClicked: switchTools()
        function switchTools() {
            if (reading) win.chooseChrome("full")
            else win.presentClean = !win.presentClean
        }
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
        // (above the pills, the navigation bar and the keyboard)
        anchors.bottomMargin: 96 + canvas.y + canvas.height - win.canvasControlsBottom
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
    // The home screen's color under a cut-out or the navigation bar at a side (the home screen itself is beside it)
    Rectangle {
        anchors.fill: parent
        z: 50
        visible: homeView.visible && (win.safeLeft > 0 || win.safeRight > 0)
        color: homeView.color
    }
    HomeView {
        id: homeView
        anchors.fill: parent
        anchors.leftMargin: win.safeLeft
        anchors.rightMargin: win.safeRight
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

    // Putting the tool bar away and getting it back: a small tab in the middle of its edge towards the pages (⋮ keeps the
    // end of the bar), and a slim strip while it is away. A finger gets a target of minTarget around the tab.
    Item {
        objectName: "toolbarToggle"
        visible: !app.homeVisible && win.fullChrome && !app.toolbarHidden && !win.toolsInFormatBar && !win.phoneChrome
        z: 58
        readonly property string edge: win.toolbarPosition
        readonly property bool side: edge === "left" || edge === "right"
        /// The target: a finger's size in the touch profile, reaching into the pages (not over the bar's buttons)
        readonly property real grip: win.adaptive.touchProfile ? win.adaptive.minTarget : 18
        width: side ? grip : 42
        height: side ? 42 : grip
        // Half over the bar's edge, the rest into the pages
        x: edge === "left" ? sideTools.width - 9 : edge === "right" ? parent.width - sideTools.width - width + 9
           : Math.round((parent.width - width) / 2)
        y: side ? Math.round(win.controlsBottom * 0.75) : edge === "bottom" ? parent.height - height + 9 : -9  // (a rail: clear of the sidebar's arrow)
        Rectangle {
            x: parent.edge === "right" ? parent.width - width : 0
            y: parent.edge === "bottom" ? parent.height - height : 0
            width: parent.side ? 18 : 42
            height: parent.side ? 42 : 18
            radius: 6
            color: "#ffffff"
            border.width: 1
            border.color: "#d5d8dc"
            Image {  // towards the bar it puts away: up, down, left or right
                anchors.centerIn: parent
                source: app.iconUrl("xqt-chevron-up")
                sourceSize.width: 15
                sourceSize.height: 15
                rotation: win.toolbarPosition === "left" ? -90 : win.toolbarPosition === "right" ? 90
                          : win.toolbarPosition === "bottom" ? 180 : 0
            }
        }
        TapHandler { onTapped: app.toolbarHidden = true }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: qsTr("Hide the tool bar")
        ToolTip.delay: 600
        HoverHandler { id: hoverHandler }
    }
    // While it is away: a slim strip at the edge where it was (top, bottom, left or right) brings it back
    Rectangle {
        id: toolbarShow
        objectName: "toolbarShow"
        visible: !app.homeVisible && win.fullChrome && app.toolbarHidden && !win.phoneChrome
        readonly property string side: win.toolbarPosition
        z: 60  // over the edge of the pen pill, which sits at the right edge by default
        width: side === "top" || side === "bottom" ? 96 : 16
        height: side === "top" || side === "bottom" ? 16 : 96
        x: side === "left" ? win.controlsLeft : side === "right" ? win.controlsRight - width : Math.round((parent.width - width) / 2)
        y: side === "top" ? win.controlsTop : side === "bottom" ? win.controlsBottom - height
                                                                : Math.round((win.controlsBottom - height) / 2)
        radius: 8
        color: "#f1f3f4"
        border.width: 1
        border.color: "#d5d8dc"
        opacity: showHover.hovered ? 1 : 0.75
        Image {  // where the bar comes in from: down from the top, up from the bottom, into the pages from a side
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-down")
            sourceSize.width: 15
            sourceSize.height: 15
            rotation: toolbarShow.side === "left" ? -90 : toolbarShow.side === "right" ? 90 : toolbarShow.side === "bottom" ? 180 : 0
        }
        TapHandler { onTapped: app.toolbarHidden = false }
        HoverHandler { id: showHover }
        ToolTip.visible: showHover.hovered
        ToolTip.text: qsTr("Show the tool bar")
        ToolTip.delay: 600
    }

    // Without a tool bar and with an ink tool: colors, width and pen / highlighter at a side of the screen
    PenPill { avoid: viewPill }
    // The setsquare / compass: what it can do, and putting it aside for a moment
    GeometryPill {
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.topMargin: 12 + win.canvasControlsTop - canvas.y
        anchors.rightMargin: 20 + canvas.x + canvas.width - win.canvasControlsRight
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
        // Only in the compact chrome (full screen): with the bar merely put away, the arrow strip brings it back at
        // once. Not while presenting without controls, nor in the reader chrome.
        visible: win.chromeMode === "compact" && !app.homeVisible && !win.hudHidden
        z: 60
        x: win.canvasControlsLeft + 16  // (over the main document, also when a reference is beside it)
        y: win.canvasControlsTop + 16
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
            "selectPdfTextRect": "xopp-select-pdf-text-area", "laserPointerPen": "xopp-laser-pointer",
            "laserPointerHighlighter": "xopp-laser-pointer"
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
            xAxis.minimum: win.controlsLeft; xAxis.maximum: win.controlsRight - quickToolSquare.width
            yAxis.minimum: win.controlsTop; yAxis.maximum: win.controlsBottom - quickToolSquare.height
        }
        TapHandler { onTapped: quickTools.visible ? quickTools.close() : quickTools.open() }
    }
    Popup {
        id: quickTools
        objectName: "quickTools"
        parent: Overlay.overlay
        // next to the square, inside the window
        // (in the window's coordinates: the overlay's; clear of the safe area and the keyboard)
        readonly property real roomTop: win.contentItem.y + win.controlsTop + 8
        readonly property real roomBottom: win.contentItem.y + win.controlsBottom - 8
        x: Math.max(win.controlsLeft + 8, Math.min(quickToolSquare.x + quickToolSquare.width + 8, win.controlsRight - width - 8))
        y: Math.max(roomTop, Math.min(win.contentItem.y + quickToolSquare.y, roomBottom - height))
        padding: 8
        width: 6 * 50 + 30
        height: Math.min(roomBottom - roomTop, quickToolsColumn.implicitHeight + 16)
        Column {
            id: quickToolsColumn
            width: parent.width
            spacing: 4
            // The tools take what is left above the two buttons, which stay inside the window (the tools scroll: F6.4)
            Item {
                id: quickToolsHolder
                width: parent.width
                height: Math.max(0, Math.min(barContent.implicitHeight,
                                             quickTools.roomBottom - quickTools.roomTop - quickTools.topPadding - quickTools.bottomPadding
                                             - laserToggle.height - presentToggle.height - leaveFullScreen.height
                                             - 3 * quickToolsColumn.spacing))
            }
            // The laser pointer at once (presenting: point at what is said; its ink fades): again, back to the pen
            Button {
                id: laserToggle
                objectName: "laserPointerButton"
                width: parent.width
                flat: true
                readonly property bool on: win.toolGroups.isLaser(app.tool)
                highlighted: on
                icon.source: app.iconUrl("xopp-laser-pointer")
                text: on ? qsTr("Back to the pen") : qsTr("Laser pointer")
                onClicked: {
                    quickTools.close()
                    if (win.toolGroups.isLaser(app.tool)) win.toolGroups.activate("pen", "pen")
                    else win.toolGroups.activate("pen", "laserPointerPen")
                }
            }
            Button {
                id: presentToggle
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
            // Full screen: back to the window; the compact chrome chosen in a window: back to the full chrome
            Button {
                id: leaveFullScreen
                objectName: "leaveFullScreenButton"
                width: parent.width
                flat: true
                text: !win.fullScreenMode ? qsTr("Show the tabs and the tool bar")
                                          : qsTr("Leave full screen") + (app.presenting ? "" : qsTr(" (Esc)"))
                onClicked: {
                    if (win.fullScreenMode) {
                        win.fullScreenMode = false
                    } else {
                        quickTools.close()
                        win.chooseChrome("full")
                    }
                }
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
    RenameDialog { id: renameDocumentDialog }
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
    Shortcut { sequences: win.keysOf("toolText"); enabled: toolKeys; onActivated: win.takeTextBox() }  // (a Markdown text box)
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
    Shortcut { sequence: "Escape"; enabled: docKeys && (app.hasSelection || app.noteSelected) && !win.sidebarDrawerOpen; onActivated: app.clearSelection() }
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
