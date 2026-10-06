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
    readonly property real controlsRight: contentItem.width - safeRight - presenterPanelWidth
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
    /// What was chosen by hand in this size class ("": the automatic choice): "sidebar", "chrome", "toolbox"
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
    /// The sidebar's History panel (version history)
    function showHistory() {
        sidebar.mode = "history"
        showSidebar(true)
    }
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

    // The chrome: "full" (tab strip, command bar, the docked toolbox, sidebar; in the phone classes the app bar and the
    // tool dock) or "compact" (full screen's: the tab dots, the floating toolbox, the view pill). Full screen (F11,
    // fullScreenMode) is the compact chrome in a full-screen window. Apart from it: the window's state
    // (windowFullScreen), presenting (black, page by page), Zen (nothing around the page) and read only
    // (qt/docs/zen.md). (The reader chrome and the chrome chosen per size class are gone since 0.8.0: Zen.)
    readonly property string chromeMode: fullScreenMode ? "compact" : "full"
    /// The full chrome is shown (not in Zen)
    readonly property bool fullChrome: chromeMode === "full" && !zenShown
    /// Nothing over the page but the page: Zen (presenting without controls too), or the replay
    readonly property bool hudHidden: zenShown || (replaying && !app.homeVisible)

    // --- Zen and read only (qt/docs/zen.md) ---------------------------------------------------------------------------
    /// Zen turned on by hand (⋮ → View → Zen, its keys, the command bar's button, Read)
    property bool zenByHand: false
    /// Zen of itself: a tiny window (under 360 px either way: split screen, a pop-up window), unless it was left there
    /// by hand (remembered for the class: layout/tiny/zen "off"; from 0.7.0, the reader chrome left there: chrome "full")
    readonly property bool zenAuto: {
        if (adaptive.layoutClass !== "tiny") return false
        const z = layoutChoice("zen")
        return z !== "off" && !(z === "" && layoutChoice("chrome") === "full")
    }
    /// Zen: everything around the page hidden (the toolbox, the command bar, the tab strip, the sidebar's arrow, the
    /// pills); its only mark is the dot in the lower left corner (zenDot), whose pill brings the controls back. The
    /// pen keeps writing. Presenting has its own: presenting without controls (presentClean).
    readonly property bool zen: app.presenting ? presentClean : (zenByHand || zenAuto)
    /// Zen as it shows: not on the home screen
    readonly property bool zenShown: zen && !app.homeVisible
    function setZen(on) {
        if (!on) readStarted = false
        if (app.presenting) {
            presentClean = on
            return
        }
        if (adaptive.layoutClass === "tiny") {
            // (left by hand: remembered for tiny windows; on again: automatic again)
            chooseLayout("chrome", "")  // (the reader chrome's choice of 0.7.0: replaced)
            chooseLayout("zen", on ? "" : "off")
            if (!on) zenByHand = false
            return
        }
        zenByHand = on
    }
    /// Read only (⋮ → View → Read only, the dot's pill, the floating toolbox's ⋯): the page cannot be written on, the
    /// pen and the fingers scroll, PDF text can still be selected and copied; the edges turn the pages. Anywhere, with
    /// or without Zen and full screen; off again when the home screen is shown.
    property bool readOnly: false
    /// Where read only can be on: a document with pages
    readonly property bool readOnlyOffered: !app.homeVisible && !textDoc && !replaying
    onReadOnlyOfferedChanged: if (!readOnlyOffered) readOnly = false
    readonly property bool readOnlyOn: readOnly && readOnlyOffered
    /// Read (Ctrl+Alt+R, ⋮ → View → Read): Zen and read only, in full screen (a tiny window stays a window). The keys
    /// again end it: read only off, and Zen and full screen where Read turned them on; Esc leaves Zen too. "Show
    /// controls" leaves only Zen.
    property bool readStarted: false
    property bool readEnteredFullScreen: false
    property bool readEnteredZen: false
    function startReading() {
        if (!readOnlyOffered) return
        readEnteredFullScreen = !fullScreenMode && adaptive.layoutClass !== "tiny"
        readEnteredZen = !zenShown
        if (readEnteredFullScreen) fullScreenMode = true
        setZen(true)
        readOnly = true
        readStarted = true
    }
    function stopReading() {
        readOnly = false
        if (readEnteredZen) setZen(false)
        if (readEnteredFullScreen && !app.presenting) fullScreenMode = false
        readStarted = readEnteredFullScreen = readEnteredZen = false
    }
    function toggleReading() { readStarted && readOnlyOn ? stopReading() : startReading() }
    /// Reading: read only is on (DocumentCanvas.readingOnly; the edges turn the pages, readingTapFields)
    readonly property bool reading: readOnlyOn
    /// The document's timeline is replayed (qt/docs/timeline.md): the page as of a moment and the play bar at the
    /// bottom, read-only; no tools (as reading), the play bar's keys. The command bar and the phone's dock are put away
    /// meanwhile (qt/replay-polish)
    readonly property bool replaying: app.timeline.active

    // --- the phone chrome (qt/docs/adaptive-layout.md, "The phone chrome") -------------------------------------------
    /// A phone class (by the layout class: "Adapt the layout" off keeps the desktop layout at every size)
    readonly property bool phoneLayout: ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptive.layoutClass) >= 0
    /// The full chrome of a phone: the app bar at the top (the library, the title, the tab count, ⋮) instead of the tab
    /// strip, and the tool dock at the bottom instead of the command bar and the view pill
    readonly property bool phoneChrome: phoneLayout && fullChrome
    /// The app bar: the phone chrome, and the home screen of a phone class in any chrome (the way back to the documents)
    readonly property bool appBarShown: phoneLayout && (fullChrome || app.homeVisible)
    /// The dock is a rail at the right side where the window is held sideways (a phone in landscape, or a tiny window
    /// in landscape); in phone portrait always at the bottom
    readonly property bool dockVertical: adaptive.orientation === "landscape" && adaptive.layoutClass !== "phonePortrait"
    /// (not while the soft keyboard is open: the format bar takes its place above the keyboard)
    readonly property bool dockShown: phoneChrome && !app.homeVisible && !keyboardOpen && !replaying
    /// The dock beside the page (a rail): what sits at the window's right edge ends at it
    readonly property bool dockRail: dockShown && dockVertical

    // --- the command bar (qt/docs/toolbox.md, "The command bar") -----------------------------------------------------
    // One row at the top: the commands (the tools are in the toolbox). The classic tool bar of before, with its places
    // (two rows, a rail at a side, the bottom), was removed in 0.8.0.
    /// A text document's command bar is merged into its format bar: one row, ⋮ at its end (F7.2)
    readonly property bool toolsInFormatBar: textDoc && formatBar.shown && fullChrome && !app.toolbarHidden && !phoneChrome
    /// The cycling buttons' groups (ToolGroups.qml): the fixed tools of the toolbox (select, snip, setsquare, text)
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
    /// Full screen (F11): the compact chrome - no tab strip, command bar or page sidebar; the toolbox floats over the
    /// page. The page / zoom pill stays - in a full-screen window (windowFullScreen).
    property bool fullScreenMode: false
    onFullScreenModeChanged: {
        if (!fullScreenMode) app.presenting = false  // (presenting is full screen)
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
    /// No command bar: in the compact chrome or Zen, or when it was put away. (The phone chrome has its dock
    /// instead, and nothing to put away.)
    readonly property bool noToolbar: !fullChrome || (app.toolbarHidden && !phoneChrome)
    /// The document is a text file (a .md, a .txt): written with the keyboard, no ink tools (qt/docs/md-editor.md)
    readonly property bool textDoc: app.textDocument !== ""
    /// Undo and redo: the toolbox's head while it is shown; else they lead the command bar while it is shown (a text
    /// document), its format bar when the bar is merged into it, the view pill while no bar is shown (the compact
    /// chrome, Zen, the bar put away), the dock in the phone chrome: one place at a time
    /// (qt/docs/adaptive-layout.md, "One place for each action")
    readonly property bool undoInToolBar: !noToolbar && !toolsInFormatBar && !phoneChrome && !toolboxShown
    /// A text document: undo and redo lead its format bar (qt/docs/toolbox.md, "Text documents")
    readonly property bool undoInFormatBar: toolsInFormatBar

    // --- the toolbox (qt/docs/toolbox.md) ----------------------------------------------------------------------------
    /// Its edge chosen by hand in this size class (⋮ → View → Toolbox position): "left", "right", "top", "bottom"
    readonly property string toolboxChoice: {
        const c = layoutChoice("toolbox")
        return ["left", "right", "top", "bottom"].indexOf(c) >= 0 ? c : ""
    }
    /// The automatic edge: the right (the author's choice: beside the page, out of the way of the writing hand's
    /// wrist for most and of the page sidebar at the left), a phone upright at the bottom
    readonly property string toolboxAuto: adaptive.layoutClass === "phonePortrait" ? "bottom" : "right"
    readonly property string toolboxEdge: toolboxChoice !== "" ? toolboxChoice : toolboxAuto
    readonly property bool toolboxVertical: toolboxEdge === "left" || toolboxEdge === "right"
    function chooseToolboxEdge(edge) { chooseLayout("toolbox", edge === toolboxAuto ? "" : edge) }
    /// Docked beside the page, taking its strip: the toolbox in the full chrome of a desktop or a tablet (a text
    /// document has no ink tools: its format bar holds undo and redo)
    readonly property bool toolboxDocked: fullChrome && !phoneLayout && !app.homeVisible && !textDoc && !replaying
    /// Floating over the page, a little off its edge: the compact chrome (full screen, presenting with the tools), on
    /// a phone too
    readonly property bool toolboxFloating: chromeMode === "compact" && !app.homeVisible && !textDoc && !hudHidden
    /// In the phone's dock (at the bottom, or the rail at the right held sideways): its first tools, "My tools"
    readonly property bool toolboxInDock: dockShown && !textDoc && !replaying
    /// The toolbox is shown (docked, or floating in full screen and on phones)
    readonly property bool toolboxShown: toolboxDocked || toolboxFloating || toolboxInDock
    /// The strip it takes at the top or the bottom (0: none, or at a side)
    readonly property real toolboxTop: toolboxDocked && toolboxEdge === "top" ? toolboxRow.height : 0
    readonly property real toolboxBottom: toolboxDocked && toolboxEdge === "bottom" ? toolboxRow.height : 0
    /// The side the docked toolbox takes ("left", "right"; "": none, or at the top or the bottom)
    readonly property string sideEdge: toolboxDocked && toolboxVertical ? toolboxEdge : ""
    /// A button's tip with the keys of its action as they are set ("Redo (Ctrl+Shift+Z, Ctrl+Y)")
    function withKeys(text, id) {
        const keys = keysOf(id)
        return keys.length > 0 ? text + " (" + keys.join(", ") + ")" : text
    }
    Connections {
        target: app
        function onHomeVisibleChanged() {
            if (!app.homeVisible) return
            win.fullScreenMode = false
            win.zenByHand = false
            win.readStarted = false
        }
        function onPresentingChanged() { if (!app.presenting) win.presentClean = false }
    }

    /// Present from the current page: full screen, a page fills it. `clean`: without controls (below). With two
    /// screens (qt/docs/presenter-view.md) this window becomes the presenter's console on one of them (off the
    /// audience's screen first) and the audience's window shows the slide on the other.
    function startPresenting(clean) {
        if (app.homeVisible) return
        presentClean = clean === true
        if (app.presenter.available) app.presenter.placeConsole(win)
        fullScreenMode = true
        app.presenting = true
    }
    // --- the presenter view on a second screen (qt/docs/presenter-view.md) ----------------------------------------
    /// Presenting with two screens: this window is the console (the page with its space for notes, the panel with
    /// the clock, the time, the next page), the audience's window shows the slide
    readonly property bool presenterConsole: app.presenting && app.presenter.active
    /// The console's panel at the right (the controls over the page stay left of it)
    readonly property real presenterPanelWidth: presenterConsole ? Math.round(Math.min(480, Math.max(280, width * 0.3))) : 0
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
    /// Presenting without controls (Ctrl+F5, or holding the presentation button): presenting in Zen - only the page
    /// shows, no floating toolbox, no other overlay; the Zen dot in the lower left corner (its "Show controls") or
    /// Ctrl+F5 brings them back, Ctrl+F5 hides them again. Every presentation starts as it is asked for: F5 with the
    /// controls (and no dot).
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
    function sharePdfOf(file, toClipboard, withHistory) {
        if (file !== "") {
            app.shareFile(file, toClipboard, !!withHistory)
            return
        }
        const step = app.shareStep()
        if (step === "share" || step === "save") {
            app.sharePdf(toClipboard, !!withHistory)
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
        toolsShown: win.phoneChrome && win.topBarShown
        toolsHeight: topBarPane.thickness
        pageShown: win.toolboxInDock && win.dockVertical
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
        visible: !app.homeVisible && !win.noToolbar && !win.toolsInFormatBar && !win.phoneChrome && !win.replaying
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
        readonly property bool docked: shown && win.phoneLayout && win.keyboardOpen && canvas.activeFocus
        width: parent ? parent.width : 0
        leftInset: win.safeLeft
        rightInset: win.safeRight
        readonly property bool shown: !app.homeVisible && !app.presenting && !win.hudHidden && !markdownPanel.visible
                                      && (app.markdownOnPage || (app.textDocument === "markdown" && app.textEditable) || app.textNotes)
        visible: shown
        // (a text document: its commands at the end of the row, the top bar; nothing folds, the row scrolls)
        holdsCommands: win.toolsInFormatBar
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
      bottomPadding: win.keyboardHeight
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
        onPagesRequested: pageGrid.open()
        hostsToolbox: win.toolboxInDock
    }
    // The phone chrome with the soft keyboard open: the dock is gone, undo and redo stay one tap away at the end of the
    // format bar right above the keyboard
    Row {
        id: keyboardUndo
        objectName: "keyboardUndo"
        parent: formatBar.trailing
        readonly property bool shown: win.phoneChrome && formatBar.docked
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
        visible: win.undoInFormatBar
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
        readonly property bool holdsToolbox: win.toolboxDocked && win.toolboxVertical
        visible: holdsToolbox
        width: !visible ? 0 : toolboxPane.thickness + (win.sideEdge === "right" ? win.safeRight : win.safeLeft)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: win.sideEdge === "right" ? parent.width - width : 0
        color: "#ffffff"
    }
    // The toolbox docked at the top or the bottom (at a side it is in sideTools)
    Rectangle {
        id: toolboxRow
        objectName: "toolboxRow"
        readonly property bool holdsToolbox: win.toolboxDocked && !win.toolboxVertical
        visible: holdsToolbox
        width: parent.width
        height: visible ? toolboxPane.thickness + (win.toolboxEdge === "bottom" ? win.contentBottomInset : 0) : 0
        y: win.toolboxEdge === "bottom" ? parent.height - height : 0
        z: 3
        color: "#ffffff"
    }
    // The toolbox (qt/docs/toolbox.md): the user's own tools; docked beside the page in the full chrome
    Toolbox {
        id: toolboxPane
        parent: win.toolboxInDock ? phoneDock.toolboxSlot : win.toolboxFloating ? win.contentItem
                : sideTools.holdsToolbox ? sideTools : toolboxRow
        visible: win.toolboxShown
        // (the phone's dock: at the bottom, or a rail at the right held sideways)
        edge: win.toolboxInDock ? (win.dockVertical ? "right" : "bottom") : win.toolboxEdge
        compact: win.toolboxInDock
        // (the phone's dock: "+" at the end of its items, the room goes to the tools)
        addInline: win.toolboxInDock
        peer: topBarPane
        floating: win.toolboxFloating
        moreShown: floating
        z: floating ? 60 : 0
        // Floating: no longer than its tools, centred along its edge, 8 px off it (clear of the tab dots at the top
        // and the view pill at the bottom); docked: the whole edge
        readonly property real floatTop: win.controlsTop + (fullScreenTabs.visible ? fullScreenTabs.height : 0) + 8
        // (the view pill sits 24 px above the bottom: the rail ends 8 px above it, however tall the pill is)
        readonly property real floatBottom: win.controlsBottom - Math.max(72, viewPill.height + 24 + 8)
        // (as long as its items; when they do not fit, it scrolls and ends through the middle of a cell)
        readonly property real floatLength: vertical ? lengthFor(floatBottom - floatTop)
                                                     : lengthFor(win.controlsRight - win.controlsLeft - 16)
        x: compact ? 0 : floating ? (edge === "right" ? win.controlsRight - width - 8 : edge === "left" ? win.controlsLeft + 8
                                        : Math.round((win.controlsLeft + win.controlsRight - width) / 2))
           : !vertical ? win.safeLeft : edge === "left" ? win.safeLeft : 0
        y: compact ? 0 : floating ? (edge === "top" ? floatTop : edge === "bottom" ? win.controlsBottom - height - 8
                                      : Math.round(Math.max(floatTop, (floatTop + floatBottom - height) / 2)))
           : 0
        width: compact ? (parent ? parent.width : 0)
               : vertical ? thickness : floating ? floatLength : (parent ? parent.width - win.safeLeft - win.safeRight : 0)
        height: compact ? (parent ? parent.height : 0)
                : !vertical ? thickness : floating ? floatLength : (parent ? parent.height : 0)
        // (a rail's end clear of the navigation bar)
        endInset: vertical && !floating ? Math.max(0, win.contentItem.height - win.controlsBottom) : 0
        // (the app's items on the rail are the window's buttons, lent to it while it is shown: hand, select, snip, mark
        // PDF text at a first start; the others are the top bar's, qt/rail-scroll)
        appButtons: toolArea.slots
        lending: win.toolboxShown
        onPagesRequested: pageGrid.open()
        onEditRequested: function(entry, button) { toolEditor.openFor(entry, button, edge) }
        onMenuRequested: function(entry, button, pos) { toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolTypeMenu.ask("add", "", button, "rail") }
        onMoreRequested: function(button) { toolboxMoreMenu.openMenu(undefined, button) }
        onGripMoved: function(pos) { win.toolboxEdgeTarget = win.edgeAt(pos) }
        // (a tool carried onto another and held there: a group; the snackbar can take it back)
        onGrouped: function(groupId, before) { win.grouped(before) }
        onRemoved: function(entry, before) { win.leftTheBars(entry, before) }
        onGripDropped: function(pos) {
            const edge = win.edgeAt(pos)
            win.toolboxEdgeTarget = ""
            if (edge !== win.toolboxEdge) win.chooseToolboxEdge(edge)
        }
    }
    /// While the toolbox's grip is dragged: the edge it would go to ("": none)
    property string toolboxEdgeTarget: ""
    /// The edge of the window's content nearest to a point of the scene
    function edgeAt(scenePos) {
        const p = contentItem.mapFromItem(null, scenePos.x, scenePos.y)
        const w = contentItem.width, h = contentItem.height
        const d = { "left": p.x, "right": w - p.x, "top": p.y, "bottom": h - p.y }
        let best = "right"
        for (const k in d) if (d[k] < d[best]) best = k
        return best
    }
    // The edge highlighted while the grip is dragged
    Rectangle {
        objectName: "toolboxEdgeHighlight"
        visible: win.toolboxEdgeTarget !== ""
        z: 95
        readonly property string edge: win.toolboxEdgeTarget
        readonly property real band: toolboxPane.thickness
        x: edge === "right" ? parent.width - band : 0
        y: edge === "bottom" ? parent.height - band : 0
        width: edge === "left" || edge === "right" ? band : parent.width
        height: edge === "top" || edge === "bottom" ? band : parent.height
        color: Qt.rgba(Material.accentColor.r, Material.accentColor.g, Material.accentColor.b, 0.22)
        border.width: 2
        border.color: Material.accentColor
    }
    // ⋯ of the floating toolbox (full screen, presenting): what the tab strip and the command bar hold in a window
    AdaptiveMenu {
        id: toolboxMoreMenu
        objectName: "toolboxMoreMenu"
        AdaptiveMenuItem {
            objectName: "toolboxPresentItem"
            text: app.presenting ? qsTr("Stop presenting (Esc)") : qsTr("Present (F5)")
            icon.source: app.iconUrl("xopp-presentation-mode")
            onTriggered: app.presenting ? (app.presenting = false) : win.startPresenting()
        }
        AdaptiveMenuItem {
            objectName: "toolboxPresentCleanItem"
            text: app.presenting ? qsTr("Hide the tools (Ctrl+F5)") : qsTr("Present without controls (Ctrl+F5)")
            icon.source: app.iconUrl("xqt-eye-off")
            onTriggered: app.presenting ? (win.presentClean = true) : win.startPresenting(true)
        }
        // Read only: the pen does not write, the edges turn the pages (qt/docs/zen.md)
        AdaptiveMenuItem {
            objectName: "toolboxReadOnlyItem"
            text: qsTr("Read only")
            icon.source: app.iconUrl("xqt-lock")
            checkable: true
            checked: win.readOnlyOn
            onTriggered: win.readOnly = !win.readOnlyOn
        }
        // Zen: only the page and the dot (qt/docs/zen.md)
        AdaptiveMenuItem {
            objectName: "toolboxZenItem"
            offered: !app.presenting  // (presenting: "Hide the tools" above)
            text: win.withKeys(qsTr("Zen (only the page)"), "zen")
            icon.source: app.iconUrl("xqt-zen")
            onTriggered: win.setZen(true)
        }
        AdaptiveMenuItem {
            objectName: "toolboxSearchItem"
            text: qsTr("Search (Ctrl+F)")
            icon.source: app.iconUrl("xqt-search")
            onTriggered: searchBar.openBar()
        }
        AdaptiveMenuItem {
            objectName: "toolboxSettingsItem"
            text: qsTr("Settings")
            icon.source: app.iconUrl("xqt-settings")
            onTriggered: settingsPage.open()
        }
        MenuSeparator {}
        AdaptiveMenuItem {
            objectName: "toolboxLeaveFullScreenItem"
            text: qsTr("Leave full screen") + (app.presenting ? "" : qsTr(" (Esc)"))
            icon.source: app.iconUrl("xopp-fullscreen")
            onTriggered: win.fullScreenMode = false
        }
        // What the top bar holds (full screen hides it; qt/top-bar): its items in its order, the members of a group one
        // by one, without what is above already; and New (the tab strip's "+" is not shown in full screen)
        MenuSeparator {}
        Repeater {
            model: win.topBarCommands()
            delegate: AdaptiveMenuItem {
                required property var modelData
                readonly property Item button: modelData.app !== undefined ? toolArea.slots[modelData.app] || null : null
                readonly property bool isTool: modelData.app === undefined
                objectName: "toolboxMore_" + (isTool ? modelData.id : modelData.app)
                offered: isTool || (button !== null && button.offered !== false)
                text: isTool ? win.toolEntryName(modelData) : button ? (button.label !== "" ? button.label : button.name) : ""
                icon.source: isTool ? "" : button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
                checkable: !isTool && button !== null && ["hand", "select", "snip", "pdfText", "geometry", "touchDrawing", "write"].indexOf(modelData.app) >= 0
                checked: isTool ? toolboxPane.inHand(modelData) : button !== null && button.checked === true
                onTriggered: {
                    const b = button, e = modelData
                    if (isTool) app.applyToolEntry(e.id)
                    else Qt.callLater(function() { b.clicked() })
                }
            }
        }
        AdaptiveMenuItem {
            objectName: "toolboxNewItem"
            text: win.withKeys(qsTr("New document"), "newDocument")
            icon.source: app.iconUrl("xopp-document-new")
            onTriggered: app.newDocument()
        }
    }
    /// What the top bar holds, for full screen's ⋯: its items in order (the members of a group one by one; no dividers),
    /// without those ⋯ has of its own (present, Zen, search, settings, full screen)
    function topBarCommands() {
        const own = ["present", "zen", "search", "settings", "fullScreen"]
        const out = []
        const list = (app.toolbox.revision, app.toolbox.top)
        for (let i = 0; i < list.length; ++i) {
            const e = list[i]
            const items = e.group === true ? e.members : [e]
            for (let j = 0; j < items.length; ++j) {
                const m = items[j]
                if (m.divider === true || (m.app !== undefined && own.indexOf(m.app) >= 0)) continue
                out.push(m)
            }
        }
        return out
    }
    /// An entry's name for people ("Pen · Body", "Arrow", "Eraser (whiteout)")
    function toolEntryName(entry) { return toolboxPane.entryName(entry) }
    // A tool's editor: a tap on the tool in hand, Edit in its menu, "+" (qt/docs/toolbox.md, "Editing a tool")
    ToolEntryEditor {
        id: toolEditor
        ownerOf: function(id) { return toolboxPane.buttonFor(id) || topBarPane.buttonFor(id) }
    }
    /// The bar that shows an item of the arrangement (the rail or the top bar)
    function paneOf(id) { return app.toolbox.barOf(id) === "top" ? topBarPane : toolboxPane }
    // A tool's menu (a long press, a right click): edit, move, replace, duplicate, add one here, a divider, to the other
    // bar, remove; for an app item (hand, select, open, …): its own list, move, add one here, a divider, to the other
    // bar, off the bars (into the catalog); for a group: its list, ungroup, …
    AdaptiveMenu {
        id: toolEntryMenu
        objectName: "toolEntryMenu"
        titleShown: true
        property var entry: ({})
        property Item button: null
        readonly property string entryId: entry && entry.id ? entry.id : ""
        readonly property var store: app.toolbox
        /// An app item's (its button: the window's); a group's (qt/docs/toolbox.md, "Groups"); else a tool's
        readonly property bool isApp: entry && entry.app !== undefined
        readonly property bool isGroup: entry && entry.group === true
        readonly property bool isTool: !isApp && !isGroup
        readonly property Item appButton: isApp ? toolArea.slots[entry.app] || null : null
        /// The bar it is on ("rail", "top") and that bar
        readonly property string bar: (store.revision, entryId !== "" ? store.barOf(entryId) : "rail")
        readonly property Item pane: bar === "top" ? topBarPane : toolboxPane
        function openFor(e, b, pos) {
            entry = e
            button = b
            title = win.toolEntryName(e)
            openMenu(pos, b)
        }
        // (an app item with a long press of its own: select's kinds, the snips' resolution, how PDF text is marked, the
        // templates of a new page, present without controls, …: on a bar the hold lifts it, so its own is here)
        AdaptiveMenuItem {
            objectName: "toolOptionsItem"
            offered: toolEntryMenu.appButton !== null && toolEntryMenu.appButton.ownHold === true
            text: toolEntryMenu.appButton && toolEntryMenu.appButton.holdText !== undefined ? toolEntryMenu.appButton.holdText
                                                                                            : qsTr("Options…")
            icon.source: app.iconUrl("xqt-more")
            onTriggered: {
                const b = toolEntryMenu.appButton
                win.afterMenus(function() { b.pressAndHold() })
            }
        }
        // A group: its list, and back to its tools one by one
        AdaptiveMenuItem {
            objectName: "toolGroupListItem"
            offered: toolEntryMenu.isGroup
            text: qsTr("Its tools…")
            icon.source: app.iconUrl("xqt-tools-more")
            onTriggered: {
                const id = toolEntryMenu.entryId, b = toolEntryMenu.button
                const pane = toolEntryMenu.pane
                win.afterMenus(function() { pane.openGroup(id, b) })
            }
        }
        AdaptiveMenuItem {
            objectName: "toolUngroupItem"
            offered: toolEntryMenu.isGroup
            text: qsTr("Ungroup")
            icon.source: app.iconUrl("xqt-column-remove")
            onTriggered: toolEntryMenu.store.ungroup(toolEntryMenu.entryId)
        }
        AdaptiveMenuItem {
            objectName: "toolEditItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Edit…")
            icon.source: app.iconUrl("xqt-pencil")
            onTriggered: {
                const e = toolEntryMenu.entry, b = toolEntryMenu.button, edge = toolEntryMenu.pane.edge
                win.afterMenus(function() { toolEditor.openFor(app.toolbox.entry(e.id), b, edge) })
            }
        }
        AdaptiveMenuItem {
            objectName: "toolMoveEarlierItem"
            text: toolEntryMenu.pane.vertical ? qsTr("Move up") : qsTr("Move left")
            icon.source: app.iconUrl(toolEntryMenu.pane.vertical ? "xqt-chevron-up" : "xqt-chevron-left")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canMoveBy(toolEntryMenu.entryId, -1))
            onTriggered: toolEntryMenu.store.moveBy(toolEntryMenu.entryId, -1)
        }
        AdaptiveMenuItem {
            objectName: "toolMoveLaterItem"
            text: toolEntryMenu.pane.vertical ? qsTr("Move down") : qsTr("Move right")
            icon.source: app.iconUrl(toolEntryMenu.pane.vertical ? "xqt-chevron-down" : "xqt-chevron-right")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canMoveBy(toolEntryMenu.entryId, 1))
            onTriggered: toolEntryMenu.store.moveBy(toolEntryMenu.entryId, 1)
        }
        AdaptiveMenuItem {
            objectName: "toolReplaceItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Replace with…")
            icon.source: app.iconUrl("xqt-rotate-right")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.entry.type !== "eraser" || toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
            onTriggered: { const id = toolEntryMenu.entryId, b = toolEntryMenu.button; win.afterMenus(function() { toolTypeMenu.ask("replace", id, b) }) }
        }
        AdaptiveMenuItem {
            objectName: "toolDuplicateItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Duplicate")
            icon.source: app.iconUrl("xqt-copy")
            onTriggered: toolEntryMenu.store.duplicate(toolEntryMenu.entryId)
        }
        AdaptiveMenuItem {
            objectName: "toolAddHereItem"
            text: qsTr("Add a tool here…")
            icon.source: app.iconUrl("xqt-plus")
            onTriggered: {
                const id = toolEntryMenu.entryId, b = toolEntryMenu.button, bar = toolEntryMenu.bar
                win.afterMenus(function() { toolTypeMenu.ask("addHere", id, b, bar) })
            }
        }
        // To the other bar, at its end (carrying it there by hand does the same)
        AdaptiveMenuItem {
            objectName: "toolOtherBarItem"
            text: toolEntryMenu.bar === "top" ? qsTr("Move to the rail") : qsTr("Move to the top bar")
            icon.source: app.iconUrl(toolEntryMenu.bar === "top" ? "xqt-columns" : "xqt-panel-top")
            onTriggered: {
                const other = toolEntryMenu.bar === "top" ? "rail" : "top"
                toolEntryMenu.store.moveTo(toolEntryMenu.entryId, other, toolEntryMenu.store.items(other).length)
            }
        }
        AdaptiveMenuItem {
            objectName: "toolDividerItem"
            readonly property bool has: (toolEntryMenu.store.revision, toolEntryMenu.store.hasDividerAfter(toolEntryMenu.entryId))
            text: has ? qsTr("Remove the divider after it") : qsTr("Add a divider after it")
            icon.source: app.iconUrl("xqt-column-remove")
            onTriggered: toolEntryMenu.store.setDividerAfter(toolEntryMenu.entryId, !has)
        }
        MenuSeparator {}
        AdaptiveMenuItem {
            objectName: "toolRemoveItem"
            offered: true
            text: toolEntryMenu.isGroup ? qsTr("Remove the group") : toolEntryMenu.isApp ? qsTr("Off the bars (into +)")
                  : (toolEntryMenu.store.revision, toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
                  ? qsTr("Remove") : qsTr("Remove (the last eraser stays)")
            icon.source: app.iconUrl("xqt-close")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
            onTriggered: toolEntryMenu.store.remove(toolEntryMenu.entryId)
        }
    }
    // The catalog ("+" at the end of either bar, "Add a tool here…" after an item; qt/top-bar): a new tool of a kind
    // (the editor makes it), and every app tool and command on neither bar, by section; a tap puts it at the end of the
    // bar it was opened from (or after the item). "Replace with…": the kinds alone. On a phone a sheet.
    AdaptiveMenu {
        id: toolTypeMenu
        objectName: "toolTypeMenu"
        titleShown: true
        /// "add", "addHere", "replace"
        property string purpose: "add"
        property string entryId: ""
        property Item button: null
        /// The bar it adds to: "rail", "top"
        property string bar: "rail"
        function ask(why, id, b, toBar) {
            purpose = why
            entryId = id
            button = b
            bar = toBar === "top" ? "top" : "rail"
            title = why === "replace" ? qsTr("Replace with") : bar === "top" ? qsTr("Add to the top bar") : qsTr("Add to the rail")
            openMenu(undefined, b)
        }
        /// Where an item goes on the bar: after the item it was asked from, else at the end (-1)
        function place() { return purpose === "addHere" ? app.toolbox.indexOf(entryId) + 1 : -1 }
        readonly property string edge: bar === "top" ? "top" : toolboxPane.edge
        function chosen(type) {
            const store = app.toolbox
            const b = button
            if (purpose === "replace") {
                const fresh = store.prefill(type)
                if (store.replace(entryId, fresh)) {
                    if (type !== "sticky" && type !== "snip") app.applyToolEntry(entryId)
                    const id = entryId, edge = win.paneOf(id).edge
                    win.afterMenus(function() { toolEditor.openFor(store.entry(id), b, edge) })
                }
                return
            }
            const at = place(), toBar = bar, edge = toolTypeMenu.edge
            win.afterMenus(function() { toolEditor.openNew(type, at, b, edge, toBar) })
        }
        /// An app item on neither bar, put on this one
        function placeItem(name) {
            const store = app.toolbox
            const id = store.place(name, bar, place())
            if (id !== "") Qt.callLater(function() { const p = win.paneOf(id); p.reveal(p.buttonFor(id)) })
        }
        // (a new tool)
        AdaptiveMenuItem {
            objectName: "catalogSection_new"
            offered: toolTypeMenu.purpose !== "replace"
            enabled: false
            text: qsTr("A new tool")
        }
        Repeater {
            model: [{ type: "pen", icon: "xopp-tool-pencil", name: qsTr("Pen") },
                    { type: "highlighter", icon: "xopp-tool-highlighter", name: qsTr("Highlighter") },
                    { type: "shape", icon: "xqt-shapes", name: qsTr("Shape (line, arrow, rectangle, …)") },
                    { type: "eraser", icon: "xopp-tool-eraser", name: qsTr("Eraser") },
                    { type: "text", icon: "xqt-text-box", name: qsTr("Text box") },
                    { type: "sticky", icon: "xqt-sticky-note", name: qsTr("Sticky note") },
                    { type: "laser", icon: "xopp-laser-pointer", name: qsTr("Laser pointer") },
                    { type: "snip", icon: "xqt-snip", name: qsTr("Snip (a picture of a rectangle or lasso to copy)") }]
            delegate: AdaptiveMenuItem {
                required property var modelData
                objectName: "toolType_" + modelData.type
                offered: !win.textDoc || toolTypeMenu.purpose === "replace"
                text: modelData.name
                icon.source: app.iconUrl(modelData.icon)
                onTriggered: toolTypeMenu.chosen(modelData.type)
            }
        }
        // Every app tool and command on neither bar, by section (a section's title, then its items)
        Repeater {
            model: toolTypeMenu.purpose === "replace" ? [] : win.catalogRows()
            delegate: AdaptiveMenuItem {
                required property var modelData
                readonly property bool heading: modelData.section !== undefined
                readonly property Item button: heading ? null : toolArea.slots[modelData.name] || null
                objectName: heading ? "catalogSection_" + modelData.section : "catalog_" + modelData.name
                enabled: !heading
                text: heading ? modelData.title : button ? (button.label !== "" ? button.label : button.name) : ""
                icon.source: !heading && button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
                onTriggered: if (!heading) toolTypeMenu.placeItem(modelData.name)
            }
        }
    }
    /// The catalog's rows of the app's items on neither bar (and offered here): [{section, title}, {name}, …]
    function catalogRows() {
        const sections = [
            { section: "tools", title: qsTr("Tools"), names: ["hand", "select", "snip", "pdfText", "write", "geometry", "touchDrawing"] },
            { section: "insert", title: qsTr("Insert"), names: ["image", "sticker", "addPage", "record"] },
            { section: "view", title: qsTr("View"), names: ["search", "read", "replay", "present", "fullScreen", "zen"] },
            { section: "document", title: qsTr("Document"), names: ["new", "open", "save", "milestone", "share", "print",
                                                                    "tags", "favourite", "bookmark", "settings"] }
        ]
        const free = (app.toolbox.revision, app.toolbox.unplaced())
        const out = []
        sections.forEach(function(sec) {
            const names = sec.names.filter(function(n) {
                const b = toolArea.slots[n]
                return free.indexOf(n) >= 0 && b && b.offered !== false
            })
            if (names.length === 0) return
            out.push({ section: sec.section, title: sec.title })
            names.forEach(function(n) { out.push({ name: n }) })
        })
        return out
    }
    /// Runs `then` once the menus (and a phone's menu sheet) have gone: a dialog or editor opened from a menu entry
    function afterMenus(then) {
        if (!menuSheet.visible) {
            Qt.callLater(then)
            return
        }
        const after = function() {
            menuSheet.closed.disconnect(after)
            then()
        }
        menuSheet.closed.connect(after)
    }
    /// An entry of ⋮ that does what a button of the bars does (`slot`: its name; qt/top-bar: ⋮ is complete)
    component CommandItem: AdaptiveMenuItem {
        property string slot
        readonly property Item button: toolArea.slots[slot] || null
        objectName: "moreCmd_" + slot
        offered: button !== null && button.offered !== false
        text: button ? (button.label !== "" ? button.label : button.name) : ""
        icon.source: button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
        checkable: ["hand", "select", "snip", "pdfText", "geometry", "touchDrawing", "write"].indexOf(slot) >= 0
        checked: button !== null && button.checked === true
        onTriggered: {
            const b = button
            // (a checkable entry toggles itself: it follows its button again)
            checked = Qt.binding(function() { return b !== null && b.checked === true })
            win.afterMenus(function() { b.clicked() })
        }
    }
    // The window's buttons of the app's items (tools and commands, qt/docs/toolbox.md): kept here, out of sight, and
    // lent to the bar that holds each of them in the arrangement (the rail or the top bar, ToolboxModel); one not placed
    // stays here (⋮ and the catalog reach it). The buttons of the moment (the emoji while writing, edit as notes, open
    // externally) sit at the top bar's end, before "+".
    Item {
        id: toolArea
        objectName: "toolArea"
        visible: false
        Material.foreground: "#303030"
        /// Where the popups of the buttons open: below the bar
        readonly property string popupSide: "top"
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
    }
    // ⋮: pinned at the very end of the top bar (a text document: of its format bar; the phone chrome: of the app bar)
    Row {
        id: toolEnd
        objectName: "toolEnd"
        parent: win.phoneChrome ? phoneAppBar.moreSlot : win.toolsInFormatBar ? formatBar.trailing : topBarPane.trailingTail
        y: win.toolsInFormatBar && !win.phoneChrome ? -2 : 0
        spacing: 2
            IconButton {
                objectName: "moreButton"
                iconName: "xqt-more"
                label: qsTr("More")
                tip: qsTr("More")
                onClicked: Popups.openAt(moreMenu)
                // The ⋮ menu (qt/docs/adaptive-layout.md, "Menus"): complete since qt/top-bar - every command, whether
                // a bar shows it or not (the bars are the user's to arrange; ⋮ is not), the commands of the bars in
                // its submenus; a sheet with drill-in on phones.
                AdaptiveMenu {
                    id: moreMenu
                    objectName: "moreMenu"
                    AdaptiveMenuItem { objectName: "saveAsItem"; offered: !win.textDoc; text: qsTr("Save as…"); icon.source: app.iconUrl("xopp-document-save"); onTriggered: openSaveDialog(null) }
                    AdaptiveMenuItem { objectName: "shareItem"; text: qsTr("Share…"); icon.source: app.iconUrl("xqt-share"); onTriggered: shareDialog.openFor("") }
                    AdaptiveMenuItem { objectName: "printItem"; text: qsTr("Print… (Ctrl+P)"); icon.source: app.iconUrl("xopp-document-print"); onTriggered: printDialog.open() }
                    // Find and replace: where text can be written (the search itself: View → Search, and the bars)
                    AdaptiveMenuItem { objectName: "replaceItem"; offered: app.canReplace && !win.reading; text: qsTr("Find and replace (Ctrl+H)"); icon.source: app.iconUrl("xqt-replace"); onTriggered: searchBar.openReplace() }
                    MenuSeparator {}
                    // The document as a file: new, open, save, its name, other ways of editing it, links, its bookmark
                    // and star
                    AdaptiveMenu {
                        objectName: "moreDocumentMenu"
                        title: qsTr("Document")
                        iconName: "xqt-file-text"
                        CommandItem { slot: "new" }
                        CommandItem { slot: "open" }
                        CommandItem { slot: "save" }
                        CommandItem { slot: "editAsNotes" }
                        CommandItem { slot: "openExternally" }
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
                        // Quick note (qt/docs/quick-note.md): a new note in the library's Inbox, or a line in today's
                        // Markdown note there (Settings → Documents). Here, not at the top of ⋮ (at most 10 entries
                        // there): a new document, as the shortcut sheet's group "Document" has it
                        AdaptiveMenuItem {
                            objectName: "documentQuickNoteItem"
                            readonly property var keys: win.keysOf("quickNote")
                            text: keys.length > 0 ? qsTr("Quick note (%1)").arg(keys[0]) : qsTr("Quick note")
                            icon.source: app.iconUrl("xqt-zap")
                            onTriggered: app.quickNote()
                        }
                        MenuSeparator {}
                        // Its name (qt/rename): the file, and what belongs to it, as the library renames it
                        AdaptiveMenuItem { objectName: "renameDocumentItem"; text: qsTr("Rename…"); icon.source: app.iconUrl("xqt-pencil"); onTriggered: renameDocumentDialog.openFor(app.currentTab) }
                        // A password to open it (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): AES-256, its PDF only
                        AdaptiveMenuItem { objectName: "protectDocumentItem"; offered: app.canProtect && !app.protectedDocument; text: qsTr("Protect with a password…"); icon.source: app.iconUrl("xqt-lock"); onTriggered: protectDialog.openFor(false) }
                        AdaptiveMenuItem { objectName: "changePasswordItem"; offered: app.canProtect && app.protectedDocument; text: qsTr("Change or remove the password…"); icon.source: app.iconUrl("xqt-lock-open"); onTriggered: protectDialog.openFor(true) }
                        // Version history (qt/docs/hybrid-pdf.md): the sidebar's History panel (off by default; what it
                        // is and the switch are there)
                        AdaptiveMenuItem { objectName: "versionHistoryItem"; offered: !win.textDoc; text: qsTr("Version history…"); icon.source: app.iconUrl("xqt-history"); onTriggered: win.showHistory() }
                        AdaptiveMenuItem {
                            objectName: "saveWithMessageItem"
                            offered: !win.textDoc
                            readonly property var keys: win.keysOf("saveWithMessage")
                            text: keys.length > 0 ? qsTr("Save with a message… (%1)").arg(keys[0]) : qsTr("Save with a message…")
                            icon.source: app.iconUrl("xqt-flag")
                            onTriggered: versionMessageDialog.openFor(-1)
                        }
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
                        // Its tags: a PDF's keywords, without typing into it (qt/docs/tags.md)
                        AdaptiveMenuItem {
                            objectName: "documentTagsMenuItem"
                            text: qsTr("Tags…")
                            icon.source: app.iconUrl("xqt-tag")
                            onTriggered: documentTagsDialog.openFor(app.currentDocumentPath())
                        }
                        // Which handwriting models read this document (qt/docs/handwriting-search.md): found from its
                        // first lines, or chosen; kept in the library's cache
                        AdaptiveMenu {
                            id: handwritingLanguageMenu
                            objectName: "handwritingLanguageMenu"
                            title: qsTr("Handwriting language")
                            iconName: "xqt-pencil"
                            offered: app.handwriting.enabled && !win.textDoc
                            readonly property string current: (app.currentTab, app.handwritingLanguage)
                            AdaptiveMenuItem { objectName: "handwritingLanguageAuto"; checkable: true; checked: handwritingLanguageMenu.current === "auto"; text: qsTr("Automatic"); onTriggered: app.handwritingLanguage = "auto" }
                            AdaptiveMenuItem { objectName: "handwritingLanguageEn"; checkable: true; checked: handwritingLanguageMenu.current === "en"; text: qsTr("English"); onTriggered: app.handwritingLanguage = "en" }
                            AdaptiveMenuItem { objectName: "handwritingLanguageDe"; checkable: true; checked: handwritingLanguageMenu.current === "de"; text: qsTr("German"); onTriggered: app.handwritingLanguage = "de" }
                            AdaptiveMenuItem { objectName: "handwritingLanguageBoth"; checkable: true; checked: handwritingLanguageMenu.current === "both"; text: qsTr("Both"); onTriggered: app.handwritingLanguage = "both" }
                        }
                        // Marks other apps put into the PDF made editable (qt/docs/adopt-annotations.md)
                        AdaptiveMenuItem {
                            objectName: "adoptAnnotationsItem"
                            offered: !win.textDoc
                            enabled: !app.adopting
                            text: app.adoptableCount > 0 ? qsTr("Adopt annotations from other apps (%1)…").arg(app.adoptableCount)
                                                         : qsTr("Adopt annotations from other apps…")
                            icon.source: app.iconUrl("xopp-tool-highlighter")
                            onTriggered: app.adoptableCount > 0 ? adoptDialog.openFor(app.adoptableCount, app.adoptableApp, false)
                                                                : app.adoptAnnotations()
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
                        // Pages as PNG or JPEG pictures (qt/docs/page-files.md)
                        AdaptiveMenuItem { objectName: "exportImagesItem"; offered: !win.textDoc; text: qsTr("Export pages as pictures…"); icon.source: app.iconUrl("xqt-file-image"); onTriggered: pageFiles.openImages(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
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
                        // Pages as files (qt/docs/page-files.md): from a file, into a new document, split
                        AdaptiveMenuItem { objectName: "insertFromFileItem"; offered: app.canInsertTemplate; text: qsTr("Insert pages from a file…"); icon.source: app.iconUrl("xqt-import"); onTriggered: pageFiles.chooseFile(app.pageNumber - 1, true) }
                        AdaptiveMenuItem { objectName: "copyPageImageItem"; text: qsTr("Copy page as image"); icon.source: app.iconUrl("xqt-copy"); onTriggered: app.copyPagesAsImage(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                        AdaptiveMenuItem { objectName: "extractPagesItem"; text: qsTr("Extract to a new document…"); icon.source: app.iconUrl("xqt-file-output"); onTriggered: pageFiles.openExtract(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                        AdaptiveMenuItem { objectName: "splitDocumentItem"; text: qsTr("Split the document…"); icon.source: app.iconUrl("xqt-page-break"); onTriggered: pageFiles.openSplit(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                        // Page templates (qt/docs/templates.md): this page saved to be added again; one added
                        AdaptiveMenuItem { objectName: "saveTemplateItem"; text: qsTr("Save page as template…"); icon.source: app.iconUrl("xqt-file-plus"); onTriggered: templateSaveDialog.openForPage(app.pageNumber - 1) }
                        AdaptiveMenuItem { objectName: "insertTemplateItem"; offered: app.canInsertTemplate; text: qsTr("Add a page from a template…"); icon.source: app.iconUrl("xopp-page-add"); onTriggered: templatePicker.openToInsert(app.pageNumber) }
                        AdaptiveMenuItem { objectName: "pageBackgroundItem"; text: qsTr("Background of this page…"); icon.source: app.iconUrl("xqt-palette"); onTriggered: backgroundDialog.openFor([app.pageNumber - 1]) }
                        // Another paper size for this page, the selected pages or all of them (PageSizeDialog)
                        AdaptiveMenuItem { objectName: "pageSizeItem"; text: qsTr("Page size…"); icon.source: app.iconUrl("xqt-scaling"); onTriggered: pageSizeDialog.openFor([app.pageNumber - 1]) }
                        // Writing space beside the slides of all pages (qt/docs/note-space.md)
                        AdaptiveMenuItem { objectName: "noteSpaceItem"; text: qsTr("Space for notes…"); icon.source: app.iconUrl("xqt-note-space"); onTriggered: noteSpaceDialog.openFor([app.pageNumber - 1], true) }
                        AdaptiveMenuItem { objectName: "chapterItem"; text: qsTr("Start a chapter here…"); icon.source: app.iconUrl("xqt-toc"); onTriggered: chapterDialog.openFor(app.pageNumber - 1) }
                        // A quarter turn of this page or of all pages (qt/docs/page-rotation.md); PDF pages of a .xopp
                        // stay, and the menu says why
                        AdaptiveMenu {
                            id: rotateMenu
                            objectName: "rotatePagesMenu"
                            title: qsTr("Rotate")
                            iconName: "xqt-rotate-right"
                            readonly property var thisPage: (app.pageNumber, app.pageCount, app.currentTab, app.modified, app.rotationOf([app.pageNumber - 1]))
                            readonly property var everyPage: (app.pageNumber, app.pageCount, app.currentTab, app.modified, app.rotationOf(app.allPages()))
                            AdaptiveMenuItem { objectName: "rotatePageLeftItem"; text: qsTr("This page left"); icon.source: app.iconUrl("xqt-rotate-left"); enabled: rotateMenu.thisPage.possible === true; onTriggered: app.rotatePages([app.pageNumber - 1], false) }
                            AdaptiveMenuItem { objectName: "rotatePageRightItem"; text: qsTr("This page right"); icon.source: app.iconUrl("xqt-rotate-right"); enabled: rotateMenu.thisPage.possible === true; onTriggered: app.rotatePages([app.pageNumber - 1], true) }
                            AdaptiveMenuItem { objectName: "rotateAllLeftItem"; text: qsTr("All pages left"); icon.source: app.iconUrl("xqt-rotate-left"); enabled: rotateMenu.everyPage.possible === true; onTriggered: app.rotatePages(app.allPages(), false) }
                            AdaptiveMenuItem { objectName: "rotateAllRightItem"; text: qsTr("All pages right"); icon.source: app.iconUrl("xqt-rotate-right"); enabled: rotateMenu.everyPage.possible === true; onTriggered: app.rotatePages(app.allPages(), true) }
                            AdaptiveMenuItem {
                                objectName: "rotateReasonItem"
                                offered: (rotateMenu.everyPage.reason || "") !== ""
                                enabled: false
                                text: rotateMenu.everyPage.reason || ""
                            }
                        }
                    }
                    // The app's tools and what is put on the page (the buttons of the bars, wherever they are)
                    AdaptiveMenu {
                        objectName: "moreToolsMenu"
                        title: qsTr("Tools")
                        iconName: "xqt-tools-more"
                        offered: !win.textDoc
                        CommandItem { slot: "hand" }
                        CommandItem { slot: "select" }
                        CommandItem { slot: "snip" }
                        CommandItem { slot: "pdfText" }
                        CommandItem { slot: "write" }
                        CommandItem { slot: "geometry" }
                        CommandItem { slot: "touchDrawing" }
                        MenuSeparator {}
                        CommandItem { slot: "image" }
                        CommandItem { slot: "sticker" }
                        CommandItem { slot: "addPage" }
                        CommandItem { slot: "record" }
                    }
                    // How the document is shown (all pages are the view pill's; hiding the top bar is the tab on its
                    // edge)
                    AdaptiveMenu {
                        objectName: "moreViewMenu"
                        title: qsTr("View")
                        iconName: "xqt-eye"
                        CommandItem { slot: "search" }
                        CommandItem { slot: "fullScreen" }
                        CommandItem { slot: "present" }
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
                        // A black sheet over part of the page, for teaching and presenting (qt/docs/curtain.md)
                        AdaptiveMenuItem { objectName: "curtainItem"; offered: !win.textDoc; checkable: true; checked: app.curtain === "curtain"; text: qsTr("Curtain (B)"); icon.source: app.iconUrl("xqt-curtain"); onTriggered: app.toggleCurtain("curtain") }
                        AdaptiveMenuItem { objectName: "spotlightItem"; offered: !win.textDoc; checkable: true; checked: app.curtain === "spotlight"; text: qsTr("Spotlight (Shift+B)"); icon.source: app.iconUrl("xqt-spotlight"); onTriggered: app.toggleCurtain("spotlight") }
                        AdaptiveMenuItem { objectName: "presentCleanItem"; text: qsTr("Present without controls (Ctrl+F5)"); icon.source: app.iconUrl("xopp-presentation-mode"); onTriggered: win.startPresenting(true) }
                        // Zen: only the page and a faint dot (qt/docs/zen.md)
                        AdaptiveMenuItem {
                            objectName: "zenItem"
                            checkable: true
                            checked: win.zenShown
                            text: win.withKeys(qsTr("Zen (only the page)"), "zen")
                            icon.source: app.iconUrl("xqt-zen")
                            onTriggered: win.setZen(!win.zenShown)
                        }
                        // Read only: the pen does not write, the edges turn the pages
                        AdaptiveMenuItem {
                            objectName: "readOnlyItem"
                            offered: win.readOnlyOffered
                            checkable: true
                            checked: win.readOnlyOn
                            text: qsTr("Read only")
                            icon.source: app.iconUrl("xqt-lock")
                            onTriggered: win.readOnly = !win.readOnlyOn
                        }
                        // Read: Zen and read only, in full screen
                        AdaptiveMenuItem {
                            objectName: "readItem"
                            offered: !win.textDoc
                            text: win.withKeys(qsTr("Read (Zen, read only)"), "readOnly")
                            icon.source: app.iconUrl("xqt-book-open")
                            onTriggered: win.startReading()
                        }
                        // Pages shown dark, the document unchanged (qt/docs/dark-pages.md)
                        AdaptiveMenu {
                            objectName: "darkPagesMenu"
                            offered: !win.textDoc
                            title: qsTr("Dark pages")
                            iconName: "xqt-moon"
                            component DarkItem: AdaptiveMenuItem {
                                property string mode
                                checkable: true
                                checked: app.darkPagesMode === mode
                                onTriggered: app.darkPagesMode = mode
                            }
                            DarkItem { objectName: "darkPagesOffItem"; text: qsTr("Off"); mode: "off" }
                            DarkItem { objectName: "darkPagesOnItem"; text: qsTr("On"); mode: "on" }
                            DarkItem { objectName: "darkPagesSystemItem"; text: qsTr("With the system's dark mode"); mode: "system" }
                        }
                        // The document's timeline: how it was written, with its recordings (qt/docs/timeline.md)
                        AdaptiveMenuItem { objectName: "replayItem"; offered: !win.textDoc; text: qsTr("Replay the writing"); icon.source: app.iconUrl("xqt-replay"); onTriggered: app.timeline.start() }
                        MenuSeparator {}
                        // The toolbox's edge in this size class (qt/docs/toolbox.md); the phone classes have their dock
                        AdaptiveMenu {
                            objectName: "toolboxPositionMenu"
                            offered: !win.phoneLayout
                            title: qsTr("Toolbox position")
                            iconName: "xqt-panel-top"
                            component EdgeItem: AdaptiveMenuItem {
                                property string edge
                                checkable: true
                                checked: win.toolboxEdge === edge
                                onTriggered: win.chooseToolboxEdge(edge)
                            }
                            EdgeItem { objectName: "toolboxRightItem"; text: qsTr("Right"); edge: "right" }
                            EdgeItem { objectName: "toolboxLeftItem"; text: qsTr("Left"); edge: "left" }
                            EdgeItem { objectName: "toolboxTopItem"; text: qsTr("Top"); edge: "top" }
                            EdgeItem { objectName: "toolboxBottomItem"; text: qsTr("Bottom"); edge: "bottom" }
                            MenuSeparator {}
                            AdaptiveMenuItem {
                                objectName: "toolboxAutoItem"
                                text: qsTr("Automatic for this window size")
                                checkable: true
                                checked: win.toolboxChoice === ""
                                onTriggered: win.chooseLayout("toolbox", "")
                            }
                        }
                    }
                    // Help (qt/docs/onboarding.md): the introduction of the first start, the tutorial, the keyboard
                    // shortcuts
                    AdaptiveMenu {
                        objectName: "moreHelpMenu"
                        title: qsTr("Help")
                        iconName: "xqt-help"
                        AdaptiveMenuItem { objectName: "helpIntroItem"; text: qsTr("Introduction"); icon.source: app.iconUrl("xqt-book-open"); onTriggered: introDialog.show() }
                        AdaptiveMenuItem { objectName: "helpTutorialItem"; text: qsTr("Tutorial"); icon.source: app.iconUrl("xqt-notebook-pen"); onTriggered: app.openTutorial() }
                        AdaptiveMenuItem { objectName: "helpRestartTutorialItem"; offered: app.tutorialExists; text: qsTr("Start the tutorial again…"); icon.source: app.iconUrl("xopp-edit-undo"); onTriggered: restartTutorialDialog.open() }
                        AdaptiveMenuItem { objectName: "helpShortcutsItem"; text: qsTr("Keyboard shortcuts (F1)"); icon.source: app.iconUrl("xqt-keyboard"); onTriggered: shortcutSheet.open() }
                    }
                    CommandItem { slot: "settings" }
                }
            }
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
        readonly property bool inline: win.toolsInFormatBar && !win.phoneChrome
        parent: win.phoneChrome ? phoneAppBar.toolsSlot : inline ? formatBar.commandsSlot : topTools
        visible: win.topBarShown
        headShown: win.undoInToolBar
        // (in the format bar: its buttons' size)
        cell: inline ? 40 : win.adaptive.touchProfile ? win.adaptive.minTarget : 44
        x: inline || win.phoneChrome ? 0 : win.safeLeft + 2
        width: inline ? naturalLength : parent ? parent.width - (win.phoneChrome ? 0 : win.safeLeft + win.safeRight + 4) : 0
        height: parent ? parent.height : 0
        appButtons: toolArea.slots
        lending: win.topBarShown
        onEditRequested: function(entry, button) { toolEditor.openFor(entry, button, "top") }
        onMenuRequested: function(entry, button, pos) { toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolTypeMenu.ask("add", "", button, "top") }
        onGrouped: function(groupId, before) { win.grouped(before) }
        onRemoved: function(entry, before) { win.leftTheBars(entry, before) }
    }
    /// Popups open that close on Android's back key (sheets, dialogs, the editor, the pickers): Zen's Back waits for
    /// them (two shortcuts of the same key would be ambiguous: neither would act)
    property int backTakers: 0
    function takeBack(on) { backTakers = Math.max(0, backTakers + (on ? 1 : -1)) }
    /// The top bar is shown (at the top, in a text document's format bar, in a phone's app bar)
    readonly property bool topBarShown: !app.homeVisible && !noToolbar && !replaying
    /// A carried item made a group: "Grouped · Undo"
    function grouped(before) {
        snackbar.show(qsTr("Grouped"), false, qsTr("Undo"), function() { app.toolbox.restore(before) })
    }
    /// A carried item let go away from both bars left them: "Removed · Undo" (an app item: "… is in + now")
    function leftTheBars(entry, before) {
        const text = entry && entry.app !== undefined ? qsTr("%1 is in + now").arg(toolEntryName(entry))
                                                       : qsTr("Removed: %1").arg(toolEntryName(entry))
        snackbar.show(text, false, qsTr("Undo"), function() { app.toolbox.restore(before) })
    }
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
            readonly property string holdText: qsTr("Markdown source beside the page…")
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
                ownerX: toolArea.popupSide === "left" ? emojiButton.width : toolArea.popupSide === "right" ? -width : 0
                ownerY: toolArea.popupSide === "bottom" ? -height : toolArea.popupSide === "top" ? emojiButton.height : 0
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
            popupSide: toolArea.popupSide
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
            tip: qsTr("Present (F5; hold: its menu with \"Present without controls\", Ctrl+F5)")
            ownHold: true
            /// What its long press does (in its menu on a bar)
            readonly property string holdText: qsTr("Present without controls (Ctrl+F5)")
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
        // Commands of the top bar's first layout that ⋮ has too (qt/docs/toolbox.md, "The top bar": ⋮ is complete)
        IconButton {
            id: shareTool
            objectName: "shareButton"
            parent: toolBank
            iconName: "xqt-share"
            label: qsTr("Share")
            tip: qsTr("Share…")
            onClicked: shareDialog.openFor("")
        }
        IconButton {
            id: printTool
            objectName: "printButton"
            parent: toolBank
            iconName: "xopp-document-print"
            label: qsTr("Print")
            tip: qsTr("Print… (Ctrl+P)")
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
            tip: win.withKeys(qsTr("Read: Zen and read only, in full screen (the edges turn the pages)"), "readOnly")
            onClicked: win.startReading()
        }
        // Zen: only the page and a faint dot in the lower left corner (qt/docs/zen.md)
        IconButton {
            id: zenTool
            objectName: "zenButton"
            parent: toolBank
            iconName: "xqt-zen"
            label: qsTr("Zen")
            tip: win.withKeys(qsTr("Zen: only the page (the dot in the lower left corner brings the controls back)"), "zen")
            onClicked: win.setZen(true)
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

    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: win.toolboxTop
        anchors.bottomMargin: win.toolboxBottom
        // As a drawer it slides in from the left edge (win.drawerSlide); clear of a cut-out at the window's left edge
        // (sidebarLeftFill has the sidebar's color there)
        x: (win.sideEdge === "left" && sideTools.width > 0 ? sideTools.width : win.safeLeft)
           - (win.sidebarDocked ? 0 : Math.round((1 - win.drawerSlide) * (width + win.safeLeft)))
        width: win.sidebarDocked ? 210 : win.drawerWidth
        bottomInset: Math.max(0, height - win.controlsBottom)
        visible: win.fullChrome && (win.sidebarShown || (win.sidebarAsDrawer && win.drawerSlide > 0))
        // As a drawer (no room beside the page): over the page, below the home screen; it closes once a page is picked
        z: win.sidebarAsDrawer ? 49 : 0
        onPagePicked: if (win.sidebarAsDrawer) win.showSidebar(false)
        onVersionMessageRequested: function(id) { versionMessageDialog.openFor(id) }
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
        visible: sidebar.visible && win.safeLeft > 0 && !(win.sideEdge === "left" && sideTools.width > 0)
        x: sidebar.x - width
        width: win.safeLeft
        anchors.top: sidebar.top
        anchors.bottom: sidebar.bottom
        z: sidebar.z
        color: sidebar.color
    }
    // The page sidebar's tab (qt/docs/adaptive-layout.md, "The page sidebar"): an arrow at the left edge of the canvas
    // area opens it (beside the page where there is room, else as the drawer); at the sidebar's edge, "‹" closes it.
    // A finger's size in the touch profile; not in the compact chrome or Zen, nor while presenting, nor while the
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
        darkPages: app.darkPagesShown  // (qt/docs/dark-pages.md)
        // (a version cut out of its file, compared or shown: read-only)
        readingOnly: win.reading || win.replaying || app.viewingVersion
        snapVertically: win.reading && !app.presenting
        // Reading: the edges turn the pages (a tap the page does not take otherwise; readingTapFields); in Zen a
        // finger's tap in the middle opens the dot's pill (after the double tap's time: a double tap zooms)
        edgeTapWidth: readingTapFields.visible ? readingTapFields.fieldWidth : 0
        onEdgeTapped: function(side) { readingTapFields.turn(side) }
        onMiddleTapped: function(pos, count) {
            if (count > 1) zenPillDelay.stop()
            else if (win.zenShown && zenDot.visible) zenPillDelay.restart()
        }
        // A stroke tried while read only: said once, at the pen (qt/docs/zen.md)
        onWritingRefused: function(pos) { if (win.readOnlyOn) readOnlyNote.tell(pos) }

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
            visible: win.presenterConsole && shown.width > 0 && shown.height > 0
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
        visible: win.presenterConsole
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: win.safeRight
        width: win.presenterPanelWidth
        z: 4
        onStopRequested: app.presenting = false
    }
    // Reference mode: another document beside this one (the canvas area is split)
    ReferenceSplit {
        id: referenceSplit
        bottomInset: y + height - win.controlsBottom
        anchors.top: parent.top
        anchors.topMargin: win.toolboxTop
        anchors.bottom: win.sourcePanel && win.sourceAtBottom ? win.sourcePanel.top : parent.bottom
        anchors.bottomMargin: win.sourcePanel && win.sourceAtBottom ? 0 : win.toolboxBottom
        anchors.right: win.presenterConsole ? presenterPanel.left
                       : win.sourcePanel && !win.sourceAtBottom ? win.sourcePanel.left
                       : win.dockRail ? phoneDock.left
                       : (win.sideEdge === "right" ? sideTools.left : parent.right)
        anchors.left: sidebar.visible && !win.sidebarAsDrawer ? sidebar.right
                      : (win.sideEdge === "left" ? sideTools.right : parent.left)
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
        anchors.leftMargin: (zenDot.visible ? 56 : 24) + win.canvasControlsLeft - canvas.x  // (in Zen: beside the dot)
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
        // also in full screen; presenting only the page number, for a moment (presentPageIndicator); not in Zen
        visible: !pageGrid.visible && !contentsOverview.visible && !app.presenting && !win.hudHidden && !win.phoneChrome
        /// The compact pill, in a canvas under 520 px wide (a phone, a half beside the reference or the source): undo,
        /// redo (while the tool bar is not shown: win.undoInToolBar), the page number (a tap: all pages), the contents
        /// and the zoom; the page layout is in ⋮ → View then
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
        /// (above the toolbox floating at the bottom edge where the two would meet: full screen, on a phone held
        /// upright too)
        readonly property real lowY: {
            const y = win.canvasControlsBottom - 24 - height
            const t = toolboxPane
            const meetsToolbox = win.toolboxFloating && t.edge === "bottom" && x < t.x + t.width && x + width > t.x
            return meetsToolbox ? Math.min(y, t.y - 12 - height) : y
        }
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
            // Undo and redo while the tool bar is not shown (it leads with them otherwise: one place at a time)
            IconButton {
                objectName: "undoButton"
                visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar
                iconName: "xopp-edit-undo"
                label: qsTr("Undo")
                tip: win.withKeys(qsTr("Undo"), "undo")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canUndo
                onClicked: app.undo()
            }
            IconButton {
                objectName: "redoButton"
                visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar && !viewPill.tight
                iconName: "xopp-edit-redo"
                label: qsTr("Redo")
                tip: win.withKeys(qsTr("Redo"), "redo")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canRedo
                onClicked: app.redo()
            }
            ToolSeparator { visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar && !viewPill.tight }
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
                        enabled: app.horizontalScrolling || win.reading  // (up and down: while reading)
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
            // The canvas turned (qt/docs/canvas-rotation.md): by how much; a tap turns it upright again
            ToolButton {
                objectName: "rotationChip"
                visible: app.canvasRotation !== 0
                readonly property int degrees: Math.round(app.canvasRotation > 180 ? app.canvasRotation - 360
                                                                                   : app.canvasRotation)
                text: "\u21ba " + degrees + "\u00b0"
                font.pixelSize: 13
                implicitHeight: 40
                leftPadding: 8
                rightPadding: 8
                focusPolicy: Qt.NoFocus
                Material.foreground: "#505050"
                Accessible.name: qsTr("Turn the canvas upright")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Turned by %1\u00b0: tap to turn it upright (two taps on the page or a fit do too)").arg(degrees)
                ToolTip.delay: 600
                onClicked: app.resetCanvasRotation()
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
        anchors.bottomMargin: win.toolboxBottom
        anchors.right: win.dockRail ? phoneDock.left : win.sideEdge === "right" ? sideTools.left : parent.right
        width: !visible ? 0 : atBottom ? referenceSplit.width : win.sourceSideWidth
        height: atBottom ? win.sourceBottomHeight : parent.height - win.toolboxTop - win.toolboxBottom
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
        anchors.bottomMargin: win.toolboxBottom
        anchors.right: win.dockRail ? phoneDock.left : win.sideEdge === "right" ? sideTools.left : parent.right
        width: !visible ? 0 : atBottom ? referenceSplit.width : win.sourceSideWidth
        height: atBottom ? win.sourceBottomHeight : parent.height - win.toolboxTop - win.toolboxBottom
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
        onStickerRequested: stickerSaveDialog.openForSelection()
    }

    // The selected sticky note: its color, cover mode, delete.
    NotePill {
        id: notePill
        objectName: "notePill"
        canvasItem: canvas
        avoid: viewPill
        bottomLimit: win.controlsBottom
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
        roomTop: win.controlsTop
        roomBottom: win.controlsBottom
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
        anchors.leftMargin: (zenDot.visible ? 56 : 20) + win.canvasControlsLeft - canvas.x  // (in Zen: beside the dot)
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
        // Find and replace (qt/docs/md-editor.md): not while reading; on the source beside the page while it is open
        replaceAllowed: !win.reading
        sourcePanel: markdownPanel.visible ? markdownPanel : null
        onNotice: function(text, undo) {
            snackbar.show(text, false, undo ? qsTr("Undo") : "", undo ? function() { app.undo() } : null)
        }
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
    // A PDF that needs a password to open (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): asked here, kept in memory only
    // while the document is open. A wrong one is said so and asked again; Cancel leaves it closed.
    AdaptiveDialog {
        id: pdfPasswordDialog
        objectName: "pdfPasswordDialog"
        kind: "question"
        property string file: ""
        property bool wrong: false
        preferredWidth: 420
        title: qsTr("Password")
        onOpened: pdfPasswordField.forceActiveFocus()
        ColumnLayout {
            width: pdfPasswordDialog.availableWidth
            spacing: 8
            Label {
                objectName: "pdfPasswordText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 is protected with a password. Enter it to open the document.").arg(pdfPasswordDialog.file)
            }
            TextField {
                id: pdfPasswordField
                objectName: "pdfPasswordField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
                onAccepted: pdfPasswordDialog.accept()
            }
            Label {
                objectName: "pdfPasswordWrong"
                Layout.fillWidth: true
                visible: pdfPasswordDialog.wrong
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: qsTr("The password is not right. Try again.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "pdfPasswordOpen"
                text: qsTr("Open")
                enabled: pdfPasswordField.text !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                objectName: "pdfPasswordCancel"
                text: qsTr("Cancel")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            const password = pdfPasswordField.text
            pdfPasswordField.text = ""  // (not kept in the window)
            app.openWithPassword(password)
        }
        onRejected: {
            pdfPasswordField.text = ""
            app.cancelPassword()
        }
    }
    Connections {
        target: app
        function onPasswordNeeded(file, wrong) {
            pdfPasswordDialog.file = file
            pdfPasswordDialog.wrong = wrong
            pdfPasswordDialog.open()
        }
    }
    // ⋮ → Document → "Protect with a password…" / "Change or remove the password…": AES-256 for its PDF, optional
    // restrictions with a second password
    AdaptiveDialog {
        id: protectDialog
        objectName: "protectDialog"
        kind: "question"
        /// The document is protected already: change or remove its password
        property bool changing: false
        readonly property string problem: {
            if (protectPassword.text !== protectConfirm.text) return qsTr("The two passwords differ.")
            return app.checkProtection(protectPassword.text, protectOwnerPassword.text, !restrictBox.checked || printBox.checked,
                                       !restrictBox.checked || copyBox.checked, !restrictBox.checked || editBox.checked)
        }
        function openFor(change) {
            changing = change
            protectPassword.text = ""
            protectConfirm.text = ""
            protectOwnerPassword.text = ""
            restrictBox.checked = false
            printBox.checked = true
            copyBox.checked = true
            editBox.checked = true
            open()
            protectPassword.forceActiveFocus()
        }
        function clearFields() {
            protectPassword.text = ""
            protectConfirm.text = ""
            protectOwnerPassword.text = ""
        }
        preferredWidth: 480
        title: changing ? qsTr("Change or remove the password") : qsTr("Protect with a password")
        ColumnLayout {
            width: protectDialog.availableWidth
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Anyone who opens %1 needs this password, in any PDF app. If it is forgotten, nobody can "
                           + "open the document again, not even this app.").arg(app.title)
            }
            Label {
                objectName: "protectVersionsNote"
                Layout.fillWidth: true
                visible: app.versions.on
                wrapMode: Text.Wrap
                color: "#b06000"
                text: qsTr("The file is written anew: the earlier versions it keeps are removed.")
            }
            TextField {
                id: protectPassword
                objectName: "protectPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: protectDialog.changing ? qsTr("New password") : qsTr("Password")
            }
            TextField {
                id: protectConfirm
                objectName: "protectConfirm"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("The same password again")
                onAccepted: if (protectDialog.problem === "") protectDialog.accept()
            }
            CheckBox {
                id: restrictBox
                objectName: "protectRestrict"
                Layout.fillWidth: true
                text: qsTr("Restrict what others can do with it")
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                visible: restrictBox.checked
                spacing: 2
                CheckBox { id: printBox; objectName: "protectAllowPrint"; text: qsTr("Allow printing"); checked: true }
                CheckBox { id: copyBox; objectName: "protectAllowCopy"; text: qsTr("Allow copying text"); checked: true }
                CheckBox { id: editBox; objectName: "protectAllowEdit"; text: qsTr("Allow changes (notes, forms, pages)"); checked: true }
                TextField {
                    id: protectOwnerPassword
                    objectName: "protectOwnerPassword"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: qsTr("A second password, to lift the restrictions")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6b6f75"
                    text: qsTr("PDF apps that respect restrictions (Acrobat, Preview) apply them; others may not.")
                }
            }
            Label {
                objectName: "protectProblem"
                Layout.fillWidth: true
                visible: protectPassword.text !== "" && protectDialog.problem !== ""
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: protectDialog.problem
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "protectRemove"
                visible: protectDialog.changing
                text: qsTr("Remove the password")
                flat: true
                onClicked: {
                    protectDialog.clearFields()
                    protectDialog.close()
                    app.removeProtection()
                }
            }
            Button {
                objectName: "protectAccept"
                text: protectDialog.changing ? qsTr("Change") : qsTr("Protect")
                enabled: protectPassword.text !== "" && protectDialog.problem === ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            const password = protectPassword.text
            const owner = protectOwnerPassword.text
            const restricted = restrictBox.checked
            clearFields()
            app.protectDocument(password, owner, !restricted || printBox.checked, !restricted || copyBox.checked,
                                !restricted || editBox.checked)
        }
        onRejected: clearFields()
    }
    // Share…: the PDF with notes (shown in the file manager, or copied), or a copy for Xournal++ users
    AdaptiveDialog {
        id: shareDialog
        objectName: "shareDialog"
        kind: "question"
        property string file: ""  // a PDF of the library; "": the current document
        /// A Markdown or text file (the current document's, or a card's): shared as the file itself, never as a PDF
        property string textFile: ""
        /// The PDF with notes keeps its versions (version history): they go along only when chosen
        property bool keepsVersions: false
        function openFor(path) {
            file = path
            textFile = app.sharedTextFile(path)
            keepsVersions = textFile === "" && app.sharedKeepsVersions(path)
            withHistoryBox.checked = false
            shareProtectBox.checked = app.protectedDocument
            sharePasswordField.text = ""
            open()
        }
        function share(toClipboard) {
            if (sharePasswordField.visible) {
                const password = sharePasswordField.text
                sharePasswordField.text = ""
                app.sharePdfProtected(password, toClipboard)
            } else {
                win.sharePdfOf(file, toClipboard, withHistoryBox.checked)
            }
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
            // Version history: without the versions unless chosen (older versions may hold ink that was deleted)
            CheckBox {
                id: withHistoryBox
                objectName: "shareWithHistory"
                Layout.fillWidth: true
                visible: shareDialog.keepsVersions
                text: qsTr("With its version history")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Off (recommended): the PDF goes as it is now. On: with every version it keeps, "
                                   + "also ink that was deleted since.")
            }
            // "Protect with a password": the PDF with notes shared as a copy that needs this password (AES-256). A
            // protected document is shared with its own password anyway.
            CheckBox {
                id: shareProtectBox
                objectName: "shareProtect"
                Layout.fillWidth: true
                visible: shareDialog.textFile === "" && shareDialog.file === ""
                enabled: !app.protectedDocument
                checked: app.protectedDocument
                text: app.protectedDocument ? qsTr("Protected with its password") : qsTr("Protect with a password")
            }
            TextField {
                id: sharePasswordField
                objectName: "sharePassword"
                Layout.fillWidth: true
                visible: shareProtectBox.visible && shareProtectBox.checked && !app.protectedDocument
                echoMode: TextInput.Password
                placeholderText: qsTr("Password to open it")
            }
            ShareChoice {
                objectName: "sharePdfChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("PDF with notes (opens in any app)")
                detail: app.canShare ? qsTr("Shown in the file manager, to send it on.")
                                     : qsTr("Not available on this system yet.")
                enabled: app.canShare && (!sharePasswordField.visible || sharePasswordField.text !== "")
                onClicked: { shareDialog.close(); shareDialog.share(false) }
            }
            ShareChoice {
                objectName: "shareCopyChoice"
                visible: shareDialog.textFile === ""
                text: qsTr("Copy the PDF with notes")
                detail: qsTr("Paste it into another app or a chat.")
                enabled: !sharePasswordField.visible || sharePasswordField.text !== ""
                onClicked: { shareDialog.close(); shareDialog.share(true) }
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
            // (PDF/A allows no encryption: an archive PDF of a protected document has no password)
            Label {
                objectName: "archiveNotProtected"
                Layout.fillWidth: true
                visible: archiveDialog.file === "" && app.protectedDocument
                wrapMode: Text.Wrap
                color: "#b06000"
                text: qsTr("This document is protected with a password. An archive PDF cannot be (PDF/A does not "
                           + "allow it): it is written without a password.")
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
    // Annotations of another app in the PDF: make them editable? (qt/docs/adopt-annotations.md; asked once per file)
    AdaptiveDialog {
        id: adoptDialog
        objectName: "adoptDialog"
        kind: "question"
        property int count: 0
        property string appName: ""
        property bool offered: false
        function openFor(n, appName, offered) {
            adoptDialog.count = n
            adoptDialog.appName = appName
            adoptDialog.offered = offered
            open()
        }
        preferredWidth: 480
        title: qsTr("Annotations from another app")
        Label {
            width: adoptDialog.availableWidth
            wrapMode: Text.Wrap
            text: (adoptDialog.appName !== ""
                   ? qsTr("This PDF has %n annotation(s) from %1.", "", adoptDialog.count).arg(adoptDialog.appName)
                   : qsTr("This PDF has %n annotation(s) from another app.", "", adoptDialog.count))
                  + " " + qsTr("Make them editable?") + "\n\n"
                  + qsTr("Ink, highlights, text boxes, shapes and pictures go into a layer of their own on each page, "
                         + "notes become sticky notes. Their originals leave the PDF when it is saved, replaced by "
                         + "these. Undo brings them back.")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "adoptNotNowButton"
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "adoptMakeEditableButton"
                text: qsTr("Make editable")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.adoptAnnotations()
        onRejected: if (offered) app.declineAdoption()
    }
    Connections {
        target: app
        function onAnnotationsToAdopt(count, appName, file) {
            adoptDialog.openFor(count, appName, true)
        }
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
    // The first start shows the introduction (qt/docs/onboarding.md), which ends in the question which way to keep
    // documents (PDF files or Xournal++ files); the question alone when the introduction was shown already but no way
    // was chosen. Then the recovery question; then (Android) where the libraries are kept.
    function afterFirstStart() {
        if (app.recoveryItems.length > 0) recoveryDialog.open()
        else homeView.offerLibrariesHomeAtStart()
    }
    DocumentModeDialog {
        id: documentModeDialog
        onChosen: win.afterFirstStart()
    }
    IntroDialog {
        id: introDialog
        onChosen: win.afterFirstStart()
    }
    // Help → Start the tutorial again: a fresh copy replaces the one written on (qt/docs/onboarding.md)
    AdaptiveDialog {
        id: restartTutorialDialog
        objectName: "restartTutorialDialog"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Start the tutorial again?")
        Label {
            width: restartTutorialDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("A fresh copy of the tutorial replaces yours; what you wrote on it is gone. To keep it, save it "
                       + "somewhere else first (⋮ → Save as…).")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "restartTutorialConfirm"
                text: qsTr("Start again")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: app.restartTutorial()
    }
    Component.onCompleted: {
        if (app.askIntro()) introDialog.openFirstStart()
        else if (app.askDocumentMode()) documentModeDialog.open()
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
        visible: win.chromeMode === "compact" && !win.zenShown && !app.presenting && !app.homeVisible
                 && app.tabs.count > 1 && !searchBar.visible
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
        visible: opacity > 0 && win.chromeMode === "compact" && !win.zenShown
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
    // --- Zen (qt/docs/zen.md) ----------------------------------------------------------------------------------------
    // Zen's only mark: a small faint dot in the lower left corner of the page (10 px; faint after 2 s, clearer while the
    // mouse or the pen is near), a finger-wide target, clear of the safe area. A tap opens its pill beside it.
    // (Presenting with the controls has none: Ctrl+F5 or the floating toolbox's ⋯ hide them.)
    AbstractButton {
        id: zenDot
        objectName: "zenDot"
        visible: win.zenShown && !pageGrid.visible && !contentsOverview.visible
        z: 91
        x: win.canvasControlsLeft
        y: win.canvasControlsBottom - height
        width: 48
        height: 48
        focusPolicy: Qt.NoFocus  // (the keys stay with the page)
        hoverEnabled: true
        Accessible.name: qsTr("Show controls, read only, the page")
        ToolTip.visible: hovered && !pressed && !zenPill.visible
        ToolTip.text: qsTr("Show controls, read only, the page")
        ToolTip.delay: 600
        /// 2 s after it showed, or after the pointer went away from it: faint
        property bool resting: false
        /// The mouse or the pen near it (or its pill open): clearer
        readonly property bool near: hovered || zenNear.hovered || pressed || zenPill.visible
        function wake() {
            resting = false
            restTimer.restart()
        }
        onVisibleChanged: if (visible) wake()
        onNearChanged: if (!near) wake()
        Timer { id: restTimer; interval: 2000; onTriggered: zenDot.resting = true }
        background: null
        contentItem: Item {
            Rectangle {
                objectName: "zenDotMark"
                width: 10
                height: 10
                radius: 5
                x: 13 - width / 2
                y: parent.height - 13 - height / 2
                // grey with a light rim: as faint on a white page as on the dark around it
                color: "#80868b"
                border.width: 1
                border.color: "#99ffffff"
                opacity: zenDot.near ? 0.9 : zenDot.resting ? 0.2 : 0.6
                Behavior on opacity { NumberAnimation { duration: 250 } }
            }
        }
        onClicked: zenPill.opened = !zenPill.opened
    }
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
    // The dot's pill, over the page beside the dot (the page does not move): Show controls (leaves Zen), Read only,
    // the page number (a tap: all pages, to go to one), fit the width / the whole page
    Pane {
        id: zenPill
        objectName: "zenPill"
        property bool opened: false
        visible: opened && zenDot.visible
        onVisibleChanged: if (!visible) opened = false
        z: 92
        x: Math.min(zenDot.x + zenDot.width - 6, win.canvasControlsRight - width - 8)
        y: Math.max(win.canvasControlsTop + 8, zenDot.y + zenDot.height - height - 4)
        padding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: 14
            color: "#f7fafafa"
            border.width: 1
            border.color: "#33000000"
        }
        ColumnLayout {
            spacing: 0
            ToolButton {
                objectName: "zenShowControls"
                Layout.fillWidth: true
                implicitHeight: Math.max(44, win.adaptive.minTarget)
                text: qsTr("Show controls")
                icon.source: app.iconUrl("xqt-eye")
                icon.width: 22
                icon.height: 22
                display: AbstractButton.TextBesideIcon
                focusPolicy: Qt.NoFocus
                onClicked: {
                    zenPill.opened = false
                    win.readStarted = false  // (Read broken up: read only and full screen stay)
                    win.setZen(false)
                }
            }
            Switch {
                id: zenReadOnly
                objectName: "zenReadOnly"
                visible: win.readOnlyOffered
                Layout.fillWidth: true
                implicitHeight: Math.max(44, win.adaptive.minTarget)
                text: qsTr("Read only")
                focusPolicy: Qt.NoFocus
                checked: win.readOnlyOn
                onToggled: {
                    win.readOnly = checked
                    checked = Qt.binding(function() { return win.readOnlyOn })
                }
            }
            RowLayout {
                spacing: 2
                ToolButton {
                    objectName: "zenPage"
                    Layout.fillWidth: true
                    implicitHeight: Math.max(44, win.adaptive.minTarget)
                    text: app.pageNumber + " / " + app.pageCount
                    font.pixelSize: 14
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Go to a page")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go to a page (all pages)")
                    ToolTip.delay: 600
                    onClicked: {
                        zenPill.opened = false
                        pageGrid.open()
                    }
                }
                IconButton {
                    objectName: "zenFitWidth"
                    iconName: "xqt-fit-width"
                    implicitWidth: Math.max(44, win.adaptive.minTarget)
                    implicitHeight: implicitWidth
                    tip: qsTr("Fit the width")
                    onClicked: {
                        zenPill.opened = false
                        app.fitWidth()
                    }
                }
                IconButton {
                    objectName: "zenFitPage"
                    iconName: "xqt-page-single"
                    implicitWidth: Math.max(44, win.adaptive.minTarget)
                    implicitHeight: implicitWidth
                    tip: qsTr("The whole page")
                    onClicked: {
                        zenPill.opened = false
                        app.fitPage()
                    }
                }
            }
        }
    }
    // Reading (qt/docs/zen.md): read only, anywhere. Big fields at the left and right edges (a fifth of the page's width each, at least a finger wide, its
    // whole height, invisible) turn the pages: the previous or the next one (its top; presenting: the slide). A short
    // arrow at that edge says the tap was taken. The page itself finds the taps (DocumentCanvas.edgeTapWidth,
    // edgeTapped: a tap that is no link and no note), so a swipe there scrolls as anywhere; these items only show
    // where the fields are and the hint (inputTransparent: the canvas takes the presses under them).
    Item {
        id: readingTapFields
        objectName: "readingTapFields"
        readonly property bool inputTransparent: true
        visible: win.reading && !win.replaying && !pageGrid.visible && !contentsOverview.visible
        z: 4  // (over the page, under its pills)
        x: canvas.x
        y: canvas.y
        width: canvas.width
        height: canvas.height
        readonly property real fieldWidth: Math.round(Math.max(48, width * 0.2))
        function turn(step) {
            if (step < 0) {
                app.previousPage()
                previousFieldHint.flash()
            } else {
                app.nextPage()
                nextFieldHint.flash()
            }
        }
        component FieldHint: Rectangle {
            id: hint
            readonly property bool inputTransparent: true
            property bool next: true
            function flash() { flashAnimation.restart() }
            anchors.verticalCenter: parent.verticalCenter
            x: next ? parent.width - width - 12 : 12
            width: 48
            height: 48
            radius: 24
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
            opacity: 0
            Image {
                anchors.centerIn: parent
                source: app.iconUrl(parent.next ? "xqt-chevron-right" : "xqt-chevron-left")
                sourceSize.width: 26
                sourceSize.height: 26
            }
            SequentialAnimation {
                id: flashAnimation
                NumberAnimation { target: hint; property: "opacity"; to: 0.9; duration: 90 }
                PauseAnimation { duration: 160 }
                NumberAnimation { target: hint; property: "opacity"; to: 0; duration: 350 }
            }
        }
        Item {
            objectName: "readingPreviousField"
            readonly property bool inputTransparent: true
            width: readingTapFields.fieldWidth
            height: parent.height
            FieldHint { id: previousFieldHint; objectName: "readingPreviousHint"; next: false }
        }
        Item {
            objectName: "readingNextField"
            readonly property bool inputTransparent: true
            x: parent.width - width
            width: readingTapFields.fieldWidth
            height: parent.height
            FieldHint { id: nextFieldHint; objectName: "readingNextHint"; next: true }
        }
    }
    // Read only: the first stroke tried says so, once until read only is turned off, at the pen; it fades
    Rectangle {
        id: readOnlyNote
        objectName: "readOnlyNote"
        readonly property bool inputTransparent: true
        /// Said in this read-only time (and how often it was said, for the tests)
        property bool told: false
        property int toldCount: 0
        property point at: Qt.point(0, 0)
        visible: opacity > 0
        opacity: 0
        z: 93
        x: Math.round(Math.max(win.canvasControlsLeft + 8,
                               Math.min(canvas.x + at.x - width / 2, win.canvasControlsRight - width - 8)))
        y: Math.round(Math.max(win.canvasControlsTop + 8,
                               Math.min(canvas.y + at.y - height - 24, win.canvasControlsBottom - height - 8)))
        width: Math.min(readOnlyNoteText.implicitWidth + 28, win.canvasControlsRight - win.canvasControlsLeft - 16)
        height: readOnlyNoteText.implicitHeight + 14
        radius: Math.min(16, height / 2)
        color: "#e6303134"
        Label {
            id: readOnlyNoteText
            objectName: "readOnlyNoteText"
            readonly property bool inputTransparent: true
            anchors.centerIn: parent
            width: Math.min(implicitWidth, readOnlyNote.width - 28)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: win.zenShown ? qsTr("Read only — tap the dot to write")
                  : win.chromeMode === "compact" ? qsTr("Read only — ⋯ → Read only to write")
                  : qsTr("Read only — ⋮ → View → Read only to write")
            color: "#ffffff"
            font.pixelSize: 14
        }
        function tell(pos) {
            if (told) return
            told = true
            ++toldCount
            at = pos
            noteFade.restart()
        }
        SequentialAnimation {
            id: noteFade
            NumberAnimation { target: readOnlyNote; property: "opacity"; to: 0.95; duration: 120 }
            PauseAnimation { duration: 2400 }
            NumberAnimation { target: readOnlyNote; property: "opacity"; to: 0; duration: 700 }
        }
        Connections {
            target: win
            function onReadOnlyOnChanged() { if (!win.readOnlyOn) readOnlyNote.told = false }
        }
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
    Shortcut { sequences: win.keysOf("readOnly"); enabled: !app.homeVisible && !win.textDoc && !win.replaying; onActivated: win.toggleReading() }
    Shortcut { sequences: win.keysOf("zen"); enabled: !app.homeVisible; onActivated: win.setZen(!win.zenShown) }
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
        target: app.versions
        // A version was restored (the History panel): one undo step
        function onRestored(ok, text) {
            if (ok) {
                snackbar.show(text, true)
            } else {
                messageDialog.title = qsTr("The version could not be restored")
                messageDialog.text = text
                messageDialog.open()
            }
        }
    }
    // "Save with a message…" (Ctrl+Alt+S, a milestone of the version history), or a version's message changed later
    AdaptiveDialog {
        id: versionMessageDialog
        objectName: "versionMessageDialog"
        kind: "question"
        /// -1: the next save; else the version whose message it is
        property int versionId: -1
        function openFor(id) {
            versionId = id
            messageField.text = id >= 0 ? app.versions.messageOf(id) : ""
            keepVersionsBox.checked = true
            open()
            messageField.forceActiveFocus()
        }
        preferredWidth: 420
        title: versionId >= 0 ? qsTr("The message of the version of %1").arg(app.versions.titleOf(versionId))
                              : qsTr("Save with a message")
        ColumnLayout {
            width: versionMessageDialog.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: versionMessageDialog.versionId < 0
                text: qsTr("A version with a message is a milestone: it is kept as it is, whatever is saved later that day.")
            }
            TextField {
                id: messageField
                objectName: "versionMessageField"
                Layout.fillWidth: true
                placeholderText: qsTr("What did you do?")
                maximumLength: 200
                onAccepted: versionMessageDialog.accept()
            }
            CheckBox {
                id: keepVersionsBox
                objectName: "versionMessageKeepVersions"
                visible: versionMessageDialog.versionId < 0 && !app.versions.on
                text: qsTr("Keep versions of this document")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: versionMessageDialog.versionId < 0 && !app.versions.available
                text: app.versions.unavailableReason
                color: "#b06000"
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "versionMessageSave"
                text: versionMessageDialog.versionId >= 0 ? qsTr("Change") : qsTr("Save")
                enabled: versionMessageDialog.versionId >= 0 || app.versions.available
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            if (versionId >= 0) {
                app.setVersionMessage(versionId, messageField.text)
                return
            }
            if (keepVersionsBox.visible && keepVersionsBox.checked) app.versions.on = true
            if (!app.saveWithMessage(messageField.text, null)) saveOrAsk(null)
        }
    }
    Connections {
        target: app
        function onPageActionDone(text, undoable) { snackbar.show(text, undoable) }
        // Copy handwriting as text (qt/copy-tools): the text near the words; why not, with the way to Settings
        function onInkTextCopy(result) {
            if (result.state === "reading" || result.state === "copied") {
                inkTextToast.show(result)
                return
            }
            inkTextToast.close()
            if (result.state === "nothing") {
                snackbar.show(qsTr("No handwriting there to copy"), false)
            } else {
                snackbar.show(result.state === "off"
                              ? qsTr("Copying handwriting as text needs the handwriting search (Settings → Search)")
                              : qsTr("No handwriting model to read it with (Settings → Search)"),
                              false, qsTr("Settings"), function() { settingsPage.open(); settingsPage.showSearch() })
            }
        }
        // A snip pasted from a document with a file (qt/docs/snip.md): a link to its page, if wanted
        function onSnipLinkOffered(title) {
            snackbar.show(qsTr("Add a link to the source page (%1)?").arg(title), false, qsTr("Add link"),
                          function() { app.addSnipLink() })
        }
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

    // Putting the command bar away and getting it back: a small tab in the middle of its edge towards the pages (⋮
    // keeps the end of the bar), and a slim strip while it is away. A finger gets a target of minTarget around the tab.
    Item {
        objectName: "toolbarToggle"
        visible: !app.homeVisible && win.fullChrome && !app.toolbarHidden && !win.toolsInFormatBar && !win.phoneChrome
                 && !win.replaying
        z: 58
        /// The target: a finger's size in the touch profile, reaching into the pages (not over the bar's buttons)
        readonly property real grip: win.adaptive.touchProfile ? win.adaptive.minTarget : 18
        width: 42
        height: grip
        // Half over the bar's edge, the rest into the pages
        x: Math.round((parent.width - width) / 2)
        y: -9
        Rectangle {
            width: 42
            height: 18
            radius: 6
            color: "#ffffff"
            border.width: 1
            border.color: "#d5d8dc"
            Image {  // up, towards the bar it puts away
                anchors.centerIn: parent
                source: app.iconUrl("xqt-chevron-up")
                sourceSize.width: 15
                sourceSize.height: 15
            }
        }
        TapHandler { onTapped: app.toolbarHidden = true }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: qsTr("Hide the tool bar")
        ToolTip.delay: 600
        HoverHandler { id: hoverHandler }
    }
    // While it is away: a slim strip at the top edge brings it back
    Rectangle {
        id: toolbarShow
        objectName: "toolbarShow"
        visible: !app.homeVisible && win.fullChrome && app.toolbarHidden && !win.phoneChrome && !win.replaying
        z: 60
        width: 96
        height: 16
        x: Math.round((parent.width - width) / 2)
        y: win.controlsTop
        radius: 8
        color: "#f1f3f4"
        border.width: 1
        border.color: "#d5d8dc"
        opacity: showHover.hovered ? 1 : 0.75
        Image {  // where the bar comes in from: down from the top
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-down")
            sourceSize.width: 15
            sourceSize.height: 15
        }
        TapHandler { onTapped: app.toolbarHidden = false }
        HoverHandler { id: showHover }
        ToolTip.visible: showHover.hovered
        ToolTip.text: qsTr("Show the tool bar")
        ToolTip.delay: 600
    }

    // The setsquare / compass: what it can do, and putting it aside for a moment
    GeometryPill {
        id: geometryPill
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.topMargin: 12 + win.canvasControlsTop - canvas.y
        anchors.rightMargin: 20 + canvas.x + canvas.width - win.canvasControlsRight
        z: 57
    }
    // The curtain: its handles, taking it away (below the setsquare's pill when that is out too)
    // A recording runs, a recording plays (qt/docs/audio.md): at the top of the canvas, in the middle
    // (below the toolbox where it floats at the top, in the middle too: full screen, presenting)
    readonly property real audioPillsTop: toolboxPane.visible && toolboxPane.floating && toolboxPane.edge === "top"
                                          ? Math.max(canvasControlsTop, toolboxPane.y + toolboxPane.height - 4)
                                          : canvasControlsTop
    RecordingPill {
        id: recordingPill
        anchors.top: canvas.top
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.topMargin: 12 + win.audioPillsTop - canvas.y
        z: 57
    }
    PlaybackPill {
        anchors.top: recordingPill.visible ? recordingPill.bottom : canvas.top
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.topMargin: recordingPill.visible ? 8 : 12 + win.audioPillsTop - canvas.y
        z: 57
    }
    Connections {
        target: app.audio
        function onMessage(text) { snackbar.show(text, false) }
    }
    // The microphone refused by the system (macOS, Android): where to allow it
    MicrophoneDialog {}
    // The replay of the timeline (qt/docs/timeline.md): its play bar at the bottom of the page, above the navigation
    // bar and clear of a cut-out (the safe area), off the side edges (where Android's back gesture starts); the view
    // pill, the tools and the phone's dock are put away meanwhile (hudHidden, dockShown)
    TimelineBar {
        id: timelineBar
        touch: win.adaptive.touchProfile
        readonly property real side: win.phoneLayout ? 12 : 16
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: (win.phoneLayout ? 10 : 16) + canvas.y + canvas.height - win.canvasControlsBottom
        x: Math.round((win.canvasControlsLeft + win.canvasControlsRight - width) / 2)
        width: Math.min(win.canvasControlsRight - win.canvasControlsLeft - 2 * side, 960)
        z: 92
    }
    Connections {
        target: app.timeline
        function onMessage(text) { snackbar.show(text, false) }
    }
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
    CurtainPill {
        anchors.top: geometryPill.visible ? geometryPill.bottom : canvas.top
        anchors.right: canvas.right
        anchors.topMargin: geometryPill.visible ? 8 : 12 + win.canvasControlsTop - canvas.y
        anchors.rightMargin: 20 + canvas.x + canvas.width - win.canvasControlsRight
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
        onIntroRequested: introDialog.show()
        onTutorialRequested: app.openTutorial()
        onRestartTutorialRequested: restartTutorialDialog.open()
    }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
        onCloseAllRequested: app.tabs.count > 1 ? closeAllDialog.open() : closeAllTabs()
        onLibrarySearchRequested: win.searchLibrary()
    }

    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible && !app.textFlowActive && !app.markdownActive && !replaying
    // The keys come from the shortcut settings (app.shortcuts); reading its revision keeps the bindings fresh.
    function keysOf(id) { return (app.shortcuts.revision, app.shortcuts.keys(id)) }
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
    ShortcutSheet {
        id: shortcutSheet
        onChangeRequested: { settingsPage.open(); settingsPage.showShortcuts() }
    }
}
