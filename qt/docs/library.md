# Library and home screen

A **library** is a plain folder of documents that a window works in, like a workspace in an editor. Each window
(process) shows one library. Two libraries mean two windows.

- `xournal-qt` opens the default library `<Documents>/Xournal_Libraries/Default` and creates it if needed.
- `xournal-qt <folder> [files…]` opens a folder as library. Any folder with PDFs, `.xopp`, Markdown files or images
  works.
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
- A **hybrid PDF** (`qt/docs/hybrid-pdf.md`) is one document, a PDF that carries its `.xopp`. Next to its `.xopp`
  export for Xournal++ (`name.xopp` with a hidden `.name.pages.pdf`) the pair is one card that opens the hybrid PDF.
  Its search text is its pages' text plus its text elements.
- **Markdown files** (`.md`) are documents: a card with a preview of their start, opened read-only for now (below).
- **Images** (`.png`, `.jpg` / `.jpeg`, `.webp`, and `.heic` / `.heif` where Qt can read them) are documents: a card
  with a thumbnail. `name.xopp` next to `name.jpg` is one document, like a PDF and its `.xopp`: it opens as the `.xopp`
  (the image is the background of its page). A `.xopp` next to a PDF of its name belongs to the PDF; of several
  images of one name, the first of `.png`, `.jpg`, `.jpeg`, `.webp`, `.heic`, `.heif` pairs (the extension written in
  lower or upper case).
- Cards show what a document is: "PDF", "MD", "IMG" ("✎": with its `.xopp`). The library model has a kind per row
  (`notes`, `pdf`, `md`, `image`) for a filter by kind; a hybrid PDF is `pdf`, with the row's `hybrid` flag set when
  it is known from the listing (next to its `.xopp` export; a lone hybrid PDF is not looked into when listing).
- **Text and code files** (`.txt`, `.tex`, `.py`, `.cpp`, `.h`, `.json`, `.csv`, `.org`, `.rst`, `.yaml`, `.toml`,
  `.sh`, and many more, also `Makefile`, `README`, … without an extension) and **all other files** (Office files and
  the rest) are items too, each known by its whole file name (`report.docx`), when the library's "Show" filter shows
  them (below; off by default). A `.tex` next to its `.pdf` is two cards.
- These files are never shown:
  - `name.xopp.bg.pdf`, an attached PDF. It belongs to its `.xopp` and travels with it.
  - `.name.pages.pdf`, the merged PDF of a `.xopp` with PDF pages pasted from other PDFs (below). It belongs to its
    `.xopp` and travels with it.
  - `name.xopp.bg_1.png`, …: page background images stored with a `.xopp` (upstream's attached images). They belong
    to their `.xopp` and travel with it (rename, move, copy, trash).
  - hidden files: autosaves, `.xournal_library`
  - backups (`~`)
  - `Thumbs.db`, `desktop.ini`
- **Rename / move** rename or move both files. The `.xopp` is loaded and written again with upstream's LoadHandler and
  SaveHandler, so its PDF reference (relative to the `.xopp`) points to the new place; the same for the image a
  `.xopp` annotates. Open tabs and the recent list follow the new paths.
- Text and other files are renamed by their whole file name, moved, copied and trashed like documents; a name that is
  taken becomes "name (2).ext".
- **Import / copy** copy a `.xopp` together with the PDF it uses. That PDF is stored as `name.pdf` next to the copy,
  even if it came from somewhere else. A PDF brings the `.xopp` next to it along, as does an image. A folder is
  copied with its whole folder structure (also empty subfolders) and all documents in it (also Markdown files and
  images). Text and other files come along only when the library shows them; hidden folders (`.git`, …) stay behind. A name that is taken becomes "name (2)": taken by
  any document (`.xopp`, PDF, `.md`, image), so a moved `.xopp` never pairs with an image or PDF that was there.
- **Trash** moves the files (or the folder) to the desktop trash.

### Markdown files and images, opened
- A **Markdown file** opens read-only for now (the `.md` editor comes later and replaces this): a new document of
  plain A4 pages with the file's text as the page's Markdown text flowing over them (`qt/src/canvas/MarkdownFile.*`,
  drawn by our Markdown renderer), titled with the file name. A note at the bottom left of the page view says it is
  read-only (× closes it for this tab). Nothing writes on it: pen, highlighter, eraser, text and select tools
  scroll like the hand (a tap still follows a link), Markdown editing and pasting do nothing (page operations of
  the sidebar still work; they would be saved as a `.xopp`). The document is never written back to the `.md`. Of a
  file over 2 MB the first 2 MB are shown (the note says so). Opening it again shows its tab.
- An **image** opens as a new document with one page that has the image as its background (upstream's image
  background), as big as the image fits into A4's long side, titled with the file name, with a note that saving
  keeps it next to the image; nothing is written until it is saved. "Save" suggests `photo.xopp` next to `photo.jpg`, and the library then shows the two as one card (above),
  which opens the `.xopp`. A PNG or JPEG that needs no turning is used by its path, as upstream refers to background
  images (the `.xopp` stays small); any other image (WebP, HEIC, a photo turned upright by its orientation tag, which
  upstream would show sideways) is stored with the `.xopp` as a PNG of at most 4096 px (`photo.xopp.bg_1.png`,
  upstream's attached image). Code: `qt/src/canvas/ImageFile.*`.
- A **text or code file** opens read-only the same way, as plain text: its text is one fenced code block of the page's
  Markdown text (monospaced, highlighted by its extension where KSyntaxHighlighting knows it; `.txt` and the like
  plain; the fence is longer than any run of backticks in the file, so nothing in it ends the block), flowing over A4
  pages, titled with the file name, with the read-only note. There is no editor for them ("Open with the system app"
  in the card's menu edits them elsewhere). Its card is the first page as it opens (the first lines). An opened text
  file is in the Recent grid like a document. Code: `MarkdownFile::readAsPlainText`.
- A library search hit in a Markdown file opens it with the search active: at the page of the passage with the hit
  (a snippet card, below: its first hit there is the current one).

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
- `notes.pack`: per document (by file name): its kind (`xopp`, `pdf`, `md`, `image`), name, the size and time of
  its `.xopp` (a Markdown file, an image alone: of that file), the PDF it uses (relative to the folder when it is in
  the library; next to it: its name) with that PDF's size and time, and per page which PDF page it shows, the text
  of its text elements and its shape. Small; written again when a `.xopp` in the folder is saved.
  - A Markdown file has no pages: its entry has the text of its passages (headings, paragraphs, list items, table
    rows, code blocks), read through md4c without the Markdown syntax, which of them are headings (a hit shows the
    headings above it), and the targets of its links and `[[wiki links]]` (for backlinks later). Reading it is cheap
    (plain text, no PDF step); of a file over 2 MB only the start is read (cut at a line end), as it opens.
  - An image has no text: its entry has its name only.
  - A text or code file (kind `text`, only while the library shows text files) has its text, simplified like the
    rest, if the file is at most 1 MB; a bigger one has its name only. A hit shows the text around it on the card
    (no snippet cards). Other files are not in the index: the search finds them by name when they are shown.
- `pdf-text.pack`: per document, the text of the PDF pages it shows, tied to the PDF's size and time. Big; written
  only when a PDF changed or a document came or went. A document with over 1 MB of PDF text gets a file of its own,
  `pdf-text-<hash>.pack`, written only when that text changes.
- `previews.pack`: the first-page previews (PNG, 360 px wide, drawn like the page thumbnails, of the title page; a
  Markdown file: its first page as it opens, an image: the image scaled down and turned upright by its orientation tag),
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

The snippet cards of Markdown files are drawn on demand as well (`MdSnippets.*`, image provider
`image://mdsnippet`): the file is read and parsed as the index reads it (the last 8 files stay parsed), the passage
alone is laid out (`md::snippet`, `qt/src/markdown/MdPassages.*`), and the hits are found in its text with the
index's matcher and marked where their source is drawn (`md::sourceRects`), so the card and the count agree.

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

## Fuzzy search
"Fuzzy" in the library's search field (and in the tab overview's, below) turns on fzf's extended search syntax
(modelled on [fzf](https://github.com/junegunn/fzf#search-syntax); its matching is ported from fzf, MIT). Off by
default; an app-wide setting (`fuzzySearch` in the `xournalQt` part of the settings file), shared by all windows. Off,
the search is exactly the plain one. The button's tooltip is the short help.

| Typed | Finds |
| --- | --- |
| `kalman` | names (and folder paths) with these letters in this order, best first; text containing `kalman` |
| `kalman filter` | both (a space is AND) |
| `kalman \| lqr` | either; `\|` binds closer than the space: `a b \| c` is a and (b or c), as in fzf |
| `!draft` | without it: not in the name, the folder path or the text (an exact substring; `!'dft`: fuzzy on names) |
| `(a b) \| c`, `!(a b)` | a group (parentheses are an addition to fzf's syntax) |
| `'lect` | exactly `lect`, also in names |
| `'lecture'` | the whole word |
| `^lec`, `ure$` | the name starts / ends with it; in text: a word starts / ends with it |
| `^lecture\ 3$` | the name is it (`\ ` is a space inside a term; `\(` `\)` parentheses) |

Case never matters. A term left empty by its marks (`^`, `!`) is ignored, as fzf ignores it. An expression that is
not valid (a `(` not closed, a `)` not opened, a `|` without a term on both sides, empty `()`) is never an error: a
short red hint next to the field says why, and the text is searched as plain text.

**What is matched where.** A document's **name** and its **folder path** in the library are matched with fzf's
algorithm (FuzzyMatchV2 and its exact, boundary, prefix, suffix and equal matches) and scored: the name first; a term
not in the name is looked for in "folder/name" (so `uni lect` finds `Uni/Lecture 3`) and scores a quarter. Its
**text** (PDF text, text elements, Markdown passages, text files) is searched per term as a substring through the
same matcher as the plain search (`TextMatch`: case, whitespace, ligatures, hyphenation), at word bounds for `^`, `$`
and `'word'` (a word broken at a line end is one word). A fuzzy subsequence over megabytes of text would match
nearly anything, so a plain term is a substring there. A term **holds for a document** when its name/path or its text
has it, and the document is found when the expression holds with these values: `kalman filter` finds a lecture with
"Kalman" on page 3 and "filter" on page 7, and `lecture !draft` drops "Lecture 3 draft" and every document with
"draft" in it. Folders are found by their names and paths, other files (not indexed) by their names only.

**Pages with hits** (the extended search): the pages with hits of the terms that are not negated on which the
expression holds, a term counting as found on a page when that page, the name or the folder path has it. So
`kalman filter` lists the pages that have both words, and `lecture kalman` (with "Lecture" in the name) every page
with "kalman". When the expression holds on no single page (the words are on different pages), all pages with hits
are listed, so a document found never shows an empty row. A Markdown file's passages are its pages here. The count
on a card is all hits of the terms that are not negated (overlapping hits of two terms count once, as they are
marked).

**Order**: fzf's score of the name and folder path first (documents found only by their text have none), then the
hits in the text, then the newest. The matched letters of a name are highlighted on its card, as fzf shows them.

Opening a hit (a card, a page, a snippet card) searches the document with the same query and syntax; refined in the
document's search bar it stays a fuzzy search until the bar is cleared. The pictures of the pages mark every term
that is not negated (poppler's search, so `^`, `$` and `'word'` are marked as substrings there).

**The tab overview** ("search all documents", Ctrl+Shift+F) has the same button and setting: a document (its
title, and the text of its search index) is marked when the expression holds, its title's matched letters are
highlighted, a card found only by its title says "In the name", one whose hits the expression excludes "No match",
and its row of pages (the extended view) lists the pages on which the expression holds, as in the library. "Names"
there with the toggle on: the expression over the titles alone.

The query is parsed once per search (`qt/src/session/FuzzyQuery.*`, fzf's port in `FuzzyMatch.*`), each text is
scanned once per term. Measured on a generated library of 3,000 Markdown files in 320 folders (~12 MB of text, six
files of ~1.7 MB) on the development machine, best of three, in several runs while other builds kept it busy (load
5-7), so as ranges: the index search takes 18-55 ms for a plain word, 24-74 ms for the same word fuzzy, 44-133 ms
for `kalman filter` and 77-215 ms for `(kalman | robust) !draft ^lin`; the library model adds 10-90 ms around it (it
lists the folders again, as the plain search does). Typing waits 300 ms before it searches, as before.
`XQT_BENCH_FUZZY=3000 xqt-shell-tests --gtest_filter='LibraryFuzzyTest.bench*'` repeats the measurement.

## Home screen
- It is the first tab (library icon and name). It is shown when no document is open, and closing the last tab
  returns to it. Ctrl+Shift+L toggles it. Ctrl+Tab goes back to the document.
- **Library**:
  - views: folders with breadcrumbs, or all documents at once; sort by name or last modified
  - **Show** (the button next to the sort button): which kinds of files the library shows. A setting of each library,
    kept in its `library.json` (`"show"`). It applies to the grid, the flat list, the counts on folder cards and the
    search results (the Recent grid is not filtered: it lists what was opened, from any library).

    | Toggle | Default |
    | --- | --- |
    | Notes (`.xopp`, `.xoj` alone) | on |
    | PDFs (alone or with their `.xopp`), and "only PDFs with notes" (with a `.xopp` next to them, or hybrid PDFs) | on, off |
    | Markdown (`.md`) | on |
    | Images (alone or with their `.xopp`) | on |
    | Text and code | off |
    | All other files | off |

    A PDF with its `.xopp` counts as a PDF, an image with its `.xopp` as an image. For "only PDFs with notes" a PDF
    alone is looked into once per version (whether it is a hybrid PDF). Turning text files on or off brings them into
    the search index or takes them out.
  - search: folders whose name matches (tap one to open it), then documents whose name or text matches; "Fuzzy"
    in the field: fzf's syntax, names ranked (see Fuzzy search above)
  - extended search (the pages button next to the search field): each result also shows its pages with hits,
    marked, in a row under the title (swipe or scroll sideways); tapping a page opens the document at that page
    with the search active. A Markdown file shows a row of **snippet cards** instead: per passage with hits (a
    paragraph, a list item, a table row under its header, a code block) the passage drawn by our Markdown renderer,
    the headings above it on top (*Lecture 3 › Kalman filter › Prediction*), the hits marked like on the pages (the
    first one, which opening the card makes current, in orange), a long passage cut to a few lines around its
    first hit. Tapping a card opens the file at that passage with the search active. The cells are taller; − / + (also Ctrl+wheel, pinch) make them smaller or bigger, in
    both views. Texts shorter than 4 characters are searched on Enter.
  - New document: name, background, paper size, orientation. It is saved at once in the current folder.
  - Import: files, or a folder with all its subfolders (the Import button's menu); also dropping files or folders
    from the file manager. They are copied.
  - New folder
- **Recent**: the documents opened lately that still exist (the list is shared by all windows:
  `recent.json` in the config folder), and the folders opened as a library that are not in
  `<Documents>/Xournal_Libraries` ("Open a folder as library…", `xournal-qt <folder>`, the file manager's action),
  mixed with the documents by when they were opened (`"library": true` in the list). A library's card is a folder
  with a library mark, its name and path; a tap opens it in a window of its own like "Open a folder as library…" (a
  window of that library that is open already comes to the front; this window's own library: its home screen). Its
  menu has Open library, Show in file manager and Remove from this list; it is never selected, renamed, moved or
  trashed from here. A folder that is gone drops out. The "Show" filter does not apply to the Recent grid.
- **On a card**:
  - tap: open (a folder: enter it; another file (not a document or text): its app, below)
  - a folder's menu: "Open as library (new window)" opens that folder as a library of its own, in another window
    (another process, as "Open a folder as library…"). Its documents' caches are already in its folders
    (`.xournal_library/`), so nothing is indexed again. (Not so when the library keeps its cache in the app cache:
    the subfolder is a library with a key and settings of its own, starts with the cache in its folders and indexes
    its documents once.)
  - right click, ⋮, or press and hold: the menu (Open, Select, Rename, Copy to…, Move to…, Show in its folder,
    Open with the system app (text and other files), Show in file manager, Remove from list, Move to trash)
- **Other files** (Office files and the rest, shown with "All other files"): a card with an icon of their type (a
  document, spreadsheet, slides, archive, audio, video or image file, by extension and MIME type; else a plain file),
  the extension as badge, the whole file name, size and date. A tap opens it with the app the system has for it
  (`QDesktopServices::openUrl`: `xdg-open` on Linux, an intent on Android); no tab opens and it is not in Recent. A
  text file's badge is its extension too ("PY", "TEX"; "TXT" without one), with its first lines as preview.
- **Show in file manager** selects the file in the file manager: `org.freedesktop.FileManager1.ShowItems` over D-Bus
  on Linux (when no file manager answers there, or Qt has no D-Bus: the folder is opened), `explorer /select,` on
  Windows, `open -R` on macOS; a folder is opened. Not offered on Android. Everything handed to the system goes
  through `qt/src/shell/SystemApps.*`, which the tests replace with a fake.
  - press and hold, then move: drag onto a folder card or a breadcrumb to move it there
- **Selection**:
  - Ctrl+click toggles, Shift+click selects a range. The circle in a card's corner selects it; while something is
    selected, taps select too. Ctrl+A selects all, Esc clears the selection.
  - The bar at the top: Open, Copy to…, Move to…, (Remove from list), Trash.
  - Dragging a selected card moves the whole selection.
