# Review: app-cpp (`qt/src/app/*.cpp|h`, `qt/src/quick/`)

Reviewed on `claude/admiring-pascal-hekja6` (2026-10-07). Nothing in the repository was changed. I counted members with
a script that reads every `Q_PROPERTY`/`Q_INVOKABLE`/signal in `AppController.h` and searches all 106 QML/JS files for
`app.<name>`, then for the bare name (to catch `pill.target.<name>`), then `qt/tests` and the other C++.

## Verdict

`AppController` is one god object, and spreading it over files did not reduce it. It is a single class of **17,052
lines of implementation** (`AppController.cpp` 5,673 lines plus 23 `App*.cpp` files that each implement more methods of
the same class), with a 2,157-line header that declares **179 `Q_PROPERTY`, 324 `Q_INVOKABLE` and 96 signals**.
`AppController.cpp` alone has 126 `#include`s. QML refers to it **2,208 times from 88 of 106 QML files** (787 of them in
`Main.qml`), and always through an untyped context property (`main.cpp:291`), so no tool tells you when a name goes
away. Every feature reads and writes the same private state.

Some parts already show the right pattern: `AudioControl`, `TimelineControl`, `ReferenceMode`, `VersionCompare`,
`PresenterConsole` and `Citations` are QObjects of their own. Each gets small accessors (for example
`std::function<DocumentSession*()>`) and QML reaches it as a `CONSTANT` sub-object (`app.audio`, `app.reference`, …).
The split should repeat that pattern feature by feature, in this order:

1. a safety net, a QML-API test;
2. deleting dead code;
3. a process-wide services object, which ends the main-window/second-window duplication;
4. one shared `CanvasActions` object, which replaces 51 members duplicated between `AppController` and `ReferenceMode`;
5. then the features, from small to large.

`DocumentCanvasItem` (2,193 lines) is dense but coherent. Splitting it is independent work with lower priority.

---

## 1. Structure

### 1.1 What `AppController` holds today (line ranges in `AppController.cpp` unless a file is named)

| Responsibility | Where | ~Lines |
| --- | --- | --- |
| Construction and wiring for two kinds of window | 145–339 (main), 342–361 (second window), `makeTabs` 363–441, dtor 443–470 | 330 |
| Handwriting search glue | 471–503 | 35 |
| Window logging (`XQT_LOG_WINDOW`), window factory, undock/dock | 505–657 | 150 |
| Deprecated text mode (TextFlow) | 684–745 | 60 |
| Markdown panel and box | 746–861, `AppMarkdownFormat.cpp` 353, `AppMarkdownImages.cpp` 169, `AppReplace.cpp` 82 | 720 |
| Relay of the current tab's signals | `currentTabChanged` 862–1030 | 170 |
| Selection, paste, sticky notes, PDF text (current view) | 1032–1146, 4582–4670, 4909–4965, 5529–5566 | 330 |
| Search | 1147–1175, 1461–1523, 2197–2270 | 190 |
| Page operations (sidebar/grid) | 1175–1342, 4892–4906, 5003–5383 | 600 |
| View layout settings, presenting, zoom, rotation | 1343–1460, 2091–2100, 4798–4890 | 230 |
| Tool state, widths, palette roles, fonts | 1632–2090, 4562–4580, 4671–4797 | 600 |
| Library switching, libraries home (Android), storage access | 2102–2770 | 670 |
| Start, recovery, received files | 2766–3080 | 320 |
| Tabs, opening | 3082–3360 | 280 |
| Saving, hybrid PDF, versions, share, archive | 3361–4515 | 1,150 |
| Export PDF, print, chapters, links | 5384–5673 | 290 |
| One file per feature, same class | `AppPageFiles` 788, `AppLinks` 611, `AppTextFiles` 577, `AppTodos` 366, `AppTemplates` 291, `AppEncryption` 274, `AppToolbox` 244, `AppBookmarks` 210, `AppRename` 197, `AppInkCopy` 198, `AppPaper` 193, `AppAdopt` 188, `AppStickers` 176, `AppSnip` 171, `AppShareZip` 157, `AppTags` 147, `AppAnnotations` 108, `AppHelp` 107, `AppQuickNote` 99 | 5,300 |

The `App*.cpp` split is only cosmetic. Each file reaches into members that are declared for other files, for example:

- `AppTags.cpp:53` defines `tabsWithFile(file)`, which uses `tabsShowing`, defined in `AppTodos.cpp:62`.
- `AppToolbox.cpp` calls `endSnip` (`AppSnip.cpp`), `insertStickyNote` and `colorRoles`.

The header's private section (`AppController.h:1803–2157`) mixes the state of about fifteen features in one list.

### 1.2 Two kinds of window in one class

A second window is a second `AppController` built by a second constructor (`AppController.cpp:342`). It *borrows* the
process-wide objects of the main window. The result is **12 `own*` / raw-pointer pairs**: `ownSettingsView/settingsView`,
`ownToolbox/toolbox`, `ownShortcuts/shortcuts`, `ownLibrary/library`, `ownLibraryBookmarks`, `ownLibraryTags`,
`ownLibraryTodos`, `ownRecent/recent`, `ownPageClipboard/pageClipboard`, `ownHandwriting/handwriting`,
`ownHandwritingView/handwritingView`, plus `colors` and `app`. The cost:

- The wiring is duplicated in both constructors (`167–175` vs `359–366`, and the `pages`/`filteredPages`/… block at
  `179–192` vs `367–378`).
- 16 `primary`/`isSecondary()` branches.
- The loop `primary ? primary : this` + `main->windows` appears four times (`AppController.cpp:3786`,
  `AppTags.cpp:56`, `AppTodos.cpp:64`, `AppMarkdownImages.cpp:97`).
- Destruction order depends on comments such as `AppController.h:1971` ("a second window is a child of the main one,
  deleted after the main one's members are gone").

These objects belong to the process, not to a window. They should live in one object owned by `main()`.

### 1.3 The current-document relay

`currentTabChanged` (`862–1030`, 169 lines) disconnects and reconnects **41 signals** of the current
`DocumentSession`/`CanvasView`/`ViewController` onto `AppController`'s own signals. It then emits **25 change signals
blindly**. A new per-document property has to be added in three places (getter, relay connect, blind emit). This relay
is why the class cannot be split without a replacement: every extracted feature object needs to hear "the current
document changed". The replacement is one small `CurrentDocument` object (block C) that the feature objects connect to
themselves.

### 1.4 An undeclared interface between `AppController` and `ReferenceMode`

Five pills take the controller as an untyped target: `SelectionPill.qml:13`, `NotePill.qml:14`, `PdfTextPill.qml:14`,
`ContextPill.qml:18`, `PdfTextHandles.qml:13` (`property var target: app`). `ReferenceSplit.qml` points them at
`app.reference` instead. The two classes therefore share **51 members with identical names**, kept in step by hand:

> `canGoBack canGoForward canGroup canPaste canUngroup clearPdfTextSelection clearSelection copyPdfText copySelection
> copyStickyNote cutSelection cutStickyNote deleteSelection deleteStickyNote dragPdfSelection fitWidth goToPage
> groupSelection hasSelection insertImage markPdfText navigateBack navigateForward noteBox noteColor noteCovers
> noteSelected pageCount pageNumber pages pasteAt pasteElements pdfSelectionBox pdfSelectionEnds pdfTextIsSelected
> selectAllOnPage selectMoreAvailable selectMoreOffered selectPdfTextAt selectedCount selectedText selectingMore
> showPdfSelection title ungroupSelection view writeNoteText zoomIn zoomOut zoomPercent zoomToRealSize`

Nearly all of them are `return canvas() ? canvas()->X() : default;` in one class and `shownView ? shownView->X() : …`
in the other. They have already drifted apart (§6.1). This is the clearest first extraction: one `CanvasActions` class,
instantiated twice.

### 1.5 Module boundaries

- **The app layer has no target of its own.** `AppController*.cpp`, the `App*.cpp` files, `AudioControl` and
  `TimelineControl` are compiled into `xqt-shell` (`qt/cmake/XqtApp.cmake:154–188`), which also exports `src/app` as a
  public include directory. Nothing in `shell/` includes `AppController.h` yet (checked), but nothing would stop it.
  Put the app layer in its own static library `xqt-app` that links `xqt-shell`.
- **Similar classes sit in different folders.** `AudioControl` and `TimelineControl` are in `app/`. `ReferenceMode`,
  `VersionCompare`, `PresenterConsole` and `Citations`, which play the same role, are in `shell/`. Pick one place for
  "window feature controllers"; I suggest `app/` once it is its own target.
- **Static helpers have nothing to do with the controller.** `iconUrl` (`5384`) only needs the resource directory, yet
  QML calls `app.iconUrl(...)` **214 times in 30 files**. For `IconButton.qml`, `AdaptiveMenu.qml`, `MenuSheet.qml`,
  `ToolEntryButton.qml` and `InkTextToast.qml` it is their *only* dependency on `app`. A QML singleton
  (`Icons.url(name)`) makes these components usable without a controller. `lowerExtension` (`3354`) and `toStringList`
  (`4312`) are free functions too.
- **QML uses the controller as a message bus.** QML *emits* the controller's signals: `app.pageActionDone(...)` in
  `StickerPicker.qml:94,101,115,336,385,387` and `TemplateSaveDialog.qml:24`, and `app.message(...)` in
  `PageFiles.qml:494`. A `Notices` object (`app.notices.toast()/.error()`) would give the 98 `pageActionDone` and 139
  `message` emits in C++ one sink with a name.

### 1.6 `qt/src/quick`

`DocumentCanvasItem.cpp` has 2,193 lines. Its big parts:

- six scene-graph node classes in an anonymous namespace (`73–470`: `TileNode`, `PageNode`, `PageClipNode`,
  `GeometryNode`, `CurtainNode`, `CanvasRootNode`);
- `HoverMarkItem` (`527–660`);
- pointer, cursor and link hover (`661–900`, with about 25 members in the header at `DocumentCanvasItem.h:262–300`);
- input (`eventFilter`, 189 lines at `1264`, nested up to 14 levels);
- painting (`updatePaintNode`, **314 lines** at `1880`; `updateGeometryNode` 114; `updateCurtainNode` 84;
  `updateSelectionNode` 66).

The per-page body of `updatePaintNode` (`1965–2165`) is a function of its own (`PageNode::update(...)`), with five
local lambdas (`gpuTone`, `keptIn`, `texCoords`, `meetsScreen`, …) that would become members or free functions.
`TextFlowEditor.{h,cpp}` (482 lines) only serves the deprecated text mode (§3).

### 1.7 `main.cpp`

`main()` is **311 lines** (`main.cpp:125–435`). It mixes:

- argument parsing;
- library choice;
- single instance;
- Android setup (`240–257`, `324–355`);
- the engine;
- **77 lines of screenshot hooks** (`358–432`: `XQT_SCREENSHOT_ACTION/SELECT/SEARCH/SET/POPUP`). Nothing in the repo
  uses them (no script, doc or CI references these variables; the README pictures come from `XQT_SHOTS` in the UI
  tests). `XQT_SCREENSHOT_ACTION` calls *any* `AppController` method by name, so the hooks silently depend on the
  API. `toggleSetsquare`/`toggleCompass` (`AppController.h:1487–1489`) exist only for this hook.

The window logger (`XQT_LOG_WINDOW`, `AppController.cpp:512–594`) also does not belong in the controller. It is the
sibling of `quick/InputLog`.

## 2. Duplication

1. **`AppController` ↔ `ReferenceMode`: 51 members** (§1.4). Example: `canPaste` exists at `AppController.cpp:1101`
   and `ReferenceMode.cpp:623`. They do the same thing, but one calls `StickyNotes::clipboardHasNote()`/
   `MixedSelection::clipboardHas()` and the other tests the MIME strings inline.
2. **Finding a file in the tabs of all windows, three ways:**
   - `tabsWithFile(file, except)` (`AppController.cpp:3783`, `fs::equivalent`);
   - `tabsWithFile(file)` (`AppTags.cpp:53`, which adds plain PDFs as background);
   - `tabsShowing(file)` (`AppTodos.cpp:62`, which adds text files and uses a local `sameFile` that exists only in
     `AppTodos.cpp:48`).

   One `OpenDocuments::find(file, Match{…})` on the process-wide window list replaces all three, plus the four
   "all windows" loops.
3. **Two constructors with duplicated wiring** (§1.2).
4. **`SaveWay` duplicates `DocumentSession::SaveKind`.** The switch at `AppController.cpp:3387–3408` maps one onto the
   other 1:1; only `ShareXopp` adds `attachedPdf`. Two hidden parameters are passed through members:
   - `nextSaveEncryption` (`AppEncryption.cpp:261`, consumed at `AppController.cpp:3411`);
   - `nextSaveMessage` (`3591`, consumed at `3412`).

   Each caller has to reset them by hand when `startSave` returns early (`AppEncryption.cpp:269`, `3593`). Pass a
   `SaveRequest` instead.
5. **Blocking twins of the background saves:** `save` (`3848`), `saveAs` (`4509`), `saveAsHybrid` (`4004`) and
   `exportXopp` (`4027`) block on `waitForSave()`. QML uses only the `*InBackground` versions. The blocking ones are
   test conveniences on the QML API (tests call `save` 374×, `saveAs` 73×, `saveAsHybrid` 60×). Move them to a test
   helper (`testing::saveNow(controller)`).
6. **Settings are reached as strings in 113 places.** The literal custom element `"xournalQt"` appears 113 times in
   21 `.cpp` files, with 7 local constants for it (`AppController.cpp:1735`, `AppPaper.cpp:30`,
   `AppPageFiles.cpp:64` "AppController.cpp's CUSTOM", `ReferenceMode.cpp:33`, `ShortcutsModel.cpp:10`,
   `PenFill.cpp:14`, `ScreenCalibration.cpp:16`). The app code alone has 90 raw `getCustomElement(...).get/set...`
   calls, most followed by a hand-written `customSettingsChanged()`. On top of that the settings exist twice for QML:
   - the generic `app.settings.get/set(key)` (`SettingsModel`, 59 keys);
   - about 15 typed setting properties on the controller (`viewColumns`, `pairedPages`, `pairsOffset`,
     `horizontalScrolling`, `viewRows`, `snapPages`, `darkPagesMode`, `snipResolution`, `toolbarHidden`,
     `textContinuous`, `colorPalette`, `documentMode`, `introSeen`, `pdfHighlightColor`, `searchFuzzy`).

   `documentMode` and `newTextDocuments` exist in both.
7. **Color conversions are written several times:** `toQColor` (`AppController.cpp:139`, `AppToolbox.cpp:35`) and
   `toColor`/`toColorKeeping`, plus 7 inline `QColor(c.red, c.green, c.blue)` in `AppToolbox`, `AppPaper`,
   `AppController`, `ReferenceMode` and `TextFlow`.
8. **Tests: 23 copies of the window fixture.** `SnipTest.cpp:57–80` and 22 others each build `AppController` + engine +
   6 image providers + `setContextProperty("app")`, and each has its own busy-wait `wait(ms)` helper. Every change to
   how the controller is built (block C) touches all 23. Do the shared `AppWindowFixture.h` first.

## 3. Dead code (each item checked by grep; "QML" = all of `qt/src/**/*.qml|js`)

### 3.1 The deprecated text mode can no longer be reached

`TextFlowPanel.open()` (`TextFlowPanel.qml:31`) is never called; `Main.qml` only *closes* it (`1833`, `3058`, `3063`).
The doc says so (`qt/docs/text-mode.md`: "Deprecated 2026-09-26 … the code stays for now"). What can go:

- `TextFlowPanel.qml` (241 lines);
- `quick/TextFlowEditor.{h,cpp}` (482 lines), and its registration in `DocumentCanvasItem.cpp:618`;
- `TextFlowSession` and the editor parts of `canvas/TextFlow.{h,cpp}` (563 lines). **Keep `TextFlow::styleFor`**
  (page margins): `MarkdownSession.cpp` (`90`, `235`, `260`, `381`, `437`, `497`) and `MarkdownFile.cpp` use it, so it
  moves to a `PageMargins.h`;
- in the controller: `beginTextFlow`, `updateTextFlow`, `endTextFlow`, `textFlowFamily`, `textFlowActive`,
  `textFlowPage`, `textFlowOverflow`, `textFlowChanged`, the members `flow`/`flowSession`/`flowPage`/`flowOverflow`
  (`AppController.cpp:682–745`, header `521–533`), and the guards in `currentTabChanged:863` and the dtor;
- `toolbarColors` + `defaultToolbarColors` (`AppController.cpp:1815–1838`, header `238–240`), which only fed the text
  mode's color row;
- tests: `tests/canvas/TextFlowTest.cpp` (191 lines), `MainWindowTest.textModeTypesThePageText` (`MainWindowTest.cpp:6832`)
  and `TEST(ToolbarColors, orangeByDefault)` (`TabsTest.cpp:348`; keep its PDF-highlight half);
- the hold-over `property bool markdownMode: true  // (always; kept for the tests and QML that read it)` (`Main.qml:1825`).

### 3.2 Members of `AppController` that QML does not use

**(a) Unused everywhere: delete** (no QML use, no test, no C++ outside the controller):

| Member | Header line | Note |
| --- | --- | --- |
| `printAllowed`, `copyAllowed` (props) | 184, 185 | Getters at `AppEncryption.cpp:62,67`. Printing and copying are refused in C++; no UI greys anything out |
| `hasFill`, `hasFillColor` (props) | 231, 232 | `ToolEntryEditor.qml:96` computes its own `hasFill` |
| `archiveExports` (prop) + `archiveExportsChanged` + `archiveRunning` | 1176 | A counter nobody reads (`4323`, `4351`, `4382`) |
| `fontFamily` (as a property) | 198 | The getter is used in C++; drop the `Q_PROPERTY` and `setFontFamily` |
| `exportXoppInBackground`, `suggestedXoppExport` | 1090, 1133 | Replaced by `shareForXournal` |
| `movePageDown` | 1377 | `movePageUp` is test-only too (below) |
| `defaultBookmarkLabel` | 871 | Used internally (`AppBookmarks.cpp:159`): drop `Q_INVOKABLE` |
| `toggleSetsquare`, `toggleCompass` | 1488–1489 | Only for the screenshot hook (§1.7) |
| signal `stickerBusyChanged` | 1710 | Never emitted, never connected |
| signals `documentTagsWritten`, `zipUnpacked`, `pdfTextSelectionCleared` | 1671, 1782, 1737 | Emitted or relayed (`AppTags.cpp:143`, `AppShareZip.cpp:115`, `AppController.cpp:974`); nobody listens (no QML handler, no spy) |

**(b) Used only by tests: take them off the QML API.** Make them plain C++ or test helpers, or delete them together
with the tests where the feature is gone:

- properties `hasFilePath`, `isHybrid`, `textMarkdown`, `markdownFontSize`, `markdownInPanel`, `lineStyle`,
  `hasLineStyle`, `fillEnabled`, `fillAlpha`, `fillColor`, `size`, `customWidth`, `palette`, `colorRole`,
  `canRedoPages`. Most of these are leftovers of the classic tool bar: QML now edits tools through toolbox entries
  (`ToolEntryEditor.qml`).
- invokables `checkTextFiles`, `highlighterOpacity`, `setPaletteColor`, `colorRoleOf`, `paletteColor`, `followPalette`
  ("for tool presets (qt/toolbox)", `AppController.h:504,507`; the toolbox does not call them), `openFile`, `moveTab`
  (QML moves tabs through `app.tabs`), `save`, `saveAs`, `saveAsHybrid`, `exportXopp`, `goToPage` (QML uses only
  `app.reference.goToPage`), `insertPageBefore`, `duplicatePage`, `deletePage`, `movePageUp`, `insertStickyNote`
  (called internally by `applyToolEntry`), `setColor`, `setSize`, `sizeWidth`.
- signals that only tests' `QSignalSpy`s hear: `stickerSaved`, `stickerPasted`, `templateSaved`, `templateInserted`,
  `pagesFromFileInserted`, `pagesExtracted`, `pageImagesExported`, `pageImageCopied`, `filesReceived`. They can stay as
  plain signals, but they belong on the feature objects (§7).

**(c) Called from `main.cpp`; they need no `Q_INVOKABLE`:** `openPaths` (connected as a slot), `shutdown`.

The selection, sticky-note and PDF-text members (`selectMoreOffered`, `selectedCount`, `noteBox`, `pasteAt`,
`pdfSelectionEnds`, …) *are* used, through `pill.target` (§1.4). Don't delete them by grepping for `app.` alone.

### 3.3 Other dead QML API (`qt/src/app` and `qt/src/quick`)

- `AudioControl`: `startRecording`, `playMoment` (Q_INVOKABLE; `playMoment` is called from C++ at
  `AppController.cpp:992`, so only drop the macro).
- `TimelineControl`: properties `durationText`, `hearing`, `shownCount`; `canStart`.
- `AdaptiveLayout`: `heightClass`, `classify`.
- `DocumentCanvasItem`: six test-only `Q_INVOKABLE`s (`darkOnGpuShown`, `previewsShown`, `mostTilesInAFrame`,
  `framesWithPreviews`, `forgetTileCount`; `showTextCursor` is unused). The tests could read them through the existing
  C++ accessors (`frameStats()`, `geometryShown()`) instead of the meta-object.

## 4. Backward compatibility to remove (xournal-qt's own old data only)

| What | Where | Removing it means |
| --- | --- | --- |
| Toolbox built from the tools "of before" on first start (pen/highlighter color and width, eraser kind, font, `toolVariants` shape) | `AppToolbox.cpp:50–100` (`migratedToolbox`), called at `AppController.cpp:203–205`; header `513–515`; doc `toolbox.md:47–48` | An empty `toolbox` setting gives `ToolboxModel::defaultEntries()`. Deletes ~55 lines and the need for `colorRoles` to be read before the toolbox exists. Pair with the v1→v2 JSON upgrade in `shell/ToolboxModel.cpp:20–22,1070–1098` (shell reviewer's area) |
| `toolbarColors` setting of "the classic tool bar of before 0.8.0" | `AppController.cpp:1826–1838`, header `238–239` | Goes with the text mode (§3.1) |
| `markdownInPanel`: "the setting … is gone: a value stored by an earlier version is not read. Only tests still switch it" | `AppController.cpp:2020–2031`, member `mdInPanel` (`AppController.h:2006`), parameter of `CanvasView::setMarkdownText` | A test-only mode is kept alive in production code. Rewrite the 10 test sites (`MainWindowTest.cpp:6191`, `6262`, `6539`, … and `CitationsTest.cpp:313`) to open the panel the way the UI does (hold the writing button / `beginMarkdownBox`), then drop the flag down to `CanvasView` |
| `textMarkdown` setting as a switch | `AppController.cpp:1986–1999`; set only by `applyToolEntry` (`AppToolbox.cpp:213`, `241`, always `true`) and tests | A one-way latch: after the first text-tool entry it is always on, and nothing in the UI turns it off. Plain (non-Markdown) text boxes come back only on a fresh settings file. Decide (ask the author) whether the text tool is always Markdown now; if so, delete the setting and the `false` path |

No data-format migrations of the app's own files were found elsewhere in `qt/src/app` or `qt/src/quick`.
`LibraryMigration` (`AppController.cpp:2443–2683`) moves the libraries between the app's own folder and the phone's
Documents when access is granted. That is a live Android feature, not compatibility code; it just belongs in its own
object (block H).

## 5. Readability

- **Header comments** (`///`, 837 lines of 2,157) are mostly useful: they give each member's meaning and link
  `qt/docs`. Three kinds bury the code:
  - **history comments** that belong in git: `AppController.h:238–239` ("of before 0.8.0"), `AppController.cpp:2020–2022`,
    `Main.qml:1818–1819` ("DEPRECATED (2026-09-26)…"), branch names in API docs (`AppController.h:504,507`
    "(qt/toolbox)", `1606` "(qt/self-reference)", `TimelineControl.h:52` "(qt/replay-polish)");
  - **ownership and lifetime facts kept in parentheses** instead of in the type: `AppController.h:1982–1984`
    ("(after `tabs`, reset before it)"), `1971` ("QPointer: a second window is a child…"). Member order *is* the
    lifetime; one `AppServices` plus one `WindowParts` struct with a comment block on top would say it once;
  - **parenthetical end-of-line asides on almost every `connect`** in the constructor (`145–339`) and in
    `currentTabChanged`.
- **Functions over 80 lines** (app and quick):
  - `updatePaintNode` 314;
  - `main` 311;
  - `startSave` 196 (`3361`);
  - `eventFilter` 189 (`DocumentCanvasItem.cpp:1264`);
  - `currentTabChanged` 169;
  - `updateGeometryNode` 114;
  - `setTodoDone` 112 (`AppTodos.cpp:122`);
  - `applyToolEntry` 101 (`AppToolbox.cpp:133`);
  - `DocumentCanvasItem::setView` 99;
  - `followDocumentLinkFrom` 95 (`AppLinks.cpp:158`);
  - `exportPageImages` 91;
  - `updateCurtainNode` 84;
  - `printDocument` 82 (`5571`);
  - `shareForXournal` 81 (`4192`);
  - `makeTabs` 79.

  Together they are 2,117 lines.
- **Naming.**
  - `app` means the `AppContext` in C++ (`std::shared_ptr<AppContext> app`, `AppController.h:1964`) and the
    controller in QML. Rename the member to `context`.
  - Getter and member names don't match: `snip` → `snipShape()`, `inkCopy` → `inkCopyArmed()`, `toolbox` →
    `toolboxObject()`, `reference` → `referenceObject()` vs `reference()`.
  - `markdownBoxSize` vs `markdownFontSize`, and `markdownActive` vs `markdownOnPage`, look the same to a reader but
    mean "panel" vs "on the page".
- **Magic numbers** are mostly named (`CUSTOM_WIDTH_TOOLS`, `TILE`, `LINK_HOVER_MS`). Some are not:
  - `std::clamp(size, 4.0, 400.0)`, twice (`2008`, `2037`);
  - `left(200)` in `saveWithMessage` (`3591`);
  - in `main.cpp`: `100`, `200`, `300`, `400` ms for the screenshot hooks and `1500` ms.
- **File-scope mutable statics:** `windowFactory`, `windowsStartMaximized` (`AppController.cpp:505–508`). They are
  process configuration and belong in `AppServices`.

## 6. Correctness risks (only ones with a line)

1. **The reference ignores the PDF's "no copying" permission.**
   - `AppController::copyPdfText` (`AppController.cpp:4947–4952`) refuses when `!session()->allowsCopying()`.
   - `ReferenceMode::copyPdfText` (`ReferenceMode.cpp:583`) and `ReferenceMode::copy` (`502–509`, Ctrl+C while the
     reference has the keys) copy anyway.

   A protected PDF opened as reference, or the document beside itself, lets you copy its text. This is a direct result
   of the duplicated interface (§1.4). Write a failing test first (AGENTS rule 5).
2. **Background jobs have no owner.**
   - 18 jobs in the app layer go to `QThreadPool::globalInstance()` (`AppController.cpp:3003`, `4014`, `4252`, `4354`,
     `4432`, `4457`; `AppLinks.cpp:557`; `AppPageFiles.cpp:117`, `211`, `317`, `677`, `729`; `AppTags.cpp:125`;
     `AppTemplates.cpp:140`, `167`; `AppTodos.cpp:158`, `178`; `AppAdopt.cpp:38`).
   - Only `AppAdopt` lowers the priority to idle (AGENTS.md: "goes to a background worker at idle priority").
   - `shutdown()` (`658–676`) does not drain them. Several *write documents*: `pdfkeywords::write` (`AppTags.cpp:127`),
     `todos::setInMarkdownFile` (`AppTodos.cpp:161`), the link rewrite (`AppLinks.cpp:557`), page files.
   - At quit they finish only in the global pool's static destructor, after `main` has returned. Their callbacks post
     to `qApp`/`QCoreApplication::instance()`, which is gone by then.
   - Nothing serialises them against a `DocumentSession` save of the same file. `setDocumentTags` checks only "unsaved
     changes" before it starts (`AppTags.cpp:115–122`).
   - Fix: one `BackgroundJobs` owned by `AppServices` (its own `QThreadPool`, idle priority, `waitForDone()` in
     `shutdown`), plus a per-path guard shared with `DocumentSession` saves.
3. **Save parameters travel as members** (`nextSaveEncryption`, `nextSaveMessage`; §2.4). It is correct today only
   because both callers reset after `startSave`. The recursive `startSave(Hybrid, …)` at `3384` consumes them in the
   inner call. One more caller that forgets the reset applies a password to an unrelated later save.
4. **Secondary windows hold raw pointers to the main window's members** (`settingsView`, `toolbox`, `library`,
   `recent`, `pageClipboard`, …; `342–358`), yet are destroyed as QObject children *after* those members
   (`AppController.h:1971`). Their destructor resets `referenceMode`/`compareMode`/`tabs`/`Citations(library)`, which
   still see the dangling pointers. It does not crash today because none of them reaches through. `AppServices` with a
   lifetime longer than every window removes the hazard.

## 7. Tests of this area

- **No unit test layer.** `AppController` is tested only as a whole: 48 test files construct it, and 23 of those load
  the full `Main.qml`. Tests call its C++ API directly (e.g. `controller->setMarkdownInPanel`, `->textFlowActive()`,
  `->save()`), so they are coupled to it. Every extraction below has to edit tests. The plan is to update them in the
  same block (sed `controller->foo(` → `controller->x().foo(`), **not** to keep forwarding methods: forwarders would
  rebuild the god object.
- **Fixed waits.** The UI tests have **926 fixed `wait(ms)` calls adding up to 187 s of pure sleeping**
  (`MainWindowTest.cpp` alone: 385 calls, 92 s). They use a busy-wait helper copied into 23 files
  (`SnipTest.cpp:87`, …). Most can be `QTRY_VERIFY`/`QTest::qWaitFor` on the condition the next line asserts. This is
  the biggest lever for the "routine run under a minute" rule.
- **`MainWindowTest.cpp`** has 10,408 lines and 197 tests in one fixture. Split it along the same feature lines as the
  controller (tool, pages, markdown, search, files), so the test file for a feature moves with that feature's block.
- **Tests of removed features:** `TextFlowTest.cpp`, `MainWindowTest.textModeTypesThePageText`,
  `TEST(ToolbarColors, orangeByDefault)` (§3.1).
- **Missing tests:**
  - nothing checks that the names QML uses exist (block A);
  - nothing checks that `AppController` and `ReferenceMode` keep the pill interface the same; that gap produced
    §6.1;
  - nothing covers quitting while a background document write runs (§6.2).

---

## Proposed refactoring blocks

Ordered by maintainability gained per effort. "Conflicts" means the blocks touch the same files and should not run in
parallel worktrees. Block G is first among the extractions on purpose: it is small and sets the pattern the others copy.

The shape every extracted feature follows:

```text
AppController (per window, thin facade, context property "app")
 ├─ CONSTANT QObject* sub-objects:  app.edit  app.tools  app.files  app.pageOps  app.search  app.markdown
 │                                   app.libraries  app.links  app.startup  (+ existing: tabs pages versions toolbox
 │                                   reference compare presenter citations audio timeline library settings …)
 ├─ WindowContext  { AppServices&, TabManager&, CurrentDocument&, Notices& }   ← what every sub-object gets
 └─ keeps only: tabs/current tab, homeVisible, title/modified/saving, window dock/undock
AppServices (one per process, owned by main()):
   AppContext, Palette, SettingsModel, ToolboxModel, ShortcutsModel, LibraryModel(+Bookmarks/Tags/Todos), RecentFiles,
   PageClipboard, HandwritingSearch/Settings, LibraryInkJob, BackgroundJobs, OpenDocuments (all windows' tabs)
```

### A. Safety net: QML API test and shared UI fixture (½ day)

- **Goal.** Make renames safe before anything moves.
- **Files.**
  - New: `qt/tests/ui/QmlApiTest.cpp`, `qt/tests/ui/AppWindowFixture.h`.
  - The 23 UI test fixtures (`SnipTest.cpp`, `TagsTest.cpp`, `ToolboxTest.cpp`, `MainWindowTest.cpp`, …).
- **Steps.**
  1. `QmlApiTest` walks `qt/src/app/qml/*.qml|js`, collects `app.<a>` and `app.<a>.<b>`, and checks `<a>` against
     `AppController::staticMetaObject` (properties, methods, signals) and `<b>` against the sub-object's metaobject,
     taken from a live `AppController`. It also checks the pill interface: every member the five pills read from
     `target` (§1.4) must exist on both `AppController` and `ReferenceMode`, later on `CanvasActions`.
  2. Move the window fixture (controller + engine + image providers + context property + the `wait` helper) into
     `AppWindowFixture.h`, and make the 23 fixtures use it.
- **Risk.** Low. The regex could miss `app[...]` (there are none today) or aliases (only `pill.target`, handled).
- **Verify.** `ctest -L ui -R QmlApi`. Also rename one member by hand and see the test fail.
- **Conflicts.** None with the src blocks. It touches every UI test's `SetUp`, so do it before blocks that edit tests.

### B. Delete dead code and old-data compatibility (½–1 day)

- **Goal.** Shrink the surface before splitting it.
- **Files.**
  - `AppController.h/.cpp`, `AppToolbox.cpp`, `AppEncryption.cpp`, `AppBookmarks.cpp`;
  - `TextFlowPanel.qml`, `Main.qml` (text-mode remnants), `quick/TextFlowEditor.*`, `quick/DocumentCanvasItem.cpp:618`;
  - `canvas/TextFlow.*` (keep `styleFor` → `canvas/PageMargins.h`), `MarkdownSession.cpp`, `MarkdownFile.cpp`
    (include change);
  - `qt/cmake/XqtApp.cmake`;
  - tests: `TextFlowTest.cpp`, `MainWindowTest.cpp:6832`, `TabsTest.cpp:348`;
  - docs: `qt/docs/text-mode.md` (delete), `toolbox.md:47–48`.
- **Steps.**
  1. Delete §3.1 (the text mode).
  2. Delete §3.2(a).
  3. Drop `Q_INVOKABLE`/`Q_PROPERTY` from §3.2(c).
  4. Delete `migratedToolbox` (§4).
  5. Ask the author about the screenshot hooks. If they are not wanted, delete `main.cpp:358–432` and
     `toggleSetsquare/Compass`.
  6. Leave §3.2(b) for the feature blocks; they move with their feature.
- **Risk.** Low. `TextFlow::styleFor` must survive. `CanvasView::setMarkdownText`'s `inPanel` parameter stays until
  the `markdownInPanel` tests are rewritten (that rewrite can be its own small step).
- **Verify.**
  - Build `xqt-shell`, `xournal-qt` and the canvas, markdown and ui test binaries.
  - `ctest -L "canvas|markdown|shell"`.
  - The ui tests that were touched.
  - `QmlApiTest`.
- **Conflicts.** It touches `AppController.*`: run it alone, before C–N.

### C. `AppServices`, `OpenDocuments`, `CurrentDocument` (1 day)

- **Goal.** One owner for the process-wide objects, and one source of "the current document changed".
- **Files.**
  - New: `app/AppServices.{h,cpp}`, `app/CurrentDocument.{h,cpp}`, `shell/OpenDocuments.{h,cpp}`.
  - Changed: `AppController.h/.cpp` (both constructors, `makeTabs`, dtor, `tabsWithFile`), `AppTags.cpp:53–76`,
    `AppTodos.cpp:48–77`, `AppMarkdownImages.cpp:97`, `main.cpp`, `AppWindowFixture.h`, the shell tests that do
    `AppController c;`.
- **Steps.**
  1. Move the 12 `own*`/pointer pairs, `colors`, `app` (renamed to `context`), `windowFactory` and the start-maximized
     flag into `AppServices`.
  2. `AppController(AppServices&, QObject*)`. Delete the second constructor; a second window is just another
     `AppController` on the same services.
  3. `OpenDocuments` keeps the list of windows and offers `find(file, {except, plainPdfBackground, textFiles})`. It
     replaces the three tab finders and the four loops.
  4. `CurrentDocument` holds the current `DocumentSession*`/`CanvasView*` with one `changed()` signal.
     `currentTabChanged` sets it. The relay connections stay for now; they move with their features.
  5. Add `BackgroundJobs` (§6.2) to `AppServices` and drain it in `shutdown()`.
  6. Tests that construct `AppController` directly get a `testing::Services` helper.
- **Risk.** Medium: destruction order, and the main-window-only work in the dtor (message sink, `VersionCache`).
  `ReferenceWindowTest` and `TabsTest` (undock/dock) cover the second-window paths.
- **Verify.**
  - `ctest -L shell`.
  - `ReferenceWindowTest`, `TabsTest`, `RecoveryTest`, `MainWindowTest`.
  - By hand: undock a tab, close the main window first, then the second.
- **Conflicts.** `AppController.*`, `main.cpp`. Must come before D–N.

### D. `CanvasActions`, shared by the document and the reference (1 day)

- **Goal.** Remove the 51 duplicated members and fix §6.1.
- **Files.**
  - New: `shell/CanvasActions.{h,cpp}`.
  - Changed: `AppController.*` (selection/notes/PDF-text members, `1032–1146`, `4582–4670`, `4909–4965`, `5529–5566`),
    `shell/ReferenceMode.{h,cpp}` (the same members), the pills (`SelectionPill`, `NotePill`, `PdfTextPill`,
    `ContextPill`, `PdfTextHandles`, `ReferenceSplit.qml` targets), and the tests that call these members.
- **Steps.**
  1. Write a failing test: copying PDF text in a reference whose PDF forbids copying is refused.
  2. Write `CanvasActions(std::function<CanvasView*()> view, Policy)`. `Policy` holds: may edit, reading only, the
     keys go here, the toast sink. It carries selection, group, paste, notes, PDF text, zoom and back/forward, and
     checks `allowsCopying` in one place.
  3. Expose it as `app.edit` and `app.reference.edit`. Set the pills' `target` to `app.edit` / `app.reference.edit`.
  4. Keep `AppController`'s key-routing logic (`editedReference`, `keyCanvas`) as the policy that picks the target for
     keyboard shortcuts.
- **Risk.** Medium. The routing rules ("the reference has the keys and is written in") in `copySelection` (`1059`),
  `cutSelection` (`1075`) and `pasteElements` (`1084`) are subtle; port them as a `CanvasActions* keyTarget()` on
  `AppController`.
- **Verify.** `ReferenceModeTest`, `ReferenceWindowTest`, `CopyToolsTest`, `StickerToolTest`, `MainWindowTest`
  selection/note tests, and `QmlApiTest` (pill interface).
- **Conflicts.** `AppController.*`, `ReferenceMode.*`, the pill QML.

### G. `VersionActions` onto `app.versions` (½ day; first feature extraction, the template)

- **Goal.** Prove the pattern on a small, self-contained feature that already has a sub-object.
- **Files.** `shell/VersionsModel.{h,cpp}` (or a new `app/VersionActions` that `app.versions` exposes),
  `AppController.cpp:3587–3711` (`saveWithMessage`, `setVersionMessage`, `viewVersion`, `viewingVersion`,
  `compareWithNow`, `compareVersions`, `openVersionAsCopy`), `HistoryPanel.qml`, `Main.qml` (15 uses), `VersionHistoryTest.cpp`.
- **Steps.**
  1. Move the methods; they get a `WindowContext`.
  2. sed QML `app.viewVersion` → `app.versions.viewVersion`, and the same for the others.
  3. Make the save message an explicit `SaveRequest` field instead of `nextSaveMessage` (§2.4, half of it).
- **Risk.** Low.
- **Verify.** `VersionHistoryTest`, `VersionsTest`, `QmlApiTest`.
- **Conflicts.** `AppController.*`, `Main.qml`.

### E. `ToolControl` (`app.tools`) (1–1½ days)

- **Goal.** The tool in hand, toolbox entries, palette roles, widths, fonts, geometry tools, the snip, ink copy and the
  to-do stamp.
- **Files.** `AppController.cpp:1632–2090`, `4562–4580`, `4671–4797`; `AppToolbox.cpp`, `AppSnip.cpp`,
  `AppInkCopy.cpp`, `AppPaper.cpp` (`inkForPaper`, `paperSwatches`); `Toolbox.qml`, `ToolGroups.qml`,
  `ToolEntryButton.qml`, `ToolEntryEditor.qml`, `Main.qml`; `ToolboxTest.cpp`, `ColorPalettesTest.cpp`, `SnipTest.cpp`,
  `CopyToolsTest.cpp`.
- **Steps.**
  1. Move the members.
  2. Delete the classic-toolbar properties of §3.2(b) (`lineStyle`, `fill*`, `size`, `customWidth`, `palette`,
     `colorRole`, `setPaletteColor`, `colorRoleOf`, `paletteColor`, `followPalette`, `setSize`, `sizeWidth`). Rewrite
     their tests against toolbox entries, or as plain C++ helpers where they test real logic (palette following).
  3. Move `applyToolEntry` (101 lines) into per-type helpers (`applyInk`, `applyEraser`, `applyLaser`, `applyText`).
- **Risk.** Medium: `toolChanged` is heard widely in QML (3 `Connections`), and the snip and stamp restore the
  previous tool.
- **Verify.** `ToolboxTest`, `ToolboxModelTest`, `ColorPalettesTest`, `SnipTest`, `CopyToolsTest`, `QmlApiTest`; by
  hand on the device (pen hover cursor, eraser).
- **Conflicts.** `AppController.*`, `Main.qml`, `Toolbox*.qml`.

### F. `DocumentFiles` / `SaveControl` (`app.files`) (2 days, the largest)

- **Goal.** Save, save as, hybrid PDF, share, archive, export PDF, print and passwords in one object.
- **Files.** `AppController.cpp:3209–3360` (opening stays in `AppController` or goes to block M), `3361–4515`,
  `5419–5486`, `5571–5673`; `AppEncryption.cpp`; `Main.qml` (save, share and print dialogs, about 60 uses),
  `PrintDialog.qml`; `PdfPasswordTest.cpp`, `PdfOnlyModeTest.cpp`, `TextPdfTest.cpp`, the save tests in
  `MainWindowTest.cpp`.
- **Steps.**
  1. Make `startSave` take a full `SaveRequest`. Delete `SaveWay` (map onto `SaveKind` + `attachedPdf`),
     `nextSaveEncryption` and `nextSaveMessage`.
  2. Split `startSave` (196 lines) into: validation, building the request, what to do with the old .xopp, and the
     after-save steps.
  3. Move the blocking `save/saveAs/saveAsHybrid/exportXopp` into `tests/…/SaveNow.h`.
  4. `whenSaved`/`whenAllSaved`/`callWhenSaved` move here too.
- **Risk.** High (data). Do it after A, so `QmlApiTest` catches renames. Keep each step a commit.
- **Verify.** `ctest -L "session|shell"`, `PdfPasswordTest`, `VersionHistoryTest`, `RecoveryTest`, and the
  `MainWindowTest` save tests; the full suite before merging; by hand: save, Ctrl+S on a hybrid, share protected.
- **Conflicts.** `AppController.*`, `AppEncryption.cpp`, `Main.qml`; also block G (`saveWithMessage`), so do G first.

### H. `LibraryControl` (`app.libraries`) (1½ days)

- **Goal.** Library switching, libraries home and move (Android), storage access, zip share/unzip, renaming, tags, and
  to-dos across documents.
- **Files.** `AppController.cpp:2102–2190`, `2271–2770`; `AppShareZip.cpp`, `AppRename.cpp`, `AppTags.cpp`,
  `AppTodos.cpp`, `AppBookmarks.cpp` (the favourite part); `HomeView.qml`, `ShareZipDialog.qml`, `OpenZipDialog.qml`,
  `FolderChooser.qml`, `RenameDialog.qml`, `TodosView.qml`; `LibraryHomeTest.cpp`, `TagsTest.cpp`, `TodosTest.cpp`,
  `LibraryFilesTest.cpp`.
- **Steps.**
  1. Move the code.
  2. Its background writes use `BackgroundJobs` (from C) and the per-file write guard (§6.2).
- **Risk.** Medium: Android-only paths (`chooseLibrariesHome` is called from `main.cpp:241`) are tested only on the
  device.
- **Verify.** The listed tests, and the device checklist (Android libraries home).
- **Conflicts.** `AppController.*`, `HomeView.qml`.

### I. `PageActions` (`app.pageOps`) (1½ days)

- **Goal.** Page copy, paste, move, delete and duplicate; insert pages, note space, page size, rotation, background;
  templates; pages as files.
- **Files.** `AppController.cpp:1175–1342`, `4892–4906`, `5003–5383`; `AppPageFiles.cpp`, `AppTemplates.cpp`,
  `AppPaper.cpp` (the background part); `PageSidebar.qml`, `PageGrid.qml`, `PageMenu.qml`, `PageFiles.qml`,
  `InsertPagesDialog.qml`, `PageSizeDialog.qml`, `NoteSpaceDialog.qml`, `BackgroundDialog.qml`; `PagesTest.cpp`,
  `PageFilesTest.cpp`, `TemplatesTest.cpp`, `PastedPdfPagesTest.cpp`.
- **Steps.**
  1. Move the code.
  2. Delete the test-only single-page wrappers (`insertPageBefore/After`, `duplicatePage`, `deletePage`,
     `movePageUp/Down`, `5324–5383`); tests call `duplicatePages({i})` and the like.
- **Risk.** Medium.
- **Verify.** The listed tests and `ThumbnailsTest`.
- **Conflicts.** `AppController.*`, page QML.

### J. `SearchControl` (`app.search`) (½ day)

- **Files.** `AppController.cpp:1147–1175`, `1461–1523`, `2197–2270`; `AppReplace.cpp`; the search members in the
  header (`275–298`, `823–848`); `SearchBar.qml`, `SearchFilterChip.qml`; `LibraryFuzzyTest.cpp` and the search
  tests in `MainWindowTest.cpp`.
- **Risk.** Low. `filteredPages->setOnlySearchHits(false)` is wired in both constructors (`187`, `373`); move it here.
- **Verify.** The search and replace tests, and `QmlApiTest`.
- **Conflicts.** `AppController.*`, `SearchBar.qml`.

### K. `MarkdownControl` (`app.markdown`) (1 day)

- **Files.** `AppController.cpp:746–861`, `1986–2050`; `AppMarkdownFormat.cpp`, `AppMarkdownImages.cpp`,
  `AppTextFiles.cpp` (text files could instead go to F); `MarkdownPanel.qml`, `MarkdownFormatBar.qml`,
  `MarkdownTableEditor.qml`; `TextDocumentTest.cpp`, the markdown tests in `MainWindowTest.cpp`.
- **Steps.**
  1. Move the code.
  2. Resolve `markdownInPanel` and `textMarkdown` (§4).
- **Risk.** Medium (undo routing: `undoneMarkdown`, `4515`).
- **Verify.** `ctest -L markdown`, `TextDocumentTest`, the markdown UI tests.
- **Conflicts.** `AppController.*`, `Main.qml`.

### L. `LinkControl` (`app.links`) (½ day)

- **Files.** `AppLinks.cpp` (611 lines), `AppController.cpp:4966–5002` and `5397–5418` (`openLink`);
  `LinkStatusLine.qml`; `DocumentLinksTest.cpp`.
- **Risk.** Low–medium: back and forward across documents mix with the view's own history.
- **Verify.** `DocumentLinksTest`.
- **Conflicts.** `AppController.*`; also D (`canGoBack`/`navigateBack` are part of the pill interface: decide in D
  whether navigation lives in `CanvasActions` or here).

### M. `Startup` (`app.startup`) (1 day)

- **Files.** `AppController.cpp:2766–3080` (session start, recovery, reopening tabs, received files, `openUrls`),
  `3737–3771` (document mode, intro); `AppQuickNote.cpp`, `AppHelp.cpp`, `AppAdopt.cpp` (or with F); `main.cpp`
  (`startSession`, `quickNote`, `receiveFiles`); `IntroDialog.qml`, `DocumentModeDialog.qml`; `RecoveryTest.cpp`,
  `QuickNoteTest.cpp`.
- **Verify.** `RecoveryTest`, `QuickNoteTest`; by hand: kill the app, restart, recover.
- **Conflicts.** `AppController.*`, `main.cpp`.

### N. What is left of `AppController`, and `main.cpp` (½ day)

- **Goal.** `AppController` keeps tabs, the current document's title/modified/saving, home, view layout and presenting,
  docking. Target: under 1,000 lines plus a header of about 300.
- **Steps.**
  1. Move the window logger to `quick/WindowLog.{h,cpp}`.
  2. Move the screenshot hooks to `app/DevHooks.cpp`, or delete them (B).
  3. Move the Android wiring in `main.cpp` to `AndroidSetup.cpp`.
  4. Replace the remaining `currentTabChanged` relay with each object's own `CurrentDocument` connection.
  5. Move `iconUrl` to a QML singleton (`Icons.url`) and sed the 214 calls. The five components of §1.5 then no
     longer need `app`.
- **Conflicts.** Everything above. Do it last.

### O. Split `DocumentCanvasItem` (1 day; independent of A–N)

- **Files.** `quick/DocumentCanvasItem.{h,cpp}`. New: `quick/CanvasNodes.{h,cpp}` (the node classes, `73–470`),
  `quick/CanvasPointer.{h,cpp}` (cursor, hover mark, link hover: `527–900` plus the ~25 header members), and
  `PageNode::update(...)` (the per-page loop out of `updatePaintNode`, `1965–2165`).
- **Steps.**
  1. Make the test-only `Q_INVOKABLE`s plain C++ accessors.
  2. Remove the `TextFlowEditor` registration (with B).
- **Risk.** Medium: scene-graph thread rules. Everything moved stays in the sync phase; no logic change.
- **Verify.** `ctest -L quick`, the `XQT_BENCH_SCROLL` benchmark before and after (no regression in `frameStats`), and
  `golden` if available.
- **Conflicts.** B (the one line at `618`); nothing else.

### Cross-area (for the integrator)

- **Typed access to custom settings:** `xqt::customSettings(Settings&)` with `get<T>`/`set<T>` that calls
  `customSettingsChanged()` itself. It replaces 113 `"xournalQt"` literals in app, shell, session and canvas. Also
  decide the single QML path for settings (typed properties on feature objects vs `app.settings.get`).
- **Split `MainWindowTest.cpp`** by feature, and replace the 926 fixed waits with condition waits.
- **The toolbox v1→v2 JSON upgrade** (`shell/ToolboxModel.cpp:20–22`, `1070–1098`) should go together with block B's
  `migratedToolbox`.
