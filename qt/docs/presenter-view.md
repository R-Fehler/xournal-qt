# The presenter view on a second screen (qt/presenter-view)

Status: built (2026-10-05). Idea A14 of [ideas-2026-10.md](ideas-2026-10.md), chosen by the author; as in Okular and
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
- **The audience's screen**: the slide as large as the screen allows, black around it. The **space for notes is never
  shown there**: the page beside or below the slide stays on the console. Strokes being written, the laser pointer,
  the curtain and the spotlight ([curtain.md](curtain.md)) show there at once, without the curtain's handles.
- **The console**: at the left the current page with its space for notes, presenting as on one screen (fitted to
  the screen, page by page, black around it; the floating toolbox, the corner field, the page number for a moment);
  at the right the panel:
  - the time of day (the locale's short format) and the time since the start (m:ss, h:mm:ss after an hour), with
    pause/go on and back to 0:00; it starts when the presentation starts (pausing keeps it, ending forgets it);
  - "Page 3 of 20";
  - the next page, smaller (its thumbnail with its own space for notes); "The last page" on the last one;
  - a hint "The space for notes shows only here" on a page that has some;
  - which screen the audience's is, **Swap screens**, **End**.
- **Keys**: Space, → ↓ Page Down on; ← ↑ Page Up Backspace back; Home, End; a page number and Enter; Escape (and F5)
  end. They work in **both windows**: a clicker or the keyboard sends keys to whichever window has the focus (a click
  on the projector's screen gives it the focus). A page number typed in the audience's window is typed on in the
  console (the console takes the focus), so the audience never sees it.
- **Which screen is the audience's**: the one that is not the primary screen (the laptop's display, where the console
  goes); with more than two, the first that is not the primary. **Swap screens** (the console, and Settings → Pen →
  Presenting) makes the primary screen the audience's. If the window was on the projector when presenting starts, it
  moves to the console's screen first; it stays there afterwards.
- **Settings → Pen → Presenting**: "Presenter view on a second screen" (on by default; off: presenting with two
  screens is as with one) and "Swap the screens".
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
  slide's shape (`slideSize`) and clips, so neither the space for notes nor the neighbouring pages show.
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
- The time: a `QElapsedTimer` and what ran before the last pause; told every second while the console shows.
- Settings: `presenterView`, `presenterSwapScreens` (`xournalQt` custom settings; `SettingsModel` keys).

## Tests

- `PresenterMirrorTest` (`-L canvas`): the slide fills the audience's view with the space for notes out of view, at
  another size and shape, with space on the left and at the top; a stroke and the laser pointer show on the audience's
  page while they last and go with them; either view may go first; the curtain and the spotlight follow without
  handles.
- `PresenterView.*` (`xqt-ui-tests`): with one screen (the ordinary run, or the setting off) presenting is as before.
  `PresenterView.ui@2screens` runs the same tests with **two off-screen screens** (`qt/tests/ui/offscreen-two-screens.json`:
  a 1920 × 1080 laptop, the primary one, and a 1280 × 720 projector): the audience's window full screen on the
  projector with only the slide (its shape, its space for notes out of view), the console on the laptop with the page
  and its space for notes beside the panel; the keys in both windows and the audience following; a page number typed in
  the audience's window going on in the console; the next page and the last page; Escape ending it everywhere; the
  time (runs, pauses, goes on, back to 0); a stroke, the laser pointer and the curtain on the audience's page; swap
  screens; the floating toolbox beside the panel; another tab presenting, the presented tab closed.

## Left for the device

The off-screen platform places windows exactly where they are asked to go; a real window system may not (see the
device checklist): the audience's window on the projector under X11, Wayland (KDE, GNOME) and Windows, full screen
there and not on the laptop; the console moving off the projector; swap screens moving both; a clicker; the
projector unplugged and plugged in again while presenting; how smooth the ink is on the projector while it is written
(the audience's page is composed again for every point); a 4K projector (the slide rendered at its size, memory).
