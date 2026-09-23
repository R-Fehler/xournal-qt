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

## Ready: bug and polish blocks

Order (2026-09-23): `qt/render-visible` and `qt/ui-polish` first, in parallel. Then `qt/markdown-fixes`.
Nothing is started yet; the author says when implementation begins.

### `qt/render-visible`: rendering what is on screen (highest priority)
Area: `qt/src/render` (RenderService, PageRaster), `qt/src/canvas` (CanvasView, CanvasMemory),
`qt/src/quick` (DocumentCanvasItem), `qt/src/shell` (PageSketches). Tests: `-L canvas`, `-L quick`.

These bugs probably share a cause, so fix them in one worktree:
- [ ] **Zoom shows grey pages.** When zooming reaches a level that needs a new render, the 1–4 pages in view
  stop showing the GPU-scaled picture and show a grey page instead. Pages at the edge or outside the view are
  fine. It recovers after zooming and scrolling around. Easy to reproduce.
  *Suspect:* the old texture of a visible page is dropped or invalidated before the new render arrives, maybe
  by the zoom block or by CanvasMemory eviction.
- [ ] **The current page sometimes stays blurry**, at preview resolution, and never gets its sharp render.
  Probably the same cause (a missed or cancelled sharp request).
- [ ] **Render the visible page first once scrolling stops.** Raise its priority, preempt background and preview
  jobs, and maybe split the visible page into tiles across several threads. Measure with `XQT_PERF=1` before
  and after.
- [ ] **Closing the app or a tab can freeze the UI** for a long time on big PDFs. Find what the UI thread waits
  for: render or preview workers that do not stop, preview or index files written on close, or poppler's
  per-document mutex. Fix: cancel queued work, wait only for the job that is running, and write to disk off
  the UI thread (or skip what can be rebuilt).

### `qt/markdown-fixes`
Area: `qt/src/markdown`, the Markdown editor in `qt/src/canvas`, `DocumentSearch`. Tests: `-L markdown`.
- [ ] **A code block at the end of the text misbehaves in live rendering.** For example
  `` ```py\n code\n #stuff ``` `` as the last thing in a box. The grey block background keeps being drawn, and
  text typed after Enter stays invisible until more text is typed or the source sidebar is toggled once.
  Probably the incremental re-layout does not handle an open or just-closed fence at the end.
- [ ] **Search boxes sit at the wrong place in rendered Markdown.** They are where the plain text would be in
  a normal Xournal text box, not on the rendered words. The search must map hits through the Markdown
  layout.

### `qt/ui-polish`
Area: `qt/src/app/qml`, a bit of `CanvasInput`. Mostly QML, so builds are cheap. Tests: `-L ui`, `-L quick`.
- [ ] **Tab bar: previous/next arrow buttons**, for example next to the overview button.
- [ ] **Full-screen button in the tool bar.** F11 full screen already exists; it only needs a button.
- [ ] **Tool bar docked left or right:**
  - the thin "show tool bar" strip stays at the top instead of moving to the dock side;
  - the hide button's arrow points into the canvas instead of towards the tool bar.
- [ ] **Paste pill on a long press with the pen.** With the pen (or another drawing tool) selected, a long
  press on the canvas should show the copy/paste pill at the bottom of the canvas, as it does elsewhere.

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
  - *Open questions:*
    - How big is `.xournal_library/` today per folder: index only, or previews too?
    - In the app-cache mode, are entries keyed by path, so a move means re-indexing?
    - How does cloud sync handle many small dot folders?
- [?] **Fuzzy search with logical operators**, modelled on fzf; clone `junegunn/fzf` as a reference.
  - *Proposal:* use fzf's own extended syntax instead of inventing one:
    - a space means AND; `|` means OR; `!term` means NOT;
    - `'exact`, `^prefix` and `suffix$` narrow a term;
    - parentheses sit behind an "advanced" toggle.
  - Upper-case `AND`/`OR`/`XOR` would also work, but they collide with searching for those words. XOR is
    rarely useful for documents.
  - *Question:* should the operators always be on, as in fzf, or only behind a button?

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
