# shell: library, tabs and the models behind the UI

Target `xqt-shell` (`qt/cmake/XqtApp.cmake`; it compiles `src/app`'s C++ too). Everything between the documents and
the QML that is not a canvas: several roles in one folder ([shell.md](../../docs/review/2026-10/shell.md) of the
review).

| Role | Classes |
| --- | --- |
| the library: storage, index, files | `Library` (the folder, its documents, file operations), `LibraryIndex` (+ `LibraryIndexEntry`, `LibraryIndexRead`, `LibraryIndexSearch`, `LibraryIndexQueries`, `LibraryIndexPacks.cpp`: the text and notes of every document), `LibraryCache` (where the cache lives, its packs), `FileStamps`, `DocumentFiles` (documents as files), `DocumentPlaces` (title page, reading place, star), `InkTextStore`, `LibraryInkJob`, `SyncConflicts`, `LinkRewrite`, `DocumentLinks` |
| library jobs | `LibraryShare`, `LibraryUnzip`, `LibraryArchive`, `LibraryMigration` (moving all libraries), `ZipFile`, `ContentFiles` (Android's `content://`) |
| library list models | `LibraryModel` (the grid and its file operations), `RecentFiles`, `LibraryBookmarks`, `LibraryTags`, `LibraryTodos`, `GridSelection` |
| pictures: providers, caches, workers | `Thumbnails` (`ThumbnailProvider`), `SessionRegistry` (the open documents they draw from; only `TabManager` registers), `PageSketches` (sketches and stand-ins), `DocumentCovers`, `HitPages`, `MdSnippets`, `AsyncImage` (the shared response, LRU and URL encoding), `ImageMemory` (the limits of all of them), `ImageWorkers` (their threads) |
| per-document list models | `PagesModel`, `PageFilterModel`, `OutlineModel`, `LayersModel`, `AnnotationsModel` (+ `Annotations`), `VersionsModel`, `DocumentChapters` |
| window controllers | `TabManager` (owns each tab's session and view), `CanvasActions` (what acts on one canvas: `app.edit`, `app.reference.edit`), `ReferenceMode`, `VersionCompare`, `PresenterConsole`, `SessionRecovery`, `PageClipboard` |
| settings-like models | `SettingsModel` (upstream's `settings.xml` keys and the fork's), `ShortcutsModel`, `ToolboxModel` (the rail and the top bar), `ColorPalettes`, `HandwritingSettings`, `ModelDownload`, `Stickers` |
| platform utilities | `Citations`, `NetFetch`, `Todos`, `TodoCalendar`, `SystemApps`, `SingleInstance`, `LocalUrl`, `PdfPrinting` |

**May depend on**: `xqt-canvas`, `xqt-hwr` and below; Qt Widgets, Network, PrintSupport, Quick (image providers),
DBus (Linux). From upstream: the model, `control/settings`, `pdf/base` (poppler for hit pages), `view/*` (drawing
thumbnails and covers), `control/xojfile`, undo. Not on `src/app` (the build does not enforce this yet: TODO.md,
"Later rounds").

**Rules**: every cache has an owner and a limit (`ImageMemory`); background work runs at idle priority on an owned
pool (`ImageWorkers`, the library's jobs), and shutdown stops it; pictures of pages are named by page revision
([image-caches.md](../../docs/architecture/image-caches.md)).

**Tests**: `qt/tests/shell` (label `shell`). **Docs**: [library](../../docs/features/library.md),
[toolbox](../../docs/features/toolbox.md), [reference view](../../docs/features/reference-view.md),
[presenter view](../../docs/features/presenter-view.md), [image caches](../../docs/architecture/image-caches.md).
