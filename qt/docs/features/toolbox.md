# The toolbox

The user's own tools on a rail beside the page, and the app's tools and commands on the rail and on the top bar, in
one arrangement the user orders, groups and carries between the two. The author: "it feels like just picking up a real
pen from a sorted toolbox on a table … this is how Drawboard works … a plus button like we already have, then the user
can select which type of tool should go there and select color and width etc. … the order of the tools should be user
configurable … drag and drop only starts after pressing a while, then the user gets visual feedback that we are moving
the tool and not the pill." And: one element in the window and in full screen; undo and redo easy to find; the same UI
on a wide desktop and on smaller screens ("since the tool placement and order can be the same"); user tools before
the app's; groups made by the user ("by dragging a tool and holding long over another tool").

Decided with the author: a rail docked to any side of the canvas, **right by default**; the eraser is an entry; the
tools are stored per device; the rail scrolls instead of folding; one stored arrangement holds the rail and the top
bar; the toolbox is the only way the tools are arranged (the classic tool bar is gone:
[history/README.md](../history/README.md)).
Zen hides it with everything else around the page; read only and presenting are apart from it (below).

## Using it

**My tools.** Each entry is a tool with its settings: a pen (color or palette role, width, line style, filling), a
highlighter, a shape (line, rectangle, ellipse, arrow, double arrow, coordinate system, recognized shapes; drawn with
the pen or the highlighter), an eraser (standard, whiteout, whole strokes; its size), a text box (its font and color),
a sticky note (its pastel), the laser pointer (pen or highlighter), a **snip** (a rectangle or a lasso whose picture
goes to the clipboard; a tap while it is armed takes the other shape). Dividers group them into sections.

The first start: three pens (body, key terms, warnings) | two highlighters (key terms, definitions) | the eraser | a
line, a text box, a sticky note | the laser pointer. The pens and highlighters take their colors from the **palette
roles** ([color-palettes.md](color-palettes.md)): choosing another palette recolors them. A settings file without a
toolbox (or with one of another version) gets these first tools; the first pen is in hand at the start.

| Gesture | What it does |
| --- | --- |
| tap | picks the tool up: the tool in hand gets everything it holds (`AppController::applyToolEntry`); it is lifted towards the page (the rail moved to another edge: towards the page at once, never along the rail, where it would leave the view at its start) |
| tap on the tool in hand | its **editor**, beside it towards the page (a sheet on a phone): a preview stroke, the color (the palette's roles, the colors used lately, a hex code, the picker), the width (0.1–150 pt on a log slider, shown in mm, and the five sizes as dots), the line style, the filling (the line's color or another, its opacity), the eraser's kind, the shape and what draws it, the font, the note's pastel. A change is written at once and the tool in hand follows; there is no OK |
| the mouse wheel over a tool | its width, a fifth more or less per notch |
| long press (400 ms, not moved), right click | its **menu**: Edit…, Move up/down (left/right), Replace with…, Duplicate, Add a tool here…, Add/Remove the divider after it, Remove (not the last eraser) |
| long press, then move | **carries it** to another place: it is lifted (larger, with a shadow), its place stays faint, a line shows where it goes, on this bar or on the top bar (below, "The top bar"); let go away from both bars: it leaves them ("Removed · Undo"; the last eraser stays) |
| a drag at once | scrolls the rail (when it scrolls) |
| "+" at the end | **the catalog** (below): the kinds of tools (the editor opens with the last tool of that kind as the start; **Add** puts it at the end and takes it), and every app tool and command on neither bar |
| P, H, E, T | the pen, highlighter, eraser, text box used last (with its settings) |

The rail itself moves only by its **grip** (the dotted cap at its start): dragged towards another edge, that edge is
highlighted, let go: the rail goes there (remembered per window size, `layout/<class>/toolbox`; also ⋮ → View →
Toolbox position).

**Head and tail.** Undo and redo lead the rail (one place: the view pill does not have them while the toolbox is
shown);
"+" ends it. They are pinned: they do not scroll.

**The app's tools** are items of the rail like the user's tools: at a first start, after the user's tools and a
divider, hand, select (rectangle ↔ lasso), **snip** (rectangle ↔ lasso, one tap away; its list also holds the snips'
resolution, [snip.md](snip.md)) and **mark PDF text ↔ copy handwriting
as text** (a cycle, the "text" group; its list says how PDF text is marked;
[handwriting-search.md](handwriting-search.md)). They are the window's own buttons, lent to a cell of the rail
(`Toolbox.fixedButtons`, the buttons of the app items on the rail): one place each; the top bar lends its own the same
way.
A tap is the button's tap (select's tap while it is in use takes the other kind); they are carried, grouped
and removed like the user's tools. Held without moving (or a right click): the rail's menu for them — "Options…" (their
own list: select's kinds on all layers, the snips' resolution, how PDF text is marked), Move, Add a tool here…, the
divider, **Move to the top bar**, **Off the bars (into +)** (the item is "not placed" then: the catalog offers it, ⋮
has it as always). A finger held on one shows its name above the finger while held.

**Write on the page, the setsquare / compass, the finger draws and record audio** are on the top bar in its first
layout (below), in full screen the floating rail's ⋯ (it lists the top bar), and ⋮ → Tools (the
setsquare's curtain and spotlight are ⋮ → View, B and Shift+B). Keys: Ctrl+Alt+M writes on the page, Ctrl+Shift+R
records. The recording and playback pills sit at the top of the page, below the toolbox when it floats at the top.

## A rail that scrolls

The rail shows its items in the order of the arrangement, the same on every screen (the author: "the same ui on wide
desktop or smaller screens since the tool placement and order can be the same"). Nothing folds. When the items do not fit, the middle **scrolls** (up and down
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

With the touch profile and the first layout (14 cells, 5 dividers): at 1920 × 1080 everything is in sight; on the
Fold 7 unfolded (900 × 1000) every one of the user's tools is in sight and the app tools scroll; the folded dock
(412 × 915) and the phone held sideways scroll.

## Groups

The user makes groups (the author: "by dragging a tool and holding long over another tool"): **carry** a tool (hold,
move) onto another and keep it there; after about 0.6 s the target shows a **ring** (`toolRing`); let go: the two are
a group in the target's place, and the snackbar says "Grouped" with **Undo** (the arrangement of before, restored
whole). Moving on before the ring is a reorder. A tool carried onto a group joins it; a group carried onto
another gives it its members; a whole group can be carried to another place.

A group (`railGroup_<id>`) shows **the member used last** with dots for how many it holds. A **tap** takes that member.
A tap while one of its members is in hand takes **the next** with two or three members, and **opens its list** with
more than three (the author's rule). A group with a command in it (on either bar) opens its list on every tap
(nothing runs by accident). The list (`toolGroupFlyout`), beside the group towards the page: a tap picks a
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
| full screen (the compact chrome), presenting with the tools | the same toolbox floating 8 px off its edge, rounded, as long as its tools (it ends above the view pill); ⋯ at its end: present, present without controls (presenting: hide the tools), read only, Zen, search, settings, leave full screen, then **what the top bar holds** (full screen hides it) and **New document** (below, "The top bar"). On a phone too: at the bottom upright (the view pill moves above it), at the right held sideways |
| phone portrait | the dock at the bottom, the same rail at the bottom edge: undo, redo, the same items scrolling sideways (the one in hand scrolled into view) with **"+"** after them (not pinned: its room goes to the tools), the page number |
| phone held sideways | the same as a rail at the right; the page number is in the app bar (the rail's room goes to the tools) |
| a text document (`.md`) | no toolbox (no ink): undo and redo lead its format bar |
| replaying the writing | no toolbox, and no top bar, its tab or phone dock either: the play bar only ([timeline.md](timeline.md)) |

## The top bar

The bar at the top is **the other list of the arrangement** (`ToolboxModel.top`), drawn by the same element as the rail
(`Toolbox.qml` with `bar: "top"`, `topBarPane`; its parts are named `topBar…`, its items `topApp_<name>`,
`topGroup_<id>`, `toolEntry_<id>`): the user's order, dividers and groups, the same on every screen.

- **It scrolls** sideways as the rail does: its view ends through the middle of a cell (half of the next shows), a fade
  marks the end that has more (`topBarFadeStart`, `topBarFadeEnd`), a tap acts, a drag at once scrolls, the wheel
  scrolls (over a tool entry: its width, as on the rail), a hold lifts. Where it was scrolled to is remembered per
  window class (`layout/<class>/topBarScroll`).
- **"+" and ⋮ are pinned at its end** (⋮ last). "+" opens the catalog for the top bar (below). The buttons of the moment
  sit before "+" while they are offered: the emoji while writing, a `.md`'s "Edit as notes", "Open externally" (they
  are not items of the arrangement). Undo and redo lead it only where no rail is shown (`win.layout.undoPlace` "toolBar").
- **Items not offered here are skipped, not removed**: the milestone where the document keeps no versions, the favourite
  outside a library, a text document's ink tools, record without an audio backend, New where the tab strip has "+",
  full screen and present in full screen. A divider that would then lead, end or follow another is left out.
- **Tool entries go on it too** (a pen, a highlighter, …): a tap takes one, the one in hand opens its editor below it.
- **Groups**: as on the rail; but **a group with a command in it** (open, share, Zen, the finger switch, write on the
  page, …: anything that runs rather than being taken in hand) **opens its list on every tap**, so nothing runs by
  accident; a group of tools follows the rail's rule (two or three: the next, more: the list). On both bars.
- **First layout** (`ToolboxModel::defaultTopLayout`): open, save, milestone, share, print | image, stickers, add a
  page, write on the page | setsquare / compass, the finger draws, record | search, read, replay, present, full
  screen, **Zen** | tags, favourite, bookmark | settings. Zen is on it on every screen, phones too (the author: "I think
  zen is helpful put it into the top bar").
- **Settings → Pen → Tools → "Back to the first layout…"** (asked first; `resetLayout()`): both bars as at a first
  start, the user's tools kept on the rail in their order, out of their groups. "Back to the first tools…" also resets
  the tools.
- In Zen and the compact chrome (full screen) it is not shown: the floating rail's ⋯ lists what it holds, in its order,
  the members of a group one by one (`toolboxMore_<name>`, `toolboxMore_<entry id>`), without what ⋯ has of its own
  (present, Zen, search, settings), and **New document** (`toolboxNewItem`).
- Its tab at the top edge puts it away (a slim strip brings it back); the toolbox stays then.

**Carrying between the bars** (one home per item: the rail, the top bar, or neither). Hold an item on either bar until
it lifts, then carry it:

| Let go | What happens |
| --- | --- |
| at a place on the same bar | it moves there (the drop line shows where) |
| over the other bar | it goes there (that bar shows the drop line; `ToolboxModel::moveTo`) |
| held over an item (either bar) until its ring shows (about 0.6 s) | a group in that item's place ("Grouped · Undo") |
| away from both bars (more than a cell and a half off) | it leaves the bars: an app item goes into the catalog ("Search is in + now · Undo"), a tool entry is removed ("Removed · Undo"), a group with its members; the carried one shows "Remove" / "Off the bars" while it is there; the last eraser stays |

The carried item is drawn over everything (it may go from one bar to the other) and along the bar it is over. A
member carried out of a group's list goes the same ways. The item's menu has the same without carrying: **Move to the
top bar** / **Move to the rail** (at its end) and **Off the bars (into +)**.

**The catalog** ("+", `toolTypeMenu`): a new tool of a kind (pen,
highlighter, shape, eraser, text box, sticky note, laser pointer, snip: the editor, then **Add**), then every app tool
and command on neither bar that is offered here, by section (Tools, Insert, View, Document: `catalogSection_<name>`,
`catalog_<name>`). A tap puts it at the end of the bar it was opened from ("+" at the rail's end: the rail; at the top
bar's end: the top bar; "Add a tool here…" in an item's menu: after that item). On a phone it is a sheet (the menus'
sheet). Dragging out of it onto a bar is not built (a tap places, then carry it).

**⋮ is complete** and not customized: every command is in it whether a bar shows it or not (decided: a ⋮ that changed
with what scrolls into sight would hide things unpredictably; nothing can become unreachable by arranging the bars).
Its top: Save as, Share, Print, Find and replace, then Document ▸ (new, open, save, edit as notes, open externally,
bookmark, favourite, …, tags, a milestone), Export ▸, Page ▸, **Tools ▸** (hand, select, snip, mark PDF text, write on
the page, setsquare / compass, the finger draws; image, stickers, add a page, record: `moreCmd_<name>`), View ▸
(search, full screen, present, Zen, read, replay, …), Help ▸, **Settings**.

**Phones**: the app bar hosts the same top bar ([adaptive-layout.md](adaptive-layout.md), "The phone chrome"): a row
of its own under the title upright, beside the title held sideways; ⋮ stays at the app bar's end. Zen is on it there
too.

**A text document's format bar** (with the toolbox): undo and redo at its start, then all the formatting (nothing
folds: no "Insert" menu, no heading button), then the commands: the top bar itself, at the end of the same row
(`formatCommands`; its cells 40 px like the format buttons), and ⋮ pinned at the end. The row scrolls as the bars do:
it ends through the middle of a button, fades at the end that has more, the wheel scrolls it. At 1366 px the formatting
and the first commands are in sight; at about 1800 px and wider everything. (Markdown on a page and the editor beside
the page keep the format bar's own ladder: they have no commands.) Find and replace adds no button: it is the search
bar's second row (Ctrl+H, the bar's replace button, ⋮ → Find and replace; [md-editor.md](md-editor.md)).

Weighed and left where they are (one place each):

- **The page number, go to page, the zoom and the fits**: the view pill at the page's corner (also in full screen
  and while reading); a second copy at the top would be the same buttons twice.
- **The reference view / compare beside**: the tab's menu ("Show this document beside", "Open as reference") and the
  page menu.
- **The version history**: the History panel of the sidebar (its button); the bar has only the milestone, and only
  where versions are kept.
- **Templates**: the add-a-page button's hold (the templates used last, all templates, save as template).

## Zen, read only and presenting

Three switches of their own: full screen, **Zen** and **read only**; Read is Zen and read only together
([zen.md](zen.md) has the whole of it). The author: "we should just reuse the full screen or present mode with a read
only / readmode toggle that allows skipping to next prev page with big touch areas on the left and right side of the
screen."

- **Zen** (`win.modes.zen`): only the page and a faint dot in its lower left corner; everything around the page is hidden
  (`win.modes.hudHidden`: this toolbox, docked or floating, the top bar, the tabs, the sidebar's arrow, the pills). The
  tool in hand keeps writing, P, H, E, T take the tools. The dot's pill: Show controls, Read only, the page number,
  fit the width / the whole page. ⋮ → View → Zen, the top bar's Zen, Ctrl+Alt+Z, the floating toolbox's ⋯;
  automatic in a tiny window (leaving it there is remembered for the class). Esc leaves it.
- **Read only** (`win.modes.readOnly`; anywhere, with or without Zen or full screen; ⋮ → View → Read only, the dot's pill,
  the floating toolbox's ⋯ → "Read only"). The tools stay where they are (Zen hides them, read only does not). While it
  is on:
  - the page cannot be written on (`DocumentCanvas.readingOnly`): the pen and the fingers scroll, PDF text can still be
    selected, copied and looked up (decided: selecting text writes nothing, and reading is where one copies a quote);
    no ink by accident;
  - **big tap fields at the left and right edges**: a fifth of the page's width each (at least 48 px), its whole height,
    invisible. A tap there goes to the previous or the next page (in full screen to its top, presenting: the slide); a
    short arrow appears at that edge. The page finds the taps itself (`DocumentCanvas.edgeTapWidth`, `edgeTapped`;
    `CanvasView::edgeTap`): a tap that is a link or opens a covering note does that instead, a swipe or a drag
    scrolls, and taps in a row turn page after page (no double tap there). The fields (`ReadingFields.qml`,
    `readingTapFields`) only show the hint and let the presses through (`inputTransparent`). In Zen a finger's tap
    in the middle opens the dot's pill (`middleTapped`);
  - **no lock**: the first stroke tried says so once, at the pen, for a moment
    ("Read only — tap the dot to write"; outside Zen where to turn it off; `CanvasView::writingRefused`,
    `readOnlyNote`).
  - It ends with its switches and when the home screen is shown (`onReadOnlyOfferedChanged`).
- **Read** (Ctrl+Alt+R, changeable; ⋮ → View → Read, the top bar's Read): Zen and read only in full screen (a tiny
  window stays a window). The keys again end it (and what it turned on); Esc too.
- **Presenting** (F5): full screen, black around the pages, page by page; writing on the slides stays possible: the
  toolbox floats. "Present without controls" (Ctrl+F5, the Present button held) is presenting in Zen: the dot and its
  pill bring the controls back, Ctrl+F5 hides and shows them. Read only while
  presenting: the edges go to the previous and next slide, the pen does not write; Ctrl+Alt+R while presenting is
  presenting without controls with read only.

Settings rather than a pill of their own: **up and down or sideways** and **whole pages** (⋮ → the view pill's page
layout menu: "Scroll sideways", "Stop on whole pages"; the latter is offered while reading too). The width or the
whole page: the view pill's zoom menu.

Up and down, "whole pages" (the setting `snapPages`, as sideways) makes a drag or a fling come to rest on a row of
pages while reading (`ViewController::setSnappingVertically`; not while presenting, which is page by page anyway): a
row taller than the view rests anywhere within it (its top at the view's top at the latest), a fling at its end goes
on to the next row's top, a row that fits rests in the middle. Outside reading, up and down scrolls freely.

## Popups beside the rail

The editor opens beside its tool and stays there while something is chosen in it. Two things keep it there: the rail's
buttons are made anew only when what is where changes (an entry, an app item, a group,
a divider), not when a tool's color, width or line style does (`Toolbox.items`, `syncItems`; the buttons read their
entry from the store); and the editor is placed when it opens (and when it or the window changes size), not bound to
its button: a button that goes away (a tool replaced) leaves it where it is, and it takes the entry's button again
(`Toolbox.buttonFor`, the group that holds it). A group's list closes when a tool is taken in it.

## Line styles

The samples of a line style (the editor's four buttons, its preview, the ink of a tool on the rail) are drawn by
`LineStyles.js`. Two things of Qt's Canvas would make them all solid: `setLineDash()` takes only a JavaScript array, and a
list that came through a model (a Repeater's `modelData.dashes`) is silently ignored; and, like `QPen`, it measures
the dashes in widths of the line, not in pixels, so upstream's `[6, 3]` on a 2.6 px sample of 17 px is one dash. The
short samples use dashes in pixels (at least two dashes or three dots fit) with butt caps; the editor's preview draws
upstream's dashes as the pen does.

## Storage

`ToolboxModel` (qt/src/shell): the **arrangement of both bars** as JSON in the settings (`toolbox` in the xournalQt part, per device), written after a pause of 400 ms (a
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
  tags, favourite, bookmark | settings. The top bar shows it (above, "The top bar").
- **The JSON** is version 2 only: another version gives the first layout.
- **What the bars use**: `items(bar)`, `moveTo(id, bar, index)` (from the other bar, out of a group: carrying, "Move to
  the top bar"), `group(id, onto)` (also across the bars), `ungroup`, `members`, `shownOf`, `use`, `remove` (an app
  item: not placed; carried away from both bars), `unplaced` (the catalog), `place(name, bar, index)` (the catalog),
  `idOfApp`, `kindOf`, `barOf`, `resetLayout()` (Settings → "Back to the first layout…"), `snapshot()` / `restore()`
  ("Grouped · Undo", "Removed · Undo").
- "+" on the rail adds a tool after the user's last tool (before the app's items that end the rail); on the top bar at
  its end.

Settings → Pen → Tools: "Back to the first layout…" (`resetLayout()`) and "Back to the first tools…" (`reset()`: the
tools and both bars of a first start).

## Code

| Where | What |
| --- | --- |
| `qt/src/shell/ToolboxModel.*` | the arrangement: the rail and the top bar (tool entries, app items, dividers, groups), the active entry and the order of use; JSON (version 2); add, update, replace, duplicate, remove, move (also between bars and out of groups), dividers, groups, the app items' homes, prefill, the most recent of a type, the first layout |
| `qt/src/app/AppToolbox.cpp` | `applyToolEntry`, `entryInHand`, `toolEntryColor`, `takeToolOfType` |
| `qt/src/app/qml/Toolbox.qml` | a bar (`bar`: "rail" or "top"): head, the items (entries, app items lent to it, groups and their list), tail; scrolling (the cut, the fades, the tool in hand into view, the place per class); carrying an item within it and to the other bar (`peer`, `reach`, `dragOver`, `dropHere`, `leaveBars`), the ring of a group; the grip (the rail) |
| `qt/src/app/qml/ToolEntryButton.qml` | one tool: its icon and a sample of its ink; lifted in hand; the hold, the carrying, the wheel; a group's face (dots), the ring |
| `qt/src/app/qml/ToolEntryEditor.qml` | the editor (and the draft of a new tool, for either bar) |
| `Main.qml` and its parts | where the rail is (`ChromeLayout.qml`, `win.layout`: `toolboxDocked`, `toolboxFloating`, `toolboxInDock`, `toolboxEdge`) and the top bar (`topBarPane`, `win.layout.topBarShown`), the format bar's undo / redo, Zen and read only (`ViewModes.qml`, `win.modes`: `zen`, `readOnly`, `readOnlyOn`); `AppButtons.qml`: the window's buttons of the app items (`toolArea.slots`); `MoreMenu.qml`: ⋮ (`moreMenu`, its `CommandItem`s); `ToolboxMenus.qml`: the menus (`toolEntryMenu` for tools, app items and groups, the catalog `toolTypeMenu`, `catalogRows()`, `toolboxMoreMenu` with `topBarCommands()`), "Grouped · Undo", "Removed · Undo"; `ReadingFields.qml`, `ZenDot.qml`, `ZenPill.qml`, `ReadOnlyNote.qml`; `backShortcut` in `WindowShortcuts.qml` ([zen.md](zen.md)) |
| `PhoneDock.qml`, `PhoneAppBar.qml` | the dock hosts the rail; the app bar hosts the top bar (`toolsSlot`) and, held sideways, the page number |
| `MarkdownFormatBar.qml` | a text document's commands (`commandsSlot`, `holdsCommands`): the row scrolls as the bars do |
| `qt/src/canvas/ViewController.*`, `CanvasView`, `DocumentCanvasItem.snapVertically` | snapping up and down while reading |
| `CanvasView::edgeTap`, `CanvasInput` (the taps of the mouse, the pen, a finger), `DocumentCanvasItem.edgeTapWidth` / `edgeTapped` | the tap fields of reading |

## Tests

Every UI, shell and canvas test runs with the toolbox.
`ToolboxApply.aSettingsFileWithoutAToolboxGetsTheFirstTools` (`-L shell`): the first tools, the pen in hand, E and T
taking its entries.
`ToolboxModel.*` (`-L shell`): besides the entries, the top bar's first layout, JSON of another version giving the first
layout, one home
per app item (removed: not placed, put back), groups (made, the member shown, carried out, a group of one dissolved,
merged, ungrouped, undone by a snapshot), a group removed, `resetLayout`.
`CopyToolsTest.*` (`-L ui`): the snip button among the app tools of the rail, not in the select list, the text tools'
cycle.
`ToolboxApply.*` (`-L shell`), `ViewSnapping.*` (`-L canvas`), `ToolboxTest.*` (`-L ui`): docked at the right with undo /
redo, the user's tools and the app tools (write, the setsquare and the finger switch on the top bar), a tap and the
tool in hand, the edges per size class, the editor, "+", the menu, carrying a tool (let go away from both bars: removed,
undone) and the grip, full screen and presenting, the phone's dock ("+" after its items; held sideways the page number
in the app bar), reading. Scrolling: `aShortRailScrollsAndKeepsItsOrder` (1300 × 600: the order, the cut at
half a cell, the fades, the tool in hand into view by a key and from elsewhere, the wheel over an app tool and over a
pen, a drag scrolls), `theSameOrderAtEverySizeAndTheToolInHandInView` (the Fold 7's 900 × 1000, 1000 × 900, 412 × 915,
915 × 412 and 1920 × 1080, with the touch profile, with and without a phone's insets: the same order, nothing folded,
the tool in hand in view; unfolded every one of the user's tools in sight), `theScrollPositionIsRememberedPerWindowClass`.
Groups: `aToolHeldOverAnotherUntilTheRingMakesAGroup` (no ring: a reorder; the ring: a group, "Grouped · Undo"),
`aGroupCyclesWithThreeAndListsWithFour` (the cycle, the list, picking, carrying out, Ungroup, select and snip grouped).
The top bar: `theTopBarShowsTheStoredArrangementAndScrolls` (the stored order, the milestone skipped not
removed, "+" and ⋮ pinned, a move in the store shown, at 1000 px the cut at half a cell, the fades, the wheel, a drag
scrolls and runs nothing, a tap runs), `itemsAreCarriedBetweenTheRailAndTheTopBar` (a pen rail → top with the drop line
there, search top → rail and still searching, a reorder within the top bar, onto an item: the ring and a group, away:
into the catalog and Undo, "Move to the rail"), `aGroupOfCommandsOpensItsListOnATap` (open + save: the list, nothing
ran; Zen in a group runs from the list; hand + select on the top bar cycle), `theCatalogAddsToEitherBar` (its sections,
an app item to the top bar's end and to the rail's end, a new highlighter to the top bar),
`backToTheFirstLayout` (asked, both bars as at first, the tools kept), `zenIsOnTheTopBarAtEverySize` (1920 × 1080,
1366 × 768, 900 × 1000, 1000 × 900, 412 × 915, 915 × 412: the same order, Zen in reach and working; on a phone in the
app bar), `backLeavesZen` (Qt::Key_Back: Zen, Zen with its pill, a sheet first then Zen, Read, presenting without
controls), `inFullScreenTheMoreMenuListsTheTopBarAndNew`, `nothingIsUnreachable` (every app item offered at 1920 × 1080,
1366 × 768, 900 × 1000 and 412 × 915 is in ⋮, and on its bar when placed; ⋮ in sight), `aTextDocumentsFormatBarScrollsWithItsCommands`
(undo / redo first, nothing folded, the formatting before the commands, the cut, the fade, the wheel, ⋮ pinned, all in
sight at 2400 px).
`ToolboxAudioTest.*` (fake microphone): the record button on the top bar and not on the rail, the recording pill
clear of the rail, docked and floating at the top in full screen (stopped by the pill), on a phone in the app bar and ⋮;
`ToolboxNoAudioTest.*`: without an audio backend nothing offers recording (not the top bar, ⋮ or the catalog).

## Not done

- Haptic feedback when a tool is lifted (the app has none).
- The highlighter's opacity in the editor (strokes keep upstream's, the author's decision).
- Carrying a divider by hand: it moves with the menu of its entries.
- Dragging an item out of the catalog onto a bar (a tap places it at the bar's end; then carry it).
- Carrying an item to the top bar in full screen (the top bar is hidden there; ⋯ lists its items).

## On the device

What only a real device, screen or another app can show; walked before a release from the [device checklist](../testing/device-checklist.md).

- [ ] The setsquare and the compass at 15 cm on the Surface at 200 % move and turn smoothly.
- [ ] A fresh profile: snapping off, the pen's side buttons erase; switch snapping on, restart: it stays on.
