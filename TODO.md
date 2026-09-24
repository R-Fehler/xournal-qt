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
      - [x] Save in the background (`qt/background-save`, merged 2026-09-24). Left:
        - [ ] A crash save while a paste merge is pending: the recovered pasted pages show "PDF background missing".
        - [ ] Autosave still runs on the UI thread.
        - [ ] Thumbnails of just-pasted pages stay white, and their text is not searchable, until the merge is done.
        - [ ] A save that drops unused pages still reloads the PDF, outline included, on the UI thread.
      - [ ] The round trip in other viewers (the author): Acrobat, Preview, Xodo, Drawboard, Chrome/pdf.js,
        Firefox, Okular, Evince, with the sample `~/xournal_qt_workspace/samples/hybrid-sample.pdf`. See the
        device checklist.
      - [ ] Not handled yet: a page deleted in another app; encrypted, rotated or cropped source PDFs (in code,
        untested); audio attachments; a "has notes" badge. Writing into the PDF itself renames over the file
        while it is read, which may fail on Windows.
   3. ~~`qt/hybrid-flow`~~: merged 2026-09-24 (see ROADMAP). Left: a hybrid flag in the index, since same-name `.xopp` + PDF pairs now get one qpdf check per listing. As planned:
      - **Save as with a format choice:** "Xournal notes (.xopp)" or "PDF with notes, editable (.pdf)", replacing the
        separate "Save as hybrid PDF…" entry. "Export as PDF" stays for a plain, flattened PDF.
      - **When a saved `.xopp` becomes a hybrid PDF, ask once** what happens to the old `.xopp`:
        - "Move it to the trash (the PDF now holds everything)", the default;
        - "Keep it updated for Xournal++" (export on every save, for this document);
        - "Keep it as it is" (an old copy that is not updated).

        The dialog has a "Don't ask again" box that stores the choice. Settings → Documents shows the stored
        choice and can change it or ask again.
      - **"Share…"** with two options: "PDF with notes (opens in any app)", which is the hybrid itself (saved first),
        and "For Xournal++ (.xopp + PDF)", a one-time export into a folder the user chooses, never next to the
        document, as `name.xopp` plus `name.xopp.bg.pdf` (upstream's attached-background name). On Linux it then
        shows the files in the file manager; on Android and iOS it will open the share sheet.
      - "Copy to clipboard" in Share (the author, 2026-09-24): the file as a `text/uri-list`, plus the PDF data, to
        paste into another app or chat. Design agreed: [qt/docs/hybrid-pdf.md](qt/docs/hybrid-pdf.md).
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
      - [x] Previews are rewritten only when the first page's image changes (`qt/preview-writes`, merged 2026-09-24).
      - [ ] Reading positions are keyed by the library's path, so a library folder renamed or moved outside the
        app starts without them. Match them by file name, size and time like the index, or keep a copy in the
        root's dot folder that the clean-up leaves alone.
   2. ~~`qt/document-search`~~: merged 2026-09-24 (see ROADMAP);
   3. ~~`.md` files and images in the library~~: `qt/library-files`, merged 2026-09-24 (see ROADMAP). Follow-ups:
      - [ ] Page operations on a read-only `.md` still work, and saving them makes a `.xopp` under the default name.
      - [ ] A `.md` changed by another program is only picked up at the next library refresh.
      - [ ] Snippet cards are small by default (9–11 px text); −/+ zooms them.
   4. ~~the "Show" filter and other files~~: `qt/library-filter`, merged 2026-09-24 (see ROADMAP). Follow-ups:
      - [ ] "Only PDFs with notes" checks each lone PDF for being hybrid on the UI thread (qpdf, cached per file
        version). Not measured on the Uni library; move it to the index (a hybrid flag in the entry).
      - [ ] Dropping a file of a hidden kind into the library gives the old "not a document the library shows"
        error; it should say which filter hides it.
   6. ~~`qt/fuzzy-text`~~: merged 2026-09-24 (see ROADMAP). Left: the first fuzzy search of a big open document builds its vocabularies on the UI thread (about 160 ms for 1,300 pages).
      - Today only names are fuzzy; `tbine` does not find "turbine" in a PDF.
      - A term matches a single word fuzzily (fzf's algorithm within one word, with a minimum score), through a
        word list per document and page, so it stays fast.
      - Typo tolerance, set in a new **Settings → Search** tab (the author, 2026-09-24): off, 1 letter for words of
        5+ letters (the default), or up to 2 letters for words of 8+. The tab also holds the fuzzy on/off setting.
      - A help page for the fuzzy search: a long press or right click on the Fuzzy toggle, and a link in Settings →
        Search. It explains the syntax with examples.
      - Whole matching words are marked; `'exact`, `^` and `$` keep their meaning.
   5. ~~fuzzy search~~: `qt/fuzzy-search`, merged 2026-09-24 (see ROADMAP). Follow-ups: hit-page pictures mark `^`/`$`/`'word'` terms as plain substrings; the document search bar shows no sign that it is in fuzzy mode.
3. **Android:** `qt/android-apk`. Its build changes touch CMake for everyone, so start it when few other
   branches are open.
4. **The `.md` editor:** `.md` documents, images in `<name>.assets/`, math, vaults. It is the biggest design
   task and builds on the library already listing and indexing `.md` files.

Blocks for tracks 2–4 get their `qt/...` names when they are planned. Research for each happens right before it
is built.

## Ready: bug and polish blocks

---

## Ready after a short plan: platform

### `qt/android-apk`: first APK (merged 2026-09-24; `~/xournal_qt_workspace/samples/xournal-qt-debug-arm64.apk`; see `qt/docs/android.md` and `qt/docs/android-roadmap.md`)
Tooling (2026-09-24, in the author's home, no sudo): JDK 17 in `~/.local/jdk-17`; Android command-line tools and
NDK r27c (27.2.12479018) in `~/Android/Sdk`; Qt 6.11.2 desktop (host, `gcc_64`) and `android_arm64_v8a` in `~/Qt` (2 GB) through
`aqtinstall` (`~/.local/bin/aqt`); 6.11.3 was not fully mirrored yet. Installed 2026-09-24. The block itself comes after the `.md` editor, at the author's wish.
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
- [x] **Recent libraries, and opening a subfolder as a library** (merged 2026-09-24, `qt/library-filter`).
  - Folders opened as a library outside `Xournal_Libraries` show in the Recent grid, as a folder with a library
    badge; tapping one opens that library in its own window.
  - A folder card in the library grid gets "Open as library" in its context menu, opening a new window.
- [x] **A library index per folder**: decided 2026-09-23 and merged as `qt/library-index` on 2026-09-24. Design,
  measurements and follow-ups: ROADMAP, [qt/docs/library.md](qt/docs/library.md), and "Order of work" above. The
  open question there is where previews go.
- [x] **Fuzzy search with logical operators** (`qt/fuzzy-search`, merged 2026-09-24), modelled on fzf; clone `junegunn/fzf` as a reference.
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
- **Decided (2026-09-24): math with MicroTeX**, vendored (MIT, no LaTeX install, works on mobile).
  - Syntax `$…$` inline and `$$…$$` as a block, like Obsidian, Zettlr and GitHub.
  - In Markdown boxes, the `.md` editor and the full-page mode.
  - Check first that its Qt backend is still maintained.
- **Decided (2026-09-24): Mermaid stays a code block.** The app must work as a bundle with no external tools
  (VISION), and Mermaid needs a browser engine. Revisit only if a native renderer turns up.
- [x] **`.md` files and images in the library and its search index, with snippet cards**: merged as
  `qt/library-files` on 2026-09-24 (see ROADMAP). Other text files (`.txt`, `.org`, code) follow in
  `qt/library-filter`.
- [x] **File type filter in the library** (merged 2026-09-24, `qt/library-filter`): a "Show" button next to the sort button opens a
  popup with toggles. The same filter applies to search results.

  | Toggle | Default | What it shows |
  | --- | --- | --- |
  | Notes (`.xopp`, `.xoj`) | on | |
  | PDFs | on | Sub-toggle "only PDFs with notes": only PDFs that have an `.xopp` next to them |
  | Markdown (`.md`) | on | Hides a vault's notes when you only want your documents |
  | Images (`.png`, `.jpg`, `.heic`, ...) | on | Photos of whiteboards and scans. Preview, and "annotate": a new `.xopp` with the image as its page background (upstream supports image backgrounds) |
  | Text and code (`.txt`, `.tex`, `.py`, ...) | off | Plain-text preview. Indexed only below a size limit |
  | All other files | off | Office files and the like: a generic icon, "Open with the system app" and "Show in file manager" |
- [x] **What the app does with other files** (merged 2026-09-24, `qt/library-filter`):
  - It edits only what it renders well, which is `.md` and plain `.txt`, through the Markdown editor in plain mode.
  - Code and LaTeX get a read-only preview and "Open with…", but no editor. A code editor in a notes app keeps
    growing and never catches up with a real one.
  - "Open with the system app" is `QDesktopServices::openUrl`: `xdg-open` on Linux, `open` on macOS,
    `ShellExecute` on Windows, an intent on Android.
  - "Show in file manager" needs one call per platform: `org.freedesktop.FileManager1.ShowItems` over D-Bus,
    `explorer /select,` on Windows, `open -R` on macOS. Android has none, so the entry is hidden there.
  - Lowest priority, only with MuPDF: EPUB and CBZ as documents, since MuPDF lays them out as pages.
  - A `.tex` file and its compiled `.pdf` could be paired like `.xopp` and `.pdf`: one card, with the source a
    tap away.
- [ ] **Vaults** (Obsidian, Zettlr, foam). Decided 2026-09-24:
  - **Detect a vault** when a `.md` is opened: a `.obsidian/` folder next to it or in a parent folder up to the
    library root. Tell the user once per vault that it is an Obsidian vault and that Markdown attachments are
    stored in and loaded from the vault's configured attachment folder (`.obsidian/app.json`), not `<name>.assets/`.
  - **Editing:** plain `.md` files are editable. A file that uses Obsidian-only syntax asks once, with an OK
    button, before it can be edited. That syntax: wikilinks and embeds `[[…]]` / `![[…]]`, block references
    `^id`, callouts `> [!note]`, comments `%%…%%`, highlights `==…==`, `dataview` blocks, Obsidian front-matter
    keys.
    - Feasible: md4c plus a scan for these patterns is cheap.
    - The editor changes only the source of the edited blocks, so untouched text stays byte-identical. The risk
      is mostly how such content is shown, not that it is rewritten.
  - Resolve `[[wikilinks]]` and Markdown links by file name; backlinks later.

### The `.md` editor (decided 2026-09-24; `qt/md-editor`, merged 2026-09-24)
- **Plain `.txt` editing** like a notepad or a simple mobile editor: no syntax highlighting, just text.
- **Other text files** (code, LaTeX, …) are editable as plain text only after a warning is accepted; read-only
  otherwise.
- **"Open externally"**, easy to reach, for every file that is not `.xopp`/`.pdf` (open it in a code editor and so on).
- **Pages by default**, the native feel, with a toggle for a continuous page (infinite canvas). Pagination exists
  from the Markdown boxes.
- **Ink on Markdown: an "Edit as notes" button** turns the `.md` into a `.xopp`-like document: its text as a
  Markdown box flowing over pages. Editing and inking continue there, in a new tab, with the `.md` left as it was.
  Plain `.md` files are edited as text; ink is never stored in a `.md`.
- Build on the live-rendering editor of the Markdown boxes, and on UI patterns from the reference editors.
- **"New Markdown file"** in the library's New menu, next to "New document", once the editor is shipped
  (the author, 2026-09-24). It creates `name.md` in the current folder and opens it in the editor.

### Horizontal scrolling and a presentation mode (the author, 2026-09-24; `qt/present`, merged 2026-09-24)
- [ ] **Horizontal scrolling mode**: pages side by side, each fit to the window height by default.
  - A toggle between snapping to whole pages and continuous horizontal scrolling.
  - Works with two or more columns (rows of pages).
  - In this mode, ‹ › buttons (previous / next page) in the page / zoom pill.
- [ ] **Presentation mode**, for teaching and presenting, built on full screen:
  - Each page fills the screen, with snapped horizontal scrolling.
  - PowerPoint-like keys: Space and the arrow keys go to the next or previous page; typing a number and Enter goes
    to that page. The number jump is useful in normal mode too.
  - Switch back and forth between present and edit from full screen, and start it from the normal tool bar.
  - Writing on slides while presenting follows from full screen (the tool square stays).
- [x] **Switching tabs in full screen (editing)** (the author, 2026-09-24; queued in `qt/present`): a slim bar at the
  top centre with one dot per tab (`PageIndicator`; "3 / 17" with many tabs). A tap opens the tab overview, and a
  horizontal swipe on the bar switches to the previous or next tab. Hidden with one tab and while presenting.
- [ ] **16:9 pages**: a PowerPoint-like 16:9 landscape paper size when creating a new `.xopp` and when inserting
  pages, for documents meant to be presented.

### Reference mode (the author, 2026-09-24; `qt/reference-view`, merged 2026-09-24)
- [~] **A second document beside the current one, in the same tab**, for reading while writing notes.
  - A draggable divider splits the canvas area. The main document has a thin highlight border. Sides can be
    swapped for left or right hand. Works in full screen.
  - The reference side is a plain scrollable canvas with no tool bar, only a tiny pill: page counter and number
    jump, fit width, swap sides, swap roles (make it the main document), close.
  - Reading by default: pen, touch and mouse scroll and zoom there, and strokes never land in it. PDF text
    selection, copy and lasso copy work, to paste into the notes.
  - **An edit toggle in the reference pill** (the author, 2026-09-24): when on, the reference is a normal canvas
    with the current tool and its own undo. It is remembered per tab.
  - **The page grid for the reference** (a grid button in its pill), but **no page sidebar** for it: space is
    limited, and the sidebar keeps showing the main document.
  - Merged 2026-09-24, with the follow-up (full text selection and context menus, shared with the main canvas).
  - [x] **Pop out** (merged 2026-09-24, `qt/reference-popout`): a button in the reference pill that shows the
    reference as its own tab, placed right after the current one, so Ctrl+Tab switches between notes and reference.
    - Copy always; Highlight, Underline, Strike through and Paste only when editing is on.
    - The lasso bar, the canvas context menu, Markdown when editable, and Ctrl+S saving the focused reference.
  - The reference is another open tab ("Open as reference" in the tab overview, the tab menu and the library and
    Recent card menus); the tab strip marks it.

### Links between documents (the author, 2026-09-24)
- [~] Design agreed 2026-09-24: [qt/docs/links.md](qt/docs/links.md). `qt/links` started 2026-09-24. Links are relative paths with `#page=`, `#chapter=…&page=`
  as the fallback, and `pdfpage=` for pages of annotated PDFs. A tap offers a new tab, reference view or "here".
  In-app renames rewrite the links, backed by the index's backlinks. Built as `qt/links` after the running blocks.

### Bugs
- [x] **A PDF page pasted into a document that has a PDF showed late on the canvas**: fixed in `qt/background-save`.
- [x] **Fit width uses the widest page, not the current one** (fixed in `qt/present`) (the author, 2026-09-24): after pasting a 16:9 page
  into an A4 document, fit width fits the 16:9 width. It should fit the current page (in several columns, the
  current row). Given to `qt/present` as its next commit.
- [ ] **A touch on the "pages with hits" filter can make the maximized window half as high** (old, flaky, probably
  touch only, KWin). Also seen with the page grid button in the page / zoom pill. Does not reproduce off-screen.
  Both taps change what is under the finger (an overlay opens, or the list is filtered); suspect a touch whose item
  disappears or moves mid-touch, with the rest of the touch taken by KWin as a window gesture. Next time: run with
  `XQT_LOG_WINDOW=1` and look for a touch cancel, or an odd touch end, just before the resize.

### Flaky tests
- [ ] `MainWindowTest.theSelectedPdfTextTakesItsHandlesAndActionsAlong` failed once in the full suite under
  `-j6` load (2026-09-24), at the check after "the way back brings it into view again". It passed 3 of 3 alone.
  The wait for the scroll back is probably too short under load.

- [ ] `MainWindowTest.sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown` ("no sharp one while racing")
  fails now and then under load: 1 of 8 and 0 of 16 after `qt/present`, and once in the full suite; 0 of 16 on a
  build from before it. It is timing-sensitive, and whether `qt/present` made it more likely is not settled.

- [x] `LibraryTest.renamedAndMovedDocumentsKeepTheirIndex` failed 1 of 4 runs on 2026-09-24 under load. Fixed in
  `qt/index-rename-race`: an update still running when the app moved a folder replaced the entry of a document
  that vanished under it with an empty one; now such an entry stays for the move, and a document without an entry
  takes over a gone one with the same size, time and name or content sample. 40 of 40 under load; the CI retry is
  gone.

- [ ] **Harden the UI tests that use fixed waits** (`qt/test-waits`). Under load (a full suite at `-j6` next to agent
  builds) timing tests fail now and then: on 2026-09-24 `theCanvasShowsThePreviewUntilThePageIsRendered` (1 of 4
  alone), `sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown`,
  `HomeScreenFilterTest.textFilesAndImagesOpenExternally`, and `CanvasMemoryTest.theCurrentDocumentRendersAheadMoreThanBehind`.
  All pass alone. Replace fixed `wait(ms)` with waiting for the condition (with a generous timeout), and find tests
  whose expectation itself depends on timing.

### Platform research
Done 2026-09-24: [qt/docs/platform-research.md](qt/docs/platform-research.md) covers native libraries and PDF
engines, with a recommendation and cheap experiments to decide.
- [x] **Pasted PDF pages stay searchable** (`qt/pdf-pages`, merged 2026-09-24).
- [?] **Experiments before the engine decision:**
  - [x] a render benchmark of poppler, MuPDF and pdfium: done on `qt/mupdf` (see "Order of work");
  - [ ] a `/Ink` + `/AP` round trip through Acrobat, Xodo, Drawboard and Preview (the author, with
    `~/xournal_qt_workspace/samples/hybrid-sample.pdf`);
  - [ ] whether an embedded `.xopp` survives saves in other apps (the author);
  - [ ] pen latency on the Surface and the iPad (the author, on the devices).
