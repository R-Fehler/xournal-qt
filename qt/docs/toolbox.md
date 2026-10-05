# The toolbox (qt/toolbox)

The author (2026-10-04): "it feels like just picking up a real pen from a sorted toolbox on a table … this is how
Drawboard works … a plus button like we already have, then the user can select which type of tool should go there and
select color and width etc. … the order of the tools should be user configurable with up/down arrows in the tool
context menu, which also allows changing the tool at the current position or by drag and dropping … drag and drop
only starts after pressing a while, then the user gets visual feedback that we are moving the tool and not the pill."
And: one element in the window and in full screen; undo and redo easy to find; fewer commands hidden behind » on a
wide screen; a reading mode without the edit tools.

The author's decisions: a rail docked to any side of the canvas, **right by default**; the classic tool bar is kept
for one release (Settings → Pen → Tools); the eraser is an entry; a folded section opens a list; the tools are stored
per device. Reading and presenting are two modes over one "tools hidden" view (below).

## Using it

**My tools.** Each entry is a tool with its settings: a pen (color or palette role, width, line style, filling), a
highlighter, a shape (line, rectangle, ellipse, arrow, double arrow, coordinate system, recognized shapes; drawn with
the pen or the highlighter), an eraser (standard, whiteout, whole strokes; its size), a text box (its font and color),
a sticky note (its pastel), the laser pointer (pen or highlighter), a **snip** (a rectangle or a lasso whose picture goes to the clipboard; a
cycling tool: a tap while it is armed takes the other shape, qt/ui-rework). Dividers group them into sections.

The first start: three pens (body, key terms, warnings) | two highlighters (key terms, definitions) | the eraser | a
line, a text box, a sticky note | the laser pointer. The pens and highlighters take their colors from the **palette
roles** ([color-palettes.md](color-palettes.md)): choosing another palette recolors them. After an update from the
classic tool bar the first pen has the pen's color and width of before, the eraser its kind, the text box the font.

| Gesture | What it does |
| --- | --- |
| tap | picks the tool up: the tool in hand gets everything it holds (`AppController::applyToolEntry`); it is lifted towards the page |
| tap on the tool in hand | its **editor**, beside it towards the page (a sheet on a phone): a preview stroke, the color (the palette's roles, the colors used lately, a hex code, the picker), the width (0.1–150 pt on a log slider, shown in mm, and the five sizes as dots), the line style, the filling (the line's color or another, its opacity), the eraser's kind, the shape and what draws it, the font, the note's pastel. A change is written at once and the tool in hand follows; there is no OK |
| the mouse wheel over a tool | its width, a fifth more or less per notch |
| long press (400 ms, not moved), right click | its **menu**: Edit…, Move up/down (left/right), Replace with…, Duplicate, Add a tool here…, Add/Remove the divider after it, Remove (not the last eraser) |
| long press, then move | **carries it** to another place: it is lifted (larger, with a shadow), its place stays faint, a line shows where it goes; let go away from the rail: it goes back |
| a drag at once | scrolls the rail (when it scrolls) |
| "+" at the end | the kinds of tools; the editor opens with the last tool of that kind as the start; **Add** puts it at the end and takes it |
| P, H, E, T | the pen, highlighter, eraser, text box used last (with its settings) |

The rail itself moves only by its **grip** (the dotted cap at its start): dragged towards another edge, that edge is
highlighted, let go: the rail goes there (remembered per window size, `layout/<class>/toolbox`; also ⋮ → View →
Toolbox position).

**Head and tail.** Undo and redo lead the rail (one place: the view pill has them no more while the toolbox is shown).
After the user's tools come the fixed tools: hand, select (rectangle ↔ lasso, the snips in its list), write on the
page, setsquare / compass (curtain and spotlight in its list), mark PDF text, the finger draws, and **record audio**
where the build can record ([audio.md](audio.md); its list: the play tool, the recordings). They are the window's own
buttons, lent to the rail (`Toolbox.fixedButtons`): one place each, so the command bar leaves them out. Recording is
among them rather than in the command bar because the rail is the one element that stays in full screen and while
presenting (floating): a lecture is recorded there too. Folded into a stack, the stack shows the record button while
it records. The recording and playback pills sit at the top of the page, below the toolbox when it floats at the top.

## Short rails

`ToolboxPlan.js` is a pure function of the rail's length and its sections. When the tools do not fit, sections fold
into **stacks**, one by one: the fixed tools first (one button showing the one in use; a tap lists them), then the
user's sections from the end. A stack shows the entry of its section used last (the one in hand if it is there) with
dots for how many it holds; a tap takes it, a tap on it while it is in hand or a long press opens the section's list
beside it. Only when everything is folded and it still does not fit, the middle scrolls. A rail that grows unfolds only
with 16 px to spare (no flicker at an edge).

## Where it is

| Window | Toolbox |
| --- | --- |
| desktop, tablet (full chrome) | docked to its edge, taking its strip (`sideTools` at a side, `toolboxRow` at the top or the bottom); the right by default, also in tablet portrait (a 52 px rail leaves an A4 page well visible) |
| full screen (the compact chrome), presenting with the tools | the same toolbox floating 8 px off its edge, rounded, as long as its tools; ⋯ at its end: present, present without controls, search, settings, leave full screen |
| phone portrait | the dock at the bottom: undo, redo, the first tools that fit (the one in hand always among them), **My tools** (a sheet: every tool, "Add a tool", the other tools and commands; record audio under Insert), the page number |
| phone held sideways | the same as a rail at the right |
| a text document (`.md`) | no toolbox (no ink): undo and redo lead its format bar |

In the toolbox mode the tool bar at the top is a **command bar**: open, save, image, stickers, add a page, search, full
screen, present, settings (New is the tab strip's "+"; recording is a fixed tool of the rail), and entries of ⋮ as
buttons where there is room (share, print, bookmark, favourite; one
place each: ⋮ leaves out what the bar shows, and shows it again when the bar has no room for it). The classic tool
square, the quick tools and the pen pill of full screen are classic only.

**A text document's format bar** (with the toolbox): undo and redo at its start, then the formatting, then the commands
that fit, then » and ⋮. One ladder for both: the inserts go into "+ Insert" first, then the commands of low priority
into », then the headings into one button, then search, full screen and save into » too; only then the row scrolls. At
1366 px search, full screen and save stay. Find and replace adds no button to this ladder: it is the search bar's
second row (Ctrl+H, the bar's replace button, ⋮ → Find and replace; [md-editor.md](md-editor.md)).

## Reading and presenting

The author (2026-10-05): "The reader mode sucks. The menu is half cut off the lower part of the screen and I feel like
we should just reuse the full screen or present mode with a read only / readmode toggle that allows skipping to next
prev page with big touch areas on the left and right side of the screen." So reading is no mode of its own any more
(qt/ui-rework): it is **read only**, a toggle of full screen and of presenting. The reading pill is gone.

- **Read only** (`win.readOnly`; on where `win.readOnlyOffered`: full screen, the compact chrome, presenting): the
  floating toolbox's ⋯ → "Read only", the compact chrome's tools (the classic tool square's popup, phones' full
  screen), **Ctrl+Alt+R** (a shortcut of its own, changeable), and **⋮ → View → Read**, which enters full screen with
  it on (so does Ctrl+Alt+R in a window). While it is on:
  - the page cannot be written on (`DocumentCanvas.readingOnly`): the pen and the fingers scroll, PDF text can still be
    selected, copied and looked up (decided: selecting text writes nothing, and reading is where one copies a quote);
    no ink by accident;
  - the tools are hidden (`win.toolsHidden`: the floating toolbox, the classic tool square and pen pill); the view
    pill (the page number, the zoom and its fits, the page layout) stays, as in full screen;
  - **big tap fields at the left and right edges**: a fifth of the page's width each (at least 48 px), its whole height,
    invisible. A tap there goes to the previous or the next page (in full screen to its top, presenting: the slide); a
    short arrow appears at that edge. The page finds the taps itself (`DocumentCanvas.edgeTapWidth`, `edgeTapped`;
    `CanvasView::edgeTap`): a tap that is a link or opens a covering note does that instead, a swipe or a drag
    scrolls, and taps in a row turn page after page (no double tap there). The fields in `Main.qml`
    (`readingTapFields`) only show the hint and let the presses through (`inputTransparent`);
  - a **lock** in the upper right corner (`readOnlyMark`, where the toolbox floats) says so; a tap on it writes again.
  - It ends with Esc (full screen ends, and read only with it), the lock, Ctrl+Alt+R again, the ⋯ entry, and when full
    screen ends or the home screen is shown (`onReadOnlyOfferedChanged`).
- **The reader chrome** (no HUD: automatic in a tiny window, or chosen in Settings → Display → Controls at this size)
  is reading too: read only with the tap fields; the corner field brings the chrome back, as before.
- **Presenting** (F5): full screen, black around the pages, page by page; writing on the slides stays possible: the
  toolbox floats. The corner field hides and shows it ("present without controls" is presenting with it hidden). Read
  only while presenting: the edges go to the previous and next slide, the pen does not write.

Kept from the reading of before, as settings rather than a pill: **up and down or sideways** and **whole pages**
(⋮ → the view pill's page layout menu: "Scroll sideways", "Stop on whole pages"; the latter is offered while reading
too). The width or the whole page: the view pill's zoom menu. Dropped: the pill itself, its ‹ › (the edges do that)
and its fading.

Up and down, "whole pages" (the setting `snapPages`, as sideways) makes a drag or a fling come to rest on a row of
pages while reading (`ViewController::setSnappingVertically`; not while presenting, which is page by page anyway): a
row taller than the view rests anywhere within it (its top at the view's top at the latest), a fling at its end goes
on to the next row's top, a row that fits rests in the middle. Outside reading, up and down scrolls freely as before.

## Popups beside the rail

The editor opens beside its tool and stays there while something is chosen in it. Two things keep it there (the
author, 2026-10-05: "when I select something on the toolbelt popup the popup moves to the upper left position of the
window"): the rail's buttons are made anew only when what is where changes (an entry, a stack and the entry it shows,
a divider), not when a tool's color, width or line style does (`Toolbox.items`, `syncItems`; the buttons read their
entry from the store); and the editor is placed when it opens (and when it or the window changes size), not bound to
its button: a button that goes away (a tool replaced) leaves it where it is, and it takes the entry's button again
(`Toolbox.buttonFor`, the stack that holds it when its section is folded). A stack's list and the folded fixed tools
close when a tool is taken in them; their buttons stay while they are open.

## Line styles

The samples of a line style (the editor's four buttons, its preview, the ink of a tool on the rail, the classic pen's
options) are drawn by `LineStyles.js`. Two things of Qt's Canvas made them all solid once (the author, 2026-10-05:
"the dashed and dotted line buttons just show a regular line"): `setLineDash()` takes only a JavaScript array, and a
list that came through a model (a Repeater's `modelData.dashes`) is silently ignored; and, like `QPen`, it measures
the dashes in widths of the line, not in pixels, so upstream's `[6, 3]` on a 2.6 px sample of 17 px is one dash. The
short samples use dashes in pixels (at least two dashes or three dots fit) with butt caps; the editor's preview draws
upstream's dashes as the pen does.

## Storage

`ToolboxModel` (qt/src/shell): the entries as JSON in the settings (`toolbox` in the xournalQt part, per device),
written after a pause of 400 ms (a dragged slider writes once), and when the app ends.

```json
{"version":1,"active":"e3","recent":["e3","e1"],"entries":[
 {"id":"e1","type":"pen","color":"#2b2b2b","role":"body","width":1.41,"lineStyle":"plain",
  "fill":{"on":false,"color":"","alpha":128}},
 {"id":"d1","divider":true},
 {"id":"e4","type":"highlighter","role":"keyTerms","color":"#ffe066","width":8.5,"fill":{…}},
 {"id":"e6","type":"shape","variant":"arrow","base":"pen",…},
 {"id":"e5","type":"eraser","variant":"whiteout","width":8.5},
 {"id":"e7","type":"text","font":{"family":"Sans","size":12},"color":"#2b2b2b","role":"body"},
 {"id":"e8","type":"sticky","color":"#fff59d"},
 {"id":"e9","type":"laser","base":"pen","color":"#ff0000","width":2.4},
 {"id":"e10","type":"snip","variant":"lasso"}]}
```

`role` is a palette role: the entry draws with that role's color in the chosen palette (ink; the highlight color for
a highlighter or a shape or laser drawn with it), else with `color`. The entry in hand follows a palette switch while
the tool still has the color the entry gave it. Widths are points, 0.1 to 150. The laser tools have five sizes but no
width of their own: the nearest size is taken. Unknown fields and kinds are dropped; broken JSON gives the first
tools. The last eraser cannot be removed.

`toolbarMode` (`toolbox` or `classic`; Settings → Pen → Tools, with "Back to the first tools…"). While it is not set,
`XQT_TOOLBAR_MODE` gives the default; the shell, canvas and UI tests of before set it to `classic`.

## Code

| Where | What |
| --- | --- |
| `qt/src/shell/ToolboxModel.*` | the entries, dividers, the active entry and the order of use; JSON; add, update, replace, duplicate, remove, move, dividers, prefill, the most recent of a type |
| `qt/src/app/AppToolbox.cpp` | `applyToolEntry`, `entryInHand`, `toolEntryColor`, `takeToolOfType`, `migratedToolbox` |
| `qt/src/app/qml/Toolbox.qml`, `ToolboxPlan.js` | the rail: head, tools, stacks, fixed tools, tail; the plan; carrying a tool; the grip |
| `qt/src/app/qml/ToolEntryButton.qml` | one tool: its icon and a sample of its ink; lifted in hand; the hold, the carrying, the wheel |
| `qt/src/app/qml/ToolEntryEditor.qml` | the editor (and the draft of a new tool) |
| `Main.qml` | where the rail is (`toolboxDocked`, `toolboxFloating`, `toolboxInDock`, `toolboxEdge`), the menus (`toolEntryMenu`, `toolTypeMenu`, `toolboxMoreMenu`), the command bar's promoted entries, the format bar's undo / redo and commands, reading (`win.readOnly`, `win.reading`, `readingTapFields`, `readOnlyMark`) |
| `PhoneDock.qml`, `PhoneToolSheet.qml` | the dock hosts the rail; the sheet "My tools" |
| `qt/src/canvas/ViewController.*`, `CanvasView`, `DocumentCanvasItem.snapVertically` | snapping up and down while reading |
| `CanvasView::edgeTap`, `CanvasInput` (the taps of the mouse, the pen, a finger), `DocumentCanvasItem.edgeTapWidth` / `edgeTapped` | the tap fields of reading |

## Tests

`ToolboxModel.*` and `ToolboxApply.*` (`-L shell`), `ViewSnapping.*` (`-L canvas`), `ToolboxTest.*` (`-L ui`): docked
at the right with undo / redo and the fixed tools, a tap and the tool in hand, the edges per size class, stacks in a
short rail, the classic bar back, the editor, "+", the menu, carrying a tool and the grip, full screen and presenting,
the phone's dock and sheet, the command bar's promoted entries, a text document's format bar, reading.
`ToolboxAudioTest.*` (fake microphone): the record button among the fixed tools and not in the command bar, the
recording pill clear of the rail, docked and floating at the top in full screen (the stack shows the recording), the
phone's sheet; `ToolboxNoAudioTest.*`: without an audio backend nothing offers recording.

## Not done

- Haptic feedback when a tool is lifted (the app has none).
- The highlighter's opacity in the editor (strokes keep upstream's, the author's decision).
- Carrying a whole stack (a folded section) or a divider by hand: they move with the menu of their entries.
