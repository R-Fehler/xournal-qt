# Review: QML (`qt/src/app/qml/`)

Reviewed 2026-10-07 on `master-qt`: 112 files, 30,062 lines. `Main.qml` has 5,551 lines, `HomeView.qml` 3,003,
`SettingsPage.qml` 1,887 and `Toolbox.qml` 1,111. The tests are `qt/tests/ui`: 23 fixtures, 26,187 lines. Nothing in
the repository was changed.

## Verdict

The leaf components are mostly in good shape: they are small, have one job each and are named after what they do
(dialogs, pills, sheets). The window is the problem. `Main.qml` is a god file. Its root object `win` holds **145
properties and functions**, and **705 lines of state and logic come before the first visual item**. It also holds 22
dialogs, about 30 tool buttons, the whole ⋮ menu, the toolbox menus, the view pill, Zen, reading mode, the full-screen
tab dots and **99 `Shortcut`s**. **206 ids** are wired to each other across the whole file.

`win` also works as a hidden global. 38 other files reach it through the QML context chain, and 25 of them guard each
access with `typeof win !== "undefined"`. A missing member therefore never shows up as an error: it silently falls
back to a default value. Every component is created inside Main's context, so the guards protect nothing. They only
hide breakage.

Splitting the file is mechanically feasible, because unqualified ids keep resolving through the context. It needs
three rules to stay safe:

1. Each visual overlay becomes its own file, and the root of that file is the overlay item itself. This keeps its z
   value, its anchors to `canvas` and its `parent.width`.
2. The root keeps the members that tests and `main.cpp` read or write: the test API.
3. `win`, `canvas` and `menuSheet` keep their names until the context-global readers are converted.

I propose one serial mechanical-split block, followed by small blocks that can run in parallel, one file each.

---

## 1. Structure

### 1.1 What `Main.qml` holds now, by line range

| Lines | What | Size |
|---|---|---|
| 14–47 | window, focus fixing, title, color | 34 |
| 48–92 | safe area insets, `controls*`, `sheet*`, soft keyboard (`keyboardTop/Height/Open`, `fakeKeyboardHeight`) | 45 |
| 94–117 | `adaptive` (AdaptiveLayout), `openTemplateSave`, `openPageFiles`, `layoutChoice`, `chooseLayout`, `menuSheet` | 24 |
| 121–202 | sidebar docked or drawer state, drawer animation, `showSidebar`, `canvasControls*`, `clearOfPills` | 82 |
| 204–231 | source panel geometry (`sourcePanel`, `sourceAtBottom`, `sourceSideWidth`, `sourcePageShare`, `sourceShareLive`) | 28 |
| 233–310 | chrome mode, Zen, read only, reading, replaying | 78 |
| 312–346 | phone chrome flags, `toolsInFormatBar`, `toolGroups`, `openAfterMenus` | 35 |
| 347–400 | full screen and the window's state machine (`windowFullScreen`, `windowedVisibility`, `leavingFullScreen`, timer) | 54 |
| 401–455 | `noToolbar`, `textDoc`, where undo goes, toolbox placement, `withKeys`, app Connections | 55 |
| 457–497 | presenting, the presenter console, `AudienceWindow` | 41 |
| 499–691 | saving, sharing, export, closing tabs and the window, web address, search forwards, arXiv | 193 |
| 693–704 | close and raise requests | 12 |
| 706–889 | header (PhoneAppBar with the recent-tabs menu, TabStrip, `topTools`, MarkdownFormatBar), footer, PhoneDock, two undo/redo rows, table editor | 184 |
| 891–994 | docked or floating toolbox (`sideTools`, `toolboxRow`, `toolboxPane`), edge drag and its highlight | 104 |
| 995–1354 | toolbox menus: `toolboxMoreMenu`, `topBarCommands`, `toolEditor`, `toolEntryMenu`, `toolTypeMenu` (catalog), `catalogRows`, `afterMenus` | 360 |
| 1355–1405 | `CommandItem`, `toolArea` with its `slots` map, `toolBank` | 51 |
| 1406–1741 | ⋮ (`toolEnd`, `moreButton`, `moreMenu` with 7 submenus) | 336 |
| 1742–1783 | top bar `Toolbox`, `backTakers`, `grouped`, `leftTheBars` | 42 |
| 1784–2304 | ~30 app buttons (hand, select, snip, write, pdfText, emoji, image, sticker, record, addPage, search, …) and their menus | 521 |
| 2306–2410 | PageSidebar, left fill, sidebar arrow, scrim, Esc/Back for the drawer | 105 |
| 2412–2528 | DocumentCanvas (with its own overlays), EmojiSuggestions, PresenterPanel, ReferenceSplit | 117 |
| 2530–2580 | `shownFileNote` | 51 |
| 2582–2933 | `viewPill` (undo/redo, layout menu, grid, contents, page number, notes, rotation chip, zoom/fit menu) | 352 |
| 2935–3077 | PageGrid, tab drag hint, TextFlowPanel, MarkdownPanel, `sourceDivider`, ContentsOverview | 143 |
| 3079–3162 | SelectionPill, NotePill, StickerSaveDialog, InkTextToast, PdfTextPill, `navPill` | 84 |
| 3164–3291 | `linkPopup` | 128 |
| 3293–3323 | SearchBar, LinkStatusLine, CanvasScrollBars | 31 |
| 3325–4466 | 22 dialogs: open, save, PDF password, protect, share (+ .xopp, PDF copy, Xournal++ folder), archive ×3, old .xopp, hybrid edited, adopt, external save, edit anyway, backlinks, link found/missing/locate, text changed, Markdown export ×2, export, image, unsaved, recovery, intro, document mode, restart tutorial, close all, message; `Component.onCompleted` | 1,142 |
| 4467–4612 | full-screen tab dots and tab toast | 146 |
| 4614–4652 | presenting's page indicator | 39 |
| 4653–4821 | Zen dot, near area, catcher, pill | 169 |
| 4822–4941 | reading tap fields, read-only note | 120 |
| 4942–4973 | Zen and Read shortcuts | 32 |
| 4975–5125 | PageJump, Snackbar, version message dialog, app message Connections | 151 |
| 5127–5214 | HomeView, toolbar hide and show tabs | 88 |
| 5216–5284 | geometry, recording, playback and curtain pills, TimelineBar, replay shortcuts | 69 |
| 5285–5359 | instances of the dialogs that already have their own files, plus 9 `Connections { target: app }` that open them; SettingsPage, TabOverview | 75 |
| 5361–5546 | `docKeys`, `toolKeys`, `keysOf`, and 87 `Shortcut`s | 186 |

`Main.qml` holds 20 `Connections { target: app }` blocks (lines 447, 694, 3055, 3183, 3440, 3757, 3907, 4071, 4648,
5079, 5104, 5119, 5299, 5306, 5318, 5322, 5329, 5333, 5337, 5341).

### 1.2 The split map for `Main.qml`

**The rules every new file follows.** These are the hard parts, written as rules:

- **A visual overlay is the root of its own file, and Main instantiates it as a direct child of the window.** Do not
  collect overlays into a wrapper `Item`:
  - z is relative to siblings, so a wrapper at z 0 would put the Zen dot (z 91) under the home screen (z 50).
  - Anchors may only reference the parent or a sibling: `anchors.horizontalCenter: canvas.horizontalCenter` stops
    working inside a wrapper.
  - `parent.width` would mean the wrapper.
- **A non-visual file (state or flows) has `Item { visible: false }` as its root, not `QtObject`.** The moved code
  contains Timers, Connections, animations and dialogs, and `QtObject` has no default property for children.
  `ToolGroups.qml:196` shows the clumsy workaround (`readonly property Connections followTools: Connections {…}`).
- **Dialogs can move freely.** `AdaptiveDialog` and `MenuSheet` reparent themselves to `Overlay.overlay`
  (`AdaptiveDialog.qml:121`). `FileDialog` and `FolderDialog` are not items. Two exceptions:
  - The plain `Popup` `linkPopup` positions itself in contentItem coordinates. It needs `parent: Overlay.overlay`
    with mapped coordinates, or it must stay a direct child.
  - The AdaptiveMenus open relative to their button and do not care.
- **No `Loader` during the split.** Tests look up dialogs by objectName before opening them (`find("protectDialog")`,
  `find("shareDialog")`, …). `window->findChild` walks the QObject tree, which moving into a file keeps. A Loader
  that is not active yet does not.
- **Keep these three context-global ids in `Main.qml` under these names:**
  - `win`: 38 files.
  - `menuSheet`: `AdaptiveMenu.qml:92` checks `typeof menuSheet !== "undefined"`. If the id disappears, every menu
    silently stops becoming a sheet on phones.
  - `canvas`: `ContextPill.qml:17` and `PdfTextHandles.qml:12` default `canvasItem: canvas`.
- **Mechanical first.** Unqualified ids of `Main.qml` still resolve from a file instantiated in Main, through the
  context chain. That is how `win` works today. The cut-and-paste step therefore needs only:
  - aliases for ids that are used *outside* the moved region (the table below lists them);
  - the root forwarders.

  Explicit `required property` interfaces come in the later per-file blocks. Do not add
  `pragma ComponentBehavior: Bound` in this phase: it turns off exactly this lookup.

The table lists the new files. "Needs" is what the moved code reads from outside today, measured with a script over
the ranges. `win.*` lists only root members.

| # | New file (root type) | Main lines | ~Lines | Needs from outside (ids / `win.*`) | Exposes (what others use) |
|---|---|---|---|---|---|
| 1 | `WindowInsets.qml` (QtObject) | 48–92 | 50 | window size, contentItem, `presenterPanelWidth` (pass it in as `rightReserve`) | `top/right/bottom/left`, `controls*`, `contentBottomInset`, `sheet*`, `keyboard*`, `fakeKeyboardHeight`. The root keeps `property alias safeTop/…/fakeKeyboardHeight` because `main.cpp:89-99` and the tests write them. |
| 2 | `ViewModes.qml` (Item, invisible) | 233–310, 347–400, 446–497 | 230 | `window` (show*/visibility), `adaptive`, `layoutChoice`/`chooseLayout`, `textDoc`, `pageJump` (the audience's digit), `audienceWindow` | `chromeMode, fullChrome, hudHidden, zen, zenShown, setZen, readOnly, readOnlyOn, readOnlyOffered, readStarted, start/stop/toggleReading, replaying, fullScreenMode, windowFullScreen, windowedVisibility, leavingFullScreen, presentClean, presenterConsole, startPresenting`. `onVisibilityChanged` becomes `Connections { target: window }`. |
| 3 | `ChromeLayout.qml` (Item, invisible) | 121–231, 312–345, 401–440, 1770–1773, 5228–5230 | 200 | `canvas` geometry (canvasControls*), `sideTools.width`, `phoneDock.width`, `markdownPanel/textFlowPanel.visible`, `formatBar.shown`, `toolboxRow.height`, `toolboxPane` (audioPillsTop), ViewModes, insets | `sidebar*`, `showSidebar`, `dockSidebar`, `drawerSlide/Width`, `canvasControls*`, `clearOfPills`, `source*`, `phoneLayout/phoneChrome/appBarShown/dock*`, `toolsInFormatBar`, `noToolbar`, `undoIn*`, `toolbox*`, `chooseToolboxEdge`, `sideEdge`, `topBarShown`, `backTakers/takeBack`, `audioPillsTop`. The geometry readers create item↔layout cycles, so either they stay on the root or the items come in as properties. |
| 4 | `SaveFlow.qml` (Item) | 499–543, 586–625, 664–680, 3339–3359, 3916–3994, 4103–4134, 4327–4353, 4439–4456 | 300 | `tabOverview` (close-all closes it: make this a signal), `win.close()`, `textDoc` | `withSavedChanges, openSaveDialog, setUpSaveDialog, saveChosen, saveOrAsk, requestCloseTab, closeAllTabs, confirmCloseAll, closeWindow, openExternally, quitting`. It owns `afterDiscardCheck` and `waitingToClose`. |
| 5 | `ShareFlow.qml` (Item) | 552–577, 3361–3377 (ShareChoice), 3578–3915 | 400 | SaveFlow.openSaveDialog, `snackbar` | `share(path)` (today `shareDialog.openFor`, used in 4 places), `archive(path)`, `sharePdfOf`, `shareTextFile` |
| 6 | `ProtectionDialogs.qml` (Item) | 3378–3577 | 200 | none | `protect(changing)`. `pdfPasswordDialog` is driven by `app.passwordNeeded`. |
| 7 | `ExportFlow.qml` (Item) | 544–551, 4258–4318 | 80 | none | `exportPdf()`, `exportMarkdown()` |
| 8 | `DocumentNotices.qml` (Item) | 3995–4102, 4135–4257, 4458–4466, 4999–5011, 5078–5125, 5244–5247, 5263–5266 | 330 | `snackbar`, `inkTextToast`, `settingsPage` ("Settings" in the ink-copy snackbar: make this a signal) | `showMessage(title, text)` (replaces 3 copies of title/text/open), `adopt(n, app, offered)`, `showBacklinks()` |
| 9 | `StartupFlow.qml` (Item) | 4355–4437 | 85 | `homeView.offerLibrariesHomeAtStart` (make this a signal `done()`) | `showIntro()`, `askRestartTutorial()`. It owns `Component.onCompleted` (4432). |
| 10 | `VersionMessageDialog.qml` (AdaptiveDialog) | 5012–5077 | 66 | SaveFlow.saveOrAsk | `openFor(id)` (sidebar, ⋮, milestone button, shortcut) |
| 11 | `LinkPopup.qml` (Popup) | 3164–3291 | 128 | `canvas` (→ `property Item canvasItem`) | none: it is driven by `app.linkTapped` |
| 12 | `ToolboxMenus.qml` (Item) | 336–346, 995–1354, 1774–1783 | 370 | `toolboxPane`, `topBarPane`, `toolArea.slots`, `menuSheet`, `snackbar`, `searchBar`, `settingsPage`, ViewModes | `openMore(button)`, `openEntryMenu(entry, button, pos)`, `askType(why, id, b, bar)`, `edit(entry, b, edge)`, `editNew(…)`, `grouped`, `leftTheBars`, `afterMenus` (merged with `openAfterMenus`), `toolEntryName` |
| 13 | `AppButtons.qml` (Item `toolArea`, invisible) | 1376–1405, 1784–2304 | 560 | 17 ids: `canvas`, `markdownPanel`, `textFlowPanel`, `searchBar`, `settingsPage`, `tabStrip`, `shareDialog`, `printDialog`, `openDialog`, `imageDialog`, `insertPagesDialog`, `templatePicker`, `templateSaveDialog`, `pageFiles`, `documentTagsDialog`, `versionMessageDialog`, `topBarPane.leadingTail`; ViewModes, `toolGroups` | `slots` (the map at 1384), `momentary` |
| 14 | `MoreMenu.qml` (Row `toolEnd`) | 1356–1371, 1406–1741 | 350 | `toolArea.slots`, 28 ids (all the dialogs, `layoutMenu`, `viewPill.layoutShown`, `tabOverview`, `searchBar`, `shortcutSheet`, …). Its `parent:` binding (`phoneAppBar.moreSlot`, `formatBar.trailing`, `topBarPane.trailingTail`) stays at the instance in Main. | none |
| 15 | `ViewPill.qml` (Pane) | 2582–2933 | 352 | `canvas.width`, `canvasControls*`, `referenceSplit.pillRect`, `toolboxPane`, `pageGrid`, `contentsOverview`, the undo placement flags | `layoutShown`, `openLayoutMenu()` (⋮ → View, line 1649), `openFitMenu()` (`pageGrid.onZoomRequested`, line 2945). `toolboxPane.floatBottom` reads its height. |
| 16 | `ZenControls` → `ZenDot.qml` (AbstractButton) and `ZenPill.qml` (Pane), plus the near and catcher items | 4653–4821, 4942–4970 | 200 | `canvas` geometry, `pageGrid`, `contentsOverview`, ViewModes | `dotVisible` (shownFileNote and navPill move beside it), `middleTapped(count)` (replaces `canvas.onMiddleTapped` touching `zenPillDelay`/`zenDot`, lines 2430–2433) |
| 17 | `ReadingFields.qml` (Item) and `ReadOnlyNote.qml` (Rectangle) | 4822–4941 | 120 | `canvas` geometry, ViewModes | `fieldWidth`, `turn(side)`, `tell(pos)` (DocumentCanvas lines 2428–2435) |
| 18 | `FullScreenTabs.qml` (Rectangle) and `TabToast.qml` | 4467–4612 | 146 | `searchBar.visible`, `tabOverview.open` (make this a signal) | `height` (`toolboxPane.floatTop`) |
| 19 | `ShownFileNote.qml`, `NavPill.qml`, `PresentPageIndicator.qml`, `TabDragHint.qml` | 2530–2580, 3122–3162, 4614–4652, 2948–2976 | 160 | `canvas`, `viewPill`, `zenDot.visible`, `clearOfPills`, `tabStrip` | none. Keep the `clearY` indirection (2541: a binding of y that reads its own geometry crashes Qt 6.7). |
| 20 | `SidebarHandles.qml`: `SidebarArrow.qml`, `SidebarScrim.qml`, left fill | 2336–2410 | 75 | `sidebar`, `referenceSplit`, ChromeLayout | none |
| 21 | `SourceDivider.qml` (Item) | 3001–3053 | 53 | `sourcePanel`, ChromeLayout source* | none |
| 22 | `ToolbarToggle.qml` and `ToolbarShow.qml` | 5154–5214 | 62 | none | `toolbarShow.width/side` (CanvasScrollBars, line 3317) |
| 23 | `WindowShortcuts.qml` (Item) | 4971–4973, 5267–5277, 5361–5546 (and 2406–2410) | 200 | 13 ids (`pageGrid`, `contentsOverview`, `tabOverview`, `settingsPage`, `searchBar`, `homeView`, `markdownPanel`, `pageJump`, `imageDialog`, `openDialog`, `printDialog`, `versionMessageDialog`, `writeButton`), ViewModes, SaveFlow | `docKeys`, `toolKeys`. `keysOf`/`withKeys` stay on the root: 83 uses, 2 in other files. |
| 24 | `WindowActions.qml` (Item): the facade (block B3) | 5285–5343 (instances and their 9 Connections), plus the forwards at 107–117 and 628–663 | 150 | the overlays (`pageGrid`, `contentsOverview`, `tabOverview`, `searchBar`, `homeView`, `markdownPanel`) as properties | `openFile, insertImage, print(pages), insertPages(at), pageFiles(what, pages), templateSave(page), togglePageGrid, toggleContents, toggleTabOverview, openSettings(section), markdownSource(), selectedPagesOrNone()`, the look-up searches |

**What stays in `Main.qml`** (about 1,000 lines, down from 5,551):
- the root window and the test API forwarders (§1.3);
- header and footer, PhoneDock, the docked toolbox and its edge highlight, the top bar `Toolbox`;
- PageSidebar, DocumentCanvas with its own child overlays, PresenterPanel, ReferenceSplit, PageGrid,
  ContentsOverview;
- the source panels, the selection, note, PDF-text and ink pills, SearchBar, LinkStatusLine, CanvasScrollBars;
- HomeView, the top pills (geometry, recording, playback, curtain), TimelineBar, AudienceWindow;
- the instance site of every file above.

These stay because they are layout: they anchor to each other.

### 1.3 The test and C++ API of the root (it must survive every block)

Tests call or read these on the root window. If a block moves any of them, it updates the tests in the same commit.

- **Functions:** `showSidebar` (14 calls), `chooseToolboxEdge` (11), `setZen` (9), `startPresenting` (6),
  `saveChosen` (5), `toggleReading`, `setUpSaveDialog`, `chooseLayout`, `startReading`, `sharePdfOf`,
  `requestCloseTab`, `openTemplateSave`, `openSaveDialog`, `openPageFiles`, `closeWindow`.
- **Properties:** `fullScreenMode` (read/write, 52 uses), `safeTop/Right/Bottom/Left` (read/write),
  `fakeKeyboardHeight` (read/write, also `main.cpp:99`), `presentClean` (read/write), `readOnly` (read/write),
  `windowFullScreen` (read/write), `drawerSlide`, `zen`, `phoneChrome`, `leavingFullScreen`, `sidebarShown`,
  `chromeMode`, `adaptive`, `windowedVisibility`, `readOnlyOn`, `phoneLayout`, `keyboardTop`, `keyboardHeight`.
- **Members other QML files use through `win.`** (36; this is the real component API):
  - `adaptive` (57 uses), `takeBack` (14), `toolGroups` (10), `openPageFiles` (10);
  - `safeTop/Bottom/Left/Right`, `safeInsets`, `sheetBottom/BottomPadding/X/Width`, `keyboardTop/Open/Height`;
  - `phoneLayout`, `withKeys`, `openWebAddress`, `toolEntryName`, `openTemplateSave`, `hudHidden`,
    `contentBottomInset`, `textDoc`;
  - `searchOpenTabs/LibraryFor/InDocument`, `layoutChoice`, `findPaper`, `chooseLayout`, `arxivSearch/Paper`;
  - `width`, `height`, `activeFocusItem`, `contentItem`.

  Block B5 groups these into `win.insets.*`, `win.modes.*` and `win.layout.*`. The guarded readers have to move in
  the same commit, because a renamed member does not fail: it falls back silently.

### 1.4 `HomeView.qml` (3,003 lines): split map

| New file | HomeView lines | Notes |
|---|---|---|
| `HomeView.qml` (stays) | 1–280, the outer `ColumnLayout` skeleton, the `StackLayout` | about 450 lines: page state, columns, selection helpers, `menu*` target state (241–257) |
| `HomeSelectionBar.qml` and `HomeSelectionActions.qml` | 288–373, 1208–1277 | the same actions twice (top bar or bottom bar): one component with a `compact` flag |
| `LibrarySwitch.qml` (with `PageTab`, `BarAction`) | 1278–1377, 1472–1679 | already reparented into `switchSlot`, so it is easy to cut |
| `LibraryCrumbs.qml` | 1680–1935 | parent `headerCrumbSlot` or `crumbRowSlot` |
| `LibrarySearchField.qml` | 1968–2091 | parent `searchSlot` or `narrowSearchSlot`. `libraryGrid` Keys write into its `searchField` (line 891), so expose `type(text)`. |
| `LibraryViewMenus.qml` (`ShowToggle`, `ShowMenu`, `SortMenu`) | 1378–1471 | |
| `DocumentGrid.qml` | 826–1078 (library), 1079–1183 (recent) | The two grids duplicate the Keys handler, WheelHandler, columns and the card wiring. One grid with `model` and `recent: bool`. Favourites differ (`model.favourite` versus `app.isFavouriteFile` plus a `favouriteRevision` counter hack, line 83): unify them in C++. |
| `MoveDragOverlay.qml` | 2092–2171 | |
| `LibraryItemMenu.qml` | 2172–2305 | needs the `menu*` target state: put it in a `QtObject { id: target }` and pass that |
| `LibraryDialogs.qml` | 2306–2650 | rename, new folder, text file, transfer, trash, storage access, libraries home, move library, new library |
| `LibraryArchive.qml` | 2654–2804 | four dialogs plus Connections |
| `LibraryImport.qml` | 2805–2985 | folder chooser, open/import dialogs, temporary import, conflict ×2 |

`HomeView.qml:2987` `errorDialog` and `:2996` `importedNote` (a Snackbar) duplicate Main's `messageDialog` and
`snackbar`.

### 1.5 `SettingsPage.qml` (1,887 lines): split map

| New file | Lines | Its dialog (moves with it) |
|---|---|---|
| `SettingsPage.qml` (frame: header, phone list, tabs, stack) | 1–273 | none |
| `SettingsRows.qml`, or one file each: `SectionTitle`, `Hint`, `SwitchRow`, `SliderRow`, `ComboRow` | 87–173 | none (the section files need them) |
| `SettingsPen.qml` | 275–484 | `resetBarsDialog`, `resetToolboxDialog` (1848–1886) |
| `SettingsTouch.qml`, `SettingsStabilizer.qml` | 485–665 | none |
| `SettingsDocuments.qml` | 666–1031 | `intoPdfExplanation` (1750) |
| `SettingsDisplay.qml` | 1032–1252 | none |
| `SettingsSearch.qml` (with handwriting) | 1253–1501 | none |
| `SettingsNewPages.qml`, `SettingsStorage.qml` | 1502–1616 | `removeCaches` (1769) |
| `SettingsShortcuts.qml` | 1617–1698 | the key capture dialog (1811) |
| `SettingsHelp.qml` | 1699–1749 | none |

Other problems in this file:
- Section names exist twice: `sectionNames` (55) and the ten `TabButton`s (259–268).
- `searchSection: 5` and `shortcutsSection: 8` are magic indices.
- **Fix:** one model `[{key, title, source}]` drives the tabs, the phone list and the `StackLayout`, and
  `showSection(key)` replaces the indices.
- `SettingsPage.qml:30-35` recomputes the soft keyboard. `win.keyboardTop` already does it.

### 1.6 `Toolbox.qml` (1,111 lines)

`Toolbox.qml` is cohesive: one element, the rail or the top bar. It is big because it holds four mechanisms:
- scrolling and length (228–362);
- drag and drop between the bars (434–580);
- lending the window's buttons (581–634);
- four delegate `Component`s (903–1075) plus the group flyout (1076–1111).

Low priority: move the delegates into `ToolboxEntryCell.qml`, `ToolboxAppCell.qml` and `ToolboxGroupCell.qml`, and
the drag logic into `ToolboxDrag.qml` (a QtObject with `box` as a property). The Main-side menus (block B2, file
12) are the bigger win.

### 1.7 State in `win` that belongs elsewhere

| State | Where it should live |
|---|---|
| `keysOf`, `withKeys` (5364, 442) | `app.shortcuts` in C++: `tipWithKeys(text, id)`. The `(revision, …)` comma trick then disappears from 83 call sites. |
| `phoneLayout` (314), and the same list in 8 other files | `AdaptiveLayout` (C++, `quick/AdaptiveLayout.h`). Its `phone` (line 68) is based on `sizeClass`, while the QML lists are based on `layoutClass`, and Main mixes both: `adaptive.phone` at 155, 226 and 3043; `phoneLayout` elsewhere. With "Adapt the layout" off they disagree. Add `phoneLayout` based on layoutClass. |
| `sheetWidth`, `sheetX`, `sheetBottom`, `sheetBottomPadding` (74–77) | `WindowInsets`, read by MenuSheet, PageMenu, EmojiPicker, StickerPicker and SettingsPage |
| presenting | Split between C++ (`app.presenting`) and QML (`presentClean`, `startPresenting`, `presenterConsole`). In the same way, `app.toolbarHidden` lives in C++ while `fullScreenMode` and `zen` live in QML. Pick one owner per feature: the view modes either all in ViewModes or all in C++. |
| `afterDiscardCheck` (41), `waitingToClose` (625), `quitting` (93) | SaveFlow |
| `toolboxEdgeTarget`, `edgeAt` (970–979) | the toolbox dock part |
| `sourceShareLive` (229) | SourceDivider |
| drawer state (126–152) | SidebarHandles or ChromeLayout |
| `backTakers`, `takeBack` (1770) | a Back-key coordinator in WindowShortcuts |
| `toolGroups` (334) | with AppButtons or the toolbox; it does not belong on the window |
| aliases of other members | `reading` is `readOnlyOn` (306). `sidebarAsDrawer` is `!sidebarDocked` (128). `layoutClass` is `adaptive.layoutClass` (130; both forms are used). `undoInFormatBar` is `toolsInFormatBar` (412). Remove the aliases. |

---

## 2. Duplication

1. **`afterMenus`** (Main 1344–1354) and **`openAfterMenus`** (336–346) are the same "after the menu sheet
   closed" helper.
2. **Opening the Markdown source** is copied: `markdownSourceItem.onTriggered` (1861–1866) and the `markdownMode`
   shortcut (5518–5521).
3. **The list of checkable app items** `["hand","select","snip","pdfText","geometry","touchDrawing","write"]` appears
   at Main 1060 and 1363. `Toolbox.qml:326` `isTool` has a *different* list (without touchDrawing and write).
4. **The button label formula** `button.label !== "" ? button.label : button.name` appears three times (1058, 1315,
   1361).
5. **Undo and redo pairs: 6 implementations.** Main has `keyboardUndo` (826), `formatUndo` (857) and `viewPill`
   (2628–2650; the visibility formula `!win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar` is written
   three times). The others are in `Toolbox.qml`, `PhoneDock.qml` and `PageGrid.qml`.
   - **Fix:** an `UndoRedoButtons.qml` with an `objectNamePrefix` property (tests use keyboardUndoButton,
     formatUndoButton, undoButton, toolUndoButton, dockUndoButton), and one `win.undoPlace` enum instead of four
     booleans.
6. **Right click works like press and hold:** the same `TapHandler { acceptedButtons: Qt.RightButton;
   acceptedDevices: PointerDevice.Mouse … }` appears 7 times in Main and 12 times in 11 other files.
   - **Fix:** `IconButton.secondaryOpensHold: true`.
7. **`url.substring(0, url.lastIndexOf("/"))`** to point a file dialog at a folder appears 7 times (Main 528,
   547, 3727, 4264, 4301; AnnotationList 53, …). **`app.pages.selectionCount > 0 ? app.pages.selectedPages() : []`**
   appears 7 times.
8. **`messageDialog.title = …; .text = …; .open()`** appears 3 times (5006, 5108, 5121). HomeView has its own
   `errorDialog`.
9. **The phone-class list** `["phonePortrait","phoneShort","tiny"].indexOf(…)` appears 9 times: Main 314,
   HomeView 27, SettingsPage 23, TabOverview 131, AdaptiveMenu 28, MenuSheet 45, PageMenu 44, TagsView 21,
   TodosView 24.
10. **Safe-area and keyboard geometry** is recomputed in SettingsPage (28–40), MenuSheet (36+) and BottomSheet
    (25–31) from the window's insets.
11. **Confirmation dialogs:** 82 `AdaptiveDialog`s (Main 22, HomeView 16, SettingsPage 5). About 30 are the same
    title, label and two-button box.
    - **Fix:** a `ConfirmDialog.qml` (`text`, `acceptText`, `destructive`) saves about 15 lines each.
12. **Rename, three ways:** `RenameDialog.qml` (a tab: fixed extension, validation through
    `app.tabRenameProblem`), HomeView `renameDialog` (2307: the whole file name, no validation) and
    `InlineRename.qml`. The behaviour differs for the same action.
13. **Shortcut hints written into texts:** 76 `qsTr` strings with hard-coded keys, such as "Search (Ctrl+F)",
    "(F11)" and "Print… (Ctrl+P)": 27 in Main, 9 in MarkdownFormatBar, 6 in SelectionPill, … At the same time
    `withKeys()` exists and the keys can be changed (`ShortcutsModel`). After a rebind the tooltips and menu entries
    are wrong.
14. **The Escape logic** is spread over 9 `Shortcut`s (2407, 4945, 5267, 5421, 5422, 5423, 5490, 5497, 5537),
    each with a hand-written exclusion condition (see §6).
15. **The 20 `Connections { target: app }` blocks** in Main: 9 of them (5298–5343) only open a dialog.
    - **Fix:** one Connections per new file.
16. **The 23 UI test fixtures** each copy the engine setup, image providers, `wait`, `find` and `findItem` (see §7).

## 3. Dead code (each item checked with grep in `qt/src` and `qt/tests`)

| Item | Where | Evidence |
|---|---|---|
| `BottomSheet.qml` (104 lines) | whole file | never instantiated. The only mentions are Main 73 (a comment) and `docs/adaptive-layout.md:623,678`. |
| `cleanPage` | Main 497 | declared, never read |
| `zoomButton.menuPending` | Main 2884 | declared, never read |
| `MarkdownPanel.lineEnd()` | MarkdownPanel 130 | never called |
| `writeButton.markdownMode` | Main 1825, 1854, 1855, 1871, 5517 | Constantly `true` ("kept for the tests"). It makes `markdownItem` permanently checked. Only `MainWindowTest.cpp:6159,6231` assert it. |
| `toolArea.popupSide` | Main 1382, 1980–1981, 2040; `StickerButton.qml:12,30-31` | a constant `"top"`; the left, right and bottom branches are dead |
| The deprecated text mode: `TextFlowPanel.qml` (241 lines) and its Main wiring | Main 207, 1818–1819, 1829, 1833, 2977–2986, 3058, 3063, 5362 (`app.textFlowActive`), 5510 | Unreachable from the UI ("DEPRECATED 2026-09-26"). Only `MainWindowTest.cpp:6832` opens it, with `invokeMethod(panel, "open")`. Removing it also removes C++ `TextFlow*` (canvas and app area). |
| A stale test step | `AdaptiveAuditTest.cpp:362-365` | Calls `window.chooseToolbar("railLeft")`, which no longer exists. The unchecked `invokeMethod` returns false, so the "sideToolbar" picture shows nothing. It also resets the layout key `"toolbar"` of the tool bar removed in 0.8.0. |
| Tests that assert removed UI is absent | `MainWindowTest.cpp:5413,5414,5467-5470,6158,7049,8266`; `ToolboxTest.cpp:452,1262,1263,1319,1609,1964,1965,2097`; `AdaptiveLayoutTest.cpp:398,1728,2835` | about 20 `EXPECT_EQ(find("penButton"), nullptr) << "… is gone"`. They pin history and never fail. |
| `onRail()` fallback to `toolStack_` | `MainWindowTest.cpp:227` | Folding into stacks was removed (qt/rail-scroll). `ToolboxTest.cpp:366` asserts that it never happens. |
| Doc comments in the wrong place | Main 105–106 ("What was chosen by hand…" above `openTemplateSave`), 157–158 ("The Pages button…" above `showHistory`, but it belongs to `showSidebar`) | |
| `offered: true` | Main 1227 | the default |
| ids never referenced | `tabSwipe` (4562), `zoomTaps` (2895), `hoverHandler` (5186; used only by its own ToolTip) | harmless; remove them while moving the code |
| `typeof win !== "undefined"` guards | 25 files (AdaptiveDialog 20, AdaptiveMenu 26/38/39/96/98, BottomSheet, DocumentCard 76, EmojiPicker 14/22, HomeView 24/38, MenuSheet 16/33/36, PageMenu 20/23/38/280, PageGrid 349, …) | Every component is created in Main's context (the canvas-only QML of `tests/quick` uses none of them). The guards only turn a broken reference into a silent default. |

## 4. Backward compatibility to remove (xournal-qt's own old data)

| Where | What | Removing it means |
|---|---|---|
| `Main.qml:247-253` `zenAuto` | Keeps honouring the 0.7.0 reader-chrome choice: `layoutChoice("chrome") === "full"` means a tiny window starts without Zen. | `zenAuto: adaptive.layoutClass === "tiny" && layoutChoice("zen") !== "off"` |
| `Main.qml:268` in `setZen` | `chooseLayout("chrome", "")`: clears the choice of 0.7.0 | Delete the line. Settings files from 0.7.0 keep a dead `layout/tiny/chrome` key; harmless, or let C++ `SettingsModel` drop unknown layout keys (settings area). |
| `AdaptiveLayoutTest.cpp:2819-2824` | the test that pins the 0.7.0 behaviour ("the reader left there in 0.7.0") | delete it with the code |
| `AdaptiveAuditTest.cpp:362-365` | the layout key `"toolbar"` of the removed classic tool bar | delete the step |
| `AdaptiveLayoutTest.cpp:1728`, `ToolboxTest.cpp:1965,2097` and the rest of the absence tests (§3) | assertions about the 0.8.0 removals | delete them |
| History comments | Main 237 ("gone since 0.8.0"), 248, 268, 329–330 ("was removed in 0.8.0"), 1818, 2977, 5510 | Delete. They describe what was, not what is. |

Nothing else in QML migrates its own old data. The toolbox v1→v2 upgrade and the cache versions are in C++
(`shell/`). There is nothing for Xournal++'s formats here.

## 5. Readability for humans

- **Comments.** Main has 543 comment lines (9 %), Toolbox 13 %. The prose `///` lines above properties help: they
  say *why* a value is what it is (insets, the keyboard, why `clearY` exists). They bury the code where they narrate
  history or process:
  - branch names that mean nothing after the merge: "qt/top-bar" ×3, "qt/copy-tools" ×3, "qt/self-reference",
    "qt/ui-rework", "qt/replay-polish", "qt/rename", "qt/rail-scroll";
  - audit ids: "audit F6", "F7.2";
  - a quote of the author with a date (4958–4959);
  - "DEPRECATED … for now".

  Replace each with a link to the doc section, or delete it.
- **Naming.** Several state names are hard to keep apart: `zen`, `zenByHand`, `zenAuto`, `zenShown`; `readOnly`,
  `readOnlyOn`, `readOnlyOffered`, `reading`, `readStarted`. The same goes for `fullScreenMode` versus
  `windowFullScreen` versus `chromeMode === "compact"`, and for `noToolbar`, `topBarShown`, `toolsInFormatBar`,
  `undoInToolBar` and `undoInFormatBar`. In ViewModes, give them one enum each (chrome: full or compact; view:
  normal, zen or read) and derived flags with distinct names. Also: `toolBank` versus `toolArea` versus the anonymous
  `Item` at 1784 that declares the buttons and immediately reparents them to `toolBank`. That is three levels for
  one invisible holder.
- **Hidden ordering.**
  - `addPageMenu`'s `Instantiator` inserts at `index + 2` (2086). Reordering the two items above it breaks the menu.
  - The ten `DigitKey`s (5383–5392) could be one `Instantiator`.
- **Long bindings and magic numbers.** Toolbox placement (938–947, nested ternaries over compact, floating and
  vertical) and `viewPill.x/lowY` (2602–2614). There are pixel constants without names: 56/28/8 in the view pill
  margins, 24 and 96 as bottom offsets, `z` values from 0 to 100 scattered over the file (3, 4, 48, 49, 50, 57, 58,
  59, 60, 90–93, 95, 100). Put the z layers in one place: `readonly property QtObject layers` with named levels.
- **No long functions.** The functions in Main are short. The complexity is in bindings that read many win flags.

## 6. Correctness risks

1. **Esc does nothing in several states (likely bugs).** Several `Escape` shortcuts are enabled at the same time, so
   Qt treats the key as ambiguous and neither `onActivated` runs. Main 1769 already documents this behaviour for
   Back.
   - Full screen (5490) does not exclude `app.noteSelected`, `app.snip`, `app.todoStamp` or `replaying`. Full screen
     with a selected sticky note (5537), an armed snip (5422), the to-do stamp (5423) or the replay (5267) therefore
     gives two enabled Escapes. Replay is on the default top bar (`ToolboxModel.cpp:236`), which full screen's ⋯
     lists.
   - Presenting (5497) collides with 5537 (a note selected), 5422 and 5423 in the same way.
   - Only Zen's Escape (4945) excludes everything.
   - **Fix:** one `Shortcut { sequence: "Escape" }` that calls an ordered `win.escape()` dispatcher. Write failing
     tests first (AGENTS rule 5).
2. **Silent fallbacks.** The `typeof win` and `typeof menuSheet` guards mean that renaming a root member during the
   refactoring raises no error. Example: `AdaptiveMenu.asSheet` becomes false and phones get desktop menus.
   Remove the guards *before* or *together with* any rename (block B6).
3. **Recording key outside the shortcut model.** `Ctrl+Shift+R` (5274) is hard-coded and not in `ShortcutsModel`,
   so it cannot be rebound and the shortcut sheet does not list it.
4. **`linkPopup` positions itself in contentItem coordinates** (3206–3208) and compares the result with
   `win.height`, which includes the header. When it moves out of the root it needs `parent: Overlay.overlay` and
   `mapToItem`.
5. **`app` and `win` are per window**, which rules out singletons. `main.cpp:293-296` gives each undocked window its
   own context with its own `app` in **one engine**, so a QML singleton (`pragma Singleton` or
   `QML_SINGLETON`) would be shared by all windows. Any "make it a singleton" refactoring is wrong here. Use
   `ApplicationWindow.window` (attached, per window; `SettingsPage.qml:20` already does) or explicit properties.

## 7. Tests (`qt/tests/ui`)

- **Fixed waits.** The fixed `wait(ms)` calls add up to about **92 s** in `MainWindowTest.cpp`, 17 s in
  `AdaptiveLayoutTest.cpp`, 17 s in `ToolboxTest.cpp` and 16 s in `ReferenceWindowTest.cpp`. This counts each call
  once; calls in loops cost more. Every `SetUp` waits another 100 ms (197 tests in MainWindowTest alone). The common
  calls are `wait(50)` ×256, `wait(100)` ×207, `wait(300)` ×77, plus 15× `wait(1500)` "the software renderer is
  slow". An `until()` already exists (MainWindowTest 194): use it.
- **Copied fixtures.** 23 fixture classes each build the `QQmlApplicationEngine`, register 5 image providers and
  define `wait`, `find`, `findItem` and `click`. Only 2 of 23 register the `"annotation"` provider that `main.cpp:290`
  has.
  - **Fix:** a shared `tests/ui/UiFixture.h`. One place also makes a later "load Main once per suite" possible.
- **Coupled to implementation details:**
  - `saveChosen`, `setUpSaveDialog`, `sharePdfOf`, `drawerSlide`, `leavingFullScreen`, `windowedVisibility`;
  - `markdownMode`;
  - the deprecated `textFlowPanel.open` (6832).

  These pin the internal layout of the root (§1.3). Keep them working through forwarders during the split; later,
  drive them through the UI or a small documented test API.
- **Absence tests and the stale `chooseToolbar` step:** see §3.
- **Gaps:**
  - nothing tests Escape in combined states (full screen + note, snip, replay; presenting + snip);
  - there is no check that each `win.*` member read by another file exists: a test could load Main and assert the
    list of §1.3;
  - qmllint is not run. `qt_add_qml_module` already provides the `xqt-ui_qmllint` target. Even with the context
    properties it reports wrong property names in explicit (`id.prop`) references.
- **What survives the split.** `window->findChild` walks the QObject tree and `findItem` walks the item tree from
  `contentItem`. Both survive moving items into files, as long as objectNames stay unique (today four are duplicated:
  `toolRing`, `sidebar`, `insertPagesItem`, `bookmarkMenu`) and no Loader is introduced.

---

## Proposed refactoring blocks

Ordered by maintainability per effort. Main.qml is one file, so **every block that touches it conflicts with every
other one that does**. That is why B2 is serial and makes the later blocks touch Main only at an instance site of a
few lines.

### B1. Remove dead QML and the 0.7.0/0.8.0 leftovers (half a sitting)
- **Goal:** less code before the split; no history in the code.
- **Files:**
  - `BottomSheet.qml` (delete), the `XQT_QML_FILES` list in `qt/cmake/XqtApp.cmake`, `docs/adaptive-layout.md:623,678`;
  - `Main.qml` (`cleanPage`, `menuPending`, `markdownMode`, `popupSide`, zen and chrome compat 247–253 and 268,
    history comments, dangling doc comments);
  - `MarkdownPanel.qml:130`, `StickerButton.qml`;
  - the tests: `MainWindowTest.cpp` (the markdownMode asserts, absence asserts, the `toolStack_` fallback),
    `ToolboxTest.cpp` and `AdaptiveLayoutTest.cpp` (absence asserts, 2819–2824), `AdaptiveAuditTest.cpp:362-365`.
- **Steps:** delete each item; update the device checklist if it mentions the reader chrome.
- **Risk:** low.
- **Verify:** `ctest -L ui -R 'MainWindow|Toolbox|AdaptiveLayout'`, and build `xqt-ui`.
- **Conflicts:** B2 to B5 (Main.qml). Do it first.

### B1b. Remove the deprecated text mode (one sitting, with the canvas/app reviewer)
- **Goal:** remove `TextFlowPanel.qml`, its Main wiring (§3 lists the lines), the test `textModeTypesThePageText`, and
  C++ `TextFlow`, `beginTextFlow` and `textFlowActive`.
- **Risk:** medium (C++ API).
- **Verify:** `-L ui`, `-L canvas`.
- **Conflicts:** B2 (Main.qml), and the C++ blocks of `app/` and `canvas/`.

### B2. Mechanical split of `Main.qml` (1–2 sittings, **one agent, nobody else in Main.qml meanwhile**)
- **Goal:** reach the table of §1.2 with *no behaviour change*: about 22 new files, Main at about 1,000 lines.
- **Steps:**
  1. Move in this order, from least coupled to most:
     - dialogs: files 6, 5, 8, 9, 10, 7, 4, 11;
     - overlays, each with the overlay as the file root and instantiated where it was: 19, 18, 17, 16, 15, 20, 21,
       22;
     - menus and buttons: 13, 14, 12;
     - shortcuts: 23.
  2. For each id that is used outside its region (the "Needs" and "Exposes" columns), give the instance an id and
     add a `property alias` or a forwarding function on the file root. Edit the outside reference.
  3. Keep the members of §1.3 on the root as forwarders.
  4. Keep `win`, `canvas` and `menuSheet`.
  5. Add each file to `XQT_QML_FILES`.
  6. Commit per file or per group, so each step is bisectable.
- **Risk:** medium.
  - z, anchors and parent: follow the root-is-the-item rule.
  - A forgotten alias produces a `ReferenceError` in the log, but a guarded reader in another file would fail
    silently. Grep the log of the UI test run for `ReferenceError` and `TypeError` after each step.
- **Verify:** the full `-L ui` after each group (it is the only coverage of Main), and the screenshots with
  `XQT_SHOTS` against the README pictures for z-order regressions.
- **Conflicts:** everything that touches Main.qml (B1, B1b, B3–B5, B11). After B2, B3–B5 touch Main only at
  instance sites.

### B3. `WindowActions` facade and decoupling of the menus and buttons (one sitting, after B2)
- **Goal:**
  - `MoreMenu.qml`, `AppButtons.qml`, `ToolboxMenus.qml` and `WindowShortcuts.qml` stop naming 28 dialog ids and
    call `actions.*` instead (file 24);
  - remove duplicates 1, 2, 4 and 7 of §2;
  - one `Connections { target: app }` for the nine dialog openers.
- **Files:** `WindowActions.qml` (new), the four files above, and the instance site in Main.
- **Risk:** low to medium.
- **Verify:** `-L ui -R 'MainWindow|Toolbox|PageFiles|TemplateTool|StickerTool'`.
- **Conflicts:** B4 (WindowShortcuts.qml). Do B4 first or in the same sitting.

### B4. Escape and Back dispatcher; keys from the shortcut model (one sitting)
- **Goal:**
  - fix §6.1 (failing tests first: Esc in full screen with a selected note, an armed snip or a replay; presenting
    with an armed snip);
  - one ordered `escape()`;
  - Ctrl+Shift+R into `ShortcutsModel`;
  - `app.shortcuts.tip(text, id)` replaces `withKeys` and the 76 hard-coded hints.
- **Files:** `WindowShortcuts.qml`, `shell/ShortcutsModel.cpp`, and every QML file with a hard-coded hint
  (MarkdownFormatBar, SelectionPill, SearchBar, PageGrid, HomeView, TabStrip, NotePill, RecordButton).
- **Risk:** medium: Esc precedence is user-visible.
- **Verify:** the new tests, plus `-L ui`.
- **Conflicts:** B3; B11 for the hint strings in shared components.

### B5. State objects out of the root (one sitting, after B2)
- **Goal:** `WindowInsets`, `ViewModes` and `ChromeLayout` (files 1–3).
  - The root keeps only the §1.3 forwarders and the insets aliases for `main.cpp`.
  - Remove the redundant aliases of §1.7.
  - Replace the undo booleans with `undoPlace`.
  - Add `phoneLayout` to `quick/AdaptiveLayout` and use it in the 9 places of §2.9.
- **Risk:** medium: the bindings form chains through window, chrome and layout; check for binding loops in the log.
- **Verify:** `-L ui`, especially `AdaptiveLayoutTest` and `AdaptiveAuditTest`.
- **Conflicts:** B6 (the readers of `win.*`).

### B6. Explicit window access instead of `typeof win` (one sitting, after B5)
- **Goal:**
  - in the 25 guarded files, `readonly property var win: ApplicationWindow.window` with no guard, or explicit
    `required property`s for the 3–4 values a leaf needs (`adaptive`, `insets`);
  - `menuSheet` becomes `win.menuSheet`;
  - `canvasItem` gets no default `canvas`: Main passes it.
- **Risk:** low per file; easy to verify because a mistake now fails loudly.
- **Verify:** `-L ui`; grep the log for `TypeError`.
- **Conflicts:** B5, B11.

### B7. Split `HomeView.qml` (one sitting; it can run in parallel with B2)
- **Goal:** the table of §1.4. Also `DocumentGrid.qml` for the two grids and unified favourites (`favouriteRevision`
  goes away; the C++ part is a recent-model role).
- **Files:** HomeView and the new files only.
- **Risk:** low to medium.
- **Verify:** `-L ui -R 'MainWindow|AdaptiveLayout|Tags|Todos|QuickNote|Citations'` (the library tests).
- **Conflicts:** none with B2–B5, apart from Main's instance (unchanged).

### B8. Split `SettingsPage.qml` (one sitting; in parallel with B2 and B7)
- **Goal:** the table of §1.5, the section model instead of indices, and no keyboard recomputation.
- **Risk:** low.
- **Verify:** `-L ui -R 'Settings|AdaptiveLayout|MainWindow'`.
- **Conflicts:** B6 (one guard there).

### B9. Toolbox internals (half a sitting, after B2)
- **Goal:**
  - the delegates and the drag into their own files (§1.6);
  - one list of checkable app items, shared with `ToolboxMenus.qml` and `MoreMenu.qml` (§2.3); it belongs in
    `ToolboxModel` (C++), with an `appIsTool(name)` there.
- **Verify:** `-L ui -R Toolbox`.
- **Conflicts:** B3 (ToolboxMenus.qml).

### B10. Shared UI test fixture and fewer fixed waits (one sitting; tests only)
- **Goal:** `UiFixture.h` (engine, all 6 providers, `find`, `findItem`, `click`, `until`, `waitOpened`), with the 23
  fixtures derived from it; replace `wait(N ≥ 100)` with `until(condition)` where a condition exists.
- **Risk:** low; flakiness is possible where a wait hid a race. Run the changed files 3 times.
- **Verify:** `-L ui` timing before and after.
- **Conflicts:** test edits in B1, B1b and B4 (the same files). Do B10 after them or rebase.

### B11. Shared small components (one sitting, last)
- **Goal:**
  - `UndoRedoButtons.qml` (with objectName prefixes);
  - `IconButton.secondaryOpensHold`;
  - `ConfirmDialog.qml` replacing about 30 confirmations;
  - a `showMessage()` that HomeView also uses (it drops `errorDialog`);
  - one rename dialog for a tab and a card (§2.12).
- **Risk:** medium: it touches many files.
- **Verify:** the full `-L ui`.
- **Conflicts:** B2–B8. Do it after them.
