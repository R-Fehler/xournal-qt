# xournal-qt roadmap

## Status (2026-09-19)
- **M0 fork and spike: done.**
  - The fork has upstream history. The build root is `qt/CMakeLists.txt`.
  - On-device evaluation of `qt/spikes/inkpad`: both hosts meet the criteria, and the user found them indistinguishable. **Decision: Qt Quick** ([ADR-0001](adr/0001-ui-host.md), with the Wayland input facts the input port must handle).
- **M1 Qt-free core: done.**
  - `xoj-util` and `xoj-core` build without GTK; see [ADR-0002](adr/0002-upstream-seams.md).
  - `xournal-qt-cli` output is pixel identical to upstream `xournalpp` for PNG and PDF export on all 49 fixtures (golden tests), and round trips keep the document structure.
  - All 116 upstream unit tests pass.
  - Upstream undo and the layer controller compile unmodified against the shadow `Control` interface.
- **Refinement of R4: shadow headers instead of new interface names.**
  - `qt/compat/include/control/Control.h` and friends are abstract interfaces with upstream's names and signatures. Reused upstream files therefore need **no edits at all**.
  - The per-tab session in M2 implements `Control`.
- **M2 headless session and render service: done.**
  - `xoj-render`: `PageRaster` (port of `RenderJob` and the `XojPageView` buffer) and `RenderService` (worker pool, zoom block). Output is pixel identical to upstream rendering, also at fractional DPR; the PDF is rendered outside the document lock.
  - `xqt-session`: `AppContext` (shared settings in `~/.config/xournal-qt`, `ToolHandler`, page templates, render workers) and `DocumentSession` (per tab, implements `Control`: load/new/annotate PDF, save/save-as/autosave ported from upstream, insert page).
- **M3+M4 merged (user request) — first app window: done, confirmed on the device.**
  - `xournal-qt`: Qt Quick window with a minimal touch tool bar (upstream Lucide icons): new/open/save, undo/redo, pen/highlighter/eraser/hand, palette, sizes, add page, zoom.
  - `xoj-tools`: upstream `StrokeHandler`, `StrokeStabilizer`, `EraseHandler` and the stroke overlays, compiled unmodified.
  - `xqt-canvas`: `CanvasPage` (port of `XojPageView`), `CanvasInput` (port of `PenInputHandler` & co. plus touch gestures and palm rejection), `CanvasView`, `DocumentLayout`, `ViewController`.
  - `DocumentCanvasItem`: tiles composed from the page buffer and overlays, with GPU zoom.
  - Replay tests (`xqt-canvas-tests`): synthetic pen input through the whole pipeline. Quick tests (`xqt-quick-tests`): input routing with dialogs, scroll bars.
  - After the first device test: dialogs usable (the canvas only takes events where it is the topmost item), touchpad momentum, scroll bars, resizable native file dialogs (`QApplication` for the KDE platform theme), "Save as" suggests the `.xopp` next to an annotated PDF.
  - Checklist: `docs/testing/device-checklist.md`.
- **M5a tabs and single instance: done, confirmed on the device.**
  - `xqt-shell`: `TabManager` (one `DocumentSession` + `CanvasView` per tab; what the tabs keep of their rendered pages: `CanvasMemory`), `SingleInstance` (`QLocalServer`; files opened from the file manager go to the running window as new tabs), `AppController`.
  - Tab strip with close buttons, reordering, unsaved-changes prompts per tab and on quit.
- **M5b page sidebar and page operations: done (awaiting on-device test).**
  - Sidebar with page thumbnails (`ThumbnailProvider`: async, rendered like upstream's `PreviewJob`; `PagesModel` follows the document events and refreshes thumbnails 400 ms after edits, undo and redo).
  - Tap a thumbnail to go to the page; long-press or ⋮ for insert before/after, duplicate, move up/down, delete. All undoable through upstream's undo actions (ports of `Control::deletePage/duplicatePage/movePageTowards*`).
  - Page counter and zoom moved to a floating pill over the canvas; the tool bar scrolls sideways when the window is narrow.
  - Fixed on the way: canvas drawn at a stale position by Qt Quick's software backend (containers are transform nodes now); canvas input blocked by an `ApplicationWindow` background item (hit test follows z order); `DocumentListener::unregisterListener` is idempotent (seam, see ADR-0002).
- **M5c settings screen: done (awaiting on-device test).**
  - Settings sheet (gear button, Ctrl+,) over upstream's `Settings`, same settings.xml keys: pressure (sensitivity, minimum, multiplier, guessing), side/barrel button tool, eraser mode, palm rejection timeout, pinch zoom on/off, stroke stabilizer (all upstream parameters), autosave, default file name, new page template (background, paper size, orientation, color, copy from current page). Changes apply live and are saved once when the sheet closes (upstream's settings transaction).
  - `SettingsModel` (key/value access for QML), unit tests; `xqt-ui-tests` load the real `Main.qml` off-screen.
- **Tab switching and overview: done (awaiting on-device test).** Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PgDown/PgUp) cycle tabs; the grid button (Ctrl+Shift+E) shows all open documents as cards with their current page: tap to switch, × to close, keyboard navigation.
- **M5d crash recovery and session restore: done (awaiting on-device test).**
  - Journal of the open tabs (`session.json` in the config folder), written when tabs open/close/move and shortly after page changes; marked clean on a normal exit.
  - Fatal signals (crash, SIGTERM at logout, SIGINT) write every modified tab to `<cache>/autosaves/<pid>-<serial>.emergency.xopp` (upstream's CrashHandler, for all tabs).
  - Next start after a crash: "Recover unsaved changes?" lists the tabs whose emergency/autosave file is newer than the document; recovered tabs keep their original file path and are marked unsaved (upstream's EmergencySaveRestore). After a normal exit: the last tabs, current tab and pages are reopened (setting "Reopen the documents of the last session").
  - Fix: unsaved tabs autosaved to the same file (upstream's `<pid>.xopp`); each tab has its own now.
- **Search: done (awaiting on-device test).**
  - Search bar (Ctrl+F, search button): PDF text and text elements, per page as upstream's `SearchControl`; the whole document is searched incrementally on the UI thread. Hits are highlighted over the pages (current hit orange); Enter / Shift+Enter or the arrows step through them, starting at the current page.
  - Tab overview: "Search all documents" marks the documents with hits (count) and dims the others; opening one shows its search at the first hit from its current page.
- **Page grid: done (awaiting on-device test).** Grid button (Ctrl+Alt+G): all pages of the document over the canvas with small gaps; fling through it, tap a page to go there; pinch / Ctrl+wheel / −+ change the number of columns (1–12). Search hits are marked on the previews (and in the sidebar), with a hit count per page; pages with hits get an orange frame, and "N pages with hits" (sidebar and grid) shows only those pages, to skim long PDFs.
- **Column layouts: done (awaiting on-device test).** Layout button in the zoom pill: one page per row, two pages side by side, book (cover alone), or N columns (1–8). Upstream's settings (`viewColumns`, `showPairedPages`, `numPairsOffset`) and its layout rules (horizontal layout, fixed columns; column/row size from the largest page); pairs meet in the middle. Fit width covers the whole row. Not ported: vertical (column-first) and right-to-left/bottom-to-top layouts.
- **Page operations: done (awaiting on-device test).**
  - Select pages in the sidebar and the page grid: Ctrl/Shift+click, "Select" mode for touch, Ctrl+A. Copy / cut / paste (Ctrl+C/X/V, also into another tab: PDF pages become image backgrounds there), duplicate (Ctrl+D), delete (Delete), move by press-and-hold drag and drop (several pages at once), page menu (right click, ⋮), action bar in the grid.
  - Page operations go onto the one undo stack of the document, as in upstream (they had a stack of their own first, which left Ctrl+N and the like without an undo on the canvas). Undoing or redoing one shows a note ("Undone: Insert page"), as the page may be out of view.
  - Touchpad momentum in the page grid, sidebar and tab overview.
- **M6a shapes: done (awaiting on-device test).** Shapes button: freehand, shape recognizer, line, rectangle, ellipse, arrow, double arrow, coordinate system — upstream's handlers (`RulerHandler`, `RectangleHandler`, ... and `ShapeRecognizer`) compiled unmodified; Shift/Ctrl modifiers as upstream. `ZoomControl` shadow for reused code.
- **M6b selection: done (awaiting on-device test).** Rectangle and lasso select tools (also tap to select one element, Shift to add); move, resize, rotate, delete (× handle, Delete), copy / cut / paste (Ctrl+C/X/V, upstream's `application/xournal` clipboard data, also between tabs and windows), select all on the page (Ctrl+A); action bar. Upstream's `Selector`, `EditSelection`, `EditSelectionContents` and `SelectorView` compiled unmodified against new shadows (`gui/PageView.h`, `gui/Layout.h`, more of `XournalView`); the selection is drawn by the canvas item (texture from `EditSelection::paint`).
- **M6c text tool: done (awaiting on-device test).** Tap to write, tap a text to edit it; keys (arrows, Home/End, Ctrl+word moves, selection with Shift or dragging, Ctrl+A/C/X/V, Enter, Backspace/Delete), input methods (dead keys, on-screen keyboard); font family and size (text button). Qt port of upstream's TextEditor model logic (it is built on GtkTextBuffer): edits a copy while the original is hidden, then InsertUndoAction / TextBoxUndoAction / DeleteUndoAction (empty text); Pango layout like the renderer.
- **M6d image insert: done (awaiting on-device test).** Image button (file dialog) or pasting an image from the clipboard: inserted in the middle of the visible part of the page, fitted into it, as a selection (move/resize right away); undoable (upstream's Image element and InsertUndoAction).
- **PDF export, links, back/forward: done (awaiting on-device test).**
  - "Export as PDF…" (⋮ menu, Ctrl+E) with upstream's exporter; the suggestion never overwrites the background PDF.
  - PDF links: a finger tap (or a tap with the hand / select tools) on a link offers "Open" (web) or "Go to page N"; the pen keeps writing on links. (Upstream only follows links with the PDF text tool.)
  - Back / forward: jumps by links, the page grid and the sidebar remember the place; a ← → pill (Alt+Left/Right) goes back and forth.
- **PDF text marking: done (awaiting on-device test).** "Mark PDF text" tool with a sticky mode: highlight (highlighter color), underline, strike through (pen color) are applied right when the drag over the text ends (upstream needs a second tap in its floating toolbox); "select" mode shows a bar with the marks and "copy text"; "select by area" for columns/tables. Upstream's `PdfElemSelection` / `PdfElementSelectionView` compiled (one seam: primary selection), marker strokes as in `PdfFloatingToolbox::createStrokes`, one undo step.
- **The scope before MuPDF is complete.** Next: MuPDF on the `mupdf` branch (not started; the user starts it), then PDF-native annotations.
- **Library and home screen: done (awaiting on-device test).** See [library.md](library.md).
  - One library per window (a folder, like a workspace): `xournal-qt` opens `<Documents>/Xournal_Libraries/Default`, `xournal-qt <folder>` opens that folder. Single instance and session journal per library; "Open a folder as library…" / "New library…" start another window.
  - Home screen (the first tab, always there; shown when no document is open): **Library** grid (folders or all documents, breadcrumbs, sort by name/date, first-page previews) and **Recent** grid (opened documents that still exist).
  - A `.xopp` and the PDF with the same name are one document; renaming, moving, copying and importing keep them together and rewrite the `.xopp`'s PDF reference (upstream LoadHandler/SaveHandler). Open tabs and the recent list follow renames and moves.
  - New document (name, background, paper size, orientation; saved in the current folder), import (file dialog, or dropping files/folders from the file manager: copies), new folder, rename, Copy to… / Move to… (folder dialog), drag onto a folder or a breadcrumb, trash.
  - Several items can be selected (Ctrl/Shift+click, the circle on a card, "Select" in the menu, Ctrl+A) and opened, copied, moved or trashed together; dragging a selected card moves all of them.
  - Library search: folder names, and the text of all documents (PDF text and text elements) from a background index in `.xournal_library/index`, plus names; hits with count and a text snippet; opening one shows the document search at the first hit.
  - Import of whole folder trees (Import → "Import a folder with its subfolders…", or dropping folders): the folder structure is kept, with its documents.
  - Search index in two parts (PDF text per PDF page, tied to the PDF used; per page its PDF page and text elements): an annotation change re-reads only the .xopp, renames/moves in the app carry the entries along, and changes of attached or external PDFs are noticed.
  - Extended search: the pages with hits of each result (marked in the page images, drawn from kept documents and page images), tap one to open the document there with the search; grid zoom (− / +, Ctrl+wheel, pinch).
  - Fix on the way: a tool tip (any non-modal popup) no longer blocks pen and mouse input on the canvas.
- **Batch of 2026-09-19 (done, awaiting on-device test):**
  - Tool bar colors: orange added to the defaults; "+" adds a color (color dialog), press and hold / right click removes one or restores the defaults.
  - PDF text highlights: three preset colors (yellow, green, pink) in the "Mark PDF text" menu and the select-mode bar.
  - "Insert pages…": background, paper size, orientation, count, before/after (page menu, ⋮ menu, press and hold on add page); one page-undo step.
  - Tool bar at the top, left or right (⋮ → Tool bar position).
  - Full screen (F11): only a small current-tool square (drag it; tap: all tools and colors) and the page / zoom pill.
  - Table of contents: Pages | Contents in the sidebar; contents overview (Ctrl+Alt+O) with level-styled headings and the pages of each section side by side. The page grid button moved into the page / zoom pill.
  - Program icon and a .deb (CPack as upstream; qt/packaging/README.md): desktop file for PDF/.xopp/.xoj, mime types, Dolphin "Open as Xournal Qt library" for folders.
  - Markdown boxes: write Markdown on a page (or beside it) and see it formatted while typing; it flows onto the next
    pages, its headings are chapters, the search finds it where it is drawn, and it is stored as ordinary Xournal++
    text in a layer "Markdown". See [markdown-boxes.md](markdown-boxes.md).
  - Text mode (Ctrl+Alt+E): type the page's text like in a word processor (headings, lists, per-paragraph bold / italic / size / color, markdown shortcuts), stored as Xournal++ text elements in a layer "Text" and read back; one undo step. See [text-mode.md](text-mode.md).
  - Downloads folder as a quick library (import warning); Copy to / Move to another library.
- **Search with short texts** (user report: a one-letter search in a large document could crash): texts shorter than 4 characters are searched on Enter or a tap on the search icon only (document, tab overview, library). A one-letter search in a 300-page PDF (207k hits) peaked at ~1 GB: the sidebar and page grid made one QML item per hit. Thumbnails now show at most 50 marks per page, spread over it (the count badge stays exact): ~260 MB.
- **Tab strip with many tabs:** the tabs use the whole width before they scroll, and the + button stays at the right end (the list and the spacer used to share the room).
- **UI polish, `qt/ui-polish` (2026-09-24, awaiting on-device test):**
  - Tab strip: ‹ › buttons beside the overview button switch documents, like Ctrl+PgUp/PgDn.
  - A full-screen button in the tool bar.
  - A tool bar docked left or right: its hide arrow and "show tool bar" strip follow the dock side.
  - A long press offers paste:
    - With the pen, highlighter or hand tool: the pen held within 6 px for 500 ms. The dot it began is taken
      back, with no undo step.
    - On PDF text, with a finger or the pen: the word is still selected, and the text pill gains "Paste here".
    - To watch on the device: a pause before writing may trigger it.
- **Rendering what is on screen, `qt/render-visible` (2026-09-24, awaiting on-device test):**
  - **Grey pages after zooming.** `PageNode::clearTiles()` removed tiles that were never in the scene graph. In a
    release build of Qt this empties the page node's child list, taking the page, its tiles and its preview with it.
    Now only tiles that are children are removed. This is probably also why pages stayed blurry; that link is
    inferred, not reproduced.
  - **Pages in view first.**
    - Background render workers start nothing new while a visible page is queued or rendering.
    - Previews and thumbnails wait for the pages in view (thumbnails for at most 500 ms).
    - A pinch that ends lifts the 300 ms zoom block at once.
    - The most visible page is queued first.
    - `XQT_PERF` logs `sharp N after avg/worst ms`.
    - Measured on pgfmanual, time until sharp after the last zoom step: pinch in 538 → 264 ms; Ctrl+wheel in
      616 → 524 ms; Ctrl+wheel out 669 → 530 ms.
    - Not done: splitting a page into tiles across threads. Poppler draws one page per PDF instance at a time.
  - **Fast tab close.** Queued thumbnails no longer count as busy, and a view cancels all its queued renders at once
    (`RenderService::cancel` for a set). It waits only for the renders already running.
    - Closing a 1,300-page PDF: 5.4–6.4 s → 0.2–0.4 s.
    - Remaining: the one render each worker is running, and freeing the PDF instances.
- **Markdown fixes, `qt/markdown-fixes` (2026-09-24, awaiting on-device test):**
  - A code block at the end of the text:
    - Enter at the end of an unclosed fence adds a line of code instead of a paragraph break.
    - `topLevelSpans` finds the fence above blank lines.
    - A finished block below the cursor is drawn as finished code.
  - Search boxes in Markdown:
    - A hit across a line break gets one box per line.
    - While writing on the page, hits follow the block shown as source, and are searched again when writing ends.
    - New `md::textRects` and `md::sourceRects` turn a text range into drawn rectangles, for the `.md` snippet cards.
  - The reported "boxes where the plain text would be" did not reproduce; it needs steps if it is still seen.
- **Window state, `qt/window-state` (2026-09-24, awaiting on-device test):**
  - The app opens maximized.
  - Leaving full screen goes back to the state before: maximized, or the size the user chose. The window follows
    its state all the time, and asks for maximized once more if the compositor gives back the unmaximized size.
- **Per-folder library cache, `qt/library-index` (2026-09-24, awaiting on-device test).** See
  [library.md](library.md).
  - Each folder has its own `.xournal_library/` with `notes.pack`, `pdf-text.pack` and `previews.pack` (CBOR +
    zlib, written whole and atomically, debounced), keyed by file name. An entry of 1 MB or more gets its own file.
  - Opening a library merges the packs of all its folders. A subfolder opened as a library reads only its own
    folders.
  - Reading positions are in the config (`~/.config/xournal-qt/libraries/<key>/pages.json`).
  - The old layout is converted in the background; its files are deleted only after the conversion succeeds.
  - Settings → Storage: the cache size, the choice "keep the cache in the app's cache folder", and "Remove all
    cache folders of this library", which then closes the app.
  - Measured on a generated library of 303 documents in 30 folders: 17.6 MB in 606 files → 11.9 MB in 94 files;
    the PDF text is 3.5× smaller; cold open 54–75 → 37–39 ms; editing one `.xopp` writes only its folder's
    `notes.pack` (1 KB).
- **Pasted PDF pages stay PDF pages, `qt/pdf-pages` (2026-09-24, awaiting on-device test).** qpdf-based
  `MergedPdf` and `PdfPageKeeper`.
  - A page pasted from another PDF is a real PDF page, with searchable and selectable text. It goes into one merged
    PDF per document: a hidden `.name.pages.pdf` when the document annotates a PDF (the original is never
    modified), or `name.pdf` when it had none.
  - Until the document is saved, the merged PDF lives in the cache. On save it is moved next to the `.xopp`, and
    pages no longer used are dropped. A renumbering save goes in steps, so a crash at any point leaves a matching
    pair.
  - A note on paste says where the pages are kept. The library carries the sidecar along on rename, move, copy
    and trash.
  - Measured: a 56-page lecture with 3 pasted pages gives a 453 KiB sidecar against the 393 KiB original.
    Pasting into a 117 MB scan takes about 0.2 s per paste, on the UI thread.
  - Resolves the backlog item "Searchable text in pages pasted from another PDF" (option 1).
- **Fast search in open documents, `qt/document-search` (2026-09-24, awaiting on-device test).**
  - `DocumentTextIndex` per open document: the PDF text of each page plus the drawn text of text elements and
    Markdown.
    - It is seeded from the library index when the entry is current. Otherwise it is read once in the background,
      from the current page outwards, yielding to the pages in view, with its own poppler instance.
    - It is kept current by edits, undo and redo, page moves, and a change of background PDF (pasted pages).
  - `DocumentSearch` works in two steps: a string scan counts the hits of all pages, and hit places are computed
    only for pages that need them (from poppler's text layout, 0 mismatches on pgfmanual).
  - One matcher, `TextMatch`, for the document and the library: case, whitespace, ligatures, and words hyphenated
    at line ends.
  - Saving hands the entry to the library index, so it does not re-read the `.xopp`.
  - The search field no longer drops typed characters: it was bound to the query and reset by stale results.
  - On pgfmanual (1,321 pages):
    - before: all counts took 3.3–3.8 s, again for every key typed;
    - after, in a library: 8–11 ms;
    - after, outside a library: counts fill in once in the background over about 6 s;
    - per key: 3–7 ms; longest UI-thread pass 45–57 → 0.3 ms.
  - Integration fix at merge: the index also rebuilds when another background PDF is loaded, so pasted pages are
    found at once.
- **Hybrid PDF, `qt/hybrid-pdf` (2026-09-24, awaiting on-device test and a round trip in other viewers).** See
  [hybrid-pdf.md](hybrid-pdf.md).
  - Writer (qpdf): the base pages; one annotation per visible layer per page (`/Ink`, or `/Stamp` for layers
    without strokes) whose `/AP` is the layer drawn by cairo; `/NM (xopp:…)`; an embedded `document.xopp`; a
    catalog marker with the format version and a hash per annotation. The write is atomic.
  - Reader: the embedded document opens with a clean copy of the file, without our annotations, as its
    background, cached. Other apps' annotations stay. Changes to ours are reported, with "keep the Xournal data" or
    "import them as plain annotations".
  - UI: "Save as hybrid PDF…" (`name.notes.pdf`); the setting "Save notes into the PDF itself" (off, keeps
    `name.original.pdf`); "Export as .xopp for Xournal++"; optional automatic `.xopp` export; one library card.
    `.xopp` stays the default.
  - Measured on 1,321 pages with notes on 53: save 5.6 s (on the UI thread; background saving is a follow-up);
    first open 5.5 s (clean copy), cached 0.5 s; 10.3 MB, the same as our PDF export.
- **PDF pages rendered once for the screen scale, `qt/pdf-hidpi` (2026-09-24).** `PdfBackgroundView` asked the PDF
  cache for zoom × device scale, and the cache's similar surface applied the device scale again: on a 2x screen,
  4x the pixels (about 100 MB per cached page at fit width), then scaled down. It was found by the MuPDF
  experiment. A one-line tagged seam in upstream's file (ADR-0002); upstream has the same bug.
- **Markdown files and images in the library, `qt/library-files` (2026-09-24, awaiting on-device test).** See
  [library.md](library.md).
  - `.md` files and images (`.png`, `.jpg`, `.webp`, `.heic` when Qt reads it) are library cards with badges and
    previews. Images are turned upright by their EXIF tag.
  - `.md` is indexed as passages (headings, paragraphs, list items, table rows, code) with heading paths, links
    and wikilinks. It opens read-only as A4 pages, and nothing is written on it.
  - Extended search: a `.md` result shows snippet cards (the rendered passage, its heading path, the hits marked);
    tapping one opens the file at that passage.
  - An image opens as a new document with it as the background; saved as `photo.xopp`, and the pair is one card.
  - The `kind` role (`notes`/`pdf`/`md`/`image`, plus `hybrid`) is ready for the file-type filter.
  - Fix: attached background images (`name.xopp.bg_N.png`) now travel with their `.xopp` on rename, move, copy and
    trash.
- **`XQT_LOG_WINDOW=1` (2026-09-24):** logs window state, size and position changes, touches and presses, and what
  the app asks of the window, to chase the window that halves after a touch.
- **The page grid stays where it is scrolled while searching, `qt/grid-search-scroll` (2026-09-24).** It moved to the
  current hit on every search update. Hit places now arrive as pages scroll into view, so it jumped back while
  scrolling. It now moves only when the current hit changes.
- **Library "Show" filter and other files, `qt/library-filter` (2026-09-24, awaiting on-device test).**
  - A "Show" button next to the sort button, remembered per library in `library.json`: notes, PDFs (with "only
    PDFs with notes"), Markdown, images, text and code, all other files. It applies to the grid, folder counts
    and search.
  - Text and code files: a first-lines preview, opened read-only (a highlighted code block over pages), indexed up
    to 1 MB while shown.
  - Other files: a type icon, "Open with the system app", and "Show in file manager" (D-Bus `ShowItems` on Linux),
    behind a fakeable `SystemApps`. Rename, move, copy and trash work on them.
  - Recent grid: libraries opened outside `Xournal_Libraries` appear as folder cards with a library mark. A folder
    card's menu has "Open as library (new window)".
- **No crash at exit from the text reader, `qt/text-worker-exit` (2026-09-24).** The never-destroyed pool that
  reads PDF text could still release a poppler document while the program's statics were torn down: about 1 in 20
  exits under load crashed, and the crash handler took that for a real crash. It is now drained from a
  QCoreApplication post routine; 0 of 240 runs crash.
- **Preview writes, `qt/preview-writes` (2026-09-24).** A changed document's first page is drawn again and compared
  with the stored preview. If it looks the same, only a tiny `preview-stamps.pack` records the new version, and
  `previews.pack` stays untouched until it is written anyway. Editing page 3: 1.1 KiB written instead of about
- **Saving in the background, `qt/background-save` (2026-09-24, awaiting on-device test).**
  - Saving: the pages are copied under the read lock on the UI thread (a few ms), then the files are written on a
    worker (`DocumentSave.cpp`). The merged PDF keeps its 5-step crash-safe order.
    - An edit during a save stays unsaved and the document stays modified; a failed save keeps it modified.
    - A second Ctrl+S during a save is queued; close and quit wait with the window usable.
    - Tabs and the title show "saving…".
  - Pasting from another PDF: the merge runs in the background, and the page is drawn at once from the pasted PDF in
    memory. Export and print wait for the merge.
  - Fix: a pasted page showed late on the canvas, because every page was re-rendered at visible priority. Now only
    pages new in the PDF are drawn again (`loadPdfKeepingPictures`): 40–110 ms instead of up to 890 ms.
  - Seam: `Document::readPdfKeepingOutline` (ADR-0002).
  - Measured on pgfmanual: a hybrid save blocks the window for at most 0.8 ms (before: 5.3 s); a paste 1–22 ms
    (before: 9.7 s).
- **Fuzzy search, `qt/fuzzy-search` (2026-09-24, awaiting on-device test).** See [library.md](library.md), "Fuzzy
  search".
  - A "Fuzzy" toggle in the library search and the tab overview; off by default, app-wide.
  - fzf's extended syntax plus parentheses: a space is AND, `|` is OR (tighter), `!` is NOT, and `'exact`,
    `'word'`, `^prefix`, `suffix$`, `^equal$` narrow a term.
  - Names and folder paths use a port of fzf's FuzzyMatchV2 (MIT; fzf's own score table passes), with the matched
    letters marked. Text uses substring terms through `TextMatch`.
  - A document matches when the expression holds over its name and text; a page is listed when it holds on that
    page.
  - Measured on 3,000 `.md` files with 12 MB of text: 20–200 ms per search under load.
- **Reference mode, `qt/reference-view` (2026-09-24, awaiting on-device test).**
  - A second document beside the notes in the same tab ("Open as reference" in the tab strip, tab overview, library
    and Recent), with a divider, a frame around the notes, and swappable sides and roles.
  - Its pill: go to page, copy, fit width, swap, close, the page grid, and an edit switch (off by default, per tab;
    its own undo, saved like any tab).
  - Input: a stroke stays on the canvas it started on. The reference is read-only by default: tools scroll there,
    and selections can be copied but not moved. Keys follow the side last tapped.
  - Memory: documents on screen keep their rendered pages before background tabs.
  - Left: the reference is not restored after a restart.
- **Presenting and sideways scrolling, `qt/present` (2026-09-24, awaiting on-device test).**
  - Page-number jump: type digits, then Enter.
  - 16:9 slides (960×540 pt) in New document and Insert pages.
  - Fit width fits the current page (or its row), not the widest page: `ViewController::fitWidthZoom(page)`.
  - Sideways scrolling: pages fit the window height, snapping on or off, a Rows setting, ‹ › in the pill; wheel,
    touchpad and swipe page through.
  - Presenting (F5): full screen on black, PowerPoint keys, one page per swipe, a fading "12 / 40". Escape returns
    to full-screen editing.
  - A full-screen tab bar of dots (`PageIndicator`): a tap opens the overview, a swipe switches tabs.
  - With a reference open, page keys act on the side that has the keys, and presenting shows the notes alone.
- **Fuzzy search in text, `qt/fuzzy-text` (2026-09-24, awaiting on-device test).**
  - With Fuzzy on, a plain term of 3+ letters matches text word by word (`WordMatch`): a word that contains it, a
    word that starts with its first letter and has all its letters in order with at most ⌊n/2⌋ others between
    them, or a typo (optimal string alignment) for terms of 5+ letters.
  - Per-page vocabularies make it fast: 1.7–7.8 ms per key on pgfmanual instead of 54–75 ms.
  - Hit-page pictures mark whole words and `^`/`$`/`'word'` like the canvas does.
  - Settings → Search: the fuzzy switch and the typo tolerance (off / 1 letter for 5+ / 2 letters for 8+). A help
    sheet opens on a long press or right click of the Fuzzy button.
- **Reference mode follow-up (2026-09-24):** the notes' scroll bars, text knobs, text pill, selection bar and
  context pill are shared components, instantiated for both canvases; copy only while reading. Markdown and Ctrl+S
  work in the reference when editing is on. Fit width never needs a horizontal scroll bar.
- **Hybrid PDF flow and Share, `qt/hybrid-flow` (2026-09-24, awaiting on-device test).**
  - Save as offers "Xournal notes (.xopp)" or "PDF with notes, editable (.pdf)". "Export as plain PDF…" stays.
  - Saving a `.xopp` as a PDF with notes asks once about the `.xopp`: trash it (the default), keep it updated
    (recorded in the PDF as `/XoppExport`), or keep it. "Don't ask again" can be undone in Settings → Documents.
  - Share (⋮, tab menu, cards) offers:
    - PDF with notes, shown in the file manager;
    - copy to the clipboard (uri-list, the PDF bytes up to 50 MB, and the path);
    - for Xournal++: a one-time export of `name.xopp` + `name.xopp.bg.pdf` into a chosen folder, checked with
      upstream's loader.
  - Trashing goes through `SystemApps::moveToTrash`.
  - A `.xopp` kept next to its hybrid PDF opens the PDF; one edited more than 60 s later is listed separately.
- **The library index keeps entries across moves, `qt/index-rename-race` (2026-09-24).** An update that looked at a
  file just being moved stored an empty entry, so the document was read again. Now a file gone while it is read
  keeps its old entry, and a document seen under a new name takes over an entry with the same size, time and
  content sample (a SHA-1 of the first and last 64 KB). The flaky test is fixed, and the CI retry is gone.
- **`.md` and text editor, `qt/md-editor` (2026-09-24, awaiting on-device test).** See [md-editor.md](md-editor.md).
  - `.md` is edited with the live-rendering Markdown editor. Only the text is saved, atomically; untouched bytes stay
    the same (CRLF, BOM, no final newline). Autosave and recovery keep the text in the cache. External changes are
    reloaded, or asked about.
  - `.txt` is a plain, monospace notepad. Other text files are editable after an "Edit anyway" warning.
  - "Open externally" through `SystemApps`. Pages or one continuous page (a per-block layout cache makes it about 3×
    faster).
  - "Edit as notes" makes a `.xopp` copy (two library cards). The New menu has New Markdown file and New text file.
  - Share for text documents shares the file itself.
- **Android, `qt/android-apk` (2026-09-24).** See [android.md](android.md) and [android-roadmap.md](android-roadmap.md).
  - vcpkg (manifest, pinned baseline, static arm64-android triplet, 55 packages).
  - `XojDeps.cmake` finds dependencies the same way on the desktop and in a toolchain, with an `android-arm64-debug`
    preset.
  - `qt/scripts/android-build.sh` runs with 4 jobs, `nice` and a memory cap.
  - A debug APK (94 MB, minSdk 28, target 36) that started and worked in an emulator. `AndroidSetup.cpp` sets up the
    folders, resources and a fontconfig pointed at `/system/fonts`.
  - Qt 6.11.2 (aqt), NDK r27c, JDK 17.
- **An empty path no longer throws out of the recent list (2026-09-24).**
- **Reference pop out, `qt/reference-popout` (2026-09-24).** "Show as a tab" in the reference pill closes the split,
  puts the reference's tab right after the notes, and switches to it; Ctrl+Tab then goes between the two.
- **Links between documents, `qt/links` (2026-09-24, awaiting on-device test).** See [links.md](links.md).
  - `DocumentLink`: relative paths with `#page=`, `pdfpage=`, `chapter=`, a `text=` fingerprint, and `.md`
    headings; resolved with fallbacks.
  - A tap offers a new tab, the reference or here, with a remembered choice; back and forward work across
    documents.
  - "Copy link" on pages, chapters, cards, search hits and ⋮. Pasting makes `[title](link)` in Markdown, or a
    🔗 marker on the page.
  - The index records links: "Linked from…", links rewritten after in-app renames and moves, and a search for
    targets that were moved outside the app.
  - Hybrid PDFs get `/Link` annotations (`GoToR` or `/URI`).
- **Windows build, `qt/windows-build` (2026-09-24).** See [windows.md](windows.md) and
  [windows-roadmap.md](windows-roadmap.md).
  - GitHub Actions `xqt-windows.yml` (on a push to `qt/windows-build`, or by hand): MSYS2 UCRT64, Qt 6, a portable
    `xournal-qt-windows-x64` folder deployed with windeployqt and the MinGW DLLs, and a smoke test (CLI exports and
    the app off-screen, with gdb backtraces of failures).
  - Windows fixes:
    - single instance, memory size, crash handling (an unhandled-exception filter);
    - printing through poppler and Qt;
    - a UTF-8 C locale and XDG folders;
    - an 8 MB thread stack;
    - gnu++20 / `NOMINMAX`;
    - `Util::toUri` for poppler;
    - `PageFilterModel` signals connected one by one.
  - Text is drawn with Pango's fontconfig backend (`WindowsFonts.cpp`: a generated fonts.conf with the Windows font
    folders, the cache warmed in the background). Pango's win32 backend dies drawing text into images; it stays as
    an informational check for an upstream report.
- **Archive export, `qt/archive-export` (2026-09-24, awaiting on-device test).** See [hybrid-pdf.md](hybrid-pdf.md),
  "Archive PDF".
  - PDF/A-3b with the ink flattened as appended, marked content streams, which the app removes again when opening,
    so the file stays fully editable. The `.xopp` is embedded as an associated file (`/AFRelationship /Source`), with
    the sRGB OutputIntent (ArgyllCMS v2 profile) and XMP.
  - An honest report when a file is not PDF/A (fonts not embedded, CMYK without a profile…); repairs that keep the
    look.
  - "Export for the archive…" (⋮, Share) and "Export library as archive…" (folder structure kept, links rewritten
    to the archived copies, README, progress and cancel).
  - veraPDF checks samples in CI; hybrid PDFs no longer inherit a PDF/A claim.
- **Tests wait for conditions, `qt/test-waits` (2026-09-24).**
  - The offenders pass 40 of 40 under load (before: up to 28 of 40 failed).
  - The worst was a tool tip covering a button. The UI tests now run with hover off, and waits for conditions
    default to 5 s.
  - Three app bugs fixed on the way: pages rendered after the memory plan stayed above the limit; a page whose
    preview arrived by another route was never stored as a sketch; two sketch writers shared one `.part` file.
- **Setsquare and compass on the GPU, `qt/geometry-gpu` (2026-09-24, awaiting on-device test).**
  `GeometryToolPicture` draws the tool once in its own coordinates, with upstream's views unchanged; the angle
  display is a small separate picture. A `GeometryNode` in the scene graph moves, turns and sizes it with a transform,
  and the page is no longer repainted. A new size, zoom or scale is drawn again 150 ms after it settles; textures are
  capped at 4096 px, with a sharp part drawn on top for large tools.
  - Measured at DPR 2: moving a 15 cm setsquare 12.1 → 0.07 ms per frame, turning 23.3 → 0.48 ms, sizing
    43.4 → 0.07 ms.
  - Behaviour change: a line being drawn shows under the tool, not over it.
  318 KiB.

- **Incremental saves of PDFs with notes, `qt/pdf-incremental` (2026-09-24).** Ctrl+S on a hybrid or archive PDF
  appends only the changed layer annotations and drawings, the embedded `.xopp` and a new cross-reference section
  (`IncrementalPdf`, objects serialised through qpdf). Written to a copy next to the file, flushed and renamed over
  it, so a crash leaves the previous revision intact. Archive PDFs stay PDF/A-3b (veraPDF in CI).
  - Compaction (a full write): Save as, before sharing, above 25% growth, a quarter of the pages new or removed, and
    whenever the file has no safe base (changed outside, other app's ink, encrypted).
  - pgfmanual: Ctrl+S 6.3 → 0.2 s (hybrid), 24.6 → 0.2–0.4 s (archive); reopen 5.5 → 0.44 s.
  - Left: no UI message when a save falls back to a full write; MuPDF and pdf.js not yet checked.

- **PDF-only mode, `qt/pdf-only` (2026-09-24, awaiting on-device test).** The first start asks once (existing
  installs too) whether documents are "PDF files (like Drawboard PDF, GoodNotes, Xodo)", recommended and
  preselected, or "Xournal++ files"; Settings → Documents shows the same cards. Nothing changes until a choice is
  stored. `XQT_DOCUMENT_MODE=xopp|pdf` sets it for tests.
  - PDF mode: new documents are `name.pdf`; Ctrl+S on an opened plain PDF writes the notes into it (first save in
    full, then incremental), with a one-time notice; pasted pages leave no sidecars; image backgrounds are embedded;
    autosaves go to the app cache, and recovery looks there. `.xopp` files stay `.xopp`.
  - The original of a PDF that first gets notes is kept for 30 days in `~/.cache/xournal-qt/originals/` (a hard
    link where possible), with no UI yet.
  - Left: the rename over a PDF held open by another program on Windows; a UI to restore the kept original.

- **Android basics, `qt/android-basics` (2026-09-24, awaiting on-device test).** "Open with" and the share sheet
  for PDF, `.xopp`/`.xoj`, `.md`/`.txt` and images: received files are copied in the background into the library's
  "Opened" folder and opened as a tab (`ContentFiles`, `XournalActivity`, `singleTask`). Import files and folders,
  Open… and Insert image through Android's pickers. Draw with the finger (all platforms): a tool bar toggle and
  Settings → Touch; on by default on Android devices without a stylus. Markdown code colours on Android
  (KSyntaxHighlighting 6.30.0 built by `android-build.sh ksyntax`). Phone UI: tab strip below the status bar,
  narrow home screen, a bundled DejaVu Sans subset for symbols, the new-document dialog above the keyboard.
  - Left (android-roadmap.md): Save as / export to a picked place (`content://`), a compact tool bar, safe areas
    other than the top, nothing saved when Android sends the app to the background.
  - The desktop tool bar no longer fits in 1600 px (it scrolls, as in narrow windows).

- **Libraries in synced folders, `qt/android-libraries` (2026-09-25, awaiting on-device test).**
  - Android: "All files access" asked with an explanation when a folder is to become a library; picked tree URIs
    of the shared storage (primary, SD cards, Downloads) map to real paths; the one window switches libraries.
    The library cache defaults to the app cache (existing `.xournal_library` folders move there once).
  - All platforms: open `.xopp`, hybrid and annotated PDFs follow changes other programs make to their files
    (reload in place when unchanged, else Reload / Keep mine); the app's own saves never count.
  - Sync conflict copies (Syncthing, Dropbox, Nextcloud/ownCloud, Seafile, OneDrive, generic) next to their
    document are a "Conflict" badge on its card: Compare side by side, keep one (the other to the trash; on
    Android deleted after a question).
  - Autosave when the app goes to the background (Android: also when inactive); on Android autosaves live in the
    app cache.
  - Checked on the emulator only. Left: see TODO.md, `qt/android-libraries`.

- **Libraries in the phone's own folders, `qt/android-storage` (2026-09-25, awaiting on-device test).** On Android,
  Qt's Documents and Download locations are app-private (deleted on uninstall). With "All files access" the libraries
  live in the phone's `Documents/Xournal_Libraries` and the Downloads quick library is the real `Download`.
  - Existing libraries move there in the background: staged copies verified by size and SHA-1, then renamed, then
    the originals removed only if unchanged since the copy; a manifest finishes an interrupted clean-up. Recent
    files, reading positions, the journal, open tabs and the library's config/cache follow. Name clashes become
    "Name (2)".
  - Declining keeps the app-private folder and a hint on the home screen. `android:hasFragileUserData` offers to keep
    the app's data on uninstall. An in-app one-level folder chooser opens folders Android's picker refuses
    (Download). "Show in file manager" is hidden on Android.
  - Checked on the emulator (move, uninstall/reinstall); not yet on the Fold 7.

- **CI green on Debian 13, qpdf 12 built with the app, `qt/ci-green` (2026-09-25).** qpdf 12.4.1 is downloaded
  (pinned by SHA-256) and linked statically on the Linux desktop (`qt/cmake/XqtQpdf.cmake`; `XQT_SYSTEM_QPDF` for
  distros, Android and Windows, which have 12.x); qpdf older than 12 is no longer supported. Incremental saves work
  on qpdf 12 (a reserved object number was a null on 12, so every save fell back to a full write).
  - pgfmanual on qpdf 12: full write 1.4 s (hybrid) / 3.3 s (archive), Ctrl+S appended 0.12–0.18 s, reopen 0.34 s.
    +2:13 min for a clean build (seconds with ccache), +1.9 MB binary, +0.9 MB `.deb`, no `libqpdf` dependency.
  - Also fixed: names sort naturally and case-insensitively in any locale; `QSaveFile` saves on a full disk fail
    instead of cutting the file (Qt 6.7); tests inject failed writes in ways that fail for root too; two UI tests
    wait for menus to close.

- **Math in Markdown, `qt/md-math` (2026-09-25, awaiting on-device test).** `$…$` and `$$…$$` are rendered with
  MicroTeX (openmath branch, vendored, MIT; Latin Modern Math compiled in), recorded as cairo paths: vector on
  screen, in PDF export, the hybrid PDF and print. Inline formulas sit on the baseline; `$$` blocks are centred and
  scaled to fit; errors show the source in red with the message on hover; search finds the TeX and marks the
  formula; the block with the cursor shows its source (a `$$` block also a preview). Fuzzed: 600,000 formulas.
  - +2.4 MB binary (1.36 MB font), 28 s build; a page of 50 formulas lays out in 4.4 ms, draws in 8.7 ms.
  - Left: the error message on touch screens; editing per block, not per formula; `\color` outside arrays.

- **Screen calibration, `qt/calibration` (2026-09-26, awaiting on-device test).** Settings → Display: a ruler in cm
  and inch, set with a slider, −/+ or a drag (mouse or finger), stored per screen (maker, model, serial or
  connector, size) as physical pixels per inch, so a new scaling needs no new calibration. 100 % is real size;
  Ctrl+1 zooms to it (Ctrl+0 still fits the width). A new 100 % changes only the shown percentage and the limits.
- **Writing button writes Markdown on the page (2026-09-26).** In its Markdown mode the button (and Ctrl+Alt+M)
  writes the page's text on the page, formatted while typing; the source beside the page is in its menu.
- **Handwriting recognition research, `qt/hwr-research` (2026-09-26).** `qt/docs/research/handwriting-recognition.md`;
  decisions for the author in TODO.md.

- **Annotations panel and "Export as Markdown", `qt/annotations-md` (2026-09-26, stage 1).** A side bar panel lists a
  document's highlights (with their PDF text), PDF comments, text and Markdown boxes, margin handwriting (picture
  crops of stroke groups) and links by page; a tap jumps there, a filter by kind, only changed pages are read
  again. The export writes `name.annotations.md` (headings from the outline, links back to the pages); handwriting
  as page links until Markdown draws images. Stage 2 (keep a marked section updated) is designed, not built.
- **Sticky notes, `qt/sticky-notes` (2026-09-26).** Opaque, coloured, resizable notes that the pen, text and eraser
  write on (clipped to the note); move and resize carry the content along (resize clips, never scales); cover mode
  for self-testing (the pen does nothing on it; a tap peeks, on screen only); per page hide/show; drawn in exports
  and the hybrid PDF as they look. Upstream Xournal++ opens the files (checked with its binary).

- **Math delimiters from chat apps, `qt/md-tex-delims` (2026-09-26).** `\( … \)` and `\[ … \]` (as ChatGPT writes
  them) are formulas: a linear pre-pass rewrites them to `$`/`$$` for md4c outside code, and every run is mapped back
  to the original source, so the file keeps what was typed and the cursor, search and pages stay right. Pasting
  into Markdown converts them to `$…$` (portable to Obsidian and GitHub), one undo step. `$ $` and `$$ $$` stay text;
  `\hbar` is ħ. `\[` counts only at a line's start and `\]` at its end (`see \[1\]` stays text); a pair next to a
  letter (`\(n\)th`) stays as written, as md4c ignores such a `$`.

- **Markdown formatting bar and table editor, `qt/md-toolbar` (2026-09-26).** A row of tools under the tool bar while
  Markdown is written (always in a `.md`; also in the source panel): paragraph/headings, bold, italic, strike, code,
  link, `$…$`, lists (bullet, numbered, task), quote, code block with a language, table, `$$` block, rule, image
  placeholder, page break; the buttons show the state at the cursor; every tool is one undo step
  (`qt/src/markdown/MdFormat`). A table editor in a popup (a grid: Tab moves, rows and columns added and removed,
  alignment, the current row and column shown) writes a padded GFM table. `<div style="page-break-after: always">`
  ends the page.
- **The same document beside itself, `qt/self-reference` (2026-09-26).** One session, two views: the second view has
  its own page, zoom, selection and history, is read-only unless its pill's pen is on, and shares the render memory
  limit. Opened from the tab menu, the tab overview (at the page in view), the page menu, sidebar and grid (at that
  page); links into the same document offer "In the reference". The page-subset view was built and removed again
  on the author's clarification ("not some page range limiting the canvas scroll"), pending confirmation.

- **Emoji, `qt/emoji` (2026-09-26).** Noto Color Emoji 2.051 is bundled (as "Xournal Qt Emoji", CBDT, 10.7 MB, OFL;
  cairo 1.16 on Ubuntu 22.04 draws no COLRv1) and registered for this process only; PDFs get sharp colour images
  at the font's resolution. `:smile:` is shown as 😄 but kept in the file (not in code); `:` + two letters opens
  suggestions, a 🙂 picker with search; the cursor and deletion go by grapheme cluster (ZWJ, flags, skin tones).
  Fixed on the way: a Markdown box's leftover cairo point drew a later text box over its last line; flags on
  Android.

- **Space for notes beside slides, `qt/note-space` (2026-09-26).** A page grows by amounts on its left, top, right
  and bottom (stored on `XojPage`, `notespace="l t r b"` in the `.xopp`, a small upstream seam); the PDF is drawn at
  the offset from the same cached picture. Text selection, links, search and index boxes, annotations and
  thumbnails add the offset. Hybrid and archive PDFs get larger MediaBox/CropBox around the untouched page
  (original boxes kept, so saving twice never grows twice; incremental saves rewrite only changed pages); plain
  export uses the cairo exporter. Dialog "Space for notes" (page menu: this or selected pages; More: all or all
  PDF pages), in % or cm, presets, preview, or a blank page after each. 300 pages in ~5 ms, one undo step.
  Upstream shows the larger page with the PDF at the top left (right/bottom space looks the same).

- **Text documents as PDF, `qt/md-pdf` (2026-09-26).** A text document is a notes document whose page 1 starts the
  page's Markdown text; saved as a PDF with notes it also carries a plain `name.md` (every full and incremental
  save; `/AFRelationship /Alternative` in archive PDFs). "New text documents: PDF document / Markdown file"
  (Settings → Documents, following DocumentMode until set); the library's "New text document…". Typing goes into the
  text (at the end of what the page in view holds) while the pen keeps drawing ink; the formatting bar is there.
  "Export as Markdown" and "Open as PDF document" (the `.md` stays as it is). Existing `.md` files are never
  converted. User guide: `qt/docs/user/markdown-from-pdf.md`. Left: images (`qt/md-images`), a "text" badge.

- **Citations, `qt/citations` (2026-09-26).** Look up selected text (PDF text pill, context pill): Google Scholar and
  a configurable translator, the exact web address confirmed first (Open / Copy / Cancel, "don't ask again").
  "Find this paper": a title guessed from a bibliography entry (IEEE, APA, ACM, LNCS, DIN, arXiv) is matched in the
  library against PDF titles and the largest text of page 1 (new `title`/`heading` fields per PDF in the index
  packs, no format bump), typo-tolerant; hits open beside, in a tab, or as a link. arXiv: ids recognised, title
  search through the export API, download named by the title into the library, "Open as reference". Networking is
  opt-in (ask/on/off), one arXiv request per 3 s.

- **Sticky notes on the clipboard, `qt/sticky-clipboard` (2026-09-26).** Copy and cut a selected note (Ctrl+C/X, the
  note pill, the long-press pill) whole, as `application/x-xournal-qt-sticky-note` plus a PNG; paste (Ctrl+V and
  the pills) puts it on the current page of the view pasted in (other tabs, windows, the second view), at the same
  place when it fits, offset over an identical note, one undo step. A note dragged onto another page moves there
  (one undo step). Upstream Xournal++ cannot paste a note.

- **All handwriting in the annotations, with its context, `qt/annotations-context` (2026-09-26).** Handwriting over
  PDF text was dropped as "marks" (whole notes on slides vanished) and dots under 4 pt were dropped; now every piece
  is listed, captioned with the words it crosses, underlines or circles (`on “…”`, also in the export), and dots
  join the nearest piece. The panel's pictures show the page under the ink (PDF, image or paper colour, dimmed to
  ~65 %; rulings skipped), rendered per crop on a worker (newest first, rows scrolled away skipped, yielding to the
  canvas), 24 MB LRU; one request per row.

- **Kinds of PDFs in the library, `qt/library-kinds` (2026-09-26).** The index records each PDF's kind (plain, with
  notes, text document, archive, archive text) from the one marker read it already does (`HybridPdf::markerOf`;
  `/Files` listing a `.md` = text document), tied to the PDF's stamp; old entries get it lazily (0.1–1.3 ms each,
  worker). Cards show "PDF", "PDF ✎", "PDF Aa", "PDF/A ✎"/"PDF/A Aa" (grid, list, Recent). "Only PDFs with notes"
  uses the index (300 × 200-page PDFs: 361 ms on the UI thread → 7 ms); new "Only PDF text documents".

- **Images in Markdown, `qt/md-images` (2026-09-26).** `![alt](path)` is drawn in the text like a formula (block
  images fit the column, never above their 96 dpi size, at most 1.4 × the column high; inline images line-high;
  missing files show alt text and path in red; the source shows while editing, the picture below it); decoded
  pictures cached (64 MB). Ctrl+V, drop and the bar's Image… save `name.assets/image-….png` (one undo step). A `.md`
  and its `name.assets/` are one document in the library (hidden folder; rename rewrites the links, move, copy,
  trash, share take it along). PDF text documents carry pictures as `name.assets/…` attachments (unpacked to the
  app cache while editing; incremental saves add new ones only). `.xopp` files carry them as extra
  `<preview xqt-file=…>` elements that Xournal++ ignores. Web pictures load only on request (opt-in, address shown).
  ⋮ → Remove unused images. Left: CLI exports of `.xopp` pictures, `![[image]]`, drop at the cursor.
- **Markdown boxes always written on the page (2026-09-26).** The text tool's "Write Markdown beside the page" switch
  is gone (a stored value is ignored); the source beside the page is the writing button's menu or Ctrl+Alt+M.

## Backlog (decide later)
- **Searchable text in pages pasted from another PDF** (user, 2026-09-19). Today a PDF page pasted into a document with another (or no) background PDF becomes an image background: it looks the same, but its text is no longer searchable or selectable. Cause: the .xopp model (and file format) has *one* background PDF per document; pages refer to page numbers in it. Options, to decide with the MuPDF work (MuPDF can write PDFs; poppler cannot):
  1. On paste, write a merged background PDF (the document's PDF + the pasted pages, e.g. `name.pages.pdf` next to the .xopp) and renumber the pages. Text stays searchable; the file stays upstream-compatible (still one PDF).
  2. The PDF-native model (M9/M10 direction): work on the PDF itself, ink as PDF annotations; copying pages = copying PDF pages (with their text) between PDFs.
  Not an option: several PDFs per .xopp (format change, incompatible with Xournal++).
- **Selected elements in autosave / emergency save.** While a selection exists its elements are held by the selection (upstream's design), so an autosave or crash save taken at that moment does not contain them (same in upstream). Option: serialize the active selection into the saved copy.
- **Touch-sized selection handles.** Upstream's handles are small for fingers; consider larger hit areas in touch mode.

---

# Plan (as approved)

## Context
The goal is a pen-first app for notes and PDF annotation. It keeps Xournal++'s tested core:
- .xopp/.xoj compatibility
- stroke rendering, pressure handling, stabilizer and palm rejection
- file I/O, undo and tools

It adds what Xournal++ lacks:
- **several documents as tabs in one window**, the main difference from Xournal++
- a modern UI that works in tablet mode without a keyboard
- iPad-like momentum scrolling and pinch zoom
- Wayland first
- a path to faster PDF rendering with MuPDF, a GoodNotes-style library, PDF-native annotations and mobile

Target repo: `~/xournal_qt_workspace/xournal_qt` (git init'ed, no commits). References: `../xournalpp` (HEAD `283c367`, full history, built binary `build/xournalpp` supporting `--create-img` and `--create-pdf`) and `../krita`.

Test device:
- ThinkPad Yoga running KDE neon 6.2 (Plasma 6.2 **Wayland**)
- Iris Xe GPU, 1920×1200 display at scale 1
- input devices `Wacom HID 5286 Pen` and `Wacom HID 5286 Finger`

Installed: Qt 6.7.2 (Quick, Controls2, Wayland, RHI, VirtualKeyboard), cairo, pango, poppler-glib 24.02, qpdf 10.6, libzip, libxml2, MuPDF 1.19 (old), maliit on-screen keyboard.

**User decisions:**
- GPL now; AGPL-3.0 is acceptable once MuPDF is added.
- A **real git fork** with upstream history.
- The first usable version must include pen, highlighter and eraser, undo/redo, tabs, PDF annotation, .xopp save, **lasso select with move/resize, text, image insert, PDF export, PDF search, and page thumbnails**.

---

## Architecture decisions

1. **Qt 6 (system 6.7.2), C++20, CMake plus Ninja.**
   - A Qt Quick (QML) shell with a custom C++ canvas, because it is touch-first and the only realistic Qt mobile path.
   - Krita dropped its QML canvas and UI after years of tablet bugs, mostly in the Qt 5 era. So the canvas is built **independent of its host**: a `CanvasCore` plus thin hosts (`QQuickItem`, `QWidget`).
   - An on-device spike in **M0** decides which host wins. If the Qt Quick host loses on input fidelity or latency, the shell becomes Widgets with QML panels via `QQuickWidget`, and nothing else changes.

2. **All canvas input goes through C++, never QML handlers.**
   - A window-level event filter catches `QTabletEvent`, `QTouchEvent`, mouse, wheel and `QNativeGestureEvent` before Qt Quick delivers them.
   - Filters run in `QCoreApplication::notify` before `QQuickWindow::event`. Accepting a tablet event there also stops Qt from synthesizing mouse events.
   - The filter hit-tests the canvas rectangle, so the pen still works on the toolbars.
   - An app-wide filter tracks proximity (Qt delivers proximity events only to `qApp`).
   - Set `AA_CompressHighFrequencyEvents=false`, so Qt doesn't merge rapid pen moves (Xournal++ also turns off event compression).

3. **The fork keeps class names and replaces only the GTK "glue".**
   - Retained code (tools, undo actions, input handlers, overlays) calls a small API: about 28 `Control` methods (`getDocument` ×106, `getSettings`, `getUndoRedoHandler`, `getToolHandler`, `fire*`, `getCursor`, `getZoomControl`, …) and about 25 `XournalView`/`XojPageView` methods.
   - We write Qt-based replacements of **`Control`** (which becomes the *per-tab session*, with `using DocumentSession = Control;`), **`XournalView`**, **`XojPageView`**, **`XournalppCursor`**, and the GTK parts of **`ZoomControl`** and **`Layout`**, with the same names and methods.
   - About 30 undo actions and the tool and input handler logic then compile with almost no edits, so later upstream merges stay easy.
   - Every fork edit to an upstream file is tagged `// xqt:`.

4. **Rendering keeps cairo**, so strokes look pixel-identical and `core/view` is reused as-is.
   - A cairo `ARGB32` image surface wraps as a `QImage::Format_ARGB32_Premultiplied` without copying.
   - The new `RenderService` renders **tiles anchored to the page** (512 device px) on a `QThreadPool`, using `DocumentView::drawPage` under `std::shared_lock<Document>` as in upstream `RenderJob`. Jobs are tagged per session and cache generation, with a global LRU memory budget.
   - The PDF layer goes through upstream `PdfCache`, rendering the whole page at moderate zoom. Above a pixel cap it renders per tile, which avoids Xournal++'s ~100 MB full-page buffers at high zoom.
   - During pinch or fling, existing tiles are GPU-scaled. They are re-rendered sharp after a ~300 ms settle (Xournal++'s `blockRerenderZoom`), with low-res page previews as placeholders.
   - **Live stroke:** upstream `StrokeToolView` draws only new segments incrementally, as in Xournal++. Dirty tiles are composited on the CPU (clean tile plus overlays, so highlighter `MULTIPLY` stays correct) and only those tiles are uploaded.
   - On pen-up, `drawAndDeleteToolView` paints the stroke permanently into the cached clean tiles, so there is no re-render and no blink.
   - Tiles snap to device pixels.

5. **PDF engine:** poppler-glib, reusing `core/pdf/popplerapi`, until M6. Then a **MuPDF** backend behind `XojPdfDocumentInterface`, through a new backend factory, selectable at runtime. It uses display lists and one `fz_context` per thread for parallel tile rendering. That makes the app AGPL-3.0, which the user accepted.

6. **GLib stays for now.** Qt on Linux runs on the GLib event loop, so upstream `g_idle_add`/`g_timeout_add` code keeps working. `Util::execInUiThread` becomes a pluggable dispatcher, which the Qt app points at `QMetaObject::invokeMethod`. Removing GLib is a later mobile task.

7. **Tabs:** `AppContext` holds process-wide state, one `Control` (session) exists per tab, and each tab has its own `XournalView` plus canvas.
   - **Shared across tabs:** Settings, ToolHandler (the same pen in every tab), Palette, PageTypeHandler, MetadataManager, ActionRegistry, InputDeviceRegistry, PalmRejection, RenderService, and a CrashHandler that covers all sessions.
   - **Per tab:** Document, UndoRedoHandler, LayerController, ZoomControl/viewport, selection, search, autosave state and file path.
   - No `replaceDocument` copying: each tab builds its own `Document`, which also avoids the `try_lock`/`unlock` undefined behaviour in `Document::operator=`.

8. **Input feel:**
   - Port the Xournal++ handlers almost line for line, with a retyped, Qt-free `InputEvent`. This keeps `filterPressure`, `inferPressureValue`, the tap filter, splitting a stroke when it crosses to another page, one device per sequence, and the stabilizers.
   - Add tilt and rotation fields for future use.
   - Add Krita's synthetic-event filter (`KisInputEventsEater` logic, GPL-compatible, with attribution).
   - **Palm rejection:** pen near, hovering or pressed blocks touch, plus the HandRecognition timeout (on by default here), and pen-down cancels any touch gesture in progress.
   - **Touch drawing:** AUTO. It turns off once a pen is seen, so fingers navigate.
   - **Gestures:** two-finger tap is undo, three-finger tap is redo.
   - An optional pressure-curve lookup table, linear by default, so the feel matches Xournal++.
   - Draw our own pen hover dot, because the pen cursor is broken on Qt 6.7 Wayland.

9. **Viewport:** a new `ViewportController` replaces GtkAdjustment and GtkScrolledWindow.
   - It reuses the `Layout`/`LayoutMapper` grid math and the `ZoomControl` zoom-sequence math.
   - Pinch zoom keeps the document point under the fingers fixed (Krita-style).
   - iOS-like momentum (exponential decay) and rubber-band edges.
   - Touchpad pinch via `QNativeGestureEvent`, and Ctrl+wheel zoom.

10. **Future hooks, built now only as seams:**
    - a `DocumentSource` abstraction for the library later;
    - the backend factory for PDF engines;
    - export backends: the qpdf-based `QPdfExport` is the base for PDF-native `/Ink` annotations.

## Repository layout (fork)
```
xournal_qt/                      (git fork of xournalpp; remote "upstream")
  CMakeLists.txt                 fork-owned; explicit source lists, no GLOB
  cmake/XojCoreSources.cmake     allowlist of reused upstream files
  FORK.md                        rules, `xqt:` tag, merge procedure, replaced-file list
  docs/decisions/*.md            ADRs (UI host, PDF engine, ...)
  src/util/ ...                  upstream, de-GTK'd in place
  src/core/{model,undo,view,pdf,control/...}  upstream, de-GTK'd in place
  src/core/control/Control.{h,cpp}            REPLACED: per-tab session
  src/core/gui/{XournalView,PageView,Layout,...}  REPLACED or edited glue
  src/core/gui/inputdevices/*    upstream handlers, retyped events
  src/qt/app/                    main.cpp, AppContext, TabManager, ActionRegistry, CrashHandler
  src/qt/canvas/                 CanvasCore, RenderService, TileCache, ViewportController,
                                 QtInputTranslator, SyntheticEventFilter, PalmRejection, hosts
  src/qt/models/                 QML-exposed models (tabs, tools, pages, outline, search)
  src/qt/qml/                    QML UI (Main.qml, TabStrip, Toolbar, Sidebar, Settings…)
  src/tools/xqt-cli/             headless render/roundtrip/export/bench CLI (Qt-free)
  spikes/input_canvas/           M0 throwaway spike
  tests/                         ported upstream unit tests + new tests, golden harness
```
CMake targets:
- `xoj-util` and `xoj-core`: Qt-free. Model, xojfile, xml, view (non-overlay), pdf plus export, settings, shaperecognizer, PdfCache, pagetype.
- `xoj-app`: the Qt glue plus tools, undo, overlays, input and canvas.
- `xournal-qt`: the executable.
- `xqt-cli`, and the test targets.

---

## Milestones

### M0: Fork bootstrap and on-device input spike (decides the UI host)
**Fork:**
- In `xournal_qt`, fetch history from the local clone: `git fetch ../xournalpp master`, create `main` at `283c367`, then `git remote add upstream https://github.com/xournalpp/xournalpp.git`.
- Add `FORK.md`. Upstream files keep their GPL-2.0-or-later headers; new files are GPL-2.0-or-later too, so they can be upstreamed. The combined app ships as GPL-3.0+, and as AGPL once MuPDF is in.
- Replace the top-level `CMakeLists.txt`. Upstream `src/*/CMakeLists.txt` stay unused.

**Spike** (`spikes/input_canvas`, `--host=quick|widget`):
- Log every tablet, touch, mouse, native-gesture and proximity event: device name, `pointerType` (pen/eraser), pressure, xTilt/yTilt, rotation, buttons (barrel buttons), `QPointF` precision, timestamp. Logging category `xqt.input`.
- Draw pressure strokes into page-anchored cairo tiles with incremental segments (StrokeViewHelper-style).
- Pan and zoom by GPU or QPainter transform, with simple momentum.
- On-screen latency readout (event timestamp vs. frame swap).
- A `TextField`, to check that the Maliit on-screen keyboard appears through Wayland text-input.
- Print the event-dispatcher class, which should be `…Glib`.

**Exit:**
- `docs/decisions/0001-ui-host.md` records the choice, backed by a checklist:
  - pressure and tilt arrive
  - the eraser end is detected
  - barrel buttons work
  - hover events arrive
  - proximity events arrive, or don't
  - no double mouse-synthesis
  - 60 fps pinch
  - comparable latency
  - the on-screen keyboard appears
- Default: Qt Quick, unless it clearly loses.

### M1: Qt-free `xoj-core`, headless CLI, tests
**De-GTK edits** (all tagged `xqt:`). Fixing `Util.h` alone clears most of the include offenders.
- **util:**
  - `util/include/util/Util.h` and `util/Util.cpp`:
    - drop `<gtk/gtk.h>` and `paintBackgroundWhite`
    - make `execInUiThread` a pluggable dispatcher with a `g_idle_add_full` default
    - make `cairo_set_source_rgbi/argb` use `cairo_set_source_rgba`
  - `util/include/util/Color.h`: remove the `GdkRGBA` converters.
  - `raii/GObjectSPtr.h` and `raii/GLibGuards.h`: remove the gtk include and the `WidgetSPtr` alias.
  - `XojMsgBox`: replace it with a callback `MessageSink` (the CLI prints to stderr; the app shows a QML dialog).
  - `VersionInfo`: rewrite without GTK.
  - Leave out of the build: `GtkUtil`, `gtk4_helper`, `gdk4_helper`, `PopupWindowWrapper`, `GListView`, `GtkWindowUPtr`, `GtkPaperSizeUPtr`.
- **model:**
  - `Document.h/.cpp`: replace the `GtkTreeStore` table of contents with a plain C++ `PdfOutline` tree.
  - `LinkDestination`: drop the GObject `XojLinkDest`.
  - `PaperSize`: delete the GTK constructor.
  - `Element.h`: remove the stray gdk include.
  - `Image.cpp` and `view/background/ImageBackgroundView.cpp`: replace `gdk_cairo_set_source_pixbuf` with a small `pixbufToCairoSurface()` helper that needs only gdk-pixbuf.
  - `Image.cpp`: an image that cannot be read freed its `GError` with `g_free` instead of `g_error_free`; with GLib's slice allocator (GLib < 2.76) that aborts the program. A fix for upstream as well.
  - `Compass`, `Setsquare` and `GeometryTool` are built (their views too); the GTK input handlers are replaced by `qt/src/canvas/GeometryToolLayer` with GTK-free shadow headers.
- **pagetype:** `PageTypeHandler` takes an `fs::path` instead of `GladeSearchpath` and reports errors through `MessageSink`.
- **view:** `View.h` and `Mask.h` include only cairo.
- **pdf:**
  - `XojPdfDocument.cpp`: `new PopplerGlibDocument()` becomes `XojPdfBackendFactory`.
  - `XojCairoPdfExport`: build the PDF outline from `PdfOutline`.
  - Move `ExportBackgroundType` from `control/jobs/BaseExportJob.h` to `pdf/base/ExportBackgroundType.h`.
- **settings** (`Settings.h/.cpp`):
  - The device map stores `InputDeviceTypeOption` plus an app enum `DeviceSource` instead of `GdkInputSource`.
  - Remove the `DeviceListHelper` include.
  - Compile the pure `gui/toolbarMenubar/model/ColorPalette` into core.
- **config headers:** generate them from upstream `src/config*.h.in` with `ENABLE_AUDIO=OFF`, `ENABLE_PLUGINS=OFF`, `ENABLE_QPDF=ON`.

Reused without changes: `control/xojfile/*` (GMarkup parser, LoadHandler, SaveHandler), `control/xml/*`, `core/view/*` except the overlays, `control/PdfCache`, `control/shaperecognizer/*`, `control/jobs/ImageExport`, and `pdf/base/{HybridPdfExport,QPdfExport,XojPdfExportFactory}`.

**`xqt-cli`** (links only `xoj-core`):
- `render <file> --dpi N --out dir`, reusing ImageExport logic
- `roundtrip in.xopp out.xopp`
- `export-pdf in out.pdf`, through qpdf hybrid export
- `bench-render <file> --zoom z`, the baseline for M6

**Tests:**
- Port the GTK-free upstream unit tests (`test/unit_tests/{util,model}/*`, `control/LoadHandlerTest`, `MetadataManagerTest`, and `SettingsTest` if feasible), using GoogleTest via FetchContent.
- `tests/golden/run.sh` renders each fixture in `../xournalpp/test/files` plus sample PDFs with both `xournalpp --create-img` and `xqt-cli render`, then pixel-diffs the results.

**Exit:**
- `ldd xqt-cli` shows no libgtk or libgdk.
- The unit tests pass.
- The golden diffs are 0, or within anti-aliasing tolerance.
- A round-trip keeps the element counts and renders identically.
- The exported PDF opens correctly in Okular.

### M2: Canvas and pen on one document (the Qt glue layer)
- **Glue replacements** (same names, the API subset listed in decision 3):
  - `src/core/control/Control.{h,cpp}` becomes the session. It owns Document, UndoRedoHandler, LayerController, ScrollHandler, ZoomControl and NavigationHistory, and implements `DocumentHandler`.
  - `getWindow()->getXournal()` call sites (about 20) become `control->getXournal()`, a mechanical edit.
  - `src/core/gui/XournalView.*`, `src/core/gui/PageView.*` (keep upstream's `onButtonPress/Motion/Release` tool dispatch logic almost verbatim; replace the whole-page buffer with tiles), and `XournalppCursor` (becomes `QCursor` plus the hover dot).
  - `src/core/gui/Layout.cpp`: switch the adjustment calls to the `ViewportController` interface. `LayoutMapper` stays.
  - `ZoomControl.cpp`: drop the GTK signal hookups (lines 24-73 and 214-233); `ActionDatabase` becomes `ActionRegistry`.
- **Input:**
  - Retype `gui/inputdevices/InputEvents.h`, `PositionInputData`, `DeviceId` and `KeyEvent` to Qt-free types: device ref, modifiers bitmask, 64-bit sequence id, timestamp, tilt.
  - New `QtInputTranslator` replaces `InputEvents::translateEvent`.
  - Keep `InputContext::handle` dispatch and the Stylus, Mouse, Touch, TouchDrawing and Pen handlers.
  - `HandRecognition` keeps its timer logic. `TouchDisableX11` is excluded.
  - Add `SyntheticEventFilter` and `PalmRejection` (Krita-style state machine).
  - Device classes are mapped by Qt device name and `QInputDevice::DeviceType`.
- **Canvas:**
  - `CanvasCore` plus the chosen host, `RenderService`/`TileCache`, and `ViewportController`.
  - The overlay path: CPU composite of dirty tiles, upload of those tiles only. The EditSelection and hover layer is drawn in viewport space.
- **Tools and data:**
  - Tools: pen, highlighter, whiteout, eraser (standard and delete-stroke), hand.
  - `StrokeHandler`, `StrokeStabilizer`, `EraseHandler` and `InputHandler` take the new `Control*`.
  - Undo and redo through `UndoRedoHandler`.
  - Open .xopp/.xoj/.pdf and save .xopp through a temporary minimal QML toolbar.
- **Exit:** the on-device checklist (`docs/testing/device-checklist.md`):
  - visible pressure and a matching look
  - eraser end and barrel buttons
  - the hover dot
  - resting a palm while writing never pans or zooms
  - smooth pinch at 60 fps
  - fling momentum
  - no gaps when writing fast
  - latency subjectively at least as good as Xournal++
  - A file saved here renders identically in Xournal++ (golden check).
  - Scrolling a 200-page PDF stays within the memory budget.

### M3: Tabs and the app shell
- **`AppContext`:** config in `~/.config/xournal-qt/` (separate from Xournal++), shared ToolHandler, `ActionRegistry` (keeps the upstream `Action` enum and names for shortcuts), `InputDeviceRegistry`, and `CrashHandler`, which emergency-saves every dirty session.
- **`TabManager` and `DocumentTabsModel`:**
  - new, open (switching to the tab if the file is already open), close with an unsaved-changes prompt, reorder
  - restore open tabs on start
  - background tabs drop their tile cache after 30 s
  - single instance: `xournal-qt file.pdf` opens a tab in the running window, via `QLocalServer`
- **QML shell (touch targets ≥ 44 px):**
  - tab strip
  - main toolbar: tools, colour swatches, width presets, undo/redo, add page, zoom/fit, overflow menu
  - contextual tool options
  - collapsible page-thumbnail sidebar (PreviewJob logic into `RenderService` low-res jobs) with page operations: insert, delete, duplicate, move, background template via `PageTypeHandler`
  - file dialogs through the xdg portal, recent files
  - autosave per session (AutosaveJob logic)
  - light and dark themes
  - desktop shortcuts (Ctrl+Z/Y/S/O/N/W/Tab)
  - `setDesktopFileName` for the Wayland app id
- **Exit:**
  - Five documents in tabs, and switching between them shows the page in under 100 ms.
  - Undo and edits stay isolated per tab.
  - The close prompts work.
  - After `kill -9`, the crash-recovery files reopen.

### M4: First usable version (the user's scope)
- **Lasso and rectangle selection:**
  - Reuse `Selector`, `EditSelection`, `EditSelectionContents` and `SelectorView`.
  - Enlarge handle hit areas for touch.
  - Move, scale and rotate.
  - Copy, cut, paste, delete and duplicate, with a `ClipboardHandler` rewritten on `QClipboard` and upstream `util/serializing` reused.
  - A long-press context menu.
  - Edge-panning through `QTimer`.
- **Text:**
  - New `TextEditor`: a port of the upstream editing logic, cursor, selection and `TextBoxUndoAction`.
  - The model and rendering stay pango, so text metrics match Xournal++.
  - Input comes from Qt input methods (`inputMethodEvent/Query`), which brings up the Maliit keyboard on Wayland.
  - `TextEditionView` is reused.
- **Images:** insert from a file or the clipboard, through `ImageHandler` and `ImageSizeSelection` logic plus a QML picker.
- **PDF:**
  - Text search with `SearchControl`, `SearchResultView` and a search bar.
  - Text selection and copy with `PdfElemSelection` and its view.
  - Links (URLs through `QDesktopServices`, internal destinations).
  - Outline sidebar built from `PdfOutline`.
- **Export:**
  - PDF via `XojPdfExportFactory`, where qpdf hybrid export keeps the original PDF intact.
  - PNG and SVG via `ImageExport`.
  - A QML dialog for page range and background options.
- **Exit:**
  - An end-to-end session: annotate a lecture PDF using text, an image and lasso-move, export it, and open the result in Okular.
  - Rasterize our export and Xournal++'s export of the same .xopp with `pdftoppm`; they should match.

### M5: Parity round 2 and polish
- Shapes: line, rectangle, ellipse, arrow, coordinate system, ruler, spline.
- Shape recognition, and grid and rotation snapping.
- A layers panel, vertical space and the laser pointer.
- A settings UI for:
  - device-class mapping
  - stylus and touch button config
  - the pressure-curve editor
  - the stabilizer
  - the palm-rejection timeout
  - touch-drawing mode
- Presentation and fullscreen mode, and inserting PDF pages.

### M6: MuPDF and performance
- Vendor MuPDF 1.26.x as `third_party/mupdf`, built as an ExternalProject with `make libs`, no X11 or GLUT. Try the system 1.19 first to prototype.
- `src/core/pdf/mupdf/` implements `XojPdfDocumentInterface`, `XojPdfPage`, `XojPdfBookmarkIterator` and `XojPdfAction`:
  - one `fz_context` per thread, via `fz_clone_context` plus lock callbacks
  - a display-list cache per page
  - tiles rendered with `fz_run_display_list` into a BGRA premultiplied pixmap that wraps the cairo surface memory
  - text search, selection and links through `fz_stext_page`
- A runtime setting chooses the backend.
- Compare backends with `xqt-cli bench-render`. The target is ≥ 2× faster tiles on heavy PDFs.
- Tune the RenderService threads and memory budget.
- Optionally tessellate the live stroke on the GPU if upload latency turns out to matter.

### M7+: Future (the seams exist; nothing is built yet)
- **PDF-native annotations:** a "save into PDF" mode that writes `/Ink` annotations whose appearance streams (`/AP`) come from cairo-pdf, using qpdf or the MuPDF `pdf_*` API, plus an embedded .xopp for a lossless round-trip. Also importing existing PDF annotations.
- **Library:** a SQLite catalog behind `DocumentSource` (QtSql), FTS5 search over PDF and typed text, a thumbnail cache, folders and tags, and a library screen.
- **Android:** the QML shell already fits. The C dependencies come through vcpkg. Reduce the GLib dependency.

## Out of scope until after M4
- Audio recording and playback. `AudioContent` is still kept when saving.
- Lua plugins.
- The LaTeX editor. TeX elements still render and round-trip.
- ~~Setsquare and compass~~ (done: the upstream model and views with Qt input in `GeometryToolLayer`).
- ~~Printing~~ (done: PDF export through `QPrintDialog`). Toolbar customization stays out (the bar is fixed, only hiding it is offered), and translations (gettext `_()` stays in core; QML uses `qsTr`).
- X11 OS-level touch-disable.
- Shapes, ruler and recognition (M5).

## Verification (every milestone)
- `ctest`:
  - the ported upstream unit tests
  - new tests for `ViewportController` physics
  - `PalmRejection` and `SyntheticEventFilter`, fed scripted event sequences
  - `TileCache` eviction and invalidation
  - a `Control` session running undo/redo scripts
- A golden render harness against `xournalpp --create-img`, and a .xopp round-trip harness.
- Input diagnostics: `QT_LOGGING_RULES="xqt.input*=true"` and an in-app debug overlay showing pressure, tilt, device and latency.
- On the device: `docs/testing/device-checklist.md`, run on Wayland (default) and again with `QT_QPA_PLATFORM=xcb`.
- Cross-compatibility: files written by xournal_qt open in upstream Xournal++, and the reverse.

## Main risks and mitigations
1. **Qt 6.7.2 Wayland tablet gaps.** Krita patches these in its own Qt: no pen cursor, no Enter/Leave for accepted tablet events, possibly unreliable proximity.
   - Draw our own hover dot.
   - Detect pen activity from event timestamps and timeouts instead of proximity alone.
   - The M0 spike measures this; if needed, move to a newer Qt through aqtinstall.
2. **Qt Quick tablet delivery.** The host-agnostic `CanvasCore` and the M0 go/no-go point keep a Widgets fallback cheap.
3. **Live-stroke latency from texture uploads.** Upload dirty 256–512 px tiles only; GPU tessellation is the fallback.
4. **Upstream merge pain.** Keep upstream class and method names, tag edits `xqt:`, list replaced files in `FORK.md`, and merge `upstream/master` regularly.
5. **Poppler serializes renders per document** with a mutex. Acceptable until MuPDF (M6). Pango works per thread as in upstream `RenderJob`.
6. **Memory with many tabs: done.** `CanvasMemory` holds one limit for the rendered pages of all tabs (Settings →
   Documents → Memory; a quarter of the RAM by default, at most a third): the document used last takes up to 70 % of
   it (35 % before the visible pages, 65 % after them) and renders them ahead on idle-priority workers; the others
   keep theirs while the rest of the limit holds them, the one used longest ago giving them up first. A tenth of the
   limit holds a preview of every page (`PageSketches`), which the canvas shows until a page is rendered and which
   the sidebar and the overviews scale their thumbnails from; they are stored in the cache for the next opening.
7. **Fractional scaling.** Snap to device pixels, and test at scales 1.25 and 1.5.

---

## Revisions after the design review (supersede the matching points above)

- **R1 Build root is `qt/CMakeLists.txt`** (`cmake -S qt -B build-qt -G Ninja`).
  - Upstream's root `CMakeLists.txt` stays untouched; it had 85 upstream commits in two years.
  - All new code lives under `qt/`: `qt/src/{session,render,canvas,input,quick,actions,app}`, `qt/qml`, `qt/cli`, `qt/tools`, `qt/spikes`, `qt/tests`, `qt/docs/adr`.
- **R2 GTK files are never deleted.** They simply aren't built, which avoids modify/delete merge conflicts.
- **R3 `qt/compat/gtkshim`** provides `gtk/gtk.h` and `gdk/gdk.h` headers:
  - glib, gio, cairo and gdk-pixbuf includes;
  - `GdkRGBA`, `GdkRectangle`, `GdkInputSource`, `GdkModifierType` and opaque types, all with GDK3 values;
  - an exact port of `gdk_cairo_set_source_pixbuf/rgba/region`.

  Many upstream files then compile unchanged, and any real GTK widget call fails at compile time.
- **R4 Seams instead of replacing `Control`.** New interfaces, `undo/UndoContext.h` and a `ToolContext`, use method names identical to `Control`'s.
  - Upstream `Control.h/.cpp` stay unbuilt.
  - Undo actions only change their signature and include lines.
  - The session (`DocumentSession`) implements both interfaces.
  - Seam guards use `#ifdef XOJ_NO_GTK` and the tag `// xournal-qt:`.
- **R5 Rendering v1.**
  - The CPU side keeps a per-page buffer (`PageRaster`), a near-verbatim port of `RenderJob` and the `XojPageView` buffer semantics: partial rect re-render, `drawAndDeleteToolView`, stale buffer scaled during zoom, the 300 ms re-render block.
  - Above a size cap it switches to a "windowed buffer" (visible region plus margin).
  - The GPU side uses 256–512 px display tiles only for partial uploads; dirty tiles are re-composed on the CPU (buffer plus overlays).
  - During pinch, one quad is drawn per page, because layout padding is in fixed pixels.
  - The PDF background is rendered outside the document lock, which avoids a stall at pen-up.
  - True page tiling comes with MuPDF display lists.
- **R6 Input on Qt 6.7 Wayland.**
  - Tablet devices come through without their real names, so devices are classified by `QInputDevice::DeviceType` plus `PointerType`.
  - The event filter must `accept()` and return `true` inside the canvas, and leave events unaccepted outside it.
  - An `OverlayRegistry` of QML rects (toolbars, popups) decides whether the canvas claims a point.
  - Touch ownership is decided at the first finger down.
  - `TouchInputHandler` becomes a new `GestureRecognizer`; `HandRecognition` becomes the new `PalmRejection`.
- **R7 Settings.** Upstream `Settings` (`settings.xml`) keeps the shared settings, with `XOJ_CONFIG_FOLDER_NAME=xournal-qt`. New app settings (tabs, gestures, UI) go in `AppSettings` (QSettings).
- **R8 `Util::execInUiThread` and `Job::afterRun`** go through `setUiDispatcher()`: `QMetaObject::invokeMethod` in the app, synchronous in the CLI.
- **R9 Milestone order.** M1 core+CLI → M2 sessions + render service (headless) → M3 tools (headless replay tests) → M4 canvas, Quick host and input (on device) → M5 tabs + shell + the user's first-usable scope. The first-usable scope is unchanged: lasso, text, image, PDF export/search, thumbnails. MuPDF and later milestones follow.
- **R10 The CLI mirrors upstream flags** (`--create-img`, `--create-pdf`, `--export-png-dpi`, …), so the golden harness can call both binaries the same way. `xoj-imgdiff` (C++/cairo) does the pixel diffs.
