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
        untested); a "has notes" badge (audio attachments: done in `qt/audio`). Writing into the PDF itself renames over the file
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
      - [x] "Only PDFs with notes" uses the index's kind (`qt/library-kinds`, 2026-09-26). Left: a `.xopp` next to a
        same-name PDF is still checked with qpdf while listing (`DocumentFiles::markHybrid`, cached per version).
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
3. **Android:** ~~`qt/android-apk`~~, ~~`qt/android-basics`~~, ~~`qt/android-libraries`~~ merged. Next: the author's
   tests on the Fold 7, then `qt/docs/android-roadmap.md` (Save as to a picked place, a compact tool bar).
4. **The `.md` editor:** ~~`qt/md-editor`~~ merged 2026-09-24; math merged (`qt/md-math`). Left: images in
   `<name>.assets/`, vaults.
5. **Windows:** ~~`qt/windows-build`~~ merged 2026-09-24; `qt/windows-feel` is the author's, on the Surface.
6. **PDF as the document:** ~~`qt/pdf-incremental`~~ and ~~`qt/pdf-only`~~ merged 2026-09-24.

Blocks for tracks 2–4 get their `qt/...` names when they are planned. Research for each happens right before it
is built.

## Ready: bug and polish blocks

### Adaptive UI ([ui-adaptive-audit.md](qt/docs/ui-adaptive-audit.md), [adaptive-layout.md](qt/docs/adaptive-layout.md))
- [x] Block 1 `qt/adaptive-foundation`: size classes, touch profile, choices per class, the sidebar binding and
  drawer, chromeMode apart from full screen, `AdaptiveLayoutTest`.
- [x] Left from block 1: the tab strip's 38 px buttons and the other F14 targets sized by `minTarget` (the Markdown
  panel and reference split in block 5, HomeView and TabOverview in block 6, the rest in block 8).
- [x] Block 2 `qt/adaptive-menus`: `AdaptiveMenu` (width of its entries, never taller than the window, clear of its
  button; a bottom sheet with drill-in in the phone classes, `MenuSheet`), the ⋮ regrouping (10 entries, Document /
  Export / Page / View), used for ⋮, library, card, tab, layout and page menus; `menusFitAtFiveSizes`,
  `menusAreSheetsOnPhones`.
- [ ] Left from block 2: (every menu is an `AdaptiveMenu` now, block 8.) The sheet is not checked on the Fold 7 yet
  (Android back key, the navigation bar via `safeBottom`). Unchecked choices show no box in the sheet (only a check
  mark when chosen).
- [x] Block 3 `qt/adaptive-dialogs`: `AdaptiveDialog` for all dialogs and sheets (scrolling body, full-screen sheet,
  bottom sheet, stacked footer buttons, back key), Settings as a list of sections on phones, the quick tools popup.
- [ ] Left from block 3: the bottom sheet keeps Material's rounded corners at the bottom edge and opens with the
  dialog's grow animation (no slide); the Settings sheet is its own popup, not an `AdaptiveDialog`; the Markdown table
  editor and the small anchored popups (link, page jump, custom width) are not adapted (the tab overview keeps to the
  safe area since block 8; the look-up menu is an `AdaptiveMenu`).
- [ ] Left from block 4 `qt/adaptive-toolbar` (tool bar layouts, "more tools", cycling buttons, the view pill, the
  sidebar arrow, the text document's bar in the format bar): the pen pill's width and colors are not cycling buttons
  with lists yet. (Done: the palette, the widths, the dock and "All tools" in `qt/phone-chrome`; the format bar above
  the soft keyboard and the remaining plain menus in block 8.)
- [ ] Left from block 6 `qt/adaptive-home` (the switch as name ▾ + Recent / ★ / Bookmarks icons, the library header's
  ladder with "+" and View, the floating "+", the
  breadcrumbs "…", the selection's bar at the bottom, the tab overview's wrapping header and aspect cells): a list view
  of the library for phones (audit D7, "consider") is not built. (Done: the phone's tab strip, the app bar of
  `qt/phone-chrome`; the `IconButton`s' short labels and the overview cards' finger-sized buttons in block 8.)
- [x] Block 5 `qt/adaptive-panels`: the Markdown source below the page in portrait (a draggable divider, remembered per
  class), the reference top and bottom in a portrait area (ratio kept), the reference's narrow pill (page + ⋮), the
  compact view pill, pills kept clear of the view pill, the drawer's slide / Esc / back key / phone width, the format
  bar's Insert overflow and fading edges.
- [ ] Left from block 5: the format bar still scrolls below ~680 px on a desktop (600×800); the Markdown panel's
  title and size rows are not made smaller on a phone; the pen pill is still tall in phone landscape (only in the
  compact chrome now: the phone chrome has the dock). (The reference's page field and the emoji picker are sheets on
  phones since block 8.)
- [x] Block 7 `qt/phone-chrome` (was `qt/compact-chrome`): in the phone classes the app bar (the library, the title
  with tab dots and a swipe for the next / previous document, the tab count: tap all, double tap the one before, hold
  the ones used lately; ⋮) instead of the tab strip, the tool dock at the bottom (a rail at the side in landscape)
  instead of the tool bar, the pen pill and the view pill, "All tools" as a sheet with every variant, the palette and
  the widths as sheets; the reader automatic only in tiny windows; the presenting / reader corner field highlighted
  with its names; no windows of their own on Android / iOS; no breadcrumbs at the library's top; `PhoneChromeTest`.
- [ ] Left from block 7: the dock and the app bar are not checked on the Fold 7 yet (device checklist, "Phone
  chrome"); the compact chrome (chosen by hand on a phone) still has the tool square, the pen pill and the tab dots of full screen; the tab menu
  (rename, reference, share) is not reachable from the phone's app bar (the overview's cards have rename, reference and
  close; ⋮ has share and rename); the page layout on a phone is only in ⋮ → View.
- [x] Block 8 `qt/safe-areas-keyboard`: `win.safeInsets` on all four edges (Qt 6.9+ safe area margins for every
  window, fake ones for the tests and `XQT_SAFE_AREA`), every control kept clear of them while the page is drawn under
  them; the soft keyboard (`win.keyboardTop`, the footer makes room for it, the dock goes, the format bar docks right
  above it on a phone, also the source panel's; the text cursor scrolled into view; sheets, menus and dialogs above
  it); the remaining F14 targets; the remaining plain menus as `AdaptiveMenu`s; the home screen's short labels;
  `SafeAreasKeyboardTest`.
- [ ] Left from block 8: nothing of it is checked on the Fold 7 yet (device checklist, "Safe areas and the soft
  keyboard"), and the APK was not built with it (the Qt 6.9+ part compiles against Qt 6.11's headers). How Qt 6.11 on
  Android 16 reports the keyboard (screen pixels, and whether the window is still made smaller) is taken from the New
  document dialog's experience: check it. Not adapted: the deprecated text flow panel (no insets, no keyboard room of
  its own), the link popup and the page jump (in the middle of the page, so clear of the bars anyway). The emoji
  button is in "All tools", which is out of reach while the keyboard is open (the dock goes): the keyboard's own emoji
  or closing the keyboard.

---

## Ready after a short plan: platform

### `qt/windows-build`: first Windows build (merged 2026-09-24; `~/xournal_qt_workspace/samples/xournal-qt-windows-x64.zip`; next steps in `qt/docs/windows-roadmap.md`)
- [~] First feedback from a Surface Pro 8 with Windows 11 (the author, 2026-09-24): "works great". Two bugs:
  - Fixed (merged 2026-09-24): Downloads opened by its path (`LocalUrl`). `XQT_LOG_INPUT` and
    `xournal-qt-debug.bat` are in the zip.
  - Pressure: not a bug. The pressure comes through (the author confirmed, 2026-09-24; the input log shows real pen
    events). A pressure calibration in Settings → Pen remains a nice-to-have.
  - The author will fine-tune the Windows feel with a Claude Code session on the Surface: local MSYS2 builds, on
    its own branch (e.g. `qt/windows-feel`), fetched and merged here.
  Original notes:
  - Pen pressure is constant. There will be an `XQT_LOG_INPUT=1` log and a `xournal-qt-debug.bat` in the zip.
  - "Open Downloads as a quick library" fails with "cannot open c//": a file URL built by hand. The whole app is
    being checked for such URL and path conversions.
- GitHub Actions on a Windows runner with MSYS2 UCRT64 (upstream Xournal++'s toolchain; the C libraries and Qt 6
  come prebuilt), started by hand or by a push to the branch. It produces a zip, with an installer later. QField's
  MSVC + vcpkg way is the fallback.
- Pushing only `qt/windows-build`, and only with the author's go.

### `qt/android-storage` (the author, 2026-09-25; merged 2026-09-25)
- "Libraries are deleted on uninstall; this goes against the philosophy of the app", and the Downloads quick library
  was empty. Built: libraries in the phone's Documents with a safe move, keep data on uninstall, in-app folder chooser.
- Left: the author's own move on the Fold 7 (device checklist); SD cards as a home; a kill in the milliseconds between
  the final renames and the manifest gives "(2)" copies on the next offer; Play Store needs another way than
  `MANAGE_EXTERNAL_STORAGE`.
- [ ] After the move on the Fold 7: switch the phone to the release-signed APK (one uninstall; the libraries stay).

### `qt/android-libraries` (the author, 2026-09-24; merged 2026-09-25)
- Left (found on the emulator):
  - [ ] Some texts on Android miss the opening quote “ and the dash — ("Default — this window", the start of the
    all-files explanation), while the message dialog shows them. Probably the symbol fallback font from
    `qt/android-basics` (a DejaVu subset) taking over for these characters.
  - [ ] After a reload the tab strip does not scroll to the current tab.
  - [ ] A Recent card drawn while access was missing stays blank until it is redrawn.
  - [x] "Show in file manager" is hidden on Android (`qt/android-storage`).
  - [ ] Google Play needs another way than `MANAGE_EXTERNAL_STORAGE` (later).
- Not verified: the Fold 7 with a real Syncthing/Autosync folder; Android 10 and older; SD-card and Downloads paths.
- **Libraries in folders kept in sync by other apps** (Syncthing, Autosync, FolderSync mirror into real folders in
  shared storage; the providers' own apps only offer `content://`).
  - **"All files access"** (`MANAGE_EXTERNAL_STORAGE`) with a short explanation of why it is asked for. Real paths
    in shared storage, so a library at, e.g., `Documents/Uni` works exactly as on the desktop (per-folder packs,
    index, file watching). Fine for sideloading and F-Droid; Google Play restricts it, to be revisited for Play.
  - **"Open a folder as library"** for shared-storage folders, with the existing recent-libraries list. The system
    picker (`content://`) stays for opening and importing single files only.
  - **The cache defaults to the app cache on Android**, so sync apps do not upload cache files.
  - **External changes to open PDF and `.xopp` documents** (all platforms): reload when unchanged, otherwise ask, as
    the `.md` editor does.
  - **Sync conflict files** (`.sync-conflict-…`, `… (conflicted copy)`, `… (Konflikt …)`, and so on) are shown as
    conflicts of their document, with "compare / keep one", not as separate documents.

### `qt/android-basics` (the author tested the APK on the Fold 7, 2026-09-24; merged 2026-09-24, APK in samples)
- "Feels the snappiest of all platforms: scrolling, fast movements, the general feel." The problems:
  - no "Open with" or Share target;
  - opening and importing files and folders fails (`content://`);
  - Markdown syntax highlighting is off on Android;
  - the UI is not adapted to Android.
- Scope:
  - "Open with" and Share intents for PDF, `.xopp`, `.md` and images (the core formats);
  - opening and importing through the Storage Access Framework (`content://`);
  - a draw-with-finger-or-mouse toggle (all platforms);
  - KSyntaxHighlighting built for Android;
  - only the clear UI problems (status bar over the tabs, the home screen overflowing). Keep the UI divergence
    small and maintainable; the details come later, as the desktop UI is still changing.

### `qt/android-apk`: first APK (merged 2026-09-24; `~/xournal_qt_workspace/samples/xournal-qt-debug-arm64.apk`; see `qt/docs/android.md` and `qt/docs/android-roadmap.md`)
Tooling (2026-09-24, in the author's home, no sudo): JDK 17 in `~/.local/jdk-17`; Android command-line tools and
NDK r27c (27.2.12479018) in `~/Android/Sdk`; Qt 6.11.2 desktop (host, `gcc_64`) and `android_arm64_v8a` in `~/Qt` (2 GB) through
`aqtinstall` (`~/.local/bin/aqt`); 6.11.3 was not fully mirrored yet. Installed 2026-09-24. The block itself comes after the `.md` editor, at the author's wish.
Research is already done in `../cross-platform-qt-research/` (03-android-plan, 05-qfield-reference,
06-risks). Keep the current PDF engine (poppler/cairo).
- [x] vcpkg manifest and toolchain-agnostic dependency lookup, following QField (`../QField`).
- [x] Android build: CMake preset, `AndroidManifest`, an unsigned debug APK that can be installed with `adb install`.
- [x] A CI job, running only when asked (`.github/workflows/xqt-android.yml`, 2026-09-25; manual dispatch or a push to
  `qt/android-build`). Signs with a release key when the repository secrets hold one.
- [x] Start `qt/docs/android-roadmap.md` for everything about Android UI/UX. That work waits until mobile
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
- [x] **Math with MicroTeX** (`qt/md-math`, merged 2026-09-25; decided 2026-09-24), vendored (MIT, no LaTeX install, works on mobile).
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
- [x] **Horizontal scrolling mode**: pages side by side, each fit to the window height by default.
  - A toggle between snapping to whole pages and continuous horizontal scrolling.
  - Works with two or more columns (rows of pages).
  - In this mode, ‹ › buttons (previous / next page) in the page / zoom pill.
- [x] **Presentation mode**, for teaching and presenting, built on full screen:
  - Each page fills the screen, with snapped horizontal scrolling.
  - PowerPoint-like keys: Space and the arrow keys go to the next or previous page; typing a number and Enter goes
    to that page. The number jump is useful in normal mode too.
  - Switch back and forth between present and edit from full screen, and start it from the normal tool bar.
  - Writing on slides while presenting follows from full screen (the tool square stays).
- [x] **Switching tabs in full screen (editing)** (the author, 2026-09-24; queued in `qt/present`): a slim bar at the
  top centre with one dot per tab (`PageIndicator`; "3 / 17" with many tabs). A tap opens the tab overview, and a
  horizontal swipe on the bar switches to the previous or next tab. Hidden with one tab and while presenting.
- [x] **16:9 pages**: a PowerPoint-like 16:9 landscape paper size when creating a new `.xopp` and when inserting
  pages, for documents meant to be presented.

### Reference mode (the author, 2026-09-24; `qt/reference-view`, merged 2026-09-24)
- [x] **A second document beside the current one, in the same tab**, for reading while writing notes.
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
- [x] Merged 2026-09-24 (`qt/links`). Design: [qt/docs/links.md](qt/docs/links.md). Left: a `pdfid=` key for the PDF `/ID` search; drag and drop onto the page; rewriting links in hybrid PDFs and `.xoj` files that are not open; link boxes on rotated pages of hybrid PDFs. Links are relative paths with `#page=`, `#chapter=…&page=`
  as the fallback, and `pdfpage=` for pages of annotated PDFs. A tap offers a new tab, reference view or "here".
  In-app renames rewrite the links, backed by the index's backlinks. Built as `qt/links` after the running blocks.

### Archive export (the author, 2026-09-24; `qt/archive-export`, merged 2026-09-24)
- [x] **"Export for the archive…"** per document (⋮ and Share), with a short explanation in the dialog of what it
  means: a PDF/A-3 file meant to stay readable for decades in any PDF viewer, with the ink flattened into the pages
  so no viewer can hide or lose it, and the full Xournal data embedded so the app can still open it for editing.
  - PDF/A-3b: fonts embedded (report source PDFs that cannot comply instead of claiming it), an output colour
    profile, XMP metadata, and the embedded `.xopp` marked as the source data (`/AFRelationship /Source`).
  - Validation with veraPDF in CI on sample files, as a test tool only.
- [x] **"Export library as archive…"** in the library menu: every document of the library (or the current folder)
  exported as an archive PDF into a chosen folder, keeping the folder structure. Other files are copied as they are,
  and a short `README.txt` explains the contents. It runs in the background with progress and can be cancelled.

### Ideas round of 2026-09-25/26 (the author)
- First wave, started 2026-09-26, all merged 2026-09-26: `qt/emoji` (bundled colour emoji font, `:smile:` completion, paste) ·
  `qt/self-reference` (the same document in the reference view, page subsets) · `qt/calibration` (1 cm on screen
  = 1 cm, per screen) · `qt/sticky-notes` (opaque, resizable, ink and text attached, cover mode for self-testing) ·
  `qt/annotations-md` (stage 1: live Annotations panel + "Export as Markdown"; stage 2 "keep updated" later).
- [x] **Markdown inside the PDF with notes** (the "word-processor" mode; `qt/md-pdf` merged 2026-09-26; images next: `qt/md-images`): new text documents follow the first-start
  choice (PDF files → a PDF with the `.md` and its images inside; Xournal++ files → `name.md` + `name.assets/`),
  changeable in Settings, and a mix must work: existing `.md` files are never converted unasked. Split into
  `qt/md-images` (images in Markdown + the `.md`/`.assets` pair as one document), `qt/md-pdf` (the container,
  text only), later ink on Markdown pages. Not started: the author wants to discuss it first.
  - Ink on reflowing Markdown (the author, 2026-09-26): no anchoring; the user handles it (e.g. inserts a page
    above and moves the text on). Document the behaviour, don't engineer around it.
  - Agreed (2026-09-26): one model for both (a document is pages; a Markdown text is a flow over a run of pages).
    A `.md` is a document with exactly one flow and nothing else; the PDF text document is the notes model (flow +
    ink) inside a PDF with notes, exportable as `.md` + assets; a page break in Markdown
    (`<div style="page-break-after: always"></div>`) ends the flow's page.
  - Agreed (2026-09-26): the PDF text document also carries a plain `name.md` (rewritten on every save) and its
    images as `name.assets/…` attachments next to the `.xopp`, so whoever gets the PDF can extract portable
    Markdown with any PDF viewer (copying text out of a PDF would not give Markdown).
  - [x] Images (`qt/md-images`, merged 2026-09-26): drawn inline like formulas (cached); paste/drop saves the image
    and inserts `![](name.assets/…)`; `.md` → `name.assets/` next to it (the pair is one document); PDF text
    document → attachments inside the PDF (unpacked to the app cache while editing); Markdown boxes in a `.xopp` →
    inside the `.xopp`. Web images are not fetched unasked (alt text + "load", URL shown first).
- [ ] **`qt/md-toolbar`** (the author, 2026-09-26): formatting tools for people who don't know Markdown syntax, in
  the `.md` editor and for Markdown boxes: heading levels, bold/italic/strike/code, bullet, numbered and checkbox
  lists, quote, code block, link, image, horizontal rule, math, page break, and **insert/edit table** in an
  interactive table editor (a grid in a popup: cells, add/remove rows and columns, alignment, the current row and
  column shown), writing a normal pipe table. A page break needs a syntax that other tools ignore or understand
  (proposal: `<div style="page-break-after: always"></div>`, as Typora and Obsidian's PDF export use).
- [x] Citations (`qt/citations`, merged 2026-09-26; left: look-up in the Markdown source panel and text mode, `.bib`): Scholar/translate on selected text; bibliography entry → library hits (fuzzy, by title and first
  page) → open in reference/tab, copy as a link; arXiv import (named by title). Networking is opt-in, and the URL is
  always shown (hover or preview) before anything is opened or downloaded. `.bib` later, after the user flow is
  thought through.
- [x] Note space for slides (`qt/note-space`, merged 2026-09-26). Left: geometry tools do not move with the slide; PDF
  pages whose crop box is smaller than their media box show the bleed in the space in other viewers.
- [ ] Forms (only on PDFs that have fields) and a "My signature" stamp. Cryptographic signing: backlog.
- [ ] **OCR (Tesseract), last of this round**: photo import with cropping, a text layer in PDFs. Never automatic:
  ask before each run, with "remember my choice", and a button in Settings to forget it.
- [?] **Handwriting search**: research done (`qt/docs/research/handwriting-recognition.md`, 2026-09-26). Proposal:
  search only in v1 (top-5 word candidates with boxes into `DocumentTextIndex`, so fuzzy search and highlights work
  as for PDF text); Linux and Android: bundled ONNX Runtime + TrOCR-small handwritten int8 (64 MB, 0.2 s per line,
  English 97 % of words found; German only 41 %); Windows: the system recogniser (German included). No fine-tuning
  in v1 (keep corrections, library words as extra candidates, a training text as a check). Questions for the
  author: search-only first? train a German model once (fhswf/german_handwriting, AFL-3.0) or rely on Windows for
  German? ML Kit on Android as an opt-in flavour or not at all? +64 MB per model in packages, or a one-time
  download from our release page? corrections in the `.xopp` or only in the cache? defer fine-tuning?
  Note: points in this codebase carry no time, only x, y and pressure (stroke order is there).
- [x] Touch multi-select (`qt/touch-multiselect`, merged 2026-09-26): "Select more" toggle + count in the pills;
  selecting in the self-reference view as in the main view. Left: a rectangle in the mode only adds; long press
  does not toggle; not offered in a view that is only for reading.
- [x] Favourites and page bookmarks (`qt/bookmarks`, merged 2026-09-26): stars beside the document (DocumentPlaces),
  bookmarks in it (`.xopp` page attribute, PDF outline item "Bookmarks"), Favourites chip and Bookmarks tab. Left:
  - [x] Bookmarks for Markdown documents (`qt/md-bookmarks`): `<!-- xqt:bookmark label -->` before the marked block,
    in `.md` files and PDF text documents (qt/docs/bookmarks.md, "Markdown"). Left:
    - [ ] A continuous page (`textContinuous`): go to the comment's place on the long page (today every bookmark is
      on the one page; the library lists the pages the file has on pages).
    - [ ] Markdown text boxes and flows that start after page 1 of a `.xopp` keep the page attribute (page
      attributes of the `qt/bookmarks` build on pages of a text are turned into comments when opened).
  - [ ] Plain PDF export: write the current bookmarks into the outline (upstream's exporter copies the outline as
    read, so it can carry an out-of-date "Bookmarks" item).
  - [ ] Read back bookmarks changed in other PDF apps (today the embedded `.xopp` wins on the next save).
  - [ ] Remember the Favourites chip across starts; a star in the tab overview for annotated PDFs without a path.
- Backlog: visual text diff between PDF versions; cryptographic signing.

### Ideas round of 2026-10-04 (the author)
Sorted into blocks by the integrating session (cloud session, integration branch `claude/admiring-pascal-hekja6`
standing in for `master-qt`). Facts found while sorting: upstream already has the laser pointer tools
(`TOOL_LASER_POINTER_PEN/HIGHLIGHTER`, not wired up here), `LineStyle` (dash/dot, saved in `.xopp`), stroke fill and
per-stroke audio (`ts`/`fn` attributes). Upstream has **no grouping** of elements. The canvas cursor is the system's
`Qt::CrossCursor`, which is why the crosshair feels faster than a drawn dot.

**Wave 1: clear, built right away**
- [x] `qt/md-tables`: table columns in rendered Markdown are too narrow in some cases. Size them like VS Code's
  preview / GitHub (the browser's automatic table layout: min-content and max-content widths per column, spread
  over the width; wrap inside cells; a table wider than the text column scrolls or shrinks, never overlaps). Then a
  research note on a **two-column Markdown mode** (a comment marker, e.g. `<!-- xqt:columns 2 -->`, for `.md`
  files and page-wise text), and the mode itself as an option if it is easy. Wide tables and images span both
  columns. Done: columns sized by min-/max-content, a table too wide drawn smaller (down to 60 %), never over the box; two-column note in `qt/docs/md-columns.md`: not easy (pagination split search and editor hit test / Up-Down need column cases), not built. Left: the author's call on building columns; column widths can differ per page of a table split over pages.
- [x] `qt/pen-styles`: line styles (solid, dashed, dash-dot, dotted, as upstream, saved in `.xopp`); filling shapes
  (and closed freehand strokes) with the stroke's color or another color, with upstream's fill opacity; the
  **laser pointer** (upstream's laser pen and highlighter tools: ink that fades after N seconds, a setting; never
  saved, never on the undo stack), reachable from the presentation mode. (Built 2026-10-04. Left: changing the line
  style or filling of a selection; a fill color for the highlighter; the device checks.)
- [x] `qt/color-palettes`: the author's role-based palettes (Classic, Marker, Pastel study, Colorblind-safe 8 and
  6, Dark; JSON spec in the prompt of 2026-10-04, kept as a resource) as tabs in the color chooser, next to the
  existing picker and hex field. Roles keep their meaning across palettes (the role name shows as a tooltip); a
  palette may omit roles; each role has an ink and a highlight color; highlighter opacity 0.5 on light paper, 0.8
  on dark. The chosen palette is a setting. A color picked from a palette remembers its role, so tool presets can
  follow a palette switch (used by `qt/toolbox`).
  Done (qt/docs/color-palettes.md); left: strokes still use upstream's fixed highlighter opacity (0.47, multiplied),
  so 0.8 on dark paper shows only in the chooser until a per-stroke opacity seam is decided.
- [x] `qt/hover-cursors`: the pen's hover dot as fast as the crosshair (a cursor of its own instead of a drawn
  item, if that is the cause), the crosshair as a setting, and an **eraser preview**: a gray circle of the eraser's
  size and shape while hovering.
  Done (qt/docs/hover-cursors.md): the dot is a cursor (the drawn dot lagged two to three frames), Dot/Crosshair in
  Settings → Pen, the eraser as a gray square (dashed: whole strokes, round: whiteout) at its zoomed size. Left: check
  on the device that Wayland shows the cursor for the pen (assumed from Qt 6.7; `XQT_PEN_CURSOR` overrides it).
- [x] `qt/undo-redo`: in Markdown text documents redo is grayed out in the bottom-right pill while Ctrl+Shift+Z
  works (bug: failing test first). Undo and redo are hard to find: make them visible buttons in the tool bar and the
  phone chrome (their final place follows `qt/toolbox`). Done: the buttons follow the editor's steps and the edited
  reference; undo/redo lead the tool bar (the pill only while the bar is not shown; a text document keeps them in the
  pill). Left: their final place in `qt/toolbox`'s docked toolbox; the device pass.
- [x] `qt/curtain`: a **curtain** for teaching and presenting: a black area that hides part of the page, and its
  inverse, a **spotlight** (only a rectangle stays visible). Placed and moved, turned and resized with handles like
  the setsquare (`GeometryToolLayer`); a tap on the black part shows the handles. Only on screen: never saved,
  printed or exported. Works in full screen and the presentation mode. *Built (qt/docs/curtain.md: B / Shift+B, the
  setsquare button's list, ⋮ → View, the tool square; edges pushed with the handles hidden, its place remembered per
  tab). Left: the device pass; QML overlays (selection pill, PDF text knobs, sticky note pill) still show over it.*
- [x] `qt/page-ops`: a long press on a page in the page grid or the sidebar starts the selection mode with that
  page selected; moving the finger after the long press still drags the pages, as today. **Rotate pages** by 90°
  left or right (current page, selected pages, all pages), undoable, in the page menu. Pages without a PDF
  background first; PDF pages need a decision (see the questions below). *Built (qt/docs/page-rotation.md): the
  sidebar got a selection mode with a bar; PDF pages turn in PDF files with notes (the PDF page's /Rotate, through the
  merged PDF) and, as the author decided, in a `.xopp` too (a turned copy in the hidden `.name.pages.pdf` that
  Xournal++ reads). Left: the device pass; no haptic tick (none in
  the app); Markdown boxes and sticky notes stay upright.*
- [x] `qt/hidpi-fractional`: check fractional scaling (125 %, 150 %, 175 %) on Plasma and GNOME (Wayland and X11),
  Windows and macOS: Qt 6 passes the factor through (`HighDpiScaleFactorRoundingPolicy::PassThrough`); check that
  nothing in the app rounds it, that pages, thumbnails and the setsquare are sharp, and that lines and borders of
  the UI do not blur or jump. Tests with `QT_SCALE_FACTOR=1.25/1.5`. *Audited and fixed (qt/docs/hidpi.md):
  thumbnails were drawn at dpr², the selection, curtain handles and the drawn pen dot were off device pixels, the
  app's lines and page frames uneven. Left: the device pass; Qt's own ToolSeparator/MenuSeparator are still uneven
  (author's call), CurtainCanvasTest at 200 % in the software renderer.*
- [x] `qt/snip`: a quick **area screenshot**: a lasso or rectangle (reusing the selection tools) copies the
  canvas's pixels (ink and background, at a good resolution) to the clipboard as an image. In the selection tools'
  cycle and in the insert image entry. Pasting it into a xournal-qt document offers to add a link to the source
  page next to it (`qt/links`). Built (qt/docs/snip.md; region renderer `qt/src/render/RegionRender.*` for
  `qt/stickers`). Left: no link offer in the Markdown panel beside the page; a snip stays on one page; the device checks.
- [x] `qt/onboarding`: a short intro on the first start (what the document modes mean, that PDFs are editable here,
  Markdown documents and turning them into PDFs to write on), ending in the existing document mode choice;
  reachable again from Settings / Help. A **tutorial document** that asks the user to try the tools, modes, search
  and menus, written in Markdown with marked placeholders for the author's ink and screenshots, opened from Help (a
  copy, so it can be written on). Built (qt/docs/onboarding.md); left: the author's screenshots and ink in place of
  the `PLACEHOLDER` quotes of `qt/resources/help/tutorial.md`, and the device checks.

**Designs first (a proposal goes to the author before anything is built)**

*Decided by the author, 2026-10-04* (the proposals are summarised in the blocks' prompts and docs):
- `qt/toolbox`: build as designed. The toolbox is a rail docked to any side of the canvas (left, right, top, bottom),
  **right by default**. Classic tool bar kept for one release; the eraser is an entry; a folded section opens a list;
  tools stored per device. Reading vs presenting (the integrator's call, asked to decide): two modes over one
  "tools hidden" view. **Reading** (⋮ → View → Read) is read-only (pen and fingers scroll; text selection, copy and
  look-up work; no ink by accident) with a reading pill (page, ‹ ›, vertical/sideways, snap/momentum, fit).
  **Presenting** (F5) keeps writing on slides; the corner field hides and shows the toolbox.
- `qt/stickers`: yes (a visible `Stickers/` folder per library, an app-wide set in the app's data folder, own order
  in a hidden file that syncs, pasted at original size). `qt/groups`: yes, after the stickers, with the `xqt-group`
  attribute seam; Ctrl+G groups, Ctrl+Shift+G ungroups (one undo step each); any member selects the group; pasted
  stickers become groups. *Built (`qt/groups`, qt/docs/groups.md). Left: the device checks; no nested groups, no group
  across layers (a sticker with Markdown boxes is two groups), no sticky notes in a group.*
- `qt/audio`: yes (Qt audio + bundled Ogg Vorbis; `.xopp` recordings in the app's audio folder as upstream; PDF
  attachments with page numbers; voice memos per page; Android foreground service; 2 s lead-in).
- Canvas rotation (`qt/canvas-rotate`): yes, after `qt/toolbox` (90° steps first, then free with snapping). *Built
  ([canvas-rotation.md](qt/docs/canvas-rotation.md)); left: the device checks.*
- `qt/todos`: a library-wide To-dos list from Markdown task lines, **grouped and filtered** (by document and
  folder, open/done, due date, text), sortable; Obsidian-style due dates; a check-box stamp for handwritten to-dos;
  "Add to calendar" as a one-way `.ics` / Android intent. A setting collects only lines marked as to-dos (default:
  the marker `todo:`, configurable, or all check boxes); stamped boxes always count. No scripting for now. *Built
  (qt/docs/todos.md): the index, the To-dos tab, the stamp with the handwriting's picture, Add to calendar and the
  exports. Left: the device checks (Android's calendar intent and `.ics` hand-off above all).*
- Rotating PDF pages in a `.xopp`: a turned copy in the hidden `.name.pages.pdf` (Xournal++ reads it too).
- Highlighter opacity on strokes: stays upstream's (compatibility).
- [x] `qt/toolbox`: user-defined tools as the tool bar (Drawboard-like), the default mode: each entry is a tool
  with its settings (pen, highlighter, shapes, sticky notes, …, with color, width, line style, fill), in a fixed
  order with dividers; added with "+", edited, reordered (arrows in its menu, long-press drag), the first N shown
  and the rest in a popup on smaller screens. It replaces the pen pill with its cycling width. Converge the tool
  pill of full screen and the normal tool bar into one element, docked to a side of the canvas (scrolling when
  long). Bring the Markdown document's tool bar in line with it, with fewer entries behind "»" on wide screens
  (search, full screen, …). A reading / zen mode (no edit tools; vertical and sideways scrolling, snapping or
  momentum), possibly one flow with the presentation mode.
- [x] `qt/stickers`: reusable content ("stickers", templates) per library: save a selection (ink, text, images,
  optionally a picture of the PDF behind it) to the library's sticker set, stored as `.xopp` files so they can be
  shared, viewed and copied to other libraries; a sticker tool opens a grid (last used or own order, subfolders);
  choosing one pastes it into the page, selected. Built (qt/docs/stickers.md). Left: the picker's search by the
  stickers' text; the device checks. (Pasted stickers are groups: `qt/groups`.)
- [x] `qt/audio`: recordings tied to pages or strokes, compatible with upstream's audio (`ts`/`fn` on strokes and
  texts). In a PDF with notes the audio files are attachments with the page number in their names, so they can be
  found without the app. Built (qt/docs/audio.md). Left: build and check the Qt Multimedia backend (the cloud env
  has no Qt Multimedia: add conda-forge `qt6-multimedia`), the Android/macOS parts and the device checks; the play
  tool's fading of ink without a recording, a speaker chip on pages/thumbnails, "Play from here" in the selection
  pill, a settings field for Xournal++'s audio folder.
- [x] `qt/record-place` (integration follow-up of `qt/audio`): the record button's place after `qt/toolbox` and
  `qt/stickers`. The classic bar at 1920 px has everything again (New is the tab strip's "+"); with the toolbox
  recording is a fixed tool of the rail (docked and floating), the pills stay clear of a floating toolbox. Left: the
  device checks.
- [x] `qt/hwr-multilang` (the author: "I want to support German as well … for the app we start with two models"):
  an English and a German model both run, readings merged for search (not transcription); per-document language
  detection to save CPU, with a choice per document; a CTC recogniser for the training block's models; the line
  dataset export (`xournal-qt-cli hwr-lines`) and a handwriting sample. Done (qt/docs/handwriting-search.md). Left:
  pin the German model when the training block publishes it (`ModelDownload.cpp`); the device checks with real
  models (detection thresholds 6 lines / 0.15 / 0.5 checked on the author's notes).
- [x] `qt/hwr-search`: an MVP of handwriting search on Linux from the research (`qt/docs/research/
  handwriting-recognition.md`): search only, no training on user data, fuzzy matching over the model's candidates.
  Where the results live: the library's dot folder cache, the `.xopp`, and an invisible text layer in PDFs with
  notes so other PDF viewers find the words too (the best candidate only there). Done (qt/docs/handwriting-search.md):
  ink layout, TrOCR in ONNX Runtime (dlopen), worker + indexer, `ink-text.pack`, library search, PDF text layer,
  Settings with the model's download. Left: pin the model's revision and sha256s (`qt/scripts/hwr-model.sh` prints
  them) and run the real-model device checks; bundle the runtime in packages; text layer in plain "Export as PDF";
  German (built as `qt/hwr-multilang`, above).
- [x] `qt/hwr-training`: everything to train the handwriting models on GPUs (≥ 11 GB, DDP), two models for the app
  (German TrOCR and CTC) plus a combined German + English one, user data and fine-tuning. Built
  (`qt/research/hwr/train`, CPU smoke tests). Left: the GPU runs, checking fhswf's writer ids and CVL's layout on the
  real data. The app's CtcRecognizer reads an exported CTC folder (`CtcTest.anExportedModelIsRead` with
  `XQT_HWR_CTC_MODEL`, checked 2026-10-05 with a CPU-trained CRNN).
- [x] Rotating the canvas (like Krita): the rotate gesture, reset by a double tap or the fit buttons. A feasibility
  check first. *Built as `qt/canvas-rotate` ([canvas-rotation.md](qt/docs/canvas-rotation.md)). Left: the device
  checks (pen latency turned, the touchpad's rotate direction, the on-screen keyboard), edge scrolling and pages in the
  corners at free angles.*
- [x] To-dos: how they could work (Markdown task lists, ink checkboxes, a list across the library?), whether the
  app may hand them to the system (calendars, reminders), and whether a script interpreter could ship (upstream
  has Lua plugins). *Decided and built as `qt/todos` (qt/docs/todos.md); no scripting. Left: the device checks.*

### Ideas round 2 of 2026-10-04 (the author chose from [qt/docs/ideas-2026-10.md](qt/docs/ideas-2026-10.md))
The other ideas (A3–A9, A15, B1–B8) stay in that file to decide later.
- [x] `qt/pen-gestures` (A1, A2; built 2026-10-05, [pen-gestures.md](qt/docs/pen-gestures.md); left: the device
  checklist, thresholds checked with real scratch-outs and handwriting on the Surface, the mouse and the finger have
  no gestures): **hold to straighten** (finish a stroke and keep the pen still about 0.5 s: it
  becomes a line, circle, ellipse, rectangle or triangle through upstream's shape recogniser, undoable in one step
  back to the freehand stroke; a setting) and **scratch out to erase** (a quick zigzag over ink deletes what it
  crosses, opt-in, one undo step; a zigzag over nothing stays a stroke).
- [x] `qt/md-find-replace` (A10): find and replace in `.md` files, text documents and Markdown boxes (case, whole
  word, replace one / all, one undo step for "all"). After `qt/toolbox` (the Markdown bar). Done 2026-10-05
  (qt/docs/md-editor.md, "Find and replace"); left: replacing in plain (non-Markdown) text boxes of Xournal++, and a
  phrase across formatting marks ("a **b**") is passed over.
- [x] `qt/quick-note` (A11): one tap (home screen, toolbox, tray/launcher shortcut on Android), a shortcut and
  `xournal-qt --quick-note` make a new note in the library's `Inbox/` named by date and time, in the document mode
  chosen (or append to today's `Inbox/<date>.md`, a setting). Built 2026-10-05 ([quick-note.md](qt/docs/quick-note.md)):
  home button / "+", ⋮ → Document, Ctrl+Alt+N, `--quick-note` through `SingleInstance` (also the `.desktop` file's action), an
  Android launcher shortcut. Left: the Android shortcut is untested on a device (no Android build here); no toolbox
  button (by the spec).
- [x] `qt/templates` (A12): "Save page as template" (the page's content, and if wanted its background, a PDF page
  included, so using it is the same as copying that page) into a `Templates/` folder of the library (and an app-wide
  set), chosen when adding pages (the add-page button's list, Insert pages dialog, new document). After `qt/stickers`
  (same folder model and picker). Built (qt/docs/templates.md). Left: templates of several pages, previews in the
  Insert pages / New document dialogs, the device checks.
- [x] `qt/tags` (A13): `#tag` in typed text, Markdown and sticky notes, XMP/Info keywords in PDFs; indexed per folder;
  a Tags chip in the library like Favourites (list of tags with counts, filter), `tag:name` in the fuzzy syntax,
  shown on cards. After `qt/todos` (both extend the index's notes pack). (Done 2026-10-05, [tags.md](qt/docs/tags.md);
  left: renaming a tag across the library, tags of handwriting, editing a `.md`'s front matter from "Tags…", a check
  in Zotero/Acrobat on a device.)
- [x] `qt/presenter-view` (A14): while presenting on a second screen, the laptop shows the current slide with its
  note space, the next slide, a timer and the page number; the audience screen shows only the slide. After
  `qt/toolbox` (presenting chrome). Built 2026-10-05 ([presenter-view.md](qt/docs/presenter-view.md)); left: the
  device checks (window placement on X11, Wayland and Windows, a clicker, unplugging the projector).
- [x] `qt/presenter-follow` (the author, 2026-10-05: "a toggle to show the full page in presentation as well,
  including the note page"; "can the presenter zoom and pan the page on the Beamer as well?"): a console switch and
  setting to show the audience the whole page with its space for notes (off by default), and the audience following
  the console's zoom and scrolling (on by default) with a frame on the console showing what the audience sees; Fit
  and a page change bring both back to the whole slide. Built 2026-10-05 ([presenter-view.md](qt/docs/presenter-view.md));
  left: the device checks (smoothness on a real projector, a 4K projector zoomed in far).

### Handwriting in German and English (the author, 2026-10-05)
Formats shared by training and app: [qt/research/hwr/train/FORMATS.md](qt/research/hwr/train/FORMATS.md).
- [x] `qt/hwr-training` (Python, PyTorch): datasets (fhswf German, synthetic German/English, IAM only from the
  author's own copy, xournal-qt ink datasets), TrOCR-small (from the handwritten model) and a small CTC model, DDP with
  torchrun on GPUs with at least 11 GB, configs for a German model, one combined English+German model and fine-tuning
  on user data, evaluation (words found, CER, WER per dataset, language and writer), export to the app's model folder.
- [x] `qt/hwr-multilang` (app): English / German / both; several models with merged readings; a CTC recogniser;
  per-document language detection and override; `xournal-qt-cli hwr-lines` exports ink lines as a dataset.
- Licences don't restrict the choice of datasets or base models (the author, 2026-10-05: a free, non-commercial
  research app); they are recorded for transparency only.
- [ ] `qt/hwr-userdata` (later, lower priority): a dataset of the user's own hand made in xournal-qt (prompted
  sentences to write, corrections of readings), exported for fine-tuning.

### A document timeline (the author, 2026-10-05: B9 levels 1 and 2 as one design; `qt/timeline`)
"A play bar at the bottom, like an audio player, that replays the audio and replays the document editing history in
read-only mode." Decided: levels 1 and 2 of [ideas B9](qt/docs/ideas-2026-10.md) together, on one clock.
- [x] Every new element (stroke, text, image, TeX image, link) gets its creation time, an absolute time saved as the
  element attribute `xqt-created` (a seam like `xqt-group`; upstream ignores and drops it). Eraser pieces and moved,
  recoloured or resized elements keep their time; pasted elements and stickers are new (the time they were pasted).
  Built 2026-10-05 ([timeline.md](qt/docs/timeline.md)); left: the device checks (Xournal++ 1.2/1.3 opening such files).
- [x] One timeline per document: elements ordered by creation time; recordings placed on it by their start time, so
  the audio plays where it overlaps (the strokes' upstream `ts`/`fn` place elements of Xournal++ files with a
  recording); long pauses between sessions compressed (a session mark on the bar). Elements without any time (older
  files, files saved by Xournal++) come first, in the order they sit in their layer.
  Built 2026-10-05; left: the device checks with real recordings (a Xournal++ lecture with its audio).
- [x] Replay, read-only: a play bar at the bottom (play/pause, scrub, speed, jump to the session marks), the pages
  drawn as of the bar's time, the stroke being written drawn on progressively (evenly along its length: points have
  no times). Tapping a stroke jumps there. Leaving it brings the document back as it was; nothing is changed.
  Built 2026-10-05; left: smoothness on the Surface and a phone, speech at other speeds than 1× (silent there).
- Not in this block: erasing, moving and page changes (level 3, with the version history); per-point times.

### Faster PDF saves, then a PDF-only mode (the author, 2026-09-24)
1. [x] **`qt/pdf-incremental`: incremental saves for hybrid and archive PDFs** (merged 2026-09-24; left: a message
   when a save falls back to a full write, and a check in MuPDF and pdf.js).
   - Ctrl+S appends only what changed (standard PDF incremental update, ISO 32000): the changed layer annotations
     or ink streams, the embedded `.xopp`, the catalog marker, and a new cross-reference section matching the file's
     style (a table, or a stream after an xref stream) with `/Prev`. The original pages are never rewritten. This is
     how Acrobat and Drawboard save.
   - Automatic compaction: a full rewrite when the appended part exceeds about 25% of the file, and on Save as.
   - Share, Export and Archive export always write a fresh, compacted file, because older versions inside an
     incrementally saved file can still hold deleted ink (privacy).
   - PDF/A-2/3 allow incremental updates; archive files must stay valid after them (veraPDF in CI).
   - The clean background copy is kept across incremental saves, since the base pages do not change.
   - Verify: qpdf `--check`; poppler, MuPDF and pdf.js render the same after many incremental saves; size growth and
     compaction; save time on pgfmanual before and after.
   - qpdf cannot write incremental updates: our own small appender, with objects serialised through qpdf.
2. [x] **`qt/pdf-only`: a mode where every document is a single PDF**, with no sidecars (merged 2026-09-24).
   Left: on Windows, the rename over a PDF another program holds open (retry or `ReplaceFileW`); a UI to restore the
   original kept in the cache.
   - New documents are hybrid `name.pdf`. Annotating an existing PDF writes into that PDF, the Drawboard way.
     Pasted pages go into the PDF, and images are inside the Xournal data. Nothing is written next to files;
     autosave and recovery stay in the app's cache.
   - **Asked at the first start**, explaining what each means, with a recommendation:
     - "PDF files (like Drawboard PDF, GoodNotes, Xodo)": every document is one PDF that any app opens. Recommended
       for most people.
     - "Xournal++ files (like Xournal++)": `.xopp` notes next to their PDFs, fully compatible with Xournal++.
       Recommended if you also use Xournal++.

     It can be changed later in Settings → Documents. Xournal++ exports stay available through Share → "For
     Xournal++".

### Setsquare and compass on high-DPI screens (the author, 2026-09-24)
- [x] **`qt/geometry-gpu` (merged 2026-09-24): the setsquare lags on the Surface Pro 8** (2880×1920 at 200%) once it is larger than
  about 10 cm; it is smooth on the full-HD Linux screen.
  - Cause: upstream's `SetsquareView`/`CompassView` draw with cairo on the CPU into the page overlay, including every
    mm tick and number as text, and the old and new bounding boxes are redrawn on every move and turn. The cost grows
    with (size × zoom × screen scale)²: 4× the pixels per cm at 200% versus 100%.
  - Fix: draw the tool once into a texture at the current zoom and scale; move and turn it as a GPU transform
    (`QSGTransformNode`) during interaction; draw it again only on a zoom or size change, and once after a turn ends,
    for sharpness.
  - Measure on Linux with `QT_SCALE_FACTOR=2` and `XQT_PERF=1`: frame times before and after, for a 15 cm setsquare
    moved and turned.

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
- [x] `AdaptiveLayoutTest.theHomeScreensPlusAndViewMenusWork` and `menusAreSheetsOnPhones` failed on every CI run
  (2026-09-27, release v0.4.0), not from timing: reproduced in the CI's containers with `qt/scripts/ci-container.sh`
  (`qt/ci-green-2`). neon: the fonts made the layout menu 261.92 px wide, and its columns row put its last button on
  whole pixels, 0.08 px outside (menus are whole pixels wide now). Debian (Qt 6.8): its file dialogs are windows of
  their own, and off-screen the app's window never got the keys back, so Esc did not reach the sheet; and its menus
  ignore a click on their button while they fade out. The test re-activates the window and waits for the menu.
- [ ] The CI still repeats a failed test up to twice (`--repeat until-pass:3`). Under load (two container runs at
  once) `CitationsTest.selectedTextIsSearchedInTheDocumentTheTabsAndTheLibrary` and
  `PhoneChromeTest.thePresentationTapField` failed once each (5 of 5 alone): harden them to wait for the state, then
  drop the repeat.
- [x] `CanvasMemoryTest.twoViewsOfOneDocumentShareTheLimit` was a real overshoot: renders a trimmed view started
  earlier landed after the trim uncounted. Fixed in `qt/two-views-memory` (2026-09-26): a trimmed view re-plans on
  every render that lands.
- [x] `MainWindowTest.theSelectedPdfTextTakesItsHandlesAndActionsAlong` failed once in the full suite under
  `-j6` load (2026-09-24), at the check after "the way back brings it into view again". It passed 3 of 3 alone.
  The wait for the scroll back is probably too short under load.

- [x] `MainWindowTest.sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown` ("no sharp one while racing")
  fails now and then under load: 1 of 8 and 0 of 16 after `qt/present`, and once in the full suite; 0 of 16 on a
  build from before it. It is timing-sensitive, and whether `qt/present` made it more likely is not settled.

- [x] `LibraryTest.renamedAndMovedDocumentsKeepTheirIndex` failed 1 of 4 runs on 2026-09-24 under load. Fixed in
  `qt/index-rename-race`: an update still running when the app moved a folder replaced the entry of a document
  that vanished under it with an empty one; now such an entry stays for the move, and a document without an entry
  takes over a gone one with the same size, time and name or content sample. 40 of 40 under load; the CI retry is
  gone.

- [x] **The UI tests that used fixed waits** are hardened (`qt/test-waits`, merged 2026-09-24).
- [ ] `MainWindowTest.postersAndFlashcardsFromTheDialogs` leaves the shared page template at 300×500, so
  `emojiInTheMarkdownEditorBesideThePage` and `emojiAreInTheExportedPdfInColour` fail when they run after it in the
  same process (both pass alone; found by `qt/undo-redo`, 2026-10-04). Restore the template in the test.
- [ ] Under `-j3` load (cloud build, 2026-10-04) `PhoneChromeTest.theFold7FoldedAndUnfolded`,
  `AdaptiveLayoutTest.toolBarPlaceIsChosenPerSizeClass` and `SafeAreasKeyboardTest.theFormatBarDocksAboveTheKeyboardAndTheCursorStaysInView`
  failed once each and passed alone (also `ColorChooserTest.theHighlighterTakesHighlightColors`, 3 of 3 alone);
  `CitationsTest.selectedTextIsSearchedInTheDocumentTheTabsAndTheLibrary` failed
  about 1 in 4. Wait for the state instead of time. Again on 2026-10-05 (`ctest -L 'hwr|shell|ui' -j3`, after
  `qt/hwr-multilang`): `theFold7FoldedAndUnfolded`, `AdaptiveLayoutTest.classesSidebarAndControlsAtFiveSizes`,
  `AdaptiveLayoutTest.colorsAndWidthsTakeTheRoomThereIs` and
  `ToolboxAudioTest.recordingIsAFixedToolOfTheRailAndItsPillStaysInSight` failed together and passed 4 of 4 alone.
- [ ] `Tabs.closingATabDoesNotWaitForQueuedWork` checks a fixed time limit for closing a tab: it failed once in the
  full suite at a load of about 15 and passed 6 of 6 alone. Make its limit relative (for example to one render's
  time), or measure the waiting rather than wall time.

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
