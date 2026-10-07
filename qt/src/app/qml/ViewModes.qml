// xournal-qt: the window's view modes (Main.qml's state, `win.modes`; qt/docs/zen.md, qt/docs/adaptive-layout.md):
// the chrome (full or compact), full screen and the window's state, presenting, Zen, read only and Read, the replay.
// The window's root keeps what the tests read and write as aliases and forwarders (fullScreenMode, zen, setZen, …).
import QtQuick
import QtQuick.Window

Item {
    id: modes
    visible: false

    // The chrome: "full" (tab strip, command bar, the docked toolbox, sidebar; in the phone classes the app bar and the
    // tool dock) or "compact" (full screen's: the tab dots, the floating toolbox, the view pill). Full screen (F11,
    // fullScreenMode) is the compact chrome in a full-screen window. Apart from it: the window's state
    // (windowFullScreen), presenting (black, page by page), Zen (nothing around the page) and read only.
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
    readonly property bool zenAuto: win.adaptive.layoutClass === "tiny" && win.layoutChoice("zen") !== "off"
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
        if (win.adaptive.layoutClass === "tiny") {
            // (left by hand: remembered for tiny windows; on again: automatic again)
            win.chooseLayout("zen", on ? "" : "off")
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
    readonly property bool readOnlyOffered: !app.homeVisible && !win.textDoc && !replaying
    onReadOnlyOfferedChanged: if (!readOnlyOffered) readOnly = false
    /// Read only is on (DocumentCanvas.readingOnly; the edges turn the pages, readingTapFields)
    readonly property bool readOnlyOn: readOnly && readOnlyOffered
    /// Read (Ctrl+Alt+R, ⋮ → View → Read): Zen and read only, in full screen (a tiny window stays a window). The keys
    /// again end it: read only off, and Zen and full screen where Read turned them on; Esc leaves Zen too. "Show
    /// controls" leaves only Zen.
    property bool readStarted: false
    property bool readEnteredFullScreen: false
    property bool readEnteredZen: false
    function startReading() {
        if (!readOnlyOffered) return
        readEnteredFullScreen = !fullScreenMode && win.adaptive.layoutClass !== "tiny"
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
    /// The document's timeline is replayed (qt/docs/timeline.md): the page as of a moment and the play bar at the
    /// bottom, read-only; no tools (as reading), the play bar's keys. The command bar and the phone's dock are put away
    /// meanwhile.
    readonly property bool replaying: app.timeline.active

    // --- full screen and the window's state ----------------------------------------------------------------------------
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
    Connections {
        target: win
        function onVisibilityChanged() {
            const v = win.visibility
            if (modes.leavingFullScreen) {
                // The way back may pass through other states (the timer ends this). A compositor may also give back
                // the size from before it was maximized when full screen ends, even after it reported maximized: then
                // ask for maximized once more.
                if (v === Window.Windowed && modes.windowedVisibility === Window.Maximized && !modes.remaximized) {
                    modes.remaximized = true
                    app.logWindow("maximized again (the compositor gave back the normal size)")
                    win.showMaximized()
                }
            } else if (!modes.windowFullScreen && (v === Window.Windowed || v === Window.Maximized)) {
                modes.windowedVisibility = v
            }
        }
    }
    Timer {  // ends the way back from full screen (a compositor may take a moment and several steps)
        id: leavingFullScreenTimer
        interval: 1500
        onTriggered: {
            modes.leavingFullScreen = false
            // settled: the state it is in now is the window's state
            if (!modes.windowFullScreen && (win.visibility === Window.Windowed || win.visibility === Window.Maximized))
                modes.windowedVisibility = win.visibility
        }
    }
    onWindowFullScreenChanged: {
        if (windowFullScreen) {
            leavingFullScreenTimer.stop()
            leavingFullScreen = false
            app.logWindow("full screen")
            win.showFullScreen()
        } else {
            leavingFullScreen = true
            remaximized = false
            leavingFullScreenTimer.restart()
            app.logWindow("leave full screen to " + (windowedVisibility === Window.Maximized ? "maximized" : "normal"))
            if (windowedVisibility === Window.Maximized) win.showMaximized()
            else win.showNormal()
        }
    }

    // --- presenting (qt/docs/presenter-view.md) ------------------------------------------------------------------------
    /// Present from the current page: full screen, a page fills it. `clean`: without controls (below). With two
    /// screens this window becomes the presenter's console on one of them (off the audience's screen first) and the
    /// audience's window shows the slide on the other.
    function startPresenting(clean) {
        if (app.homeVisible) return
        presentClean = clean === true
        if (app.presenter.available) app.presenter.placeConsole(win)
        fullScreenMode = true
        app.presenting = true
    }
    /// Presenting without controls (Ctrl+F5, or holding the presentation button): presenting in Zen - only the page
    /// shows, no floating toolbox, no other overlay; the Zen dot in the lower left corner (its "Show controls") or
    /// Ctrl+F5 brings them back, Ctrl+F5 hides them again. Every presentation starts as it is asked for: F5 with the
    /// controls (and no dot).
    property bool presentClean: false
    /// Presenting with two screens: this window is the console (the page with its space for notes, the panel with
    /// the clock, the time, the next page), the audience's window shows the slide
    readonly property bool presenterConsole: app.presenting && app.presenter.active
    /// The console's panel at the right (the controls over the page stay left of it)
    readonly property real presenterPanelWidth: presenterConsole ? Math.round(Math.min(480, Math.max(280, win.width * 0.3))) : 0

    Connections {
        target: app
        function onHomeVisibleChanged() {
            if (!app.homeVisible) return
            modes.fullScreenMode = false
            modes.zenByHand = false
            modes.readStarted = false
        }
        function onPresentingChanged() { if (!app.presenting) modes.presentClean = false }
    }
}
