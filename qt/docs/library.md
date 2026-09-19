# Library and home screen

A **library** is a plain folder of documents that a window works in, like a workspace in an editor. Each window
(process) shows one library. Two libraries mean two windows.

- `xournal-qt` opens the default library `<Documents>/Xournal_Libraries/Default` and creates it if needed.
- `xournal-qt <folder> [files…]` opens a folder as library. Any folder with PDFs and `.xopp` files works.
- The library menu (▾ next to the library name) lists the libraries in `<Documents>/Xournal_Libraries`. The one of
  this window is highlighted; choosing another one opens it in a new window, as do "New library…" and "Open a folder
  as library…". A window never shows two libraries.
- The Downloads folder is offered there too, as a quick library: all downloaded papers at once. Like every library
  it keeps its metadata (previews, search index) in its own `.xournal_library/`, so opening it again is as fast as
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

## `.xournal_library/`
Every library has one, Downloads included. It is only a cache, and it can be deleted at any time. For folders that
cannot be written, it lives in `~/.cache/xournal-qt/libraries/<hash>` instead.
- `previews/<hash>.png`: the first page, 360 px wide, rendered like the page thumbnails. The file name comes from the
  path, sizes and modification times, so a changed document gets a new preview. Previews of documents outside a
  library (recent files) go to `~/.cache/xournal-qt/previews`.
- `index/<hash>.json`: what the library search searches, per document, in two parts:
  - the text of the PDF pages the document shows, with the path of the PDF it uses (next to it, elsewhere or
    attached) and that PDF's size and modification time;
  - per page: which PDF page it shows, the text of its text elements, its shape.

  A background thread keeps it up to date, one document at a time:
  - nothing changed (the `.xopp` and the PDF it uses have their size and time): nothing is read;
  - only the `.xopp` changed (annotations, text elements, pages added or moved): the `.xopp` is read again, the PDF
    text is kept (only PDF pages not shown before are read) — e.g. 100 ms instead of 470 ms for a 300-page PDF;
  - the PDF changed (also an attached one, or one elsewhere): its text is read again;
  - renamed or moved in the app (also whole folders): the entries move along; only a pair's `.xopp`, which is written
    again with the new path of its PDF, is read again (without PDF text). Renamed by another program: the PDF text
    is taken over from the entry of the same file (same size and time);
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
