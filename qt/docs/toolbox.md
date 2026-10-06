# The toolbox (qt/toolbox)

The author (2026-10-04): "it feels like just picking up a real pen from a sorted toolbox on a table … this is how
Drawboard works … a plus button like we already have, then the user can select which type of tool should go there and
select color and width etc. … the order of the tools should be user configurable with up/down arrows in the tool
context menu, which also allows changing the tool at the current position or by drag and dropping … drag and drop
only starts after pressing a while, then the user gets visual feedback that we are moving the tool and not the pill."
And: one element in the window and in full screen; undo and redo easy to find; fewer commands hidden behind » on a
wide screen; a reading mode without the edit tools.

The author's decisions: a rail docked to any side of the canvas, **right by default**; the classic tool bar is kept
for one release (Settings → Pen → Tools); the eraser is an entry; the tools are stored per device. Reading and
presenting are two modes over one "tools hidden" view (below).

**One UI (0.8.0, qt/rail-scroll).** The author on 0.7.0, the Fold 7 unfolded: "the user defined tools on the rail are
fully collapsed into a single button while the system tools are expanded to the rest. I don't like that. I want the
user tools to have higher priority and I think scrolling the tools would be good. Maybe we can let the user group
tools into cycle groups themselves if they like? By dragging a tool and holding long over another tool? … A scrolling
rail would help having the same ui on wide desktop or smaller screens since the tool placement and order can be the
same." And: "A tap should open the group list if more than 3 tools in the group." So the rail scrolls instead of
folding (below, "A rail that scrolls"), the user makes groups (below, "Groups"), and one stored arrangement holds the
rail and the top bar (below, "Storage"; the top bar's own UI follows in qt/top-bar).

**The classic tool bar was removed in 0.8.0** (qt/classic-removal; the author, 2026-10-06: "let's get rid of all the
other ui variants we have. Remove the classic toolbar to not drag dead weight as well."). Gone with it: the tool
square of full screen and its quick tools, the pen pill, the colour strip and the widths of the bar (and their ladder
in `ToolBarPlan.js`), the bar's places (two rows, a rail at a side, the bottom), the classic dock of a phone (the tool
in use, the color, the width), the setting `toolbarMode` (Settings → Pen → Tools) and `XQT_TOOLBAR_MODE`. The toolbox
is the only way the tools are arranged; a settings file that chose the classic bar gets the toolbox (below, "After an
update").

## Using it

**My tools.** Each entry is a tool with its settings: a pen (color or palette role, width, line style, filling), a
highlighter, a shape (line, rectangle, ellipse, arrow, double arrow, coordinate system, recognized shapes; drawn with
the pen or the highlighter), an eraser (standard, whiteout, whole strokes; its size), a text box (its font and color),
a sticky note (its pastel), the laser pointer (pen or highlighter), a **snip** (a rectangle or a lasso whose picture goes to the clipboard; a
cycling tool: a tap while it is armed takes the other shape, qt/ui-rework). Dividers group them into sections.

The first start: three pens (body, key terms, warnings) | two highlighters (key terms, definitions) | the eraser | a
line, a text box, a sticky note | the laser pointer. The pens and highlighters take their colors from the **palette
roles** ([color-palettes.md](color-palettes.md)): choosing another palette recolors them. **After an update** from
the classic tool bar (a settings file without a toolbox, whatever its `toolbarMode` says: `classic`, `toolbox` or
nothing; `AppController::migratedToolbox`) the first pen has the pen's color and width of before, the highlighter its
width, the eraser its kind, the text box the font, the shape the one used last; the first pen is in hand at the
start.

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

**Head and tail.** Undo and redo lead the rail (one place: the view pill has them no more while the toolbox is shown);
"+" ends it. They are pinned: they do not scroll.

**The app's tools** are items of the rail like the user's tools (since qt/rail-scroll): at a first start, after the
user's tools and a divider, hand, select (rectangle ↔ lasso), **snip** (rectangle ↔ lasso, one tap away since
qt/copy-tools; its list also holds the snips' resolution, [snip.md](snip.md)) and **mark PDF text ↔ copy handwriting
as text** (a cycle, the "text" group; its list says how PDF text is marked;
[handwriting-search.md](handwriting-search.md)). They are the window's own buttons, lent to a cell of the rail
(`Toolbox.fixedButtons`, the buttons of the app items on the rail): one place each, so the command bar leaves them out.
A tap is the button's tap (select's tap while it is in use takes the other kind, as before); they are carried, grouped
and removed like the user's tools. Held without moving (or a right click): the rail's menu for them — "Options…" (their
own list: select's kinds on all layers, the snips' resolution, how PDF text is marked), Move, Add a tool here…, the
divider, **Remove from the rail** (the item is "not placed" then: the command bar shows it again, and "+" offers "Put
back: Hand" until qt/top-bar's catalog has every item). A finger held on one shows its name above the finger while
held.

**Write on the page, the setsquare / compass, the finger draws and record audio left the rail** (0.8.0): the top bar's
first layout holds them (qt/top-bar). Until that block they are where they were before the toolbox lent them: the
command bar (or its "more tools" in a narrow window), the phone's "My tools" (record under Insert), and in full screen
the end of the floating rail's ⋯ (`toolboxMore_write`, `_geometry`, `_touchDrawing`, `_record`; the setsquare's
curtain and spotlight are ⋮ → View, B and Shift+B). Keys: Ctrl+Alt+M writes on the page, Ctrl+Shift+R records. The
recording and playback pills sit at the top of the page, below the toolbox when it floats at the top.

## A rail that scrolls

The rail shows its items in the order of the arrangement, the same on every screen (the author: "the same ui on wide
desktop or smaller screens since the tool placement and order can be the same"). Nothing folds (0.7.0's `ToolboxPlan`
folded a user section as a whole, so one long section was all or one button, and what its fold freed went to the fixed
tools; it is gone with its stacks and its hysteresis). When the items do not fit, the middle **scrolls** (up and down
at a side, sideways at the top, the bottom and in the phone's dock):

- its view ends through the middle of a cell, so half of the next one shows (`Toolbox.cutFor`: the largest cut at
  the middle of a cell within the room; a room for less than a cell and a half, a phone held sideways, keeps all of
  it); a **fade** (`toolboxFadeStart`, `toolboxFadeEnd`) marks each end that has more;
- a tool taken by a key (P, H, E, T, A, S, …), the sheet, a menu or the rail itself is **scrolled into view**
  (`Toolbox.revealInHand`: the cell of the entry in hand, the group that holds it, or the app tool in use, with half a
  cell to spare);
- **where it was scrolled to is remembered per window class** (`layout/<class>/toolboxScroll`, written 600 ms after
  it stops). A class shown again takes its place back, unless the tool in hand would be out of sight then;
- a tap takes, a **drag at once scrolls**, a hold (400 ms) lifts the tool to carry it; the **wheel** over a gap or an
  app tool scrolls (a sideways rail too), over one of the user's tools it changes its width;
- the floating rail (full screen) is as long as its items, or as long as the room there is, cut the same way
  (`Toolbox.lengthFor`).

Measured off-screen with the touch profile and the first layout (14 cells, 5 dividers): at 1920 × 1080 everything is in
sight; on the Fold 7 unfolded (900 × 1000) every one of the user's tools is in sight and the app tools scroll; the
folded dock (412 × 915) and the phone held sideways scroll.

## Groups

The user makes groups (the author: "by dragging a tool and holding long over another tool"): **carry** a tool (hold,
move) onto another and keep it there; after about 0.6 s the target shows a **ring** (`toolRing`); let go: the two are
a group in the target's place, and the snackbar says "Grouped" with **Undo** (the arrangement of before, restored
whole). Moving on before the ring is a reorder, as before. A tool carried onto a group joins it; a group carried onto
another gives it its members; a whole group can be carried to another place.

A group (`railGroup_<id>`) shows **the member used last** with dots for how many it holds. A **tap** takes that member.
A tap while one of its members is in hand takes **the next** with two or three members, and **opens its list** with
more than three (the author's rule). The list (`toolGroupFlyout`), beside the group towards the page: a tap picks a
member (the one in hand: its editor), held: its menu, held and moved: **carried out** of the group (onto the rail: it
leaves it). The group's menu (held, right click): "Its tools…" (the list), Move, Add a tool here…, the divider,
**Ungroup** (the members back in its place, in their order). App tools group like the user's (select and snip in one
group: a tap goes from one to the other); outside a group their own cycles (select's rectangle ↔ lasso) stay; inside
one the group's tap rule wins and their kinds are in their "Options…".

The ids, members and the member used last are stored with the arrangement (below); taking a member (a tap, a key, the
sheet) makes it the one its group shows.

## Where it is

| Window | Toolbox |
| --- | --- |
| desktop, tablet (full chrome) | docked to its edge, taking its strip (`sideTools` at a side, `toolboxRow` at the top or the bottom); the right by default, also in tablet portrait (a 52 px rail leaves an A4 page well visible) |
| full screen (the compact chrome), presenting with the tools | the same toolbox floating 8 px off its edge, rounded, as long as its tools (it ends above the view pill); ⋯ at its end: present, present without controls, read only, search, settings, leave full screen. On a phone too (since 0.8.0: the classic tool square went): at the bottom upright (the view pill moves above it), at the right held sideways |
| phone portrait | the dock at the bottom, the same rail at the bottom edge (since qt/rail-scroll): undo, redo, the same items scrolling sideways (the one in hand scrolled into view), **My tools** (a sheet: every tool, "Add a tool", the other tools and commands; record audio under Insert), the page number |
| phone held sideways | the same as a rail at the right |
| a text document (`.md`) | no toolbox (no ink): undo and redo lead its format bar |
| replaying the writing | no toolbox, and no command bar, its tab or phone dock either: the play bar only ([timeline.md](timeline.md)) |

The tool bar at the top is a **command bar**, one row: write on the page, the setsquare, the finger draws, open, save,
image, stickers, add a page, record, search, full screen, present, settings (New is the tab strip's "+"; the app tools
on the rail are not in it), and entries of ⋮ as buttons
where there is room (one place each: ⋮ leaves out what the bar shows, and shows it again when the bar has no room for
it). Its tab at the top edge puts it away (a slim strip brings it back); the toolbox stays then.

### The command bar (qt/ui-rework)

The author (2026-10-05): "The main toolbar is now very empty. Populate it with the important new tools." Since
qt/ui-rework the entries of ⋮ shown as buttons where there is room (`ToolBarPlan.PROMOTED`, `promoted: true` in
`Main.qml`) are, in the order they give way back into ⋮ as the bar gets narrower:

| Gives way | Button | Its entry in ⋮ | Offered |
| --- | --- | --- | --- |
| first | **Tags** (`tagsButton`) | Document → Tags… | always |
| | Favourite | Add to favourites | a document with a file in a library |
| | Bookmark | Bookmark this page | where pages can be bookmarked |
| | Print | Print… | always |
| | **Milestone** (`milestoneButton`, the flag) | Document → Save with a message… (Ctrl+Alt+S) | only where the document keeps versions |
| | **Replay** (`replayButton`) | View → Replay the writing | not for a text document |
| | **Read** (`readButton`) | View → Read (full screen, read only; Ctrl+Alt+R) | not for a text document |
| last | Share | Share… | always |

They sit with their kind: Read and Replay after Present (view), Milestone after Save (file), Tags after Favourite
(document). Measured off-screen with a new document (no favourite): everything up to 1920 and down to 860 px; the tags
go into ⋮ at 800, the bookmark at 760, print at 720, the replay at 680; Read and Share stay longest. Below that the
ladder of before goes on (the commands into "more tools").

Weighed and left where they are (one place each):

- **The page number, go to page, the zoom and the fits**: the view pill at the page's corner (also in full screen
  and while reading); a second copy at the top would be the same buttons twice.
- **The reference view / compare beside**: the tab's menu ("Show this document beside", "Open as reference") and the
  page menu; `qt/version-compare` is reworking it (the reference view, the History panel), so the bar waits for it.
- **The version history**: the History panel of the sidebar (its button); the bar has only the milestone, and only
  where versions are kept.
- **Snip**: a tool, so the rail's (an app tool of the rail since qt/copy-tools, and a toolbox entry) and the image button's
  list.
- **Templates**: the add-a-page button's hold (the templates used last, all templates, save as template).
- **Present, full screen, search, share**: already buttons of the bar.

**A text document's format bar** (with the toolbox): undo and redo at its start, then the formatting, then the commands
that fit, then » and ⋮. One ladder for both: the inserts go into "+ Insert" first, then the commands of low priority
into », then the headings into one button, then search, full screen and save into » too; only then the row scrolls. At
1366 px search, full screen and save stay; at 1920 all commands; below 800 px (with undo and redo at its start) the
folded row scrolls a little. (qt/classic-removal: the room the commands take was counted as nothing, so a text document
kept all of them and its row scrolled at 960 px, the lists out of sight; found once the layout tests ran with the
toolbox.) Find and replace adds no button to this ladder: it is the search bar's
second row (Ctrl+H, the bar's replace button, ⋮ → Find and replace; [md-editor.md](md-editor.md)).

## Reading and presenting

The author (2026-10-05): "The reader mode sucks. The menu is half cut off the lower part of the screen and I feel like
we should just reuse the full screen or present mode with a read only / readmode toggle that allows skipping to next
prev page with big touch areas on the left and right side of the screen." So reading is no mode of its own any more
(qt/ui-rework): it is **read only**, a toggle of full screen and of presenting. The reading pill is gone.

- **Read only** (`win.readOnly`; on where `win.readOnlyOffered`: full screen, the compact chrome, presenting): the
  floating toolbox's ⋯ → "Read only" (on a phone too), **Ctrl+Alt+R** (a shortcut of its own, changeable), and
  **⋮ → View → Read**, which enters full screen with
  it on (so does Ctrl+Alt+R in a window). While it is on:
  - the page cannot be written on (`DocumentCanvas.readingOnly`): the pen and the fingers scroll, PDF text can still be
    selected, copied and looked up (decided: selecting text writes nothing, and reading is where one copies a quote);
    no ink by accident;
  - the tools are hidden (`win.toolsHidden`: the floating toolbox); the view pill (the page number, the zoom and its
    fits, the page layout) stays, as in full screen;
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
window"): the rail's buttons are made anew only when what is where changes (an entry, an app item, a group,
a divider), not when a tool's color, width or line style does (`Toolbox.items`, `syncItems`; the buttons read their
entry from the store); and the editor is placed when it opens (and when it or the window changes size), not bound to
its button: a button that goes away (a tool replaced) leaves it where it is, and it takes the entry's button again
(`Toolbox.buttonFor`, the group that holds it). A group's list closes when a tool is taken in it.

## Line styles

The samples of a line style (the editor's four buttons, its preview, the ink of a tool on the rail) are drawn by
`LineStyles.js`. Two things of Qt's Canvas made them all solid once (the author, 2026-10-05:
"the dashed and dotted line buttons just show a regular line"): `setLineDash()` takes only a JavaScript array, and a
list that came through a model (a Repeater's `modelData.dashes`) is silently ignored; and, like `QPen`, it measures
the dashes in widths of the line, not in pixels, so upstream's `[6, 3]` on a 2.6 px sample of 17 px is one dash. The
short samples use dashes in pixels (at least two dashes or three dots fit) with butt caps; the editor's preview draws
upstream's dashes as the pen does.

## Storage

`ToolboxModel` (qt/src/shell): the **arrangement of both bars** (since qt/rail-scroll; before, the user's entries
alone) as JSON in the settings (`toolbox` in the xournalQt part, per device), written after a pause of 400 ms (a
dragged slider writes once), and when the app ends. Each bar is an ordered list of items: the user's tool entries,
the app's items by name, dividers, and groups (an id, its members — tool entries and app items — and the member used
last).

```json
{"version":2,"active":"e3","recent":["e3","e1"],
 "rail":[
  {"id":"e1","type":"pen","color":"#2b2b2b","role":"body","width":1.41,"lineStyle":"plain",
   "fill":{"on":false,"color":"","alpha":128}},
  {"id":"d1","divider":true},
  {"id":"g1","group":true,"last":"e5","members":[
    {"id":"e4","type":"highlighter","role":"keyTerms","color":"#ffe066","width":8.5,"fill":{…}},
    {"id":"e5","type":"eraser","variant":"whiteout","width":8.5}]},
  {"id":"e6","type":"shape","variant":"arrow","base":"pen",…},
  {"id":"e7","type":"text","font":{"family":"Sans","size":12},"color":"#2b2b2b","role":"body"},
  {"id":"e8","type":"sticky","color":"#fff59d"},
  {"id":"e9","type":"laser","base":"pen","color":"#ff0000","width":2.4},
  {"id":"e10","type":"snip","variant":"lasso"},
  {"id":"d2","divider":true},
  {"id":"a1","app":"hand"},{"id":"a2","app":"select"},{"id":"a3","app":"snip"},{"id":"a4","app":"pdfText"}],
 "top":[{"id":"a5","app":"open"},{"id":"a6","app":"save"},{"id":"d3","divider":true},…]}
```

`role` is a palette role: the entry draws with that role's color in the chosen palette (ink; the highlight color for
a highlighter or a shape or laser drawn with it), else with `color`. The entry in hand follows a palette switch while
the tool still has the color the entry gave it. Widths are points, 0.1 to 150. The laser tools have five sizes but no
width of their own: the nearest size is taken. Unknown fields, kinds and app names are dropped; broken JSON gives the
first layout. The last eraser cannot be removed (a group removed leaves it in its place).

- **The app's items** (`ToolboxModel::appItemNames`): the tools hand, select, snip, pdfText, write, geometry,
  touchDrawing, and the commands record, open, save, milestone, share, print, image, sticker, addPage, search, read,
  replay, present, fullScreen, zen, tags, favourite, bookmark, settings, new (the names of `toolArea.slots`). Each has
  **one home**: the rail, the top bar, or nowhere ("not placed", `unplaced()`: what the catalog offers; a second place
  in a file is dropped).
- **The top bar's first layout** (`defaultTopLayout`): open, save, milestone, share, print | image, stickers, add
  page, write on the page | setsquare, the finger draws, record | search, read, replay, present, full screen, Zen |
  tags, favourite, bookmark | settings. It is stored; the top bar's UI does not read it yet (qt/top-bar).
- **The upgrade from 0.7.0** (version 1, `"entries"`): its entries start the rail (the ids, the tool in hand and the
  order of use stay), a divider and hand, select, snip, mark PDF text follow; the top bar gets its first layout.
- **For qt/top-bar**: `items(bar)`, `moveTo(id, bar, index)` (from the other bar, out of a group), `group(id, onto)`,
  `ungroup`, `members`, `shownOf`, `use`, `remove` (an app item: not placed), `unplaced`, `place(name, bar, index)`,
  `idOfApp`, `kindOf`, `barOf`, `resetLayout()` (both bars back to their first layout, the user's tools kept on the rail
  out of their groups), `snapshot()` / `restore()` (an undo).
- "+" adds a tool after the user's last tool (before the app's items that end the rail).

Settings → Pen → Tools: "Back to the first tools…" (`reset()`: the tools and both bars of a first start). The setting
`toolbarMode` and `XQT_TOOLBAR_MODE` of before 0.8.0 are not read any more (a stored `toolbarMode` stays in the file,
unused); the shell, canvas and UI tests run with the toolbox.

## Code

| Where | What |
| --- | --- |
| `qt/src/shell/ToolboxModel.*` | the arrangement: the rail and the top bar (tool entries, app items, dividers, groups), the active entry and the order of use; JSON (version 2, the upgrade of 1); add, update, replace, duplicate, remove, move (also between bars and out of groups), dividers, groups, the app items' homes, prefill, the most recent of a type, the first layout |
| `qt/src/app/AppToolbox.cpp` | `applyToolEntry`, `entryInHand`, `toolEntryColor`, `takeToolOfType`, `migratedToolbox` |
| `qt/src/app/qml/Toolbox.qml` | the rail: head, the items (entries, app items lent to it, groups and their list), tail; scrolling (the cut, the fades, the tool in hand into view, the place per class); carrying a tool, the ring of a group; the grip |
| `qt/src/app/qml/ToolEntryButton.qml` | one tool: its icon and a sample of its ink; lifted in hand; the hold, the carrying, the wheel; a group's face (dots), the ring |
| `qt/src/app/qml/ToolEntryEditor.qml` | the editor (and the draft of a new tool) |
| `Main.qml` | where the rail is (`toolboxDocked`, `toolboxFloating`, `toolboxInDock`, `toolboxEdge`), the menus (`toolEntryMenu` for tools, app items and groups, `toolTypeMenu` with "Put back", `toolboxMoreMenu`), "Grouped · Undo", the command bar's promoted entries, the format bar's undo / redo and commands, reading (`win.readOnly`, `win.reading`, `readingTapFields`, `readOnlyMark`) |
| `PhoneDock.qml`, `PhoneToolSheet.qml` | the dock hosts the rail; the sheet "My tools" |
| `qt/src/canvas/ViewController.*`, `CanvasView`, `DocumentCanvasItem.snapVertically` | snapping up and down while reading |
| `CanvasView::edgeTap`, `CanvasInput` (the taps of the mouse, the pen, a finger), `DocumentCanvasItem.edgeTapWidth` / `edgeTapped` | the tap fields of reading |

## Tests

Since 0.8.0 every UI, shell and canvas test runs with the toolbox (the classic bar's tests moved to it, or went with
it where they tested what is gone: the five widths of the bar, the pen pill, the tool square).
`ToolboxApply.aSettingsFileOfTheClassicToolBarGetsTheToolboxWithTheToolsOfBefore` (`-L shell`): a settings file of
0.7.0 with `toolbarMode` `classic` (or nothing) and no toolbox gets the first tools with the pen, the eraser and the text
box of before, the pen in hand, E and T taking its entries.
`ToolboxModel.*` (`-L shell`): besides the entries, the top bar's first layout, the upgrade of 0.7.0's JSON, one home
per app item (removed: not placed, put back), groups (made, the member shown, carried out, a group of one dissolved,
merged, ungrouped, undone by a snapshot), a group removed, `resetLayout`.
`CopyToolsTest.*` (`-L ui`): the snip button among the app tools of the rail, not in the select list, the text tools'
cycle.
`ToolboxApply.*` (`-L shell`), `ViewSnapping.*` (`-L canvas`), `ToolboxTest.*` (`-L ui`): docked at the right with undo /
redo, the user's tools and the app tools (write, the setsquare and the finger switch in the command bar), a tap and the
tool in hand, the edges per size class, the editor, "+", the menu, carrying a tool and the grip, full screen and
presenting (⋯ with the tools that left the rail), the phone's dock and sheet, the command bar's promoted entries, a
text document's format bar, reading. Scrolling: `aShortRailScrollsAndKeepsItsOrder` (1300 × 600: the order, the cut at
half a cell, the fades, the tool in hand into view by a key and from elsewhere, the wheel over an app tool and over a
pen, a drag scrolls), `theSameOrderAtEverySizeAndTheToolInHandInView` (the Fold 7's 900 × 1000, 1000 × 900, 412 × 915,
915 × 412 and 1920 × 1080, with the touch profile, with and without a phone's insets: the same order, nothing folded,
the tool in hand in view; unfolded every one of the user's tools in sight), `theScrollPositionIsRememberedPerWindowClass`.
Groups: `aToolHeldOverAnotherUntilTheRingMakesAGroup` (no ring: a reorder; the ring: a group, "Grouped · Undo"),
`aGroupCyclesWithThreeAndListsWithFour` (the cycle, the list, picking, carrying out, Ungroup, select and snip grouped).
`ToolboxAudioTest.*` (fake microphone): the record button in the command bar and not on the rail, the recording pill
clear of the rail, docked and floating at the top in full screen (stopped by the pill), the phone's sheet;
`ToolboxNoAudioTest.*`: without an audio backend nothing offers recording.

## Not done

- Haptic feedback when a tool is lifted (the app has none).
- The highlighter's opacity in the editor (strokes keep upstream's, the author's decision).
- Carrying a divider by hand: it moves with the menu of its entries.
- The top bar does not read its arrangement yet, items are not carried between the bars by hand, and "+" is not yet
  the catalog of every item not placed (qt/top-bar). Until then a removed app tool is put back by "+" → "Put back".
