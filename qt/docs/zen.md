# Zen, read only and Read

The author (2026-10-06), on the reader chrome of 0.7.0: "the read only mode still has this big ass lock icon … The idea
of the small dot chrome is to be invisible and produce a zen mode UI which fitted also for tiny screen document
reading and referencing." Decided: "pill on the dot, zen can write". Built in `qt/zen` (0.8.0).

Three switches, each on its own; the window's state (full screen) is the third:

| | What | How |
| --- | --- | --- |
| **Full screen** (`win.fullScreenMode`) | the compact chrome in a full-screen window, as before | F11, the top bar's button |
| **Zen** (`win.zen`) | everything around the page hidden: the toolbox (docked, floating, the phone's dock), the top bar, the tab strip and the tab dots, the phone's app bar, the sidebar and its arrow, the view pill, the back / forward pill and the other pills over the page (`win.hudHidden`). The pen keeps writing | the top bar's Zen (`zenButton`, in its first layout on every screen, phones too; qt/top-bar), ⋮ → View → Zen (checkable), **Ctrl+Alt+Z** (changeable), the floating toolbox's ⋯; automatic in a tiny window |
| **Read only** (`win.readOnly`) | the page cannot be written on (`DocumentCanvas.readingOnly`): the pen and the fingers scroll, PDF text can be selected and copied; the left and right fifths of the page turn the pages | ⋮ → View → Read only (checkable), the dot's pill, the floating toolbox's ⋯ |

Combinations:

- **Read** (Ctrl+Alt+R, ⋮ → View → Read, the top bar's Read): Zen and read only, in full screen (a tiny window stays
  a window). Ctrl+Alt+R again ends it: read only off, and Zen and full screen where Read turned them on. Esc ends it and
  leaves Zen. "Show controls" leaves only Zen (read only and full screen stay).
- **Present without controls** (Ctrl+F5, the Present button held, ⋮ → View): presenting in Zen. While presenting, Zen
  is `win.presentClean` (not the window's own Zen, which comes back when presenting ends). Presenting with the controls
  has no dot; Ctrl+F5 and the floating toolbox's ⋯ → "Hide the tools" hide them.
- **A tiny window** (under 360 px either way: split screen, a pop-up window): Zen is on of itself (`win.zenAuto`).
  Leaving it there is remembered for tiny windows (`layout/tiny/zen` = `off`, as the reader chrome's choice was);
  turning it on there again makes it automatic again.

## The dot

Zen's only mark (`zenDot`): a 10 px grey dot with a light rim in the lower left corner of the page, clear of the safe
area (`win.canvasControlsLeft`, `canvasControlsBottom`). It shows at about 60 % and fades to about 20 % after 2 s; the
mouse or the pen near it (a 128 px square around the corner, `zenNear`, looked through by the page) or its pill open
make it clearer (90 %). Its target is 48 px. A tap opens its **pill** (`zenPill`) over the page beside it; the page does
not move:

- **Show controls** (`zenShowControls`): leaves Zen;
- **Read only** (`zenReadOnly`): a switch;
- **the page number** (`zenPage`, "3 / 12"): a tap opens all pages (the page grid), to go to one;
- **fit the width** (`zenFitWidth`) and **the whole page** (`zenFitPage`).

A tap on the page closes it and writes nothing (`zenPillCatcher`); so do Esc and its entries but the switch. In Zen with read only
on, a finger's tap in the middle of the page opens the pill too (`DocumentCanvas.middleTapped`, after the double tap's
time: a double tap still zooms).

## Read only without a lock

The lock in the corner of 0.7.0 (`readOnlyMark`) is gone. Instead the first stroke tried while read only (a pen or
mouse drag with a tool that writes; `CanvasView::writingRefused`) shows a small note at the pen for a moment, once until
read only is turned off (`readOnlyNote`): "Read only — tap the dot to write" in Zen, "Read only — ⋯ → Read only to
write" in full screen, "Read only — ⋮ → View → Read only to write" in a window. The edge fields keep turning the pages
(`readingTapFields`, [toolbox.md](toolbox.md), "Zen, read only and presenting").

## Leaving

Esc (the pill first, a selection first; while presenting Esc ends presenting), Zen's keys again, "Show controls".
**Android's Back** (the key and the back gesture; the author, 2026-10-06: "the back gesture or button should leave zen
mode on android") leaves Zen before anything else Back does (qt/top-bar; `zenBackShortcut`): the pill closes with it,
Read ends (read only off, and the full screen it entered), presenting without controls gets its controls back
(presenting goes on). Only a popup open over the page takes Back first, since it is in front: a sheet, a dialog, the
tool editor, the stickers or the emoji (they count themselves open, `win.takeBack`, so that two shortcuts on one key are
never ambiguous); the next Back leaves Zen. Tested off-screen with `Qt::Key_Back` (`ToolboxTest.backLeavesZen`); the
gesture itself is on the device checklist.
Read ends with its keys again or Esc. Showing the home screen ends Zen by hand, Read and read only.

## Gone

The reader chrome (`chromeMode` "reader", its automatic switch in a tiny window, `chromeAuto`, `chromeSetting`,
`chooseChrome`), the chrome chosen per size class (Settings → Display → "Controls at this size", `layout/<class>/chrome`,
no longer read; the compact chrome is full screen's only), presenting's own corner field (`presentCornerMark`: the Zen
dot replaces it while presenting without controls) and the lock mark.

Tests: `ToolboxTest.backLeavesZen`, `ToolboxTest.zenIsOnTheTopBarAtEverySize`, `ReadingTest` (ToolboxTest.cpp: Read, read only on its own, Zen hides all but the page and the dot, the pill and
its entries, Zen writes, the note once), `PhoneChromeTest.zenIsAutomaticOnlyInATinyWindow`,
`PhoneChromeTest.presentingWithoutControlsHasTheZenDot`, `MainWindowTest.presentingWithoutControls`,
`ReadingPhone.ui@phone`.
