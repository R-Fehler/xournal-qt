# Review: infra (canvas, tests, build/CI/scripts, docs)

Reviewed 2026-10-07 on `claude/admiring-pascal-hekja6` (= `master-qt` d6d16ba). Nothing in the repo was changed.
Numbers come from `wc`, `grep`, the CMake files, `ctest -N` (run with `/opt/xqt-env/env/bin/ctest`, see 3.6)
and `build-qt/Testing/Temporary/CTestCostData.txt`. That file holds the average time of each test over the last runs.

## Verdict

The infrastructure works, but it has grown by adding things and never by consolidating them. Four problems stand out.

- **`qt/src/canvas` is not a module.** It is one god object, `CanvasView`, with nearly every feature attached to it:
  - 5 base classes, ~120 public methods, 30 signals, 838 + 3614 lines plus 3 satellite .cpp files.
  - A second one, `CanvasInput`, has a 406-line `touchEvent` and ~70 state members.
  - Next to them sit ~6k lines of text-editing code and document operations that have nothing to do with a view.
    The shell uses some of them as a file format.
- **The tests are thorough (1865 cases) but slow and copy-pasted.**
  - The UI label alone needs ~1720 s of CPU time (median 3.8 s per test).
  - 26 files each define their own `wait(int ms)` busy loop, and there are ~1000 fixed `wait(N)` calls.
  - The `Main.qml` fixture is copied 23 times and has already drifted from `main.cpp`: 21 of 23 copies miss the
    `annotation` image provider.
  - `waitFor` / `makeTextPdf` / `writeFile` exist 19 / 17 / 15 times, with different signatures and timeouts.
- **The build files** repeat the same 6-line test stanza 9 times and keep two modules in one file
  (`XqtSession.cmake` defines session, canvas and both test binaries). `xqt-shell` also compiles all of `src/app`. The
  declared minimums are wrong (CMake 3.21, but `block()` needs 3.25), and a throw-away spike is built by default.
- **The docs (17.5k lines) describe how features came about, not how the system works.**
  - There is no architecture overview, no testing guide and no description of the on-disk formats or settings
    keys.
  - `ROADMAP.md` is a 1140-line changelog under a heading dated 2026-09-19.
  - `TODO.md` has 126 done items against 52 open ones.
  - The device checklist has 1546 unchecked boxes.
  - AGENTS.md has stale facts: "441 tests" where there are 1865, and it is missing the `audio` and `hwr` labels.

In maintainability gained per hour, the order is: fix the tests' shared support, write the docs entry points and an
architecture page, delete the deprecated text mode and the own-format fallbacks, then cut up `CanvasView` and
`CanvasInput` in small, mechanical steps.

---

## 1. Structure

### 1.1 `CanvasView`: the god object (`qt/src/canvas/CanvasView.h` 838 lines, `.cpp` 3614)

`class CanvasView final: public QObject, public XournalView, public Layout, public RasterHost, public DocumentListener`
(CanvasView.h:85). Its responsibilities, each a section of the header:

| Concern | Where (header / cpp) | ~cpp lines |
| --- | --- | --- |
| pages, layout, zoom/scroll glue, presenting, rotation, snapping | h:91–180, cpp:92–420 | 450 |
| presenter mirror | cpp:422–474 | 50 |
| element selection (port of XournalView), Markdown-layer juggling, selection drag | h:240–258, 505–516; cpp:474–1005, 3483–3600 | 650 |
| clipboard: copy/cut/paste elements, text, images, link markers, snip links | h:262–325; cpp:760–1005 | 450 |
| snip / ink sweep (std::async jobs) | cpp:1005–1100 | 100 |
| stickers I/O (also `CanvasStickers.cpp` 249) and groups (`CanvasGroups.cpp` 105) | h:326–370 | 350 |
| links: PDF links, text links, hover links, math errors, text columns | h:372–409; cpp:~1400–1658 | 300 |
| PDF text selection / highlight / underline | h:410–480; cpp:1658–2060 | 400 |
| second view / reference / reading only / edge taps | cpp:2060–2596 | 500 |
| memory planning (CanvasMemory), visibility, render requests | cpp:2500–2628 | 130 |
| timeline replay | cpp:2628–2901 | 270 |
| XournalView / RasterHost / DocumentListener shims, PDF caches | cpp:2901–3483 | 580 |
| navigation history (back/forward) | h:482–490 | 100 |
| text editing: TextEditor, MarkdownEditor, emoji completion, text-file mode, to-do stamps (`TodoStamp.cpp` defines `CanvasView::addTodoStamp`) | h:492–520 | 400 |

Symptoms:

- Every other file in `canvas/` includes `CanvasView.h`, and `CanvasView` includes 18 of them. There are hard cycles
  `CanvasView ↔ CanvasPage`, `CanvasView ↔ MarkdownEditor`, `CanvasView ↔ StickyNotes`, `CanvasView ↔ MixedSelection`
  (include map from all `canvas/*.cpp`).
- The view→page mapping `layout.pageAt(viewController.viewToContent(p), viewController.zoom())` is written out
  14 times in CanvasView.cpp (e.g. :766, :777, :786). It wants one `pagePointAt(QPointF viewPos)`.

The proposed split is in blocks B8–B10. Each new class takes a `CanvasView&` (or only what it needs), and
`CanvasView` keeps forwarding methods for one step so that callers in `app/` and `quick/` compile unchanged:

1. `NavigationHistory` (NavPoint, backStack/forwardStack, jumpToPage/jumpToRect/navigateBack/Forward, ~100 lines):
   no shared state with anything else.
2. `PdfTextSelection`: `pdfSelection`, `pdfSelectionPage`, `pdfTextMode`, `pdfHighlightColor` and the 15 `pdfText*` /
   `*PdfSelection*` methods (cpp:1658–2060).
3. `PageLinks`: `findLinkSpots` (88 lines, cpp:1488), `linkAt`, `textLinkAt`, `hoverLinkAt`, `followLinkAt`,
   `mathErrorAt`, `textColumnAt`, `linkSearches`.
4. `CanvasClipboard`: `copySelection`, `cutSelection`, `pasteElements` (175 lines, cpp:760), `pasteText`,
   `pasteLinkMarker`, `insertImage`, `addLinkMarker`, snip-link offer, plus the sticker functions now in
   CanvasStickers.cpp.
5. `TextEditing`: the two editors, `completion`, `markdownText*`, and `startText`/`startMarkdown`/`textPress`/
   `textKeyPressed`/`writeNoteText`/`toggleMarkdownCheckBox` …
6. `ViewRasterHost` (RasterHost implementation, PDF caches, `renderZoom`/`renderDpr`, `sharpWanted`, `rasterUpdated`) and
   `ViewMemory` (`planCache`, `trimTo`, `window`, `trimmed`, visibility timer). Do this last; it carries the
   threading risk of 6.2.

### 1.2 `CanvasInput` (`CanvasInput.h` 253, `.cpp` 1828)

- One class ports `PenInputHandler` and holds all the touch, touchpad, wheel, palm-rejection, pen-hold,
  straighten, curtain-touch and geometry-tool-touch state machines.
- It has ~70 member flags (CanvasInput.h:110–245), e.g. `touchCurtain`, `touchOnCurtain`, `curtainGesture` and
  `curtainScrolls` side by side.
- `touchEvent` is 406 lines (cpp:1315–1720). `actionStart` is 179 (:637), `actionMotion` 147 (:817), `actionEnd` 114
  (:980), `mouseEvent` 109 (:334) and `tabletEvent` 108 (:196).

Proposed split (B11):
- `TouchSession`: the struct of per-session state, cleared in one place. The cancel branch at cpp:1319–1338
  resets 10 fields by hand.
- `touchEvent` split into `touchBegin` / `touchUpdate` / `touchEnd`, with gesture classification in one function.
- `WheelMomentum` (`wheelSamples`, `wheelPages`, `wheelSnapTimer`, cpp:1724–1810).
- `PenHold` (straighten + hold timers, cpp:130–190).

Inject a clock. `monotonicMs()` (cpp:63) and `QTimer`s are used directly, so the canvas tests use real sleeps
(see 7.2).

### 1.3 Code in `canvas/` that is not about a view

| File(s) | Lines | Depends on CanvasView? | Used by | Belongs |
| --- | --- | --- | --- | --- |
| `MarkdownFile.*` | 355 | no | **shell**: Library.cpp, Previews.cpp, HitPages.cpp, MdSnippets.cpp, DocumentLinks.cpp; app | a file format → `session/` (or `markdown/`) |
| `MarkdownSession.*` | 765 | no | app, PageResize, FindReplace, MarkdownEditor | `session/` (document edit of the page's text) |
| `PageResize.*`, `PageRotate.*` | 336 + 576 | no | app only | `session/` (document operations with undo) |
| `ImageFile.*`, `MdImageDecoder.*` | 156 + 93 | no | app, shell/Previews | `shell/` or `session/` |
| `DarkPages.*`, `PagePictures.*` | 453 + 206 | no | quick, shell (Thumbnails, PageSketches), app | `render/` (colour transforms of page pictures) |
| `ScreenCalibration.*`, `HoverPointer.*`, `Perf.*` | 193 + 266 + 183 | no | shell/SettingsModel, quick | fine here, or `util/` for Perf |
| `TextEditor`, `MarkdownEditor`, `MarkdownImages`, `MarkdownBookmarks`, `MarkdownBoxResize`, `EmojiCompletion`, `CanvasTextInput`, `TextFlow` | ~4300 | yes (most) | — | `canvas/text/` (a sub-folder first; see 1.4) |

So `shell` depends on `canvas` only to read `.md` documents. Moving `MarkdownFile` to `session` removes that
dependency.

### 1.4 Flat include paths hide the module graph

- `xqt-session` exports `src/session` as a PUBLIC include dir (XqtSession.cmake:131). `xqt-canvas` exports
  `src/canvas` (:224), `xqt-quick` exports `src/quick` (XqtApp.cmake:27) and `xqt-shell` exports `src/app`
  (XqtApp.cmake:200).
- So canvas code writes `#include "MdBox.h"` (12×), `"MdPaginate.h"` (5×) and `"PageNoteSpace.h"`. Tests write
  `"AppController.h"`, `"TextFlow.h"` and `"MarkdownFile.h"` (MainWindowTest.cpp:112–114).
- A reader cannot tell which module a header comes from. A file can't be moved into a sub-folder without adding one
  more include dir. Block B5 switches to `"module/Header.h"` everywhere.

### 1.5 Build targets do not match folders

- `xqt-shell` (XqtApp.cmake:43–199) compiles `src/shell/*` **and** `src/app/*` (AppController.cpp 5673 lines and the
  ~30 `App*.cpp`, lines 153–199). The "shell" tests therefore link the whole controller, and a shell file may include
  `AppController.h` without the build noticing.
- Split into `xqt-shell` and `xqt-app` (app → shell, never back).
- `XqtSession.cmake` defines `xqt-session`, `xqt-canvas` and both their test binaries. Give canvas its own
  `XqtCanvas.cmake`.

---

## 2. Duplication

**Tests: these helpers are defined again and again** (top-level definitions only; the ones inside fixtures come
on top):

| Helper | Copies | Note |
| --- | --- | --- |
| `waitFor(cond, ms)` | 19 | timeouts 5 s / 10 s / 20 s; some return `void`, so a timeout does not fail the test (LibraryTest.cpp:125 returns void, InkCopyTest.cpp:76 returns bool). `QTest::qWaitFor` already exists. |
| `makeTextPdf` | 17 | same cairo body, different signatures and font sizes (HybridPdfTest.cpp:65, LibraryTest.cpp:1783, PageFilesUiTest.cpp:46) |
| `writeFile` / `readFile` | 15 / 9 | |
| `qpdfCheck` | 10 | |
| `makePdf` / `bytesOf` / `drawStroke` / `fileBytes` / `gunzip` | 9 / 9 / 8 / 6 / 5 | |
| `static void wait(int ms)` (busy `processEvents` loop) | 26 fixture copies | every UI file and 3 quick files |
| UI fixture `SetUp`: AppController + engine + image providers + `loadFromModule("XournalQt","Main")` | 23 | 21 register 5 image providers. `src/app/main.cpp:285–290` registers 6 (`annotation`), so most UI tests run with a different engine than the app. |
| `findItem` / `until` / `click(QQuickItem*)` | 6 / 21 / 14 | |

Only 7 small shared headers exist (`qt/tests/*.h`, 444 lines in all), and they are reached as `"../SearchHits.h"`.

**Settings defaults written twice.** 106 raw `getCustomElement("xournalQt")` lookups take the key and its default as
literals. Two examples:
- `rotateGesture`: CanvasView.cpp:370–374 and again inline at SettingsModel.cpp:393, where the comment
  "(CanvasView::rotateGestureSetting)" points at the copy instead of calling it.
- `snapPages`: CanvasView.cpp:364 and AppController.cpp:1394.

**Canvas**: two margin constants, `TextFlow::MARGIN` (TextFlow.h:54) and `PageMargins::FULL` (PageMargins.h:28). Both
are 56.7. `TextFlow::styleFor` wraps `PageMargins::of`.

**CI**: `xqt-release.yml` repeats the whole Linux job of `xqt-build.yml`: container, deps, ccache, configure, build,
the same ctest line (release.yml:40–77 vs build.yml:49–80). A reusable workflow (`workflow_call`) would share it, as
`xqt-windows.yml` already does.

**CMake**: the test stanza (add_executable, link `Qt6::Test GTest::gtest`, include `TEST_CONFIG_DIR`, define
`XQT_BUILD_RESOURCE_DIR`, `gtest_discover_tests … DISCOVERY_TIMEOUT 30 … QT_QPA_PLATFORM=offscreen`) is written out 9
times. Each source path also carries `${CMAKE_CURRENT_LIST_DIR}/../` (≈400 times in `qt/cmake`).

---

## 3. Dead code and stale material

Verified with grep over `qt/src` and `qt/tests`, counting only references outside the declaration:

- `CanvasMemory::setPlanDelay` (CanvasMemory.h:65): no caller.
- `CanvasView::getPdfTextMode` (CanvasView.h:410): no caller.
- `CanvasView::isRotatable` (h:177): no caller.
- `GeometryToolPicture::displaysDrawn` (GeometryToolPicture.h:73): a test hook no test calls.
- `CanvasView::snipBusy` is public but used only inside CanvasView.cpp:1013. Make it private.
- **The deprecated text mode** (TextFlow.h:1–3 "DEPRECATED (2026-09-26)…kept for now"; docs/text-mode.md):
  - `canvas/TextFlow.*` (563 lines) and `quick/TextFlowEditor.*` (482).
  - `app/qml/TextFlowPanel.qml` (241), still instantiated at Main.qml:2978.
  - `AppController::beginTextFlow/updateTextFlow/endTextFlow` (AppController.cpp:693–760 and 4 more calls).
  - `tests/canvas/TextFlowTest.cpp` (191).
  - What other code still uses from it is the margins (MarkdownFile.cpp:29–240, MarkdownSession.cpp:90–497,
    MarkdownBoxResize.cpp:89, TextEditor.cpp:107–110, CanvasView.cpp:2315). Switch those to `PageMargins` first. B6.
- **The spike** `qt/spikes/inkpad` (2197 lines): its README says "This throwaway app…". ADR 0001 has decided. Yet
  `XQT_BUILD_SPIKES` defaults to ON on desktop (CMakeLists.txt:65), and the AGENTS.md configure line does not turn it
  off, so every dev build compiles it. CI passes `-DXQT_BUILD_SPIKES=OFF`. Delete it (it is in git history) or default
  the option to OFF.
- **Stale comments in canvas**:
  - CanvasPage.h:6–7 says the dispatch covers "the tools supported so far (pen, highlighter, eraser incl. whiteout)".
    CanvasPage.cpp handles 14 tool types.
  - CanvasView.h:279–281 documents `pasteElements` with two contradicting sentences, left over from a merge.
  - CanvasView.h:411 "(page coordinates, points)" sits above `selectPdfTextAt(QPointF viewPos …)`, which takes view
    coordinates.
  - CanvasInput.cpp:50 "Pens that report proximity: touch works again this soon after the pen left." is attached to
    `TAP_MAX_MS`, which is the tap duration.
- **Stale docs**:
  - `docs/ui-adaptive-audit.md` (632 lines) still says "**Nothing here is built yet**" (line 5). The UI rework is
    merged.
  - `docs/text-mode.md` describes a removed feature.
  - Three docs and TODO.md point to `../cross-platform-qt-research/`, which is outside the repo
    (platform-research.md:4, android-roadmap.md:6, windows-roadmap.md:5).
  - TODO.md:53 points to `qt/docs/pdf-engine-experiment.md` "on that branch"; the file does not exist on master-qt.
- **Root README.md**: lines 1–120 are the fork's. From "Translations" (line 122) to the end it is upstream's README,
  which offers the GTK build (`readme/LinuxBuild.md`), "Windows 10"/"macOS Catalina" sections and a "File format"
  section saying "PDFs are not embedded". That contradicts the fork's hybrid PDF. README.md is an upstream file that
  is edited but not listed in `adr/0002-upstream-seams.md`.

## 4. Backward compatibility to remove (this area, and the tests of others)

**In canvas** (and the session file it relies on; coordinate with the session reviewer):
- `session/PageMargins.cpp:50–66` `pageBox()` falls back to the "unscaled" 2 cm box and the "lined page from before
  its line scaled" box. `PageMargins.h:46–51` declares `unscaled()` for "older text of a small page".
  - Users: `canvas/MarkdownSession.cpp:181–191` ("older text of a small page moves there"), PageResize.cpp:59,
    CanvasView.cpp:2277 and session/TextDocument.cpp:28.
  - Removing it means `pageBox` becomes `md::pageBoxOf(layer, m.left, m.top)`, and the `!pageText` move branch in
    MarkdownSession goes away.
- `TextFlow` (above): its "Text" layer is read back into blocks so that pages written with the text mode can be
  edited in it again. Pages keep working as ordinary text elements once it is removed.
- No own-format version checks were found in canvas otherwise. `LegacyRedrawable` (CanvasPage.h:150) is upstream's
  interface name, not compatibility code.

**Tests that only exist for xournal-qt's own older data** (delete them together with the code the other reviewers
list):
- `shell/ToolboxModelTest.cpp:296–330` `theToolboxOf070IsUpgraded` (toolbox JSON v1 → v2).
- `shell/ToolboxModelTest.cpp:~620–680`: the `migratedToolbox()` test, "what 0.7.0 wrote with the classic tool bar".
- `ui/AdaptiveLayoutTest.cpp:2819–2830`, the part of `zenIsAutomaticOnlyInATinyWindow` starting "From 0.7.0: the
  reader chrome left in a tiny window".
- `shell/SettingsModelTest.cpp:310ff`, the part of `snappingToTheGridIsOffUnlessChosen` with "Settings saved by an
  earlier version". Keep only the "new setup" half.
- `session/HybridPdfTest.cpp:2169` `IncrementalSaveTest.aFileOfAnEarlierVersionIsAppendedTo` (no layer record in its
  marker).
- `canvas/PdfTextDocumentTest.cpp:473` `pageAttributeBookmarksOfATextBecomeComments`. It converts bookmarks that an
  earlier xournal-qt stored as page attributes into comments. Confirm with the session reviewer, then delete it.
- Keep `session/ElementTimesTest.cpp:188` / `GroupsTest.cpp:190` (`addImageLegacy`): those are upstream's loader
  interface. Keep `SyncConflictsTest.cpp:52` (other apps' file names).

## 5. Readability

- **Comment density.**
  - Headers are ~40 % prose: CanvasView.h has 344 of 838 lines as comment lines; ViewController.h 32 %. The `.cpp`
    files are 3–7 %.
  - The header prose does help: it is the only description of behaviour, e.g. `planCache` at h:205.
  - Where it buries the code: whole feature specs sit on single declarations (`pasteLinkMarker` h:288–293,
    `insertImage` h:297–304, `StickerSource` h:328–340).
  - Parenthetical asides `(…)` follow almost every statement in the .cpp files, e.g. CanvasView.cpp:2961–2975.
  - Rule to adopt: a declaration says *what* in ≤2 lines and links the feature doc for *why*. Long design notes
    move into the feature doc.
- **Long functions in canvas** (>80 lines, 25 of them):
  - `CanvasInput::touchEvent` 406, `MarkdownEditor::keyPressed` 195 (MarkdownEditor.cpp:1007),
    `CanvasInput::actionStart` 179, `CanvasView::pasteElements` 175, `CanvasPage::onButtonPressEvent` 160
    (CanvasPage.cpp:132), `CanvasPage::onButtonReleaseEvent` 154 (:553), `CanvasInput::actionMotion` 147,
    `MixedSelection::pasteGroup` 129, `MarkdownEditor::newLine` 121, `CanvasView::CanvasView` 118 (cpp:92: a
    constructor made of lambdas connected to settings), `CanvasPage::selectObjectAt` 117,
    `CanvasView::endSelectionDrag` 108.
- **Naming**: test-only accessors are mixed into the public API without a marker, e.g. `visibilityUpdates`,
  `setVisibilityDelay`, `linkLookups`, `cacheWindow`. There are 66 "(tests)" hooks across `src/*.h`, 5 in
  CanvasView.h. Group them under a `// --- for tests` section, or put them in a `friend struct CanvasViewProbe`.
- **Magic numbers** in CanvasInput.cpp:
  - `wheelSnapTimer.setInterval(180)` (:95);
  - the pressure inference constants `3.142 / 2.0 + atan(inverseSpeed * 3.14 - 1.3)` (:597, upstream's, but
    unnamed);
  - the velocity windows `100.0` (:1607), `50.0` (:1706), `120.0` (:1770);
  - `1.0015` zoom per wheel unit (:1728).
  - ViewController.cpp:18/21: `8` ms momentum and `300` ms settle.
  - `PALM_TIMEOUT_MS = 0` (:46) is a default that turns the feature off. Fine, but it makes the timeout branch of
    `touchBlocked` (:1251) dead by default.
- **Settings read on every event**: `touchBlocked()` (:1251) and `penNear()` (:1239) look up
  `getCustomElement("touch")` string maps on every touch event.

## 6. Correctness risks

1. **`pdfCache` is read from render threads while the UI thread replaces it.**
   - `PageRaster::renderToBuffer` (render/PageRaster.cpp:209–210) calls `host->rasterPdfCache(false)`, which returns
     `pdfCache.get()` (CanvasView.cpp:2921) with no lock.
   - At the same time `CanvasView::replacePdfCache` (:2955–2975) runs on the UI thread:
     `retiredPdfCaches.push_back(std::move(pdfCache)); … pdfCache = std::make_shared<…>`.
   - That is a data race on a `shared_ptr`. A render that reads between the move and the assignment gets `nullptr`
     and draws a page without its PDF. Formally it is UB.
   - Fix: a `std::atomic<std::shared_ptr<PdfCache>>` (C++20), or take `backgroundPdfMutex` for both caches and return
     a `shared_ptr` that the render holds.
2. **`retiredPdfCaches` only grows** (CanvasView.h:717, cpp:2959/2963). Every document reload or PDF replacement
   appends 1–2 caches that live as long as the view. They are emptied by `evictPdfCache({})` first, so each is small,
   but a long-open tab whose PDF is re-merged often (pasting pages) accumulates them. Drop retired caches once no
   render of the old generation is running (the render queue knows).
3. **A whole PDF is loaded under a mutex on a render thread.** `rasterPdfCache(true)` (cpp:2920–2948) runs
   `XojPdfDocument::load` while holding `backgroundPdfMutex`, so the second background worker blocks for the duration
   of a big PDF's load. That is acceptable, but it belongs in a comment or a one-time task on the UI side.
4. **`renderZoom` and `renderDpr` are two separate atomics** (h:741–742), read separately by `rasterParams()` (:2984).
   A render can pair the new zoom with the old dpr (e.g. when a window moves between screens while zooming). That
   costs one wasted render, nothing worse.
5. **The tests share process state.** gtest cases in one binary share the config folder and the `AppContext`
   singleton. TODO.md:1046–1066 lists 3 order-dependent failures: `postersAndFlashcardsFromTheDialogs` leaves the page
   template at 300×500, and `StickerFileTest.thePictureBehindLiesAtTheBottom` fails depending on order. ctest hides
   this because it runs every case in its own process. Running a binary directly (as agents do with
   `--gtest_filter`) shows it.

## 7. Tests

### 7.1 Size and time

There are 1865 ctest tests in 9 gtest binaries: `xoj-unit-tests`, `xqt-session-tests`, `xqt-canvas-tests`,
`xqt-markdown-tests`, `xqt-quick-tests`, `xqt-ui-tests`, `xqt-shell-tests`, `xqt-audio-tests` and `xqt-hwr-tests`. On
top come the golden script and 7 extra `add_test` re-runs at other scales and screens (XqtApp.cmake:371–437).
AGENTS.md says "441 tests" and lists 8 labels; the labels `audio` and `hwr` (105 tests) are missing from it.

| Label | Tests | Σ time (serial) | Median | Slowest |
| --- | --- | --- | --- | --- |
| ui | 417 | **1720 s** | 3.83 s | PresenterView.ui@2screens 26.8 s, AdaptiveLayoutTest.theHomeScreensPlusAndViewMenusWork 22.6 s, …menusFitAtFiveSizes 20.2 s, ToolboxTest.inFullScreenTheMoreMenu… 15.6 s |
| canvas | 302 | 144 s | 0.40 s | CanvasReplayTest.aPenHeldStillOffersWhatCanBeDoneHere 5.2 s, SidewaysTest.aSwipeGoesOnePageOn… 3.2 s |
| quick | 64 | 74 s | 0.81 s | FractionalScaleCanvas.quick@167 7.1 s |
| shell | 424 | 62 s | 0.08 s | PastedPdfPages.anUndoWhileTheMergedPdfIsWrittenIsSavedRight 10.3 s (two `sleep_for(300ms)`, PastedPdfPagesTest.cpp:684/1037) |
| session | 269 | 26 s | 0.10 s | BackgroundSaveTest.theEventLoopKeepsRunningDuringAHybridSave 2.2 s |
| markdown / unit | 141 / 134 | 5 s / 3 s | | |

AGENTS rule 6 ("keep the routine test run under a minute") is broken by a factor of ~5 even at `-j8`, because the UI
label alone is ~215 s of wall time.

### 7.2 Why the UI and canvas tests are slow

- **About 2.5 s per UI test is fixed cost** (the 10th percentile is 2.54 s). Each ctest case is its own process. It
  creates a fresh `AppController` and `QQmlApplicationEngine` and loads `Main.qml`: 5551 lines out of 106 QML files
  and 29k lines in all.
- With `XQT_FAST_DEV=ON` (the build-qt here has it), QML is not compiled ahead of time. Every one of the ~420
  processes therefore compiles the whole UI. `tests/ui/main.cpp:24–27` also points `XDG_CACHE_HOME` at a fresh
  temporary folder, so no QML disk cache survives between them.
  - This is a hypothesis to measure: compare the ui label with FAST_DEV ON and OFF.
  - If it holds, build the test UI module with cachegen even in FAST_DEV, or keep a shared QML cache folder for the
    test run.
- **Fixed waits.** The `wait(int ms)` helper (MainWindowTest.cpp:153) is a busy loop that always waits the full
  time.
  - `click()` waits 50 ms after every click (:221–225) and `key()` 20 ms (:176).
  - There are ~1000 literal `wait(N)` calls: 286× `wait(50)`, 217× `wait(100)`, 80× `wait(300)`, 75× `wait(200)`.
  - MainWindowTest.cpp alone has 385 of them, ≈92 s of fixed waiting. AdaptiveAuditTest has 17.8 s, ToolboxTest
    17.3 s, AdaptiveLayoutTest 17.2 s, ReferenceWindowTest 16.0 s.
  - TODO.md:1033 says "The UI tests that used fixed waits are hardened (qt/test-waits, merged 2026-09-24)". That is
    no longer true. The CI's `--repeat until-pass:3` (xqt-build.yml:80, xqt-release.yml:77) papers over the
    result.
- **The canvas tests sleep in real time** because `CanvasInput` and `ViewController` read the steady clock and run
  real `QTimer`s:
  - `QThread::msleep` at CanvasReplayTest.cpp:343, :2031 (`msPerStep` per swipe step), :2135 (`80`) and :2168
    (`30`); DarkPagesTest.cpp:194.
  - The 300 ms zoom-settle timer (ViewController.cpp:21) is waited out by every `settle()`.
  - An injectable clock (`std::function<double()>`, plus `QTimer` intervals as members) would make these tests
    instant and deterministic.

### 7.3 Infrastructure bugs found on the way

- **The parameterized PageRasterTest is named by pointer addresses.** `INSTANTIATE_TEST_SUITE_P(Fixtures,
  PageRasterTest, Values(make_tuple(u8"test1.xoj", …)))` (tests/unit/PageRasterTest.cpp:145–151) prints `char8_t*` as
  pointers. The ctest names become `Fixtures/PageRasterTest.fullRenderMatchesUpstreamRenderJob/(0x556e9a0b6223`, cut
  at the first space. Two cases share a name (same literal) and the names change with every link. Add a name
  generator.
- **The golden tests never run in CI or here.** `golden-quick` exits 77 when `../xournalpp/build/xournalpp` is
  missing (run_golden.sh:28), and the cost file records 0 runs. The "pixel identical to upstream" claim
  (ROADMAP.md:9) is not being checked. Either build upstream's CLI in one CI job, or keep reference PNGs in the repo.
  `StickyNoteTest` has the same dependency on `XQT_UPSTREAM_BIN` (XqtSession.cmake:261).
- **A settings file is written into the working directory.** This is TODO.md:1059. The test that writes it is
  upstream's `test/unit_tests/control/SettingsTest.cpp:17` (`Settings settings{"non-existing-file-path"}`). Fix it
  on our side: give the `xoj-unit-tests` discovery a `WORKING_DIRECTORY` under the build folder
  (XojTests.cmake:60–63).
- **ctest from the wrong CMake fails outright.** `CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST`
  (CMakeLists.txt:51) makes `ctest` include the configure-time CMake's `GoogleTestAddTests.cmake`. In the cloud
  container the `ctest` on PATH (3.28) aborts on every label with "CMake 3.30 or higher is required", because
  build-qt was configured with conda's CMake 4.2. Say in AGENTS.md: "use the `ctest` of the CMake that configured the
  build (`source /opt/xqt-env/cloud-env.env`)". Running the wrong one also empties
  `Testing/Temporary/LastTest.log`; it happened during this review, the cost data survived.
- **`PenStylesTest.cpp` is listed twice** in `xqt-canvas-tests` (XqtSession.cmake:288 and :295). CMake de-duplicates
  it, but it marks a list maintained by merging.

### 7.4 Coupling and gaps

- **UI tests drive QML internals.** There are 516 `QMetaObject::invokeMethod` calls into QML functions (e.g.
  `"toggleReading"`, AdaptiveLayoutTest.cpp:2816), 1442 `property("…")` reads and 1163 `findItem`/`find<>` lookups
  by objectName. Renaming a QML function breaks tests at run time, not at compile time. A thin C++ "page object" per
  area (toolbox, home screen, sheets) inside the shared fixture would keep the names in one place.
- **Tests sit under the wrong layer.** `shell/CanvasMemoryTest.cpp` and `shell/ThumbnailsTest.cpp` test canvas/render
  scheduling in the shell binary, and `shell/PastedPdfPagesTest` tests rendering of pasted pages. They are there
  only because shell links everything. Fine for now; note it for the module split.
- **Gaps in this area**:
  - There is no unit test of `CanvasView::rasterPdfCache`/`replacePdfCache` under concurrent renders (6.1). A TSan
    job would find it: `qt/tools/tsan.supp` exists but no CI job uses it.
  - Nothing tests that `main.cpp`'s engine setup equals the tests' setup (2.).
- **Duplicated tests across layers**: `TodosTest`, `TagsTest` and `QuickNoteTest` each exist in both `shell/` and
  `ui/`. They test different layers, but the names collide in ctest output: `TodosTest.*` from two binaries.

---

## 8. Build, CI, scripts

- **Wrong declared minimums.**
  - `cmake_minimum_required(VERSION 3.21)` (qt/CMakeLists.txt:4). Yet `block()` (XojTests.cmake:14) needs 3.25,
    `TEST_FILTER` needs 3.22, and `CMakePresets.json` declares 3.25.
  - `find_package(Qt6 6.5)` (:73) and AGENTS.md / FORK.md / linux-deps.sh all say "Qt 6.5 or newer". CI and
    releases test 6.7 and 6.8, and the brief says 6.7+.
  - Set CMake 3.25 and Qt 6.7 and say so once.
- **Version**: `project(xournal-qt VERSION 0.8.0)` while `release-notes/0.9.0.md` is the next draft. That is
  intended (release.yml:60–66 checks the tag), but nothing says the version is bumped at the cut. Add one line to
  `releasing.md`.
- **Platform builds run only at release.** `xqt-windows.yml`, `xqt-macos.yml` and `xqt-android.yml` trigger on
  `workflow_dispatch`/`workflow_call` and on branches `qt/windows-build`, `qt/windows-feel`, `qt/macos-build` and
  `qt/android-build`, none of which exist any more. A Windows/macOS/Android breakage on master-qt shows only when a
  release is cut. Add a weekly `schedule:` or a `master-qt` push filter on `qt/cmake/**`.
- **`--repeat until-pass:3`** in both Linux workflows hides flakiness. TODO.md:1010 already asks to drop it after
  hardening.
- **Scripts**:
  - `build-slot.sh` (30 lines) is good.
  - `cloud-env.sh:95` copies `cloud-env.env` into `qt/scripts/` inside the checkout (`cp -f "$envfile"
    "$here/cloud-env.env"`). Generated files should not land in the tree; AGENTS.md already points at
    `/opt/xqt-env/cloud-env.env`.
  - `macos-smoke.sh` (189) and `windows-smoke.sh` (260) run the same 4–5 steps (CLI version, exports, app
    off-screen, audio info) with platform-specific wrappers. A shared `smoke-steps.sh` would remove ~100 lines.
- **CMake style**: every path is spelled `${CMAKE_CURRENT_LIST_DIR}/../src/...` (≈400×), and nine test stanzas are
  copies (see 2). A `xqt_add_test_binary(<name> LABEL <l> LIBS … SOURCES …)` function and per-module
  `target_sources(… PRIVATE <relative paths>)` would cut `qt/cmake` from 1960 to roughly 1300 lines and make the
  labels and environments consistent. `xqt-markdown-tests` sets no `QT_QPA_PLATFORM` (XqtMarkdown.cmake:108); the
  others do.

---

## 9. Documentation

### 9.1 What there is

| File | Lines | What it is now | Current? |
| --- | --- | --- | --- |
| `AGENTS.md` | 113 | rules + build/test + module table | mostly. Stale: 441 tests, 8 labels, Qt 6.5. The table misses `audio`, `hwr`, `render` (Qt-free `xoj-render`), `compat`, `cli`, `tools`, `3rdparty`, `packaging`. |
| `VISION.md` | 54 | the author's goals | yes (hand-maintained) |
| `TODO.md` | 1069 (99 KB) | open tasks, but **126 `[x]`, 52 `[ ]`, 4 `[?]`**, and many sections headed "merged 2026-09-2x" | ~25 % open work. Its own rule (line 10: "remove it once merged and recorded in ROADMAP") is not followed. |
| `qt/docs/ROADMAP.md` | 1541 | lines 3–1145: a changelog of every block under "## Status (2026-09-19)"; 1145–1155: backlog; 1155–1541: "Plan (as approved)", the original M0–M7 plan and the risks of September | history, not a roadmap |
| `FORK.md` | 75 | branches, fork rules, build | yes. Says Qt 6.5. |
| `README.md` (root) | 449 | 120 lines of the fork, then upstream's README | half wrong for this fork (see 3) |
| `qt/docs/<feature>.md` | ~40 files, 42–908 lines | one per block: author's quote, status line ("design agreed …; built as qt/x"), design, "What is built" | behaviour is current, but mixed with design history. E.g. hybrid-pdf.md (908 lines) holds 7 blocks' design + "what is built"; the actual file layout of a hybrid PDF is spread over 6 sections. |
| `qt/docs/adr/` | 3 ADRs | fork policy, UI host, upstream seams | yes, but only 3 decisions are recorded. Big ones are only in feature docs: CanvasMemory, revisions, the library index, the hybrid PDF format, background save. |
| research / one-off notes | platform-research, md-columns, ideas-2026-10, ui-adaptive-audit, research/* (989) | dated research | history; ui-adaptive-audit stale |
| platform docs | android, macos, windows, releasing (+ android-roadmap, windows-roadmap) | how to build/package | current; the two roadmaps are open work and belong in TODO |
| `testing/device-checklist.md` | **3942** | 166 sections, **1546** manual checkboxes | no one can run it. It has become a second, unstructured feature spec, because rule 4 asks to update it per feature. |
| `testing/performance-logging.md`, `user/*`, `release-notes/*` | | | current |

### 9.2 What a new human developer cannot find

1. **An entry point.** There is no `qt/docs/README.md`. The root README's "Building" sends them to the GTK build.
2. **An architecture overview.** No document gives:
   - the layers and the allowed direction of dependencies: `xoj-util → xoj-core → {xoj-tools, xoj-render} →
     xqt-markdown/xqt-audio → xqt-session → xqt-canvas → {xqt-quick, xqt-hwr} → xqt-shell(+app) → xournal-qt`;
   - what a tab is (TabManager → DocumentSession + CanvasView + DocumentCanvasItem);
   - how an input event travels (QQuickWindow → DocumentCanvasItem → CanvasInput → CanvasPage → upstream
     StrokeHandler → undo);
   - how a page gets on screen (CanvasView visibility → PageRaster → RenderService workers → tiles in
     DocumentCanvasItem, with CanvasMemory/PageSketches/Thumbnails owning memory).
   - AGENTS.md's "What the moving parts assume" is the only, very compressed, trace of this.
3. **Where data lives on disk**: the config folder and the `xournalQt` custom settings keys with their defaults
   (106 lookups, see 2), the library index and its cache folders, thumbnails and previews by revision, autosave and
   recovery, sidecars (`name.assets/`, audio), and the hybrid PDF layout. All of it is scattered over ~10 feature
   docs.
4. **A testing guide**: the binaries and labels, which fixture to start from, how to write a UI test (the
   objectName rule, waiting for state rather than time), the time budget, the env variables (`XQT_SHOTS`,
   `XQT_BENCH_*`, `XQT_PERF`, `XQT_ARCHIVE_SAMPLES`), and how to reproduce CI (`ci-container.sh`).
5. **A glossary.** Block, tab, session, view, primary view, reference, library, sketch vs thumbnail vs preview,
   revision, hybrid / archive PDF, Zen / Read.

### 9.3 Proposed structure

```
README.md                       users: what it is, download, link to qt/docs/README.md for developers
                                (drop upstream's sections below line 120, or link to them; record the seam in ADR 0002)
AGENTS.md                       agents: ≤120 lines. Rules, the build/test commands (correct counts, the ctest pitfall),
                                the module map with the allowed dependency direction, "where to record what", and
                                links to qt/docs/README.md and the module READMEs
VISION.md                       unchanged
TODO.md                         open work only (≤300 lines). Done items go to CHANGELOG at merge.
CHANGELOG.md (qt/)              what was merged, per date/block, one or two lines each: ROADMAP's Status section moves here
qt/docs/
  README.md                     START HERE: 1-page orientation, reading order, glossary link
  architecture/
    overview.md                 layers, targets, dependency rule, the life of a tab; the diagram's text
    architecture.yaml           machine-readable model (components: dir, target, depends_on, threads, owns_memory,
                                docs) — source of the later browsable diagram; checked in CI against
                                `cmake --graphviz` so it cannot drift
    input.md                    event path, palm rejection, gestures, tools (from CanvasInput.h header + docs)
    rendering.md                visibility → PageRaster → RenderService, CanvasMemory, PageSketches, Thumbnails,
                                revisions, threads and locks (from AGENTS "moving parts" + hidpi.md + dark-pages.md)
    documents.md                DocumentSession, undo, autosave/background save, text files, hybrid/archive PDF
                                layout (the format spec extracted from hybrid-pdf.md)
    ui.md                       QML structure (Main.qml and its parts), AppController as the QML API, adaptive layout
    data-on-disk.md             config, settings keys + defaults (generated from the settings table, B13), caches,
                                indexes, sidecars
    glossary.md
  features/                     one file per feature: current behaviour only, ≤150 lines; "Design history" section
                                at the end or a link to the decision. (today's *.md, trimmed)
  decisions/                    ADRs 0000–0002 + new ones distilled from the feature docs (hybrid PDF format, library
                                index, memory owners, background save, Qt Quick canvas tiles, text mode removed)
  development/
    building.md                 deps, presets, FAST_DEV, ccache/worktrees, build-slot, cloud env
    testing.md                  binaries, labels, fixtures (tests/support), writing UI tests, time budget, env vars,
                                ci-container.sh, golden tests
    releasing.md, android.md, windows.md, macos.md, performance-logging.md
    device-smoke.md             ≤60 manual checks that cover the device-only paths (pen, palm, scaling, platforms);
                                the per-feature manual checks move into each features/*.md "On the device" section
  user/                         unchanged
  history/                      release-notes/, research/, ideas-2026-10.md, ui-adaptive-audit.md, platform-research.md,
                                md-columns.md, the original M0–M7 plan from ROADMAP.md, text-mode.md
qt/src/<module>/README.md       ≤40 lines each: purpose, main classes and their one-line jobs, allowed dependencies,
                                threads, the test label. The agent's map when it opens a folder; linked from overview.md
```

What this gives:
- Humans get one entry point, an overview they can read in 15 minutes, and a model file that a browsable diagram
  (an HTML page rendered from `architecture.yaml`) can be built from.
- Agents get a short AGENTS.md and a README in every folder they open.
- "Where things go" becomes explicit: open work goes to TODO, merged work to CHANGELOG, current behaviour to
  features/, why to decisions/, and dated notes to history/.

---

## Proposed refactoring blocks

Ordered by maintainability per effort. "Conflicts" lists the blocks that touch the same files; run those one after the
other.

### B1. Shared test support and one UI fixture (high value, low risk)
- **Goal**: one implementation of each test helper, and one `Main.qml` fixture that equals the app's engine setup.
- **Files**:
  - new `qt/tests/support/` (`TestSupport.h/.cpp`: `writeFile`, `readFile`, `bytesOf`, `fileBytes`, `gunzip`,
    `qpdfCheck`, `makePdf`, `makeTextPdf(path, words, fontSize)`, `drawStroke`, `addStroke`);
  - the 7 existing `qt/tests/*.h` moved into it;
  - new `qt/tests/ui/WindowFixture.h/.cpp` (MainWindowTest.cpp:119–325: SetUp/TearDown, `find`, `findItem`,
    `until`, `click`, `key`, `entryOf`, `scrollIntoView`, `toolEntry`, `onRail`);
  - new `src/app/QmlEngineSetup.h/.cpp` with `void setUpEngine(QQmlEngine&, AppController&)`, used by `main.cpp:285`
    and the fixture;
  - the 23 UI files, the ~50 files with copied helpers, and `qt/cmake/*` (a `xqt-test-support` static lib).
- **Steps**:
  1. Add `xqt-test-support` with the helpers, signatures unified.
  2. Replace every `waitFor` with `QTest::qWaitFor` inside `ASSERT_TRUE`. Do not keep void waits.
  3. Extract `WindowFixture` and let `MainWindowTest` derive from it.
  4. Convert the other 22 UI files one per commit.
  5. Add `setUpEngine` and use it in `main.cpp` and the fixture.
- **Risk**: low. Behaviour changes only where a void `waitFor` used to hide a timeout; such a test may now fail
  honestly.
- **Verify**: `ctest -N | tail -1` still reports 1865, and labels ui, shell, session, hwr, canvas pass. A new test
  checks that the fixture's engine has the `annotation` provider.
- **Conflicts**: B2 (ui/*), B3 (unit/, cmake), B4 (cmake), B7 (some test files).

### B2. Make the UI and canvas tests fast and deterministic
- **Goal**: the routine run under a minute again, and the CI repeat dropped.
- **Files**: `tests/ui/*` (via the B1 fixture), `src/canvas/CanvasInput.*`, `ViewController.*`,
  `tests/canvas/CanvasReplayTest.cpp`, `DarkPagesTest.cpp`, `qt/cmake/XqtApp.cmake`, the workflows.
- **Steps**:
  1. Measure the ui label with `XQT_FAST_DEV` ON vs OFF and with a shared QML cache dir. If ahead-of-time QML wins,
     let the UI test module keep cachegen.
  2. Make `click()` and `key()` wait for the next frame or a condition instead of 50/20 ms. Replace `wait(N)` with
     `until(cond)` file by file, starting with MainWindowTest (385 calls).
  3. Inject a clock into `CanvasInput` and `ViewController` (`setClock`, timer intervals as members). Replace the
     `QThread::msleep` calls in CanvasReplayTest (:343, :2031, :2135, :2168) with clock steps.
  4. Give the 50 tests over 6 s (7.1 lists the worst) the label `slow` and leave them out of the routine run.
  5. Drop `--repeat until-pass:3` once the full suite passes 5× under `-j$(nproc)`.
- **Risk**: medium. Waiting for state needs the right condition per step, and a wrong one adds flakiness.
- **Verify**: the Σ time of the ui label in CTestCostData before and after. The full suite 5× green under load
  (`ci-container.sh`).
- **Conflicts**: B1 first. B11 (CanvasInput).

### B3. Test-infra bugs (small)
- **Goal**: stable test names, nothing written outside temp folders, honest golden status.
- **Files**:
  - `tests/unit/PageRasterTest.cpp:145`: add a name generator;
  - `qt/cmake/XojTests.cmake:60–63`: `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/unit-cwd`;
  - `XqtSession.cmake:295`: remove the duplicate PenStylesTest;
  - `XqtMarkdown.cmake:108`: add the offscreen env;
  - `qt/CMakeLists.txt:4`: `cmake_minimum_required(VERSION 3.25)`; `:73`: `Qt6 6.7`;
  - `run_golden.sh` / CI: print "golden SKIPPED: no upstream build" as a CI annotation, or build upstream's CLI once
    per week.
- **Risk**: low.
- **Verify**: `ctest -N` has no duplicate names (`sort | uniq -d` is empty), and no `non-existing-file-path` appears in
  the checkout after the unit label.
- **Conflicts**: B4 (cmake).

### B4. CMake consolidation
- **Goal**: one file per module, a test-binary function, app split from shell, the spike gone.
- **Files**:
  - `qt/cmake/XqtSession.cmake` → `XqtSession.cmake` + `XqtCanvas.cmake`;
  - `XqtApp.cmake` → `XqtQuick.cmake`, `XqtShell.cmake`, `XqtAppTarget.cmake`;
  - new `XqtTesting.cmake` with `xqt_add_test_binary()`;
  - `qt/CMakeLists.txt`; delete `qt/spikes/` (or default `XQT_BUILD_SPIKES` to OFF).
- **Steps**:
  1. Add the function and convert one test binary per commit.
  2. Use `target_sources` with paths relative to the module.
  3. Create the `xqt-app` target from `src/app/*.cpp`, linking `xqt-shell`. Move `target_include_directories(... src/app)`
     there.
  4. Remove the spike.
- **Risk**: low to medium. The include order changes when `src/app` is no longer on shell's PUBLIC path, and any
  shell → app include then becomes a compile error, which is the point.
- **Verify**: a clean configure and build of all targets on both CI containers; `ctest -N` count unchanged.
- **Conflicts**: B3, B5, B12.

### B5. Module-qualified includes
- **Goal**: `#include "canvas/CanvasView.h"`, `"markdown/MdBox.h"`, `"session/PageNoteSpace.h"` everywhere. Only `src/`
  stays on the include path for our modules.
- **Files**: all of `qt/src`, `qt/tests` and `qt/cmake` (remove the per-module PUBLIC include dirs).
- **Steps**:
  1. A script rewrites bare includes that resolve to a module header.
  2. Remove the include dirs one module per commit; the compiler finds what was missed.
- **Risk**: low, but it touches every file, so do it in a quiet window with no open blocks.
- **Verify**: a full build and the full suite.
- **Conflicts**: everything. Run it right after B4 and before B8–B12.

### B6. Remove the deprecated text mode
- **Goal**: −1500 lines of a feature nobody can reach.
- **Files**:
  - delete `canvas/TextFlow.*`, `quick/TextFlowEditor.*`, `app/qml/TextFlowPanel.qml`, `tests/canvas/TextFlowTest.cpp`
    and `docs/text-mode.md`;
  - remove `AppController::beginTextFlow/updateTextFlow/endTextFlow` and their 4 call sites, Main.qml:1818, :2978 and
    :5510, and the cmake lists;
  - switch `TextFlow::MARGIN` / `styleFor` users (MarkdownFile.cpp, MarkdownSession.cpp, MarkdownBoxResize.cpp,
    TextEditor.cpp, CanvasView.cpp:2315) to `PageMargins::of(page)` / `PageMargins::FULL`.
- **Risk**: low. Text written with it stays ordinary text elements.
- **Verify**: labels canvas, markdown, ui. Grep for `TextFlow` finds nothing.
- **Conflicts**: B7 (MarkdownSession), B12 (moves the same files), B9 (TextEditor).

### B7. Remove own-older-data fallbacks touching canvas, and their tests
- **Goal**: no code paths for data that only earlier xournal-qt versions wrote.
- **Files**:
  - `session/PageMargins.{h,cpp}` (`unscaled`, the fallbacks in `pageBox`) and `canvas/MarkdownSession.cpp:181–191`;
  - the tests listed in 4: ToolboxModelTest, AdaptiveLayoutTest:2819, SettingsModelTest:310, HybridPdfTest:2169 and
    PdfTextDocumentTest:473, each together with the code the shell and session reviewers remove.
- **Risk**: low (0 users).
- **Verify**: labels session, canvas, shell, ui.
- **Conflicts**: B6, B12; also the shell and session reviewers' compat blocks (do them together).

### B8. CanvasView, step 1: self-contained parts out
- **Goal**: −900 lines from CanvasView.cpp with no behaviour change.
- **Files**: new `canvas/NavigationHistory.*`, `canvas/PdfTextSelection.*`, `canvas/PageLinks.*`;
  `CanvasView.{h,cpp}`; callers in `app/` and `quick/DocumentCanvasItem.cpp` (keep forwarding methods for one commit,
  then switch callers to `view.pdfText()`, `view.links()` and `view.history()`).
- **Steps**: one class per commit. Move the state and the methods, leave thin forwards, then move the callers.
  Also add `pagePointAt(viewPos)` and replace the 14 inline copies.
- **Risk**: low. Mostly moving code; watch the signals (`pdfTextSelected`, `linkTapped`, `navigationChanged`) and
  re-emit them from CanvasView or connect them through.
- **Verify**: labels canvas, quick, ui (LinkMouseTest, MiddleClickFit, PDF text tests in MainWindowTest).
- **Conflicts**: B9, B10 (CanvasView.*); B5 before.

### B9. CanvasView, step 2: clipboard and text editing
- **Goal**: `CanvasClipboard` (pasteElements 175 lines, pasteText, insertImage, pasteLinkMarker, CanvasStickers.cpp,
  snip links) and `TextEditing` (editors, completion, the Markdown-layer selection juggling of h:505–516).
- **Files**: CanvasView.*, CanvasStickers.cpp, TodoStamp.cpp (its `CanvasView::addTodoStamp` moves with text
  editing), StickyNotes.cpp, MixedSelection.cpp (callers), CanvasInput.cpp (text calls), app/*.
- **Risk**: medium. Selection ownership and `actingScope` must stay where they are; move code, change no logic.
- **Verify**: labels canvas (CanvasReplayTest, MarkdownEditorTest, EmojiEditingTest, StickySelectTest), ui
  (StickerToolTest, SnipTest, CopyToolsTest).
- **Conflicts**: B8, B10, B11, B6.

### B10. CanvasView, step 3: render host and the PDF cache race
- **Goal**: `ViewRasterHost` + `ViewMemory` out of CanvasView, and the fixes for 6.1–6.4.
- **Files**: CanvasView.*, render/PageRaster.cpp (it takes a `shared_ptr<PdfCache>`), CanvasMemory.*.
- **Steps**:
  1. Write a failing test first, under TSan: renders while `replacePdfCache` runs.
  2. Hold the cache as `std::atomic<std::shared_ptr<PdfCache>>`, or behind one mutex, and let the render keep a
     `shared_ptr`. That retires `retiredPdfCaches`.
  3. Pack `RasterParams` into one atomic or a mutex-guarded struct.
- **Risk**: medium to high (threads). This one needs the full suite plus `XQT_BENCH_SCROLL`.
- **Verify**: the TSan build of canvas and shell labels is clean, and the `XQT_PERF=1` numbers are unchanged.
- **Conflicts**: B8, B9.

### B11. CanvasInput split and clock injection
- **Goal**: `touchEvent` under 100 lines per phase, state in `TouchSession`, `WheelMomentum` and `PenHold` as their
  own classes, settings read once (on change).
- **Files**: CanvasInput.*, new `canvas/input/TouchSession.*` etc., ViewController.* (clock),
  tests/canvas/CanvasReplayTest.cpp, quick/CanvasItemInputTest.cpp.
- **Risk**: medium. The touch state machine has many corner cases, but CanvasReplayTest (82 tests) and
  CanvasItemInputTest cover them well.
- **Verify**: labels canvas and quick; the device checklist's "Touch and palm rejection" on the device.
- **Conflicts**: B2 (clock), B9 (text calls).

### B12. Move non-view code out of canvas
- **Goal**: `canvas/` holds the view only. `MarkdownFile`/`MarkdownSession`/`PageResize`/`PageRotate` move to
  `session/`, `DarkPages`/`PagePictures` to `render/`, `ImageFile`/`MdImageDecoder` to `shell/`, and the text editors
  into `canvas/text/`.
- **Files**: those files, their includes in shell/app/tests, and cmake.
- **Risk**: low once B5 is done (mechanical).
- **Verify**: the full build. Shell no longer links `xqt-canvas` for MarkdownFile (check with `cmake --graphviz`).
- **Conflicts**: B4, B5, B6, B7, B9.

### B13. A typed table of our settings
- **Goal**: one table of key, default, range and setter for the 40-odd `xournalQt` and `touch` keys (39 spelled as literals, more via constants such as HoverPointer.cpp:24), replacing the 106
  raw lookups. SettingsModel builds its rows from it, and `data-on-disk.md` is generated from it.
- **Files**: new `session/XqtSettings.{h,cpp}`; CanvasView.cpp:364–380, CanvasInput.cpp:1238/1251, PenGestures.cpp,
  HoverPointer.cpp, SettingsModel.cpp (43 lookups), AppController.cpp (19), the others listed in 2.
- **Risk**: low to medium (defaults must not change; a test can compare old and new defaults key by key).
- **Verify**: labels shell (SettingsModelTest), ui, canvas.
- **Conflicts**: the shell reviewer's SettingsModel work; B11 (CanvasInput).

### B14. Docs restructure (no build needed)
- **Goal**: the structure of 9.3.
- **Steps**, each one commit:
  1. `qt/docs/README.md` + `architecture/overview.md` + `glossary.md`, written from AGENTS "moving parts", the
     CMake graph and this review.
  2. `architecture/architecture.yaml`, with a CI check against `cmake --graphviz`.
  3. `development/testing.md` (after B1/B2) and `development/building.md`.
  4. Move ROADMAP's "Status" section to `qt/CHANGELOG.md`, and its plan, research, audits and ideas to `history/`.
  5. Trim each feature doc to current behaviour, with design history at the end. Extract the hybrid-PDF format spec
     into `architecture/documents.md`.
  6. Cut the device checklist to `development/device-smoke.md` (≤60 checks) and move the per-feature checks into
     the feature docs.
  7. Fix root README.md (drop upstream's lower half or link to it) and record it as a seam in ADR 0002.
- **Risk**: none for the code. The risk is losing information, so move text rather than delete it.
- **Conflicts**: B15.

### B15. AGENTS.md and TODO.md refresh (an hour)
- **AGENTS.md**:
  - correct counts and labels (1865 tests; add `audio`, `hwr`);
  - Qt 6.7 / CMake 3.25;
  - the ctest-version pitfall;
  - `XQT_BUILD_SPIKES=OFF` in the configure line (until B4);
  - the module table extended with `render`, `audio`, `hwr`, `compat`, `cli`, `tools`, `3rdparty`;
  - the dependency direction;
  - a "where to record what" rule (TODO = open, CHANGELOG = merged, features/ = behaviour, decisions/ = why);
  - rule 4 changed to "add the device-only checks to the feature doc", not to a 4000-line checklist.
- **TODO.md**: move every `[x]` and every "merged" section to CHANGELOG (or ROADMAP until B14). Keep about the 52
  open items and the 4 decisions.
- **Conflicts**: B14.

### Order

B15 → B1 → B3 → B6 → B7 → B4 → B5 → B8 → B11 → B2 → B9 → B12 → B14 (alongside, docs only) → B13 → B10.
