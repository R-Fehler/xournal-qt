# Library and home screen

A **library** is a plain folder of documents that a window works in, like a workspace in an editor. Each window
(process) shows one library. Two libraries mean two windows.

- `xournal-qt` opens the default library `<Documents>/Xournal_Libraries/Default` and creates it if needed.
- `xournal-qt <folder> [files…]` opens a folder as library. Any folder with PDFs and `.xopp` files works.
- The library menu (▾ next to the library name) lists the libraries in `<Documents>/Xournal_Libraries`. The one of
  this window is highlighted; choosing another one opens it in a new window, as do "New library…" and "Open a folder
  as library…". A window never shows two libraries.
- The Downloads folder is offered there too, as a quick library: all downloaded papers at once. Like every library
  it keeps its cache (previews, search index) in `.xournal_library/` folders, so opening it again is as fast as
  any other library. A note in the library says that its files are
  short-lived, and importing or copying documents into it from elsewhere asks first.
- "Copy to…" / "Move to…" can go into another library: the dialog has a library choice above the folders.
  Documents keep their PDF (the .xopp is rewritten), folders keep their subfolders; on another disk a moved folder
  is copied, then deleted.
- Starting the app again for the same library hands the files over to the running window. Each library has its own
  single-instance socket and its own session journal: `session.json` for the default library, else
  `sessions/<hash>.json` in the config folder.

## Documents on disk
- `name.xopp` next to `name.pdf` is **one** document, and it opens as the `.xopp`. A lone `.xopp` (or `.xoj`) or a
  lone PDF is one document too. Older `name.pdf.xopp` files pair with `name.pdf`.
- These files are never shown:
  - `name.xopp.bg.pdf`, an attached PDF. It belongs to its `.xopp` and travels with it.
  - `.name.pages.pdf`, the merged PDF of a `.xopp` with PDF pages pasted from other PDFs (below). It belongs to its
    `.xopp` and travels with it.
  - hidden files: autosaves, `.xournal_library`
  - backups (`~`)
- **Rename / move** rename or move both files. The `.xopp` is loaded and written again with upstream's LoadHandler and
  SaveHandler, so its PDF reference (relative to the `.xopp`) points to the new place. Open tabs and the recent list
  follow the new paths.
- **Import / copy** copy a `.xopp` together with the PDF it uses. That PDF is stored as `name.pdf` next to the copy,
  even if it came from somewhere else. A PDF brings the `.xopp` next to it along. A folder is copied with its whole
  folder structure (also empty subfolders) and all documents in it. Other files and hidden folders (`.git`, …) stay
  behind. A name that is taken becomes "name (2)".
- **Trash** moves the files (or the folder) to the desktop trash.

### PDF pages pasted from another PDF
A `.xopp` has one background PDF, and its pages refer to page numbers in it (that is the format, and it stays
openable by Xournal++). PDF pages pasted from another PDF therefore go into one **merged PDF** per document, which
the document uses as its background from then on. Their text stays searchable and selectable.
- A document that annotates a PDF (`lecture.xopp` + `lecture.pdf`): the hidden `.lecture.pages.pdf` next to it, with
  the lecture's pages followed by the pasted ones. `lecture.pdf` itself is never changed.
- A document that had no PDF: the merged PDF is `lecture.pdf` next to it, so the library pairs the two as one
  document; if that name is taken by another PDF, the hidden `.lecture.pages.pdf`.
- Nothing is written next to the document before it is saved: pasted pages go into a merged PDF in the cache
  (`~/.cache/xournal-qt/pasted-pages`), and saving puts it next to the `.xopp` by the rules above. Closed without
  saving, the document leaves nothing next to it, and the cache copy is removed (also for a document recovered
  after a crash, and when the recovery is declined).
- Every later paste, from any PDF, goes into the same file; the same copied pages pasted again are not added twice.
  Pages pasted within the same document keep referring to its own PDF pages.
- Saving drops the PDF pages no page shows any more (deleted pasted pages, deleted pages) and renumbers the pages;
  nothing is written when nothing changed. Pages that come back through undo get their PDF pages back (kept in
  memory). "Save as" gives the new name its own copy.
- A save that renumbers the merged PDF the saved `.xopp` refers to writes it as `.<name>.next.pdf` first, writes the
  `.xopp` referring to that, gives the PDF its name (a second link to the same file, renamed over the old one),
  writes the `.xopp` again and removes the other name. A crash at any point leaves a `.xopp` with a PDF that
  matches it; the next save tidies up.
- A merged PDF carries a mark (`/XournalQtPages` in its document information), so a user's own PDF is never
  rewritten. All writes go to a hidden `.….part` file first, which is then renamed over the target.
- The note after pasting says where the pages are kept.

Code: `qt/src/shell/DocumentFiles.*`; the merged PDF: `qt/src/session/MergedPdf.*` (qpdf), `PdfPageKeeper.*`.

## The library cache (`.xournal_library/`)
The cache only speeds things up and can be deleted at any time. Each folder with documents has its own hidden
`.xournal_library/`, with the cache of **only the documents directly in it** (never those of its subfolders). A
folder without documents gets none, and when its last document goes, the folder goes too (unless something other
than the app's files is in it). Opening a library reads the caches of all its folders and merges them; a subfolder
opened as a library of its own finds its caches already there. Entries are keyed by file name, so a folder moved or
renamed by any program keeps its cache.

**Where the cache is kept** is a setting of each library (Settings → Storage), stored next to the library's other
state in `~/.config/xournal-qt/libraries/<key of the library>/library.json`:
- in the folders (the default): the hidden `.xournal_library/` in each folder;
- in the app cache (recommended for folders that sync clients upload): the same cache folders under
  `~/.cache/xournal-qt/libraries/<key of the library>/<folder in the library>/.xournal_library/`, so the library's
  folders get no files of the app. Documents moved by another program are found again by name, size and time.
  (A subfolder opened as a library of its own has its own setting and cache there.)

Switching moves the packs from one place to the other. Folders that cannot be written keep their cache in the app
cache in either mode.

**Clean-up** (Settings → Storage) shows what the cache of the library takes (bytes and files). "Remove all cache
folders of this library…" explains, then removes every `.xournal_library/` below the library (only the files the app
recognises as its own; a folder with other files in it stays) and the library's folder in the app cache, and closes
the window (asking about unsaved documents first), so the app does not build the cache again right away, e.g. while
the library is zipped to be sent. Until the library is opened again nothing is cached or indexed. The reading
positions are kept.

A cache folder holds a few **packs**, one file each, split by how often they change:
- `notes.pack`: per document (by file name): its kind (`xopp`, `pdf`), name, the size and time of its `.xopp`, the
  PDF it uses (relative to the folder when it is in the library; next to it: its name) with that PDF's size and
  time, and per page which PDF page it shows, the text of its text elements and its shape. Small; written again
  when a `.xopp` in the folder is saved.
- `pdf-text.pack`: per document, the text of the PDF pages it shows, tied to the PDF's size and time. Big; written
  only when a PDF changed or a document came or went. A document with over 1 MB of PDF text gets a file of its own,
  `pdf-text-<hash>.pack`, written only when that text changes.
- `previews.pack`: the first-page previews (PNG, 360 px wide, drawn like the page thumbnails, of the title page),
  each with the size and time of the document's files and its title page, so a changed document gets a new
  preview. Not compressed again (PNG is). A folder's pack is read when its first card is shown and kept in memory
  (up to 48 MB of previews; the folders used least recently go first); new previews are written a few seconds
  later. Renamed or moved in the app, a document takes its preview along. Previews of documents outside a library
  (recent files) are PNG files in `~/.cache/xournal-qt/previews`.

A pack is CBOR (Qt's `QCborValue`) compressed with zlib, behind a header with a format number: a pack of another
format is read anew. It is always written whole, under another name first (`QSaveFile`), never changed in place:
sync clients upload whole files anyway, and neither the app nor a sync client ever sees half a file. Changed packs
are written in the background a few seconds after the last change (at the latest 30 s after the first one), and
when the library is closed.

**Reading positions are not cache.** The title page and the page each document was left at (and when it was last
read) are kept in the config folder, `~/.config/xournal-qt/libraries/<key of the library>/pages.json`, by the
document's path in the library: renaming and moving in the app take them along, and removing the cache folders
keeps them. The `pages.json` of a library's `.xournal_library/` from before is taken over the first time. (A library
folder moved or renamed outside the app gets another key and starts without them.)

**The layout before the packs** (until 2026-09: everything in the root's `.xournal_library/`: `index/` with one JSON
file per document, `previews/` with one PNG per document, `pages.json`) is converted when such a library is opened,
in the background before indexing: the entries and previews go into the packs of their folders (nothing is read
again; entries of documents changed since are read as usual), the reading positions into the config folder. Only
when all packs are written are `index/`, `previews/` and `pages.json` removed, nothing else. A read-only library's old
cache in `~/.cache/xournal-qt/libraries/<key>/` is converted the same way.

**The search index** is what the library search searches. A background thread keeps it up to date, one document at
a time:
- nothing changed (the `.xopp` and the PDF it uses have their size and time): nothing is read;
- only the `.xopp` changed (annotations, text elements, pages added or moved): the `.xopp` is read again, the PDF
  text is kept (only PDF pages not shown before are read) — e.g. 100 ms instead of 470 ms for a 300-page PDF; only
  the `notes.pack` of its folder is written;
- the PDF changed (also an attached one, or one elsewhere): its text is read again;
- renamed or moved in the app (also whole folders): the entries move along; only a pair's `.xopp`, which is written
  again with the new path of its PDF, is read again (without PDF text). A moved folder takes its cache folders
  along;
- moved or renamed by another program: a folder keeps its cache; a document moved into another folder is found
  again by its name, size and time (nothing is read). A renamed document takes over the PDF text of the entry of
  the same file (same size and time), only the `.xopp` is read;
- a document that is gone: its entry is removed;
- an older index format: everything is read once.

Unsaved changes of open documents are not in the index (it reads the files). A document saved in the app hands its
entry over (made from the document in memory and the PDF text its search knows), so the index does not read the
saved `.xopp` again.

The pages with hits of the extended search are drawn on demand (`HitPages.*`): the last 12 documents used stay
loaded, drawn pages stay in memory (up to 128 MB) without marks, and the marks are painted into the page image.
Measured on a 300-page text PDF: 3–13 ms per page (13 ms with ~700 marks of a one-letter search), 1–2 ms when only
the search changed.

Code: `qt/src/shell/Library.*` (library, search index), `Previews.*`, `LibraryModel.*`, `RecentFiles.*`.

## Search in an open document
The search of an open document (Ctrl+F, and the tab overview's search over all open documents) works on a text index
of the document (`qt/src/session/DocumentTextIndex.*`), kept while the document is open:
- the PDF text per PDF page, simplified like the library index's. On opening it is taken from the library index when
  that read this PDF as it is now (same size and time): the search has all counts at once. Otherwise it is read once
  in the background (2 s after opening, or at the first search), from the current page outwards, only while the
  canvas has no page in view to render, with a poppler instance of its own (the canvas never waits for it);
- per page the text its text elements show (visible layers; Markdown boxes as drawn), from the document in memory:
  unsaved edits, undo and redo are searched; a changed page is read again once the edits pause, pages that come, go
  or move take their text along.

A search has two steps (`DocumentSearch.*`): a string scan over the index counts the hits of every page (about 7 ms
for the 1,300 pages of the pgf manual, on each key typed) - the count, "n pages with hits", the marks in the sidebar
and the page grid and the counts in the tab overview come from it at once. Where the hits are drawn is computed
only for the pages shown (canvas, thumbnails in view, the first pages of the overview) and the page of the current
hit, from the text and the box of each character that poppler gives for the page (read on demand before any other
work; the last 48 pages are kept). Both steps match the same text with the same matcher (`TextMatch.*`:
case-insensitive, whitespace runs as one space - so a phrase across a line break is found -, ligatures as their
letters, a word broken at a line end with a hyphen found whole), so the count and the marks agree.

Measured on the pgf manual (1,321 pages, 2026-09-24): before, poppler searched every page on the UI thread for every
query, 3.3–3.8 s per query and up to 57 ms per event loop pass; now the counts of a document in a library are there
in about 10 ms, a document read for the first time has all counts after about 6 s in the background (the first hits
after 0.1 s) with at most 0.3 ms per event loop pass, and each key typed costs about 7 ms. The index takes 6.6 MB
for the manual's text, the kept character boxes about 4 MB, the worker's poppler instance about 8 MB.

## Home screen
- It is the first tab (library icon and name). It is shown when no document is open, and closing the last tab
  returns to it. Ctrl+Shift+L toggles it. Ctrl+Tab goes back to the document.
- **Library**:
  - views: folders with breadcrumbs, or all documents at once; sort by name or last modified
  - search: folders whose name matches (tap one to open it), then documents whose name or text matches
  - extended search (the pages button next to the search field): each result also shows its pages with hits,
    marked, in a row under the title (swipe or scroll sideways); tapping a page opens the document at that page
    with the search active. The cells are taller; − / + (also Ctrl+wheel, pinch) make them smaller or bigger, in
    both views. Texts shorter than 4 characters are searched on Enter.
  - New document: name, background, paper size, orientation. It is saved at once in the current folder.
  - Import: files, or a folder with all its subfolders (the Import button's menu); also dropping files or folders
    from the file manager. They are copied.
  - New folder
- **Recent**: the documents opened lately that still exist (the list is shared by all windows:
  `recent.json` in the config folder).
- **On a card**:
  - tap: open (a folder: enter it)
  - right click, ⋮, or press and hold: the menu (Open, Select, Rename, Copy to…, Move to…, Show in its folder /
    file manager, Remove from list, Move to trash)
  - press and hold, then move: drag onto a folder card or a breadcrumb to move it there
- **Selection**:
  - Ctrl+click toggles, Shift+click selects a range. The circle in a card's corner selects it; while something is
    selected, taps select too. Ctrl+A selects all, Esc clears the selection.
  - The bar at the top: Open, Copy to…, Move to…, (Remove from list), Trash.
  - Dragging a selected card moves the whole selection.
