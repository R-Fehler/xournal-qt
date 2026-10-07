# The layout for the window's size

Every window of xournal-qt adapts to its size: a wide desktop window, a narrow one, a 2-in-1 or Surface held upright,
a phone (Android now, iOS later). This page describes the one place that knows the size, the choices made by hand per
size class, and what each part of the window does at each size.

## One place that knows the size: `AdaptiveLayout`

`qt/src/quick/AdaptiveLayout.h` is a small C++ type, one per window (`win.adaptive` in `Main.qml`). Every QML file
reads the window's size class from there instead of keeping a threshold of its own.

| Property | What |
| --- | --- |
| `sizeClass` | `desktopWide`, `desktopNarrow`, `tabletPortrait`, `phonePortrait`, `phoneShort` or `tiny` (below) |
| `layoutClass` | the class the layout follows: `sizeClass`, or `desktopWide` when "Adapt the layout" is off |
| `widthClass` | `compact` (< 600), `medium` (< 840), `expanded` (< 1280), `wide` |
| `orientation` | `portrait` (h > w) or `landscape` |
| `phone` | one of the phone classes (phone portrait, phone short, tiny) |
| `phoneLayout` | laid out as a phone: `phone` by the layout class (false while "Adapt the layout" is off); what the QML reads (the app bar, the dock, menus as sheets) |
| `roomForSidebar` | the page sidebar fits beside the page (below) |
| `classWidth`, `classHeight` | the size the class was taken from |
| `held` | a pointer is held in the window (mouse button, pen, finger), or QML set `hold` |
| `touchProfile`, `minTarget` | fingers are in use: targets of 48 px, else 40 (below) |
| `mobilePlatform` | Android or iOS (the platform, not the size): one window, no tab dragged out into a window of its own (written by the tests only) |

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

`minTarget` is 48 with the touch profile and 40 without. It sizes the page sidebar's switch (Pages, Layers,
Contents, Annotations), the full-screen tab bar (36 px high, arrows 48 wide), the title row and stacked buttons of
the dialogs (below), the sidebar's arrow and the target of the tab that puts the top bar away, the tab strip's
overview, ‹ ›, + and the tabs' × (the strip grows
to 54 px with the touch profile), the tab overview's card buttons (×, the star, the reference; the star shows on every
card with the touch profile: no hover), the layer list's eye, ⋮ and "show all", the annotations' filter and export,
the fuzzy and names toggles of the library and the overview, and the document search's button.

## Choices made by hand, per class

The automatic choices apply only while the user has not chosen otherwise **for that class**. A choice made by hand
is stored per class in the app's settings (the `xournalQt` part of `settings.xml`), as `layout/<class>/<what>`:

| `<what>` | Values | Used by |
| --- | --- | --- |
| `sidebar` | `shown`, `hidden` (none: automatic) | the page sidebar (below) |
| `zen` | `off` in a tiny window: Zen left there by hand (none: automatic, Zen in a tiny window) | Zen ([zen.md](zen.md)) |
| `sourceSplit` | the page's share of the height above the Markdown source below it, `0.2` to `0.8` (none: 0.5, a phone 0.4) | the source panel (below) |
| `toolbox` | `left`, `right`, `top`, `bottom` (none: automatic, the right; a phone upright: the bottom) | the toolbox ([toolbox.md](toolbox.md)) |
| `toolboxScroll`, `topBarScroll` | where the rail and the top bar were scrolled to | the toolbox |

`app.settings.layoutChoice(class, what)`, `setLayoutChoice(class, what, value)` ("" or "auto" removes it),
`hasLayoutChoices()` and `resetLayoutChoices()` (`SettingsModel`). A change counts as a settings revision, so QML
bindings of the form `(app.settings.revision, app.settings.layoutChoice(…))` follow it. In `Main.qml`,
`win.layoutChoice(what)` and `win.chooseLayout(what, value)` do this for the window's `layoutClass`.

Turning a Surface to portrait gives the portrait choices, and back to landscape the landscape ones.

**Settings → Display → Window size**: "Adapt the layout to the window size" (`adaptiveLayout`, on by default; off:
the desktop layout at every size, the choices are those of `desktopWide`), the class the window is in now and its
size, and "Reset the layout choices".

## The page sidebar

`win.layout.sidebarShown` (`ChromeLayout.qml`) is a binding:

- **docked** (beside the page) when this class chose `shown`, or nothing was chosen and `roomForSidebar`: the window
  is at least 1110 px wide (the page keeps ~900 px beside its 210 px), and the class is a desktop one. So a portrait
  tablet and a phone never get it docked automatically: a single A4 page must stay well visible;
- otherwise hidden. The **arrow** at the left edge of the canvas area (`sidebarArrow`) then opens it as a **drawer** over the page, with the rest dimmed. Picking a
  page (or a chapter, or an annotation) closes it, and so does a tap on the dimmed page. The drawer is for the
  moment: it is not remembered. The button just outside its edge (the sidebar icon) keeps it beside the page in this
  class (`shown`).
- The same arrow sits at the sidebar's edge while it is open ("‹") and closes it: a docked sidebar is then `hidden`
  for this class where it would be shown automatically. Showing it again where there is room clears the choice.
- The arrow: a slim tab at 40 % of the canvas's height (clear of the search bar at the top and the pills at the
  bottom), `minTarget` wide in the touch profile (24 px otherwise). Not in the compact chrome or Zen, not while
  presenting, and not while the top bar is put away (unless the sidebar is open: then it closes it).

`win.layout.showSidebar(shown)` and `win.layout.dockSidebar()` are the functions; `sidebarDocked`, `sidebarDrawerOpen`
and `sidebarShown` the state.

The drawer:
- it **slides** in from the left and out again (180 ms, `win.layout.drawerSlide` 0 → 1), the dimmed page fades with it. Only
  a tap slides it (the arrow, the dimmed page, a page, a chapter or an annotation picked, Esc, the back key); a change
  of the size class takes it away at once;
- **Esc and Android's back key** close it;
- its width (`win.layout.drawerWidth`): 210 px on a tablet and a desktop; on a phone up to 85 % of the window (at most
  360 px: 350 at 412), so the thumbnails are larger (the list is one column as wide as the drawer); a phone held
  sideways 260 px (a page's thumbnail stays shorter than the window);
- Pages, Layers, Contents and Annotations work the same in it; a page, a chapter or an annotation picked closes it,
  choosing a mode does not. On a tablet it stays until tapped away or a page is picked.

## The chrome, apart from the window state

Four separate things (Zen and read only are switches of their own, [zen.md](zen.md)):

| | What | Set by |
| --- | --- | --- |
| `chromeMode` | `full` (tab strip, top bar, the docked toolbox, sidebar; in the phone classes the app bar and the tool dock, below), `compact` (the full-screen chrome: tab dots, the floating toolbox, the view pill; on a phone too) | full screen (`fullScreenMode`) |
| `windowFullScreen` | the window's state (`showFullScreen()`) | full screen (F11) |
| `app.presenting` | black around the pages, page by page | F5 |
| `zen` | everything around the page hidden (`hudHidden`; `fullChrome` is false): only the page and the dot in its lower left corner | ⋮ → View → Zen, Ctrl+Alt+Z, Read; automatic in a tiny window; presenting without controls |

- **Full screen** (F11, `fullScreenMode`) is the compact chrome in a full-screen window. Leaving it gives the window
  back its state and ends presenting.
- **Zen** hides the HUD (`win.modes.hudHidden`: the top bar, the toolbox, the pills, the format bar) and the chrome
  around the page (`win.modes.fullChrome` false: the tab strip, the sidebar and its arrow, the phone's app bar and dock; the
  compact chrome's tab dots); the dot in the lower left corner and its pill bring it back. The pen writes on. Presenting
  without controls (`win.modes.presentClean`) is presenting in Zen.
- **Read only** is apart from all of these (anywhere): [zen.md](zen.md).
- The home screen always keeps the tab strip (it is the way back to the documents); in the phone classes the app bar.

Zen of itself (`win.modes.zenAuto`): in a **tiny** window (under 360 px either way: split screen, Android's pop-up
view), nowhere else. Leaving it there stores `off` for the tiny class; Zen turned on there again stores `""`
(automatic again). The compact chrome is full screen's only.

## Menus

`AdaptiveMenu.qml` is the menu of the app's menus: ⋮, the library menu, the card menus of the home screen (and
the page-with-hits menu of a card), the tab menu and the layout menu; also the layer menu,
the annotations' filter, a bookmark's and a chapter's menu in the sidebar, the look-up menu, the table editor's cell
menu, the toolbox's menus (a tool's menu, the kinds of tools, ⋯) (every menu of the app now). `PageMenu.qml` (the page menu of the sidebar
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
  - a bottom sheet, as wide as the safe area (at most 640 px, centred in it), at most 85 % of the window high (the
    rest scrolls), its last row above the bottom safe area (`win.insets.bottom`); while the soft keyboard is open it
    rests on the keyboard (below, "Safe areas and the soft keyboard");
  - rows of at least 48 px; a check mark for a checked choice, an arrow for a submenu;
  - **a submenu drills in**: the sheet shows its entries, with a back arrow and its title (and deeper: View → Dark
    pages). A menu with a `title` (the tab menu: the tab's name; a card's menu: the file's name) shows it on
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
  reads as invisible, and the sheet asks while the menu is closed. An entry with a string property `detail` shows it
  as the row's second line in the sheet (the look-up menu's web addresses). A separator that comes and goes gets a `property
  bool offered` too. A submenu is an `AdaptiveMenu` with a `title`, and its `offered` hides its entry.

### The ⋮ menu

At most ten entries at the top, the rest one level deeper. It is **complete**: every command is in it,
whether a bar shows it or not (the bars are the user's to arrange, ⋮ is not; [toolbox.md](toolbox.md), "The top bar").

| Top level | Inside |
| --- | --- |
| Save as… (not for text files) | |
| Share… | |
| Print… (Ctrl+P) | |
| Find and replace (where text can be written) | |
| **Document ▸** | New document, Open…, Save, Edit as notes / Open externally (where offered), Bookmark this page / Remove the bookmark, Add to / Remove from favourites, Quick note, Rename…, the password, Version history…, Save with a message… (the milestone), Edit anyway…, Open as PDF document, Remove unused images…, Tags…, Handwriting language ▸, Adopt annotations…, Linked from…, Copy link to this page |
| **Export ▸** | Export as plain PDF…, Export pages as pictures…, Export for the archive…, Export as Markdown |
| **Page ▸** (not for text files) | Insert pages…, Background of this page…, Page size…, Space for notes…, Start a chapter here…, Rotate ▸, … |
| **Tools ▸** (not for text files) | Hand, Select, Snip, Mark PDF text, Write on the page, Setsquare / compass, The finger draws; Image, Stickers, Add a page, Record audio (the bars' buttons: `moreCmd_<name>`) |
| **View ▸** | Search, Full screen, Present, All open documents (not in the phone chrome), Page layout… (where the view pill has no button for it), Curtain, Spotlight, Present without controls (Ctrl+F5), Zen, Read only, Read, Dark pages ▸, Replay the writing, Toolbox position ▸ (not in the phone classes) |
| **Help ▸** | Introduction, Tutorial, Keyboard shortcuts |
| **Settings** | |

Entries that depend on the document are left out where they do not apply. "Markdown source beside the page" is in the menu of the
writing button (its long press). Entries and submenus have icons where an obvious one exists; the phone sheet shows them
too.

## The top bar

The bar at the top is **the top bar**: the other list of the toolbox's arrangement, in the user's order, which
**scrolls** instead of folding, with "+" (the catalog) and ⋮ pinned at its end ([toolbox.md](toolbox.md), "The top
bar"). The author's rules: a single A4 page stays well visible; ⋮ pinned at the end; nothing scrolls away silently
(the fade and the half cell at the end that has more; ⋮ is complete).

The top bar is the same element as the rail (`Toolbox.qml`, `bar: "top"`): `topTools` holds it in the full chrome; in
a text document it is the end of the format bar's row; in the phone chrome the app bar holds it (below). The compact
chrome and Zen do not show it: the floating toolbox's ⋯ lists its items and New.

The tab that puts the bar away sits in the middle of its top edge towards the pages (⋮ keeps the end); a slim strip at
the top brings it back. Its target reaches into the pages, `minTarget` deep in the touch profile. The toolbox stays
where it is meanwhile.

### Cycling buttons

Tools that do almost the same share one button (`ToolCycleButton.qml`, the logic in `ToolGroups.qml`, `win.toolGroups`):
the app tools of the bars, ⋮ → Tools, and the toolbox's snip entry.

| Group | Variants (a tap goes through them) | Only in its list |
| --- | --- | --- |
| `select` | rectangle ↔ lasso | rectangle and lasso on all layers |
| `geometry` | setsquare ↔ compass | the curtain and the spotlight (put out or taken away, beside the tool in hand); "Take it off the page" (also the × of the geometry pill) |
| `snip` | snip a rectangle ↔ snip with the lasso (one picture to the clipboard, then the tool before; [snip.md](snip.md)); an app tool of the rail | the snips' resolution (a setting) |
| `text` | mark PDF text ↔ copy handwriting as text (one sweep, its words to the clipboard, then the tool before; [handwriting-search.md](handwriting-search.md)) | how PDF text is marked (the PDF text button's own list) |

`ToolGroups.qml` also lists the kinds of shapes and erasers, which the toolbox's editor offers: the pens, highlighters,
erasers, shapes and laser pointers are entries of the toolbox, each with its settings.

**The groups** (the author: "think of the cycling groups we currently have and whether we can
have the snipping screenshots as a cycling tool in the toolbelt"):

- **The snips** are a group of their own: in the toolbox an entry of the kind "Snip" ("+" or "Add a tool here…"; not
  among the first tools), whose icon is its shape (`xqt-snip-rect`, `xqt-snip-lasso`) with two dots for the two. A tap
  snips with its shape; a tap while it is armed takes the other shape, which the entry keeps; its editor (Edit… in its
  menu) chooses the shape too. A snip is never the toolbox's active entry: the tool before stays the one that comes
  back. Among the fixed tools the snips have a button of their own (the author: "Snip should be
  one click away … not hidden behind the normal select tool"), after select, not in the select button's list (one
  place each; Shift+S, Shift+L); the image button's list keeps them as well, as an insert (a picture to paste).
- **Mark PDF text ↔ copy handwriting as text** (the author: "make it its own tool similar to select
  pdf text (could be a cycle) to not interrupt the ink annotation flow"): one button, a cycle as suggested rather than
  a fixed tool more (the rail is already full at 1080 px). A tap while one of them is in use takes the other; held:
  both, and how PDF text is marked. Copy handwriting is a one-shot tool like the
  snips: the tool before comes back after its sweep. Shift+T.
- **Curtain ↔ spotlight** stay in the setsquare's list (and ⋮ → View), not a group with a button: they are not tools
  but sheets over the page whatever tool is in hand, and are put out and taken away; a cycle (curtain → spotlight →
  none) on a button would hide which one is out. Their keys are B and Shift+B.

- A tap on the button while its tool is in use: the next variant. A tap while another tool is in use: its tool with
  the variant used last (remembered per group in the setting `toolVariants`).
- The icon is the variant in use (or the one last used); small dots under it say how many there are and which one.
- A long press (or a right click) lists all variants with icon and name, with the group's name on top: to pick one.
- The keys (S, L; Shift+S and Shift+L for the snips, Shift+T to copy handwriting) take a variant directly; the button
  follows and remembers it. P, H, E and T take the toolbox's entry of that kind used last.
- There is no shapes or eraser menu: the sticky note, the shapes and the eraser's kind and size are the toolbox's
  entries, the setsquare and the compass their group, snapping to the grid a setting.

The **text box** and **writing on the page** stay two tools: writing on the page is an app tool (on the top bar at a first start) switched on and off by its button (a tap while it is on ends it); the text box is an entry of the toolbox (its font in its
editor). The text box is always a Markdown text box; there is no plain text tool (T takes the toolbox's text box). Plain texts in documents are still drawn, and the text box tool still edits them as plain text (a tap on
one: `CanvasView`, "an ordinary text there is edited as it is").

### Labels without hover

Every button of the bars and the pills says what it is by its icon; where an icon alone was
ambiguous it was replaced (below). Without hover (a finger):

- a finger held on a button (`IconButton`) shows its name (`label`) above the finger while held; letting go then does
  not press it. The mouse and the pen's hover show the tool tip; a mouse or a pen held long still presses
  the button on release;
- a button with a long press of its own keeps it (`ownHold`): a cycling button (its list), the writing button (the
  Markdown source beside the page), the text box (the font), Mark PDF text (how it marks), Add a page (Insert
  pages…), Present (without controls), the page layout (its menu), the zoom percentage (the whole page), a color (its
  menu). A menu it opens shows the button's name on top (`AdaptiveMenu.titleShown`);
- ⋮ and the catalog show the names beside the icons; the phone sheet shows the menus' icons and names.

**Icons changed** (in `qt/resources/icons`, Lucide's or drawn in their style; see its README): the finger draws
(`xqt-finger-draw`), mark PDF text (`xqt-mark-text`), the text box (`xqt-text-box`), writing on the page
(`xqt-page-text`), the eraser's whiteout (`xqt-eraser-whiteout`) and whole strokes (`xqt-eraser-stroke`), the page
layout (`xqt-page-single` / `xqt-book-open`), "more tools" (`xqt-tools-more`), and icons for the ⋮ entries.

### The view pill

Undo and redo where no bar holds them (`win.layout.undoPlace`: "toolbox", "formatBar", "toolBar" or "viewPill":
the bar is put away, the compact chrome, Zen), the page layout, the page
grid, **the contents** (moved here from the tool bar), the page number, and a small **zoom percentage** (no − / + any
more):

- a tap on the percentage: after the platform's double-click time (so a double tap does not flash it) a menu: Fit the
  width (Ctrl+0), Real size 100 % (Ctrl+1), Fit the height (the page's height fills the view), Fit the whole page;
- a double click / double tap, or a long press: the whole page; a right click: the menu at once;
- pinch, Ctrl+wheel, Ctrl+plus / minus / 0 and the middle button zoom.

How far out every zoom goes (pinch, wheel, Ctrl+minus, the fits, the reference's view, the presenter's screens): to
**Settings → Display → Zoom → "Smallest zoom"** (5–50 % of the real size, 20 % unless set; `smallestZoom` in the
xournalQt part of settings.xml; `ViewController::minZoom`), and further whenever that is still too close to see the
biggest page whole, or the widest row of pages with its gaps (two A4 pages side by side on the unfolded Fold 7,
`DocumentLayout::wholeGroupZoom`; sideways: a column or a pair). Before, it stopped at upstream's 30 %, which on the
unfolded Fold 7 (100 % being some 2.35 px per point there) kept two A4 pages beside the rail from fitting.

It never runs out of its canvas (28 px from the canvas's right edge, 8 where that is too
much; beside a reference or the Markdown source it stays in its own half), and it moves up above the reference's
pill where the two would meet. In phone portrait (and tiny) the page layout button is left out: ⋮ → View → Page
layout… opens its menu.

**The compact pill** (`viewPill.compact`): in a canvas under 520 px wide (a phone upright, a half
beside the reference or the source): undo, redo, the contents, **the page number as a button** (a tap: all pages, the
page grid button's place), the zoom percentage (its menu as above). No page layout button (⋮ → View → Page layout…
is offered whenever the pill has none), no page grid button, no ‹ › of sideways scrolling (a swipe turns the page).
Under 360 px also no redo (Ctrl+Y) and no separators.

**Pills that meet**: the view pill keeps the canvas's lower right corner; the others go above it where
they would meet it: the selection's pill (centred at the bottom; smaller where the canvas is narrower than it), the
back / forward pill and the "shown read-only" note (lower left), the sticky note's pill; the toolbox floating at the
bottom edge in the compact chrome (a phone upright) puts the view pill above it, and floating at a side it ends above
the pill (`floatBottom`). `win.layout.clearOfPills(item, lowY, others)`
does it for the pills of `Main.qml`. (Qt 6.7 crashes when a binding of `y` reads the geometry of items itself through
a function: the result goes through a property of its own, `clearY`.)

### One place for each action

The author's rule: only one way to do things, to reduce menu clutter. The buttons of the app's items have one home each
(the rail, the top bar, or neither: the catalog), arranged by the user; ⋮ has every command as well (so
nothing can be arranged out of reach), and the view pill and the sidebar keep theirs. Where the button can be out of
sight:

| Action | Its one place | Keys | With the bar put away / in the compact chrome | In the phone chrome |
| --- | --- | --- | --- | --- |
| Pen, highlighter, eraser, text box, sticky note, shapes, laser pointer, snip (each with its color, width, …: the editor) | the toolbox's entries (on the rail, or carried to the top bar) | P H E T | the toolbox (docked; floating in the compact chrome) | the dock (the top bar in the app bar) |
| Hand, select, snip, mark PDF text | items of the arrangement, the rail's at a first start | A S L, Shift+S | as above; ⋮ → Tools | the dock; ⋮ → Tools |
| The finger draws, write on the page, setsquare / compass | items of the arrangement, the top bar's at a first start | Ctrl+Alt+M | the floating toolbox's ⋯ (what the top bar holds); ⋮ → Tools | the top bar in the app bar; ⋮ → Tools |
| Insert image, stickers, add a page (its menu: templates, Insert pages…), record audio | the top bar's at a first start | I, Ctrl+N, Ctrl+Shift+R | the floating toolbox's ⋯; ⋮ → Tools (⋮ → Page → Insert pages… is kept) | the top bar; ⋮ → Tools |
| Search, full screen, present, Zen, read, replay | the top bar's at a first start | Ctrl+F, F11, F5, Ctrl+Alt+Z, Ctrl+Alt+R | the floating toolbox's ⋯ (in full screen: "Leave full screen", Esc); ⋮ → View | the top bar; ⋮ → View |
| Present without controls | ⋮ → View (it differs from Present); Present's menu on a bar (held) | Ctrl+F5 | the floating toolbox's ⋯ | ⋮ → View |
| Settings | the top bar's at a first start; ⋮ → Settings | Ctrl+, | the floating toolbox's ⋯; the home screen's settings | the top bar; ⋮ |
| New | the tab strip's + where it is shown | Ctrl+Shift+N | the floating toolbox's ⋯ → New document | the catalog can place it; ⋮ → Document; the tab overview and the library |
| Open, Save, share, print, a milestone, tags, favourite, bookmark | the top bar's at a first start | Ctrl+O, Ctrl+S, Ctrl+P | the floating toolbox's ⋯ | the top bar; ⋮ |
| Edit as notes, Open externally | the top bar's end, while offered (not items of the arrangement) | | | the top bar; ⋮ → Document |
| All pages (was also ⋮ → View) | view pill (the compact pill: its page number) | Ctrl+Alt+G | the view pill stays in the compact chrome | the dock's page number |
| Contents overview | view pill | Ctrl+Alt+O | as above | the page grid's pill (the dock's page number) |
| Page layout | view pill (long press: the menu); phone portrait and the compact pill: ⋮ → View → Page layout… | | | ⋮ → View → Page layout… |
| Zoom fits | view pill's percentage | Ctrl+0, Ctrl+1 | | the page grid's pill (its zoom %); pinch |
| The page sidebar | the arrow at the canvas's edge / the sidebar's edge | | not in the compact chrome | the arrow (a drawer) |
| Hide the top bar (was also ⋮ → View) | the tab on the bar's edge | | the strip at the top edge shows it again | – (the dock stays; Zen hides everything) |
| Toolbox position | ⋮ → View → Toolbox position; its grip dragged to an edge | | | – (the dock) |
| Snap to the grid | Settings | | | Settings |
| Plain text box (removed) | – (T and the text box make Markdown text boxes; plain texts are still edited) | | | – |
| All open documents | the tab strip's overview button, ⋮ → View | Ctrl+Shift+E | the tab dots (compact) | the tab count of the app bar (a tap; a double tap: the document used before; a long press: the ones used lately) |
| Undo, redo | the head of the toolbox (pinned); a text document: the start of its format bar | Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y (the tips show the keys set) | the floating toolbox; a text document in the compact chrome: the view pill | the dock; with the soft keyboard open, the end of the format bar above it |

The keys in this table are the defaults: Settings → Shortcuts changes them, and every tip and menu entry names the keys
as they are set (`win.withKeys(text, id)`: all of an action's keys, the tips of undo and redo; `win.keyNote(id)`: its
first key, " (Ctrl+P)", for the other tips, the menus and sentences). No label writes a key that can be changed into its
text; the keys that cannot be changed (Esc, Enter, Space, the format bar's Ctrl+B …) are written as they are.

Zen hides everything; its dot and the dot's pill bring the controls back ([zen.md](zen.md)). Zen: the top bar (on every
screen, phones too), ⋮ → View → Zen, Ctrl+Alt+Z, the floating toolbox's ⋯; Android's Back leaves it. Read only: ⋮ →
View → Read only, the dot's pill, the floating toolbox's ⋯.

## Panels

The author's rule stays: a single A4 page stays well visible; nothing at the side narrows the page in portrait.

### The Markdown source panel

The source of Markdown on a page (and of a Markdown text box; the deprecated text flow panel the same) goes where the
window has room for it (`win.layout.sourceAtBottom`, `win.layout.sourcePanel`):

| Where | When | Size |
| --- | --- | --- |
| **beside the page** (right) | desktop wide and narrow, a phone held sideways (915×412), "Adapt the layout" off | 38 % of the window, 360 to 600 px, never more than half of it (`win.layout.sourceSideWidth`) |
| **below the page** | tablet portrait, phone portrait, and a desktop-narrow or tiny window whose area is portrait (600×800) | the page above keeps the whole width; the source takes the bottom half on a tablet, the bottom 60 % on a phone |

Below the page, the **divider** between them (`sourceDivider`, a touch-sized grip; the whole edge takes a drag) moves
the split between 20 % and 80 %; the page's share is remembered per size class (`layout/<class>/sourceSplit`).

Why the split and not a full-screen sheet on a phone: the page formats as one types, and that is the point of the
panel; a sheet would hide it. At 412×915 the page above is 412 px wide (fit to the width) and about 330 px high,
the source below about 490 px (title, format bar, size, and the text). While the soft keyboard is open the panel ends
above it (the split applies to the room above the keyboard), its format bar moves to the panel's bottom, right above
the keyboard, and the text keeps its cursor in view; the page stays visible above it.

### The reference split

Side by side where the canvas area is landscape, top and bottom where it is portrait (`referenceSplit.vertical`,
16 px of margin around square). The divider keeps its ratio when that flips. In a half under 480 px wide the
reference's pill is its page number and a ⋮ with the rest (`referenceMoreButton`, `referenceMenu`). See
[reference-view.md](reference-view.md).

### The format bar

`MarkdownFormatBar.qml`. **A text document's** bar holds its commands: undo and redo at its start, all
the formatting as buttons, then the top bar itself at the end of the same row (`formatCommands`), ⋮ pinned at its end;
nothing folds, and the row scrolls as the bars do (its view ends through the middle of a button, a fade at the end
that has more, the wheel scrolls it; `holdsCommands`, `scrollsAsBar`). At 1366 px the formatting and the first
commands are in sight, at about 1800 px everything. Markdown on a page and the panel's bar (no commands) keep the ladder:
on a desktop and a tablet they take the richest form that fits, in this order:

1. everything as buttons: ¶ H1 H2 H3 | the six marks | the four lists | code block, table, formula block, image, rule,
   page break;
2. the six blocks go into **"+ Insert"** (`mdInsertButton`, `mdInsertMenu`: an AdaptiveMenu with Code block ▸ the
   languages, Table…, Formula block, Image…, Horizontal rule, Page break);
3. "Insert" without its word (a "+"), where the headings as buttons still fit with it;
4. else ¶ H1 H2 H3 become one button with the level at the cursor and a menu (`mdBlockButton`, `mdBlockMenu`), and
   "Insert" gets its word back if it fits then;
5. only then (a window under ~680 px, 600×800) the row scrolls.


The marks and the lists always stay in the row. On a phone the row scrolls sideways (the norm of mobile editors), with
fading edges where there is more (`formatBarFadeLeft`, `formatBarFadeRight`); while the soft keyboard is open for the
page's Markdown it docks right above the keyboard (below, "The soft keyboard"). The level buttons are 40 px wide in the touch profile (F7.4); a finger held on any button shows
its name. The panel's own bar (beside or below the page) follows the same rules for its own width.

### What other parts can use

`win.layout.sourceAtBottom` and `win.layout.sourceBottomHeight` (the panel's place; the keyboard shrinks the room it is taken
from), `win.layout.drawerWidth`, `viewPill.compact`, `MarkdownFormatBar.phone` (its scrolling form, the one docked above the
keyboard), `win.layout.clearOfPills` (for pills that float over the page). The phone chrome (below) took the drawer's width and the
format bar as they are; its dock is a bar of its own (the page ends above it), so no pill needs to go above it.

## Dialogs and sheets: `AdaptiveDialog`

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

`SettingsPage.qml` keeps its own sheet (sections of fixed rows). It holds the frame (the header, the phone's list,
the tabs); each section is a file (`SettingsPen.qml` … `SettingsHelp.qml`, with the dialogs it opens), made of the rows
`SettingsSwitchRow.qml`, `SettingsSliderRow.qml`, `SettingsComboRow.qml`, `SettingsHint.qml` and
`SettingsSectionTitle.qml` (they read `sheet.s` and `sheet.narrow` through the sheet's context):

- On a desktop or a tablet: the tabs, in a sheet `min(920, window − 32)` wide and the window's height less 48, above
  the soft keyboard. Each section scrolls.
- On a phone (`phonePortrait`, `phoneShort`, `tiny`): the whole screen, the sections as a list (`settingsSectionList`,
  items `settingsSection<i>`), each opening as a page with a back arrow (`settingsBackButton`, `sectionShown`). Esc and
  the back key go back to the list first, then close. Shortcuts stays the last section.
- Below 600 px (the sheet's own width): the slider, combo and other two-part rows put their label above the control
  (`narrow`); a slider keeps at least 120 px.

### The toolbox of the compact chrome

The floating toolbox stays inside a short window (it scrolls as a short rail does), and its ⋯ menu with
"Present" and "Leave full screen" too (`theFloatingToolboxFitsAShortWindow`).

## The home screen and the tab overview

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
Dragging cards onto a crumb moves them there.

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

**Labels without hover**: the icon buttons of the home screen are `IconButton`s with a short `label` (New, Import,
View, Settings, Sort, Up, …) that a held finger shows; the longer `tip` stays for the mouse.
The home screen's own buttons (the switch's icons and ★, ▾, the floating "+") show their word while a finger is held
on them.

## The phone chrome

The author's decisions: one place per action, clear icons without hover (a held finger shows the name), a single A4
page well visible, and in phone portrait a **bottom tool dock**. The main target is the Galaxy Fold 7 folded (412 × 915,
phone portrait) and unfolded (900 × 1000: tablet portrait, which keeps the tablet's layout: the tab strip, the command
bar and the docked toolbox), and very small or slim desktop windows of the same classes.

In the phone classes (`win.layout.phoneLayout`: phone portrait, phone short, tiny, by the layout class) the full chrome is the
**phone chrome** (`win.layout.phoneChrome`): no tab strip, no bar of its own for the top bar and no view pill, but

**The app bar** (`PhoneAppBar.qml`, `phoneAppBar`, 48 px below the status bar, `safeTop`; with the top bar under it
upright):

| Where | What |
| --- | --- |
| left | the library (`phoneHomeButton`) |
| middle | the document's title (`phoneTitle`, elided in the middle, ● when it has changes) with small dots for the open documents under it (`phoneTabDots`; more than 12: "3 / 14"). A **swipe along the title** (`phoneTabSwipe`, 40 px): to the left the next document, to the right the previous one (round the ends); only there, the page keeps every touch and the top bar scrolls |
| the top bar | **the same top bar as on a larger screen** (its items in the user's order, scrolling, "+" at its end; `phoneToolsSlot`): upright (a bar narrower than 600 px) a row of its own under the title (the app bar is then 48 px + the top bar's 56 taller); held sideways in the row between the title (a quarter of the width) and the tab count. Zen is on it |
| held sideways | the page number (`phonePageButton`: all pages), before the tab count: the dock at the side gives its room to the tools |
| right | the **tab count** (`phoneTabCount`, the number in a square): a tap shows all open documents (after the double-tap time, like the zoom %, so a double tap does not flash them); a **double tap** goes back to the document used before (Alt+Tab: `app.previousUsedTab()`, the order of use kept by `TabManager::usedOrder`); a **long press** lists the documents used lately, the current one first (`recentTabsMenu`, a sheet) |
| far right | ⋮, the top bar's own (`toolEnd` goes into `phoneAppBar.moreSlot`) |

New documents come from the overview ("+") and the library. Ctrl+Tab and Ctrl+Shift+Tab work. On the home
screen of a phone class the app bar stays in every chrome (the way back to the documents): the home button is marked,
the title is the document behind the home screen (greyed; a tap goes back to it), no ⋮.

**The tool dock** (`PhoneDock.qml`, `phoneDock`) instead of the view pill: it hosts **the toolbox**
([toolbox.md](toolbox.md), "Where it is"): undo and redo, the rail's items scrolling sideways (the one in hand
scrolled into view) with **"+"** (the catalog, a sheet; `toolboxAddInline`) after them, and the page number
(`toolboxPageButton`: all pages; the page grid's pill has the
**contents** and the **zoom %** there, its fits as a sheet; a fit goes back to the page; no − / +, the pinch sets the
columns). While the soft keyboard is open (the dock gone) undo and redo are at the end of the format bar above it
(`keyboardUndoButton`, `keyboardRedoButton`). A text document (no ink tools) keeps the dock's own cells: undo, redo
(`dockUndoButton`, `dockRedoButton`) and the page number (`dockPageButton`); its commands are the top bar's and ⋮'s.

- Phone portrait: at the bottom, above the navigation bar (`win.insets.bottom`), in the window's footer: the page ends
  above it and keeps the whole width.
- Held sideways (phone short, or a tiny window in landscape; `win.layout.dockVertical`): a **rail at the right side**, the
  same cells from the top down, below the app bar; the page number is in the app bar then (the rail's room goes to
  the tools). A phone in
  landscape lacks height, not width. The page, the Markdown source beside it and the reference end at the rail
  (`win.layout.dockRail`).
- Full screen (the compact chrome) on a phone: the toolbox floats at its edge as on a desktop.

There is no sheet of every tool: the tools and commands are on the two bars (the same items as on a larger screen), every command is in ⋮, and "+" (the catalog, a sheet) adds a tool
or puts an item that is on neither bar onto one.

**Sheets**: in the phone classes a tool's editor (`toolEntryEditor`: its colors, width, …) is a sheet (its
`asSheet`): as wide as the window (at most 640 px), at most 85 % high (the rest scrolls), above `safeBottom`, the
handle of `MenuSheet`, Esc and the back key close it; the menus are sheets of the same form (`MenuSheet`).

**Zen in a tiny window**: automatic only there (`zenAuto`); everywhere else
chosen by hand. Only the page and the dot; the pen writes; the dot's pill shows the controls again (and in a tiny
window stores `off` for that class). ⋮ → View → Read is Zen with read only, in full screen ([zen.md](zen.md)).

**The Zen dot** (`zenDot`): a 48 px target in the lower left corner of the page, a 10 px grey dot faint after 2 s, clearer when the pointer
or the pen is near; a tap opens its pill. Presenting with the controls has no dot (Ctrl+F5 hides them).

**Android and iOS** (`win.adaptive.mobilePlatform`, the platform, not the size): one window. No tab is dragged out of
the tab strip into a window of its own (`TabStrip.undockable`), and the tab menu has no "Move to a window of its own".
Libraries already open in the same window there (`app.libraryWindows`). Moving tabs about stays.

**The library's top**: the row of breadcrumbs is left out where it would only repeat the library's name (the folder is
the top, every size; `crumbBar.needed`), unless it shows the library importing or indexing.

**Changing the fold**: the jump from 412 × 915 to 900 × 1000 (and back) is more than 64 px, so the class changes at
once (the foundation's jump rule); the phone chrome and the tablet's layout switch cleanly, and the choices of each
class are kept apart.

The safe areas and the soft keyboard (below): the app bar takes `safeTop` and the side insets
at its ends, the dock `safeBottom` (at the bottom and at the rail's end) and the side insets (the rail the right one).
While the soft keyboard is open the dock goes and, on a phone, the format bar takes its place right above the
keyboard.

## Safe areas and the soft keyboard

The window is drawn edge to edge on Android 15+ (and on iOS): the status bar lies over its top, the navigation or
gesture bar over its bottom, and a camera cut-out over a side when a phone is held sideways (audit D11, D12, F15).

### Safe areas

`win.insets` (`WindowInsets.qml`: `top`, `right`, `bottom`, `left`; the window's `safeTop`, `safeRight`,
`safeBottom`, `safeLeft` are their aliases for `main.cpp` and the tests): set by `main.cpp` for every window (also a tab's window of its own) from `QWindow::safeAreaMargins()` on Qt
6.9+, and again when they change (turned, folded, unfolded); 0 with the desktop's Qt 6.7. `XQT_SAFE_AREA="t,r,b,l"`
sets them by hand (to look at a phone's insets on the desktop); the tests set the properties.

**The pages go on under the bars** (edge to edge); only the controls keep clear. Where the controls over the pages may
go, in the content item's coordinates (the area between the header and the footer): `win.insets.controlsLeft`,
`controlsRight`, `controlsTop`, `controlsBottom` (the last also above the keyboard), and over the canvas
`win.layout.canvasControlsLeft/Right/Top/Bottom`. `win.insets.contentBottomInset` is how much of the content item lies under the
bottom inset (0 where a footer took it: the dock, the keyboard's room).

| Edge | What keeps clear |
| --- | --- |
| top | the app bar (`topInset`), the tab strip, the tab dots and the floating toolbox of the compact chrome, the search bar, the geometry pill, full-screen sheets and dialogs, Settings, the tab overview |
| bottom | the dock, the floating toolbox, the view pill, the back / forward pill, the note of a file shown read-only, the selection and note pills, the snackbar (in the document and on the home screen), the page grid's and the contents' pills, the reference's pill, the canvas's scroll bars, the Zen dot and its pill, presenting's page number, the drawer's lists, the Markdown source's text, bottom sheets (menus, a tool's editor, page menu, emoji, the reference's page field), dialogs, Settings, the tab overview, the home screen's "+" and selection bar |
| left / right | the app bar's and the tab strip's ends, the top bar, the docked toolbox (it grows by the inset), the dock's rail (the right inset) and its row, the drawer (beside a left cut-out, `sidebarLeftFill` has its color under it), the sidebar's arrow, the floating toolbox, the view pill, the scroll bars, the Markdown source beside the page, the format bar's row, the home screen (its color under the insets), the sheets (as wide as the safe area, centred in it), dialogs, Settings, the tab overview |

Bottom sheets take their place from `win.insets.sheetWidth`, `sheetX`, `sheetBottom` (the keyboard's top while it is open,
else the window's bottom) and `sheetBottomPadding` (the room for the navigation bar under their last row);
`MenuSheet` and the editor's sheet compute the same.

### The soft keyboard

`win.insets.keyboardTop` (the keyboard's top in the window; the window's height while it is closed), `keyboardHeight` and
`keyboardOpen`, from `Qt.inputMethod.keyboardRectangle` (Android reports it in the screen's pixels: divided by the
device pixel ratio; the logic `NewDocumentDialog` had). Where the platform makes the window smaller instead, the
keyboard lies below the window and nothing changes. `win.insets.fakeKeyboardHeight` (tests; `XQT_FAKE_KEYBOARD=<height>`)
puts a keyboard of that height at the bottom.

- **The footer makes room for it** (`bottomPadding: keyboardHeight`), as Android's `adjustResize` would: the pages,
  the Markdown source, the pills, the home screen and the snackbar end above the keyboard. AdaptiveDialog, Settings,
  the sheets and the menus (the desktop menus' bottom margin) keep above it.
- **The dock goes** while it is open (`dockShown`), also the rail held sideways: the format bar takes its place.
- **The format bar docks right above the keyboard** on a phone (`win.layout.phoneLayout`) while it is open and the page's
  Markdown (in place, a text box, a text document) has the keys (`formatBar.docked`: it moves into the footer), as
  Obsidian, iA Writer and Google Docs have it. The source panel's bar moves to the panel's bottom
  (`MarkdownPanel.barDocked`), right above the keyboard too. On a tablet the bar stays at the top.
- **The text cursor stays in view.** The canvas scrolls it into view when the canvas becomes shorter (the keyboard
  came: `DocumentCanvasItem::geometryChange`), after each key and each input method event (`showTextCursor()`,
  `CanvasView::scrollToTextCursor`; the Markdown editor followed its cursor already, an ordinary text did not). The
  source panel's text scrolls its cursor into view when its view becomes shorter (`MarkdownPanel.showCursor`).
- The emoji picker's search does not take the keys when it opens as a sheet (that would open the keyboard over the
  emoji); a tap on it does.

## The ladder

What changes as the window gets smaller, automatically (each step can be overruled by hand where it has a choice):

| Step | When | What changes |
| --- | --- | --- |
| 0 | desktop wide, room for the sidebar | everything shown: the sidebar docked, the toolbox docked, the top bar scrolling where it must, ⋮ pinned |
| 1 | window < ~1110 px, or tablet portrait | the sidebar is a drawer; the Markdown source below the page in portrait, the reference top and bottom; in tablet portrait the toolbox's slim rail at the right leaves an A4 page well visible |
| 2 | phone portrait (w < 600) or short (h < 560) | the phone chrome: the app bar (the title, tab dots, the tab count), a bottom tool dock in phone portrait and a rail at the side in landscape; dialogs and menus as sheets |
| 3 | tiny only (w or h < 360) | Zen, automatically; everywhere else Zen and Read are chosen by hand ([zen.md](zen.md)) |

## Adding to it

- Read the class from `win.adaptive` (`sizeClass`, `phone`, `widthClass`, …), never from `win.width`. A component's
  own width (e.g. a dialog's `availableWidth`) is still fine for its own inner layout.
- An automatic choice is a binding: `property bool x: choice !== "" ? choice === "…" : auto`, where
  `choice = win.layoutChoice("<what>")`. A toggle by hand calls `win.chooseLayout("<what>", value)`, and stores
  `""` when the value equals the automatic one. Never assign to the bound property.
- New `<what>` keys: plain words; document them in the table above.
- Sizes of targets: `win.adaptive.touchProfile ? win.adaptive.minTarget : <the desktop size>`, or `minTarget`
  directly where 40 on the desktop is fine.
- Hiding HUD items: add `!win.modes.hudHidden` to their `visible`; the chrome's own items test `win.modes.chromeMode`.
- Drag gestures of QML that must not see the layout change under them can set `win.adaptive.hold`.

### Thresholds not moved yet

These keep a width of their own (not a size class):

| Where | Threshold | Block |
| --- | --- | --- |
| `NewDocumentDialog.qml` | its body < 520 → one column (its own width: fine as it is) | – |
| `ShortcutSheet.qml`, `AppendPages.qml` | their own widths (620, 260): fine as they are | – |

The panels' own thresholds are widths of the canvas or the half they are in, not of the window,
so they are not size classes either:

| Where | Threshold | What |
| --- | --- | --- |
| `ChromeLayout.qml` `sourceAtBottom` | the class (tablet portrait, phone portrait), or a portrait area in desktop narrow / tiny | the Markdown source below the page |
| `ChromeLayout.qml` `sourceSideWidth` | `min(600, max(min(360, area / 2), 0.38 w))` | the source beside the page |
| `ChromeLayout.qml` `drawerWidth` | 210; phone: `min(360, 0.85 w)`; phone held sideways: 260 | the sidebar as a drawer |
| `ViewPill.qml` `compact`, `tight` | canvas < 520, < 360 | the compact view pill |
| `ReferenceSplit.qml` `vertical` | the area h > w (16 px margin) | top and bottom |
| `ReferenceSplit.qml` `narrow` | the reference's half < 480 | the pill: page and ⋮ |
| `MarkdownFormatBar.qml` | the room of its row against the widths of its forms | Insert menu, block menu, scrolling |

## Tests

- `SizeClasses.*` (`qt/tests/ui/AdaptiveLayoutTest.cpp`): the class of each audit size, the edges in their order,
  the hysteresis.
- `AdaptiveLayoutTest.*` (label `ui`, about 15 s): the real window at 1920×1080, 1280×800, 960×1392, 412×915 and
  915×412 (class, sidebar, no control of the home screen or the document outside the window), the sidebar choices
  per class and their reset, the drawer, "Adapt the layout" off, the hysteresis and a held pointer, the touch
  profile, and the chrome apart from the window state. What is known not to fit yet is listed in the test
  (`knownOutside`, `expectLater`): it prints `[ KNOWN ]`, and `[ NOW HOLDS ]` once fixed, so the check can be made
  strict.
- `AdaptiveLayoutTest.menusFitAtFiveSizes` (about 15 s): at the same five sizes, ⋮ (its four submenus at 1280×800),
  the page menu, the library menu and a card's menu lie inside the window, no taller than it, no entry cut off, not
  over their button; ⋮ has at most 12 entries at the top and needs no scrolling in desktop wide; in the phone classes
  they open as the sheet, at the bottom, with rows of 48 px. `AdaptiveLayoutTest.menusAreSheetsOnPhones`: the drill-in
  (two levels), the back arrow, Esc a level up, a row that triggers its entry, the layout menu's columns row borrowed
  and given back, the tab menu's rename after the sheet closed.
- `XQT_UI_ADAPTIVE=1 ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.allSizes*'`: the same checks (with the menus
  and all submenus) at all 18 sizes of the UI audit (about 70 s), with a `[ WALK ]` line per screen (outside, hidden
  by scrolling, small).
- `AdaptiveLayoutTest.dialogsFitTheWindow` (about 5 s): eight dialogs at 1920×1080, 1024×700, 1280×500, 412×915 and
  915×412: inside the window, the confirm button too, every control of the body reachable by scrolling it to its end
  and none wider than the window, and the placement of its kind. `aPhoneSheetAndTheBackKey`: × and the confirm button
  of a full-screen sheet, the footer back in a wide window, Esc and the back key. `settingsOnAPhoneAreAListOfSections`
  and `theFloatingToolboxFitsAShortWindow`.
- `XQT_UI_ADAPTIVE=1 ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.allSizesDialogs'`: the same for all 48 dialogs
  (document, library, settings) at the 18 sizes (about a minute). `XQT_UI_DIALOG_SHOTS=<folder>` saves a picture of
  each (without the dialogs' backgrounds off-screen, except full-screen sheets).
- The top bar and the toolbox (`checkToolBar`), in the same checks at the five sizes (and all 18 with `XQT_UI_ADAPTIVE=1`): ⋮ shown and outside
  anything that scrolls; the toolbox docked with undo and redo at its head; the finger switch and writing on the top
  bar; no "more tools"; the view pill inside the window; the phone chrome's dock with the toolbox, the app bar with the
  top bar and Zen on it. `theToolsFitAt720` (the top bar scrolls), `newIsTheTabStripsPlusWhereThereIsOne`,
  `sidebarArrowOpensAndCloses`, `viewPillWithContentsInsideAndClearOfTheReference` (five
  sizes, a reference open).
  `XQT_TOOLBAR_SHOTS=<folder> ./xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.toolBarPictures'` saves pictures of
  the layouts (about 20 s).
- The panels, label `ui`: `sourcePanelBesideOrBelowThePage` (the five sizes: beside, below at
  half, below at 60 %; the page's width; the view pill inside it), `sourceDividerIsDraggedAndRememberedPerClass`,
  `referenceSplitFollowsTheAreaAndItsPillsStayApart` (the five sizes: orientation, the ratio kept, both pills inside
  their halves and apart, the narrow pill and its ⋮ sheet), `compactViewPillOnAPhone`, `sidebarDrawerKeysAndPhoneWidth`
  (Esc, the back key, 85 %, larger thumbnails, the modes), `formatBarFoldsIntoInsertInsteadOfScrolling` (a text
  document: nothing folds, the commands at the row's end, the formatting first in sight, the row scrolls at 1280 and
  narrower; Markdown on a page: the Insert menu at 800 and it fits; 40 px headings with touch; a phone scrolls with
  fading edges), `pillsKeepClearOfTheViewPill` (selection, back /
  forward, the floating toolbox at the right and the bottom edge).
- `MainWindowTest.zoomPercentageTapDoubleTapAndHold`, `cyclingToolButtons`, `theEraserEntryErasesTheWayItsEditorSays`,
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
- The phone chrome, `PhoneChromeTest.*` (label `ui`, about 30 s): the app bar and the dock at 412 ×
  915, 915 × 412 and 340 × 700 with a bottom safe area of 24 px (inside the window, above it, the rail at the side),
  the toolbox in the dock, the top bar in the app bar; a swipe on the title; the tab count's tap (after
  the double-tap time), double tap (A → B → C: back to B, and to C) and long press (the sheet of the ones used lately,
  in that order); a tool's editor, its menu and the catalog as sheets at the bottom, the tool in hand in the dock;
  the page number's grid with the contents and the zoom; Zen automatic only in a tiny window (left: remembered) and
  its pill inside it; presenting without controls with the Zen dot, with the controls none; no tab dragged out with `mobilePlatform`; no breadcrumbs at the library's
  top; the Fold 7 folded and unfolded (900 × 1000 and 960 × 1392: the top bar and the docked toolbox). In the checks at the five sizes
  and the full walk, `checkPhoneChrome` replaces the top bar's checks in the phone classes. `Tabs.theOrderOfUse`
  (label `shell`): the order of use.
- Safe areas and the soft keyboard, `SafeAreasKeyboardTest.*` (label `ui`, about 25 s):
  `controlsStayOutOfTheSafeArea` (insets of 32 at the top and 24 at the bottom, and a 40 px cut-out at the left held
  sideways, at 412 × 915, 915 × 412 and 900 × 1000: no control of the app bar, the tab strip, the top bar, the dock,
  the pills, the scroll bars, the drawer, ⋮ (sheet or menu), a dialog, the page grid's pill, the compact chrome and
  the snackbar in them; the page edge to edge), `theFormatBarDocksAboveTheKeyboardAndTheCursorStaysInView` (a fake
  keyboard of 360 px: the dock gone, the bar right above the keyboard, the cursor above it while 30 lines are typed
  and when the keyboard comes, the snackbar, ⋮'s sheet and a dialog above it),
  `theSourcePanelsFormatBarAndCursorWithTheKeyboard`, `touchTargetsOfTheTabStripTheOverviewAndTheSidebar`,
  `theRemainingMenusAreSheetsOnAPhone`, `theHomeScreensIconButtonsHaveShortLabels`.
  `XQT_SAFE_AREA_SHOTS=<folder>` saves pictures of them with the insets drawn as red bands and the keyboard grey.
- `SettingsModelTest.layoutChoicesPerSizeClass` (label `shell`): the storage.
- The audit's own walk (`XQT_UI_AUDIT`, pictures and `report.tsv`, now with the class) shares the walker
  (`qt/tests/ui/LayoutWalk.h`). Its screens `allTools`, `recentTabs` and `dockPages` show the phone chrome's sheets;
  `presenting` is another name for `chrome`.
