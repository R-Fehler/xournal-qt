# The presenter view on a second screen (qt/presenter-view)

Status: built (2026-10-05); the notes for the audience and the audience following the zoom: qt/presenter-follow
(2026-10-05). Idea A14 of [the ideas of 2026-10](history/README.md), chosen by the author; as in Okular and
PowerPoint.

While presenting with two screens (a laptop and a projector), the audience's screen shows only the slide, full screen,
and the laptop's screen shows the **presenter's console**: the current page large with its space for notes
([note-space.md](note-space.md)), the next page smaller, a clock, the time since the start, the page number, and the
toolbox ([toolbox.md](toolbox.md)) to write on the slide: the audience sees the ink as it is written. With one screen,
presenting is as before.

## Using it

- **F5** (or the presentation button, ⋮ → Present) with a second screen connected (the desktop extends to it, not
  mirrors it): the audience's window opens full screen on the other screen; this window becomes the console, full
  screen on its screen. Ctrl+F5 (without controls) does the same; the console's panel stays, only the controls over
  the page hide.
- **The audience's screen**: the slide as large as the screen allows, black around it. The **space for notes is not
  shown there** (unless "Notes for the audience too" is on, below): the page beside or below the slide stays on the
  console. Strokes being written, the laser pointer,
  the curtain and the spotlight ([curtain.md](curtain.md)) show there at once, without the curtain's handles.
- **The console**: at the left the current page with its space for notes, presenting as on one screen (fitted to
  the screen, page by page, black around it; the floating toolbox, the page number for a moment; without controls the
  Zen dot, [zen.md](zen.md));
  at the right the panel:
  - the time of day (the locale's short format) and the time since the start (m:ss, h:mm:ss after an hour), with
    pause/go on and back to 0:00; it starts when the presentation starts (pausing keeps it, ending forgets it);
  - "Page 3 of 20";
  - the next page, smaller (its thumbnail with its own space for notes); "The last page" on the last one;
  - a hint "The space for notes shows only here" on a page that has some ("The audience sees the space for notes
    too" with the switch below on);
  - **Notes for the audience too** (a switch, off by default);
  - **The audience follows my zoom** (a switch, on by default) and **Fit**;
  - which screen the audience's is, **Swap screens**, **End**.
- **Notes for the audience too** (qt/presenter-follow): the audience sees the whole page, the slide with its space
  for notes, as large as the screen allows, and the ink written there as it is written. It takes effect at once while
  presenting. Pages without space for notes look the same either way.
- **The audience follows my zoom** (qt/presenter-follow): zoom in on the console's page (pinch, Ctrl+wheel, the zoom
  pill) and scroll around (fingers, the wheel, the hand): the audience sees the same part of the page, as large as
  their screen allows. Their screen's shape differs from the console's, so they see what the presenter sees widened
  to it (never less), and at the slide's edge it stops at the edge (black beyond: not the space for notes, unless
  shown, and never the next page). A thin orange frame on the console shows exactly what they see, only while it is a
  part of the page (it can reach past the console's sides: they see more there). **Fit** in the console, the fits of
  the zoom pill and a double tap bring both screens back to the whole slide; going to another page while zoomed in
  shows that page whole on both. Off: the audience always sees the whole slide, whatever the console shows.
- **Keys**: Space, → ↓ Page Down on; ← ↑ Page Up Backspace back; Home, End; a page number and Enter; Escape (and F5)
  end. They work in **both windows**: a clicker or the keyboard sends keys to whichever window has the focus (a click
  on the projector's screen gives it the focus). A page number typed in the audience's window is typed on in the
  console (the console takes the focus), so the audience never sees it.
- **Which screen is the audience's**: the one that is not the primary screen (the laptop's display, where the console
  goes); with more than two, the first that is not the primary. **Swap screens** (the console, and Settings → Pen →
  Presenting) makes the primary screen the audience's. If the window was on the projector when presenting starts, it
  moves to the console's screen first; it stays there afterwards.
- **Settings → Pen → Presenting**: "Presenter view on a second screen" (on by default; off: presenting with two
  screens is as with one), "Swap the screens", "The audience sees the space for notes too" (off by default) and
  "The audience follows the zoom of the console" (on by default): the same as the console's switches.
- Screens plugged in or out while presenting: the audience's window comes or goes; with one screen left the console
  becomes ordinary presenting.

## How it is built

- `PresenterConsole` (`qt/src/shell`, `app.presenter`): owned by `AppController`, told the presented view by
  `AppController::updatePresentedView` (presenting, another tab, the tab closed). With two screens and the setting on,
  it makes the **audience's view**: a second `CanvasView` of the presented document, as the self-reference does
  ([reference-view.md](reference-view.md)): one session, one undo history, previews and thumbnails per document, and
  the rendered pages of every view under `CanvasMemory`'s one limit (nothing new to own). The audience's view is for
  reading only and its canvas item takes no input. It goes before the presented view's session can go (a tab is
  closed: the current-tab change comes first).
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
- **The windows**: `AudienceWindow.qml` (a `Window` of its own, not transient for the console, frameless, black) and
  `PresenterPanel.qml` (the panel; `Main.qml` puts the canvas area left of it and `controlsRight` keeps the controls
  over the page, the floating toolbox among them, beside it). `PresenterConsole::placeWindows` puts the windows on
  their screens (`QWindow::setScreen`, the screen's geometry, full screen): QML's `Window.screen` takes only the
  screens of `Qt.application.screens`.
- **What the audience sees** (`PresenterConsole::place`, `shownRect`): the frame is the slide, or the whole page with
  `showNotes`. Following a presenter zoomed in (its zoom more than 1 % above `ViewController::presentedZoom`, the
  fit of presenting; not while its fit is kept), the part is `presenter::audienceRegion(frame, seen, audienceSize)`
  (`qt/src/canvas/AudienceRegion.h`): `seen` is the presenter's view on the page (`CanvasView::viewOnPage`) cut to the
  frame, made wider or taller around its middle to the audience's window's shape (`audienceSize`, told by
  `AudienceWindow.qml`), moved into the frame and cut to it where it is larger; nothing of the frame seen (the
  presenter looks at the space for notes only): the whole frame. The audience's canvas item takes the part's shape
  (`shownSize`), so where the frame cuts the part it is letterboxed in black. `audienceFrame` is the part in the
  console canvas's coordinates, for the frame drawn over it in `Main.qml`.
- **Cost**: `place` runs on the presenter's `ViewController::changed` (every zoom and scroll step) and on page
  revisions, and fits the audience's view again only when the page or the part changed (a page's size changing fits
  it again too). Nothing is rendered for it: the audience's view draws the part it shows at its zoom (a big page in
  part, as every view, `PageRaster::drawnWhole`), after the zoom settles; pen points do not move the view.
- The time: a `QElapsedTimer` and what ran before the last pause; told every second while the console shows.
- Settings: `presenterView`, `presenterSwapScreens`, `presenterShowNotes`, `presenterFollowView` (`xournalQt` custom
  settings; `SettingsModel` keys).

## Tests

- `PresenterMirrorTest` (`-L canvas`): the slide fills the audience's view with the space for notes out of view, at
  another size and shape, with space on the left and at the top; a stroke and the laser pointer show on the audience's
  page while they last and go with them; either view may go first; the curtain and the spotlight follow without
  handles.
- `AudienceRegion.widenedToTheScreensShapeWithinTheSlide` (`-L canvas`): the part at the screen's shape around what
  is seen, moved into the slide at its edges, cut to it when larger, the slide's part only, nothing seen: the slide.
- `PresenterView.*` (`xqt-ui-tests`): with one screen (the ordinary run, or the setting off) presenting is as before.
  `PresenterView.ui@2screens` runs the same tests with **two off-screen screens** (`qt/tests/ui/offscreen-two-screens.json`:
  a 1920 × 1080 laptop, the primary one, and a 1280 × 720 projector): the audience's window full screen on the
  projector with only the slide (its shape, its space for notes out of view), the console on the laptop with the page
  and its space for notes beside the panel; the keys in both windows and the audience following; a page number typed in
  the audience's window going on in the console; the next page and the last page; Escape ending it everywhere; the
  time (runs, pauses, goes on, back to 0); a stroke, the laser pointer and the curtain on the audience's page; swap
  screens; the floating toolbox beside the panel; another tab presenting, the presented tab closed; the notes for the
  audience (the fitted rect the slide when off, the whole page when on, at once); the audience following the zoom
  (the part at the projector's shape containing what the presenter sees, the audience's view fitted to it, the frame
  on the console, scrolling, Fit, a page change, the notes shown, the switch off and on).

## Decisions

- **Notes for the audience: off by default.** Today's behaviour stays the default: the space for notes is the
  presenter's. On, the audience's screen shows the whole page, not the slide plus a part of the notes.
- **Following the zoom: on by default.** Zooming in on the console is almost always meant for the audience (pointing at
  a detail); a presenter who wants to look at something alone switches it off.
- **Never less than the presenter sees.** The screens' shapes differ, so the part is widened to the audience's shape,
  never cropped: what the presenter points at is always on the projector. At the slide's edge the part is moved into
  the slide instead of showing black or the space for notes beyond it; only where the slide is narrower (or lower)
  than the part is it letterboxed. Hence the frame on the console: it shows what the audience sees beyond the
  presenter's view.
- **Zoomed in means more than presenting's fit.** At the fit (or zoomed out) the audience sees the whole slide, so
  sliding to the next page at the fit never zooms the audience in on half a page.
- **The presenter looking only at the space for notes**: the audience sees the whole slide (rather than a magnified
  corner of it).
- **A page change shows the new page whole on both screens**: the console's view is fitted too when it was zoomed
  in, so the two stay in step (otherwise the next scroll would jump the audience into a zoomed part of the new page).
- **Rotation: the audience stays upright.** The canvas cannot be turned while presenting (`setPresenting` turns it
  upright, `rotationAllowed` says no), so this is trivially so; were it turned, the part would be the bounding box of
  the presenter's view on the page (`viewOnPage`), upright, and the frame that box.

## Left for the device

The off-screen platform places windows exactly where they are asked to go; a real window system may not (see the
device checklist): the audience's window on the projector under X11, Wayland (KDE, GNOME) and Windows, full screen
there and not on the laptop; the console moving off the projector; swap screens moving both; a clicker; the
projector unplugged and plugged in again while presenting; how smooth the ink is on the projector while it is written
(the audience's page is composed again for every point); a 4K projector (the slide rendered at its size, memory); how
smoothly the projector follows zooming and scrolling on the console, and a 4K projector zoomed in far.
