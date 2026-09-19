# Library and home screen

A **library** is a plain folder of documents that a window works in, like a workspace in an editor. Each window
(process) shows one library. Two libraries mean two windows.

- `xournal-qt` opens the default library `<Documents>/Xournal_Libraries/Default` and creates it if needed.
- `xournal-qt <folder> [files…]` opens a folder as library. Any folder with PDFs and `.xopp` files works.
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
This folder is only a cache, and it can be deleted at any time. For folders that cannot be written, it lives in
`~/.cache/xournal-qt/libraries/<hash>`.
- `previews/<hash>.png`: the first page, 360 px wide, rendered like the page thumbnails. The file name comes from the
  path, sizes and modification times, so a changed document gets a new preview. Previews of documents outside a
  library (recent files) go to `~/.cache/xournal-qt/previews`.
- `index/<hash>.json`: the text of every page (PDF text and text elements) for the library search. A background
  thread builds it, one document at a time, and rebuilds it when a document changes.

Code: `qt/src/shell/Library.*` (library, search index), `Previews.*`, `LibraryModel.*`, `RecentFiles.*`.

## Home screen
- It is the first tab (library icon and name). It is shown when no document is open, and closing the last tab
  returns to it. Ctrl+Shift+L toggles it. Ctrl+Tab goes back to the document.
- **Library**:
  - views: folders with breadcrumbs, or all documents at once; sort by name or last modified
  - search: folders whose name matches (tap one to open it), then documents whose name or text matches
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
