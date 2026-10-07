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
    /// navigation bar below its content (MenuSheet, the page menu, the palette and the widths)
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
    /// The sidebar's History panel (version history)
    function showHistory() {
        sidebar.mode = "history"
        showSidebar(true)
    }
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

    // --- the source panel: the Markdown source beside or below the page --------------------------------------------
    // (qt/docs/adaptive-layout.md, "Panels")
    /// The panel open now, or null
    readonly property Item sourcePanel: markdownPanel.visible ? markdownPanel : null
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
    // (qt/docs/zen.md).
    readonly property string chromeMode: fullScreenMode ? "compact" : "full"
    /// The full chrome is shown (not in Zen)
    readonly property bool fullChrome: chromeMode === "full" && !zenShown
    /// Nothing over the page but the page: Zen (presenting without controls too), or the replay
    readonly property bool hudHidden: zenShown || (replaying && !app.homeVisible)

    // --- Zen and read only (qt/docs/zen.md) ---------------------------------------------------------------------------
    /// Zen turned on by hand (⋮ → View → Zen, its keys, the command bar's button, Read)
    property bool zenByHand: false
    /// Zen of itself: a tiny window (under 360 px either way: split screen, a pop-up window), unless it was left there
    /// by hand (remembered for the class: layout/tiny/zen "off")
    readonly property bool zenAuto: adaptive.layoutClass === "tiny" && layoutChoice("zen") !== "off"
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
    // One row at the top: the commands (the tools are in the toolbox).
    /// A text document's command bar is merged into its format bar: one row, ⋮ at its end (F7.2)
    readonly property bool toolsInFormatBar: textDoc && formatBar.shown && fullChrome && !app.toolbarHidden && !phoneChrome
    /// The cycling buttons' groups (ToolGroups.qml): the fixed tools of the toolbox (select, snip, setsquare, text)
    readonly property ToolGroups toolGroups: ToolGroups {}
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
    /// The first key of an action as a note for a label or a sentence (" (Ctrl+P)"; "" when it has none):
    /// qsTr("Print…") + win.keyNote("print"). Labels name the keys as they are set, never keys written into the text.
    function keyNote(id) {
        const keys = keysOf(id)
        return keys.length > 0 ? " (" + keys[0] + ")" : ""
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
            shareFlow.shareDialog.openFor("")
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
        onEditRequested: function(entry, button) { toolboxMenus.toolEditor.openFor(entry, button, edge) }
        onMenuRequested: function(entry, button, pos) { toolboxMenus.toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolboxMenus.toolTypeMenu.ask("add", "", button, "rail") }
        onMoreRequested: function(button) { toolboxMenus.toolboxMoreMenu.openMenu(undefined, button) }
        onGripMoved: function(pos) { win.toolboxEdgeTarget = win.edgeAt(pos) }
        // (a tool carried onto another and held there: a group; the snackbar can take it back)
        onGrouped: function(groupId, before) { toolboxMenus.grouped(before) }
        onRemoved: function(entry, before) { toolboxMenus.leftTheBars(entry, before) }
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
    ToolboxMenus { id: toolboxMenus }
    /// An entry's name for people ("Pen · Body", "Arrow", "Eraser (whiteout)")
    function toolEntryName(entry) { return toolboxPane.entryName(entry) }
    AppButtons { id: toolArea }
    // ⋮: pinned at the very end of the top bar (a text document: of its format bar; the phone chrome: of the app bar)
    MoreMenu {
        id: toolEnd
        parent: win.phoneChrome ? phoneAppBar.moreSlot : win.toolsInFormatBar ? formatBar.trailing : topBarPane.trailingTail
        y: win.toolsInFormatBar && !win.phoneChrome ? -2 : 0
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
        onEditRequested: function(entry, button) { toolboxMenus.toolEditor.openFor(entry, button, "top") }
        onMenuRequested: function(entry, button, pos) { toolboxMenus.toolEntryMenu.openFor(entry, button, pos) }
        onAddRequested: function(button) { toolboxMenus.toolTypeMenu.ask("add", "", button, "top") }
        onGrouped: function(groupId, before) { toolboxMenus.grouped(before) }
        onRemoved: function(entry, before) { toolboxMenus.leftTheBars(entry, before) }
    }
    /// Popups open that close on Android's back key (sheets, dialogs, the editor, the pickers): Zen's Back waits for
    /// them (two shortcuts of the same key would be ambiguous: neither would act)
    property int backTakers: 0
    function takeBack(on) { backTakers = Math.max(0, backTakers + (on ? 1 : -1)) }
    /// The top bar is shown (at the top, in a text document's format bar, in a phone's app bar)
    readonly property bool topBarShown: !app.homeVisible && !noToolbar && !replaying

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

    ShownFileNote { id: shownFileNote }

    ViewPill { id: viewPill }

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
        onZoomRequested: Popups.openAt(viewPill.fitMenu)
    }

    TabDragHint {}
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

    NavPill { id: navPill }

    LinkPopup { id: linkPopup }

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
        anchors.bottomMargin: 96 + canvas.y + canvas.height - win.canvasControlsBottom
    }
    VersionMessageDialog { id: versionMessageDialog }

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
