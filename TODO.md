# TODO

Open work only. The goals behind it are in [VISION.md](VISION.md); how the app works now is in the docs
([qt/docs/README.md](qt/docs/README.md)); what was built is in the [release notes](qt/docs/release-notes/) and in git.

**How to use this file:**
- Each **block** is one branch `qt/<block>` in its own worktree `../xournal_qt-<block>`, merged into `master-qt` by
  the integrating session ([workflow.md](qt/docs/development/workflow.md)). Within a block, each item is one commit.
- Markers: `[ ]` open · `[~]` in progress (write the branch next to it) · `[x]` done in a block, with a one-line
  note of what is left · `[?]` needs a decision from the author.
- When a block is merged, the integrator **deletes** its `[x]` items (what is left stays as new `[ ]` items): the
  behaviour is in its feature doc, the user-visible change in the next release notes, the rest in git.

---

## The refactoring of 2026-10

The plan, the five reviews and the reasons: [qt/docs/review/2026-10/README.md](qt/docs/review/2026-10/README.md).
All four waves are merged (2026-10-07). The architecture overview:
[qt/docs/architecture/README.md](qt/docs/architecture/README.md), generated from `architecture.yaml` by
`qt/scripts/architecture/generate.py` (`--check` runs in CI).

### What the merged blocks left (checked against the code, 2026-10-07)
- [ ] App: the canvas names with rules of their own still on `app`: `copySelection` (the reference's own copy),
  `pasteElements` (a text file's fixed pages, the reference only when written in), `selectAllOnPage`,
  `clearSelection`, `navigateBack/Forward` and `canGoBack/Forward` (across documents: AppLinks), `goToPage`
  (test-only), and the per-document properties (`pageNumber`, `pageCount`, `zoomPercent`, …): with their feature
  objects (app-cpp E–N, which get a `WindowContext`). MoreMenu/AppButtons/ToolboxMenus still name dialog ids (qml B3:
  `actions.*` for them).
- [ ] Shell: the favourite as a role of `RecentFiles` (the Recent grid still asks `app.isFavouriteFile` with
  HomeView's `favouriteRevision` counter); `Stickers`' `CoverRole` is still "preview" for the sticker picker.
- [ ] Shell: `readJsonObject` (`shell/JsonFile.h`) next to `fileio::readFile`, and PdfHistory's `gzip`/`gunzip`
  internal, once the session block is merged; "preview" in MainWindowTest test names (with the tests block).
- [ ] Session: **the plain autosave still runs on the UI thread** (risk 7; `DocumentSession::autosave` builds the XML
  under the read lock and gzips and writes it there). Not moved in `qt/session-rest`, because a worker needs: (1) a
  synchronous path kept for `AppController::autosaveAll` when the app goes to the background (Android may kill it
  right after; `RecoveryTest.goingToTheBackgroundWritesTheAutosaves` checks "autosaved at once"), so `autosaveChanges`
  needs a "now"/"in the background" choice from its two callers; (2) the copy taken as a save does (`snapshotOf`
  under the read lock, `PictureSaveHandler` on the worker, `updateDocumentInfo` back on the UI thread under the lock,
  as `finishWrite`); (3) an order with saves: an autosave written after a Ctrl+S that started later is newer on disk
  than the saved file but older in content, and recovery would offer it, so it is dropped in its last step when a
  save finished since its copy; (4) the session's destructor waiting for it; (5) `undoRedo->documentAutosaved()` only
  for a copy that is written. Also: `prepareSave` writes a missing attached `name.xopp.bg.pdf` with poppler under the
  read lock (`SaveHandler::visitPage`); the save copies it on its worker instead (`writeAttachedPdf`), the autosave
  should too.
- [ ] Canvas: upstream's elements compute their size lazily (`Element::getBoundingBox` → `calcSize`, `mutable`) also
  under the shared document lock: two render threads, or a render and the UI thread copying a page for a save
  (`Text::clone`), compute it at once. Same values and the flag set last (the fork's seam), but a data race
  (suppressed in `qt/tests/tsan.supp`). Fix: compute the size before an element is shared, or a seam that makes the
  cache safe.
- [ ] Tests: ThreadSanitizer does not check `QThreadPool` jobs against the thread that started them, nor calls queued
  to another thread (Qt is prebuilt without TSan, its hand-off invisible; suppressed). A Qt built with
  `-sanitize thread`, or `__tsan_release`/`__tsan_acquire` at the hand-off of `ImageWorkers`/`BackgroundJobs`/the
  session's worker, would check them. Optionally a CI job (about 15 min for canvas and shell on 16 cores).
- [ ] Upstream: `QPdfExport::overlayAndSave` gives `QPDFWriter` a temporary file name (`u8string().data()`); qpdf keeps
  the pointer and reads it for the `/ID` (valgrind: a read after free). Offer the fix upstream (the fork's own writers
  were fixed in `qt/canvas-rest`).
- [ ] Tests: the `qpdfCheck` (10), `makePdf`/`drawStroke`/`addStroke` variants and the quick tests' `wait` copies into
  `qt/tests/support`; the fixed "settle" waits without an observable state (`XQT_WAIT_LOG` ranks them:
  AdaptiveLayoutTest's `resize` 150 ms and `click` 50 ms, the SetUp waits of MainWindowTest and ToolboxTest); a
  `slow` label and dropping the CI's `--repeat until-pass:3` (infra B2 4–5); `MainWindowTest` split by feature.
- [ ] Docs: **the author enables GitHub Pages once** (Settings → Pages → Source: GitHub Actions; a private
  repository needs a paid plan, [ci.md](qt/docs/development/ci.md)), then `xqt-pages.yml` publishes the interactive
  architecture page.
- [ ] Docs: the settings keys table in `architecture/data-on-disk.md` once they are typed (B13); the macOS, Windows and
  Android paths there tried on a machine.

### Later rounds (from the reviews, not started)
- [ ] The feature objects out of `AppController` (app-cpp E–N, `app.versions` first as the template) · `CanvasView`
  steps (infra B8, B9) · the `CanvasInput` split (B11) · non-view code out of `canvas` (B12) · `DocumentSession` split
  (session 4) · one PDF-writing entry point (session 7) · `LibraryService` and a list-model base (shell 6–8) · typed
  settings (shell 10, infra B13) · module-qualified includes and one CMake file per module (infra B4, B5; shell 13) ·
  `DocumentCanvasItem` split (app-cpp O) · shared small QML components (qml B11).

## Decisions for the author
- [ ] Reading library cache packs from the other cache location (shell review §4 #15) stays: it also serves read-only
  folders with a cache of their own. Remove it?

- [?] **PDF engine**: MuPDF (AGPL) or stay on poppler? A branch `qt/mupdf` (not merged) had MuPDF behind
  `-DXQT_WITH_MUPDF=ON`: faster at 1× (2× on text, 4.5× on scans) and with threads, not faster at 4×, about twice the
  memory; pdfium has one global lock. Proposal: retest with MuPDF 1.26 after the HiDPI fix, then decide. Before
  comparing: `PdfCache` holds its lock for a whole render, so one view's visible pages are drawn one after another.
- [?] Highlighter opacity per stroke (0.8 on dark paper): needs an upstream seam; today strokes keep upstream's
  fixed opacity.
- [?] Two-column Markdown (`<!-- xqt:columns 2 -->`): researched, doable but not easy (pagination, search and the
  editor's hit test need column cases); not built.
- [?] Qt's own `ToolSeparator` / `MenuSeparator` stay uneven at fractional scales.

## Bugs

- [x] **Pasted text was white** (or yellow) when the tool in hand was not the pen (the author, 2026-10-08: handwriting
  copied as text, then Ctrl+V): a plain-text paste took the color of the tool in hand; it takes the text tool's now
  (`qt/paste-colour`). Left: a Markdown box started in the panel (`AppController::startMarkdown`) and a new plain
  text of `TextEditor` still take the color of the tool in hand; both are reached with the text tool in hand, except
  the sticky note pill's "Text" with a select tool in panel mode. Untested: make them the text tool's too, with a test.
- [ ] **Android: the top bar scrolls into a blank area at its end** (about the last quarter; the author on 0.8.0).
  Not reproduced off-screen. Likely the buttons pinned before "+" (`Toolbox.leadingTail`) take room while hidden.
  Needs the device (adb, a screenshot scrolled to the end): postponed by the author.
- [ ] **A touch on the "pages with hits" filter can make the maximized window half as high** (old, touch only,
  KWin; also the page grid button). Suspect a touch whose item disappears mid-touch, taken by KWin as a window
  gesture. Next time: `XQT_LOG_WINDOW=1`, look for a touch cancel before the resize.
- [ ] **Qt 6.8.4 (conda-forge, QML compiled ahead of time): two UI tests fail every time**, on 0.8.0's `master-qt`
  too; the CI's Debian 13 (Qt 6.8.2) passes them. `HomeScreenTest.theFavouritesChipShowsOnlyStarredDocuments`
  (in MainWindowTest.cpp) segfaults at its start, with nothing logged; `ToolboxTest.theTopBarShowsTheStoredArrangement
  AndScrolls`: at 1000 px the top bar does not scroll (`scrolls` false, no end fade). Both pass on 6.9.3 and 6.11.2.
  Build: `~/xqt-env68` via `XQT_QT_SPEC=qt6-main=6.8 qt/scripts/cloud-env.sh`, `-DXQT_FAST_DEV=OFF`. gdb needs ptrace,
  which the training server does not allow.
- [ ] **Qt 6.11.2** (the Android build's Qt, conda-forge on the desktop): the whole suite passes but
  `AdaptiveLayoutTest.theHomeScreensPlusAndViewMenusWork`, every time: at 412×915, after "Open a file…" the star
  does not switch Recent to the library's favourites (`page` stays 1); the off-screen file dialog likely still takes
  the click. Also logged there and on 6.9: `StickerPicker.qml:46/47` "Cannot read property 'width'/'height' of null".
- [ ] Nothing runs the tests on the Qt the Android APK ships (6.11.2): the Back key bug fixed for 0.9.0 (Qt 6.11
  lists the Back key under `QKeySequence::Back`) showed only there. A CI job (conda-forge's Qt 6.11 or aqt) for the
  `ui` and `shell` labels would catch the next one.

## Flaky tests
- [ ] Under heavy load (four agents building, 2026-10-07) `StickerToolTest.theSelectionBecomesAStickerOfTheLibraryAndIsOnTheClipboard`,
  `ToolboxTest.aToolHeldThenMovedIsCarriedToAnotherPlace`, `AdaptiveLayoutTest.theHomeScreensPlusAndViewMenusWork` and
  `ColorChooserTest.theHighlighterTakesHighlightColors` failed once each in a full suite; all pass alone.
- [ ] `PageFilesTest.aProtectedDocumentIsExtractedProtectedAndNeverAsXopp` fails about 1 in 3 runs alone (6 of 20 on
  the base of `qt/compat-dead`, 2026-10-07): extracting pages of the protected `locked.pdf` reads it with the wrong key
  at times (qpdf: "/Perms field in encryption dictionary doesn't match expected value"). A race on the file's password
  (PdfEncryption) or on the clean copy; find it. Not the dangling file name of `QPDFWriter` (fixed in
  `qt/canvas-rest`): it still fails after that, and the TSan run shows no race in it.

Rerun a failure alone before calling it a flake; harden a test by waiting for the state, not for time.

- [ ] The CI repeats a failed test up to twice (`--repeat until-pass:3`): drop it once the suite passes 5× under
  load (infra B2).
- [ ] Failed once each under load, pass alone: `CitationsTest.selectedTextIsSearchedInTheDocumentTheTabsAndTheLibrary`
  (about 1 in 4 under `-j3`), `PhoneChromeTest.presentingWithoutControlsHasTheZenDot`,
  `PhoneChromeTest.theFold7FoldedAndUnfolded`, `AdaptiveLayoutTest.classesSidebarAndControlsAtFiveSizes`,
  `AdaptiveLayoutTest.menusAreSheetsOnPhones`, `AdaptiveLayoutTest.theFloatingToolboxFitsAShortWindow`,
  `SafeAreasKeyboardTest.theFormatBarDocksAboveTheKeyboardAndTheCursorStaysInView`,
  `ColorChooserTest.theHighlighterTakesHighlightColors`,
  `ToolboxAudioTest.recordingIsAFixedToolOfTheRailAndItsPillStaysInSight`,
  `MainWindowTest.theSelectedPdfTextTakesItsHandlesAndActionsAlong`,
  `MainWindowTest.sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown`,
  `TextPdf.picturesAreCarriedInsideAPdfTextDocument`.
- [ ] `MainWindowTest.postersAndFlashcardsFromTheDialogs` leaves the shared page template at 300×500, so
  `emojiInTheMarkdownEditorBesideThePage` and `emojiAreInTheExportedPdfInColour` fail after it in the same process.
- [ ] `Tabs.closingATabDoesNotWaitForQueuedWork` checks a fixed time limit; make it relative to one render's time.
- [ ] `StickerFileTest.thePictureBehindLiesAtTheBottom` fails in the full session run, passes alone (test order).

## Open follow-ups, by area

### Saving and PDFs
- [ ] A crash save while a paste merge is pending: the recovered pasted pages show "PDF background missing".
- [ ] Autosave still runs on the UI thread; a save that drops unused pages reloads the PDF on the UI thread; a paste
  merge runs on the UI thread (about 0.2 s per paste on a 117 MB scan).
- [ ] Thumbnails of just-pasted pages stay white, and their text unsearchable, until the merge is done.
- [ ] Hybrid PDFs: a page deleted in another app; rotated or cropped source PDFs (untested); the round trip in other
  viewers (Acrobat, Preview, Xodo, Drawboard, pdf.js, Okular, Evince: the author, on devices); whether an embedded
  `.xopp` survives saves in other apps.
- [ ] Windows: writing into a PDF renames over it while it may be open elsewhere (retry or `ReplaceFileW`); the
  `.next.pdf` step relies on Linux rename semantics.
- [ ] A message when an incremental save falls back to a full write; a UI to restore the original PDF kept in the
  cache (PDF-only mode).
- [ ] Bookmarks: write the current ones into the outline of a plain PDF export; read back bookmarks changed in other
  PDF apps (today the embedded `.xopp` wins).
- [ ] Version history: compare and play on the timeline's play bar; "Save a copy with history"; pruning if real
  files need it; on a changed page, what was added and removed highlighted (the signatures exist in `VersionDiff`).
- [ ] The library index: a hybrid flag, so same-name `.xopp` + PDF pairs need no qpdf check per listing.

### Library and search
- [ ] Reading positions are keyed by the library's path: a library renamed outside the app starts without them.
- [ ] Dropping a file of a hidden kind says "not a document the library shows" instead of which filter hides it.
- [ ] A `.md` changed by another program is picked up only at the next library refresh; page operations on a
  read-only `.md` still work and saving makes a `.xopp`.
- [ ] Search: a password-protected PDF's text is not searched in the open document (fall back to its own instance);
  `HitPages` still uses poppler's `findOnPage` and library element text includes Markdown source and hidden layers
  (move both to `TextMatch`); every tab reads its PDF text 2 s after opening; the tab overview places hits on the
  first 24 pages only; hit pictures mark `^`/`$`/`'word'` as plain substrings; the search bar shows no sign of fuzzy
  mode; the first fuzzy search of a big document builds its vocabularies on the UI thread (~160 ms for 1,300 pages).
- [ ] Links: a `pdfid=` key for the PDF `/ID` search; drag and drop onto the page; rewriting links in hybrid PDFs and
  `.xoj` files that are not open; link boxes on rotated pages of hybrid PDFs.
- [ ] Tags: renaming a tag across the library, tags of handwriting, editing a `.md`'s front matter from "Tags…".
- [ ] Favourites: remember the chip across starts; a star in the tab overview for annotated PDFs without a path.

### Markdown and text
- [ ] A Markdown box written while recording is not tied to the recording (plain new texts were; since 0.9.0 the text
  tool always writes Markdown).
- [ ] **Vaults** (Obsidian, Zettlr, foam), decided 2026-09-24: detect a `.obsidian/` folder and use its attachment
  folder; files with Obsidian-only syntax ask once before editing; resolve `[[wikilinks]]` by file name; backlinks
  later.
- [ ] Bookmarks in Markdown: on a continuous page, go to the comment's place; Markdown boxes and flows that start
  after page 1 of a `.xopp`.
- [ ] Find and replace in plain (non-Markdown) text boxes; a phrase across formatting marks ("a **b**").
- [ ] Citations: look-up in the Markdown source panel; `.bib` files.
- [ ] Table column widths can differ per page of a table split over pages.

### Tools and canvas
- [ ] Pen styles: change the line style or filling of a selection; a fill color for the highlighter.
- [ ] Curtain: QML overlays (selection pill, PDF text knobs, sticky note pill) still show over it.
- [ ] Note space: geometry tools do not move with the slide; PDF pages whose crop box is smaller than their media box
  show the bleed in other viewers.
- [ ] Snip: no link offer in the Markdown panel beside the page; a snip stays on one page.
- [ ] Groups: nested groups, groups across layers, sticky notes in a group.
- [ ] Canvas rotation: edge scrolling and pages in the corners at free angles.
- [ ] Touch multi-select: a rectangle in the mode only adds; long press does not toggle.
- [ ] Copy handwriting: no live marking of the words during the sweep; "Copy as text" on the reference's pill.
- [ ] Top bar: dragging out of the "+" catalog onto a bar (a tap places); carrying to the top bar in full screen.
- [ ] Zen: the page number opens the page grid rather than "Go to page".
- [ ] Templates of several pages; previews in the Insert pages / New document dialogs; the sticker picker's search
  by the stickers' text.
- [ ] Audio: the play tool's fading of ink without a recording, a speaker chip on pages, "Play from here" in the
  selection pill, a setting for Xournal++'s audio folder.
- [ ] Dark pages: "print with white pages"; thumbnails turn their pictures dark too.
- [ ] The onboarding tutorial: the author's screenshots and ink in place of the `PLACEHOLDER` quotes of
  `qt/resources/help/tutorial.md`.

### Adaptive UI
- [ ] The bottom sheet keeps Material's rounded corners at the bottom and opens with the grow animation; the Settings
  sheet is its own popup; the Markdown table editor and the small anchored popups (link, page jump, custom width) are
  not adapted; unchecked choices show no box in a sheet.
- [ ] The format bar scrolls below ~680 px on a desktop; the Markdown panel's rows are not smaller on a phone.
- [ ] A list view of the library for phones; the tab menu (rename, reference, share) from the phone's app bar.
- [ ] The emoji button is out of reach while the soft keyboard is open on a phone.

### Handwriting
- [x] `qt/hwr-library-search`: the library's search did not find handwriting found in open documents (the author,
  2026-10-08). Open documents now go to the library's cache once read (and on saving, what is read so far); a
  device's battery no longer stops the background reading. Left: the author's check with real notes on the device;
  a document opened under another spelling of the library's path (a symlink) is still not matched to its entry.
- [ ] Handwriting at an angle, what is left: writing leftwards (upside down) is read as level; a column of single
  letters or digits can pass for a line written downwards; poppler splits words at a free angle into letters
  ("K a l m a n"; 90° is fine); a level or 90° line turned by the lasso is read again; check real angled handwriting
  on the device (handwriting-search.md, "On the device").
- [ ] The bundled handwriting model (`crnn-de-en`, ONNX Runtime 1.30.0 in every package), what is left: the first
  runs of `xqt-release.yml`, `xqt-windows.yml`, `xqt-macos.yml` and `xqt-android.yml` with it (their `--hwr-info`
  smoke tests); the device checks (handwriting-search.md, "On the device": Android's first start copying the model,
  dlopen by name; macOS and Windows). The APK keeps its native libraries uncompressed (the author, 2026-10-08: less
  space on the phone and fast updates over 20 MB less download; `QT_ANDROID_LEGACY_PACKAGING` if that changes).
- [ ] Later, if package size matters: a minimal ONNX Runtime build with only the operators the model uses (the full
  Android library is 33 MB; a reduced build is often a few MB), built in the CI per platform and redone when the
  model's operators change. Microsoft's ORT builds are 16 KB page aligned (checked for the Android AAR).
- [ ] The text layer in plain "Export as PDF".
- [?] **Text or drawing, and writing of any size** (the author is unsure how stable a size-aware layout would be;
  nothing changed yet). Today `InkLayout` calls a stroke a drawing when it is filled, taller than 2.5 times the
  page's letter height, or long and straight: big writing (headlines) is dropped, and curvy letter-sized drawings
  (circles around words, sketches, hatching, formulas) are read as text, their nonsense readings kept as weak
  candidates (the text layer's MIN_CONF keeps them out of PDFs; the in-app search takes readings from 5 %). The robust
  direction: geometry only proposes lines at their own scale, the small CTC model (about 35 ms a line) reads them,
  and its confidence accepts or rejects them. First a labelled set of real pages (diagrams with labels, headlines,
  margin notes, formulas: "text here / drawing here", a dozen of the author's pages) to measure text found and
  nonsense admitted; without it no change.
- [ ] Later, if reading speed matters: **static int8 quantization** of the CTC model. The export quantizes
  dynamically, so only the LSTMs and the output layer are int8 (`DynamicQuantizeLSTM`, `MatMulInteger`) and the six
  convolutions stay fp32: int8 is 6-14 % faster than fp32 on an AVX2 CPU without VNNI (measured on a Zen+, the German
  CTC: 30 against 34 ms for a 400 px line on 2 threads) and 2.5 times smaller, with the same accuracy. Static
  quantization (QDQ with a calibration set) would make the convolutions int8 too, which VNNI desktops (Intel Alder
  Lake+, AMD Zen 4+) and ARM's dot product and i8mm (Apple M, Snapdragon, phones) run much faster; check it with
  `evaluate.py` on the test sets.
- [ ] CTC word boxes: measure again on a larger real hand (only 13 distinct lines differed in test/files:
  `CtcTest.wordBoxesOfRealInk` with `XQT_HWR_BOXES_FILES`).
- [x] `qt/hwr-bench`: handwriting forms as data and benchmark, `xournal-qt-cli hwr-form` and `hwr-bench`
  (handwriting-search.md, "Forms and the benchmark"). Left: run both on the forms block's real PDFs and filled forms
  (only synthetic forms and the benchmark page's line in turned boxes so far); what the rotation benchmark shows
  (`FormTest.theBuiltInModelReadsRealInkInTurnedBoxes`, 2026-10-08): at ±90° the layout takes a piece of the line as
  level (2 lines, one at 0°), at 15° the line falls apart into 6 level lines, 180° is read as level (nonsense).
- [ ] `qt/hwr-userdata` (later): a dataset of the user's own hand made in the app, for fine-tuning.
- [x] `qt/hwr-forms`: the handwriting forms (`qt/research/hwr/forms/`: generator, `pdf/xqt-hwr-en.pdf`, `-de`,
  `-en-de`, manifests embedded). Left: fill one on the device and time it (15 pages came out longer than the
  plan's 8 to 10); the app side (`hwr-form`, `hwr-bench`) must read the manifest's new optional `in` (DESIGN.md).

### Platforms
- [ ] Android ([roadmap](qt/docs/development/android-roadmap.md)): some texts miss “ and — (probably the symbol
  fallback font); the tab strip does not scroll to the current tab after a reload; a Recent card drawn while access
  was missing stays blank; Google Play needs another way than `MANAGE_EXTERNAL_STORAGE`; SD cards as a home; the
  share sheet for a shared zip (today "Save a copy…"); exporting pictures into a `content://` folder; no zip password
  (Android's libzip has no crypto); after the move on the Fold 7, switch to the release-signed APK.
- [ ] Windows ([roadmap](qt/docs/development/windows-roadmap.md)): a pressure calibration in Settings → Pen; an
  installer; the author's `qt/windows-feel` on the Surface.
- [ ] Platform builds run only at release: a weekly `schedule:` or a `master-qt` push filter (infra §8).

## Not started (decide before building)

- [ ] Forms (only on PDFs that have fields) and a "My signature" stamp; cryptographic signing (backlog).
- [ ] OCR (Tesseract): photo import with cropping, a text layer in PDFs; never automatic.
- [ ] Selected elements in autosave and crash saves (upstream holds them in the selection while it exists).
- [ ] Touch-sized selection handles of upstream's tools in touch mode.
- [ ] A visual text diff between PDF versions.
- [ ] EPUB and CBZ as documents (only with MuPDF).
- The ideas the author did not choose in the round of 2026-10-04 (A3–A9, A15, B1–B8): see
  [qt/docs/history/README.md](qt/docs/history/README.md), "Where the old notes went".
