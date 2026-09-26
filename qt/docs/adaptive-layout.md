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
Contents, Annotations) and the full-screen tab bar (36 px high, arrows 48 wide). The later blocks size their own
targets with it (audit F14).

## Choices made by hand, per class

The automatic choices apply only while the user has not chosen otherwise **for that class**. A choice made by hand
is stored per class in the app's settings (the `xournalQt` part of `settings.xml`), as `layout/<class>/<what>`:

| `<what>` | Values | Used by |
| --- | --- | --- |
| `sidebar` | `shown`, `hidden` (none: automatic) | this block |
| `chrome` | `compact`, `reader` (none: automatic, the full chrome) | this block |
| `toolbar` | `top`, `twoRowsTop`, `twoRowsBottom`, `railLeft`, `railRight` (none: automatic) | `qt/adaptive-toolbar` |

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
- otherwise hidden. Its **Pages** button then opens it as a **drawer** over the page, with the rest dimmed. Picking a
  page (or a chapter, or an annotation) closes it, and so does a tap on the dimmed page. The drawer is for the
  moment: it is not remembered. The button just outside its edge (the sidebar icon) keeps it beside the page in this
  class (`shown`).
- The Pages button in a docked sidebar hides it: `hidden` for this class where it would be shown automatically.
  Showing it again where there is room clears the choice.

`win.showSidebar(shown)` and `win.dockSidebar()` are the functions; `sidebarDocked`, `sidebarAsDrawer` and
`sidebarDrawerOpen` the state.

## The chrome, apart from the window state

Three separate things (audit D5):

| | What | Set by |
| --- | --- | --- |
| `chromeMode` | `full` (tab strip, tool bar, sidebar), `compact` (the full-screen chrome: tab dots, the tool square, the pen pill, the view pill), `reader` (no HUD) | the class's choice, full screen |
| `windowFullScreen` | the window's state (`showFullScreen()`) | full screen (F11) |
| `app.presenting` | black around the pages, page by page | F5 |

- **Full screen** (F11, `fullScreenMode`, as before) is the compact chrome in a full-screen window. Leaving it gives
  the window back its state and ends presenting, as before.
- **Compact** chosen for a class (Settings → Display) shows the same chrome inside the window, without full screen.
  The tool square's popup then ends with "Show the tabs and the tool bar" instead of "Leave full screen".
- **Reader** hides the HUD (`win.hudHidden`: the tool bar, the pills, the tool square, the format bar); the faint mark
  in the lower left corner (the one of presenting) brings the full chrome back. Presenting without controls
  (`cleanPage`) also counts as `hudHidden`.
- The home screen always keeps the tab strip (it is the way back to the documents).

This block changes the chrome only by hand. Automatic chrome per class comes with `qt/compact-chrome`.

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

Ten entries at the top (about 450 px high, 530 with the touch profile), the rest one level deeper:

| Top level | Inside |
| --- | --- |
| Save as… (not for text files) | |
| Share… | |
| Print… (Ctrl+P) | |
| Bookmark this page / Remove the bookmark of this page | |
| Add to favourites / Remove from favourites | |
| **Document ▸** | Rename…, Open externally, Edit anyway (as plain text)…, Edit as notes, Open as PDF document, Remove unused images…, Linked from…, Copy link to this page |
| **Export ▸** | Export as plain PDF…, Export for the archive…, Export as Markdown |
| **Page ▸** (not for text files) | Insert pages…, Insert image…, Insert sticky note, Background of this page…, Page size…, Space for notes…, Start a chapter here… |
| **View ▸** | All pages (Ctrl+Alt+G), All open documents, Full screen (F11), Present (F5), Present without controls (Ctrl+F5), Read (only the page: the reader chrome of this size class), Hide the tool bar, Tool bar position ▸ (Top, Left, Right) |
| Settings (Ctrl+,) | |

Entries that depend on the document (a `.md`: Edit as notes, Open as PDF document, Remove unused images; a text
file: no Save as, no Page) are left out as before. "Markdown source beside the page" stays in the menu of the writing
button.

## The collapse ladder (what the later blocks build)

The author's decisions of 2026-09-26 on the audit's proposals:

| Step | When (automatic) | What changes | Block |
| --- | --- | --- | --- |
| 0 | desktop wide, room for the sidebar | everything as today; the tool bar grouped (colors, widths), ⋮ pinned | `qt/adaptive-toolbar` |
| 1 | window < ~1110 px, or tablet portrait | the sidebar is a drawer (**done**) | this block |
| 1b | tablet portrait (a 2-in-1 or Surface upright) | **two tool rows** at the top by default, all important tools shown; "two rows at the bottom" (closer to the fingertips) as the class's choice; no side chrome that narrows the page: an A4 page stays well visible | `qt/adaptive-toolbar` |
| 2 | phone portrait (w < 600) or short (h < 560) | the compact chrome in the window: tab dots, and a **bottom tool dock** in phone portrait (the tool square with the pen pill in landscape); dialogs and menus as sheets (menus: **done**) | `qt/compact-chrome`, `qt/adaptive-menus`, `qt/adaptive-dialogs` |
| 3 | **tiny only** (w or h < 360) | the reader chrome, automatically; everywhere else "Read" is a manual choice | `qt/compact-chrome` |

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
| `HomeView.qml` `narrow` | window < 760: the search in its own row | `qt/adaptive-home` |
| `HomeView.qml` `roomy` | window ≥ 1500: the Bookmarks tab and Favourites chip with words | `qt/adaptive-home` |
| `TabOverview.qml` | the header row's width, cells of ≥ 280 px, 1.25 × high | `qt/adaptive-home` |
| `Main.qml` Markdown panel | `min(max(360, 0.38 w), 600)` | `qt/adaptive-panels` |
| `ReferenceSplit.qml` | always side by side | `qt/adaptive-panels` |
| `SettingsPage.qml` | `width - 32` × `height - 48`, rows with fixed label widths | `qt/adaptive-dialogs` |
| `NewDocumentDialog.qml` | its body < 520 → one column (its own width: fine as it is) | – |
| `ShortcutSheet.qml`, `AppendPages.qml` | their own widths (620, 260): fine as they are | – |

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
- `SettingsModelTest.layoutChoicesPerSizeClass` (label `shell`): the storage.
- The audit's own walk (`XQT_UI_AUDIT`, pictures and `report.tsv`, now with the class) shares the walker
  (`qt/tests/ui/LayoutWalk.h`).
