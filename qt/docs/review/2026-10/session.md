# Review: session (qt/src/session, render, audio, hwr, markdown)

Scope: `qt/src/session` (99 files, 28.7k lines), `qt/src/render` (1.6k), `qt/src/audio` (2.2k), `qt/src/hwr` (5.3k),
`qt/src/markdown` (8.7k), their tests in `qt/tests/{session,unit,audio,hwr,markdown}` and the docs
`qt/docs/hybrid-pdf.md`, `adopt-annotations.md`, `audio.md`, `timeline.md`, `research/version-history.md`. Checked
at `d6d16ba`. Nothing in the repository was changed.

## Verdict

The layering is mostly sound. Nothing in session, render, audio, markdown or hwr includes shell, app or canvas code;
mentions of them are in comments only. render and audio depend on the core only. The tricky parts are tested well:
writing failures are injected, incremental files are checked with `qpdf --check`, pixel comparisons are made against
full writes, and the two big hooks (`stopSaveAt`, `failWriteAt`) make background saves testable. The problems are
concentration and copy-paste:
- **Two god objects carry most of the risk.** `HybridPdf.cpp` (4,549 lines, one anonymous namespace holding the full
  writer, the incremental writer, version history, the clean-copy cache and the reader) and `DocumentSession`
  (696-line header, about 170 members, spread over `DocumentSession.cpp`, `DocumentSave.cpp`, `DocumentAdopt.cpp`).
- **There is no shared file-writing layer.** The same five helpers are written again and again: temporary file plus
  rename, the file "stamp", FNV-1a, gzip, and a deep page copy. One hand-copied stamp format is silently
  load-bearing.
- **The full and incremental PDF writers each implement the PDF format.** The marker, the invisible text layer of the
  handwriting and the embedded files are built twice, once on a `QPDF` and once on an `IncrementalPdf::Update`.

Backward-compatibility code for xournal-qt's own data is rare here: two real cases, plus two one-time settings flags.
The biggest win per hour is a small `session/FileIo.h` (block 1) followed by mechanical splits of `HybridPdf.cpp` and
`DocumentSession.cpp` (blocks 3 and 4).

---

## 1. Structure

### Sizes (lines)

| File | Lines | Comment |
| --- | --- | --- |
| `session/HybridPdf.cpp` | 4549 | 7 subsystems in one file (see below) |
| `session/DocumentSession.cpp` | 1852 | + `DocumentSave.cpp` 946 + `DocumentAdopt.cpp` 385 = one class, 3.2k lines |
| `markdown/MdLayout.cpp` | 1769 | `class Layouter` alone spans about 1,130 lines (271–1400) |
| `markdown/MdFormat.cpp` | 1081 | `scanLine` is 144 lines |
| `session/AdoptAnnotations.cpp` | 1032 | |
| `session/DocumentTextIndex.cpp` | 981 | `Worker::drain` is 114 lines |
| `session/ArchivePdf.cpp` | 903 | `conform` is 216 lines |
| `session/StickyNote.cpp` | 873 | `draw` is 114 lines |
| `session/PdfPageKeeper.cpp` | 821 | |
| `session/PdfRevisions.cpp` | 763 | `read` is 129 lines, `parseStream` is 122 lines |
| `session/IncrementalPdf.cpp` | 700 | `Update::serializeWith` is 234 lines |
| `session/DocumentSession.h` | 696 | 39 % comment lines |

### `HybridPdf.cpp`: what is in it

All of it except the public functions sits in one anonymous namespace (87–3188), which is why it cannot be split by
moving a function.

| Lines | Subsystem |
| --- | --- |
| 103–748 | Helpers: stamp, FNV, hex, gzip/gunzip, string streams, `writePdfTo`, annotation hashing, `strip`, note-space boxes, ink-text font and stream |
| 749–1210 | `HybridSaveHandler` (a subclass of upstream's `SaveHandler`), `Prepared`, `prepare()`, which draws everything with cairo |
| 1210–1872 | The full write: `basePages`, `formOf`, `annotDict`, `annotate`, `flatten`, `assemble()` (168 lines; writes Plain, Hybrid and Archive files) |
| 1874–3144 | The incremental write: `Existing`, `openExisting` (156 lines), `class Appending` (about 940 lines: `placePages`, `updatePage`, `embedData` 131, `embedAudio` 74, `placeInkText`, `mark`) |
| 3146–3420 | The clean-copy cache: `retained`, `prune`, `markUnpacked`, `removeProtectedLeftovers`, `forgetCopies`, `keepCleanCopy` |
| 3423–3923 | Writing the version history: `rewriteFrom`, `appendWhole`, `storeAsDelta`, `writeKeeping` (157 lines), `writeVersion`, `setVersionMessage`, `keepCacheEntry` |
| 3924–4110 | Public `write`, `writeArchive`, `exportXopp` |
| 4108–4549 | Marker sniffing (`kindOf`), `compact`, recordings, `open()` (150 lines), `importCopy` |

`PdfHistory.cpp` only reads the history. Writing it lives in `HybridPdf.cpp`, so a reader of
`PdfHistory.h` does not find half of the feature.

### `DocumentSession`: what is in it

It implements upstream's shadow `Control` and adds the following:
- page structure and links (`DocumentSession.cpp` 499–1001);
- bookmarks (887–936) and recordings (941–1022);
- file names and the display name (1055–1135);
- **text files** (1135–1222);
- PDF pages (1222–1262);
- the preview and `writeDocument` (1293–1353);
- the on-disk watch (1370–1433);
- PDF with notes and version history (1463–1640);
- encryption (1640–1689);
- autosave and recovery (1689–1852);
- background saves (`DocumentSave.cpp`, a six-step state machine);
- annotation adoption (`DocumentAdopt.cpp`).

The private state confirms the mix (`DocumentSession.h` 627–694):
- about 60 members in 8 unrelated groups;
- `hybridRevision`, `hybridNumbering`, `hybridRevisionFile`, `hybridBase`, `hybridChanges`, `retainedBases`,
  `versionsChoice` and `restoredMessage` form a "PDF with notes" state object that does not exist yet;
- `text`, `textModified`, `textContinuous`, `madeSuggestion`, `lastAutosavedText` and `imageRoot` form a "text file
  session";
- `passwordHolds`, `knownFile`, `protectedFlag`, `olderEncryption`, `allowPrint` and `allowCopy` form a "protection"
  object.

### Module boundaries

- **session is several modules in one folder and one library** (`xqt-session`, `qt/cmake/XqtSession.cmake` 29–132).
  Natural groups:
  - **pdf** (file formats): HybridPdf, IncrementalPdf, PdfRevisions, PdfHistory, ByteDelta, VersionCache, VersionDiff,
    ArchivePdf, MergedPdf, PdfPageKeeper, PdfEncryption, PdfBookmarks, PdfKeywords, PdfTitle, AdoptAnnotations,
    InkTextLayer; about 12k lines.
  - **search**: DocumentSearch, DocumentTextIndex, TextMatch, TextReplace, WordMatch, FuzzyMatch, FuzzyQuery,
    Vocabulary, InkText, Citation; about 5k lines.
  - **document features** (model extensions): StickyNote, ElementGroups, ElementTimes, Timeline, PageNoteSpace,
    PageMargins, PenFill, PageBookmarks, Tags, DocumentLink.
  - **files**: TextFile, TextDocument, DocumentImages, StickerFile, TemplateFile, PageFiles.
  - **the session proper**.
- **Include paths hide the boundaries.** `xqt-session` exports `qt/src/session` as a PUBLIC include directory
  (XqtSession.cmake:129), and `xqt-markdown` exports `qt/src/markdown`. That is why the code mixes `#include
  "HybridPdf.h"` (178 unprefixed includes of session headers in `qt/src`) with `#include "session/HybridPdf.h"` (267).
  Inside session, `HybridPdf.cpp` includes `"MdBox.h"` (markdown) next to `"audio/AudioFiles.h"`.
- **The CMake files are in the wrong place.** `xqt-canvas` and its whole test list are defined in
  `XqtSession.cmake` (138–307). `tests/canvas/PenStylesTest.cpp` is listed twice (lines 288 and 295).
- **A render helper lives above render.** `notespace::renderPdf` (`session/PageNoteSpace.cpp:31`) belongs in render.
  Because render may not depend on session, `render/RegionRender.cpp:87–97` and `render/PageRaster.cpp:241–250`
  carry their own copies of it.
- **The test hook of the save lives in the page keeper.** `PdfPageKeeper::stopSaveAt` (`PdfPageKeeper.h:129`) is the
  test hook of `DocumentSave.cpp` (`stopAt`).
- **Process-global registries are scattered and hard to reset in tests**:
  - the clean-copy refcounts (`HybridPdf.cpp:3148`);
  - the password registry (`PdfEncryption.cpp:66`);
  - the audio search folders (`AudioFiles.cpp:13`);
  - the Markdown image roots (`MdImages.cpp:87`);
  - `VersionCache::instance`;
  - two `Vocabulary` singletons;
  - five static `DocumentHandler`s.

### How many ways a PDF is written

There are 12 distinct writers, with 4 different temporary-file schemes and 1 that `fsync`s.

| # | Writer | Mechanism | Temp file and fsync |
| --- | --- | --- | --- |
| 1 | `HybridPdf::assemble` → `writePdfTo` (HybridPdf.cpp:245), modes Plain, Hybrid and Archive: Save as, full Ctrl+S, Export hybrid, archive, base pages for Xournal++ | QPDFWriter | unique `.name.<pid>-<n>.part`, no fsync |
| 2 | `Appending::run` → `IncrementalPdf::append`: Ctrl+S | own appender | `.part` copy, **fsync** |
| 3 | `appendWhole` (HybridPdf.cpp:3506): history fallback, assembles to `work/whole.pdf`, then `Update::copyAll` | 1 + 2 | both |
| 4 | `rewriteFrom` (3449): the day's version replaced, `serializeOver` | 2 | fsync |
| 5 | `storeAsDelta` (3569) → `rewriteFrom` | 2 | fsync |
| 6 | `writeVersion` (3799): byte prefix (`PdfRevisions::extract`), then append | byte copy + 2 | `.name.<pid>.part`, then fsync |
| 7 | `setVersionMessage` (3851): marker-only append | 2 | fsync |
| 8 | `HybridPdf::compact` (4249), the clean copy in `open` (4438), `importCopy` (4540) | `writePdfTo` | as 1 |
| 9 | `MergedPdf::writeAtomically` (MergedPdf.cpp:52): pasted pages, also used by `AdoptAnnotations.cpp:1020` | QPDFWriter | **fixed** `.name.part`, no fsync |
| 10 | `PdfEncryption::rewrite` (PdfEncryption.cpp:184): protect, unprotect, and the plain export's encryption from `AppController.cpp:5468`; decrypting for printing from `PdfPrinting.cpp:154` | QPDFWriter | its own `partOf` copy (PdfEncryption.cpp:77) |
| 11 | `pdfkeywords::write` (PdfKeywords.cpp:173): tags, called from `AppTags.cpp:127` on the global thread pool | 2 | fsync |
| 12 | upstream `ExportHelper::exportPdf`: Export as plain PDF (`AppController.cpp:5454`), library share (`LibraryShare.cpp:724`), printing (`PdfPrinting.cpp:159`); plus poppler's `getPdfDocument().save()` for an attached PDF (`DocumentSave.cpp:622`) | cairo, poppler | whatever upstream does |

Some formats are also built in memory: `MergedPdf::extract`, `AdoptAnnotations` (an appearance rendered through a
one-page PDF), and the cairo PDF of drawings in `prepare`.

Callers above session also write PDFs directly instead of going through the session or one façade:
- `AppController.cpp:4257` (`exportXopp`), `4360` (`writeArchive`), `4436` and `4459` (`compact`), `5468`
  (`rewrite`);
- `LibraryShare.cpp:563` and `662`;
- `LibraryArchive.cpp:219`;
- `AppTags.cpp:127`.

### Locking of the Document

- The rule "never hold the document lock while drawing a PDF" is kept by `PageRaster`, `RegionRender`, `previewOf`
  (DocumentSave.cpp:137) and `prepare`.
- **`DocumentSession::updatePreview` breaks it** (DocumentSession.cpp:1299–1328): it renders the PDF page under
  `lock_shared`, as a straight port of upstream. Only `writeDocument` uses it, on documents that are not shown, so
  the effect is small. It also duplicates `previewOf`.
- **`takeSnapshot` writes a whole PDF on the UI thread under the shared lock** (DocumentSave.cpp:612–627,
  `doc->getPdfDocument().save(attached)`). This is the attached PDF of a `.xopp`; pen input waits for it.
- Manual `doc->lock()/unlock()` pairs (DocumentSession.cpp 476, 711, 723, 863, 1329, 1336, 1349, 1356, 1726, 1812)
  sit next to RAII locks. They are ports of upstream and not exception-safe. Use `std::unique_lock` throughout.
- `takeSnapshot` (599–610) changes the live document while saving, under a unique lock. It sets `setAttach(true)` on a
  *copy* of `BackgroundImage` and relies on the shared implementation to reach the page. There is no undo entry and
  no modified flag. This is too clever and easy to break.

### Boundary to upstream

- The seams used here (`setPdfPassword`, `readPdfKeepingOutline`, `setDocumentHandler`, `LoadHandler::loadDocument(
  InputStream…)`, `LoadHandler::pdfPassword`) are all listed in ADR 0002.
- Upstream classes are reached through access hacks. Use one helper header, or add a tiny listed seam.
  - `struct XmlAccess: XmlNode { using XmlNode::children; }` appears twice (HybridPdf.cpp:765, VersionDiff.cpp:48).
  - `struct Access: XojPage { using XojPage::setLayerVisible; }` appears twice, inside verbatim copies of `copyOf`
    (DocumentSave.cpp:99–117, PageFiles.cpp:50–63).
- `HybridSaveHandler::visitPage` (HybridPdf.cpp:812–868) rewrites the `<background>` node that upstream's
  `SaveHandler` produced, and depends on attribute order and `firstPdfPageVisited`. This is fragile on upstream
  merges, yet no merge check mentions it. Add it to the "Ported, not reused" table of ADR 0002.
- Outside this area, but found on the way: upstream files with `xournal-qt:` markers that ADR 0002 does not list:
  - `src/util/include/util/TextLinks.h` (new);
  - `control/settings/ButtonConfig.h`, `control/ToolHandler.h`, `control/Tool.h`;
  - `control/tools/EditSelection.h/.cpp`.

## 2. Duplication

| What | Copies |
| --- | --- |
| The file "stamp" (size and mtime) | `HybridPdf.cpp:120` ("-"), `DocumentAdopt.cpp:39` (":"), `PdfPageKeeper.cpp:32` (":"), `shell/PageClipboard.cpp:22` (":"), a lambda in `DocumentSession::setVersionMessage` (DocumentSession.cpp:1491), `TextFile::stampOf`, `DocumentSession::diskStampOf`, `LibraryShare.cpp:459`. **The lambda must match `HybridPdf`'s format byte for byte**, or `hybridRevision` silently goes invalid and every later Ctrl+S writes in full. |
| Temporary file plus rename | `writePdfTo`, `MergedPdf::writeAtomically`, `PdfEncryption::rewrite`, `PdfPageKeeper::copyAtomically` (:47), `PdfPageKeeper::commitFile` (:759), `PdfRevisions::extract` (:731), `IncrementalPdf::append`, `HybridPdf::exportXopp` (4082), `TemplateFile.cpp:28`. There are 3 copies of `partOf` (HybridPdf:231, PdfEncryption:77, IncrementalPdf:138). |
| FNV-1a 64 | 9 copies: HybridPdf.cpp:131 and 762, IncrementalPdf.cpp:243, PdfPageKeeper.cpp:111, DocumentImages.cpp:54, InkTextLayer.cpp:80, VersionDiff.cpp:24, MdImages.cpp:185, hwr/InkLayout.cpp:147 |
| gzip and gunzip | `HybridPdf.cpp:151/171` (`gzipped`, `gunzipped`) against `PdfHistory.cpp:55/77` (`gzip`, `gunzip`); HybridPdf uses both pairs. `IncrementalPdf.cpp:35` has `deflate`. |
| MD5 of a buffer, inline | HybridPdf.cpp:592, 2184, 2820, 2891 |
| A deep page copy | `DocumentSave.cpp:99` = `PageFiles.cpp:50`, character for character |
| A static `DocumentHandler` with no listeners | DocumentSession.cpp:102, DocumentSave.cpp:83, StickerFile.cpp:25, TemplateFile.cpp:21, PageFiles.cpp:22 (plus 2 in canvas) |
| `hasExtension` | DocumentSession.cpp:107 = DocumentSave.cpp:88 (and `lowerExtension` and `isPdf` in app) |
| The 128 px file preview (a port of `SaveJob::updatePreview`) | `DocumentSession::updatePreview` (1293) and `previewOf` (DocumentSave.cpp:137) |
| Drawing a PDF page at its note-space offset | `notespace::renderPdf`, `RegionRender.cpp:87`, `PageRaster.cpp:241` |
| The two save paths for a `.xopp` | `writeXoppFile` (DocumentSave.cpp:166) and `DocumentSession::writeDocument` (DocumentSession.cpp:1334), both PictureSaveHandler plus carried pictures; `autosave` (1712) is a third |
| The full and incremental marker writers | `assemble` (1768–1825) and `Appending::mark` (3059–3112) set the same 12 keys (`/InkText`, `/InkFont`, `/Files`, `/Audio`, `/Annots`, `/Flattened`, `/XoppExport`, `/Drawn`, `/Spaces`, `/Layers`, history, `/Producer`/`/ModDate`) |
| The ink text layer | `assemble` 1773–1799 and `Appending::placeInkText` 3019–3057 (the sig loop, the font, the stream, `putInkText`) |
| Embedded files | `assemble` 1755–1771 (`addEmbedded`, `addEmbeddedFile`) and `Appending::embedData`/`embedAudio` (2810–3018, with MD5 reuse) |
| "Try incremental, else full" | `write()` (3942–3984) and `writeKeeping()` (3742–3787) have the same try/catch/`openExisting`/`Appending`/`keepCleanCopy` block |
| `ScreenDrawing` and `PageRaster::ScreenScope` | PageRaster.cpp:29 and 37: the same thread-local flag; the local one does not restore the previous value |

`PdfBookmarks::write(QPDF&, entries, Update*)` already shows how one function can serve both writers. The marker,
ink-text and embedded-file code should follow it (block 5).

## 3. Dead code

Each entry was checked with grep over `qt/src` and `qt/tests`, QML included.

| Item | Where | Note |
| --- | --- | --- |
| `LoadResult::fileVersion`, `isNewerFileVersion()` | DocumentSession.h:69–70, .cpp:114 | set, never read |
| `getHybridChanges()` | DocumentSession.h:210 | no caller |
| `recordingName()` | DocumentSession.h:509 | no caller |
| signal `audioChanged` | DocumentSession.h:574 | emitted 3×, connected only in `tests/canvas/AudioDocumentTest.cpp:175`. Either dead or a missing UI refresh: decide. |
| signal `AppContext::selectionColorChangeRequested` | AppContext.h:64, .cpp:123 | no receiver; the override `changeColorOfSelection` can be empty |
| `DocumentTextIndex::pagesMissing()` | DocumentTextIndex.h:134 | no caller |
| `DocumentTextIndex::setStartDelay` | DocumentTextIndex.h:121, .cpp:432 | no caller; `startDelayMs` can be `constexpr` |
| `PdfEncryption::loadPoppler` | PdfEncryption.h:127, .cpp:346 | no caller (still named in the header comment, line 10) |
| `SessionActions::getActionState` | SessionActions.h:26 | no caller |
| `RenderService::threadCount` | RenderService.h:65 | no caller |
| `RasterHost::rasterMarkAudioStrokes` | PageRaster.h:63 | virtual, never overridden: always false |
| `VorbisWriter::bytesWritten` | OggVorbis.h:51 | no caller |
| `CtcRecognizer::manifestRead` | CtcRecognizer.h:74 | no caller |
| `LanguagePlan::probed` | LanguagePlan.h:60 | no caller |
| `OrtRuntime::libraryPath` | OrtRuntime.h:35 | no caller |
| `HybridPdf::keepCacheEntry` | HybridPdf.h:205 | only used inside HybridPdf.cpp: make it file-local |

Stale docs:
- `hybrid-pdf.md` opens with "design draft" (line 1). Its "Decided" section (86–93) says "Notes … go to a new file
  by default", which the PDF-only mode (596ff) overrides.
- Its "Build plan" (95–102) is history.
- The qpdf 10.6 measurement tables (138–148, 566) are superseded by the qpdf 12.4 table and are kept next to it.
- `research/version-history.md` (270 lines) restates what `hybrid-pdf.md` "Version history" now specifies.

## 4. Backward compatibility to remove (xournal-qt's own data)

| # | Code | What it keeps alive | Removing it means |
| --- | --- | --- | --- |
| 1 | `TextDocument::migrateBookmarks` (TextDocument.cpp:115–158, called at DocumentSession.cpp:121), test `PdfTextDocumentTest.cpp:470` | Bookmarks that the `qt/bookmarks` build wrote as `xqt-bookmark` page attributes on pages of a *text* document become Markdown comments | Delete the function, its call and the test. `syncBookmarks` then derives the attributes from the comments only. |
| 2 | HybridPdf "files of older versions" (HybridPdf.cpp:1894 `recorded`, 2078–2108 the page scan in `openExisting`, `!e.recorded` in `unchanged` 2514; `qt/docs/hybrid-pdf.md` "Files of earlier versions … are appended to as well"), test `IncrementalSaveTest.aFileOfAnEarlierVersionIsAppendedTo` (HybridPdfTest.cpp:2171) | Incremental saves onto PDFs with notes written before `/Layers` and the sigs existed | In `openExisting`, `!marker.hasKey("/Layers")` → `return nullptr` with `why = "no record"` (a full write). About 35 lines and one test go. |
| 3 | `AppContext` one-time flags `stylusButtonsSet` and `snapGridSet` (AppContext.cpp:49–64) | Fork defaults pushed once onto existing settings of earlier xournal-qt builds | Apply the defaults only when `settings.xml` did not exist (a fresh profile), and drop the two keys. Keep this if the author also wants it for profiles created by upstream; the fork has its own config folder (`XOJ_CONFIG_FOLDER_NAME`). |
| 4 | `StickyNote.cpp:274–278`, clipboard type `"StickyNote3"` | Not compatibility, only a format tag; nothing reads an older name | Keep, but name it `StickyNote` (the number is history). |

Not found: version checks or upgrades of the cache folders (`hybrid-pdf/`, `versions/`, `md-assets/`, `originals/`
are keyed by stamp or hash), and old attachment names (`audio-p…`, `document.xopp.bg_N.png` have one spelling each).
The marker's `/Version 1/2` check (HybridPdf.cpp:1986, 4401) protects against *newer* files: keep it.

## 5. Readability

- **The comments.** Header comments are behaviour specs: what, when, why, with links to the doc section. They
  help, and they are the reason this code can be reviewed at all. They bury the API, though:
  - in `DocumentSession.h` 274 of 696 lines are comments, and declarations of unrelated features alternate every
    3–6 lines;
  - in the .cpp files the dominant style is the trailing parenthetical (`// (a hybrid PDF as the background; …)`,
    91 of them in HybridPdf.cpp). Some are split across code lines: HybridPdf.cpp:1762–1764 runs one sentence over
    three lines of a nested `if`.

  Rule for the refactor: one comment block per function, above it; trailing comments only for a single fact.
- **Naming.** The UI and half the docs say "PDF with notes"; the code says `HybridPdf`, `isHybrid`, `saveAsHybrid`,
  `SaveKind::Hybrid`, `hybridRevision` (284 "hybrid" against 38 "PDF with notes" in session). Pick one in code (keep
  `HybridPdf`, and say so in the header). Some names do not say what they hold:
  - `Revision` (HybridPdf.h:57) is a file stamp plus a page map, and is confused with `PdfRevisions::Revision`;
  - `Existing` is an open file state;
  - `Appending` is the incremental writer;
  - `Kind` comes in three meanings: `MergedPdf::Kind`, `PdfHistory::Kind` (string constants) and
    `HybridPdf::{anon}::Kind` (int 0/1/2, HybridPdf.cpp:4109).
- **Long parameter lists.** `prepare(doc, pdfName, work, baseOf, pdfPageCount, attach, linkFolder, linkMap, reuse,
  audioNames)` (HybridPdf.cpp:1002) takes 10 positional parameters and is called 5 times with
  `false, target.parent_path(), nullptr, &existing->reuse`. Use a `PrepareOptions` struct. `HybridPdf::write`
  takes 6 parameters, 4 of them default-valued, and an options struct that already exists for half of them.
- **Indentation slip.** HybridPdf.cpp:3955 `prep.encryption = options.encryption;` is indented 8 spaces inside a
  20-space block: clang-format is not run on this file.
- **Overly clever code**:
  - member-pointer access to protected members (see section 1);
  - `InkTextLayer::makeFont` (InkTextLayer.cpp:120, 129 lines) writes a TrueType font byte by byte. A generated,
    checked-in byte array (like `SrgbIcc.cpp`) is easier to trust;
  - `BackgroundImage` shared-content mutation (DocumentSave.cpp:606).
- **Functions over 80 lines** (rough count): `Update::serializeWith` 234, `ArchivePdf::conform` 216, `takeSnapshot`
  206 (its worker lambda alone, 694–792, about 100 lines with five save kinds), `FuzzyMatch::match` 176,
  `Timeline::build` 166, `openExisting` 156, `HybridPdf::open` 150, `prepare` 149, `scanLine` 144, `strip` 131,
  `embedData` 131, `InkLayout::layout` 130, `PdfRevisions::read` 129, `parseStream` 122, `applyAdoption` 121,
  `TrocrRecognizer::readPicture` 116, `StickyNote::draw` 114, `Worker::drain` 114, `planFiles` 111,
  `BeamSearch` 102, `loadFile` 100.
- **Magic numbers and constants are mostly named** (`compactAbove`, `KEYFRAME_EVERY`, `ORIGINALS_KEPT`,
  `WORK_FOLDER_DAYS`). Exceptions:
  - `previewSize = 128` is written twice;
  - `2.0` MARGIN is fine;
  - `1600.0` and `4.0` in AdoptAnnotations.cpp:534;
  - the PdfHistory delta threshold `* 2 >` (HybridPdf.cpp:3607).
- **Logging** mixes `g_warning`, `g_message` and `std::fprintf(stderr, …)`, behind `XQT_HYBRID_TIMES`
  (`Steps`, HybridPdf.cpp:106).

## 6. Correctness risks (with lines)

1. **A tag change writes the PDF behind the session's back.** `AppController::setDocumentTags` (AppTags.cpp:116–147)
   checks `isModified() || isSaving()` on the UI thread, then runs `pdfkeywords::write` on
   `QThreadPool::globalInstance()`. Nothing stops a Ctrl+S from starting in between, and then two writers
   copy-and-rename the same file, with one update lost. On a PDF with version history, the tags revision has no
   `/History /Start`. `PdfHistory::list` (PdfHistory.cpp:206–238) therefore counts it as **another app's**
   revision:
   - the day's version can no longer be replaced (`replacesLast` needs `lastIsOurs`);
   - "Save with a message" refuses ("changed by another app", HybridPdf.cpp:3855);
   - the version list shows a foreign revision.

   It also changes the stamp, so the next open rebuilds the clean copy (about 5 s at 1,300 pages). No test covers tags
   together with history. Fix: route tag writes through the session (a `SaveRequest` kind, or the marker-only
   update `setVersionMessage` uses, which keeps history and the cache entry).
2. **The UI thread writes a PDF under the document lock**: DocumentSave.cpp:612–627 (see "Locking").
3. **Full writes never fsync** (`writePdfTo`, `MergedPdf::writeAtomically`, `PdfEncryption::rewrite`), while
   `IncrementalPdf::append` does (IncrementalPdf.cpp:685). In PDF files mode the first save renames a new file over
   the user's own PDF. The cache original (DocumentSave.cpp:203) softens this, but a power loss after the rename can
   still leave a truncated file on non-ext4 file systems. One `AtomicFile` with fsync fixes all of them.
4. **`MergedPdf::writeAtomically` uses a fixed temporary name** `.name.part` (MergedPdf.cpp:54), unlike the unique
   `partOf` elsewhere ("several threads may make the same clean copy at once", HybridPdf.cpp:230). Two writers of the
   same target, for example adoption copies in the cache from two tabs of one PDF, collide.
5. **`LoadHandler::pdfPassword` is assigned on every `loadFile`** (DocumentSession.cpp:188), and `loadFile` runs on
   library and preview workers. The assignment is a non-atomic write of a global from several threads (formally a
   data race, with the same value each time). Install it once in `AppContext` with the other hooks
   (AppContext.cpp:26–29).
6. **The stamp format is coupled by copy**: DocumentSession.cpp:1491 against HybridPdf.cpp:120 (section 2). Any
   change to one silently disables incremental saves after "Save with a message".
7. **Plain autosave runs on the UI thread**: `DocumentSession::autosave` (1711–1749) gzips the whole document
   synchronously, even though `saveInBackground` exists and the protected-document autosave already uses it
   (1690–1707). On long documents the autosave timer causes a visible hitch.
8. Smaller items:
   - `HybridSaveHandler` depends on the attribute order and the node layout of upstream's `SaveHandler` output
     (section 1);
   - `DocumentSession::setVersionMessage` and `HybridPdf::setVersionMessage` run qpdf synchronously on the UI
     thread;
   - the AES-CBC of incremental updates is hand-written (PdfEncryption.cpp:386–410) over qpdf's block cipher. It is
     tested, but keep it small and documented as such.

## 7. Tests

- **Size and layout.** `tests/session/HybridPdfTest.cpp` has 2,314 lines with 3 fixtures (`HybridPdfTest`,
  `ArchivePdfTest`, `IncrementalSaveTest`) and 3 benchmarks (`benchSaveAndOpen`, `benchCtrlS`, `benchOneSave`) that
  skip without an environment variable. Split it into 3 test files plus `HybridPdfBench.cpp`.
- **Global test knobs**:
  - `HybridPdf::compactAbove` is set by tests in 6 files. They restore it in 3 ways: RAII in TextPdfTest.cpp:301,
    a hard-coded `0.25` in NoteSpaceTest.cpp:506, and no restore when an assertion fails in
    MainWindowTest.cpp:7310–7314. Add a scoped `CompactAboveScope` in a shared test header.
  - `PdfPageKeeper::stopSaveAt` should move with the save code (`DocumentSession::testHooks`).
- **Fixed waits are few**:
  - `BackgroundSaveTest.cpp:341` (`sleep_for(300ms)`, to prove that `s.reset()` waits);
  - `PageRasterTest.cpp:485` (60 ms "nothing started" window, inherent to the check);
  - hwr `RecognizerTest.cpp:109` and `TrocrTest.cpp:271`.

  Replace the 300 ms with a latch: the releaser waits until the destructor has entered the wait. The others are
  acceptable.
- **Coupled to implementation**: `HybridPdf::markerReads()` counts reads (`LibraryKindsTest.cpp:182`), and some
  tests assert exact appended byte counts (IncrementalSaveTest). Both are deliberate performance guards; keep them,
  but behind a `perf` label.
- **Gaps**:
  - (a) tags written on a PDF with history and an open tab (risk 1);
  - (b) a full save interrupted (only incremental appends have failure injection, `failWriteAt`);
  - (c) concurrent `loadFile` of encrypted `.xopp` backgrounds on two threads (risk 5);
  - (d) `DocumentSession` itself has 297 lines of direct tests (`DocumentSessionTest.cpp`) for about 3,200 lines of
    code. Most coverage comes through shell and UI tests, which are slow (CitationsTest and TimelineUiTest run
    3–7 s each). Page-structure operations (`applyPageOrder`, `movePages`, link rewriting) deserve session-level
    tests before the split in block 4.
- **Session tests cost**: `BackgroundSaveTest.theEventLoopKeepsRunningDuring{Hybrid,Xopp}Save` take 2.2 s and 2.0 s
  each. They build 40×60-stroke documents and could use half the size.

---

## Proposed refactoring blocks

Ordered by maintainability gained per effort. Each block builds and passes `ctest -L session -L unit` (plus the
named labels) on its own.

### Block 1: `session/FileIo.h`, one home for the repeated file helpers (small, high value)
- **Goal**: one implementation each of the stamp, atomic write, FNV-1a, gzip/gunzip, MD5 hex and `hasExtension`.
- **Files**:
  - new `qt/src/session/FileIo.h/.cpp`;
  - edits in HybridPdf.cpp, IncrementalPdf.cpp, PdfEncryption.cpp, MergedPdf.cpp, PdfPageKeeper.cpp,
    PdfRevisions.cpp, DocumentAdopt.cpp, DocumentSession.cpp, DocumentSave.cpp, PdfHistory.cpp, DocumentImages.cpp,
    InkTextLayer.cpp, VersionDiff.cpp, TemplateFile.cpp;
  - optionally `shell/PageClipboard.cpp`, `markdown/MdImages.cpp`, `hwr/InkLayout.cpp`. They may include session,
    except markdown: give markdown its own copy, or put FNV in a header-only `qt/src/util/Fnv.h`.
- **Steps**:
  1. Add `fileStamp(path)` (choose one separator) and `AtomicFile` (a unique `partOf`, write, optional
     `fsync`, rename, cleanup), plus `fnv1a`, `gzip`, `gunzip(ok)`, `md5Hex`, `hasExtension`.
  2. Replace the copies one file at a time. Keep `IncrementalPdf::append`'s copy-and-append logic and only use
     `AtomicFile` for the temporary file.
  3. Turn on fsync in `writePdfTo`, `MergedPdf::writeAtomically` and `PdfEncryption::rewrite`.
  4. Delete the stamp lambda in `DocumentSession::setVersionMessage`.
- **Risk**: changing the stamp separator invalidates the clean-copy cache keys once (harmless at 0 users). Changing
  the gzip level would change the bytes of the embedded `.xopp`; keep `Z_DEFAULT_COMPRESSION`, because
  `theEmbeddedXoppIsTheSameBytesForTheSameDocument` depends on it.
- **Verify**: `-L session`, `-L unit`, `-R 'IncrementalSave|PdfHistory|PdfEncryption|MergedPdf'`.
- **Conflicts**: blocks 3, 4, 5 and 7 touch the same files. Do this one first.

### Block 2: remove dead code and the xournal-qt compatibility paths (small)
- **Goal**: the lists in sections 3 and 4.
- **Files**: DocumentSession.h/.cpp, AppContext.h/.cpp, DocumentTextIndex.h/.cpp, PdfEncryption.h/.cpp,
  SessionActions.h/.cpp, RenderService.h, PageRaster.h/.cpp, OggVorbis.h/.cpp, CtcRecognizer.h, LanguagePlan.h/.cpp,
  OrtRuntime.h/.cpp, TextDocument.h/.cpp, HybridPdf.cpp/.h; tests `PdfTextDocumentTest.cpp:470…`,
  `HybridPdfTest.cpp:2171…`.
- **Steps**:
  1. Delete the dead members.
  2. Decide on `audioChanged`: connect it where the page sidebar shows memos, or delete it.
  3. Delete `migrateBookmarks` and its test.
  4. Make `openExisting` return nullptr without `/Layers`, delete the page-scan branch and `recorded`, and delete
     the test.
  5. Optionally rework the two `AppContext` once-flags (ask the author).
  6. Make `keepCacheEntry` file-local.
- **Risk**: low. `rasterMarkAudioStrokes` removal touches `localView.setMarkAudioStroke` (PageRaster.cpp:213):
  keep the call with `false`, or drop it if upstream's default is false.
- **Verify**: `-L session -L unit -L canvas -L hwr -L audio`; `xqt-ui-tests -R Audio` for `audioChanged`.
- **Conflicts**: block 3 (HybridPdf.cpp) and block 4 (DocumentSession). Do it before both, right after block 1.

### Block 3: split `HybridPdf.cpp` mechanically (medium)
- **Goal**: no behaviour change; files of 300–1,300 lines with one subsystem each.
- **Files**: new internal header `qt/src/session/pdf/HybridInternal.h` (namespace `xqt::HybridPdf::detail`: the
  constants, `PageSpec`, `AnnotSpec`, `LinkSpec`, `Prepared`, `Reuse`, `Mode`, `HistoryMark`, `Revision` helpers,
  `isOurs`, `hashOf`, `strip`, `writePdfTo`, `Steps`), and:

  | New file | Contents | About |
  | --- | --- | --- |
  | `HybridPrepare.cpp` | `HybridSaveHandler`, `prepare`, `addInkWords` | 450 lines |
  | `HybridFullWrite.cpp` | `basePages`, `formOf`, `annotDict`, `annotate`, `flatten`, `assemble` | 650 lines |
  | `HybridAppend.cpp` | `Existing`, `openExisting`, `Appending` | 1,270 lines |
  | `HybridHistory.cpp` | `rewriteFrom`, `appendWhole`, `storeAsDelta`, `writeKeeping`, `writeVersion`, `setVersionMessage` | 500 lines |
  | `HybridCache.cpp` | retain/release/touch/prune, protected leftovers, `forgetCopies`, `keepCleanCopy`, `keepCacheEntry` | 300 lines |
  | `HybridOpen.cpp` | `kindOf`, `markerOf`, `open`, `importCopy`, `compact`, recordings | 450 lines |
  | `HybridPdf.cpp` | `write`, `writeArchive`, `exportXopp` | 200 lines |
- **Steps**:
  1. Create the internal header by moving declarations; the functions stay in HybridPdf.cpp but leave the
     anonymous namespace.
  2. Move one group per commit, compiling after each.
  3. Then replace the 10-argument `prepare` with a `PrepareOptions` struct.
- **Risk**: ODR and name clashes once things leave the anonymous namespace (`Kind`, `stampOf`, `partOf`). The
  `detail` namespace plus block 1 removes most of them.
- **Verify**: `-L session` (HybridPdfTest, PdfHistoryTest, PdfEncryptionTest, AudioStorageTest, NoteSpaceTest),
  `-L shell -R 'Library|Tags|PdfOnly|TextPdf'`.
- **Conflicts**: blocks 1, 2, 5, and block 7 (`write`/`writeKeeping`).

### Block 4: split `DocumentSession` (medium to large, two stages)
- **Goal**: a session that owns parts instead of being them.
- **Stage A** (mechanical, low risk): move the definitions into `DocumentPages.cpp` (page structure, links,
  bookmarks, recordings), `DocumentTextFile.cpp`, `DocumentAutosave.cpp` (autosave, recovery, autosave paths),
  `DocumentPdfNotes.cpp` (hybrid, versions, import, detach, `xoppExport`) and `DocumentProtection.cpp`. Leave
  `DocumentSession.cpp` with `loadFile`, the constructors and `Control`. Group the header by the same sections.
- **Stage B**: move the state into member objects:
  - `PdfNotesState` (`hybridRevision`, `hybridNumbering`, `hybridRevisionFile`, `hybridBase`, `hybridChanges`,
    `retainedBases`, `versionsChoice`, `restoredMessage`, the `xoppExport` cache);
  - `TextFileState`;
  - `Protection`;
  - `DiskWatch` (`diskStamps`, `filesOnDisk`, `filesChangedOnDisk`, `stampFiles`).

  DocumentSession keeps thin forwarding methods first, so the 36 calls in AppController and the shell callers
  compile unchanged. Remove the forwarding later.
- **Also**:
  - replace `copyOf` duplicates and `detachedHandler()` copies with block 1's helpers;
  - fold `DocumentSession::updatePreview` into `previewOf`, drawing the PDF outside the lock;
  - split `takeSnapshot`'s worker lambda into one function per save kind (`writeExportXopp`, `writeArchiveCopy`,
    `writePdfWithNotes`, `writeXoppWithStaged`).
- **Risk**: medium. The save state machine reads many of these members (`takeSnapshot` 648–676), so keep the
  accessors exact. Write the session-level page-structure tests from section 7(d) **before** stage B.
- **Verify**: `-L session -L canvas -L shell`, then the full suite once (UI tests use the session heavily).
- **Conflicts**: blocks 1, 2 and 6 (`DocumentSave.cpp`, `DocumentSession.cpp`).

### Block 5: one marker, ink-text and embedded-file writer for both write paths (medium)
- **Goal**: remove the parallel implementations in `assemble` and `Appending`.
- **Files**: after block 3, `HybridFullWrite.cpp` and `HybridAppend.cpp`, plus a new `HybridMarker.cpp`.
- **Steps**:
  1. Introduce `struct ObjectSink { add; addStream; touch; isNew; }` with a `QPDF` implementation (makeIndirectObject
     or newStream, touch a no-op) and an `IncrementalPdf::Update` one. `makeInkFont` already takes these two lambdas.
  2. Write `writeMarker(sink, marker, MarkerContent)` once, then `placeInkText(sink, …)` with an "unchanged → skip"
     callback, then `embedFiles(sink, …, reuseByMd5)`.
  3. Port `assemble` first; the tests compare full against incremental output.
- **Risk**: medium. The incremental path keeps unchanged objects untouched, so the shared code must not touch
  objects whose sig did not change. The `IncrementalSaveTest` byte-count assertions catch regressions.
- **Verify**: `-L session` (especially `ctrlSAppendsOnlyWhatChanged`, `manySavesLookAndOpenLikeFullWrites`,
  `anArchivePdfStaysPdfAAfterIncrementalSaves`) and the veraPDF CI step.
- **Conflicts**: block 3 (do it after) and block 7.

### Block 6: Document locking and UI-thread I/O fixes (small, needs failing tests first: rule 5)
- **Goal**: risks 2, 5 and 7, and the manual lock pairs.
- **Files**: DocumentSave.cpp (`takeSnapshot` attached PDF: move the `getPdfDocument().save` to the worker step 2
  as a planned file, or copy the source PDF file by path), DocumentSession.cpp (manual `lock()`/`unlock()` →
  `std::unique_lock`; `LoadHandler::pdfPassword` moved to `AppContext`; plain autosave through a `SaveKind::Autosave`
  on the save worker).
- **Risk**: medium for autosave. A recovery file must never be half-written, and autosave already uses
  `~`/`.swap`; keep that on the worker.
- **Verify**: `-L session -R 'BackgroundSave|Autosave|Recovery'`, and `-L shell -R SessionRecovery`. Add a test in
  which an attached-PDF `.xopp` save keeps the event loop running (as the existing `theEventLoopKeepsRunning…`).
- **Conflicts**: block 4.

### Block 7: one entry point for writing PDFs (medium)
- **Goal**: callers above session stop calling `HybridPdf::{write,writeArchive,exportXopp,compact}`,
  `PdfEncryption::rewrite` and `pdfkeywords::write` directly. Fix risk 1.
- **Files**: new `qt/src/session/PdfWriter.h` (a façade: `exportXopp`, `exportArchive`, `compactCopy`, `protect`,
  `writeTags`, each taking a loaded `Document` or a path and returning one `Result`); AppController.cpp (4257, 4360,
  4436, 4459, 5468), AppTags.cpp:127, LibraryShare.cpp (563, 662), LibraryArchive.cpp:219, PdfPrinting.cpp:154.
- **Steps**:
  1. Write a failing test: tags written while a session has history on → the version list shows no foreign revision,
     and "Save with a message" still works.
  2. Make the tag write a marker-style append that updates `/History /Start` and calls `keepCacheEntry`, or route
     it through `DocumentSession` when a tab has the file open (queued after a running save).
  3. Move the call sites behind the façade.
- **Risk**: medium (UI flows: share, library archive, encryption).
- **Verify**: `-L session -L shell -R 'Tags|LibraryShare|LibraryArchive|Encryption|Print'`, then the full suite.
- **Conflicts**: blocks 3 and 5 (HybridPdf API), and app/shell blocks of other reviews.

### Block 8: put render helpers in render and fix the CMake homes (small)
- **Steps**:
  1. Move `notespace::renderPdf` to `render/PdfBackground.{h,cpp}` (`drawPdfAtOffset(cr, pdf, width, height,
     NoteSpace, forPrinting)`) and use it in `PageRaster.cpp:241`, `RegionRender.cpp:87`, `previewOf`,
     `PdfPageKeeper.cpp:325`, Thumbnails and Annotations.
  2. Merge `ScreenDrawing` into `ScreenScope`.
  3. Move `xqt-canvas` and `xqt-canvas-tests` into a new `qt/cmake/XqtCanvas.cmake`.
  4. Drop the duplicate `PenStylesTest.cpp`.
  5. Stop exporting `qt/src/session` as a PUBLIC include directory, and fix the 178 unprefixed includes (a sed
     pass).
- **Risk**: low. The include change is wide but mechanical, and it conflicts with every open branch: do it at a
  quiet moment.
- **Verify**: a full build, `-L unit -L session -L canvas`, `golden-quick`.
- **Conflicts**: anything that edits includes. Do it last, or between rounds.

### Block 9: tests hygiene (small)
- **Steps**:
  1. Split `HybridPdfTest.cpp` into `HybridPdfTest`, `ArchivePdfTest`, `IncrementalSaveTest` and `HybridPdfBench`.
  2. Add a shared `CompactAboveScope` RAII helper in `qt/tests/` and use it in the 6 files.
  3. Replace the 300 ms sleep in `BackgroundSaveTest.cpp:341` with a latch.
  4. Halve the stroke counts of the two 2 s event-loop tests.
  5. Put `markerReads` and byte-count assertions behind a `perf` label, or keep them and document them as
     performance guards.
- **Verify**: `-L session`, and compare the timing before and after.
- **Conflicts**: block 2 (it deletes one test in HybridPdfTest.cpp). Do block 2 first.

### Block 10: docs (small, no build)
- Turn `hybrid-pdf.md` (908 lines, 8 features) into a format spec (`pdf-with-notes.md`: the file, the marker, opening,
  the clean copy) plus `pdf-incremental.md`, `pdf-history.md`, `pdf-encryption.md` and `archive-pdf.md`.
- Delete "design draft", the "Build plan" and the qpdf 10.6 tables, and resolve "Decided 1" against PDF-only mode.
- Reduce `research/version-history.md` to the research part, or delete it.
- Add `HybridSaveHandler` (a subclass that rewrites upstream XML) to ADR 0002 "Ported, not reused", and list the 6
  unlisted seam files.
- **Conflicts**: none.

**Suggested order**: 1 → 2 → 9 → 3 → 5 → 6 → 4 (A, then B) → 7 → 8 → 10. Blocks 9 and 10 can run in parallel with
anything.
