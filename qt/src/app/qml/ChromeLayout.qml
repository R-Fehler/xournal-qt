// xournal-qt: where the window's chrome goes (Main.qml's state, `win.layout`; qt/docs/adaptive-layout.md,
// qt/docs/toolbox.md): the page sidebar (beside the page or a drawer), the controls' room over the canvas, the source
// panel, the phone chrome (app bar and dock), the command bar and the toolbox's edge. It reads the window's items it
// lays out (canvas, sideTools, toolboxRow, …) through Main's context. The window's root keeps what the tests read as
// aliases and forwarders (sidebarShown, showSidebar, toolboxEdge, chooseToolboxEdge, …).
import QtQuick

Item {
    id: layout
    visible: false

    // --- the page sidebar ------------------------------------------------------------------------------------------
    // Beside the page when there is room for it (the page keeps ~900 px; not in portrait or on a phone), unless it was
    // hidden or shown by hand in this size class. Without room, its button opens it as a drawer over the page, which
    // closes when a page is picked or the page is tapped.
    readonly property string sidebarChoice: win.layoutChoice("sidebar")
    readonly property bool sidebarDocked: sidebarChoice === "shown" || (sidebarChoice === "" && win.adaptive.roomForSidebar)
    property bool sidebarDrawerOpen: false
    readonly property bool sidebarShown: sidebarDocked || sidebarDrawerOpen
    onSidebarDockedChanged: closeDrawerNow()
    readonly property string layoutClass: win.adaptive.layoutClass
    onLayoutClassChanged: closeDrawerNow()  // (a drawer is for the moment, in the size it was opened in)
    /// The drawer slides in from the left and out again (0: out of sight, 1: in place); beside the page there is no
    /// slide. Only a tap (the arrow, the dimmed page, a page picked, Esc, the back key) slides it; a change of the size
    /// class takes it away at once.
    property real drawerSlide: 0
    NumberAnimation {
        id: drawerSlideAnimation
        target: layout
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
    readonly property real drawerWidth: !win.adaptive.phone ? 210
                                        : layoutClass === "phoneShort" ? 260 : Math.min(360, Math.round(win.width * 0.85))
    /// The Pages button: hides the sidebar (remembered for this size class), or shows it again - beside the page where
    /// there is room, else as a drawer (for the moment, not remembered)
    function showSidebar(shown) {
        if (shown) {
            if (sidebarDocked) return
            if (win.adaptive.roomForSidebar) {
                win.chooseLayout("sidebar", "")  // (it was hidden by hand: automatic again)
            } else {
                sidebarDrawerOpen = true
                slideDrawer(true)
            }
        } else {
            if (sidebarDrawerOpen) slideDrawer(false)
            sidebarDrawerOpen = false
            if (sidebarDocked) win.chooseLayout("sidebar", win.adaptive.roomForSidebar ? "hidden" : "")
        }
    }
    /// The drawer's pin: keep the sidebar beside the page in this size class, although room is short
    function dockSidebar() {
        closeDrawerNow()
        win.chooseLayout("sidebar", win.adaptive.roomForSidebar ? "" : "shown")
    }

    // --- the controls over the canvas --------------------------------------------------------------------------------
    /// The bottom of the canvas where controls may go over it (above the navigation bar and the keyboard), and its
    /// sides (clear of a cut-out): the pills over the page keep inside them
    readonly property real canvasControlsBottom: Math.min(canvas.y + canvas.height, win.insets.controlsBottom)
    readonly property real canvasControlsLeft: Math.max(canvas.x, win.insets.controlsLeft)
    readonly property real canvasControlsRight: Math.min(canvas.x + canvas.width, win.insets.controlsRight)
    readonly property real canvasControlsTop: Math.max(canvas.y, win.insets.controlsTop)
    /// A pill at the canvas's bottom edge (`lowY`: where it would sit) goes above the pills it would meet there
    /// (`others`, from the bottom up): the view pill keeps the lower right corner
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
    /// The recording and playback pills: at the top of the canvas, in the middle (below the toolbox where it floats at
    /// the top, in the middle too: full screen, presenting)
    readonly property real audioPillsTop: toolboxPane.visible && toolboxPane.floating && toolboxPane.edge === "top"
                                          ? Math.max(canvasControlsTop, toolboxPane.y + toolboxPane.height - 4)
                                          : canvasControlsTop

    // --- the source panel: the Markdown source beside or below the page (qt/docs/adaptive-layout.md, "Panels") -------
    /// The panel open now, or null
    readonly property Item sourcePanel: markdownPanel.visible ? markdownPanel : null
    /// Below the page (the page above, its source below, a divider between them) in a portrait tablet or phone, and in
    /// any portrait window too narrow for a panel beside the page; else beside it, at the right (a desktop, narrow or
    /// wide, a phone held sideways). "Adapt the layout" off: beside it, as before.
    readonly property bool sourceAtBottom: {
        const c = win.adaptive.layoutClass
        if (c === "tabletPortrait" || c === "phonePortrait") return true
        if (c !== "desktopNarrow" && c !== "tiny") return false
        return win.contentItem.height > win.width - sideTools.width
    }
    /// Beside the page: 38 % of the window, 360 to 600 px, never more than half of it (a small window keeps its page)
    readonly property real sourceSideWidth: {
        const room = win.width - sideTools.width - (dockRail ? phoneDock.width : 0)
        return Math.round(Math.min(600, Math.max(Math.min(360, room * 0.5), win.width * 0.38)))
    }
    /// Below the page: the page's share of the height, dragged at the divider and remembered per size class
    /// (layout/<class>/sourceSplit); half on a tablet, 40 % on a phone (the source gets the bottom 60 %)
    readonly property real sourcePageShare: {
        const v = parseFloat(win.layoutChoice("sourceSplit"))
        return v >= 0.2 && v <= 0.8 ? v : (win.adaptive.phone ? 0.4 : 0.5)
    }
    /// While the divider is dragged: the share under the finger (-1: not dragged)
    property real sourceShareLive: -1
    readonly property real sourceBottomHeight: Math.round(win.contentItem.height
                                                          * (1 - (sourceShareLive >= 0 ? sourceShareLive : sourcePageShare)))

    // --- the phone chrome (qt/docs/adaptive-layout.md, "The phone chrome") -------------------------------------------
    /// A phone class (by the layout class: "Adapt the layout" off keeps the desktop layout at every size)
    readonly property bool phoneLayout: win.adaptive.phoneLayout
    /// The full chrome of a phone: the app bar at the top (the library, the title, the tab count, ⋮) instead of the tab
    /// strip, and the tool dock at the bottom instead of the command bar and the view pill
    readonly property bool phoneChrome: phoneLayout && win.modes.fullChrome
    /// The app bar: the phone chrome, and the home screen of a phone class in any chrome (the way back to the documents)
    readonly property bool appBarShown: phoneLayout && (win.modes.fullChrome || app.homeVisible)
    /// The dock is a rail at the right side where the window is held sideways (a phone in landscape, or a tiny window
    /// in landscape); in phone portrait always at the bottom
    readonly property bool dockVertical: win.adaptive.orientation === "landscape" && win.adaptive.layoutClass !== "phonePortrait"
    /// (not while the soft keyboard is open: the format bar takes its place above the keyboard)
    readonly property bool dockShown: phoneChrome && !app.homeVisible && !win.insets.keyboardOpen && !win.modes.replaying
    /// The dock beside the page (a rail): what sits at the window's right edge ends at it
    readonly property bool dockRail: dockShown && dockVertical

    // --- the command bar (qt/docs/toolbox.md, "The command bar") -----------------------------------------------------
    // One row at the top: the commands (the tools are in the toolbox).
    /// A text document's command bar is merged into its format bar: one row, ⋮ at its end; undo and redo lead it
    readonly property bool toolsInFormatBar: win.textDoc && formatBar.shown && win.modes.fullChrome && !app.toolbarHidden
                                             && !phoneChrome
    /// No command bar: in the compact chrome or Zen, or when it was put away. (The phone chrome has its dock
    /// instead, and nothing to put away.)
    readonly property bool noToolbar: !win.modes.fullChrome || (app.toolbarHidden && !phoneChrome)
    /// The top bar is shown (at the top, in a text document's format bar, in a phone's app bar)
    readonly property bool topBarShown: !app.homeVisible && !noToolbar && !win.modes.replaying
    /// Where undo and redo are, one place at a time (qt/docs/adaptive-layout.md, "One place for each action"):
    /// "toolbox" (its head, while it is shown), "formatBar" (a text document's format bar holding the commands),
    /// "toolBar" (leading the command bar while it is shown: a text document), "viewPill" (no bar shown: the compact
    /// chrome, Zen, the bar put away; in the phone chrome the dock has its own)
    readonly property string undoPlace: toolboxShown ? "toolbox"
                                      : toolsInFormatBar ? "formatBar"
                                      : !noToolbar && !phoneChrome ? "toolBar" : "viewPill"

    // --- the toolbox (qt/docs/toolbox.md) ----------------------------------------------------------------------------
    /// Its edge chosen by hand in this size class (⋮ → View → Toolbox position): "left", "right", "top", "bottom"
    readonly property string toolboxChoice: {
        const c = win.layoutChoice("toolbox")
        return ["left", "right", "top", "bottom"].indexOf(c) >= 0 ? c : ""
    }
    /// The automatic edge: the right (the author's choice: beside the page, out of the way of the writing hand's
    /// wrist for most and of the page sidebar at the left), a phone upright at the bottom
    readonly property string toolboxAuto: win.adaptive.layoutClass === "phonePortrait" ? "bottom" : "right"
    readonly property string toolboxEdge: toolboxChoice !== "" ? toolboxChoice : toolboxAuto
    readonly property bool toolboxVertical: toolboxEdge === "left" || toolboxEdge === "right"
    function chooseToolboxEdge(edge) { win.chooseLayout("toolbox", edge === toolboxAuto ? "" : edge) }
    /// Docked beside the page, taking its strip: the toolbox in the full chrome of a desktop or a tablet (a text
    /// document has no ink tools: its format bar holds undo and redo)
    readonly property bool toolboxDocked: win.modes.fullChrome && !phoneLayout && !app.homeVisible && !win.textDoc
                                          && !win.modes.replaying
    /// Floating over the page, a little off its edge: the compact chrome (full screen, presenting with the tools), on
    /// a phone too
    readonly property bool toolboxFloating: win.modes.chromeMode === "compact" && !app.homeVisible && !win.textDoc
                                            && !win.modes.hudHidden
    /// In the phone's dock (at the bottom, or the rail at the right held sideways): its first tools, "My tools"
    readonly property bool toolboxInDock: dockShown && !win.textDoc && !win.modes.replaying
    /// The toolbox is shown (docked, or floating in full screen and on phones)
    readonly property bool toolboxShown: toolboxDocked || toolboxFloating || toolboxInDock
    /// The strip it takes at the top or the bottom (0: none, or at a side)
    readonly property real toolboxTop: toolboxDocked && toolboxEdge === "top" ? toolboxRow.height : 0
    readonly property real toolboxBottom: toolboxDocked && toolboxEdge === "bottom" ? toolboxRow.height : 0
    /// The side the docked toolbox takes ("left", "right"; "": none, or at the top or the bottom)
    readonly property string sideEdge: toolboxDocked && toolboxVertical ? toolboxEdge : ""
    /// While the toolbox's grip is dragged: the edge it would go to ("": none)
    property string toolboxEdgeTarget: ""
    /// The edge of the window's content nearest to a point of the scene
    function edgeAt(scenePos) {
        const p = win.contentItem.mapFromItem(null, scenePos.x, scenePos.y)
        const w = win.contentItem.width, h = win.contentItem.height
        const d = { "left": p.x, "right": w - p.x, "top": p.y, "bottom": h - p.y }
        let best = "right"
        for (const k in d) if (d[k] < d[best]) best = k
        return best
    }
}
