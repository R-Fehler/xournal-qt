# Version history inside the PDF: research and plan (2026-10-05; confirmed with changes, see "Confirmed by the author")

Status: **built** in `qt/pdf-history` as the section "Confirmed by the author" says (see [hybrid-pdf.md](../hybrid-pdf.md), "Version history"); compare and play and pruning are not. Before: a proposal for the author to confirm. The author, 2026-10-05: "a fully version controlled
PDF document leveraging the append saving … enable/disable that on documents … a version sidebar … the save date as
the commit message … commit-like messages on save … for milestones … the progression of an ink-based lecture".
Background: [ideas-2026-10.md](../ideas-2026-10.md) B6 (versions inside the PDF) and B9 (the timeline),
[hybrid-pdf.md](../hybrid-pdf.md) ("Saving: incremental updates").

## What the code gives us today

- **Every Ctrl+S of a PDF with notes appends one revision** (`HybridPdf::write` → `Appending::run` →
  `IncrementalPdf::append`): the changed pages, the new or rewritten annotations and drawings (`/AP`), the page tree
  root when pages moved, a **new stream with the whole embedded `document.xopp`**, the marker (`/Annots` hashes,
  `/Layers` record with one sig per layer, `/Drawn`, `/Spaces`, `/Base`, `/Updates`), `/Info /ModDate`, and an xref
  section with `/Prev`. Written to a copy, `fsync`ed, renamed: the file never has a half-written tail.
- So **an earlier revision is a prefix of the file**: cut after its `%%EOF` and it is the complete file as saved
  then, with its own marker and its own complete `.xopp` (exact ink, no reconstruction). Its date is that
  revision's `/Info /ModDate`; which pages changed follows from the `/Layers` sigs of two revisions.
- **History is destroyed today** by: the 25 % growth rule (`compactAbove`), "more than a quarter of the pages
  new/removed", every fallback to a full write (another app saved, edited in another app, encrypted, page tree not
  flat, other images, an exception), Save as (`SaveRequest::compact`), and **Share, which compacts the user's file in
  place** (`AppController::shareFile` → `HybridPdf::compact`, `shareStep` asks for a compacting save).
- In PDF files mode the first save of a user's PDF is a full write, so **the PDF as received is not in the chain**
  (only a 30-day copy in `~/.cache/xournal-qt/originals/`).
- Autosaves and crash saves are `.xopp` files in the cache; they never touch the PDF. So "autosave excluded" is
  already true: a version can only come from a real save.

## What the standards and other apps do

- **ISO 32000-1, 7.5.6**: an incremental update appends changed objects, an xref section and a trailer with
  `/Prev`; earlier bytes stay. Truncating after an earlier `%%EOF` gives that revision back; forensic tools rely on
  it ([SANS ISC](https://isc.sans.edu/diary/25904), [pdforensic](https://github.com/genie360s/pdforensic)).
- **Acrobat**: "Save" appends, "Save As" rewrites and drops all earlier revisions
  ([Adobe community](https://community.adobe.com/questions-9/save-vs-save-as-1232898)). Its only revision UI is for
  signatures: each signature covers a revision, and "View Signed Version" opens that prefix
  ([Adobe community](https://community.adobe.com/t5/acrobat-discussions/adobe-acrobat-when-doing-a-signature-which-invalidates-previous-signature/td-p/12833566)).
  Nobody shows unsigned revisions as versions: this would be new.
- **qpdf** always writes one flattened revision and cannot select an earlier one
  ([qpdf #22](https://github.com/qpdf/qpdf/issues/22), [TODO](https://github.com/qpdf/qpdf/blob/main/TODO.md)).
  Truncation is enough: qpdf opens the prefix as a normal file. Our appender already does the writing qpdf cannot.
- **PDF/A-2/3** allow incremental updates (we already validate an archive PDF saved three times with veraPDF).
  `xmpMM:History` is a "should" for provenance ([PDF/A TechNote 0010](https://pdfa.org/wp-content/uploads/2017/07/TechNote0010.pdf)):
  a possible mirror for archive PDFs, not the store.
- **Google Docs**: automatic versions plus *named versions* ("Name current version", "Only show named versions");
  naming keeps a version from being merged away, at most 40 named
  ([MakeUseOf](https://www.makeuseof.com/how-to-use-version-history-in-google-docs/),
  [Android Police](https://www.androidpolice.com/google-docs-version-history/)). This is the model to copy.
- **OneNote**: page versions, read-only with a banner offering restore/delete/copy; cloud notebooks only
  ([CustomGuide](https://www.customguide.com/course/onenote/onenote-page-versions)).
- **GoodNotes, Notability**: none; a standing feature request
  ([GoodNotes feedback](https://feedback.goodnotes.com/forums/191274-customer-suggestions-for-goodnotes/suggestions/46246006-version-history-function)).
- **Obsidian File recovery**: snapshots at least 5 minutes apart, kept 7 days, stored outside the vault
  ([Obsidian help](https://obsidian.md/help/plugins/file-recovery)). macOS Versions ("Browse All Versions") keeps
  them in the file system. Both lose history when the file is copied elsewhere; ours travels with the file.

## The size problem (measured)

The appended bytes are dominated by the embedded `.xopp`, written whole on every save. On the test notes of
hybrid-pdf.md that is 22 KB per save. But real handwriting is heavier: the benchmark `handwritten-text.xopp`
(13,064 strokes on one page) is 4.3 MB gzipped, about **330 bytes per stroke**. A dense lecture page of 300–800
strokes is then 100–260 KB, and a 20-page written lecture a **2–5 MB `.xopp` appended on every Ctrl+S**, plus the
changed layer's `/AP`. Fifty versions would be 100–250 MB. (Without history the 25 % rule hides this by compacting
almost every save of such a file.)

So history needs the **embedded data split by layer**: each layer's XML as its own flate stream, named by its sig
(the marker already records one per layer); the embedded document becomes a small manifest referring to them. An
unchanged layer is the same object and costs nothing; a save appends only the layers that changed (and their
`/AP`): about 50–500 KB on a dense page, a few KB on a light one. A full write (Share, Save as, export) still
writes one ordinary `document.xopp`. The marker version goes to 3, so older builds refuse instead of reading stale
data.

## 1. The model

- **A version is a revision our save wrote while history is on.** Every Ctrl+S, Save on close and "Save with a
  message" is one; autosaves never are.
- **Coalescing:** an unnamed save within 10 minutes of the previous unnamed version of the same session replaces it
  (the tail is rewritten: same cost as an append, which copies the file anyway). Habitual Ctrl+S does not flood the
  list; a 90-minute lecture gives about nine versions.
- **A milestone is a version with a message.** It is never coalesced or pruned. The message is optional, plain text,
  up to 200 characters; the date (local time shown, UTC stored) is the "commit message" when there is none.
- **Where the list lives:** in the PDF, so it travels with the file. The marker gets
  `/History << /On true /KeepUnnamed 30 >>` and `/Versions`, a flate-compressed stream with one JSON line per
  version: `{"id":17,"date":"2026-10-04T12:30:00Z","msg":"Before the exam","end":1834567,"pages":[3,4],"n":42}`
  (`end`: the byte offset after that revision's `%%EOF`; `pages`: changed pages; `n`: page count). A stream, because
  the list is rewritten on every save: compressed it costs about 2–4 KB for 200 versions. Each revision's list
  covers the versions before it; the latest is the truth. No author field (privacy; one user per file).
- **The file is checked, not trusted:** opening the panel walks the `startxref`/`/Prev` chain (our own small
  parser beside `IncrementalPdf::readTail`) and keeps only listed offsets that are real revision ends. Revisions in
  the chain that are not listed (another app appended, e.g. Acrobat added a comment) show as "Changed in another
  app" with their `/ModDate`. Listed versions that are gone show once as "N versions were removed by another app".
- **Reading a version back:** copy the prefix up to `end` into the version cache (`copy_file_range`/reflink, as the
  appender does; about 10 ms for 10 MB) and open it with `HybridPdf::open` read-only: its own `.xopp` gives exact ink.
  The background: the current clean copy when the version's page objects are a subset of the current ones
  (`pages.txt`), else a clean copy of the prefix, made in the background (seconds on a 1,300-page PDF, then cached).
  The **version cache** has an owner and a limit (the last 5 versions opened or 500 MB, removed on close).
- **What is shown:** the version's pages read-only (canvas and sidebar), changed pages marked from the sig diff
  (no rendering needed), and its message and date.

## 2. The per-document switch

- **"Keep version history"**, stored in the marker (so it travels). Off by default; turned on from the History
  panel, from "Save with a message…" (it offers it), or by a global setting "Keep version history for new PDFs with
  notes" (off). Turning it off asks whether to remove the history now (a compaction) or keep it until the next
  compaction.
- **What changes when it is on:**
  - No silent compaction: the 25 % and many-pages rules are off. The fallbacks that would write in full instead
    **append the whole new document as one update** (`Update::copy` of a full write: large but history-safe).
    Encrypted files cannot have history (the switch is disabled, with the reason).
  - **Pruning instead of compaction:** milestones are always kept; unnamed versions are thinned: all of the last
    24 hours, then one per day for 30 days, one per week after that, at most `KeepUnnamed` (setting: 10 / 30 / 100 /
    all). Pruning rewrites the chain without the dropped revisions (block 3), never touches milestones, and runs
    after a save on the worker.
  - **Size:** the panel shows "History: 12 MB of 18 MB" and "Remove unnamed versions". Once, when history exceeds
    twice the file without it, a snackbar suggests pruning.
  - **Share** offers **"Without history (recommended)"** and "With its history"; the old ink of deleted strokes lives
    in old versions. Without history it shares a compacted **copy** from the cache; the user's file keeps its
    history (today Share compacts the file in place, which must change when history is on).
  - **Save as** writes a new file without history (as today); "Save a copy with history" copies the bytes.
  - **Exports** (plain PDF, archive, For Xournal++) write fresh files as today.
- When history is off, the revisions that happen to be in the file until the next compaction can still be listed,
  dates only (the B6 idea), at almost no cost.

## 3. The UI

- **History in the page sidebar**: a clock button next to Annotations (`sidebarHistoryButton`, mode `history`).
  Newest first: "Unsaved changes" at the top when modified, then rows with the date ("Today 14:30", "4 Oct 14:30"),
  the message in bold with a flag for milestones, "2 pages changed". A chip "Milestones only". Row menu: Add/edit
  message, Compare with current, Open as copy, Restore, Export this version as PDF, Delete version (asks).
- **Preview:** tapping a row shows that version read-only in the canvas, with a banner: "Version of 4 Oct 14:30 ·
  Before the exam — **Restore** · **Open as copy** · **Compare** · **Back**" (Escape is Back). The page in view stays
  where it was. The sidebar thumbnails show that version, changed pages dotted.
- **Restore** never rewrites history: the version's pages replace the document's pages as one undoable step; the
  next save is a new version, its message prefilled "Restored the version of 4 Oct 14:30".
- **Open as copy:** a new unsaved tab with that version.
- **Compare:** the version opens in the reference view beside the current document (read-only, pages in step),
  changed pages marked. Stroke-level marks (added green, removed red, from the two layers' elements) come later.
- **Save with a message…**: **Ctrl+Alt+S** (free; Ctrl+Shift+S is Save as), and in the ⋮ menu. A one-line field,
  "What did you do?". Editing a message later appends a tiny update that only changes the marker; it is not a
  version.
- **Play the progression:** the banner has a scrubber across the versions (milestones as ticks) and ▶, one version
  a second, with "Follow changes" jumping to the first changed page. This is the "lecture building over time" at
  save granularity. B9 Level 2 (creation time per element) would later animate the strokes between two versions;
  Level 3's journal is not needed for this.

## 4. `.xopp` documents and plain PDFs

- **Recommendation: history only for PDFs with notes.** A store in the library or app cache would break VISION's
  rule that caches can always be removed (removing it would lose history), and would not travel with the file.
  Putting it into the `.xopp` breaks upstream compatibility.
- A `.xopp` document's History panel says "Version history needs a PDF with notes" with **Save as PDF with notes**
  (the existing flow and its old-`.xopp` question).
- A plain PDF becomes a PDF with notes on its first save with history. Optionally that first save **appends to
  the original PDF** instead of rewriting it, so "Version 0: as received" is in the history (decision 6).

## 5. Risks

- **File size:** per-layer data (above), coalescing and pruning keep it bounded; still the main risk. Measured
  in block 1.
- **Opening a long history:** qpdf reads every xref section of the chain; 200 small sections cost milliseconds.
  Listing reads the marker only; a version is opened on demand.
- **Other apps rewriting the file:** Acrobat "Save As", "Reduce file size", macOS Preview (PDFKit writes whole
  files), many mobile apps and "optimize" tools flatten it: the history is gone, the latest state stays. Acrobat's
  "Save" appends and keeps it (to verify in the author's round trip). Detected and said once (section 1).
- **Sync conflicts:** two devices append different revisions to the same prefix; the sync client keeps a conflicted
  copy. Later: on opening a copy that shares a prefix, offer "Add its latest state as a version". Pruning on two
  devices makes byte prefixes differ; pruning only runs on the device that saves.
- **PDF/A:** incremental updates stay PDF/A-2/3 (validated). The split data streams are ordinary streams; pruning
  rewrites through the same appender and is re-validated.
- **Signatures:** a signed revision must never be pruned or coalesced (its `/ByteRange` covers the prefix); pruning
  stops at the last signed revision.
- **Encryption:** no history (above).
- **Corrupted tails:** our writes are atomic. A file damaged by another tool: the chain walk finds the last good
  `%%EOF`, offering "Recover the last good version". History helps recovery here.
- **Privacy:** deleted ink and messages stay in the file; hence Share's default and Save as without history.

## 6. Implementation plan (block `qt/pdf-history`, one commit each)

0. **Measure** (half a day): `.xopp` size, `/AP` size and per-save delta on a realistic written lecture (a
   generator with the benchmark's stroke density, 20 pages); open time of a prefix at 50 and 1,321 pages.
   Decides whether step 2 comes first.
1. **`PdfRevisions`**: walk the chain (classic and stream xref), revision ends, dates; extract a prefix. Tests: eight
   appended saves give nine revisions, each prefix passes `qpdf --check` and opens as the document saved then;
   another app's classic-table update; garbage appended (last good revision found); a compacted file has one.
2. **Per-layer data** (marker version 3): layer streams by sig, manifest, full writes still plain. Tests: the
   round trip equals today's; one stroke on a 20-page dense document appends under 2× that layer; older marker
   versions still open; Share and Save as write version 1 files.
3. **History on in the writer**: `/History`, `/Versions`, `SaveRequest::message`, no compaction, whole-document
   append fallback, coalescing. Tests: 30 saves never compact; the list matches the chain; coalescing within 10
   minutes (injected clock); a fallback keeps every earlier version openable.
4. **Pruning** (`PdfRevisions::rebuild(file, keep)`: the objects that changed between two kept revisions,
   found by comparing qpdf's xref tables of the two prefixes, serialised as one update through
   `IncrementalPdf::Update`; atomic). Tests: every kept version's `.xopp` is byte for byte what it was, the file
   shrinks, `qpdf --check`, `failWriteAt` leaves the file as it was, a signed revision is kept, an archive PDF stays
   PDF/A (veraPDF in CI).
5. **Share and Save as**: compacted copy when history is on, the choice in Share, "Save a copy with history". Tests:
   the shared copy has one revision and no `/Versions`; the user's file keeps its history (UI test).
6. **Read-only version view**: version cache (owner, limit), read-only session, banner, Back, Restore (one undo
   step), Open as copy. Tests: restore equals the version, undo brings the document back, the next save is a new
   version; the cache limit.
7. **History panel and messages**: sidebar mode, Ctrl+Alt+S dialog, edit a message (marker-only update), milestones
   filter, size line, prune button, the switch and settings. UI tests by `objectName`.
8. **Compare and play**: reference view comparison, changed-page marks, the scrubber. Tests: changed pages after
   moves and inserts; the scrubber steps through every version.
9. **Docs and checklist**: hybrid-pdf.md section, device checklist (Acrobat Save keeps history, Preview drops
   it), measurements of step 0 again.

About 3 weeks; steps 1, 3 and 6 alone (no split, no pruning, no compare) are a usable first version in about a
week for light documents.

## Confirmed by the author (2026-10-05)

The plan above, changed as follows after the author's questions (decisions 1–13 otherwise as recommended):

- **Off by default, but easy to find (decision 1):** a History button among the sidebar's modes (Pages, Layers,
  Contents, Annotations, History) shows, while history is off, what it does and a "Keep versions of this document"
  switch; ⋮ → Document → Version history… opens the same; Settings → Documents has "Keep versions of new PDFs with
  notes" (off). Documents with history on carry a mark on their library card (and in the list view), with the number
  of versions in its tooltip/details.
- **A version per day, plus milestones (decision 2):** the first save of a day appends a new version; every further
  save that day replaces it (the file is cut back to where that day's version began and appended again: the cost of
  a normal save). "Save with a message…" (Ctrl+Alt+S) makes a milestone, never replaced or pruned; saves after it
  start a new "today". A day is the local calendar day. Another app's revision after ours is never cut away.
- **No split of the embedded data (decision 5):** every version keeps an ordinary embedded `document.xopp`, so the
  latest one is always extractable with any PDF tool (Acrobat's attachments, `pdfdetach`). Decision 13 is moot.
- **Older versions as deltas instead:** at the first save of a new day, yesterday's version (still the last revision
  of the file, and ours) is cut off and appended again with its `.xopp` stored as a byte delta (copy ranges +
  inserts, xdelta-like, on the uncompressed `.xopp`, then compressed) against the version before it; today's version
  goes on with a full `document.xopp`. Keyframes: every milestone, every 30th version, and any version whose delta
  would be more than half of the full size are stored full. Each version records the sha256 of its `.xopp`; a rebuilt
  version is checked against it. Only that last revision of ours is ever rewritten; with another app's revision after
  it, it stays full. Covers every element (the delta knows nothing of elements). Requires a deterministic `.xopp`
  writer (a test). An old version cut out of the file still shows correctly in any PDF viewer (its page drawings are
  complete); its `.xopp` needs xournal-qt or `xournal-qt-cli export-xopp <file.pdf> [--version N]`.
- **Retention (decision 4):** with deltas, no automatic pruning in the first build: all versions are kept, the
  panel shows the size history takes; thinning (the rebuild of step 4) only if real files need it.
- **Background-image attachments** (`document.xopp.bg_N.png`) are no longer written again on a save when their
  checksum is unchanged (also without history).
- **Onboarding:** the introduction and the tutorial get a short explanation of version history (after the feature
  is built).

## Decisions for the author

1. **Default:** history off, a per-document switch, a global setting for new PDFs with notes. *Recommended.*
   (Alternative: on for every PDF with notes in PDF files mode.)
2. **What is a version:** every real save, autosaves never, unnamed saves within **10 minutes** coalesced.
   *Recommended* (the interval a setting?).
3. **Milestone = a version with a message**, never pruned. *Recommended* (no separate "keep" pin).
4. **Retention of unnamed versions:** thinning (24 h all, then daily for 30 days, then weekly), capped at **30**.
   *Recommended*; or a plain "last N".
5. **Split the embedded data per layer before shipping history** (marker version 3, older builds refuse those
   files). *Recommended*, unless step 0 shows the author's lectures stay under about 500 KB of `.xopp`.
6. **Version 0 "as received"**: the first save of a plain PDF with history on appends to the original instead of
   rewriting it. *Recommended* (lectures start from blank slides).
7. **Share default "Without history"**, with a clear choice; Save as without history. *Recommended.*
8. **History only for PDFs with notes**; `.xopp` documents are offered "Save as PDF with notes". *Recommended.*
9. **Shortcut Ctrl+Alt+S** for "Save with a message…". *Recommended.*
10. **No author or device name** in versions. *Recommended* (privacy); a device name would help with sync
    conflicts later.
11. **No `xmpMM:History` mirror** for now (messages stay in our marker); maybe for archive PDFs later.
    *Recommended.*
12. **Restore as a new version on top**, never by truncating the file. *Recommended* (as the author said).
13. **Getting the notes out of a file with split data** (added 2026-10-05 on the author's question). Every export
    (Share → For Xournal++, "Keep it updated for Xournal++", Save as, archive) writes one ordinary `document.xopp`
    from the open document, as today, so nothing changes there. What changes is the user's own working file with
    history on: its layer pieces are private streams the marker points to, so a generic tool (Acrobat's attachment
    panel, `pdfdetach`) no longer finds a complete `document.xopp` in it. *Recommended:* drop the stale
    `document.xopp` attachment on the first split save (a stale one would mislead), and add
    `xournal-qt-cli export-xopp <file.pdf> [--version N] [-o out.xopp]`, which rebuilds the plain `.xopp` of the
    latest or any version. (Alternative: also append a complete `document.xopp` every N saves or at each milestone,
    for generic tools, at its size cost.)
