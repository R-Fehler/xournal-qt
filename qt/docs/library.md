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

Code: `qt/src/shell/DocumentFiles.*`.

## The library cache (`.xournal_library/`)
The cache only speeds things up and can be deleted at any time. Each folder with documents has its own hidden
`.xournal_library/`, with the cache of **only the documents directly in it** (never those of its subfolders). A
folder without documents gets none, and when its last document goes, the folder goes too (unless something other
than the app's files is in it). Opening a library reads the caches of all its folders and merges them; a subfolder
opened as a library of its own finds its caches already there. Entries are keyed by file name, so a folder moved or
renamed by any program keeps its cache. Folders that cannot be written keep theirs in the app cache,
`~/.cache/xournal-qt/libraries/<key of the library>/<folder in the library>/.xournal_library/`.

A cache folder holds a few **packs**, one file each, split by how often they change:
- `notes.pack`: per document (by file name): its kind (`xopp`, `pdf`), name, the size and time of its `.xopp`, the
  PDF it uses (relative to the folder when it is in the library; next to it: its name) with that PDF's size and
  time, and per page which PDF page it shows, the text of its text elements and its shape. Small; written again
  when a `.xopp` in the folder is saved.
- `pdf-text.pack`: per document, the text of the PDF pages it shows, tied to the PDF's size and time. Big; written
  only when a PDF changed or a document came or went. A document with over 1 MB of PDF text gets a file of its own,
  `pdf-text-<hash>.pack`, written only when that text changes.

A pack is CBOR (Qt's `QCborValue`) compressed with zlib, behind a header with a format number: a pack of another
format is read anew. It is always written whole, under another name first (`QSaveFile`), never changed in place:
sync clients upload whole files anyway, and neither the app nor a sync client ever sees half a file. Changed packs
are written in the background a few seconds after the last change (at the latest 30 s after the first one), and
when the library is closed.

The first-page previews and the reading positions are still in the root's `.xournal_library/` (`previews/`,
`pages.json`), as before.

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

Unsaved changes of open documents are not in the index (it reads the files).

The pages with hits of the extended search are drawn on demand (`HitPages.*`): the last 12 documents used stay
loaded, drawn pages stay in memory (up to 128 MB) without marks, and the marks are painted into the page image.
Measured on a 300-page text PDF: 3–13 ms per page (13 ms with ~700 marks of a one-letter search), 1–2 ms when only
the search changed.

Code: `qt/src/shell/Library.*` (library, search index), `Previews.*`, `LibraryModel.*`, `RecentFiles.*`.

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
