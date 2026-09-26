# UI audit: menus and window sizes (2026-09-26)

This audit looks at how the Qt Quick UI copes with smaller windows: narrow, short and portrait ones. It covers
desktop windows, portrait tablets and 2-in-1s, and phones. It lists what is wrong and at which size it starts, then
a set of discussion points with a recommendation for each, then implementation blocks. **Nothing here is built
yet**: the author decides first.

## Summary

- **The top tool bar is 2007 px wide** for a notes document (40 buttons, with the 10 default colors). Even at
  1920 px the Settings and ⋮ buttons are scrolled off the right edge. At 1366 px, 13 tools are hidden, among them
  all colors and widths. At 412 px only 7 tools show: Pages, Contents, New, Open, Save, Pen, Highlighter. The row
  scrolls sideways without any sign that there is more, and the tools people use most (⋮, colors, widths) are the
  first to disappear, because they sit at the end.
- **The ⋮ menu has about 26 entries and is 1234 px tall.** It is taller than the window at every size under
  1250 px of height, including 1920×1080. It then opens over the tab strip and the tool bar and scrolls, with only a
  thin scroll indicator. It is 200 px wide, so entries are cut off ("Bookmark this p…", "Background of this pa…").
- **Nothing adapts after start-up.** The page sidebar's `width >= 900` is a start value. Once the Pages button is
  used, the binding is gone and the sidebar never reacts to the window size again. Dialogs, the Settings sheet, the
  tab overview and the side panels have fixed or minimum sizes. Some of them overflow the window: dialog buttons
  cannot be reached, and whole pages get laid out wider than the window.
- **Portrait tablets lose most of the tools with the top bar**, but the existing **side tool bar** (two columns,
  104 px) shows all 40 tools at heights of 1300 px or more: Surface 960×1392, 1280×1872 and a 2-in-1 at 864×1488.
  At 720×1232 only ⋮ is missing. For portrait tablets the fix is mostly to pick the right layout, not to hide tools.
- **The full-screen chrome already works inside a small window** once it is separated from `showFullScreen()`.
  This is the author's idea for step 2: the tab dots with arrows, the tool square, the pen pill and the view pill.
  Presentation without controls is already a HUD-free reader. What is missing is a mode property that is separate
  from the window state, a phone-sized view pill, and bigger touch targets in that chrome.
- On phones the **tab overview, the library's breadcrumbs and selection bar, the Settings sliders, the Markdown source
  panel and the reference split** are broken. Their fixes are listed below.

## How this was measured

`qt/tests/ui/AdaptiveAuditTest.cpp` is an opt-in test that runs only when `XQT_UI_AUDIT` is set. It opens the real
window off-screen at each size and walks these screens:

- the library, the library menu, the selection bar and a deep folder;
- a document, with and without the sidebar, and with the tool bar at the side;
- the ⋮ menu (with the tool bar scrolled to its end first, as a user would), the layout menu, the search bar and
  the page grid;
- the Insert pages and Page size dialogs, and Settings;
- four open documents: the tab strip, the tab overview and the reference split;
- the full-screen chrome, the tool popup, presenting, and presenting without controls, all inside a normal window of
  that size;
- a `.md` document with the format bar, the Markdown source panel, and the library with tabs open.

For each screen it saves `<screen>-<w>x<h>-<label>.png` and writes a line to `report.tsv`. The line lists enabled
controls outside the window, controls hidden in a scrolled area, buttons under 40 px, the fit of the tool bar
(buttons shown and hidden) and the height of the open menu or dialog against the window.

```sh
XQT_UI_AUDIT=/tmp/ui-audit ./xqt-ui-tests --gtest_filter='AdaptiveAudit*'   # all 18 sizes, about 20 minutes
XQT_UI_AUDIT=/tmp/ui-audit XQT_UI_AUDIT_SIZES=412x915,1280x500 XQT_UI_AUDIT_SCREENS=doc,moreMenu ./xqt-ui-tests --gtest_filter='AdaptiveAudit*'
```

Sizes in logical px. Tablets and 2-in-1s have a 48 px task bar taken off.

| Label | Size | Device |
| --- | --- | --- |
| desktop-fhd | 1920×1080 | desktop, maximized |
| laptop / laptop-16x10 | 1366×768, 1280×800 | small laptops |
| small-desktop / tiny-desktop | 1024×700, 800×600 | a window made small, tiled |
| narrow-tall | 600×800 | a window tiled to a third |
| phone-portrait / phone-landscape | 412×915, 915×412 | Galaxy phones, the Fold 7 folded |
| fold7-inner | 900×1000 | the Fold 7 unfolded |
| short-wide | 1280×500 | a short window, a laptop with a docked panel |
| surface-200-portrait / -landscape | 960×1392, 1440×912 | Surface Pro 3:2 at 200 % |
| surface-150-portrait / -landscape | 1280×1872, 1920×1232 | Surface Pro at 150 % |
| 2in1-150-portrait, 2in1-125-portrait | 864×1488, 720×1232 | 16:9 2-in-1 at 150 % / 125 % |
| 2in1-landscape, 2in1-125-landscape | 1536×816, 1280×672 | the same, landscape |

A caveat about the pictures: the off-screen platform draws with Qt Quick's software renderer, which has no shader
effects. So the Material elevation layer is missing. **Dialog and popup backgrounds come out transparent** in the
pictures, and so do filled (`highlighted`) buttons such as Insert, Done and Create. On a real screen they are there.
The dim overlay behind modal popups is missing for the same reason.

## What adapts today

| Where | What | Code |
| --- | --- | --- |
| Main window | page sidebar shown at start when the window is ≥ 900 px wide (a start value, lost at the first toggle) | `Main.qml:28` |
| Tool bar | scrolls sideways (top) or up and down (side) when it does not fit | `Main.qml:386-395` |
| Tool bar | position top / left / right, and "hide the tool bar" (the tool square takes over) | `Main.qml:360-383`, `2816-2873` |
| Library header | scrolls sideways; below 760 px the search gets its own row; below 1500 px the Bookmarks tab and the Favourites chip show only their icons | `HomeView.qml:20-23`, `283-297`, `332` |
| Library grid | columns = width / 210 (at least 2), zoom with − / + | `HomeView.qml:36` |
| New document dialog | stays above the soft keyboard, scrolls when there is no room, paper and orientation wrap below 520 px | `NewDocumentDialog.qml:13-30`, `100` |
| Markdown format bar | scrolls sideways | `MarkdownFormatBar.qml:62-72` |
| Append pages (sidebar) | 3 columns below 260 px | `AppendPages.qml:14` |
| Shortcut sheet | two columns above 620 px | `ShortcutSheet.qml:65` |
| Menus | Material clamps a menu to the window and lets it scroll | Qt's `Menu.qml` |
| Dialogs | most are `min(parent.width * 0.9…0.94, N)` wide; the sheets (arXiv, Find paper, Folder chooser, Fuzzy help) are also limited in height | the dialog files |
| Android | the tab strip starts below the status bar (`safeTop`, top edge only) | `main.cpp:209-217`, `TabStrip.qml:22-24` |
| Full screen | slim tab dots with arrows, the tool square with all tools, the pen pill, the view pill | `Main.qml:2525-2635`, `2876-2984` |
| Presenting | black background, page by page; without controls only the page and a faint corner dot | `Main.qml:2671-2745` |

There is no central notion of a size class, a touch profile or a height breakpoint. Each file has its own
thresholds (260, 520, 620, 760, 900, 1500).

## Findings per screen

Each finding gives the size where it starts, a picture and the code.

### F1 The document tool bar (top)

- **F1.1 The row is 2007 px wide for a notes document** (677 px for a text document). Tools shown without scrolling:
  38 of 40 at 1920, 30 at 1536, 29 at 1440, 27 at 1366, 25 at 1280, 19 at 1024, 18 at 960, 17 at 900, 16 at 864,
  15 at 800, 13 at 720, 11 at 600 and 7 at 412. At 1920×1080, Settings and ⋮ are off the right edge
  (`doc-1920x1080-desktop-fhd.png`). At 1366, colors 10, +, the widths, Add page, Search, Full screen, Present,
  Settings and ⋮ are hidden (`doc-1366x768-laptop.png`). At 412 only New, Open, Save, Pen and Highlighter show;
  Eraser, Hand, Select, Text, Markdown, Sticky note, all colors and widths are hidden
  (`doc-412x915-phone-portrait.png`). `Main.qml:386-1016`.
- **F1.2 Nothing shows that the row scrolls**: no fade, no arrow, no overflow button. A mouse user has to know that
  the wheel scrolls it, and a pen user has to know to drag the bar.
- **F1.3 The order puts the least used buttons first and the most used last.** Pages and Contents come first, then
  New, Open and Save (also available as the tab strip's +, Ctrl+O and Ctrl+S). The colors and widths, used for every
  stroke, come late, and ⋮ comes very last. In a narrow window, what is lost first is what matters most.
- **F1.4 Ten color swatches and five width buttons take 750 px**, 37 % of the row.
- **F1.5 Portrait tablets** (`tabs-960x1392-surface-200-portrait.png`, `doc-720x1232-2in1-125-portrait.png`): the top
  bar leaves 1100+ px of height unused and hides colors, widths, select, sticky note and ⋮.
- **F1.6 A text document gets two bars**: a tool bar with 13 buttons (Pages, Contents, New, Open, Save, Edit as
  notes, Open externally, emoji, and at the far right Search, Full screen, Present, Settings, ⋮) and the format bar
  below. That is 146 px of chrome with the tab strip,
  29 % of a 500 px high window (`markdownDoc-1280x500-short-wide.png`).
- **F1.7 The "hide the tool bar" tab** (`toolbarToggle`, 42×18 px) sits over the right end of the bar and the edge
  of the page; it is small for a finger. `Main.qml:2817-2844`.

### F2 The side tool bar

- **F2.1 All 40 tools fit when the window is at least ~1300 px high** (the column is 1249 px). Pictures:
  `sideToolbar-864x1488-2in1-150-portrait.png`, `sideToolbar-960x1392-…`, `sideToolbar-1280x1872-…`. At 720×1232 only
  ⋮ is cut. At 1920×1080 six tools are cut (Add page … ⋮), at 1366×768 sixteen. It is the best layout for portrait
  tablets and a poor one for landscape laptops.
- **F2.2 The tool bar is fixed at 104 px wide**, two columns, whatever the height. In landscape a third column would
  fit everything at 1080 px of height.
- **F2.3 It is only a setting** (⋮ → Tool bar position). Nothing picks it for a portrait window.

### F3 The ⋮ menu and the other menus

- **F3.1 The ⋮ menu is 1234 px tall** for a notes document (26 entries of 48 px). It fits only in windows at least
  1250 px high: the portrait tablets. In every other window it is clamped to the window, starts at the top edge
  (`y = -50` from the button), covers the tab strip and the tool bar, and scrolls with a 2 px indicator
  (`moreMenu-1366x768-laptop.png`, `moreMenu-915x412-phone-landscape.png`: 7 of 26 entries visible).
  `Main.qml:877-1013`.
- **F3.2 The menu has no width set** and comes out 200 px wide (Material's minimum), so entries are cut off:
  "Bookmark this p…", "Background of this pa…", "Export for the archive…" at the edge.
- **F3.3 The entries mix five kinds of things**: document (Save as, Rename, Favourite, Open externally, Edit
  anyway), sharing and output (Share, Copy link, Export ×3, Print), the page (Bookmark, Chapter, Insert image or
  sticky note or pages, Background, Page size, Space for notes), views (All pages, All open documents, Linked from,
  Full screen, Present ×2, Hide tool bar, Tool bar position) and Settings. Several also exist as tool bar buttons,
  so the list is longer than it needs to be.
- **F3.4 A cascading submenu (Tool bar position) sits at the very end.** It is reached only after scrolling the
  menu, and cascading menus are awkward on a phone.
- **F3.5 The other menus are shorter.** The library menu (301 px) and the layout menu (338 px) fit at every audited
  size. The library menu is shifted to fit on a phone (`x = -250` from its button), which is fine. The shapes menu
  was not opened by the audit: it has 12 entries and 3 separators, about 620 px (estimated), so it is taller than a
  phone-landscape or 1280×500 window.
- **F3.6 The home-screen card menus, the page menu (`PageMenu.qml`) and the tab menu** are popups at the pointer.
  They fit, but on a phone they are small targets far from the thumb.

### F4 The tab strip

- **F4.1 On a phone the strip is almost all chrome.** The home tab takes the library name (up to 160 px + icon), then
  the overview button and ‹ ›. The current tab shows as a sliver ("notes.x", "kalman") at 412 px
  (`tabs-412x915-phone-portrait.png`). `TabStrip.qml:33-120`.
- **F4.2 Tabs are 140–260 px each.** At 960 px four tabs don't fit and the first tab scrolls off, cut at its close
  button (`tabs-960x1392-surface-200-portrait.png`).
- **F4.3 The overview and ‹ › buttons are 38×38 px.** `TabStrip.qml:85-112`.

### F5 The page sidebar and its panels (Pages, Layers, Contents, Annotations)

- **F5.1 The sidebar shows at start in any window 900 px wide or more, and is never hidden by resizing.**
  `Main.qml:28` is a start value, and a toggle replaces it. At 900×1000 (Fold 7 unfolded) it takes 23 % of the width;
  at 915×412 (phone landscape) 23 % of a short window (`doc-915x412-phone-landscape.png`); at 960×1392 22 %.
- **F5.2 The sidebar is fixed at 210 px** and pushes the canvas aside (`Main.qml:1019-1027`). There is no overlay
  (drawer) mode for small windows.
- **F5.3 The mode buttons are 32 px high** (Pages, Layers, Contents), and the Annotations button is 36×32.
  `PageSidebar.qml:22-60`.

### F6 The view pill, the navigation pill and the other pills

- **F6.1 The view pill is about 450 px wide** (undo, redo, layout, page grid, the page number, −, the zoom, +). It
  is anchored to the canvas's right edge, so in a 412 px window its left end (undo) is cut
  (`doc-412x915-phone-portrait.png`). In a narrow canvas, beside the Markdown panel or a reference, it runs far out
  of the window: the layout button at `x = -265` (`markdownPanel-412x915-phone-portrait.png`).
  `Main.qml:1152-1391`.
- **F6.2 The reference pill and the view pill overlap** when each half is narrower than about 560 px: at 960×1392
  the reference pill (page, grid, pen, fit, swap …) runs under the main view pill (`reference-960x1392-…png`). At
  412 the reference's page button is off the left edge. `ReferenceSplit.qml:270-390`.
- **F6.3 The pen pill's buttons are 36×36 px** (`PenPill.qml:72`, `101`, `124`, `152`), used by finger in full
  screen.
- **F6.4 The quick tool popup** (full screen, 330×751 px) keeps "Present" and "Leave full screen" below the tools;
  in windows under ~740 px high, Leave full screen falls below the window: at 1024×700, 800×600, 1280×672,
  1280×500 and 915×412 (`quickTools-1280x500-short-wide.png`). `Main.qml:2934-2976`: the holder is capped at
  `height - 90`, but the two buttons need about 110.

### F7 Markdown: the format bar, text documents, the source panel

- **F7.1 The format bar is 905 px of tools** and scrolls below about 920 px, again with no sign that it does.
  `MarkdownFormatBar.qml:62-72`.
- **F7.2 Text documents stack two bars** (F1.6). Most of the 13 buttons of that tool bar belong in ⋮; the rest would fit into the
  format bar's row.
- **F7.3 The Markdown source panel is at least 360 px wide** (`Main.qml:1437-1443`: `min(max(360, 0.38 w), 600)`).
  In a 412 px window it leaves the page a 52 px strip, and the view pill runs off the window
  (`markdownPanel-412x915-phone-portrait.png`). The same holds for the (deprecated) text flow panel.
- **F7.4 The heading buttons are 36×40 px.** `MarkdownFormatBar.qml:39-50`.

### F8 The reference split

- **F8.1 The split is always side by side** (`ReferenceSplit.qml:26-31`). In a portrait window each half gets less
  than half of a narrow width: about 200 px each at 412 (`reference-412x915-phone-portrait.png`) and 470 px at 960,
  while 1300 px of height stay unused. Top and bottom would suit portrait better.
- **F8.2 The two pills collide** (F6.2).

### F9 Full-screen and presentation chrome

- **F9.1 Full screen is tied to the window state.** Setting `fullScreenMode` calls `showFullScreen()`, and leaving it
  restores the window (`Main.qml:61-80`). The author's step 2 needs the full-screen chrome in a normal window, which
  means the chrome mode has to be separate from the window state. On iOS and on Android the window is always full
  screen anyway.
- **F9.2 The chrome itself works in a small window.** In the audit the mode was set, then the window was made normal
  again (`fullscreenChrome-412x915-phone-portrait.png`, `presenting-…`, `presentClean-…`). The tool square, the tab
  dots, the pen pill and the view pill all fit, except the view pill at 412 (F6.1).
- **F9.3 The tab dots bar is 26 px high, with 26×26 arrows** (`Main.qml:2528-2610`). Its comment says "a finger's
  height", but 26 px is below any touch guideline (Material 48, Apple 44, ours 40).
- **F9.4 The tool square is at the top left**, the hardest corner to reach on a phone held in the right hand. The
  popup it opens has 6 columns (330 px) and takes the whole height of a phone (`quickTools-412x915-…`).
- **F9.5 "Presenting" means black and page by page.** A "reader without HUD" on a phone would rather keep the normal
  scrolling view with the HUD hidden (see D2).

### F10 The library (home screen)

- **F10.1 Header**: a scrolling row with the Library, Recent and Bookmarks switch, the library menu, search, grid
  zoom, Last page, New, Import, New folder, Flat, Favourites, Show, Sort, − and +, and Settings. Below 1500 px it
  scrolls. At 960 px Favourites, Show, Sort, zoom and Settings are off-screen (`home-960x1392-…png`). At 412 even the
  Recent switch is cut (`home-412x915-phone-portrait.png`). `HomeView.qml:283-686`.
- **F10.2 A deep folder breaks the whole page.** The breadcrumbs are a plain `Row` with no eliding or scrolling
  (`HomeView.qml:716-757`), and its implicit width widens the column. Then the search field, the header buttons and
  the empty-folder text and buttons all lie outside the window, at 412, 600 and 720 px
  (`homeDeep-412x915-phone-portrait.png`: "This folder is…", "Import f…"). The existing phone test
  (`aPhoneWideWindowFitsTheHomeScreenAndTheNewDocumentDialog`) checks the library root only.
- **F10.3 The selection bar is one fixed row** (`HomeView.qml:218-280`): below about 700 px, Copy to…, Move to…
  and Trash… lie outside the window (`homeSelection-412x915-…`). This is also noted in `android-roadmap.md`.
- **F10.4 In phone landscape** the tab strip, the header and the breadcrumbs take 150 of 412 px, and a card row
  doesn't fit (`home-915x412-phone-landscape.png`). The search field shrinks until its placeholder no longer shows.
- **F10.5 Small targets**: the search's Fuzzy and Names toggles (46×36, 54×36), the search button (36×36), the
  card ⋮ (36×40), the library menu chevron (36×48), and the page switch (38 px high).
- Good: two columns of cards at 412 px, and the search in its own row below 760 px.

### F11 The tab overview

- **F11.1 Below about 580 px the overview is laid out wider than the window.** The header row (title, search at
  `min(380, 0.4 w)`, extended view, New, Close all, ×) has a minimum width of about 580 px, and the column around the
  grid takes that width. The grid then shows two columns of ~290 px, half off the right edge. The close buttons of
  the right-hand cards, New, Close all and × are outside the window, and the search placeholder overlaps Fuzzy and
  Names (`overview-412x915-phone-portrait.png`). `TabOverview.qml:120-262`.
- **F11.2 Cells are 1.25 × their width high** with at least 280 px per column (`TabOverview.qml:262-266`). So a
  phone shows one card per column, and phone landscape shows about one card at a time
  (`overview-915x412-phone-landscape.png`).

### F12 Settings

- **F12.1 Settings is a centered sheet, `width - 32` by `height - 48`** (`SettingsPage.qml:10-16`), with a scrolling
  tab bar of 9 sections. On a phone the tabs past Display are hidden without a hint
  (`settings-412x915-phone-portrait.png`).
- **F12.2 The slider rows have a 220 px label and a 72 px value**, and the slider gets what is left: at 412 px that
  is nothing, and the sliders collapse to their knob (Minimum pressure, Pressure multiplier). `SettingsPage.qml:57-78`
  (also `249`, `602`).
- **F12.3 The combo rows keep 260 px for the box**, so a label wraps to three lines ("Side button / eraser end").
  `SettingsPage.qml:96`, `451`, `492`.
- **F12.4 In phone landscape** the title and the tab bar take half of the sheet, and two setting rows are visible
  (`settings-915x412-phone-landscape.png`).

### F13 Dialogs

- **F13.1 Most dialogs have no scrolling body.** They are centered with a width limit and no height limit. When the
  content is taller than the window, Qt clamps the dialog to the window, the body runs under the header and the
  footer, and the rest cannot be reached. Insert pages is 731 px tall: at 1024×700, 800×600, 1280×672, 1280×500 and
  915×412, Paper, Portrait/Landscape, the count and Where lie outside the dialog
  (`insertPages-915x412-phone-landscape.png`). The same pattern (`anchors.centerIn`, a `ColumnLayout`, no
  `ScrollView`) is in `BackgroundDialog`, `NoteSpaceDialog`, `PrintDialog`, `ChapterDialog`, `BookmarkDialog`,
  `DocumentModeDialog`, `RenameDialog`, and the share, archive, old-.xopp and unsaved dialogs in `Main.qml`
  (`1735-2527`).
- **F13.2 The New document dialog is the good example**: it limits itself to the room above the soft keyboard and
  scrolls (`NewDocumentDialog.qml:13-30`). The other dialogs should do the same.
- **F13.3 On a phone the dialogs are centered cards.** The sheets on phones would be full-screen, or bottom sheets
  with the confirm button in reach.
- **F13.4 `EmojiPicker` (360×400) and `LookUpMenu` (340 wide) have fixed sizes.** They fit a 412 px phone but not a
  landscape phone's height (400 of 412).

### F14 Touch targets under 40 px (the smallest a finger hits reliably; Material asks 48, Apple 44)

The audit counted, over all sizes and screens:

- the tab strip: overview 38×38, ‹ › 38×38;
- the sidebar mode switch: 78×32, the Annotations button 36×32;
- the library: the search button 36×36, Fuzzy 46×36, Names 54×36, the card ⋮ 36×40, the page switch 38 high;
- the pen pill: 36×36 (tool, colors, +, width);
- the format bar: headings 36×40;
- the full-screen tab bar: 26 high, arrows 26×26;
- the tool bar's hide tab: 42×18;
- the reference pill: 40×40 (fine).

The tool bar itself (48×48 icons, 40×44 color and width buttons) is fine.

### F15 Android and phones in general

- **F15.1 Only the top safe area is handled** (`safeTop`). The gesture bar at the bottom lies over the view pill,
  the navigation pill, the pen pill and the snackbar. A landscape cut-out lies over the sidebar or the tool bar.
- **F15.2 The soft keyboard covers the lower half.** Only the New document dialog moves above it. The canvas does not
  scroll the caret into view (see `android-roadmap.md`), and the format bar stays at the top, far from the keyboard.
- **F15.3 No touch profile**: the same target sizes and the same hover-first design (tool tips, hover-lit corner
  mark) on desktop and phone.

## Discussion points and recommendations

### D1 Breakpoints and device classes

**Recommendation**: one place that knows the size class, as a small QML singleton (`Adaptive`) or properties on the
window, with width **and** height classes and the orientation. Every file reads from it instead of keeping its own
threshold. Use a hysteresis of about 32 px so that dragging a window edge doesn't make the layout flicker.
Don't change the class while a stroke is being drawn.

| Class | Rule (logical px) | Examples | Layout |
| --- | --- | --- | --- |
| **Desktop wide** | w ≥ 1280 and h ≥ 560 | 1920×1080, 1366×768, 1280×800, 1440×912, 1536×816, 1920×1232 | everything: tab strip, top tool bar (grouped, D3), sidebar if room (D2) |
| **Desktop narrow / tablet landscape** | 840 ≤ w < 1280, h ≥ 560, w ≥ h | 1024×700, 900×… landscape | top tool bar with priorities and overflow; sidebar as an overlay |
| **Tablet portrait** | 600 ≤ w < 1280, h ≥ 900, h > w | 960×1392, 864×1488, 720×1232, 1280×1872, 900×1000 (Fold 7 unfolded) | **full tools**: the side rail, or two rows at the top (D3); sidebar as a drawer; reference top and bottom |
| **Phone portrait** | w < 600 | 412×915, 600×800 at its edge, iPhones (375–430) | compact chrome in the window (step 2) with a bottom tool dock (D4) |
| **Phone landscape / short** | h < 560 | 915×412, 1280×500 | compact chrome; no tab strip; one-column side rail or the tool square with the pen pill |
| **Tiny** | w < 360 or h < 360 | split screen, Android pop-up view, a very small desktop window | reader chrome (step 3) |

Notes on the thresholds:

- Width 600 and 840 are Material 3's compact/medium/expanded limits. Android's window size classes use the same
  numbers, and they suit the Fold 7 (412 folded, 900 unfolded).
- Height 560 separates a real short window (a phone in landscape, 500 px) from 800×600, which still works as a small
  desktop.
- 600×800 sits right at the phone/tablet edge. It is better as "phone portrait with the side rail allowed" than as
  a tablet: at 600 px a sidebar would leave 390 px of page.
- The Fold 7 unfolded (900×1000) is almost square. It is a tablet, not a phone: tools stay, and the sidebar is a
  drawer.
- The classes decide the layout; they do not decide pen versus touch. The touch profile is a separate flag (D14).

### D2 The collapse ladder (the author's idea, evaluated)

The author's steps were: (1) hide the pages sidebar; (2) the full-screen chrome inside the window; (3) presentation
chrome without tools, as a reader or annotator without a HUD.

**Evaluation**: the order is right, and step 2 needs little new code (F9.2). Three refinements:

1. **The width alone is not enough.** A portrait Surface (960×1392) is narrower than a small desktop window
   (1024×700) but has far more room. The author says it must keep all tools. So the ladder has a branch: in tablet
   portrait the tool bar changes shape (side rail or two rows, D3) instead of collapsing.
2. **Step 3 should not be automatic on a phone.** A phone is where one writes with the finger. Removing the tools
   when the phone is turned or the window gets narrower would take the pen away mid-work. Better: step 3 is automatic
   only for **tiny** windows (under 360 px either way), and otherwise it is a **manual "Read" mode**, a button in the
   compact chrome, the same as presenting without controls today. Its HUD comes back with the corner dot or a tap
   in a corner.
3. **The reader is not the presentation.** Presenting is black and page by page, for a projector. The reader keeps
   the normal background and scrolling, and only hides the HUD. So the chrome mode and "presenting" should be two
   independent things (D5).

Proposed ladder:

| Step | When (auto) | What changes |
| --- | --- | --- |
| 0 | desktop wide, room for the sidebar (canvas ≥ 900 px beside it) | everything as today, but the tool bar is grouped (D3) |
| 1 | the canvas would be under ~900 px with the sidebar (window < ~1110), or the window is tablet portrait | the sidebar hides; its button opens it as a **drawer over the page**, which closes after a page is picked |
| 1b | tablet portrait | the tools become the side rail (or two rows); the reference goes top/bottom; the Markdown panel goes to the bottom half |
| 2 | phone portrait (w < 600) or short (h < 560) | the **compact chrome** inside the window: the tab dots bar instead of the tab strip, no top tool bar, a bottom tool dock (phone portrait) or the tool square with the pen pill (landscape), a compact view pill; dialogs and menus become sheets |
| 3 | tiny (w or h < 360) or chosen ("Read") | the **reader**: no HUD, the corner dot or a corner tap brings it back; a tap-and-hold still gives the context pill |

### D3 The tool bar on portrait tablets (and a grouped tool bar for everyone)

**Never hidden**, in any class above phone: pen, highlighter, eraser, hand / finger-draws toggle, select (rectangle
and lasso), text / Markdown, sticky note, the current color plus at least 4 presets, the width, undo / redo, zoom,
the page number and page navigation, and ⋮. Undo, redo, zoom and page are in the view pill already. **May go into
an overflow ("more tools") popover**: New, Open, Save (Ctrl+S; the tab strip's +; the library), Insert image,
Shapes, Mark PDF text, Add page, Search, Full screen, Present, Settings, Pages and Contents.

**Recommendations**:

1. **Group the colors**: the current color as one big swatch, the last 4–5 used or preset colors beside it, and the
   rest plus "Add a color" in a palette popover. The widths become one button that shows the current width and opens
   the five widths plus the custom one. This saves about 500 px and changes nothing for the pen: the same number of
   taps for the colors people actually use.
2. **Order by use**: tools, then color and width, then insert, then the view and the document. Keep ⋮ **pinned at
   the right end** and outside the scrolling area, so it can never scroll away (F1.1).
3. **Priority plus overflow instead of silent scrolling** (desktop narrow, tablet landscape). Each button gets a
   priority. What doesn't fit goes into a "more tools" button next to ⋮, never hidden by scrolling. If scrolling
   stays for a while, show fading edges with an arrow.
4. **Tablet portrait: the side rail by default** when h ≥ ~1100 and h > w. It already fits all tools at 1300 px and
   more (F2.1); with the grouping above it fits at 1000 px. It costs 104 px of width. The page is portrait anyway, so
   fit-width loses little. Put it on the left for right-handers (the hand doesn't cover it); the setting chooses the
   side. The alternative is **two rows at the top** (the tools on the first row, colors, widths and the rest on the
   second), which fits 720–960 px after grouping. Two rows cost 56 px of height out of 1200+, and the pen hand
   doesn't cover them. **Recommendation: the side rail**, because it exists, is tested, and shows everything. The
   author's own preference on a Surface decides; offer both in the setting (Top / Two rows / Left / Right / Auto).
5. **A text document's tool bar merges into the format bar**: one row, file actions in ⋮ (F1.6, F7.2).

### D4 The tools on phones

**Recommendation**: a **bottom tool dock** in phone portrait. One pill at the bottom within thumb reach, in place of
both the top tool bar and the view pill:

- the current tool (a tap opens a sheet of all tools: the same buttons as the tool square's popup, laid out for a
  thumb);
- the color (a tap opens colors);
- the width;
- undo and redo;
- the page number (a tap opens the page grid);
- ⋮ (a sheet).

The pen pill's work (color, width, pen / highlighter) moves into the dock, so there is one pill, not three. Zoom is
the pinch; fit width sits in the page-number sheet. In phone landscape: the tool square with the vertical pen pill
at a side (as full screen today), a compact view pill, and the tab dots at the top. The dock sits above the gesture
bar (D11) and moves above the soft keyboard when one is shown (D12).

### D5 Automatic changes: overridable and remembered

**Recommendation**:

- The automatic changes apply only while the user hasn't chosen otherwise **for that class**. When the sidebar,
  the tool bar position or the chrome is toggled by hand, the choice is stored per class: `layout/<class>/sidebar`,
  `layout/<class>/toolbar`, `layout/<class>/chrome`, each "auto" or a value. Turning the Surface back to landscape
  gives the landscape choices, and portrait gives the portrait ones.
- One setting, "Adapt the layout to the window size" (on by default), turns all of it off for those who want the
  desktop layout everywhere. Settings → Display could also show which class the window is in and let the user
  reset the per-class choices.
- Make the mode explicit in the code: `chromeMode: "full" | "compact" | "reader"` (auto or chosen), separate from
  `windowFullScreen` (the window state; F11 toggles it, and on a desktop it also picks the compact chrome, as today)
  and from `presenting` (black, page by page). `fullScreenMode` becomes "compact chrome and full-screen window".
  This replaces `Main.qml:61-80`.
- The sidebar gets a real binding again: `sidebarShown = override !== undefined ? override : autoSidebar`, never an
  assignment that breaks the binding (F5.1).

### D6 Menus taller than the window, and the ⋮ menu

**Recommendations**:

1. **No menu may be taller than the window.** Material already clamps and scrolls (F3.1). So the fix is shorter
   menus and, on phones, bottom sheets. Keep a top-level menu to about **12 entries (~600 px)**. That fits every
   desktop class; below that height the phone and short classes use a sheet anyway.
2. **Give menus a width** that fits their longest entry (280–320 px), or let them measure their items (F3.2).
3. **Regroup the ⋮ menu** into top-level entries for the frequent actions and submenus for the rest. On phones the
   submenus become sections of a bottom sheet, or a drill-in (the sheet replaces its content, with a back arrow).
   Cascading submenus don't work on a phone.

   | Top level | Inside |
   | --- | --- |
   | Save as… | (text files: hidden, as now) |
   | Share… | |
   | Print… (Ctrl+P) | |
   | Bookmark this page / Add to favourites | the two stars, as now |
   | **Document ▸** | Rename…, Open externally, Edit anyway…, Edit as notes, Open as PDF document, Remove unused images…, Linked from…, Copy link to this page |
   | **Export ▸** | Export as plain PDF…, Export for the archive…, Export as Markdown |
   | **Page ▸** | Insert pages…, Insert image…, Insert sticky note, Background of this page…, Page size…, Space for notes…, Start a chapter here… |
   | **View ▸** | All pages, All open documents, Full screen (F11), Present (F5), Present without controls (Ctrl+F5), Read (no HUD), Hide the tool bar, Tool bar position ▸ |
   | Settings (Ctrl+,) | |

   That is 10 top-level entries (about 500 px) instead of 26. Everything is at most one level deeper, and the
   entries in the tool bar (image, sticky note, full screen, present) are still reachable in one tap there. On a
   phone, the tool bar buttons that went into the overflow come back in the sheet.
4. **An `AdaptiveMenu` component**: a `Menu` on desktop, a bottom sheet on phone and short classes, used for ⋮, the
   library menu, the card menus, the page menu, the tab menu and the layout menu. The context pill already works
   this way for the canvas.

### D7 The library and the tab overview on phones

**Recommendations for the library**:

- Put the Library / Recent / Bookmarks switch in a row of its own on phones: a segmented control across the width,
  or a bottom navigation bar with three items, which suits Android and iOS.
- Keep the search in its own row (as now).
- Put the actions behind two buttons: a **"+" button** (a floating action button on phones; New document, New
  Markdown, New text, Import files, Import folder, New folder) and a **"View" button** (Show filter, Favourites
  only, Flat, Sort, zoom, Last page), plus Settings in the app ⋮. Then the header fits in one row at 412 px.
- **The breadcrumbs elide from the middle** ("Library › … › Quantum mechanics › Exercise sheets"), or scroll
  sideways. They must never widen the page: give the row `Layout.maximumWidth` or clip it (F10.2). The whole page
  column must never be wider than the window. A test guards that (block 1).
- **The selection bar** on phones: icon-only buttons with a ⋮ for the rest, or a bottom action bar (Open, Move,
  Copy, Trash) as Android's file apps do (F10.3).
- **Phone landscape**: one header row (switch, search, + and View), no breadcrumb row (the breadcrumb goes into the
  header or the title), and shorter cards (an aspect of about 1:1).
- Consider a list view (a thumbnail and a title per row) as an option in View for phones. The two-column grid is
  fine too.

**For the tab overview**:

- The header wraps below 600 px: the title and the buttons on one row, the search across the width below it. The
  column must not grow wider than the window (F11.1).
- At least 2 columns on phones (cells of 170 px and up), and the cell height follows the page's aspect instead of
  1.25 × the width. In landscape: 3–4 columns of shorter cells (F11.2).
- On phones, open it from the tab dots bar (as full screen does now) and from the tab count.

### D8 Dialogs on small screens

**Recommendation**: one base component, `AdaptiveDialog` (or a mixin over `Dialog`):

- On **desktop and tablet**: centered, `height ≤ window - 48`, the body in a `ScrollView` (or a `Flickable`), and
  the header and footer pinned. Take the soft-keyboard logic of `NewDocumentDialog.qml:13-30` into the base.
- On **phone portrait**: a **full-screen sheet** (title, × at the left and the confirm button at the top right, as
  Android's full-screen dialogs), or a **bottom sheet** for short questions (Unsaved changes, Recovery, Web confirm,
  Share with its choices). Short confirmations can stay centered cards.
- On **phone landscape / short**: always full-screen, with the body scrolling.
- Apply it to all dialogs of F13.1, to Settings and to the sheets (arXiv, Find paper, Folder chooser, Fuzzy help,
  Shortcuts, Unused images).

**Settings on phones**: full screen, and the 9 tabs become a **list of sections** that opens each section as a page
with a back arrow (list → detail), instead of a scrolling tab bar. Slider and combo rows put the label above the
control below 600 px (F12.2, F12.3). The Shortcuts page matters little on phones; hide it when no hardware keyboard
is attached, or keep it last.

### D9 Side panels as overlays and drawers; the reference split

**Recommendations**:

- **The page sidebar** (and its Layers, Contents and Annotations modes) is a panel beside the canvas in desktop
  wide, and a **drawer** everywhere else: it slides over the page from the left, dims the rest on phones, and
  closes after a page is picked (or stays open on tablets until tapped away). The 210 px width stays; on a phone,
  up to 85 % of the width with larger thumbnails.
- **The Markdown source panel** (and the text flow panel): in tablet portrait, the **bottom half** (the page above
  and its source below, split by a draggable divider); on phones a full-screen sheet with a "Show the page" peek, or
  the bottom 60 % with the page visible above. Keep the side panel from desktop narrow up.
- **The reference split**: top and bottom when the canvas area is portrait (h > w). The divider keeps its ratio.
  On phones: top and bottom as well, or one pane at a time with a swap button in the pill. Merge the two pills in
  narrow halves: the reference pill shows only its page number and a ⋮ with the rest when its half is under ~560 px
  wide (F6.2, F8.1).
- **The view pill** has a compact variant under ~520 px of canvas width: undo, redo, the page number (a tap opens a
  sheet with the grid, layout and zoom) and a fit-width button. It hides the layout button and the − % + group
  (pinch zooms). It must never lie outside the canvas: anchor it with a maximum width equal to the canvas width
  minus the margins (F6.1).

### D10 The Markdown format bar

**Recommendation**:

- **Desktop and tablet**: priority plus overflow. The block, the marks and the lists stay in the row; code block,
  table, math, image, rule and page break go into a "⋯ Insert" menu when there is no room. For text documents, one
  merged row (F1.6).
- **Phones**: keep the scrolling row (it is the norm in mobile editors), with fading edges. While the soft keyboard
  is open, **dock it just above the keyboard** as a keyboard accessory bar, which is what Obsidian, iA Writer and
  Google Docs do on phones. The bar at the top of the window is far from the typing thumb and pushed away by the
  canvas's scrolling.
- Headings become 40 px wide on touch (F7.4).

### D11 Safe areas, notches and system bars

**Recommendation**: extend `safeTop` to all four edges, as a `safeInsets` object on the window that `main.cpp` sets
from `QWindow::safeAreaMargins()` on Qt 6.9+ (0 elsewhere). The desktop build (Qt 6.7) still compiles, and the
tests can set fake insets. Use it for:

- the bottom: the tool dock, the view pill, the navigation pill, the snackbar, the bottom sheets, and the page grid's
  pill;
- the left and right: the side rail, the sidebar drawer, and the pen pill, where a landscape cut-out lies;
- the top: the tab strip (done), the tab dots bar, the tool square, and full-screen sheets.

The canvas itself keeps drawing edge to edge under the system bars, which is fine for a page. Only the controls
move in.

### D12 The soft keyboard on phones

**Recommendation**: a window-wide `keyboardTop`, taken from `NewDocumentDialog`'s version, that the bottom dock,
the sheets, the format bar (docked above the keyboard, D10) and the snackbar follow. The canvas scrolls the text
caret into view when the keyboard opens or the caret moves below `keyboardTop`; this is already an open item in
`android-roadmap.md`. Search fields at the top are fine as they are.

### D13 iOS (no port yet; design implications only)

- iPhone portrait widths are 375–430 and landscape heights 375–430, so the phone classes cover them. iPad sizes (mini
  744×1133, Air 820×1180, Pro 1024×1366) fall in tablet portrait, and their landscape sizes in desktop narrow or
  wide. **Stage Manager and Split View** give arbitrary window sizes, so the classes must be dynamic, as planned.
- **The window is always full screen on iOS**, and so is Android's. The compact chrome must not depend on
  `showFullScreen()` (D5), and "Full screen" as a menu entry has no meaning there: hide it, keep Present.
- **Safe areas** everywhere: the notch or Dynamic Island at the top, the home indicator at the bottom (D11).
- **No right click and no hover**: every context menu needs a long press (already the rule), and tool tips can't
  carry information that is needed. Apple Pencil hover on iPadOS can light the corner mark as the mouse does.
- **The edge swipe from the left is the system's back gesture** on iOS and Android. Don't put a swipe-to-open
  drawer on the left edge; open the sidebar drawer with its button only.
- **Menus**: iOS uses action sheets and context menus that come up from the bottom on iPhone, so the bottom sheet of
  D6 matches. On iPad, popovers suit, which is the desktop menu.
- **Dynamic Type**: allow the UI text to follow the system font size. That means not fixing heights to text of
  13–15 px, which many labels do today.

### D14 A touch profile and target sizes

**Recommendation**: a `touchProfile` flag, true on Android and iOS and when a touch screen is attached and the last
input was a finger (the canvas knows the last input device). Two sizes follow from it: 48 px for touch and 40 px
for mouse and pen. Fix the known small targets (F14):

- the tab strip buttons 38 → 44;
- the sidebar switch 32 → 40 high;
- the library search toggles and the card ⋮ → 40–44;
- the pen pill 36 → 44;
- the format bar headings → 40;
- the full-screen tab bar 26 → 36 visible, with a 48 px hit area;
- the tool bar's hide tab → a 48 px hit area around the 18 px mark.

Hover-only affordances need a touch alternative: tool tips, and the hover-lit corner mark.

## Implementation blocks

In this order. Sizes are rough: S about a day, M two or three days, L a week. Each block has its own UI tests in
the `ui` label. Walks over many sizes are heavier and sit behind an environment variable, like the shots
(AGENTS.md rule 6).

| # | Block | Size | Depends on | What it does | Tests that guard it |
| --- | --- | --- | --- | --- | --- |
| 1 | `qt/adaptive-foundation` | M | – | The `Adaptive` size classes (w, h, orientation, hysteresis), the `touchProfile` flag, the per-class overrides in the settings, and the "Adapt the layout" setting. `chromeMode` split from the window state; `fullScreenMode` rebuilt on it (F9.1). The sidebar's binding with an override (F5.1). The audit test committed with this document stays as the tool to look. | Unit: the class for each audit size, and the hysteresis. UI: `AdaptiveLayoutTest` walks the main screens at 412×915, 915×412, 720×1232, 1024×700 and 1280×500 and asserts that no enabled control lies outside the window unless it is in a scrolling area, and that no page column is wider than the window. The quick version (2 sizes, 5 screens, < 20 s) runs in the `ui` label; the full walk runs with `XQT_UI_ADAPTIVE=1`. Resizing across a class keeps the user's sidebar choice for that class. |
| 2 | `qt/adaptive-menus` | M | 1 | The ⋮ regrouping (D6.3), menu widths, and `AdaptiveMenu` (a sheet on phone and short classes) for ⋮, the library, card, page, tab and layout menus. | Every menu opened at 412×915, 915×412, 1280×500 and 1920×1080: height ≤ window height (without scrolling on desktop wide), no entry elided, ⋮ has ≤ 12 top-level entries, on phone classes it is a sheet within the safe area. |
| 3 | `qt/adaptive-dialogs` | M | 1 | `AdaptiveDialog` (scrolling body, pinned footer, full-screen sheet on phones, keyboard-aware), applied to all dialogs and sheets; Settings full screen on phones with a section list and stacked rows; the quick tool popup scrolls and keeps Present and Leave full screen visible (F6.4). | Each dialog at 412×915, 915×412 and 1280×500: the footer buttons are inside the window, the last field of the body can be scrolled into view, no slider is narrower than 120 px. |
| 4 | `qt/adaptive-toolbar` | L | 1 | Grouped colors and widths (D3.1), the order (D3.2), ⋮ pinned outside the scrolling area, priorities with a "more tools" overflow, the side rail (or two rows) automatically in tablet portrait, the text document's bar merged into the format bar, and the format bar's insert overflow. | At 1920×1080, 1366×768 and 1280×800: ⋮ visible and nothing scrolled. At 960×1392, 864×1488 and 720×1232: every "never hidden" tool (D3) is visible without scrolling. At every size: an overflowed tool is reachable through "more tools". |
| 5 | `qt/adaptive-panels` | M | 1 | The sidebar as a drawer below desktop wide; the Markdown panel at the bottom half in portrait and as a sheet on phones; the reference top and bottom in portrait; the compact view pill; the reference pill never overlapping the view pill. | The canvas is ≥ 60 % of the window width in every class with a panel open; pills lie inside their canvas half and don't intersect; the drawer closes after a page is picked (phone). |
| 6 | `qt/adaptive-home` | M | 1, 2 | The library header regrouped (+ and View), the breadcrumbs eliding, the selection bar adaptive, the phone-landscape header in one row; the tab overview header wrapping, 2 columns minimum and the cell aspect; the phone tab strip (the current title, a count and the overview). | The library at 412×915 in a deep folder, with a selection and with an empty folder: nothing outside the window. The overview at 412×915: two columns within the window, the close buttons inside. |
| 7 | `qt/compact-chrome` | L | 1, 3, 4, 5 | Phone portrait and short classes: the compact chrome in the window (tab dots with bigger targets, the bottom tool dock merging the pen pill and the view pill's essentials, the tool square in landscape); the reader chrome (automatic when tiny, and a manual "Read"); the per-class choices remembered. | At 412×915: the "never hidden" tools reachable in at most one tap from the dock; in reader chrome no HUD item is visible except the corner mark; turning 412×915 ↔ 915×412 keeps the tool, color and page. |
| 8 | `qt/safe-areas-keyboard` | S–M | 1, 7 | `safeInsets` on all edges (Qt 6.9+), the controls kept out of them; `keyboardTop` for the window; the caret scrolled into view; the format bar docked above the keyboard on phones. | Fake insets set in the test (bottom 48, left 32): no interactive control inside them. A fake keyboard rectangle: the dock, the sheets and the format bar lie above it. On the device: the Fold 7 checklist (Android). |
| – | touch targets (F14) | S | 1 | Spread over the blocks above: each one fixes the targets in its screens to the `touchProfile` sizes. | The walk of block 1 also reports buttons under 40 px (48 with the touch profile) in the phone classes. |

Blocks 2 and 3 are independent and can run in parallel after 1. So can 4 and 6. Block 5 can start after 1, and
block 7 needs 3, 4 and 5. Block 8 goes last, with the Android device tests (build on the desktop part first, as
always).

## Decisions for the author

1. The classes and thresholds of D1 (600 / 840 / 1280 wide, 560 / 900 high, portrait = h > w), and the Fold 7
   unfolded counted as a tablet.
2. The ladder of D2: step 3 (the reader) automatic only in tiny windows, and a manual "Read" otherwise.
3. Tablet portrait: the side rail by default, or two rows at the top (D3.4).
4. Phones: a bottom tool dock (D4), or the tool square with the pen pill of today's full screen.
5. The ⋮ regrouping (D6.3).
6. Automatic changes overridable per class and remembered, with a switch to turn them off (D5).
7. Settings on phones as a list of sections (D8).
