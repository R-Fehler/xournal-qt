# The layout for the window's size

Every window of xournal-qt adapts to its size: a wide desktop window, a narrow one, a 2-in-1 or Surface held upright,
a phone (Android now, iOS later). This page describes the foundation that block `qt/adaptive-foundation` built, and
how the later blocks of the [UI audit](ui-adaptive-audit.md) plug into it. The audit has the findings and the
reasons; this page has what the code does.

## One place that knows the size: `AdaptiveLayout`

`qt/src/quick/AdaptiveLayout.h` is a small C++ type, one per window (`win.adaptive` in `Main.qml`). Every QML file
reads the window's size class from there instead of keeping a threshold of its own.

| Property | What |
| --- | --- |
| `sizeClass` | `desktopWide`, `desktopNarrow`, `tabletPortrait`, `phonePortrait`, `phoneShort` or `tiny` (below) |
| `layoutClass` | the class the layout follows: `sizeClass`, or `desktopWide` when "Adapt the layout" is off |
| `widthClass` | `compact` (< 600), `medium` (< 840), `expanded` (< 1280), `wide` |
| `heightClass` | `short` (< 560), `medium` (< 900), `tall` |
| `orientation` | `portrait` (h > w) or `landscape` |
| `phone` | one of the phone classes (phone portrait, phone short, tiny) |
| `roomForSidebar` | the page sidebar fits beside the page (below) |
| `classWidth`, `classHeight` | the size the class was taken from |
| `held` | a pointer is held in the window (mouse button, pen, finger), or QML set `hold` |
| `touchProfile`, `minTarget` | fingers are in use: targets of 48 px, else 40 (below) |
| `mobilePlatform` | Android or iOS (the platform, not the size): one window, no tab dragged out into a window of its own (qt/phone-chrome; written by the tests only) |

### The size classes

Sizes in logical pixels. The first rule that matches wins, in this order:

| Class | Rule | Examples |
| --- | --- | --- |
| `tiny` | w < 360 or h < 360 | split screen, Android's pop-up view, a very small window |
| `phonePortrait` | w < 600 | 412×915 (Galaxy phones, the Fold 7 folded), iPhones |
| `phoneShort` | h < 560 | 915×412 (a phone in landscape), 1280×500 (a short window) |
| `tabletPortrait` | 600 ≤ w ≤ 1280, h ≥ 900, h > w | 960×1392 and 1280×1872 (Surface Pro upright), 864×1488, 720×1232, 900×1000 (the Fold 7 unfolded) |
| `desktopWide` | w ≥ 1280 (and h ≥ 560) | 1920×1080, 1366×768, 1280×800, 1440×912, 1536×816 |
| `desktopNarrow` | 840 ≤ w < 1280 in landscape, and whatever is left | 1024×700, 800×600, 600×800, a portrait window under 900 px high |

The one change against the audit's table (D1): tablet portrait includes w = 1280, because a Surface Pro upright at
150 % is exactly 1280 wide (the audit lists it as a tablet).

**Hysteresis.** When a window edge is dragged, the class stays until the size is 32 px past a limit
(`HYSTERESIS_PX`), so the layout does not flicker at the edge. A jump of more than 64 px at once (the device turned,
the window maximized or snapped) takes the class of the new size as it is. The width and height classes, the
orientation and `roomForSidebar` follow the same rule.

**Never during a stroke.** While a mouse button, the pen or a finger is held in the window, the class does not
change; the new one comes once it is let go, after the release has reached the item it was for. The window's pointer
events are watched through the application's event filters, in front of the canvases' filters (which take the
events of the pages).

### The touch profile

Separate from the size: a Surface in landscape can be used with the fingers, a phone with a mouse. `touchProfile` is
on on Android and iOS, and elsewhere once a finger touched the screen, until the mouse or the touch pad is used again.
The pen changes nothing (a 2-in-1 user writes with the pen and scrolls with a finger). It changes after the pointer is
let go, never under it. The setting **Settings → Touch → Buttons sized for fingers** (`touchProfile`: auto / on /
off) overrides it.

`minTarget` is 48 with the touch profile and 40 without. So far it sizes the page sidebar's switch (Pages, Layers,
Contents, Annotations), the full-screen tab bar (36 px high, arrows 48 wide), the title row and stacked buttons of
the dialogs (below), the sidebar's arrow and the target of the tab that puts the tool bar away. The later blocks size
their own targets with it (audit F14).

## Choices made by hand, per class

The automatic choices apply only while the user has not chosen otherwise **for that class**. A choice made by hand
is stored per class in the app's settings (the `xournalQt` part of `settings.xml`), as `layout/<class>/<what>`:

| `<what>` | Values | Used by |
| --- | --- | --- |
| `sidebar` | `shown`, `hidden` (none: automatic) | this block |
| `chrome` | `compact`, `reader` (none: automatic, the full chrome) | this block |
| `toolbar` | `top`, `twoRowsTop`, `twoRowsBottom`, `railLeft`, `railRight` (none: automatic) | `qt/adaptive-toolbar` (below) |
| `sourceSplit` | the page's share of the height above the Markdown source below it, `0.2` to `0.8` (none: 0.5, a phone 0.4) | `qt/adaptive-panels` (below) |

`app.settings.layoutChoice(class, what)`, `setLayoutChoice(class, what, value)` ("" or "auto" removes it),
`hasLayoutChoices()` and `resetLayoutChoices()` (`SettingsModel`). A change counts as a settings revision, so QML
bindings of the form `(app.settings.revision, app.settings.layoutChoice(…))` follow it. In `Main.qml`,
`win.layoutChoice(what)` and `win.chooseLayout(what, value)` do this for the window's `layoutClass`.

Turning a Surface to portrait gives the portrait choices, and back to landscape the landscape ones.

**Settings → Display → Window size**: "Adapt the layout to the window size" (`adaptiveLayout`, on by default; off:
the desktop layout at every size, the choices are those of `desktopWide`), the class the window is in now and its
size, "Controls at this size" (the chrome choice of this class), and "Reset the layout choices".

## The page sidebar (step 1 of the ladder)

`sidebarShown` is a binding again (it was a start value that the first toggle replaced, F5.1):

- **docked** (beside the page) when this class chose `shown`, or nothing was chosen and `roomForSidebar`: the window
  is at least 1110 px wide (the page keeps ~900 px beside its 210 px), and the class is a desktop one. So a portrait
  tablet and a phone never get it docked automatically: a single A4 page must stay well visible;
- otherwise hidden. The **arrow** at the left edge of the canvas area (`sidebarArrow`, since qt/adaptive-toolbar: it
  replaces the tool bar's Pages button) then opens it as a **drawer** over the page, with the rest dimmed. Picking a
  page (or a chapter, or an annotation) closes it, and so does a tap on the dimmed page. The drawer is for the
  moment: it is not remembered. The button just outside its edge (the sidebar icon) keeps it beside the page in this
  class (`shown`).
- The same arrow sits at the sidebar's edge while it is open ("‹") and closes it: a docked sidebar is then `hidden`
  for this class where it would be shown automatically. Showing it again where there is room clears the choice.
- The arrow: a slim tab at 40 % of the canvas's height (clear of the search bar at the top and the pills at the
  bottom), `minTarget` wide in the touch profile (24 px otherwise). Not in the compact or reader chrome, not while
  presenting, and not while the tool bar is put away (unless the sidebar is open: then it closes it).

`win.showSidebar(shown)` and `win.dockSidebar()` are the functions; `sidebarDocked`, `sidebarAsDrawer` and
`sidebarDrawerOpen` the state.

The drawer (qt/adaptive-panels):
- it **slides** in from the left and out again (180 ms, `win.drawerSlide` 0 → 1), the dimmed page fades with it. Only
  a tap slides it (the arrow, the dimmed page, a page, a chapter or an annotation picked, Esc, the back key); a change
  of the size class takes it away at once;
- **Esc and Android's back key** close it;
- its width (`win.drawerWidth`): 210 px on a tablet and a desktop; on a phone up to 85 % of the window (at most
  360 px: 350 at 412), so the thumbnails are larger (the list is one column as wide as the drawer); a phone held
  sideways 260 px (a page's thumbnail stays shorter than the window);
- Pages, Layers, Contents and Annotations work the same in it; a page, a chapter or an annotation picked closes it,
  choosing a mode does not. On a tablet it stays until tapped away or a page is picked.

## The chrome, apart from the window state

Three separate things (audit D5):

| | What | Set by |
| --- | --- | --- |
| `chromeMode` | `full` (tab strip, tool bar, sidebar; in the phone classes the app bar and the tool dock, below), `compact` (the full-screen chrome: tab dots, the tool square, the pen pill, the view pill), `reader` (no HUD) | the class's choice (else automatic: `reader` in a tiny window, `full` elsewhere), full screen |
| `windowFullScreen` | the window's state (`showFullScreen()`) | full screen (F11) |
| `app.presenting` | black around the pages, page by page | F5 |

- **Full screen** (F11, `fullScreenMode`, as before) is the compact chrome in a full-screen window. Leaving it gives
  the window back its state and ends presenting, as before.
- **Compact** chosen for a class (Settings → Display) shows the same chrome inside the window, without full screen.
  The tool square's popup then ends with "Show the tabs and the tool bar" instead of "Leave full screen".
- **Reader** hides the HUD (`win.hudHidden`: the tool bar, the pills, the tool square, the format bar); the faint mark
  in the lower left corner (the one of presenting) brings the full chrome back. Presenting without controls
  (`cleanPage`) also counts as `hudHidden`.
- The home screen always keeps the tab strip (it is the way back to the documents); in the phone classes the app bar.

The automatic chrome (`win.chromeAuto`, qt/phone-chrome): the reader in a **tiny** window (under 360 px either way:
split screen, Android's pop-up view), `full` everywhere else; "Read" (⋮ → View) is the choice by hand. `chromeSetting`
is the class's choice or the automatic one; `chooseChrome(mode)` stores `""` when the mode is the automatic one (so in a
tiny window "full" is stored). Settings → Display → "Controls at this size" shows `chromeSetting`.

## Menus (`qt/adaptive-menus`)

`AdaptiveMenu.qml` is the menu of the app's main menus: ⋮, the library menu, the card menus of the home screen (and
the page-with-hits menu of a card), the tab menu and the layout menu. `PageMenu.qml` (the page menu of the sidebar
and the page grid, a popup of icons) follows the same rules.

- **Desktop and tablet classes**: a `Menu`
  - as wide as its widest entry (`entryWidth`, a binding over the entries offered; 200 to 420 px, never wider than
    the window minus 16), so no entry is cut off;
  - never taller than the window; what does not fit scrolls, with a scroll bar that shows it (`menuScrollBar`);
  - never over the button it came from: from a button (an anchor up to 96 px high) in the upper half of the window it
    opens below it and at most down to the window's bottom, from one in the lower half above it. A menu opened at a
    press on something big (a card, a page) opens at the press;
  - rows of `minTarget` (48 in the touch profile, 40 without), keyboard navigation and Esc as a `Menu` has them.
- **Phone classes** (the layout class is phone portrait, phone short or tiny; "Adapt the layout" off keeps the menus):
  the menu itself stays closed and **`MenuSheet.qml`** shows its entries, one sheet per window (`menuSheet` in
  `Main.qml`):
  - a bottom sheet, as wide as the window (at most 640 px, centred), at most 85 % of the window high (the rest
    scrolls), above the bottom safe area (`win.safeBottom`, set by `main.cpp` from the window's safe area margins on
    Qt 6.9+);
  - rows of at least 48 px; a check mark for a checked choice, an arrow for a submenu;
  - **a submenu drills in**: the sheet shows its entries, with a back arrow and its title (and deeper: View → Tool
    bar position). A menu with a `title` (the tab menu: the tab's name; a card's menu: the file's name) shows it on
    top;
  - a drag down on the handle or a tap beside it closes it; **Esc and Android's back key** go back a level, and close it
    at the top; the arrow keys, Return and Space work too;
  - a row triggers its entry as the menu would (a checkable one toggles first), and the menu's `aboutToShow`,
    `aboutToHide` and `closed` are sent as if it had opened (the tab menu starts its rename on `closed`);
  - what is not an entry comes along: a separator as a line, a label as a caption, anything else (the layout menu's
    columns row) is borrowed from the menu while the sheet shows it and given back after.
  The page menu is a sheet of its own in the phone classes (`asSheet`: across the bottom, with the handle, buttons of
  `minTarget`).

How a menu uses it:

- Open it with `Popups.openAt(menu, pos)` or `menu.openMenu(pos, anchor)`, not `popup()`: only these know the sheet
  and keep it clear of its button.
- Entries that come and go are `AdaptiveMenuItem`s with **`offered`**, not `visible`: every item of a closed menu
  reads as invisible, and the sheet asks while the menu is closed. A separator that comes and goes gets a `property
  bool offered` too. A submenu is an `AdaptiveMenu` with a `title`, and its `offered` hides its entry.

### The ⋮ menu

Nine entries at the top (at most), the rest one level deeper. Since qt/adaptive-toolbar it holds only what has no
button of its own ("One place for each action", below):

| Top level | Inside |
| --- | --- |
| Save as… (not for text files) | |
| Share… | |
| Print… (Ctrl+P) | |
| Bookmark this page / Remove the bookmark of this page | |
| Add to favourites / Remove from favourites | |
| **Document ▸** | Rename…, Edit anyway (as plain text)…, Open as PDF document, Remove unused images…, Linked from…, Copy link to this page |
| **Export ▸** | Export as plain PDF…, Export for the archive…, Export as Markdown |
| **Page ▸** (not for text files) | Insert pages…, Background of this page…, Page size…, Space for notes…, Start a chapter here… |
| **View ▸** | All open documents (not in the phone chrome: its tab count), Page layout… (where the view pill has no button for it: phone portrait, the compact pill, the phone chrome), Present without controls (Ctrl+F5), Read (only the page: the reader chrome of this size class), Tool bar position ▸ (Top, Two rows at the top, Two rows at the bottom, Left, Right, Automatic for this window size; not in the phone classes: their dock) |

Entries that depend on the document (a `.md`: Open as PDF document, Remove unused images; a text file: no Save as, no
Page) are left out as before. "Markdown source beside the page" is in the menu of the writing button (its long
press). Entries and submenus have icons where an obvious one exists (`AdaptiveMenu.iconName` for a submenu's entry);
the phone sheet shows them too.

## The tool bar (`qt/adaptive-toolbar`)

The author's decisions of 2026-09-26: a single A4 page stays well visible (no side chrome that narrows the page in
portrait); two rows at the top on a portrait tablet; the tools grouped and prioritised for everyone, ⋮ pinned at the
end; what does not fit goes into a "more tools" button next to ⋮, never scrolled away silently.

### Its place, per size class

`win.toolbarLayout` is the class's choice (`layout/<class>/toolbar`, ⋮ → View → Tool bar position), else automatic:

| Layout | Automatic when | What |
| --- | --- | --- |
| `twoRowsTop` | tablet portrait (a 2-in-1 or a Surface upright); any other class that is not a phone when one row would hide a tool that is never hidden (e.g. 800×600, 600×800; back to one row with 32 px to spare) | two rows at the top: the tools on the first, the colors, widths, insert and file buttons on the second (below) |
| `twoRowsBottom` | never (chosen) | the same two rows below the page, closer to the fingertips (like a phone browser's address bar) |
| `top` | every other class (the older setting "top") | one row |
| `railLeft`, `railRight` | the older setting "left" / "right" (Settings of before; not by size) | a column of two at a side, ⋮ pinned at its bottom |

"Automatic for this window size" in the submenu removes the choice. `win.chooseToolbar(layout)` stores `""` when the
layout is the automatic one. The older global `app.toolbarPosition` (top / left / right) is still read as the
automatic place outside tablet portrait.

### What goes where: ToolBarPlan.js

`qt/src/app/qml/ToolBarPlan.js` is a pure function of the room (the bar's width, or a rail's height) and the buttons
offered for the document. `toolArea` in `Main.qml` lays the buttons out by it (they are placed, not in a Layout), and
again when the room, the buttons or the colors change. Groups, in order of use: **tools** (pen/highlighter, eraser,
hand, the finger draws, select, text box, write on the page, sticky note, shapes, setsquare/compass, mark PDF text,
the emoji while writing), **colors**, **widths**, **insert** (image, add a page), **view** (search, full screen,
present, settings), **file** (new, open, save; a `.md`: edit as notes; a text file: open externally). ⋮ and "more
tools" are pinned at the end, outside the part that could scroll.

The bar is a flexible space filler: each group has several forms, and the bar takes the richest that fits, in this
order of compression as the room runs short:

1. everything expanded: all palette colors and "+", the five widths;
2. the widths become **one cycling width button** (a tap: the next width, as the pen pill's; a long press: the five);
3. the colors become **the current color, the recent ones and a palette button**: at least 4 recent colors (used
   last, then the palette's), as many as fit (they are the filler: more room, more colors);
4. low-priority buttons go into **"more tools"**, one by one: New, Open, Save, Settings, Present, Full screen, Edit as
   notes, Open externally, Search, Add a page, Image, Emoji, Mark PDF text, Setsquare/compass, Shapes;
5. the colors become **one cycling color button** (a tap: the next of the first five palette colors; a long press:
   the palette) – phone-sized rooms only;
6. on phones, last: Sticky note, Write on the page, Text box, The finger draws, Select, Hand. Pen/highlighter and the
   eraser always stay.

Two rows: the first row (tools, view) and the second (colors, widths, insert, file) are fitted one after the other,
the second first; the end (⋮, "more tools") sits at the end of the first row. At 960 px (a Surface at 200 %) both rows
show everything; at 720 px (a 2-in-1 at 125 %) the view buttons go into "more tools", the widths become one button and
the colors the recent ones. The plan is deterministic; a bar that grows takes a richer plan only with 24 px to spare
(no flicker at an edge), and nothing changes while a pointer is held (a stroke).

The tools that are **never hidden** above phones (audit D3): pen, highlighter, eraser, hand and the finger draws,
select (rectangle and lasso), the text box and writing on the page, the sticky note, the current color and at least
four more, the width, and ⋮.

**"More tools"** (`moreToolsButton`, », next to ⋮) opens `moreToolsPopup` below the bar (above a bottom bar, beside a
rail): the buttons themselves, each with its name beside it (a tap on the name is a tap on the button), two columns
when there are more than eight. It closes after a button was used, unless the button opened a menu of its own (the
shapes' list), and when a tool is chosen.

**The compact chrome** (full screen) shows the same buttons in the tool square's popup, in six columns (layout
`grid`: nothing goes into "more tools" there; the popup scrolls).

**A text document** being written (a `.md` with its format bar): the tool bar is merged into the format bar (F7.2): ⋮
and "more tools" at the format bar's end (`MarkdownFormatBar.trailing`), all the other buttons (new, open, save, edit
as notes, open externally, search, full screen, present, settings, emoji) in "more tools". One row instead of two.

The tab that puts the bar away sits in the middle of its edge towards the pages (⋮ keeps the end); a rail's at 75 % of
its height. Its target reaches into the pages, `minTarget` deep in the touch profile.

### Cycling buttons

Tools that do almost the same share one button (`ToolCycleButton.qml`, the logic in `ToolGroups.qml`, `win.toolGroups`):

| Group | Variants (a tap goes through them) | Only in its list |
| --- | --- | --- |
| `pen` | pen ↔ highlighter (freehand) | |
| `eraser` | standard ↔ whiteout ↔ whole strokes (`eraserMode`) | |
| `select` | rectangle ↔ lasso | rectangle and lasso on all layers |
| `shape` | line, rectangle, ellipse, arrow, double arrow, coordinate system, recognize shapes (the pen, or the highlighter in hand, draws them) | |
| `geometry` | setsquare ↔ compass | "Take it off the page" (also the × of the geometry pill) |

- A tap on the button while its tool is in use: the next variant. A tap while another tool is in use: its tool with
  the variant used last (remembered per group in the setting `toolVariants`; the eraser's is `eraserMode`).
- The icon is the variant in use (or the one last used); small dots under it say how many there are and which one.
- A long press (or a right click) lists all variants with icon and name, with the group's name on top: to pick one.
- The keys (P, H, E, S, L) take a variant directly; the button follows and remembers it.
- The pen pill of the compact chrome uses the same `pen` button (smaller); the tool square's popup holds the tool
  bar's buttons themselves.
- The shapes menu and the eraser menu are gone. What they held besides variants: the sticky note (a button of its
  own), the setsquare and the compass (their group), snapping to the grid (Settings), the eraser's size (the widths).

The **text box** and **writing on the page** stay two buttons, not a cycling pair: writing on the page is switched on
and off by its button (a tap while it is on ends it), which a cycle cannot do; the text box is a tool like the pen.
The text box is always a Markdown text box now (`win.takeTextBox()` sets `textMarkdown`); the plain text tool is no
longer offered (T takes the Markdown text box). Plain texts in documents are still drawn, and the text box tool still
edits them as plain text (a tap on one: `CanvasView`, "an ordinary text there is edited as it is"). Its long press
(or a tap while it is in use) opens the font: family and size.

### Labels without hover

Every button of the tool bar, the pills and "more tools" says what it is by its icon; where an icon alone was
ambiguous it was replaced (below). Without hover (a finger):

- a finger held on a button (`IconButton`) shows its name (`label`) above the finger while held; letting go then does
  not press it. The mouse and the pen's hover show the tool tip as before; a mouse or a pen held long still presses
  the button on release;
- a button with a long press of its own keeps it (`ownHold`): a cycling button (its list), the writing button (the
  Markdown source beside the page), the text box (the font), Mark PDF text (how it marks), Add a page (Insert
  pages…), Present (without controls), the page layout (its menu), the zoom percentage (the whole page), a color (its
  menu). A menu it opens shows the button's name on top (`AdaptiveMenu.titleShown`);
- "more tools" shows the names beside the buttons; the phone sheet shows the menus' icons and names.

**Icons changed** (in `qt/resources/icons`, Lucide's or drawn in their style; see its README): the finger draws
(`xqt-finger-draw`), mark PDF text (`xqt-mark-text`), the text box (`xqt-text-box`), writing on the page
(`xqt-page-text`), the eraser's whiteout (`xqt-eraser-whiteout`) and whole strokes (`xqt-eraser-stroke`), the page
layout (`xqt-page-single` / `xqt-book-open`), "more tools" (`xqt-tools-more`), and icons for the ⋮ entries.

### The view pill

Undo, redo, the page layout, the page grid, **the contents** (moved here from the tool bar), the page number, and a
small **zoom percentage** (no − / + any more):

- a tap on the percentage: after the platform's double-click time (so a double tap does not flash it) a menu: Fit the
  width (Ctrl+0), Real size 100 % (Ctrl+1), Fit the height (the page's height fills the view), Fit the whole page;
- a double click / double tap, or a long press: the whole page; a right click: the menu at once;
- pinch, Ctrl+wheel, Ctrl+plus / minus / 0 and the middle button zoom as before.

It never runs out of its canvas (since qt/adaptive-panels: 28 px from the canvas's right edge, 8 where that is too
much; beside a reference or the Markdown source it stays in its own half), and it moves up above the reference's
pill where the two would meet. In phone portrait (and tiny) the page layout button is left out: ⋮ → View → Page
layout… opens its menu.

**The compact pill** (`viewPill.compact`, qt/adaptive-panels): in a canvas under 520 px wide (a phone upright, a half
beside the reference or the source): undo, redo, the contents, **the page number as a button** (a tap: all pages, the
page grid button's place), the zoom percentage (its menu as above). No page layout button (⋮ → View → Page layout…
is offered whenever the pill has none), no page grid button, no ‹ › of sideways scrolling (a swipe turns the page).
Under 360 px also no redo (Ctrl+Y) and no separators.

**Pills that meet** (audit F6): the view pill keeps the canvas's lower right corner; the others go above it where
they would meet it: the selection's pill (centred at the bottom; smaller where the canvas is narrower than it), the
back / forward pill and the "shown read-only" note (lower left), the sticky note's pill, and the pen pill of the
compact chrome (at a side; its buttons are 44 px in the touch profile, F6.3). `win.clearOfPills(item, lowY, others)`
does it for the pills of `Main.qml`. (Qt 6.7 crashes when a binding of `y` reads the geometry of items itself through
a function: the result goes through a property of its own, `clearY`.)

### One place for each action

The author's rule: only one way to do things, to reduce menu clutter. No ⋮ entry repeats a button of the tool bar
(or its "more tools"), the view pill or the sidebar; keyboard shortcuts stay. Where the button can be out of sight:

| Action | Its one place | Keys | With the bar put away / in the compact or reader chrome | In the phone chrome |
| --- | --- | --- | --- | --- |
| Pen, highlighter, eraser, hand, the finger draws, select, text box, write on the page, sticky note, shapes, setsquare / compass, mark PDF text, colors, widths | tool bar (tools: cycling buttons) | P H E A S L T I, Ctrl+Alt+M | the tool square's popup (compact); the pen pill (colors, width, pen / highlighter); the strip at the edge brings the bar back | the dock (the tool in use, cycling; colors, width) and "All tools" (every tool and variant) |
| Insert image (was also ⋮ → Page) | tool bar / more tools | I | as above | "All tools" |
| Insert sticky note (was also ⋮ → Page and the shapes menu) | tool bar | | as above | "All tools" |
| Add a page (long press: Insert pages…) | tool bar / more tools | Ctrl+N | as above; ⋮ → Page → Insert pages… is kept (a dialog: several pages, background, size) | "All tools" (hold: Insert pages…) |
| Search | tool bar / more tools | Ctrl+F | | "All tools" |
| Full screen (was also ⋮ → View) | tool bar / more tools | F11 | in full screen: "Leave full screen" in the tool square's popup, Esc | "All tools" |
| Present (was also ⋮ → View) | tool bar / more tools | F5 | the tool square's popup ("Present") | "All tools" |
| Present without controls | ⋮ → View (it differs from Present) | Ctrl+F5; a long press on Present | | ⋮ → View; hold Present in "All tools" |
| Settings (was also ⋮) | tool bar / more tools | Ctrl+, | the tool square's popup; the home screen's settings | "All tools" |
| New, Open, Save | tool bar / more tools | Ctrl+Shift+N, Ctrl+O, Ctrl+S | the tab strip's + | "All tools"; new documents also in the tab overview and the library |
| Edit as notes, Open externally (were also ⋮ → Document) | tool bar / more tools (a `.md`: more tools in the format bar) | | | "All tools" |
| All pages (was also ⋮ → View) | view pill (the compact pill: its page number) | Ctrl+Alt+G | the view pill stays in the compact chrome | the dock's page number |
| Contents overview (was the tool bar) | view pill | Ctrl+Alt+O | as above | the page grid's pill (the dock's page number) |
| Page layout | view pill (long press: the menu); phone portrait and the compact pill: ⋮ → View → Page layout… | | | ⋮ → View → Page layout… |
| Zoom fits | view pill's percentage | Ctrl+0, Ctrl+1 | | the page grid's pill (its zoom %); pinch |
| The page sidebar (was the tool bar's Pages button) | the arrow at the canvas's edge / the sidebar's edge | | not in the compact or reader chrome | the arrow (a drawer) |
| Hide the tool bar (was also ⋮ → View) | the tab on the bar's edge | | the strip at the edge shows it again | – (the dock stays; "Read" hides everything) |
| Tool bar position | ⋮ → View → Tool bar position | | | – (the dock) |
| Snap to the grid (was the shapes menu) | Settings | | | Settings |
| Plain text box (removed) | – (T and the text box make Markdown text boxes; plain texts are still edited) | | | – |
| All open documents | the tab strip's overview button, ⋮ → View | Ctrl+Shift+E | the tab dots (compact) | the tab count of the app bar (a tap; a double tap: the document used before; a long press: the ones used lately) |
| Undo, redo | the view pill | Ctrl+Z, Ctrl+Y | the view pill | the dock |

The reader chrome hides everything; its corner field brings the full chrome back (as before).

## Panels (`qt/adaptive-panels`)

The author's rule stays: a single A4 page stays well visible; nothing at the side narrows the page in portrait.

### The Markdown source panel

The source of Markdown on a page (and of a Markdown text box; the deprecated text flow panel the same) goes where the
window has room for it (`win.sourceAtBottom`, `win.sourcePanel`):

| Where | When | Size |
| --- | --- | --- |
| **beside the page** (right) | desktop wide and narrow, a phone held sideways (915×412), "Adapt the layout" off | 38 % of the window, 360 to 600 px, never more than half of it (`win.sourceSideWidth`) |
| **below the page** | tablet portrait, phone portrait, and a desktop-narrow or tiny window whose area is portrait (600×800) | the page above keeps the whole width; the source takes the bottom half on a tablet, the bottom 60 % on a phone |

Below the page, the **divider** between them (`sourceDivider`, a touch-sized grip; the whole edge takes a drag) moves
the split between 20 % and 80 %; the page's share is remembered per size class (`layout/<class>/sourceSplit`).

Why the split and not a full-screen sheet on a phone: the page formats as one types, and that is the point of the
panel; a sheet would hide it. At 412×915 the page above is 412 px wide (fit to the width) and about 330 px high,
the source below about 490 px (title, format bar, size, and the text). The soft keyboard (qt/compact-chrome) will
cover the lower part of the source; the page stays visible above it.

### The reference split

Side by side where the canvas area is landscape, top and bottom where it is portrait (`referenceSplit.vertical`,
16 px of margin around square). The divider keeps its ratio when that flips. In a half under 480 px wide the
reference's pill is its page number and a ⋮ with the rest (`referenceMoreButton`, `referenceMenu`). See
[reference-view.md](reference-view.md).

### The format bar

`MarkdownFormatBar.qml` is a flexible filler like the tool bar. On a desktop and a tablet it takes the richest form
that fits the room left of its trailing buttons (a text document's » and ⋮), in this order:

1. everything as buttons: ¶ H1 H2 H3 | the six marks | the four lists | code block, table, formula block, image, rule,
   page break;
2. the six blocks go into **"+ Insert"** (`mdInsertButton`, `mdInsertMenu`: an AdaptiveMenu with Code block ▸ the
   languages, Table…, Formula block, Image…, Horizontal rule, Page break);
3. "Insert" without its word (a "+"), where the headings as buttons still fit with it;
4. else ¶ H1 H2 H3 become one button with the level at the cursor and a menu (`mdBlockButton`, `mdBlockMenu`), and
   "Insert" gets its word back if it fits then;
5. only then (a window under ~680 px, 600×800) the row scrolls.

A text document's merged bar (» and ⋮ at its end): all buttons at 1024 px and wider, "¶ ▾ … + Insert" at 800, "¶ ▾ … +"
at 720 (a 2-in-1 upright), scrolling at 600.

The marks and the lists always stay in the row. On a phone the row scrolls sideways (the norm of mobile editors), with
fading edges where there is more (`formatBarFadeLeft`, `formatBarFadeRight`); docking it above the soft keyboard is
qt/compact-chrome's. The level buttons are 40 px wide in the touch profile (F7.4); a finger held on any button shows
its name. The panel's own bar (beside or below the page) follows the same rules for its own width.

### What the later blocks can use

`win.sourceAtBottom` and `win.sourceBottomHeight` (the panel's place; a keyboard can shrink it), `win.drawerWidth`,
`viewPill.compact`, `MarkdownFormatBar.phone` (its scrolling form, the one to dock above the keyboard),
`win.clearOfPills` (for pills that float over the page). The phone chrome (below) took the drawer's width and the
format bar as they are; its dock is a bar of its own (the page ends above it), so no pill needs to go above it.

## Dialogs and sheets: `AdaptiveDialog` (qt/adaptive-dialogs)

`qt/src/app/qml/AdaptiveDialog.qml` is a `Dialog` that fits every window. Every dialog and sheet of the app is one
(Main, HomeView, Settings, the layer and annotation lists, the dialog files, and the former popup sheets: web confirm,
web picture, unused images, Find paper, arXiv and its opt-in, Fuzzy help, Shortcuts).

- **The body scrolls.** What is declared inside the dialog is its body (the default property): it lies in a
  `Flickable` (`bodyFlickable`) with a scroll bar in the right padding, while the title row and the footer stay in
  place. The body's children size themselves as in a plain `Dialog` (`width: dlg.availableWidth`); they must not use
  `anchors.fill: parent`, except with `fillBody` (below).
- **Where it goes** (`placement`) follows `win.adaptive.layoutClass` and the dialog's `kind`:

  | `kind` | desktop, tablet | phone portrait | phone landscape, short, tiny |
  | --- | --- | --- | --- |
  | `form` (default: fields, choices, lists) | `centered` | `fullScreen` | `fullScreen` |
  | `question` (unsaved changes, recovery, share, web confirm, trash, …) | `centered` | `bottom` | `fullScreen` |
  | `card` (a message, a report, "press the keys") | `centered` | `centered` | `centered` |

  - `centered`: `min(preferredWidth, window − 32)` wide, at most the window's height less 48 px, above the soft
    keyboard (`keyboardTop`, the logic `NewDocumentDialog` had), below `safeTop`.
  - `fullScreen`: the whole window below `safeTop` (above the keyboard), no rounded corners, 16 px side padding. The
    title row has × at the left (`dialogCloseButton`: rejects, as Cancel and Esc) and, when the footer holds nothing
    but the confirm button and Cancel, the confirm button at the top right (`dialogConfirmButton`, the text and
    enabled state of the footer's button; a click clicks it). The footer then has no height and is not shown.
  - `bottom`: at the window's bottom edge, at most 640 wide, at most the window's height less 32.
  - Cards stay in the middle everywhere, also in a short window (they are a few lines; their body scrolls if not).
- **Footer buttons that do not fit in one row** (long labels on a phone: "Open without saving", "Import the other
  app's changes") are shown one below the other instead (`footerStacked`), the confirm button on top, Cancel last;
  each does what its footer button does (the stacked button's objectName is the footer button's plus `Stacked`).
- `fillBody: true` with `preferredHeight`: the body takes the dialog's whole height and its own list scrolls (the
  folder chooser, Move / Copy to). Sheets with a list that grows (Find paper, arXiv) instead show the whole list in the
  scrolling body.
- `closeButton: true`: a × in the title row also in the middle of a window (the sheets without a Cancel button).
- A field that gets the keys is scrolled into view, again when the keyboard makes the dialog smaller.
- **Esc and Android's back key** close it (reject). Qt closes a popup on the back key only while the popup has the keys,
  so the base adds a `Back` shortcut (a shortcut of the top-most popup only, so stacked dialogs do not compete).
- For the tests: `placement`, `confirmItem` (the confirm button where it is shown now), `bodyFlickable`.

A new dialog: `AdaptiveDialog { kind: "question"; preferredWidth: 480; title: …; <body>; footer: DialogButtonBox {…} }`,
without `parent`, `modal`, `anchors.centerIn`, `width`, `height`, `x` or `y` (the base sets them).

### Settings

`SettingsPage.qml` keeps its own sheet (sections of fixed rows):

- On a desktop or a tablet: the tabs, in a sheet `min(920, window − 32)` wide and the window's height less 48, above
  the soft keyboard. Each section scrolls.
- On a phone (`phonePortrait`, `phoneShort`, `tiny`): the whole screen, the sections as a list (`settingsSectionList`,
  items `settingsSection<i>`), each opening as a page with a back arrow (`settingsBackButton`, `sectionShown`). Esc and
  the back key go back to the list first, then close. Shortcuts stays the last section.
- Below 600 px (the sheet's own width): the slider, combo and other two-part rows put their label above the control
  (`narrow`); a slider keeps at least 120 px.

### The quick tools of the compact chrome

The tool popup of the tool square gives the tools what is left above "Present" and "Leave full screen" / "Show the tabs
and the tool bar", so both stay inside a short window; the tools scroll.

## The home screen and the tab overview (qt/adaptive-home)

`HomeView.qml` reads `win.adaptive` (`layoutClass`, `orientation`, `touchProfile`, `minTarget`); the phone classes
follow the layout class, so "Adapt the layout" off keeps the desktop home screen.

**The switch** (`switchBox`, the same at every size): the library's **name** (`libraryPageButton`, its page; the
library icon beside it outside the phones; elided when long), its **▾** (`libraryMenuButton`, the libraries menu), then
three icons: **Recent** (clock), **Favourites** (★, `favouritesChip`) and **Bookmarks** (ribbon). One rule for their
words: beside the icons only where the header has room for every button and the words (`roomy`, e.g. 1920 px);
everywhere else the icons alone, with their word in a tip on hover and while a finger is held on them.

The star is the Favourites filter as the chip was: a **toggle**, not a page of its own. On the Library and the
Bookmarks page it shows only the starred documents of the whole library (combined with Show and the search); it is
filled and marked yellow while on. On Recent (which it does not filter) a tap shows the library's favourites. It is
not in View: one place per action.

**The header's ladder.** One row, as many actions as buttons of their own as fit:

- **expanded** (`expanded`: the row has room for every button with a search of 300 px, e.g. 1920 px): the switch,
  the search, Last page, New ▾, Import ▾, New folder, Flat, Show, Sort, − and +, Settings. The need is measured from
  the buttons (`expandedNeed`), not a fixed width.
- **grouped** (a narrower desktop window: 1280, 1366, the tablets): the switch, the search, **"+"**
  (`newDocumentButton` with the plus icon: New document…, New Markdown file… or New text document…, New text file…,
  Import files…, Import a folder…, New folder…; on Recent: Open a file…) and **View** (`homeViewButton`, the sliders
  icon, marked while it shows less than everything: All documents at once, Kinds of files shown ▸ (the Show switches,
  which leave the menu open), Sort ▸, Open documents where they were left off, and the size of the cards − +, which
  leaves it open too), Settings. Where even that leaves the search less than its smallest width, the search moves to a
  row of its own (`searchOwnRow`).
- Only one of the two is there at a time: no action in two places. Both menus are `AdaptiveMenu`s (sheets on phones),
  and so are Import ▾ and Sort of the expanded row, the Show button's menu (`showPopup`), and the menu of a bookmark
  in the Bookmarks view.

**Phones** (`phoneLayout`):

- **"+" floats** at the bottom right (`newDocumentFab`, 56 px, above the safe area), not in the header; the snackbar
  moves above it. While items are selected it goes.
- **Upright** (`portraitPhone`): the header is **one row**: the switch (the library's name takes what is left, about
  110 px at 412 with the touch profile), View, Settings (44 px each). Below it the search, then the breadcrumbs.
- **Sideways** (`shortLayout`, phone short, or tiny in landscape): **one header row**: the switch (the name at most
  180 px), the **breadcrumbs** (`crumbBar` moves into the header), the search, View, Settings. Cards about as high as wide (`cardHeight`: 0.8 × the width +
  the title, instead of 1.2 ×).
- At least two columns of cards (`fewestColumns`, also for − and +), names on up to two lines in narrow cards
  (`twoLineName`), the card's ⋮ 44 × 48 with the touch profile.

**Breadcrumbs** (`crumbArea`): they never widen the page (the area has no width of its own and clips). When they do
not fit, the last folder comes first (up to 60 % of the room), then the library's name with what is left (elided,
at most half; left out below 56 px), then as many folders before the last as fit; the rest is **"…"**
(`crumbEllipsis`), which opens a menu of those folders (`crumbMenu`). A too long last name is elided at its end.
Dragging cards onto a crumb moves them there, as before.

**The selection bar**: the whole row (Select all, Open, Copy to…, Move to…, Remove from list, Trash…) where it fits
(`selectionFull` is measured while hidden); on a phone, or where it does not fit, the top bar keeps × and the count,
and the actions go into **a bar at the bottom** (`selectionActionBar`: Open, Copy to, Move to, Trash, and More with
Select all and, on Recent, Remove from list), icons above their words, as Android's file apps have them.

**The tab overview** (`TabOverview.qml`):

- Below 600 px (`widthClass` compact) the header wraps: the count, extended search, New, Close all and × in one row,
  the search across the width below it (`narrowHeader`). The count elides; nothing forces a width, and the search's
  placeholder elides instead of running under Fuzzy and Names.
- Columns: cells of at least 280 px on a desktop, 170 on a phone upright (so at least two columns), 210 held sideways
  (four at 915 px). The cells are as high as the **tallest current page of the open documents** needs
  (`TabManager::tallestPageAspect`, height / width, 0.5 to 1.6) instead of 1.25 × their width; held sideways never
  higher than the grid.
- On a phone it opens from the tab dots of the compact chrome (a tap between their arrows; with more than 12 documents
  the count "3 / 14" is there instead) and from the tab strip's overview button.

**Labels without hover**: the icon buttons of the home screen are `IconButton`s, which get the long-press label of
`qt/adaptive-toolbar` when it is merged (their `tip` until they have a `label`). The home screen's own buttons (the
switch's icons and ★, ▾, the floating "+") show their word while a finger is held on them.

## The phone chrome (qt/phone-chrome)

The author's decisions: one place per action, clear icons without hover (a held finger shows the name), a single A4
page well visible, and in phone portrait a **bottom tool dock**. The main target is the Galaxy Fold 7 folded (412 × 915,
phone portrait) and unfolded (900 × 1000: tablet portrait, which keeps the tablet's layout: the tab strip and two tool
rows), and very small or slim desktop windows of the same classes.

In the phone classes (`win.phoneLayout`: phone portrait, phone short, tiny, by the layout class) the full chrome is the
**phone chrome** (`win.phoneChrome`): no tab strip, no tool bar, no pen pill and no view pill, but

**The app bar** (`PhoneAppBar.qml`, `phoneAppBar`, 48 px below the status bar, `safeTop`):

| Where | What |
| --- | --- |
| left | the library (`phoneHomeButton`) |
| middle | the document's title (`phoneTitle`, elided in the middle, ● when it has changes) with small dots for the open documents under it (`phoneTabDots`; more than 12: "3 / 14"). A **swipe along the bar** (`phoneTabSwipe`, 40 px): to the left the next document, to the right the previous one (round the ends); only on the bar, the page keeps every touch |
| right | the **tab count** (`phoneTabCount`, the number in a square): a tap shows all open documents (after the double-tap time, like the zoom %, so a double tap does not flash them); a **double tap** goes back to the document used before (Alt+Tab: `app.previousUsedTab()`, the order of use kept by `TabManager::usedOrder`); a **long press** lists the documents used lately, the current one first (`recentTabsMenu`, a sheet) |
| far right | ⋮, the tool bar's own (`toolEnd` goes into `phoneAppBar.moreSlot`) |

New documents come from the overview ("+") and the library. Ctrl+Tab and Ctrl+Shift+Tab work as before. On the home
screen of a phone class the app bar stays in every chrome (the way back to the documents): the home button is marked,
the title is the document behind the home screen (greyed; a tap goes back to it), no ⋮.

**The tool dock** (`PhoneDock.qml`, `phoneDock`) instead of the tool bar and the pills, one row of cells a finger wide
(48 px; less in a small window, at least 36):

| Cell | What |
| --- | --- |
| the tool in use (`dockToolButton`) | a cycling button of its group (pen / highlighter, the eraser's three, select, shapes): a tap takes the next variant, a long press lists them; a tool without variants is its tool bar button (the hand; the text box: a tap again, the font; mark PDF text: how it marks). Not in a text document |
| All tools (`dockToolsButton`) | the sheet of every tool (below) |
| the color (`colorCycleButton`) | the tool bar's cycling color button: a tap the next of the first five colors, a long press the palette as a **sheet** |
| the width (`widthButton`) | the cycling width button: a tap the next width, a long press the five as a **sheet** |
| undo, redo (`dockUndoButton`, `dockRedoButton`) | as the view pill's |
| the page number (`dockPageButton`) | all pages (the page grid); its pill has the **contents** and the **zoom %** there (its fits as a sheet; a fit goes back to the page), and no − / + (the pinch sets the columns) |

- Phone portrait: at the bottom, above the navigation bar (`win.safeBottom`), in the window's footer: the page ends
  above it and keeps the whole width.
- Held sideways (phone short, or a tiny window in landscape; `win.dockVertical`): a **rail at the right side**, the
  same cells from the top down, below the app bar. A phone in landscape lacks height, not width. The page, the
  Markdown source beside it and the reference end at the rail (`win.dockRail`).
- The colors and widths are the tool bar's `ColorStrip` and `WidthStrip` in their "single" form: `toolArea.apply()`
  puts them into `phoneDock.colorSlot` and `widthSlot` while `win.phoneChrome` is on.

**All tools** (`PhoneToolSheet.qml`, `phoneToolSheet`, a `BottomSheet`): every tool and every variant as a cell with
its icon and its name under it (`toolCell_<group>_<variant>`, `toolCell_<button>`), in sections: Write and draw (pen,
highlighter, the eraser's three, hand, the finger draws, text box, write on the page, sticky note, mark PDF text, the
emoji while writing), Select (the four), Shapes (the seven), Setsquare and compass, Insert (image, add a page), Document
and view (search, present, full screen, settings, new, open, save, edit as notes, open externally). One tap takes it
and the sheet goes; a long press on a button with a long press of its own does that (the text box: the font; Add a
page: Insert pages…; Present: without controls; Write on the page: its source). This sheet is the phone's "more tools"
(») and the tool square's popup: the phone chrome has neither.

**Sheets**: `BottomSheet.qml` is the sheet of the phone classes that is not a menu: as wide as the window (at most
640 px), at most 85 % high (the rest scrolls), above `safeBottom`, the handle of `MenuSheet`, Esc and the back key
close it. The palette (`colorPalette`) and the widths (`widthChoices`) of the cycling buttons are sheets of the same
form in the phone classes (their `asSheet`); the menus were already (`MenuSheet`).

**The reader**: automatic only in a tiny window (`chromeAuto`); elsewhere by hand (⋮ → View → Read). No HUD; the
corner field brings the chrome back (and in a tiny window stores "full" for that class).

**The corner field** (`presentCornerMark`) of presenting and of the reader: a 48 px target in the lower left corner of
the page. While the tools show (presenting with controls: `highlighted`) it is clearly there: an accent-colored dot in
a ring; it **pulses** once when presenting (or the reader) starts. While they are hidden (without controls, the reader)
it is a faint grey dot (the pointer or the pen over it makes it clearer). Its name (`labelText`): "Hide the tools" /
"Show the tools", as a tip on hover and while a finger is held on it (letting go then does not tap it).

**Android and iOS** (`win.adaptive.mobilePlatform`, the platform, not the size): one window. No tab is dragged out of
the tab strip into a window of its own (`TabStrip.undockable`), and the tab menu has no "Move to a window of its own".
Libraries already open in the same window there (`app.libraryWindows`). Moving tabs about stays.

**The library's top**: the row of breadcrumbs is left out where it would only repeat the library's name (the folder is
the top, every size; `crumbBar.needed`), unless it shows the library importing or indexing.

**Changing the fold**: the jump from 412 × 915 to 900 × 1000 (and back) is more than 64 px, so the class changes at
once (the foundation's jump rule); the phone chrome and the tablet's layout switch cleanly, and the choices of each
class are kept apart.

Hooks for qt/safe-areas-keyboard: the app bar takes `safeTop`, the dock `safeBottom` (at the bottom and at the rail's
end); a left or right cut-out (`safeInsets` on all edges) would go into the rail's side and the app bar's ends. The
dock is the footer of the window: a soft keyboard can hide it (or dock the format bar above the keyboard in its
place) without moving anything over the page. The remaining small targets (F14) and the plain menus are that block's.

## The collapse ladder (what the later blocks build)

The author's decisions of 2026-09-26 on the audit's proposals:

| Step | When (automatic) | What changes | Block |
| --- | --- | --- | --- |
| 0 | desktop wide, room for the sidebar | everything as today; the tool bar grouped (colors, widths), ⋮ pinned (**done**) | `qt/adaptive-toolbar` |
| 1 | window < ~1110 px, or tablet portrait | the sidebar is a drawer (**done**; the slide, Esc and the phone width: qt/adaptive-panels); the Markdown source below the page in portrait, the reference top and bottom (**done**) | this block, `qt/adaptive-panels` |
| 1b | tablet portrait (a 2-in-1 or Surface upright) | **two tool rows** at the top by default, all important tools shown; "two rows at the bottom" (closer to the fingertips) as the class's choice; no side chrome that narrows the page: an A4 page stays well visible (**done**) | `qt/adaptive-toolbar` |
| 2 | phone portrait (w < 600) or short (h < 560) | the phone chrome in the window: the app bar (the title, tab dots, the tab count), a **bottom tool dock** in phone portrait and a rail at the side in landscape (**done**); dialogs and menus as sheets (**done**) | `qt/phone-chrome`, `qt/adaptive-menus`, `qt/adaptive-dialogs` |
| 3 | **tiny only** (w or h < 360) | the reader chrome, automatically; everywhere else "Read" is a manual choice (**done**) | `qt/phone-chrome` |

## How a later block plugs in

- Read the class from `win.adaptive` (`sizeClass`, `phone`, `widthClass`, …), never from `win.width`. A component's
  own width (e.g. a dialog's `availableWidth`) is still fine for its own inner layout.
- An automatic choice is a binding: `property bool x: choice !== "" ? choice === "…" : auto`, where
  `choice = win.layoutChoice("<what>")`. A toggle by hand calls `win.chooseLayout("<what>", value)`, and stores
  `""` when the value equals the automatic one. Never assign to the bound property.
- New `<what>` keys: plain words; document them in the table above. `toolbar` is reserved for `qt/adaptive-toolbar`
  with the values listed there.
- Sizes of targets: `win.adaptive.touchProfile ? win.adaptive.minTarget : <the desktop size>`, or `minTarget`
  directly where 40 on the desktop is fine.
- Hiding HUD items: add `!win.hudHidden` to their `visible`; the chrome's own items test `win.chromeMode`.
- Drag gestures of QML that must not see the layout change under them can set `win.adaptive.hold`.

### Thresholds not moved yet

These still keep a width of their own. They are not like-for-like replacements of a class, and belong to the blocks
that rework their screens:

| Where | Threshold | Block |
| --- | --- | --- |
| `NewDocumentDialog.qml` | its body < 520 → one column (its own width: fine as it is) | – |
| `ShortcutSheet.qml`, `AppendPages.qml` | their own widths (620, 260): fine as they are | – |

The panels' own thresholds (qt/adaptive-panels) are widths of the canvas or the half they are in, not of the window,
so they are not size classes either:

| Where | Threshold | What |
| --- | --- | --- |
| `Main.qml` `sourceAtBottom` | the class (tablet portrait, phone portrait), or a portrait area in desktop narrow / tiny | the Markdown source below the page |
| `Main.qml` `sourceSideWidth` | `min(600, max(min(360, area / 2), 0.38 w))` | the source beside the page |
| `Main.qml` `drawerWidth` | 210; phone: `min(360, 0.85 w)`; phone held sideways: 260 | the sidebar as a drawer |
| `Main.qml` `viewPill.compact`, `tight` | canvas < 520, < 360 | the compact view pill |
| `ReferenceSplit.qml` `vertical` | the area h > w (16 px margin) | top and bottom |
| `ReferenceSplit.qml` `narrow` | the reference's half < 480 | the pill: page and ⋮ |
| `MarkdownFormatBar.qml` | the room of its row against the widths of its forms | Insert menu, block menu, scrolling |

## Tests

- `SizeClasses.*` (`qt/tests/ui/AdaptiveLayoutTest.cpp`): the class of each audit size, the edges in their order,
  the hysteresis.
- `AdaptiveLayoutTest.*` (label `ui`, about 15 s): the real window at 1920×1080, 1280×800, 960×1392, 412×915 and
  915×412 (class, sidebar, no control of the home screen or the document outside the window), the sidebar choices
  per class and their reset, the drawer, "Adapt the layout" off, the hysteresis and a held pointer, the touch
  profile, and the chrome apart from the window state. What a later block fixes is listed as known in the test
  (`knownOutside`, `expectLater`): it prints `[ KNOWN ]` with the block's name, and `[ NOW HOLDS ]` once fixed, so
  that block makes the check strict.
- `AdaptiveLayoutTest.menusFitAtFiveSizes` (about 15 s): at the same five sizes, ⋮ (its four submenus at 1280×800),
  the page menu, the library menu and a card's menu lie inside the window, no taller than it, no entry cut off, not
  over their button; ⋮ has at most 12 entries at the top and needs no scrolling in desktop wide; in the phone classes
  they open as the sheet, at the bottom, with rows of 48 px. `AdaptiveLayoutTest.menusAreSheetsOnPhones`: the drill-in
  (two levels), the back arrow, Esc a level up, a row that triggers its entry, the layout menu's columns row borrowed
  and given back, the tab menu's rename after the sheet closed.
- `XQT_UI_ADAPTIVE=1 ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.allSizes*'`: the same checks (with the menus
  and all submenus) at all 18 sizes of the audit (about 70 s), with a `[ WALK ]` line per screen (outside, hidden by
  scrolling, small).
- `XQT_UI_ADAPTIVE=1 ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.allSizes*'`: the same checks at all 18 sizes of
  the audit (about 15 s), with a `[ WALK ]` line per screen (outside, hidden by scrolling, small).
- `AdaptiveLayoutTest.dialogsFitTheWindow` (about 5 s): eight dialogs at 1920×1080, 1024×700, 1280×500, 412×915 and
  915×412: inside the window, the confirm button too, every control of the body reachable by scrolling it to its end
  and none wider than the window, and the placement of its kind. `aPhoneSheetAndTheBackKey`: × and the confirm button
  of a full-screen sheet, the footer back in a wide window, Esc and the back key. `settingsOnAPhoneAreAListOfSections`
  and `quickToolsFitAShortWindow`.
- `XQT_UI_ADAPTIVE=1 ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.allSizesDialogs'`: the same for all 48 dialogs
  (document, library, settings) at the 18 sizes (about a minute). `XQT_UI_DIALOG_SHOTS=<folder>` saves a picture of
  each (without the dialogs' backgrounds off-screen, except full-screen sheets).
- The tool bar (qt/adaptive-toolbar), in the same checks at the five sizes (and all 18 with `XQT_UI_ADAPTIVE=1`): ⋮
  shown and outside anything that scrolls; the tools that are never hidden shown (on phones: shown or in "more
  tools"); the colors (the current one and at least 4 more, above phones) and the width; "more tools" only when
  something is in it; the view pill inside the window; two rows with nothing in "more tools" at 960×1392.
  `twoRowsFitAt720`, `toolBarPlaceIsChosenPerSizeClass` (two rows at the bottom, remembered per class, the rail with ⋮
  at its bottom, "Automatic"), `colorsAndWidthsTakeTheRoomThereIs` (all at 1920, the width button and the recent
  colors at 1280, the cycling color button at 412: tap and long press), `moreToolsHoldsWhatDoesNotFit`,
  `sidebarArrowOpensAndCloses`, `viewPillWithContentsInsideAndClearOfTheReference` (five sizes, a reference open).
  `XQT_TOOLBAR_SHOTS=<folder> ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.toolBarPictures'` saves pictures of
  the layouts (about 20 s).
- The panels (qt/adaptive-panels), label `ui`: `sourcePanelBesideOrBelowThePage` (the five sizes: beside, below at
  half, below at 60 %; the page's width; the view pill inside it), `sourceDividerIsDraggedAndRememberedPerClass`,
  `referenceSplitFollowsTheAreaAndItsPillsStayApart` (the five sizes: orientation, the ratio kept, both pills inside
  their halves and apart, the narrow pill and its ⋮ sheet), `compactViewPillOnAPhone`, `sidebarDrawerKeysAndPhoneWidth`
  (Esc, the back key, 85 %, larger thumbnails, the modes), `formatBarFoldsIntoInsertInsteadOfScrolling` (no
  scrolling at 1920, 1280, 960, 800, 720; the Insert menu fits and its rule goes in; 40 px headings with touch; a
  phone scrolls with fading edges), `pillsKeepClearOfTheViewPill` (selection, back / forward, the pen pill).
- `MainWindowTest.zoomPercentageTapDoubleTapAndHold`, `cyclingToolButtons`, `theEraserButtonCyclesHowItErases`,
  `theGeometryButtonPutsTheSetsquareOnThePage`, `aFingerHeldOnAButtonShowsItsName`.
- The home screen (`checkHomeScreens`, part of `classesSidebarAndControlsAtFiveSizes` and of the full walk): each of
  its three pages, a deep empty folder (the breadcrumbs: "…" on a phone, the last one whole), everything selected (the
  bar at the bottom on a phone) and the tab overview lie inside the window, and no row of the home screen is wider
  than it; wherever the header is grouped it is one row with the name, ▾, the three icons, View and Settings (at
  412 × 915 the search below); at 915 × 412 the breadcrumbs and
  the search are in the header. The menus test opens "+" and View (sheets on phones) and the crumbs' "…".
  `theHomeScreensPlusAndViewMenusWork` (about 20 s) triggers every entry of "+" and View and the star at 412 × 915
  and 1024 × 700;
  `theTabOverviewOpensFromTheTabDotsOnAPhone`.
- The phone chrome (qt/phone-chrome), `PhoneChromeTest.*` (label `ui`, about 30 s): the app bar and the dock at 412 ×
  915, 915 × 412 and 340 × 700 with a bottom safe area of 24 px (inside the window, above it, the rail at the side),
  every tool that is never hidden in the dock or the sheet of all tools; a swipe on the bar; the tab count's tap (after
  the double-tap time), double tap (A → B → C: back to B, and to C) and long press (the sheet of the ones used lately,
  in that order); the palette, the widths and all tools as sheets at the bottom, a variant from the sheet in the dock;
  the page number's grid with the contents and the zoom; the reader automatic only in a tiny window; the corner field
  (highlighted, pulse, faint, its names); no tab dragged out with `mobilePlatform`; no breadcrumbs at the library's
  top; the Fold 7 folded and unfolded (900 × 1000 and 960 × 1392 keep two tool rows). In the checks at the five sizes
  and the full walk, `checkPhoneChrome` replaces the tool bar's checks in the phone classes. `Tabs.theOrderOfUse`
  (label `shell`): the order of use.
- `SettingsModelTest.layoutChoicesPerSizeClass` (label `shell`): the storage.
- The audit's own walk (`XQT_UI_AUDIT`, pictures and `report.tsv`, now with the class) shares the walker
  (`qt/tests/ui/LayoutWalk.h`). Its screens `allTools`, `recentTabs` and `dockPages` show the phone chrome's sheets;
  `presenting` is another name for `chrome`.
