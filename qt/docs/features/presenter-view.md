# The presenter view on a second screen

As in Okular and PowerPoint.

While presenting with two screens (a laptop and a projector), the audience's screen shows only the slide, full screen,
and the laptop's screen shows the **presenter's console**: presenting as it looks on one screen (the current page
fitted, page by page, with its space for notes, [note-space.md](note-space.md), and the toolbox,
[toolbox.md](toolbox.md), to write on the slide: the audience sees the ink as it is written), with a small **pill**
at the top left that holds the time since the start (folded: only the minutes), and, unfolded, the clock, the page
number, the next page and the console's settings. With one screen, presenting shows the slide in the one window.

The author (2026-10-11): "The presenter view when running at an external screen does not help a lot. It has the giant
empty bar on the right hand side etc. It should instead be just the normal single screen presenter view but with the
tools visible and a laser pointer part of the tool rail per default. A clock and timer can be an expandable pill with
just the running minutes showing when minimized. Make sure that navigating slides works well in presenter view, so
have a grid overview toggle to jump between pages easily." And: "make sure that in presenter view I can zoom in for
making detailed annotations. The full page view is only needed on the Beamer screen."

## Using it

- **F5** (or the presentation button, ⋮ → Present) with a second screen connected (the desktop extends to it, not
  mirrors it): the audience's window opens full screen on the other screen; this window becomes the console, full
  screen on its screen. Ctrl+F5 (without controls) does the same; the console's pill stays (the audience never sees
  it), only the toolbox and the other controls over the page hide.
- **The audience's screen**: the slide as large as the screen allows, black around it. The **space for notes is not
  shown there** (unless "Notes for the audience too" is on, below): the page beside or below the slide stays on the
  console. Strokes being written, the laser pointer, the curtain and the spotlight ([curtain.md](curtain.md)) show
  there at once, without the curtain's handles. While the page grid is open on the console the audience keeps the
  slide.
- **The console**: presenting as on one screen: the whole window, the page fitted to it with its space for notes, page
  by page, black around it; the floating toolbox at its edge (the right by default) with the **laser pointer** at hand
  (below); the page number for a moment after each page change; without controls the Zen dot ([zen.md](zen.md)). No
  panel beside the page.
- **The pill** (`presenterPill`), at the top left of the page (beside the toolbox where that floats at the left or the
  top):
  - **folded**: a clock and the whole **minutes** since the start ("0 min", "12 min", "1 h 05 min"; grey with a pause
    sign while the time is paused), and the button of **all pages** (the page grid);
  - a tap on the minutes **unfolds** it: the time of day (the locale's short format); the time since the start (m:ss,
    h:mm:ss after an hour) with pause/go on and back to 0:00 (it starts when the presentation starts; pausing keeps
    it, ending forgets it); "Page 3 of 20"; the next page smaller ("The last page" on the last one); a hint "The space
    for notes shows only here" on a page that has some; **Notes for the audience too**; **The audience follows my
    zoom** and **Fit**; which screen the audience's is, **Swap screens**, **End**;
  - it folds again on a tap beside it (on the page or the toolbox: that tap does nothing else, no ink), on a tap on the
    minutes, and on Esc or Back (their first step: "Esc and Back" in [zen.md](zen.md)).
- **Zooming in to write a detail** (pinch, Ctrl+wheel, Ctrl+plus): the console zooms, the audience keeps the whole
  slide (unless it follows the zoom, below) and sees the ink written there at once. Zoomed in, a drag, a fling, the
  wheel and a finger's swipe move **within the slide**, never on to the next one; the tools all work as usual. A double
  tap, Fit (the pill) and any page change bring back the whole page: the next slide always comes whole.
- **Notes for the audience too** (off by default): the audience sees the whole page, the slide with its space for
  notes, as large as the screen allows, and the ink written there as it is written. It takes effect at once while
  presenting. Pages without space for notes look the same either way.
- **The audience follows my zoom** (off by default): zoom in on the console's page and scroll around: the audience sees
  the same part of the page, as large as their screen allows. Their screen's shape differs from the console's, so they
  see what the presenter sees widened to it (never less), and at the slide's edge it stops at the edge (black beyond:
  not the space for notes, unless shown, and never the next page). A thin orange frame on the console shows exactly
  what they see, only while this is on and it is a part of the page (it can reach past the console's sides: they see
  more there). **Fit** in the pill, a double tap and a page change bring both screens back to the whole slide.
- **Going from page to page** works in **both windows** (a clicker or the keyboard sends keys to whichever window has
  the focus; a click on the projector's screen gives it the focus): Space, → ↓ Page Down on; ← ↑ Page Up Backspace
  back; Home, End; a page number and Enter (typed in the audience's window it is typed on in the console, which takes
  the focus, so the audience never sees it); a finger's swipe on the console at the fit; Escape (and F5) end it.
- **All pages** (the pill's grid button, **G** in either window, changeable in Settings → Shortcuts as "All pages while
  presenting"): the app's page grid over the console's page (`PageGrid.qml`, as in normal use); the audience keeps the
  current slide while it is open; a page tapped is presented on both screens and the grid closes; G again, its ×,
  Esc or Back close it (the presentation goes on). With one screen it is the window's own grid (G, and "All pages" in
  the floating toolbox's ⋯).
- **Which screen is the audience's**: the one that is not the primary screen (the laptop's display, where the console
  goes); with more than two, the first that is not the primary. **Swap screens** (the pill, and Settings → Pen →
  Presenting) makes the primary screen the audience's. If the window was on the projector when presenting starts, it
  moves to the console's screen first; it stays there afterwards.
- **Settings → Pen → Presenting**: "Presenter view on a second screen" (on by default; off: presenting with two
  screens is as with one), "Swap the screens", "The audience sees the space for notes too" (off by default), "The
  audience follows the zoom of the console" (off by default; the same as the pill's switches) and "The laser pointer
  on the rail while presenting" (on by default).
- Screens plugged in or out while presenting: the audience's window comes or goes; with one screen left the console
  becomes ordinary presenting (the pill goes with it).

### The laser pointer on the rail

The first tools ([toolbox.md](toolbox.md)) have a laser pointer on the rail: while presenting it is there as always.
A rail **without** a laser pointer of its own (the user removed it, or carried it to the top bar) gets one at its head
while presenting, after undo and redo (`toolboxPresentLaser`: pinned, never scrolled out of sight), on one screen and
on the console alike. It is not an entry of the arrangement: the user's rail is never changed by presenting. A tap takes
the laser pointer used last (an entry on the top bar, with its color), else the laser pen. Held (or right-clicked), its
menu: **Keep it on the rail** (a laser pointer entry of the user's own at the end of the user's tools: the head's goes
then) and **Not while presenting** (the setting "presenterLaser" off; Settings → Pen → Presenting brings it back).

## How it is built

- `PresenterConsole` (`qt/src/shell`, `app.presenter`): owned by `AppController`, told the presented view by
  `AppController::updatePresentedView` (presenting, another tab, the tab closed). With two screens and the setting on,
  it makes the **audience's view**: a second `CanvasView` of the presented document, as the self-reference does
  ([reference-view.md](reference-view.md)): one session, one undo history, sketches, stand-ins and thumbnails per
  document, and the rendered pages of every view under `CanvasMemory`'s one limit (nothing new to own). The audience's
  view is for reading only and its canvas item takes no input. It goes before the presented view's session can go (a
  tab is closed: the current-tab change comes first).
- **The console** is the window presenting as on one screen: `win.modes.presenterConsole` (`ViewModes.qml`) only shows
  the pill (`PresenterPill.qml`) and the audience's frame; the canvas, the floating toolbox and the other controls are
  laid out as for presenting on one screen (no room is reserved: `WindowInsets` has no right reserve any more). The
  pill folds by itself (`expanded`); while it is unfolded a `MouseArea` over the window (`presenterPillCatcher`, below
  the pill) folds it on a press and takes the press. Esc and Back: the step "presenterPill" first in
  `WindowShortcuts.stepFor`.
- **All pages**: the window's `PageGrid` (anchored to the canvas, its first row below the pill: `topInset`); the
  `presentPages` action of `ShortcutsModel` (G) has a `Shortcut` in `WindowShortcuts.qml` (while presenting, also
  while the grid has the keys) and one in `AudienceWindow.qml` (its `pagesRequested` activates the console and toggles
  the grid). A page chosen goes through `app.jumpToPage`: only then does the presented view's page change, and the
  audience follows it. Esc and Back: the step "pages" (the grid's selected pages unselected, else the grid closed),
  before the curtain, presenting and full screen; this applies outside presenting too.
- **Zooming in on the console** (`ViewController::setPresenting`, set by `CanvasView::applyScrolling` while presenting;
  `stayingOn()`): zoomed in by hand further than 1 % over presenting's fit (`presentedZoom`), the view stays on the
  slide of the last fit (`presentedGroup`, kept by `clamp()` while the fit is kept): `clamp()` holds the scroll
  position within that slide's resting range (`restRange`), `endScroll` flings within it instead of snapping to another
  slide; `stepPages` (the keys, a wheel notch) fits the next slide (`fitPresentedPage`), `scrollToPage` and
  `scrollToPageRect` to another slide fit it, a jump to the same slide keeps the zoom. `PresenterConsole::place` fits
  the console again when the page changes while zoomed in, whether the audience follows or not.
- **Following the page**: the presented view's `currentPageChanged`, its pages changing (inserted, deleted, resized,
  space for notes) and the page revisions: the audience's view goes to that page with
  `ViewController::fitPageRect(page, slide)`: the slide (the page without its `NoteSpace`) fills the view as far as
  its shape allows, centred, a fit kept when the view's size or the layout changes. The audience's canvas item has the
  shape of what it shows (`shownSize`: the slide's, the page's with the notes shown, the part's when following the
  zoom) and clips, so neither the space for notes (unless shown) nor the neighbouring pages show.
- **What shows only for a moment** (`CanvasView::setMirror`): a stroke while it is written and the laser pointer's ink
  are overlay views of upstream's handlers on one `CanvasPage`. The presenter's page gives the audience's page a
  second view of the same handler (upstream's handlers tell all their views through a `DispatchPool`, so it follows
  every point and goes when the handler finishes or cancels); the views go with their handler, when the mirror ends,
  and when either view goes first. A finished stroke is drawn onto the audience's page buffer too, so it does not
  flicker until the page is rendered again. `CurtainLayer::follow` gives the audience's view the same curtain or
  spotlight at the same place of the same page (page coordinates, so another zoom does not matter), without handles;
  it follows every repaint of the presenter's view and changes nothing when nothing moved.
- **The windows**: `AudienceWindow.qml` (a `Window` of its own, not transient for the console, frameless, black).
  `PresenterConsole::placeWindows` puts the windows on their screens (`QWindow::setScreen`, the screen's geometry,
  full screen): QML's `Window.screen` takes only the screens of `Qt.application.screens`.
- **What the audience sees** (`PresenterConsole::place`, `shownRect`): the frame is the slide, or the whole page with
  `showNotes`. Following a presenter zoomed in (its zoom more than 1 % above `ViewController::presentedZoom`, the
  fit of presenting; not while its fit is kept), the part is `presenter::audienceRegion(frame, seen, audienceSize)`
  (`qt/src/canvas/AudienceRegion.h`): `seen` is the presenter's view on the page (`CanvasView::viewOnPage`) cut to the
  frame, made wider or taller around its middle to the audience's window's shape (`audienceSize`, told by
  `AudienceWindow.qml`), moved into the frame and cut to it where it is larger; nothing of the frame seen (the
  presenter looks at the space for notes only): the whole frame. The audience's canvas item takes the part's shape
  (`shownSize`), so where the frame cuts the part it is letterboxed in black. `audienceFrame` is the part in the
  console canvas's coordinates, for the frame drawn over it in `Main.qml`.
- **The laser pointer at the rail's head** (`Toolbox.qml`: `railHasLaser`, `presentLaserShown`, `takeLaser`): not an
  item of `ToolboxModel`; shown while presenting when no laser entry is on the rail (also in a group) and the setting
  `presenterLaser` is on. "Keep it on the rail" is `ToolboxModel::add(prefill("laser"))`.
- **Cost**: `place` runs on the presenter's `ViewController::changed` (every zoom and scroll step) and on page
  revisions, and fits the audience's view again only when the page or the part changed (a page's size changing fits
  it again too). Nothing is rendered for it: the audience's view draws the part it shows at its zoom (a big page in
  part, as every view, `PageRaster::drawnWhole`), after the zoom settles; pen points do not move the view. The pill's
  next page is a thumbnail (`image://thumbnail`, the thumbnails' cache), asked for only while it is unfolded.
- The time: a `QElapsedTimer` and what ran before the last pause; told every second while the console shows.
- Settings: `presenterView`, `presenterSwapScreens`, `presenterShowNotes`, `presenterFollowView` (off by default),
  `presenterLaser` (`xournalQt` custom settings; `SettingsModel` keys).

## Tests

- `PresenterMirrorTest` (`-L canvas`): the slide fills the audience's view with the space for notes out of view, at
  another size and shape, with space on the left and at the top; a stroke and the laser pointer show on the audience's
  page while they last and go with them; either view may go first; the curtain and the spotlight follow without
  handles.
- `AudienceRegion.widenedToTheScreensShapeWithinTheSlide` (`-L canvas`): the part at the screen's shape around what
  is seen, moved into the slide at its edges, cut to it when larger, the slide's part only, nothing seen: the slide.
- `ViewSnapping.presentingZoomedInStaysOnTheSlide` (`-L canvas`): at the fit a fling goes on; zoomed in, a drag and a
  fling end at the slide's edges, a jump to the slide keeps the zoom, the next slide and a page chosen come whole; not
  presenting, a drag goes on as before.
- `PresenterView.*` (`xqt-ui-tests`): with one screen (the ordinary run, or the setting off) presenting is as on one
  screen (no pill; G and the floating toolbox's ⋯ open the grid, Esc closes it first); zoomed in, written on, dragged,
  flung and swiped, the page stays and the next one comes whole; the laser pointer at the rail's head when the rail
  has none (a tap, its menu, the setting).
  `PresenterView.ui@2screens` runs the same tests with **two off-screen screens** (`qt/tests/ui/offscreen-two-screens.json`:
  a 1920 × 1080 laptop, the primary one, and a 1280 × 720 projector): the audience's window full screen on the
  projector with only the slide (its shape, its space for notes out of view), the console on the laptop: no panel,
  the page with its space for notes over the whole window, the floating toolbox, the pill folded at the top left with
  the minutes, unfolded with the page and the hint; the keys in both windows and the audience following; a page number
  typed in the audience's window going on in the console; the next page and the last page; Escape ending it
  everywhere; the time (the minutes folded; runs, pauses, goes on, back to 0); the pill folding on a tap beside it
  (no ink) and on Esc before Esc ends presenting; the page grid (the pill's button, G in both windows, the audience
  keeping the slide until a page is tapped, Esc closing it first); zoomed in on the console the audience keeping the
  whole slide with the stroke written there; a stroke, the laser pointer and the curtain on the audience's page; swap
  screens; the toolbox at the window's edge; another tab presenting, the presented tab closed; the notes for the
  audience (the fitted rect the slide when off, the whole page when on, at once); the audience following the zoom
  when switched on (the part at the projector's shape containing what the presenter sees, the audience's view fitted
  to it, the frame on the console, scrolling, Fit, a page change, the notes shown, the switch off and on).
- `EscapeOrBackTest.thePageGridBeforePresentingAndFullScreen` (Esc and Back): the grid's selected pages, then the grid,
  then presenting, then full screen.
- Pictures: `XQT_SHOTS=<folder>` with the two screens (`PresenterView.shotsOfTheConsole`, the grid and the laser
  pointer: `thePageGridOnTheConsole`, `theLaserPointerIsOnTheRailWhilePresenting`) writes the console at 1920 × 1080
  folded, unfolded, zoomed in, with the grid open, and the audience's screen beside each.

## Decisions

- **The console is presenting as on one screen** (the author). The panel is gone; what it held is in the pill. The
  pill is the console's only addition, so the console looks and works as presenting does on one screen.
- **Folded, the pill shows whole minutes ("12 min")**, not m:ss: nothing changes every second at the edge of the
  presenter's eye, and the minutes are what one checks while talking. Unfolded it shows the seconds.
- **The pill at the top left**: the toolbox floats at the right by default, the page number and the view's controls
  are at the bottom; a slide's top left corner holds its title more often than what is pointed at, and the pill is
  small there. It is not draggable (one fixed place, found without looking).
- **The pill stays when presenting without controls** (Ctrl+F5), as the panel did: the audience never sees it, and the
  time is what the presenter needs then too.
- **No pill on one screen**: there the audience sees the window; the page grid is G and ⋯ → All pages there.
- **The laser pointer**: the first tools have one on the rail already; a rail without one gets it at its head while
  presenting rather than an entry being added to the user's arrangement (the user's own arrangement is never changed
  by presenting; removed by hand, it comes back only while presenting, and its menu or the setting turns that off).
  On one screen too, for the same rail in both.
- **Following the zoom: off by default** (the author, 2026-10-11: zooming in on the console is for writing details;
  "the full page view is only needed on the Beamer screen"). It stays an option, with its frame only while on.
- **Zoomed in, the view stays on the slide**: a swipe or a fling at the slide's edge while zoomed in is moving around
  the slide, not a page change; page changes are the keys, a swipe at the fit, the grid. Every page change shows the
  new slide whole on the console.
- **Esc closes the page grid before anything else** (after the pill and the drawer), also outside presenting: the grid
  is in front of the page (before, in full screen Esc left full screen with the grid still open).
- **Never less than the presenter sees** (following the zoom). The screens' shapes differ, so the part is widened to
  the audience's shape, never cropped: what the presenter points at is always on the projector. At the slide's edge the
  part is moved into the slide instead of showing black or the space for notes beyond it; only where the slide is
  narrower (or lower) than the part is it letterboxed. Hence the frame on the console.
- **Zoomed in means more than presenting's fit.** At the fit (or zoomed out) the audience sees the whole slide, so
  sliding to the next page at the fit never zooms the audience in on half a page.
- **The presenter looking only at the space for notes**: the audience sees the whole slide (rather than a magnified
  corner of it).
- **Rotation: the audience stays upright.** The canvas cannot be turned while presenting (`setPresenting` turns it
  upright, `rotationAllowed` says no), so this is trivially so; were it turned, the part would be the bounding box of
  the presenter's view on the page (`viewOnPage`), upright, and the frame that box.

## Not done

- A page narrower than the window (an A4 page upright, a slide with its space for notes beside it) shows the
  neighbouring pages at its sides while presenting, on one screen as on the console (the layout of presenting is a
  row of pages); black there would be a change of the canvas's drawing while presenting.

## On the device

The off-screen platform places windows exactly where they are asked to go; a real window system may not (see the
device checklist): the audience's window on the projector under X11, Wayland (KDE, GNOME) and Windows, full screen
there and not on the laptop; the console moving off the projector; swap screens moving both; a clicker; the
projector unplugged and plugged in again while presenting; how smooth the ink is on the projector while it is written
(the audience's page is composed again for every point); a 4K projector (the slide rendered at its size, memory); how
smoothly the projector follows zooming and scrolling on the console (when following), and a 4K projector zoomed in far.

- [ ] Presenting (F5) with a projector: the console on the laptop (the page, the toolbox, the pill folded at the top
      left), the slide alone on the projector (also when the window was on the projector); a clicker turns pages (also
      with the focus on the projector's window: click there first); its "black screen" key (often B or .) puts out
      the curtain (B) or does nothing (.); writing on the console shows on the projector at once; unplugging the
      projector ends the second screen cleanly.
- [ ] On a tablet as the console: the pill's minutes, its grid button and its switches are easy to tap with a finger;
      a tap beside the unfolded pill folds it and writes nothing (also with the pen); the page grid scrolls with a
      finger and a page tapped is presented.
- [ ] Zoomed in on the console with a pinch to write a detail: the projector keeps the whole slide and shows the ink
      at once; a one-finger swipe at the slide's edge stays on the slide; Space brings the next slide whole.
