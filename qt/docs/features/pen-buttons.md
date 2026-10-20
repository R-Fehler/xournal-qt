# The eraser of the pen's buttons

The eraser the pen's eraser end and its two side buttons give, beside what each of them does (Settings → Pen →
Buttons and eraser). Code: `qt/src/session/ButtonWidths.{h,cpp}` (the setting and the tools it sizes),
`AppContext::initButtonTools` (it applies at the start and after a button's tool changed), `SettingsModel` (the keys
the settings sheet reads and writes), `HoverPointer::eraserMark` (the outline the pointer shows before the button
touches), `CanvasPage::eraseAlong` (erasing between the pen's samples), `qt/src/app/qml/SettingsPen.qml` and
`SettingsButtonWidthRow.qml` (the rows). Tests:
`qt/tests/shell/SettingsModelTest.cpp` (label `shell`), `qt/tests/canvas/PenStylesTest.cpp` (the quick wipe,
`canvas`), `qt/tests/ui/MainWindowTest.cpp` (the rows in Settings → Pen, `ui`).

## What a side button erases with

A button's `ButtonConfig` (upstream) either names one of the tool's five sizes or none, and with none ("no change")
the button's tool takes the tool bar eraser's size and kind. The tool bar eraser's width is one of those five sizes
unless an eraser of the toolbox is taken: that gives the tool a width of its own (0.1–150 pt, `setCustomThickness`),
and the five sizes then mean nothing. So a side button could not be given the size of the eraser it gives: it erased
as wide as the last eraser entry taken.

Each of the three buttons of the pen that erase has a width of its own now:

| Setting → Pen → Buttons and eraser | Button | Key in settings.xml (`xournalQt` part) |
| --- | --- | --- |
| the row under "Side button / eraser end" | the pen's eraser end (`BUTTON_ERASER`) | `eraserWidth` |
| the row under "Lower side button" | the lower side button (`BUTTON_STYLUS_ONE`) | `stylusWidth` |
| the row under "Upper side button" | the upper side button (`BUTTON_STYLUS_TWO`) | `stylus2Width` |

The row offers what the eraser's editor in the toolbox offers for an eraser: 0.1 to 150 pt on a log slider, shown in
mm beside it (8.5 pt, 3 mm, when it is first given one: the medium size the tool bar eraser had). Beside it there is
the switch **"Own width"**, off by default: off, a button erases with the tool bar eraser's width and kind, as before
(the setting is absent from `settings.xml`); on, with the width on the slider.

- **The eraser in hand follows at once**, not from the next start: the button's tool (a copy of the eraser that
  `ToolHandler` keeps per button) takes the width as a width of its own, as an eraser taken from the toolbox does
  (`ToolHandler::setButtonCustomThickness`, a `xournal-qt:` seam in upstream's `ToolHandler`). So the five sizes never
  override it: picking another size for the tool bar eraser (its five sizes, or an eraser entry of the toolbox) cannot
  make the button's eraser smaller. The tool bar eraser's own width and sizes are not touched.
- **Only a button set to the eraser** (`eraserButtonTool` / `stylusButtonTool` / `stylusButton2Tool`) takes the width:
  a button set to the hand, the highlighter or a selection has nothing to size.
- **Before the button touches**, the hovering pen shows the eraser the button would give: the pen's eraser end its
  own width, a side button that erases while it is held its own width (the outline the pointer shows,
  [hover-cursors.md](hover-cursors.md)).
- **The switch off again**: the button's tool is made anew as the button's setting says (the tool bar eraser's width
  and kind), and the key leaves `settings.xml`. A tool in hand that was that very button's is the pen's again (the old
  copy goes away), never a pointer to it.

What a side button does *not* change is the eraser's kind: standard (cuts strokes), whole strokes or whiteout come
from the tool bar eraser's entry in the toolbox, as the buttons always had it.

## A quick wipe leaves no ink behind

A pen reports a few samples a second (and fewer while it hovers than while its tip is down). Upstream's
`EraseHandler::erase(x, y)` erases one box where it is told to and does not join two of them, so a wipe that moves
further than the eraser is wide between two samples used to leave ink where the eraser really was: the tool bar's
eraser had it too, and a small eraser made it plain (a 2.83 pt eraser has a box of 5.66 pt a side; a pen moving 400 pt
in three samples skips 100 pt at a time).

`CanvasPage::eraseAlong` (`qt/src/canvas/CanvasPage.cpp`) keeps where the eraser was last told to erase (`eraserAt`)
and, for each move, erases in steps of **half the eraser's reach** (the tool's width; at least 0.5 pt) along the line
from there to the new point, then at the point itself. Two boxes half a reach apart overlap by half, so nothing in
between is skipped. The release erases along to where the pen was let go too (the last piece of a quick wipe), before
the erase is finalized into one undo step.

- **The same eraser**: every step calls upstream's `EraseHandler::erase`, so cutting strokes, whole-stroke deletion,
  the sticky-note clipping and the undo step are upstream's, unchanged. This is the tool bar eraser's path as well,
  so it erases between samples too (a fast drag with the mouse).
- **Cost**: the number of steps is the distance over half the reach (a wipe of 400 pt with a 2.83 pt eraser: about
  280 calls for that move), each still culled by the strokes' bounding boxes. It follows how far the pen moved, not
  how much is on the page. At most `MAX_ERASE_STEPS` (400) steps are taken for one move; a bigger jump (a page crossed
  in one event, a device glitch) erases at its ends only rather than stalling or skipping ink silently.
  `PenStylesTest.aFastWipeStaysCheap` (label `canvas`) wipes across a page of 40 strokes and checks the time.

## On the device

What only a real device, screen or another app can show; walked before a release from the [device checklist](../testing/device-checklist.md).

- [ ] With "Own width" on for the lower side button, that button erases as wide as the slider says, and its outline
      is that wide while the pen hovers and the button is held; the tool bar's eraser keeps its own width.
- [ ] A quick wipe with the side button held leaves no ink behind (a small eraser, a low zoom): the pen's real sample
      rate, which the tests can only imitate.
