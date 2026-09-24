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

1. **Bugs and polish:** done (`qt/render-visible`, `qt/ui-polish`, `qt/markdown-fixes`; see ROADMAP).
1b. **PDF writing with qpdf, high priority (the author, 2026-09-24):**
   1. ~~`qt/pdf-pages`~~: merged 2026-09-24 (see ROADMAP). Follow-ups: the merge runs on the UI thread (about 0.2 s
      per paste on a 117 MB scan); the `.next.pdf` step relies on Linux rename semantics (check before Windows).
   2. ~~`qt/hybrid-pdf`~~: merged 2026-09-24 (see ROADMAP). Follow-ups:
      - [~] Save in the background: a 1,300-page hybrid save blocks the window for about 6 s. `qt/background-save`,
        started 2026-09-24, also covering the paste merge (0.2 s per paste on a 117 MB scan).
      - [ ] The round trip in other viewers (the author): Acrobat, Preview, Xodo, Drawboard, Chrome/pdf.js,
        Firefox, Okular, Evince, with the sample `~/xournal_qt_workspace/samples/hybrid-sample.pdf`. See the
        device checklist.
      - [ ] Not handled yet: a page deleted in another app; encrypted, rotated or cropped source PDFs (in code,
        untested); audio attachments; a "has notes" badge. Writing into the PDF itself renames over the file
        while it is read, which may fail on Windows. Design agreed: [qt/docs/hybrid-pdf.md](qt/docs/hybrid-pdf.md).
   - Experiment `qt/mupdf`, done 2026-09-24: branch `qt/mupdf` (not merged), findings in
     `qt/docs/pdf-engine-experiment.md` on that branch.
     - The MuPDF backend works in the app behind `-DXQT_WITH_MUPDF=ON` + `XQT_PDF_BACKEND=mupdf`.
     - Faster than poppler at 1x (2x on text, 4.5x on scans), scales with threads (4.3x poppler on text, 13x on
       scans with 4 threads), text 2–3x faster. Not faster on text at 4x, slower on scans at 4x, about 2x the
       memory.
     - pdfium links easily and has the features, but it has one global lock and no gain from threads.
     - Found on the way and fixed in master-qt: PDF pages were rendered at 4x the pixels on 2x screens
       (`qt/pdf-hidpi`).
     - [?] **Engine decision (the author):** MuPDF (AGPL) or stay on poppler? Proposal: retest with MuPDF 1.26
       (the roadmap's vendored version; 1.19 is from 2021) after the HiDPI fix, then decide.
     - [ ] `PdfCache` holds its lock for a whole render, so the visible pages of one view are drawn one after
       another, whatever the engine. Worth fixing before comparing engines in the app.
2. **Library track**, in this order, because each step builds on the one before:
   1. ~~the per-folder index format~~ `qt/library-index`: merged 2026-09-24 (see ROADMAP). Follow-ups:
      - [?] **Previews: in the folders or always in the app cache?** A pack is rewritten whole, so a new preview
        rewrites its folder's `previews.pack`: about 0.6 MB uploaded per changed document on the Uni library. The
        index is expensive to rebuild (PDF text) and worth syncing; a preview is one page render.
        *Proposal:* keep the index in the folders and put previews always in the app cache.
      - [ ] Reading positions are keyed by the library's path, so a library folder renamed or moved outside the
        app starts without them. Match them by file name, size and time like the index, or keep a copy in the
        root's dot folder that the clean-up leaves alone.
   2. ~~`qt/document-search`~~: merged 2026-09-24 (see ROADMAP);
   3. ~~`.md` files and images in the library~~: `qt/library-files`, merged 2026-09-24 (see ROADMAP). Follow-ups:
      - [ ] Page operations on a read-only `.md` still work, and saving them makes a `.xopp` under the default name.
      - [ ] A `.md` changed by another program is only picked up at the next library refresh.
      - [ ] Snippet cards are small by default (9–11 px text); −/+ zooms them.
   4. the "Show" file type filter and the handling of other files: `qt/library-filter`, started 2026-09-24;
   5. fuzzy search behind its toggle.
3. **Android:** `qt/android-apk`. Its build changes touch CMake for everyone, so start it when few other
   branches are open.
4. **The `.md` editor:** `.md` documents, images in `<name>.assets/`, math, vaults. It is the biggest design
   task and builds on the library already listing and indexing `.md` files.

Blocks for tracks 2–4 get their `qt/...` names when they are planned. Research for each happens right before it
is built.

## Ready: bug and polish blocks

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

- [x] **Live text index for open documents** (`qt/document-search`, merged 2026-09-24). Follow-ups:
  - [ ] A PDF with a password: the index worker cannot open it, so its PDF text is not searched in the open
    document (before, it was). Fall back to the document's own instance.
  - [ ] The library's "pages with hits" (`HitPages`) still use poppler's `findOnPage`. Library element text still
    includes Markdown source and hidden layers. Move both to `TextMatch` and drawn text.
  - [ ] Every open tab reads its PDF text 2 s after opening, even if it is never searched. Consider starting on
    the first search only for documents outside a library.
  - [ ] The tab overview places hits only on the first 24 pages with hits of each document.
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

### Bugs
- [~] **A PDF page pasted into a document that has a PDF stays blank on the canvas** (only the previews show it).
  Pasting into a `.xopp` without a PDF works. Given to `qt/background-save`, which reworks that paste path; to fix
  first, with a test. Likely cause: the page is drawn against the old PDF before the merged PDF is loaded, and it is
  not drawn again afterwards.
- [ ] **A touch on the "pages with hits" filter can make the maximized window half as high** (old, flaky, probably
  touch only, KWin). Also seen with the page grid button in the page / zoom pill. Does not reproduce off-screen.
  Both taps change what is under the finger (an overlay opens, or the list is filtered); suspect a touch whose item
  disappears or moves mid-touch, with the rest of the touch taken by KWin as a window gesture. Next time: run with
  `XQT_LOG_WINDOW=1` and look for a touch cancel, or an odd touch end, just before the resize.

### Flaky tests
- [ ] `MainWindowTest.theSelectedPdfTextTakesItsHandlesAndActionsAlong` failed once in the full suite under
  `-j6` load (2026-09-24), at the check after "the way back brings it into view again". It passed 3 of 3 alone.
  The wait for the scroll back is probably too short under load.

### Platform research
Done 2026-09-24: [qt/docs/platform-research.md](qt/docs/platform-research.md) covers native libraries and PDF
engines, with a recommendation and cheap experiments to decide.
- [x] **Pasted PDF pages stay searchable** (`qt/pdf-pages`, merged 2026-09-24).
- [?] **Experiments before the engine decision:**
  - a render benchmark of poppler, MuPDF and pdfium (running: `qt/mupdf`);
  - a `/Ink` + `/AP` round trip through Acrobat, Xodo, Drawboard and Preview;
  - whether an embedded `.xopp` survives saves in other apps;
  - pen latency on the Surface and the iPad.
