# Glossary

The words the code, the docs and the commit messages use, each with what it means here and where to read more. Where
a word names a class, the class is the definition. Files and folders on disk: [data-on-disk.md](data-on-disk.md).

## The fork and upstream

| Word | Meaning |
| --- | --- |
| **upstream** | Xournal++ ([xournalpp/xournalpp](https://github.com/xournalpp/xournalpp)): its code in `src/`, the branch `master`. Its files stay as they are ([ADR 0000](../decisions/0000-fork-policy.md)). |
| **the core** | the GTK-free part of upstream that the Qt build compiles (`xoj-util`, `xoj-core`, `xoj-tools`, from the list in `qt/cmake/XojSources.cmake`): the model, `.xopp` load and save, undo, the tools, the views that draw pages. |
| **shadow header** | a header in `qt/compat/include` with upstream's name (`control/Control.h`, `gui/PageView.h`, …) that stands in for a GTK class upstream's code expects; the fork implements it (`DocumentSession` is a `Control`, `CanvasPage` an `XojPageView`). |
| **GTK shim** | `qt/compat/gtkshim`: GDK's plain types, so upstream code compiles without GTK; any real GTK call fails to compile. |
| **seam** | an unavoidable edit in an upstream file: tiny, marked `xournal-qt:`, listed in [ADR 0002](../decisions/0002-upstream-seams.md). |
| **port** | an upstream class rewritten for Qt instead of reused (`PageRaster` from `RenderJob`, `CanvasPage` from `XojPageView`, `CanvasInput` from the input handlers); listed in ADR 0002. |
| **`xournalQt` part** | the fork's own settings, a custom element of upstream's `settings.xml` that upstream ignores. |

## The code

| Word | Meaning |
| --- | --- |
| **module** | a folder `qt/src/<module>/` with a README, usually one CMake target; dependencies point down the list in [AGENTS.md](../../../AGENTS.md). |
| **`DocumentSession`** | one open document (one tab) without a view: load, save, autosave, undo, page revisions ([session](../../src/session/README.md)). |
| **`AppContext`** | what all sessions of the process share: upstream's `Settings`, the tool in hand, page templates, render workers, the UI-thread dispatcher. |
| **`CanvasView`** | a document session shown in a view: visible pages, rendering, selection, text editing; one per tab, plus one for the reference or the presenter's audience ([canvas](../../src/canvas/README.md)). |
| **`CanvasPage`**, **`CanvasInput`** | one page as a view shows it (the tools' dispatch); the pen, touch and mouse input of a canvas. |
| **`DocumentCanvasItem`** | the Qt Quick item (`DocumentCanvas` in QML) that shows a `CanvasView` as tiles and takes all canvas input in C++ ([quick](../../src/quick/README.md)). |
| **tile** | a 256-pixel square of a page's raster, a texture in the scene graph; only dirty tiles are uploaded again ([ADR 0007](../decisions/0007-canvas-tiles.md)). |
| **`PageRaster`** | a page drawn by cairo at a zoom and pixel ratio, on a worker of `RenderService` ([render](../../src/render/README.md)). |
| **page revision** | a number per page (`DocumentSession::pageRevision`) that changes when the page's picture does; pictures and their files are named by it. A page keeps it when pages before it come or go. |
| **`AppServices`** | what all windows of the process share: settings view, toolbox, library, recent files, open documents, background jobs; one per process ([ADR 0003](../decisions/0003-app-services.md)). |
| **`AppController`**, **`app`** | one window's C++ side and its QML API (the context property `app`). |
| **`win`** | the window's QML object (`Main.qml`) and its state objects: `win.insets`, `win.modes`, `win.layout`, `win.actions`, `win.adaptive`. |
| **`WindowContext`** | what a window's feature objects get from their window: the services, its tabs, its current document. |
| **`CurrentDocument`** | the current tab's session and view, with their signals relayed once per tab change. |
| **`OpenDocuments`** | the windows of the process; "is this file open anywhere" (`find`). |
| **`CanvasActions`**, **`app.edit`**, **`app.keyTarget`** | what acts on one canvas (selection, clipboard, zoom, …) with its rules; `app.edit` is the notes' side, `app.reference.edit` the reference's, `app.keyTarget` the side with the keys. |
| **`SessionRegistry`** | the open documents the image providers may draw from, by the id in their URLs; only `TabManager` registers. |
| **`BackgroundJobs`**, **`ImageWorkers`** | the owners of the app's work off the UI thread and of the image providers' threads; shutdown stops them ([ADR 0005](../decisions/0005-memory-owners.md)). |
| **`CanvasMemory`**, **`ImageMemory`** | the owners of the memory for rendered pages (the setting `canvasMemory`) and of the limits of every image cache. |
| **`FileIo`**, **`AtomicFile`**, **`FileWriteLock`** | a file written whole or not at all (temporary name, fsync, rename); one writer of a file at a time in the process. |
| **`PdfObjectSink`** | where a PDF write's objects go: a file written in full or an incremental update; both kinds of save write through it. |
| **feature object** | a part of the window's API split off `AppController` with a `WindowContext` (`AudioControl`, `TimelineControl`; more to come). |

## Pictures

The words for the pictures the app keeps ([image-caches.md](image-caches.md) has their sizes, owners and code).

| Word | Meaning |
| --- | --- |
| **page preview** | what the page sidebar, page grid and tab overview show of a page: its sketch, then its thumbnail. |
| **sketch** | a small picture of every page of the open documents, drawn in advance. |
| **thumbnail** | the sharp picture of a page at the width a list asks for. |
| **stand-in** | a bigger picture of a page shown on the canvas until the page is rendered; kept on disk for the next opening. |
| **cover** | the picture of a document's title page on its card; in the folder's `previews.pack`. |
| **hit page**, **snippet**, **annotation picture** | a page with search hits marked; a passage of a Markdown file with hits; the handwriting of an annotation. |
| **preview** (older use) | the model role `preview` of the cards (the cover's URL) and the preview image inside a `.xopp`. |

## Documents and files

| Word | Meaning |
| --- | --- |
| **library** | a folder of documents shown on the home screen, with an index for search ([library.md](../features/library.md)); any folder can be one. |
| **libraries home** | where the libraries are made by default: `Documents/Xournal_Libraries` (on Android the phone's or the app's own folder). |
| **library key** | a short hash of a library's folder (`Library::key`): the name of its folders in the config and cache folders, and on the desktop of its single-instance socket and its session journal (`sessions/<key>.json`; the default library has none of these two by key); a library moved outside the app gets a new key. |
| **quick library** | the Downloads folder, offered in the library menu and opened as a library; its files are taken as short-lived. |
| **index**, **pack** | the library's search index; it is kept in packs (`notes.pack`, `pdf-text.pack`, `previews.pack`, `ink-text.pack`), one set per folder ([ADR 0004](../decisions/0004-library-index.md)). |
| **cache folder** | a folder's hidden `.xournal_library/` with its packs, or the same under the app's cache folder (a setting of each library). |
| **reading position** | the page a document was left at and its title page: not cache, kept in `pages.json` in the config folder. |
| **document mode** | how documents are kept, asked at the first start: Xournal++ files (`.xopp` next to the PDF) or PDF files (every document a PDF with notes); the setting `documentMode`. |
| **PDF with notes**, **hybrid PDF** | a PDF that any viewer shows with its ink (annotations drawn by cairo) and that carries the full `.xopp` embedded ([hybrid-pdf.md](../features/hybrid-pdf.md)). |
| **base page**, **clean copy** | a page's own PDF content without our ink; the copy of a PDF with notes without them, which the app shows under the editable strokes. |
| **incremental save** | a save of a PDF with notes that appends an update instead of writing the file anew. |
| **archive PDF** | PDF/A-3b with the ink flattened and the `.xopp` embedded. |
| **version history** | the incremental updates of a PDF with notes kept as versions inside the file. |
| **attached PDF** | a `.xopp`'s background PDF written next to it as `name.xopp.bg.pdf`. |
| **merged PDF** | the one background PDF of a `.xopp` into which pasted PDF pages are merged (`.name.pages.pdf`). |
| **text file**, **text document** | a `.md` or text file opened as a document; a Markdown document kept as `.md` or as a PDF that carries the Markdown ([md-pdf.md](../features/md-pdf.md)). |
| **Markdown box** | Markdown written on a page of a `.xopp` ([markdown-boxes.md](../features/markdown-boxes.md)). |
| **autosave**, **recovery** | the copy of a changed document written every few minutes (upstream's autosave setting); offered at the next start after a crash, with the open tabs from the session journal. |
| **session journal** | the open tabs of a window, for recovery and the next start: `session.json` in the config folder for the default library (and on Android, one window for all libraries), `sessions/<library key>.json` for a window of another library on the desktop. |
| **conflict copy** | a file a sync client left beside a document (`.sync-conflict-…`, "(conflicted copy)"); a badge on its card. |

## The window

| Word | Meaning |
| --- | --- |
| **home screen** | the library grid, recent files, bookmarks, to-dos and tags (`HomeView`). |
| **toolbox**, **rail**, **top bar** | the user's own pens and tools; the vertical bar they sit in; the bar above the page ([toolbox.md](../features/toolbox.md)). |
| **pill** | a small floating bar over the page: the selection's, a note's, the view's. |
| **reference** | a second document (or the same one) beside the notes ([reference-view.md](../features/reference-view.md)). |
| **Zen**, **read only**, **Read** | nothing but the page and a faint dot; no editing; reading with both ([zen.md](../features/zen.md)). |
| **size class**, **touch profile** | how big the window is and how it is used, from `AdaptiveLayout` (`win.adaptive`); the layout follows them ([adaptive-layout.md](../features/adaptive-layout.md)). |
| **sheet** | a menu or dialog that comes up from the bottom on a phone and is a menu or dialog on a desktop (`AdaptiveMenu`, `AdaptiveDialog`). |
| **safe area** | the part of the window not under a status bar, a cut-out or the gesture bar (`win.insets`). |
| **Esc/Back dispatcher** | the one ordered list of what Esc and Android's Back close first (`PageKeys.qml`). |

## Working on the fork

| Word | Meaning |
| --- | --- |
| **block** | one piece of work on a branch `qt/<block>` in its own worktree, done by an agent with [the brief](../agents/block-brief.md) ([workflow.md](../development/workflow.md)). |
| **integrator** | who plans the blocks, merges them into `master-qt` and releases. |
| **label** | a ctest label, one per test binary: `unit session canvas markdown audio hwr quick shell ui golden` ([testing](../testing/README.md)). |
| **FAST_DEV** | `-DXQT_FAST_DEV=ON`: a development build (QML not compiled ahead of time, less debug info, lld). |
| **device checklist** | the checks only a real device can make, walked before a release ([device-checklist.md](../testing/device-checklist.md)). |
| **refactoring** | a change of structure, not of behaviour: the tests that passed before pass after ([review/2026-10](../review/2026-10/README.md)). |
