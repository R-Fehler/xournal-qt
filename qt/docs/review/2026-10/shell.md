# Review: `qt/src/shell` (state of `master-qt` at d6d16ba, 2026-10-07)

## Verdict

`shell` is not a module. It is the place where everything that is neither canvas nor session landed: 49 header/source
pairs, 31,303 lines, in eight unrelated roles (library storage and index, library jobs, library list models, image
providers, per-document list models, window controllers, settings models, platform utilities). Most of it is
competent and well tested, and the comments explain the "why" well. Three things make it costly to maintain:
- **`Library.cpp` is a god file.** It holds 2,473 lines: the storage, update, move handling, two search engines,
  the bookmark, to-do, tag, link and citation queries, and an old-cache converter. It is surrounded by about 15
  "entries from before …" upgrade paths that a 0-user alpha does not need.
- **Six image caches have one name.** They grew separately: each has its own `QQuickImageResponse` subclass, its
  own LRU and its own thread pool, and three different things are all called "preview".
- **Many classes mix being a model and being a service.** `LibraryModel` owns the library, its index, its file
  operations and its cache administration. `ThumbnailProvider` is also the process-wide session registry. `TabManager`
  owns the sessions and the views.

Removing the backward compatibility and splitting `Library.cpp` give the most per hour. Unifying the image-cache
plumbing comes next.

---

## 1. Structure

### What is in the folder (lines of .h + .cpp)

| Role | Classes | Lines |
| --- | --- | --- |
| A. Library storage, index, file ops (services) | `Library`, `LibraryIndex` (Library.*), `LibraryCache` (CacheLocation, Packs, CacheFolders, WriteScheduler), `InkTextStore`, `LibraryInkJob`, `DocumentFiles`, `DocumentPlaces`, `SyncConflicts`, `LinkRewrite`, `DocumentLinks` | 6,989 |
| B. Library jobs (QObject + worker) | `LibraryShare`, `LibraryUnzip`, `LibraryArchive`, `LibraryMigration`/`LibraryMove`, `ZipFile`, `ContentFiles` | 3,927 |
| C. Library list models | `LibraryModel`, `RecentFiles`, `LibraryBookmarksModel`, `LibraryTagsModel`, `LibraryTodosModel`, `GridSelection` | 3,580 |
| D. Image providers and caches | `ThumbnailProvider`, `PageSketches`+`SketchProvider`, `PreviewCache`+`PreviewProvider`, `HitPageProvider`, `MdSnippetProvider` (+ `AnnotationImageProvider` inside AnnotationsModel.*) | 2,899 |
| E. Per-document list models | `PagesModel`, `PageFilterModel`, `OutlineModel`, `LayersModel`, `AnnotationsModel`, `VersionsModel`, `annotations::` (Annotations.*), `DocumentChapters` | 3,807 |
| F. Window controllers | `TabManager`, `ReferenceMode`, `VersionCompare`/`CompareMarks`, `PresenterConsole`, `SessionRecovery`, `PageClipboard` | 3,487 |
| G. Settings-like models | `SettingsModel`, `ShortcutsModel`, `ToolboxModel`, `ColorPalettes`, `HandwritingSettings`, `ModelDownload`, `Stickers`/`StickersModel` | 4,697 |
| H. Platform utilities and domain helpers | `Citations`/`ArxivQueue`/`NetFetch`, `Todos`, `TodoCalendar`, `SystemApps`, `SingleInstance`, `LocalUrl`, `PdfPrinting` | 1,917 |

### Models for QML vs services: what each class really is

- **Real QML list models** (`QAbstractListModel`): `PagesModel` (+ the `PageFilterModel` proxy), `OutlineModel`,
  `LayersModel`, `AnnotationsModel`, `VersionsModel`, `RecentFiles`, `LibraryBookmarksModel`, `LibraryTagsModel`,
  `LibraryTodosModel`, `ShortcutsModel`, `StickersModel`. Mostly clean.
- **Called "Model" but not models:**
  - `ToolboxModel` is a `QObject` with `QVariantList` properties.
  - `SettingsModel` is a key/value facade over upstream `Settings`. It also has static helpers for other
    subsystems: `previewMemory`, `canvasMemory`, `fuzzyTypos`, `handWhenOpening`, `paperSize`, `paperType`.
- **Model plus service (split these):**
  - `LibraryModel` (LibraryModel.h, 375 lines; .cpp 1,407 lines; 61 properties and invokables) owns three things
    besides the grid's rows. One is `std::unique_ptr<Library>` and `std::unique_ptr<LibraryIndex>`. Another is the
    `QFileSystemWatcher`. The third is setting the process-wide statics `PreviewCache::setLibrary` and
    `DocumentPlaces::setLibrary`. It also does every file operation (`transfer`, `trashPaths`, `importUrls`,
    `resolveConflict`, `createFolder`, `rename`), cache administration (`setCacheInAppCache`, `measureCache`,
    `removeCaches`, `adoptFolderCaches`) and the old-layout conversion. `LibraryBookmarksModel`, `LibraryTagsModel`
    and `LibraryTodosModel` reach the index only through `LibraryModel::searchIndex()`, so they depend on a model
    to get a service.
  - `TabManager` is the tab strip's model and also the owner of every `DocumentSession` and `CanvasView`. It also
    wires sketches to views (TabManager.cpp:44-55, 234-236).
  - `ThumbnailProvider` (Thumbnails.h) is an image provider, a static renderer (`renderPage` / `renderDocument`,
    used by `Previews.cpp:383/390`, `HitPages.cpp:225`, `PageSketches.cpp:603`) and the process-wide
    **session registry** (`registerSession` / `acquireSession` / `idOf`). That registry is used by
    `PagesModel.cpp:46`, `AnnotationsModel.cpp:106/218/529`, `PageSketches.cpp:574`, `PresenterConsole.cpp:211`,
    `ReferenceMode.cpp:66` and `TabManager`.
  - `AnnotationsModel.h` holds a list model and the `AnnotationImageProvider`. `PageSketches.h` holds a service
    and the `SketchProvider`. `Previews.h` holds the `PreviewCache` service and the `PreviewProvider`.
  - `Stickers.h` is a file-store namespace (`stickers::`) plus `StickersModel`.
- **Services that QML never sees:** `Library`, `LibraryIndex`, `InkTextStore`, `PreviewCache`, `PageSketches`
  (singleton), `CompareMarks` (singleton), `WriteScheduler`, `SessionRecovery`, `ArxivQueue`, `SystemApps`,
  `SingleInstance`.
- QML gets every one of these as an untyped `QObject*` (`AppController.h:117-158, 346-358, 758, 859, 885, 898,
  1179-1204, 1402, 1421`). That gives up qmllint, property completion and AOT compilation for all of them.

### Module boundaries

- **`xqt-shell` compiles the app.** `qt/cmake/XqtApp.cmake:43-182` puts `AppController.cpp` and all 25
  `App*.cpp` files into the `xqt-shell` static library. `target_include_directories` adds `src/app`
  (XqtApp.cmake:183). So "shell" and "app" are one library, and 23 of the 38 test files labelled `shell` construct
  `AppController` (CanvasMemoryTest, ExternalChangesTest, LibraryTest, PastedPdfPagesTest, ToolboxModelTest, …).
  No file in `src/shell` includes an app header (good). The split can therefore be done in CMake alone: a new
  `xqt-app` target on top of `xqt-shell`.
- **No upward includes from session/canvas into shell** (checked). Shell depends on canvas in several places:
  - `CanvasView.h` in TabManager, ReferenceMode, PresenterConsole, VersionCompare and SettingsModel;
  - `MarkdownFile.h` in Library, Previews, HitPages, MdSnippets and DocumentLinks;
  - `CanvasMemory.h`, `PenGestures.h`, `ScreenCalibration.h` and `HoverPointer.h` in SettingsModel.

  The library index (a pure data service) depends on canvas only for `MarkdownFile`, so that dependency should
  move down into session or markdown.
- **Window controllers (role F) are not shell.** `ReferenceMode` (928 lines) re-implements the whole canvas-pill
  API of `AppController`: `pdfSelectionEnds`, `selectPdfTextAt`, `markPdfText`, `pasteAt`, `cutSelection`,
  `selectAllOnPage`, `insertImage`, `canPaste` (ReferenceMode.h:186-212, .cpp:528-654). It does this so that QML
  can treat both as one duck type, and `pdfTextModeChanged` is "(not emitted …)" (ReferenceMode.h:239). One
  `CanvasActions` object per `CanvasView`, used by both, would remove that duplication. This crosses into the app
  area.
- **Process-wide statics make "one library per process" a hidden assumption.** These are `PreviewCache` (all
  static, `Previews.cpp:52-62`), `DocumentPlaces` (namespace with static state), `PageSketches::instance()`,
  `CompareMarks::instance()`, the `ThumbnailProvider` registry, and `Library::setPlatformFolders` /
  `setDefaultCacheMode` (test hooks on statics).
- **Utility functions live in the wrong header.** `fileStamp`, `documentStamp`, `contentHash` and `ownFileOf`
  live in `Library.h:153-166` and `Library.cpp:296-341`. As a result `PageSketches.cpp` includes `Library.h` (the
  567-line index header) only to stamp a file.

### God files and long functions

- `Library.cpp` 2,473 lines (sections at :343 index, :508 packs, :896 old layout, :970 reading, :1403 update,
  :1659 moves, :1744 queries, :1858 search, :2379 titles). `Library.h` 567 lines, half of it the private `Entry`
  with 25 fields.
- `LibraryModel.cpp` 1,407 lines; `DocumentFiles.cpp` 1,309; `ToolboxModel.cpp` 1,131; `LibraryShare.cpp` 1,128;
  `Annotations.cpp` 994.
- Functions over ~80 lines (counted):
  - **SettingsModel constructor: 519 lines** (SettingsModel.cpp:135), made of 60 `add()` calls;
  - `DocumentFiles::relocate` 210 (DocumentFiles.cpp:349);
  - `LibraryIndex::search(FuzzyQuery)` 206 (Library.cpp:1987);
  - `Annotations::itemsOf` 161;
  - `LibraryUnzip::unpack` 159;
  - `LibraryTodosModel::refresh` 151;
  - `LibraryModel::rebuild` 140;
  - `LibraryIndex::run` 134;
  - `LibraryIndex::search(QString)` 128;
  - `ShortcutsModel` constructor 124;
  - `PageSketches::next` 119;
  - `LibraryIndex::entryOf` 118;
  - `LibraryMigration::copyAndVerify` 117;
  - `ToolboxModel::tidy` 114;
  - `LibraryModel::data` 114;
  - `PageSketches::plan` 111;
  - `LibraryIndex::read` 111.

### Proposed split of `Library.*` (keeps building at every step)

1. `FileStamps.{h,cpp}`: `fileStamp`, `documentStamp`, `contentHash`, `contentSample` (now anonymous in
   Library.cpp:391), `ownFileOf`. Shell files that only need stamps include this instead of Library.h.
2. `Library.{h,cpp}`: `PlatformFolders`, `Library` (paths, key, `library.json` settings). Read `library.json` once
   and cache it. Today `cacheMode()`, `hasCacheSetting()` and `showFilter()` each read and parse the file on every
   call (Library.cpp:214-262), and `LibraryModel::cacheInAppCache()` is a QML property getter.
3. `LibraryIndex.h` (public API) + `LibraryIndexEntry.h` (the private `Entry`, `Folder`).
4. `LibraryIndex.cpp`: lifetime, `update` / `run`, `movedHere`, `adopt`, `fillHashes`, `applyMoves`, `snapshot`.
5. `LibraryIndexPacks.cpp`: `notesOf` / `entryOf` / `load` / `writeChanged`, the CBOR schema in one place.
6. `LibraryIndexRead.cpp`: `read`, `fillPages`, `fillTitle`, `fillPdfTags`, `addTodos`.
7. `LibraryIndexSearch.cpp`: both `search` overloads, `wordsOf`, `prepareWords`, `matchTitles`, `findTitle`.
8. `LibraryIndexQueries.cpp`: bookmarks, todos, tagged, tagsOf, inkCandidates, filesNamed, linkPages,
   linkSources, filesWithPageText, pageCount, pdfKind, versionsOf, lockedOf.

---

## 2. Duplication

- **Image-provider plumbing, five times.**
  - Five `QQuickImageResponse` subclasses do the same thing (image + `cancelled` + queued `finished`):
    Thumbnails.cpp:45, Previews.cpp:394, HitPages.cpp:113, MdSnippets.cpp:130, AnnotationsModel.cpp:452.
  - Five hand-written LRU lists: Thumbnails.cpp:62-155, HitPages.cpp:44-90 (two lists), MdSnippets.cpp:41,
    AnnotationsModel.cpp:493, Previews.cpp folder trim :101-118.
  - The base64url `encode` / `decode` pair is copied in HitPages.cpp:105-111, MdSnippets.cpp:87-93 and
    Previews.cpp:487, 709.
  - Each provider computes a stamp hash of its own: MD5 in HitPages.cpp:140, MdSnippets.cpp:143 and
    Previews.cpp:491.
- **Thread pools: 12 private pools plus 11 uses of the global pool, with no common policy.**
  - Thumbnails: up to 4 threads, *normal* priority (Thumbnails.cpp:165).
  - PageSketches: 2 threads, lowest priority.
  - Previews: up to 3, plus 1 writer.
  - HitPages: 2 to 4.
  - MdSnippets: up to 2.
  - Annotations: 1.
  - VersionsModel: 1.
  - LibraryIndex: 1 reader plus 1 writer.
  - LibraryInkJob: 1.

  Together that can be about 20 drawing threads on the 8-thread device. AGENTS.md says "two preview workers" and
  "background work at idle priority", which only PageSketches follows.
- **Three copy-pasted list models.** `LibraryBookmarksModel`, `LibraryTagsModel` and `LibraryTodosModel` repeat
  the same pieces: the constructor wiring (LibraryBookmarks.cpp:15-38 ≈ LibraryTags.cpp:14-37 ≈
  LibraryTodos.cpp:26-50), `setActive` with a **1 s polling timer**, `changedMaybe` comparing a change counter, and
  the item filter "kind via index + favourites only". That filter appears four times: LibraryBookmarks.cpp:81-83,
  LibraryTags.cpp:101-103, LibraryTodos.cpp:249-251, and `LibraryModel::shown` at LibraryModel.cpp:536-539. The
  polling exists because `LibraryIndex` emits only `progress()`. Per-kind change signals (or one
  `changed(flags)`) would remove the timers and the four counters (`kindChanges`, `markChanges`,
  `todoChangeCount`, `tagChangeCount`).
- **Four background-job QObjects with identical shells.** `LibraryArchive`, `LibraryShare`, `LibraryUnzip` and
  `LibraryMove` all have `running` / `done` / `total` / `current` / `progressChanged` / `cancel`, an opaque
  `struct State` behind a `shared_ptr`, a start on `QThreadPool::globalInstance()`, and a `QPointer` self check
  (LibraryArchive.h:30-89, LibraryShare.h:49-150, LibraryUnzip.h:38-96, LibraryMigration.h:122-160). One
  `BackgroundJob` base class fits all four.
- **Atomic file writing, about 12 copies.** These places use `QSaveFile` + `flush()` + `commit()`, the Qt 6.7
  short-write workaround explained once at LibraryCache.cpp:68-72:
  - Library.cpp:232
  - DocumentPlaces.cpp:40
  - SessionRecovery.cpp:236
  - Stickers.cpp:53
  - TodoCalendar.cpp:167
  - Citations.cpp:358
  - LibraryMigration.cpp:195 and :510
  - ModelDownload.cpp:244 and :262

  Some copies have the flush and some do not. Three more write a hand-rolled `.part` file and rename it:
  RecentFiles.cpp:57 ("no QSaveFile: its sync … can stall the UI"), Previews.cpp:450 and PageSketches.cpp:93. One
  `writeFileAtomically(path, bytes, {sync})` and one `readJsonObject(path)` would replace them all.
- **The library key is computed twice.** `Library::key()` (Library.cpp:192, SHA-1 of the normalized root, 12 hex)
  and `CacheLocation`'s app-cache folder (LibraryCache.cpp:89-94, the same hash, recomputed) must agree. So must
  `LibraryMigration::relocateState`, which rebuilds `configs / key` by hand (LibraryMigration.cpp:579-585) instead
  of using `Library::configDir()`.
- **Two "kind" vocabularies.** The index entry stores `"xopp"|"pdf"|"md"|"image"|"text"` as a `QString`
  (Library.cpp:380 `entryKind`; there are 20+ comparisons such as `e->kind == QLatin1String("image")`). The
  library uses `DocumentItem::Kind` / `kindName()` = `"notes"|"pdf"|"md"|"image"|"text"|"other"`
  (DocumentFiles.h:62-66). Use the enum in `Entry` and serialize it in one place.
- **Two content fingerprints for the same question.** `contentSample` (SHA-1 of size + first and last 64 KB,
  Library.cpp:391) and `contentHash` (BLAKE2b of the whole file, Library.cpp:324) both answer "is this the same
  file?". The sample decides `movedHere` and the hash decides `adopt`. Once hashes are always filled
  (`fillHashes`), the sample can go.
- **RecentFiles and LibraryModel are parallel.** They have the same roles (Name, Path, Preview, HasPdf, HasXopp,
  Selected, LastPage, Kind, PdfKind, Versions) and the same selection invokables (RecentFiles.cpp:168-190 ≈
  LibraryModel.cpp:1173-1195; `GridSelection` already shares the core). A shared card-role helper would cover both.
- **Settings keys are repeated as string literals with their defaults.**
  - SettingsModel.cpp has 51 `getCustomElement(...)` calls and 28 `customSettingsChanged()`.
  - The same keys are read again in app/ with their defaults copied: `textContinuous` 4 times in
    AppTextFiles.cpp:103-176, `resumeAtLastPage` (AppController.cpp:3312), `toolVariants` (AppToolbox.cpp:57).

  A typed accessor (key + default once) would serve both.

## 3. Dead code (each verified with grep over src/, cli/, tools/ and QML)

- `annotations::setNoteSource` and the whole `NoteSource` indirection (Annotations.h:110-114,
  Annotations.cpp:45-52, 436-439, with a mutex). It is never called: the default `stickyNotesOf` is always used.
  Inline it.
- `ToolboxModel` has the Q_PROPERTY `rail` (ToolboxModel.h:60), which QML never reads. QML reads `entries`,
  "the name of before" (ToolboxModel.h:57; Toolbox.qml:111). Keep one name, preferably `rail`, and change
  Toolbox.qml.
- `ToolboxModel::recentAmong`, a Q_INVOKABLE (ToolboxModel.h:185), is unused in QML and C++.
- Signals that are emitted but never connected:
  - `TabManager::usedOrderChanged` (TabManager.cpp:375, 380);
  - `Citations::webOpened` (Citations.cpp:88);
  - `ReferenceMode::pdfTextModeChanged` (declared "not emitted").
- Q_PROPERTYs that no QML reads: `Citations.downloadedPath`, `HandwritingSettings.onBattery`,
  `LibraryMove.totalBytes`, `VersionCompare.currentChange` (each tested at most in C++).
- `LibraryInkJob::documentsDone()` (LibraryInkJob.h:61) is unused.
- `#include "shell/PageSketches.h"` in app/main.cpp:54 is unused.
- Functions that are public but only used in their own .cpp should be private or anonymous:
  `annotations::pageLink`, `DocumentLinks::linkToFile`, `LinkRewrite::rewriteDocument`, `ModelDownload::manifestOf`,
  `stickers::isPicture` / `isStickerFile` / `companionsOf` / `orderFile`, `ToolboxModel::variantsOf`,
  `HandwritingSettings::downloadOf` / `anyDownloading`, `RecentFiles::dropLibraries`.
- Test-only counters on production classes:
  - `LibraryIndex` has ten atomics and accessors (`documentsRead`, `pdfPagesRead`, `packsWritten`, `titlesRead`,
    `pdfKindsRead`, `keywordsRead`, `savedTakenOver`, `entriesAdopted`, `hashesComputed`, `oldLayoutsConverted`;
    Library.h:200-214, 257, 339-340, 562-563).
  - `PreviewCache` has three, and PageSketches and HitPages have their own counters.

  Several of these exist only for the backward-compatibility paths below (`titlesRead`, `pdfKindsRead`,
  `keywordsRead`, `oldLayoutsConverted`). Gather the rest into one `Stats` struct.

## 4. Backward compatibility to remove (xournal-qt's own data only)

| # | Where | What it keeps alive | Removing it means |
| --- | --- | --- | --- |
| 1 | Library.h:193-201, Library.cpp:896-968 (`hasOldLayout`, `convertOldLayout`, `convert`), LibraryModel.cpp:96-103 | the cache layout before the packs (`index/*.json` format 3, `previews/*.png`), converted on open | old caches are ignored, and a library opened with one is indexed from scratch once; the stale `index/`, `previews/` stay as foreign files (or are deleted by one `remove_all` in the clean-up) |
| 2 | Previews.h:69-71, Previews.cpp:654-677 (`convertOldFiles`) | old preview PNGs into packs | previews are drawn again |
| 3 | LibraryCache.h:93-103 / .cpp:218-310 (`isOldLayout`, `isOldFile`, the `old` flag of `forOurs`, `removeOldLayout`, the `"index"`, `"previews"`, `pages.json(.part)` cases of `removeOurs`) | recognising old-layout files as "ours" | `removeOurs` / `removeIfOnlyOurs` become a plain walk over `*.pack` |
| 4 | Library.h:141-143, Library.cpp:270-287 (`placesFile`) | copies `<root>/.xournal_library/pages.json` (or the app-cache one) into the config folder on first use; a getter with a side effect | `placesFile()` returns `configDir()/"pages.json"` |
| 5 | Library.h:419-460 and Library.cpp:490-499, 662, 670-722 | per-field "read once" flags for entries written before the field existed: `linksRead`, `todosRead`, `tagsRead`, `titleRead`, `pdfKind == Unknown` (`pdfKindMissing`), "bookmarks added 2026-09" | **bump `LibraryIndex::FORMAT` from 4 to 5** (one full re-index) and delete the flags, `upToDate`'s extra terms, `onlyMetaMissing`, and the read-only-metadata branch of `run()` (Library.cpp:1478-1503) with its counters `titleReads`/`kindReads`/`keywordReads`. `entryOf` and `upToDate` then shrink by about 50 lines |
| 6 | Library.cpp:371-377 (`kindOfPdf(…, doc)`), the comment at :366-369 | "a PDF with notes written by a build before it carried `name.md`" is detected by looking into the carried document (`TextDocument::isTextDocument`) | the kind comes from the marker alone; the `Document*` parameter goes |
| 7 | Library.cpp:1351-1367 (`movedHere`), Library.h:419-420 | entries "of older versions" without `sample` are matched by name only | always require the sample (or switch to `sha`, see §2) |
| 8 | Library.cpp:1594 `fillHashes` comment and library.md:314-316 | "entries of before get them once" | keep the function (new entries need it); drop the wording |
| 9 | PageSketches.h:95-96, PageSketches.cpp:51-52, 313-320 | `trimDisk` deletes page-preview folders "of the old layout (named without the document's prefix)" | delete `oldLayout()` and the branch |
| 10 | ToolboxModel.h:16-17, 57; ToolboxModel.cpp:20-21, 1069-1109 (`VERSION_1`, `"entries"` key, the upgrade block) | the toolbox JSON of 0.7.0 | `fromJson` accepts version 2 only; QML `entries` is renamed to `rail` |
| 11 | app/AppToolbox.cpp:50 `AppController::migratedToolbox`, app/AppController.cpp:205, the comment in SettingsModel.cpp:476-478 | classic tool bar → toolbox migration (0.8.0), including the `shape=` toolVariants case | a settings file without `toolbox` gets the first layout (cross-area: app) |
| 12 | ShortcutsModel.cpp:83-84 | shortcut id `readOnly` kept "so keys chosen then [0.7.0] stay" | rename it to `read` (QML and app references follow) |
| 13 | SettingsModel.h:87-92 documents `"chrome"`; Main.qml:252, 268 | `layout/<class>/chrome` of 0.7.0 (cross-area: QML) | drop the `chrome` reads/resets; SettingsModel needs no change beyond its doc comment |
| 14 | LibraryModel.cpp:119-134 (`adoptFolderCaches`) | "cache folders of its own (from before that default, or from a desktop)" moved into the app cache on Android | **keep**: the desktop→phone sync case is a feature. Only the "from before that default" reason goes (comment and library.md:258-260) |
| 15 | Library.cpp:742-751 and InkTextStore.cpp:180 (packs read from "where it may have been kept before": the other cache location) | read fallback for a mode switch that did not move the packs | `CacheFolders::move` and #14 cover switching; keep only the read-only-folder case (`dirOf` already maps it). Medium risk: decide with the author |
| 16 | Tests: LibraryTest.cpp:1562 `anOldCacheIsConvertedWithoutReadingAnythingAgain`; the old-layout half of `benchLibraryCache` (LibraryTest.cpp:2035-2058); LibraryCacheTest.cpp:185, 200; LibraryKindsTest.cpp:179 (the "written before kinds" part, :199-225) and bench :398; TagsTest.cpp:229 `entriesFromBeforeAreReadAgainOnce`; TodosTest.cpp:249 (same); CitationLibraryTest.cpp:129 `titlesAreKeptInTheIndexAndReadOnceForOldEntries`; ToolboxModelTest.cpp:245, 296-320 `theToolboxOf070IsUpgraded`, :635-660 `aSettingsFileOfTheClassicToolBarGetsTheToolboxWithTheToolsOfBefore` | tests of the above | delete them together with the code |
| 17 | Docs: library.md:68-72 ("written by a build before…", "Entries written before kinds were kept"), :314-316, :328-336 ("The layout before the packs"), :358 ("an older index format: everything is read once" can stay as one line); toolbox.md:13; ToolboxModel.h header | stale explanations | rewrite as the current state |

**Not to remove** (Xournal++ or platform compatibility):
- `name.pdf.xopp` pairing (library.md:31);
- `.xoj`;
- upstream's `settings.xml` keys;
- sync-app conflict names, including "older ownCloud" (SyncConflicts.cpp:48);
- the Qt 6.7 `QSaveFile` short-write workaround (LibraryCache.cpp:70-71).

`LibraryMigration` is **not** a data-format migration. It moves the libraries to Android shared storage, which is a
feature. It should be renamed `LibraryHomeMove` so that nobody deletes it as compatibility code.

## 5. Readability

- **"Preview" means three things.**
  - `PreviewCache` / `image://preview`: a document's first page at 360 px, on disk in `previews.pack`.
  - `PageSketches` "previews" (768/512/384 px per page, shown on the canvas before render, JPEG under
    `~/.cache/xournal-qt/pages`). Their memory is "a tenth of the memory for rendered pages", via
    `CanvasMemory::previewBudget`.
  - The setting `previewMemory` (`SettingsModel::previewMemory`, Settings → Documents). It governs thumbnails and
    sketches, but neither kind of preview.

  Add "sketch", "thumbnail", "hit page", "snippet" and "annotation picture", and there are six image caches. Each
  has its own limit: 3/4 of previewMemory, 1/4 of previewMemory, 1/10 of canvas memory, 48 MB (Previews.cpp:34),
  128 MB + 12 loaded `Document`s (HitPages.cpp:31-32), 8 parsed files (MdSnippets) and 24 MB
  (AnnotationsModel.h:131). Only the first three follow a setting. AGENTS.md's rule "a cache needs an owner and a
  limit" is met in the letter but not in spirit: there is no single owner of image memory. Proposed names:
  - `DocumentCovers` for PreviewCache;
  - `PageSketches::{sketch, stand-in}`;
  - `thumbnailMemory` for previewMemory (rename the key: there are 0 users);
  - a `ImageMemory` owner that hands out the budgets.
- **Comments.** The file headers (Library.h:1-31, PageSketches.h:1-31, LibraryShare.h:1-25) are excellent: they
  say what and why, with measurements. Inside code, the parenthetical asides bury the logic. For example
  `// (a document's handwriting follows it; a folder's goes with its cache)` and `// (only when empty)`, or
  Library.cpp:1405-1427 and :1478-1480, where every other line carries one. `Library.h` is 201 `///` lines out of
  567, and the private `Entry` documents compat history field by field. Rule of thumb: keep the header essays,
  cut asides that restate the next line, and move history ("added 2026-09 with …") out of code.
- **Magic numbers with no shared home:**
  - pool sizes (`min(4, ideal-1)`, `min(3, ideal/2)`, `max(2, min(4, ideal/2))`);
  - `WIDTH_STEP = 64` in Thumbnails, with HitPages repeating "rounded up to 64";
  - `shownDelay = 400`, `WRITE_DELAY_MS = 400`;
  - the 1000 ms polls;
  - `BIG_PACK = 4 MB`, `OWN_FILE_SIZE = 1 MB`, `TEXT_LIMIT = 1 MB`;
  - `MAX_RECENT = 32`;
  - TodoCalendar's "at most 20" (TodoCalendar.cpp:155 `i >= 19`).
- **Stringly-typed core data:** `Entry::kind` (§2), `ToolboxModel` items as `QVariantMap` throughout its 1,131
  lines, including validation, and `LibraryModel::sortKey` `"name"|"modified"`.
- `ShowFilter::operator==` (DocumentFiles.h:105-109) is written out by hand; `= default` would do, as `Todo` already
  does.

## 6. Correctness risks

1. **Shutdown is incomplete.** `AppController::shutdown` (AppController.cpp:663-666) stops the Preview, HitPage
   and MdSnippet workers "before the application takes its plugins away". It does not stop:
   - ThumbnailProvider's pool;
   - PageSketches' pool;
   - PageSketches' disk writes. These are started on `QThreadPool::globalInstance()` (PageSketches.cpp:543) and
     **save JPEG through Qt's image plugin**.
   - AnnotationImageProvider's pool (never destroyed, AnnotationsModel.cpp:28).

   A sketch JPEG write during teardown is exactly the case the comment warns about. Fix: one `ImageWorkers::shutdown()`
   that stops every pool.
2. **Session registry lifetime.** `ThumbnailProvider::registerSession` is called by `TabManager`, but also by
   `PagesModel::setSession` (PagesModel.cpp:46) and `AnnotationsModel::setSession` (:106). Only TabManager
   unregisters (TabManager.cpp:77, 328). This is safe today because every session given to those models is a tab.
   A future non-tab session (a preview session, a version shown alone) would leave a dangling `DocumentSession*` in
   the registry, and a queued thumbnail would `acquireSession` it. Fix: models look up (`idOf`) and only the owner
   registers.
3. **Preview requests cannot be cancelled.** `PreviewResponse` (Previews.cpp:394-400) has no `cancel()`, and the
   worker always runs `PreviewCache::preview`, which may load a whole document. Flinging through a library grid
   queues a full document load for every card that scrolled past. Thumbnails and HitPages already handle this.
4. **Thumbnail workers run at normal priority** with up to 4 threads (Thumbnails.cpp:165-167). They block in
   `RenderService::waitForVisiblePages(500ms)` instead of yielding. This contradicts "nothing is drawn in front of
   the page the reader is looking at".
5. **Getters that touch the disk on the UI thread:**
   - `Library::cacheMode()`, `hasCacheSetting()` and `showFilter()` parse `library.json` on each call
     (Library.cpp:214-262), and QML reads `cacheInAppCache` as a property.
   - `Library::placesFile()` copies files.
   - `DocumentPlaces::set` writes the whole JSON synchronously under a global mutex on every change
     (DocumentPlaces.cpp:79-104).
6. **Favourites and reading positions are stored in the cache folder.** For documents outside the library they go
   to `~/.cache/xournal-qt/documents/pages.json` (DocumentPlaces.cpp:67-69). The cache folder is the one users and
   cleaners delete, and library.md says "Reading positions are not cache".
7. `LibraryIndex::setCheckHook` replaces a `std::function` read on the worker, with no synchronisation. The comment
   says "set it while idle". It is a test hook, but it lives in production code.

## 7. Tests (qt/tests/shell: 38 files, 17,643 lines, ~425 tests, ~62 s serial per CTestCostData)

- **The label is misnamed.** 23 of 38 files construct `AppController`, so they are app integration tests. Some of
  the slowest are not shell at all: `PastedPdfPages.anUndoWhileTheMergedPdfIsWrittenIsSavedRight` takes
  **10.3 s**, apparently a hidden timeout, which should be investigated. `PageFilesTest`, `CanvasMemoryTest` and
  `TemplatesTest` also belong to other areas. After the CMake split, move them to an `app` label.
- **Fixed waits:**
  - PastedPdfPagesTest.cpp:684 and :1037 sleep 300 ms in a releaser thread;
  - LibraryTest.cpp:414, 435, 1941 and 1957 call `QThread::msleep(20)` "for a new modification time";
  - RecoveryTest.cpp:531 sleeps 1 ms in a loop.

  Set the mtime with `fs::last_write_time` instead of sleeping.
- **A copy-pasted wait helper.** `waitFor` / `until` / `waitUntil` is defined in 15 files (ArxivTest.cpp:29,
  FavouritesTest.cpp:42, LibraryCacheTest.cpp:40, LibraryFilesTest.cpp:72, LibraryFilterTest.cpp:47, …,
  ToolboxModelTest.cpp:43), with timeouts from 2 s to 30 s. Put it in `tests/shell/Wait.h`.
- **Coupled to implementation:**
  - work counters (`documentsRead() == 0`, `pdfPagesRead()`, `packsWritten()`, `titlesRead()`, …) assert *how*
    the index works. They are worthwhile for "nothing is read again" but freeze internals; keep them for the cache
    contract only.
  - `ToolboxModelTest` compares JSON strings (`stored.contains(R"("entries")")`).
- **LibraryTest.cpp is 2,060 lines with 47 tests.** Split it along the Library.cpp split: index, moves, packs,
  search, model.
- **Gaps:**
  - no test of image-provider cancellation for Previews (none exists in code either);
  - no test that shutdown stops every pool;
  - no direct tests of `DocumentChapters` or `PageClipboard` (0 references in tests/);
  - the `LibraryIndex` race between `moved()` and a running `run()` is covered only by `setCheckHook`;
  - nothing checks that `Library::key()` and `CacheLocation`'s app-cache key agree (§2).
- Benchmarks are properly behind environment variables (`XQT_BENCH_*`). `benchLibraryCache` still builds and
  measures the old layout (LibraryTest.cpp:2035-2058), which goes with block 1.

---

## Proposed refactoring blocks (most maintainability per effort first)

**Block 1: Remove the library cache's backward compatibility** (½ day)
- Goal: one cache format, no upgrade paths (§4 #1-#9, #16-#17).
- Files: Library.h/.cpp, LibraryModel.cpp, LibraryCache.h/.cpp, Previews.h/.cpp, PageSketches.h/.cpp,
  InkTextStore.cpp (only if #15 is accepted), docs/library.md, and the tests listed in §4 #16.
- Steps:
  1. Bump `LibraryIndex::FORMAT` to 5.
  2. Delete `convert*` / `hasOldLayout` / `convertOldFiles` / `isOldLayout` / `removeOldLayout`.
  3. Delete the `*Read` flags, `onlyMetaMissing` and `pdfKindMissing` together with their branch in `run()` and
     the three counters.
  4. Make `kindOfPdf` marker-only.
  5. Make `movedHere` require the sample.
  6. Make `placesFile()` plain.
  7. Delete PageSketches' `oldLayout`.
  8. Delete the tests and rewrite the doc sections.
- Risk: low. A cache rebuilds once, and there are no users.
- Verify: `ctest -L shell -R 'Library|Tags|Todos|Citation|Kinds|Thumbnails|LibraryCache'`, then open a real
  library twice: the second open reads nothing (`documentsRead()==0` test).
- Conflicts: Blocks 3, 6, 7.

**Block 2: Remove the toolbox and settings compatibility** (2–3 h, partly app/QML)
- Goal: §4 #10-#13.
- Files: ToolboxModel.h/.cpp, Toolbox.qml (`entries` → `rail`), AppToolbox.cpp, AppController.cpp:205,
  ShortcutsModel.cpp, Main.qml:252/268, SettingsModel.h/.cpp comments, ToolboxModelTest.cpp, docs/toolbox.md.
- Risk: low.
- Verify: `-L shell -R Toolbox`, `-L ui -R Toolbox`.
- Conflicts: the app/QML reviewers' blocks on Main.qml / AppController; Block 10.

**Block 3: Split `Library.*`** (1 day, mechanical)
- Goal: the eight files of §1. `FileStamps.h` first, then `Library` / `LibraryIndex` headers, then the .cpp
  split by section.
- Files: Library.h/.cpp → 8 files; includes in PageSketches, Previews, HitPages, MdSnippets, LibraryShare,
  LibraryUnzip, DocumentLinks, LinkRewrite, app/*; XqtApp.cmake.
- Steps: move code only, no logic changes; build after each new file.
- Risk: low (compile-only).
- Verify: build `xqt-shell-tests`, run `-L shell`.
- Conflicts: Blocks 1 (do it first), 6, 7.

**Block 4: Shared image-provider infrastructure and shutdown** (1 day)
- Goal: fix §6 #1-#4 and remove the duplication of §2.
- Steps:
  1. Add `shell/images/AsyncImage.h`: `AsyncImageResponse` (image, cancelled, `finish(QImage)`), `LruImageCache`
     (byte budget, key → image), `urlEncode` / `urlDecode`.
  2. Add `ImageWorkers` (named pools with priority and thread count from one table, plus `shutdownAll()`).
  3. Move the session registry out of `ThumbnailProvider` into `SessionRegistry` (only TabManager registers;
     models call `idOf`).
  4. Make `PreviewResponse` cancellable.
  5. Give the thumbnail pool low priority.
- Files: Thumbnails.*, PageSketches.cpp, Previews.cpp, HitPages.cpp, MdSnippets.cpp, AnnotationsModel.*,
  PagesModel.cpp, TabManager.cpp, PresenterConsole.cpp, ReferenceMode.cpp, AppController.cpp (shutdown).
- Risk: medium (threads, lifetime).
- Verify: `-L shell -R 'Thumbnails|Pages|Annotations|Library'`, `-L ui -R 'Annotations|Library'`, plus a new test
  that shutdown leaves no running worker.
- Conflicts: Blocks 5, 6 (TabManager), and the canvas reviewer (RenderService).

**Block 5: One vocabulary and one owner for image memory** (½ day after Block 4)
- Goal: rename `PreviewCache` → `DocumentCovers` (`image://cover`) and `PageSketches` "previews" → "stand-ins";
  rename the setting `previewMemory` → `thumbnailMemory` (no compat read); one `ImageMemory` hands out all budgets,
  including HitPages, Annotation and Covers, as shares of one setting.
- Files: Previews.* → DocumentCovers.*, PageSketches.*, Thumbnails.*, SettingsModel.*, CanvasMemory (canvas),
  the QML URLs, docs.
- Risk: low/medium (QML URL strings).
- Verify: `-L shell`, `-L ui -R 'Library|Recent|Pages'`.
- Conflicts: Block 4; the canvas reviewer (CanvasMemory::previewBudget).

**Block 6: `LibraryService` out of `LibraryModel`** (1 day)
- Goal: a non-QML `LibraryService` owns `Library`, `LibraryIndex`, the watcher, the `PreviewCache` /
  `DocumentPlaces` binding, cache administration and file operations, and emits per-kind index change signals.
  `LibraryModel` keeps rows, search, sort and selection (target under 700 lines). The bookmarks, tags and todos
  models take the service. AppController exposes the operations through the service (or keeps thin forwarding
  invokables on the model for QML).
- Files: LibraryModel.*, new LibraryService.*, LibraryBookmarks/Tags/Todos.*, Citations.*, RecentFiles (versions,
  kinds via the service), AppController (construction), tests constructing `LibraryModel`.
- Risk: medium (wide API surface).
- Verify: `-L shell -R Library`, `-L ui -R Library`.
- Conflicts: Blocks 1, 3, 7; the app reviewer.

**Block 7: One base for index-derived list models; no polling** (½ day, after 6)
- Goal: `LibraryIndexListModel` with `active`, refresh-on-signal and the shared visibility filter. Delete the three
  1 s timers and `changedMaybe`.
- Files: LibraryBookmarks.*, LibraryTags.*, LibraryTodos.*, Library (signals), LibraryModel::shown.
- Risk: low.
- Verify: `-R 'Favourites|Tags|Todos'` in shell and ui.
- Conflicts: Blocks 3, 6.

**Block 8: `BackgroundJob` base and the `LibraryHomeMove` rename** (½ day)
- Goal: the common running/progress/cancel shell for Archive, Share, Unzip and Move.
- Files: LibraryArchive.*, LibraryShare.*, LibraryUnzip.*, LibraryMigration.* → LibraryHomeMove.*, AppController
  (names), the QML that reads `running` / `done` / `total` (unchanged names).
- Risk: low.
- Verify: `-R 'LibraryArchive|LibraryShare|LibraryHome'`.
- Conflicts: none in shell; the app reviewer for AppController's includes.

**Block 9: File I/O helpers** (2–3 h)
- Goal: `shell/FileIO.h`: `writeFileAtomically(path, bytes, sync = true)`, `readJsonObject(path)`, used by the ~15
  places in §2. Cache `library.json` in `Library`. Move outside-library places from the cache to the config
  folder (§6 #6).
- Files: Library.cpp, DocumentPlaces.cpp, SessionRecovery.cpp, Stickers.cpp, TodoCalendar.cpp, Citations.cpp,
  LibraryMigration.cpp, ModelDownload.cpp, RecentFiles.cpp, LibraryCache.cpp, Previews.cpp, PageSketches.cpp.
- Risk: low.
- Verify: `-L shell`.
- Conflicts: Blocks 1 and 3 (Library.cpp lines; do it after them).

**Block 10: Table-driven `SettingsModel` and typed settings keys** (½ day)
- Goal: replace the 519-line constructor with `addBool("xournalQt", "resumeAtLastPage", false)`-style helpers plus
  a few custom entries. Move `previewMemory` / `canvasMemory` / `fuzzyTypos` / `handWhenOpening` / paper helpers
  into `XqtSettings.h` (key + default once), used by app/ too (`textContinuous`, `resumeAtLastPage`,
  `toolVariants`).
- Files: SettingsModel.*, new XqtSettings.*, AppTextFiles.cpp, AppController.cpp, AppToolbox.cpp,
  SettingsModelTest.cpp.
- Risk: low (keys are tested by `SettingsModelTest::keys`).
- Verify: `-R SettingsModel`, `-L ui -R Settings`.
- Conflicts: Block 2, Block 5 (the `previewMemory` rename), the app reviewer.

**Block 11: Dead code sweep** (1–2 h)
- Goal: everything in §3.
- Files: Annotations.*, ToolboxModel.* (unless Block 2 did `rail`), TabManager.*, Citations.*, ReferenceMode.h,
  HandwritingSettings.h, LibraryMigration.h, VersionCompare.h, LibraryInkJob.h, main.cpp, and the
  internal-only public functions.
- Risk: low.
- Verify: build and `-L shell`.
- Conflicts: Blocks 2 and 8 touch the same headers; do this last or fold it into them.

**Block 12: Test hygiene** (½ day)
- Goal: `tests/shell/Wait.h` replaces the 15 `waitFor` copies; mtimes set instead of slept; LibraryTest.cpp split
  by topic; the AppController-based files moved to an `app` label once Block 13 exists; investigate the 10 s
  PastedPdfPages test.
- Risk: low.
- Conflicts: Blocks 1 and 3 (LibraryTest.cpp).

**Block 13: Module layout and CMake** (1 day, after 3, 4 and 6)
- Goal:
  - subfolders `shell/library/` (A, C), `shell/library/jobs/` (B), `shell/images/` (D), `shell/document/` (E),
    `shell/workspace/` (F: TabManager, ReferenceMode, VersionCompare, PresenterConsole, SessionRecovery,
    PageClipboard), `shell/settings/` (G), `shell/platform/` (H);
  - move AppController and App*.cpp out of `xqt-shell` into a new `xqt-app` static target;
  - stop adding `src/app` to shell's include path;
  - move `MarkdownFile` (used by the index) below shell;
  - optionally expose typed pointers instead of `QObject*` to QML.
- Risk: medium (every include path; one large mechanical commit).
- Verify: full build, then the full suite once.
- Conflicts: everything. Do it last, alone.
