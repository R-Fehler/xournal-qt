# TODO

Tasks that agent sessions pick up. The goals behind them are in [VISION.md](VISION.md). What is done and measured
is in [qt/docs/ROADMAP.md](qt/docs/ROADMAP.md), which also has an older backlog at the end.

**How to use this file:**
- Each **block** is one branch `qt/<block>` in its own worktree `../xournal_qt-<block>`. It is merged into
  `master-qt` by the integrating session.
- Within a block, each item is one commit.
- Markers: `[ ]` open · `[~]` in progress (write the branch next to it) · `[x]` done (remove it once merged and
  recorded in ROADMAP) · `[?]` needs a decision from the author first.
- Tests: build and run only the labels named for the block (see AGENTS.md). The full suite runs at integration.

---

## Order of work (2026-09-23)

`qt/render-visible` and `qt/ui-polish` are merged (2026-09-24, see ROADMAP).

1. **Bugs and polish:** `qt/markdown-fixes`, started 2026-09-24 (`qt/render-visible` and `qt/ui-polish` are done).
2. **Library track**, in this order, because each step builds on the one before:
   1. the per-folder index format (one dot folder per folder, split packs, reading positions out of the cache);
   2. `.md` files and images in the library and its index, with snippet cards in the extended search;
   3. the "Show" file type filter and the handling of other files;
   4. fuzzy search behind its toggle.
3. **Android:** `qt/android-apk`. Its build changes touch CMake for everyone, so start it when few other
   branches are open.
4. **The `.md` editor:** `.md` documents, images in `<name>.assets/`, math, vaults. It is the biggest design
   task and builds on the library already listing and indexing `.md` files.

Blocks for tracks 2–4 get their `qt/...` names when they are planned. Research for each happens right before it
is built.

## Ready: bug and polish blocks

### `qt/markdown-fixes`
Area: `qt/src/markdown`, the Markdown editor in `qt/src/canvas`, `DocumentSearch`. Tests: `-L markdown`.
- [~] **A code block at the end of the text misbehaves in live rendering.** For example
  `` ```py\n code\n #stuff ``` `` as the last thing in a box. The grey block background keeps being drawn, and
  text typed after Enter stays invisible until more text is typed or the source sidebar is toggled once.
  Probably the incremental re-layout does not handle an open or just-closed fence at the end.
- [~] **Search boxes sit at the wrong place in rendered Markdown.** They are where the plain text would be in
  a normal Xournal text box, not on the rendered words. The search must map hits through the Markdown
  layout.

---

## Ready after a short plan: platform

### `qt/android-apk`: first APK
Research is already done in `../cross-platform-qt-research/` (03-android-plan, 05-qfield-reference,
06-risks). Keep the current PDF engine (poppler/cairo).
- [ ] vcpkg manifest and toolchain-agnostic dependency lookup, following QField (`../QField`).
- [ ] Android build: CMake preset, `AndroidManifest`, an unsigned debug APK that can be installed with `adb install`.
- [ ] A CI job modelled on QField's, running only when asked (manual dispatch).
- [ ] Start `qt/docs/android-roadmap.md` for everything about Android UI/UX. That work waits until mobile
  testing is a real concern.

---

## To decide (elaborate before building)

- [?] **Recent libraries, and libraries anywhere.** Keep a list of recently opened library folders on the home
  screen. "Open a folder as library…" already exists, so this is small.
  *Proposal:* yes. Also add "New library…" with a free location instead of only under `Xournal_Libraries`.
- [?] **A library index per folder.** Each folder gets its own dot folder with the index and cache of its own
  files only. The app merges them when a parent folder is opened. Opening a subfolder as a library is then
  instant, and moving a folder carries its index along.
  - A setting keeps the index in the folders or in the app cache (the cache is already used for read-only
    folders).
  - A clean-up tab removes all dot folders. It warns first, then the app quits so it does not rebuild them.
  - *Measured 2026-09-23* on the biggest library, `~/OneDrive/000_GoodNotes/00_Uni` (read only):
    - 3,323 documents in 292 folders.
    - `.xournal_library/` is 165 MB in **4,459 files, all at the top level**, and OneDrive syncs every one of them:
      - `index/`: 3,323 JSON files, one per document, 100 MB. 2,196 are under 10 kB, 14 are over 1 MB, and the
        largest is 9.1 MB. The PDF text is stored as plain JSON strings.
      - `previews/`: 1,135 PNG files, 65 MB.
      - `pages.json`: the last page read in each document. This is **user state, not cache**, so deleting the
        folder loses it.
    - Entries are named by a hash of the path, so a folder moved outside the app loses its entries. The PDF text
      is taken over only when an old entry with the same size and time still exists.
  - *Proposal from these numbers:*
    - **Decided (2026-09-23): one hidden dot folder per folder** (`.xournal_library/`), holding the cache of only
      the documents directly in that folder, never those of subfolders. It is a folder rather than a file because
      later features may need more hidden files. Folders without documents get none.
    - **A few packs per dot folder**, not one file per document. Entries are keyed by file name and compressed,
      so the PDF text shrinks to roughly a quarter. The packs are split by how often they change:
      - `pdf-text`: PDF text, keyed by the PDF's size and time. This is the big part and changes only when a PDF
        does.
      - `notes`: per page, the text elements, Markdown and shape of each `.xopp`. It is small and changes on
        every save.
      - `previews`: the first-page images.
      - An entry over about 1 MB (a huge PDF's text) gets a file of its own in the dot folder, so it is not
        rewritten along with its neighbours.
    - **Writes:** a changed pack is rewritten whole, atomically (`QSaveFile`), by the background indexer,
      debounced (a few seconds after the last change, and on close).
      - Partial in-place writes and SQLite are out for synced folders: sync clients upload whole files anyway,
        and torn files or conflict copies of a journal are worse than a small rewrite.
      - SQLite would be fine for the app-cache mode, if that ever pays off.
    - 292 folders then mean about 600 files instead of 4,459, which cloud sync handles far better. A folder moved
      by any program keeps its index.
    - Opening a parent folder reads and merges the packs of its subfolders, and a subfolder opens as a library
      instantly.
    - A setting per library chooses where the cache lives: in the folders (the default) or in the app cache,
      which is recommended for synced folders. In the app-cache mode, files moved outside the app are matched
      again by name, size and time.
    - Move reading positions (`pages.json`) out of the cache, into the config folder or a small file of its own
      that the clean-up keeps. Otherwise "remove all dot folders" loses where you were in every document.
- [ ] **Fuzzy search with logical operators**, modelled on fzf; clone `junegunn/fzf` as a reference.
  - **Decided (2026-09-23): opt in, behind a toggle button** in the search bar. Without the toggle, search works
    as it does today.
  - *Proposal for the syntax when the toggle is on:* fzf's extended syntax:
    - a space means AND; `|` means OR; `!term` means NOT;
    - `'exact`, `^prefix` and `suffix$` narrow a term;
    - parentheses group terms.

    XOR is left out; it is rarely useful for documents.

### Markdown
- **Decided (2026-09-23): the `.md` engine is native**, not a web view: md4c with our layout and
  pagination, or QTextDocument where it fits.
  - Why:
    - it works the same on Android and iOS; QtWebEngine does not exist there;
    - pagination and PDF export reuse what exists;
    - ink and search boxes share one coordinate system.
  - **Don't reinvent the wheel.** Take UI/UX patterns from good editors: live preview, how to show and hide
    Markdown syntax, keyboard handling, outline, link completion.
  - Before building, clone them as references: Zettlr and MarkText (TypeScript); Ghostwriter, QOwnNotes, VNote
    and ReText (Qt); foam for wikilinks.
- **Decided (2026-09-23): images in `.md` go in the sidecar folder `<name>.assets/`** (Typora's convention).
  Inside `.xopp`, images live in the file bundle.
- [?] **Math:** MicroTeX, in Markdown boxes, the `.md` editor and the full-page Markdown mode.
- [?] **Mermaid:** optional; it needs a JavaScript engine or a renderer written from scratch. Until then, show
  the source as a code block.
- [?] **`.md` files in the library and its search index.** Today the library only knows `.xopp`, `.xoj` and `.pdf`
  (`qt/src/shell/DocumentFiles.cpp`).
  - `.md` files get entries in the folder's `notes` pack. The text is read through md4c, so the syntax is not
    indexed. Headings are kept for snippets and for jumping to a hit, and links and wikilinks are kept for
    backlinks later.
  - Indexing is cheap: the file is plain text, so there is no PDF step.
  - A hit points to a heading or line, since `.md` has no pages until the paginated view exists.
  - **Decided (2026-09-23): the extended search shows `.md` hits as rendered snippet cards, not page images.**
    - Each card is the block that holds the hit (paragraph, list item, table row or code block), drawn by our
      Markdown renderer.
    - Above it is its heading path, for example *Lecture 3 › Kalman filter › Prediction*, taken from the headings
      in the index.
    - Hits are highlighted like the page marks, with the current hit in orange. Long blocks are cut to a few
      lines around the hit.
    - Tapping a card opens the file at that block with the document search active.
    - In the results list, a PDF or `.xopp` result keeps its row of page images, and a `.md` result gets a row
      of cards.
    - Maybe later: a "snippets / pages" switch that shows paginated page previews, once the paginated view
      exists.
    - Markdown boxes inside `.xopp` pages stay in the page images. They depend on the search box fix in
      `qt/markdown-fixes`.
  - Listing and indexing could land before the `.md` editor, with a hit opening the file read-only in the
    Markdown renderer.
  - Other text files (`.txt`, `.org`) could follow the same path later.
- [ ] **File type filter in the library** (decided 2026-09-23): a "Show" button next to the sort button opens a
  popup with toggles. The same filter applies to search results.

  | Toggle | Default | What it shows |
  | --- | --- | --- |
  | Notes (`.xopp`, `.xoj`) | on | |
  | PDFs | on | Sub-toggle "only PDFs with notes": only PDFs that have an `.xopp` next to them |
  | Markdown (`.md`) | on | Hides a vault's notes when you only want your documents |
  | Images (`.png`, `.jpg`, `.heic`, ...) | on | Photos of whiteboards and scans. Preview, and "annotate": a new `.xopp` with the image as its page background (upstream supports image backgrounds) |
  | Text and code (`.txt`, `.tex`, `.py`, ...) | off | Plain-text preview. Indexed only below a size limit |
  | All other files | off | Office files and the like: a generic icon, "Open with the system app" and "Show in file manager" |
- [ ] **What the app does with other files** (decided 2026-09-23):
  - It edits only what it renders well, which is `.md` and plain `.txt`, through the Markdown editor in plain mode.
  - Code and LaTeX get a read-only preview and "Open with…", but no editor. A code editor in a notes app keeps
    growing and never catches up with a real one.
  - "Open with the system app" is `QDesktopServices::openUrl`: `xdg-open` on Linux, `open` on macOS,
    `ShellExecute` on Windows, an intent on Android.
  - "Show in file manager" needs one call per platform: `org.freedesktop.FileManager1.ShowItems` over D-Bus,
    `explorer /select,` on Windows, `open -R` on macOS. Android has none, so the entry is hidden there.
  - Later, desktop only: "Annotate as PDF" for Office files, through `soffice --headless --convert-to pdf` when
    LibreOffice is installed. The result becomes a PDF + `.xopp` pair.
  - Lowest priority, only with MuPDF: EPUB and CBZ as documents, since MuPDF lays them out as pages.
  - A `.tex` file and its compiled `.pdf` could be paired like `.xopp` and `.pdf`: one card, with the source a
    tap away.
- [?] **Vaults** (Obsidian, Zettlr, foam): open a vault folder as a library; resolve `[[wikilinks]]` and
  Markdown links by file name; backlinks later.

### Platform research (a written comparison only; nothing to build yet)
- [?] **Platform-native libraries behind our interfaces.** Candidates:
  - PDFKit and PencilKit input (keeping our stroke model) on iOS and macOS;
  - Windows Ink on the Surface;
  - handwriting-recognition SDKs on iOS, Windows and Android.

  For each: where it would plug in (input, the PDF backend `XojPdfDocumentInterface`, a recognizer
  interface), what it gains, and the cost of keeping it up.
- [?] **PDF engines compared:** poppler+cairo (today), MuPDF (AGPL, fast, writes PDFs), pdfium (BSD, used by
  Chrome and Android, can write annotations) and PDFKit. Compare feel, speed, and writing annotations like
  Drawboard or Xodo. This also decides the backlog item "searchable text in pasted PDF pages" and the hybrid
  PDF in the vision.
