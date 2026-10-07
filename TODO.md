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
- [ ] Canvas: `renderZoom`/`renderDpr` read as one (infra §6.4); the quick tests' input on the canvas clock
  (`CanvasItemInputTest`); a ThreadSanitizer run of the canvas and shell labels.
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

- [ ] **Android: the top bar scrolls into a blank area at its end** (about the last quarter; the author on 0.8.0).
  Not reproduced off-screen. Likely the buttons pinned before "+" (`Toolbox.leadingTail`) take room while hidden.
  Needs the device (adb, a screenshot scrolled to the end): postponed by the author.
- [ ] **A touch on the "pages with hits" filter can make the maximized window half as high** (old, touch only,
  KWin; also the page grid button). Suspect a touch whose item disappears mid-touch, taken by KWin as a window
  gesture. Next time: `XQT_LOG_WINDOW=1`, look for a touch cancel before the resize.
- [x] A test leaves a settings file `non-existing-file-path` in the working directory: the unit tests now run in
  `build-qt/unit-cwd` (`qt/test-support`). Nothing left.

## Flaky tests
- [ ] Under heavy load (four agents building, 2026-10-07) `StickerToolTest.theSelectionBecomesAStickerOfTheLibraryAndIsOnTheClipboard`,
  `ToolboxTest.aToolHeldThenMovedIsCarriedToAnotherPlace`, `AdaptiveLayoutTest.theHomeScreensPlusAndViewMenusWork` and
  `ColorChooserTest.theHighlighterTakesHighlightColors` failed once each in a full suite; all pass alone.
- [ ] `PageFilesTest.aProtectedDocumentIsExtractedProtectedAndNeverAsXopp` fails about 1 in 3 runs alone (6 of 20 on
  the base of `qt/compat-dead`, 2026-10-07): extracting pages of the protected `locked.pdf` reads it with the wrong key
  at times (qpdf: "/Perms field in encryption dictionary doesn't match expected value"). A race on the file's password
  (PdfEncryption) or on the clean copy; find it.

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
- [ ] Pin the models' revisions and sha256s (`qt/scripts/hwr-model.sh` prints them), the German one when the training
  publishes it (`ModelDownload.cpp`); bundle ONNX Runtime in the packages; the text layer in plain "Export as PDF".
- [ ] The GPU training runs (`qt/research/hwr/train`), checking fhswf's writer ids and CVL's layout on the real data.
- [ ] `qt/hwr-userdata` (later): a dataset of the user's own hand made in the app, for fine-tuning.

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
