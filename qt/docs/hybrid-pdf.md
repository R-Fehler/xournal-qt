# Hybrid PDF: design draft

Status: **design agreed 2026-09-24**; built as `qt/hybrid-pdf` right after `qt/pdf-pages`. Background: [VISION.md](../../VISION.md) ("PDF as
the document") and [platform-research.md](platform-research.md) (qpdf can write everything needed).

## Goal

The document is **one PDF file**. Any PDF app (Acrobat, Preview, Xodo, Drawboard, a browser) shows it as the
author sees it: the pages, the ink, the text, the pasted pages. It also carries the full Xournal data, so
xournal-qt opens it again with every feature: strokes stay editable, and layers, text elements, Markdown and page
backgrounds stay as they were. Like draw.io PDFs.

## The file

A hybrid PDF is a normal PDF with four additions:

1. **Base pages.** Each page's own content is the original PDF page (copied with qpdf) or a generated background
   (plain, ruled, graph, ...). Pages pasted from other PDFs are real pages of this file, so no `.pages.pdf`
   sidecar is needed.
2. **Our drawing as annotations, not page content.** Each page gets one annotation per Xournal layer:
   - `/Ink` for strokes, with the stroke points in `/InkList`;
   - `/Stamp` for text, images and TeX.

   The exact look (pressure widths, highlighter blending) is in each annotation's appearance stream `/AP`, drawn by
   our cairo renderer as a Form XObject, the way upstream's PDF export already builds overlays. `/InkList` alone
   holds one width per stroke. Each annotation is marked as ours: an `/NM` starting with `xopp:`, plus a private
   key.

   Annotations rather than page content, because we have to show the base page *without* our drawing when we open
   the file. The drawing then comes from the embedded Xournal data. Content merged into the page could not be
   separated again.
3. **The Xournal data as an embedded file.** A `document.xopp` in `/EmbeddedFiles` (qpdf
   `QPDFEmbeddedFileDocumentHelper`). Its background is "this PDF", its pages refer to the base pages, and it
   records a hash of each of our annotations as last written.
4. **A marker** in the catalog (a private `/XournalQt` dictionary with a format version), so opening the file
   knows it is hybrid without unpacking the attachment.

## Opening a hybrid PDF in xournal-qt

1. Read the marker and the embedded `document.xopp`.
2. Make a **clean copy** of the file without our annotations, with qpdf, into the app cache, keyed by the file's
   size and time. Poppler renders that as the background, so the drawing is not shown twice. Annotations that
   other apps added (highlights, comments) are not ours, so they stay in the clean copy and are shown and kept.
   MuPDF could skip annotations while rendering and make the copy unnecessary; that is for later.
3. Compare our annotations with the hashes in the embedded data. If another app **moved, changed or deleted** one
   of ours, say so: "This PDF was edited in another app: its ink differs from the Xournal data." Offer: keep the
   Xournal data, or import the other app's version of the changed layers as plain annotations.

## Saving

- A full rewrite with qpdf, from the clean base, our annotations built from the model, and the embedded `.xopp`.
  The write is atomic (a temp file plus rename), in the background, with the document read under its lock. qpdf
  has no incremental save; measure on a 1,300-page PDF to see whether the full rewrite is fast enough. (Since
  `qt/pdf-incremental`, Ctrl+S appends an incremental update instead: see "Saving: incremental updates" below.)
- Pages removed from the document are removed from the file. Base pages are copied once and kept across saves.
- Autosave and crash saves stay `.xopp` files in the cache (as today). The hybrid file is written only on a real
  save.

## How it appears in the app

- **Save as… with the type "PDF with notes, editable (.pdf)"** (it was "Save as hybrid PDF…" first) for any document. For an annotated PDF it suggests `lecture.notes.pdf`, or
  `lecture.pdf` itself when the setting "Save notes into the PDF itself" is on; the original is then kept once as
  `lecture.original.pdf`.
- **Export plain `.xopp` for Xournal++**: once (menu), or automatically on each save (a setting per document or
  library). That writes `name.xopp` plus the clean base as `name.pdf` (or the hidden sidecar rules of
  `qt/pdf-pages`).
- **Library:** a hybrid PDF is one card. The index reads its text from the base pages plus the embedded `.xopp`
  text elements. A small badge could show "has notes".
- **Upstream Xournal++** opens a hybrid PDF as an ordinary PDF: it sees our annotations as part of the page and
  can annotate over them. Nothing breaks, but it does not get the editable strokes. That is what the `.xopp`
  export is for.

## Risks

- **Viewer support for `/AP` details:** whether `/BM /Multiply` for the highlighter is honoured, and whether
  appearance streams are shown as given. This is the round-trip test the author will do: Acrobat, Preview, Xodo,
  Drawboard, Chrome, Firefox (pdf.js).
- **Other apps saving the file:** some rewrite it and may drop the attachment or unknown dictionaries. If the
  `.xopp` is lost, the ink is still there as annotations. Importing them back as strokes is possible from
  `/InkList`, losing pressure.
- **Size:** the `/AP` streams plus the embedded `.xopp` roughly double the notes' weight. Measure it; compress the
  streams, as qpdf does by default with flate.
- **Many annotations:** one annotation per layer per page, not per stroke, keeps viewers fast. Other apps then see
  a layer as one annotation, which is fine for viewing and coarse for editing there.

## Decided (the author, 2026-09-24)

1. **Notes of an annotated PDF go to a new file** (`name.notes.pdf`) by default; the original PDF is left alone.
   A setting "Save notes into the PDF itself" (off by default) writes into the original instead, keeping
   `name.original.pdf` once. It is for people who work like in Xodo or Drawboard. Turning it on shows a one-time
   explanation.
2. **New notes stay `.xopp` by default** until the round trip works in the author's viewers.
3. **One annotation per layer per page.**

## Build plan (`qt/hybrid-pdf`, after `qt/pdf-pages`)

1. Writer: base pages plus our annotations (`/Ink` and `/Stamp` with cairo `/AP`) plus the embedded `.xopp` plus
   the marker. Tests: poppler and MuPDF render it like our own export (golden comparison), and qpdf `--check`
   passes.
2. Reader: marker, embedded `.xopp`, clean copy for the background, the hash check with its message.
3. UI: Save as hybrid PDF, export `.xopp` (once, automatic), library card and index.
4. Measurements: save time and size for a 50-page and a 1,300-page PDF with notes.

## What is built (`qt/hybrid-pdf`)

Code: `qt/src/session/HybridPdf.*` (qpdf and cairo), tests in `qt/tests/session/HybridPdfTest.cpp`.

1. **Writer** (done). `HybridPdf::write(document, target)`:
   - Base pages: the background PDF is opened with qpdf and its page tree is rebuilt in document order (pages shown
     twice are shallow copies, pages no longer shown lose their content but stay for bookmarks), so the PDF's
     outline, links, names and metadata stay. Generated backgrounds (plain, ruled, graph, images) are drawn by cairo
     into a page of their own. Pages whose PDF page is missing get a drawn page too.
   - One annotation per **visible** layer with content per page (hidden layers are only in the embedded data).
     A layer with strokes is an `/Ink` (all its strokes in `/InkList`, `/C` and `/BS /W` of its first stroke); a
     layer without strokes is a `/Stamp`. The `/AP` draws the **whole layer** in its order (strokes, text, images,
     TeX), so a mixed layer stays one annotation and looks exact; text elements also go into `/Contents`. The
     appearance is the layer drawn by cairo on a PDF page of its own and turned into a Form XObject with qpdf, as
     upstream's export does (its transparency group not isolated, so the highlighter multiplies with the page);
     its `/BBox` is the layer's box plus 2 pt, its `/Matrix` places it on the page's crop box with the page's
     `/Rotate` undone. Points and boxes are written with 0.1 pt precision.
   - Marks: `/NM (xopp:p<page>-l<layer>)` (1-based) and a private `/XournalQt << /Page /Layer >>`; `/F 4` (print).
   - The embedded `document.xopp` (`/EmbeddedFiles`, subtype `application/x-xopp`) is written by upstream's
     SaveHandler with one change: its PDF background is this file by name (`domain="absolute"`, relative
     `filename`), and PDF page *i* of the `.xopp` is base page *i*. Attached background images go along as
     `document.xopp.bg_N.png`; image files of the user are referred to by absolute path.
   - The marker: `/XournalQt << /Version 1 /Data (document.xopp) /Files [...] /Annots << /xopp:p1-l1 (hash) ... >> >>`
     in the catalog. **Deviation:** the hashes are in the marker, not in the `.xopp` (which stays exactly upstream's
     format). A hash covers what another app may change: `/Subtype`, `/Rect` and `/InkList` (to 0.1 pt), `/C`; not
     the appearance stream, which some apps write again on every save.
   - A merged-PDF mark (`/XournalQtPages`, `qt/pdf-pages`) that the background had is removed: a hybrid PDF is never
     rewritten as a merged PDF.
   - Written to a temporary file next to the target and renamed over it; object streams (smaller).
   - Tests: `qpdf --check` (through `QPDFJob`) passes; each page drawn by poppler (with annotations) matches our PDF
     export of the same document (mean difference < 0.5/255, < 0.2 % of the pixels off); the embedded document
     opens. `XQT_HYBRID_SAMPLE=<file>` makes the first test copy its hybrid PDF there (a sample for other apps).
2. **Reader** (done). `DocumentSession::loadFile` opens a PDF with the marker as its embedded document
   (`HybridPdf::open`); the marker is looked up with qpdf (about 20 ms for a 1,300-page PDF, remembered by path,
   size and time), so ordinary PDFs open as before. The clean copy (`~/.cache/xournal-qt/hybrid-pdf/<hash of the
   path>-<size>-<time>/base.pdf`, with the extracted `document.xopp` and a `changed.txt`) is made once per version
   of the file and is the document's background PDF; the file path of the document is the hybrid PDF, so the tab,
   recent files and Ctrl+S use it. Library, previews and search load it the same way.
   - Our annotations are removed from the clean copy; annotations of other apps stay in it (poppler shows them)
     and are written again on save, also on pages with a generated background (the session remembers which page of
     the clean copy each page was, so they follow a page that is moved). The clean copy is the document's PDF even
     when no page shows a PDF page.
   - The clean copy carries the merged-PDF mark `Own` of `qt/pdf-pages`, so "Save as" `.xopp` from a hybrid PDF
     puts its pages next to the `.xopp` (`name.pdf` or `.name.pages.pdf`) instead of referring into the cache.
   - The cache: a document retains the clean copy it uses (in this process); opening a file removes the clean
     copies of its other versions (older than a minute) and every entry not used for a day. Each save touches the
     entry in use.
   - The hash check: each annotation of ours whose hash differs, or that is missing, is reported
     (`LoadResult::hybridChanged`). `DocumentSession::importHybridChanges` takes the other app's version: a clean
     copy that keeps those annotations as plain ones (`/NM (imported:…)`, without our key) becomes the background,
     and the layers they stood for are emptied, undoably. A page removed in another app is not handled: the
     embedded document then refers to base pages by their old numbers.
   - Saving (`save()` of a document whose file is a `.pdf`, `saveAsHybrid`) writes the hybrid PDF from the
     document. Writing into a user's PDF that is not hybrid yet keeps `name.original.pdf` once, and the document
     takes its pages from a copy in the cache from then on (the file it read them from changes).
   - `.xopp` stays the format of "Save as" and of every document that was not saved as a hybrid PDF; autosaves and
     crash saves stay `.xopp` files.
   - Tests: a written file opens as the same document as a `.xopp` round trip gives (pages, sizes, backgrounds and
     the PDF text on each page, layers, strokes with pressure, colours and tools, texts); saved again after a
     change; a plain PDF and "Save as" `.xopp` behave as before; a comment added with qpdf stays in the clean copy
     and survives a save; a moved and a deleted annotation of ours are reported, and importing them empties those
     layers (undo brings them back); notes saved into the PDF itself keep `name.original.pdf` byte for byte; the
     `.xopp` export opens as the same document with a base PDF without annotations.
3. **UI** (done; the flow around it changed in `qt/hybrid-flow`, below).
   - More → **Save as hybrid PDF…** for any document (now a type in Save as…). The suggestion: the document's own hybrid PDF; for an
     annotated PDF `name.notes.pdf` next to it, or the PDF itself with the setting on; for other documents the
     `.xopp` suggestion as `.pdf` (`name.notes.pdf` if a PDF of that name exists). Once saved as hybrid, the title
     is the PDF and Ctrl+S writes it again.
   - Settings → Documents → Hybrid PDF: **Save notes into the PDF itself** (off; turning it on explains it once:
     Ctrl+S on an annotated PDF then writes into it without asking, keeping `name.original.pdf` the first time) and
     **On every save of a hybrid PDF, also write a .xopp for Xournal++** (off).
   - More → **Export as .xopp for Xournal++…** (shown for a hybrid PDF): `name.xopp` next to it, its PDF
     `.name.pages.pdf` (the hybrid PDF has the name `name.pdf`); written again by the next export. Deviation: the
     automatic export is one global setting, not per document or library.
   - Export as PDF of a hybrid PDF suggests `name_export.pdf`, never the file itself.
   - Opening a hybrid PDF whose ink another app changed asks: **Keep the Xournal data** (the next save writes it
     again) or **Import the other app's changes** (plain annotations; the layers emptied, undoable).
   - Library: a hybrid PDF is one card; its search text is the text of its pages plus the text elements of the
     embedded document, and it is read again when the file changes (the index keys it by the hybrid PDF itself,
     not by the clean copy). A hybrid PDF with its exported `.xopp` is one card that opens the hybrid PDF (only
     checked when `.name.pages.pdf` exists, so listing folders stays cheap). No "has notes" badge yet.
4. **Measurements** (2026-09-24, the 2-in-1 while other builds ran, load 2–5; `XQT_BENCH_HYBRID=<pdf>` runs
   `HybridPdfTest.benchSaveAndOpen`, `XQT_HYBRID_TIMES=1` prints the steps). Notes on every 25th page: 20 pressure
   strokes, a highlighter and a text.

   | | 50 pages (first 50 of pgfmanual, 982 KB), 2 noted | 1,321 pages (pgfmanual, 9.9 MB), 53 noted |
   | --- | --- | --- |
   | our PDF export (upstream's qpdf overlay) | 1.1 s, 1,002 KB | 6.5 s, 10,182 KB |
   | hybrid PDF | 1.1 s, 967 KB | 5.6 s, 10,321 KB |
   | open the hybrid PDF, clean copy made | 1.1 s | 5.5 s |
   | open it again (clean copy cached) | 0.11 s | 0.5 s |
   | open the plain PDF, for comparison | 0.11 s | 0.7 s |
   | save again from the clean copy | 1.0 s | 7.1 s |

   Steps of a 1,321-page save: drawing and the `.xopp` 0.45 s, the base pages (qpdf resolves every page) 1.3 s,
   the 53 annotations 0.55 s, writing 3–3.8 s. Writing leaves the PDF's streams as they are (`qpdf_dl_none`;
   decoding and compressing them again doubled the time), and the PDF is only stripped of our annotations when it
   has our marker. After a save in the app the clean copy of the new version is made in the background, so the
   next open of that file does not wait for it.

   The save used to run on the UI thread and blocked the window for about 6 s at 1,300 pages (1 s at 50 pages).
   Since `qt/background-save` it runs in the background (DocumentSave.cpp): the document's pages are copied on the UI
   thread (a few milliseconds), the drawing, the `.xopp` and qpdf work on that copy on a worker, and the undo stack's
   saved point is the copied state. The whole save still takes as long; the window stays usable. qpdf has no
   incremental save; appending an incremental update ourselves makes saves of long PDFs cheap (done in
   `qt/pdf-incremental`, below).

## The flow around it (`qt/hybrid-flow`)

1. **Save as with a type.** One Save as dialog (⋮ → Save as…, Ctrl+Shift+S, and Ctrl+S of a document without a
   file) with two file types: "Xournal notes (.xopp)" and "PDF with notes, editable (.pdf)". New documents start on
   `.xopp` (decided above), a document that is a hybrid PDF already starts on the PDF with its own name. The name
   follows the type (`AppController::fileForFormat`: the suggestion of one type becomes the other's; else the
   extension is swapped, and a `.pdf` taken by another PDF becomes `name.notes.pdf`). The extension typed wins over
   the chosen type (`savesAsPdf`): `x.pdf` is a PDF with notes, `x.xopp` a `.xopp`; no extension: the type. The
   separate "Save as hybrid PDF…" entry is gone. "Export as PDF…" is now "Export as plain PDF…" (the notes drawn
   into the pages, nothing editable). The `.xopp` suggestion of a hybrid PDF is `name.xopp` (not upstream's
   `name.pdf.xopp`).
2. **The old `.xopp`.** Saving a document that was saved as `name.xopp` as a PDF with notes asks once, before
   anything is written, what happens to `name.xopp`:
   - **Move it to the trash** (the default; the PDF now holds everything). After the PDF is written, the `.xopp`
     goes to the desktop trash with the files that belong to it alone (the library's trash:
     `DocumentFiles::trash` with its attached PDF, `.name.pages.pdf` and background images, plus a `name.pdf` of
     only pasted pages made for it). The PDF it annotates stays. When the document shows its pages from one of
     those files, it takes them from a copy in the app cache first (`DocumentSession::detachBackground`, a hard
     link where possible: the same pages under the same numbers).
   - **Keep it updated for Xournal++**: the `.xopp` is written again from the PDF's notes now (with its PDF by the
     export's rules: `name.pdf` if free, else the hidden `.name.pages.pdf`) and on every save of this document.
     This is per document: the hybrid PDF records it in its marker (`/XoppExport`, relative to the PDF when it is
     beside it or below; `HybridPdf::xoppExportOf`, `DocumentSession::xoppExport`), so it survives closing and
     reopening. Deleting that `.xopp` ends it (a save no longer writes it or records it). The global setting "On
     every save of a hybrid PDF, also write a .xopp" stays as it was.
   - **Keep it as it is**: not touched, not updated. Kept beside the hybrid PDF of its name (`notes.xopp` next to
     the new `notes.pdf`), the library shows one card that opens the hybrid PDF, as for its export: every same-name
     pair of a `.xopp` and a PDF looks into the PDF (`HybridPdf::isHybrid`, remembered per file version; lone PDFs
     are not looked into, and the library index does not record it). A `.xopp` changed more than a minute after the
     PDF (edited in Xournal++ afterwards; an export is written right after the PDF) is not hidden: the two are listed
     as two documents, each opening its own file.

   The dialog has **"Don't ask again"**, which stores the choice in the setting `hybridOldXopp` (`ask`, `trash`,
   `update`, `keep`); Settings → Documents → Hybrid PDF shows it and sets it back to "Ask each time". Cancel
   writes nothing. Only `.xopp` files are asked about (`.xoj` ones are left alone).
   If the `.xopp` is open in another tab of this process: without unsaved changes that tab is closed; with changes
   the `.xopp` is kept as it is and a message says why. (Another process, e.g. a library's window, is not seen.)
   All trashing of the app now goes through `SystemApps::moveToTrash`, so tests never fill the user's trash.
3. **Share…** (⋮ → Share…, the tab menu, and the menu of a PDF or notes card in the library or Recent; a card of
   notes, also a PDF with its `.xopp`, is opened first and shared as the open document) offers three things:
   - **PDF with notes (opens in any app)**: the hybrid PDF itself, saved first when it has changes; a PDF without
     notes as it is; notes that go into the PDF itself are saved first. On the desktop the file manager then shows
     it, selected (`SystemApps::share`: `ShowItems` over D-Bus, `explorer /select,`, `open -R`); Android and iOS
     will open the share sheet (false there for now). A document without a file opens Save as on the PDF type and
     shares after the save. A `.xopp` is never turned into a PDF unasked: the window offers **Save as PDF with
     notes…** (the document becomes the PDF; the old-.xopp question applies) or **Save a PDF copy…** (a hybrid PDF
     written from the document, `SaveKind::ExportHybrid`, never over the PDF it shows; the document keeps its file,
     format and unsaved changes).
   - **Copy the PDF with notes** (the author, 2026-09-24): the same PDF onto the clipboard, to paste it into another
     app or a chat (`SystemApps::copyToClipboard`): its URL as `text/uri-list` (Dolphin, browsers, Telegram and
     most chat apps take a pasted file that way) and as GNOME's `x-special/gnome-copied-files`, the PDF's bytes as
     `application/pdf` up to 50 MB, and the path as text. "PDF copied: paste it into another app". A `.xopp` is
     not asked about here: a PDF copy is written into the app cache (`~/.cache/xournal-qt/share/name.pdf`) and
     copied; the document stays as it is.
   - **For Xournal++ (.xopp + PDF)**: a one-time export into a folder the user chooses, never the document's own
     folder (there the library would take it for the document), as `name.xopp` + `name.xopp.bg.pdf`: upstream's
     attached PDF (`<background type="pdf" domain="attach" filename="bg.pdf">`, which upstream's `LoadHandler`
     resolves as the `.xopp`'s path + `.bg.pdf`), so the pair opens with its pages right wherever it is moved
     together (tested with the LoadHandler, also after moving both). Its PDF is the base pages in document order,
     page *i* of the `.xopp` showing page *i*; no PDF is written when no page shows a PDF page. A name taken there
     becomes "name (2)". Then the file manager shows the files, and the note offers **Copy** (both files as a
     URI list). From a library card, the PDF is loaded and exported on a worker, without opening a tab.
   "Export as .xopp for Xournal++…" (next to the hybrid PDF) left the ⋮ menu: the one-time export is Share's, and
   the `.xopp` kept beside the PDF is "Keep it updated for Xournal++" (or the global setting).

## Archive PDF (`qt/archive-export`)

The author asked for an export meant for keeping (TODO.md, "Archive export"): a **PDF/A-3b** file that stays readable
for decades in any PDF viewer, with the ink merged into the pages so no viewer can hide or lose it, and the full
Xournal data embedded so xournal-qt still opens it for editing.

### The file (`HybridPdf::writeArchive`, `qt/src/session/ArchivePdf.*`; tests `ArchivePdfTest` in `HybridPdfTest.cpp`)

It is a hybrid PDF with three differences:

1. **The ink is page content, not annotations.** Each visible layer is drawn by cairo exactly as for a hybrid PDF's
   `/AP`, and placed as a Form XObject (`/XqtInkN` in the page's resources, the same `/Matrix`: crop box, rotation
   undone; the transparency group not isolated, so the highlighter multiplies) by a content stream appended **after**
   the page's own streams, which stay untouched: `/Contents [ (q) <the page's own streams> (Q q /XqtInk1 Do Q …) ]`,
   like upstream's PDF export (QPdfExport). Every viewer draws it as part of the page.
   - **Reopening stays fully editable.** Both added streams carry our private key `/XournalQt` in their stream
     dictionary; the second lists the XObjects it adds (`/XObjects [/XqtInk1 …]`) and the layers it draws
     (`/Layers [(xopp:p1-l1) …]`). The reader's clean copy (the background) removes exactly those two streams and
     those XObjects (`unflatten`), so the background is the original page, pixel for pixel, and the strokes come from
     the embedded `.xopp` only: erasing one and saving removes it from the page and from the data. The page's
     resources get their own copy when written (a shared dictionary is never changed for other pages).
   - Content another app appended **after** ours stays in the background; it keeps a plain `q`/`Q` around the page's
     own content (in place of our marked ones), so it is drawn where it was. The marker lists the layers merged in
     (`/Flattened`); one whose stream is gone (another app rewrote the page's content as one stream) is reported like
     a changed annotation ("edited in another app"; importing keeps the other app's page as it is).
2. **Links stay `/Link` annotations** (`/URI`, `/GoToR`, as in a hybrid PDF), with the print flag (PDF/A wants it on
   every annotation). They are the only annotations of ours.
3. **PDF/A-3b** (`ArchivePdf::conform`, run on the assembled file):
   - The `document.xopp` (and attached background images) are **associated files** of the document: the catalog's
     `/AF` array, `/AFRelationship /Source` (images `/Supplement`), MIME type `application/x-xopp` (it is gzipped
     XML, so not `+xml`), `/Params /ModDate`, `/F` and `/UF`. Embedded files of the source PDF become associated files
     too (`/Unspecified`, a MIME type if they had none).
   - An **sRGB output intent** (`/GTS_PDFA1`) with its ICC profile embedded: Graeme W. Gill's sRGB profile from
     ArgyllCMS, version 2.2, 3,268 bytes, public domain (MIT in TeX Live's copy; see `qt/resources/icc/README.md`),
     compiled into the app (`XqtSession.cmake` writes it as a byte array). A version 2 profile is accepted by every
     PDF/A part and validator.
   - **XMP metadata** (unfiltered: qpdf leaves metadata streams uncompressed) with the same title, author, subject,
     keywords, creator (`xournal-qt <version>`), producer and dates as the rebuilt document information dictionary
     (dates in UTC, `D:…+00'00'` and `…+00:00`), and `pdfaid:part 3`, `pdfaid:conformance B` **only when every check
     passed**. The title is the source PDF's, else the file name without `.archive.pdf`.
   - Written with a document `/ID`, never encrypted, at least PDF 1.7, an end of line before every `endstream`
     (veraPDF's rule 6.1.7.1-2), object streams (allowed from PDF/A-2 on). Streams with an LZW filter are decoded and
     compressed again.
   - The marker says `/Version 2 /Archive true` (older builds refuse it instead of showing the ink twice); hybrid PDFs
     stay version 1.
   - **A hybrid PDF never claims PDF/A.** Its annotations and attachment are not PDF/A, so when its source PDF was
     PDF/A the writer removes the PDF/A identification (`pdfaid:part`, `conformance`, `amd`, `rev`, as elements or
     attributes) from the XMP metadata it keeps; the rest of the metadata and an output intent (which alone claims
     nothing) stay. The same for the base pages exported for Xournal++. Silent: the UI says nothing about it.
   - **Saving an archive PDF again in the app** (Ctrl+S after opening it) writes an archive PDF again (the file's
     marker decides); the clean copy has no output intent, `/AF` or metadata of ours left, so a hybrid PDF written
     from it never claims PDF/A.

### What is checked, what is repaired

The source PDF's pages are walked (content streams, resources, Form XObjects, patterns, Type 3 glyphs, annotation
appearances). The file is always written; when a check fails, it has no PDF/A identification and the report lists
why ("not PDF/A: the source PDF has fonts that are not embedded: Helvetica"). Compliance is never claimed when a
check failed.

- **Not PDF/A (reported):** fonts without `/FontFile*` (also the standard 14), DeviceCMYK colours (operators `k`/`K`,
  colour spaces, images, shadings, inline images, group spaces) without `/DefaultCMYK` (the output intent is RGB),
  annotations other than links and pop-ups without an appearance, form buttons whose appearance has no states,
  sound/movie/screen/3D/rich media/file attachment annotations, PostScript XObjects, reference XObjects, streams
  stored in other files, inline images with LZW or smoothing.
- **Repaired (the pages look the same):** JavaScript (`/Names /JavaScript`, an `/OpenAction` script), `/AA` of the
  catalog, pages, annotations and form fields, actions PDF/A forbids (launch, sound, movie, hide, named actions other
  than the four page moves, …) and every action of a form field: removed ("JavaScript and actions PDF/A does not
  allow were removed"). Hidden annotations: removed. Annotations: the print flag set, text notes no zoom/no
  rotate, appearance states other than the normal one removed. Images: `/Interpolate false`, no `/Alternates`,
  `/OPI`. Graphics states: no transfer functions or halftones. Fonts: `/CharSet` and `/CIDSet` removed (optional,
  and often wrong in subsets). Forms: no `/NeedAppearances`, no XFA. Optional content configurations get a name
  and lose `/AS`. Page-level output intents, `/Perms` and `/Requirements` are removed.
- **Not checked** (veraPDF would find them): `.notdef` glyphs referenced by text, the insides of font programs
  (widths, cmaps), ICC profiles inside the source PDF, implementation limits, and the rarer rules. In a sample of
  19 PDFs from the system's documentation, every file our check called PDF/A-3b passed veraPDF, and every file it
  did not failed veraPDF for the reasons listed (the files that also used `.notdef` glyphs used CMYK too).

### Export for the archive (per document)

- **⋮ → Export for the archive…** (not for Markdown and text files) and **Share… → For the archive (PDF/A)** (the tab
  menu and a library card's Share too) open a dialog that says what it means, in the author's words: "A PDF made for
  keeping (PDF/A-3). It stays readable for decades in any PDF viewer. Your ink is merged into the pages, so no
  viewer can hide or lose it. The full Xournal data is embedded, so this app can still open it for editing."
- Where it goes: **next to the document as `name.archive.pdf`** (the default; `lecture.notes.pdf` gives
  `lecture.archive.pdf`; an archive PDF itself gets `name.archive (2).pdf`, never its own file), or **in a folder I
  choose…** (a folder dialog; the same name there). A document without a file yet only has the folder. An existing
  file of that name is replaced (it is the archive of the same document).
- It runs in the background ("Writing the archive PDF…"): the current document through the save machinery
  (`SaveKind::ExportArchive`, `DocumentSession::exportArchive`): its state now, unsaved changes included; the document
  keeps its file, its changes and its saved point. A library card's document is loaded and written on a worker
  without a tab (`AppController::exportArchive(target, file)`).
- Afterwards a report: "name.archive.pdf is a PDF/A-3b file …", or "… was written … It is not PDF/A, because:" with
  the reasons, plus a grey line with what was changed to conform, and **Show in folder** (the file manager, where
  sharing works).
- Tests: `MainWindowTest.exportForTheArchive` (the dialog's text and default, the report, the file with the unsaved
  stroke, the document still modified, Share's entry, a folder, a card's document).

### Export library as archive (`qt/src/shell/LibraryArchive.*`; tests `LibraryArchiveTest`, `HomeScreenTest.exportLibraryAsArchive`)

- **Library menu → Export library as archive…**: a dialog explains it (every document an archive PDF, other files
  copied as they are, a README, a new folder, the library unchanged) and offers **the whole library** or **only this
  folder** (with its subfolders; when a folder is open). Then a folder dialog.
- Everything goes into a **new folder "<name> archive <YYYY-MM-DD>"** (" (2)" when it exists) inside the chosen one,
  so nothing there is overwritten; `<name>` is the library's (or the folder's). A chosen folder inside the library is
  refused ("The archive never goes into the library itself"); nothing is ever written into the library.
- The folder structure is kept. The documents are the library's cards (`DocumentFiles::scanRecursive` with all files):
  a `.xopp` alone, a `.xopp` with its PDF, a hybrid PDF (with its exported `.xopp`), a plain PDF, and a `.xopp` over
  an image. Each is loaded like a card (`DocumentSession::loadFile`) and written as `<name>.pdf` (not `.archive.pdf`
  there: the whole folder is the archive; " (2)" when two documents of a folder share a name). The image a `.xopp`
  annotates is copied too. Markdown, images, text and all other files are copied as they are, with their times.
  Hidden files and folders (the library's `.xournal_library`) are not.
- **Links** between documents lead to their archive PDFs: `HybridPdf::LinkMap` reads each link from the document's own
  folder, and a linked document of the export (its `.xopp` or its PDF) becomes a `/GoToR` to its archive PDF, at the
  document's page (`page=`; archive pages are document pages).
- A **README.txt** in the new folder: what the files are (PDF/A-3, the ink in the pages), how to edit again (open the
  PDF in xournal-qt; `document.xopp` is an attachment), that other files are copied, the counts, the files that are
  not PDF/A with the reasons, the ones that failed, and "incomplete" when it was cancelled.
- It runs on a worker thread of the global pool at low priority, one file after the other, with a progress dialog
  ("3 of 12 · Lectures/lecture.xopp") and **Cancel** (looked at between files; what was written stays, with the
  README). At the end a summary: "N archived (N of them PDF/A-3b), N copied, N not PDF/A, N failed", the reasons,
  and **Show in file manager**. A document whose PDF is missing, or that cannot be read, is listed as failed and the
  rest goes on.

### Validation

- The tests: `qpdf --check` passes; poppler draws each page like our PDF export (mean difference < 0.5/255, < 0.2 %
  of the pixels off); the embedded `.xopp` opens as the same document; the background of the reopened file is the
  original PDF pixel for pixel; erasing a stroke and saving removes it from the page and the data; another app's
  appended content stays; the associated file, output intent (the profile's bytes), XMP (identification, title and
  dates matching the document information) are there; a source PDF with Helvetica not embedded and CMYK colours is
  written without the identification and both reasons are reported; JavaScript and a smoothed image are repaired.
- **veraPDF** (the reference validator, Java; a test tool only, never run by the app): CI (Linux) downloads its
  greenfield CLI from Maven Central (`org.verapdf.apps:greenfield-apps`, checked by SHA-1), runs `ArchivePdfTest`
  with `XQT_ARCHIVE_SAMPLES=<folder>` (the tests copy their archive PDFs that claim PDF/A there) and fails when one
  of them is not PDF/A-3b. Locally: `java -cp greenfield-apps-1.28.2.jar org.verapdf.apps.GreenfieldCliWrapper
  --flavour 3b --format text <files>`; `XQT_ARCHIVE_SOURCE=<pdf>` makes `ArchivePdfTest.archiveOfAGivenPdf` write
  an archive of any PDF (with a stroke on page 1) and print its report, to compare with veraPDF.

## Saving: incremental updates (`qt/pdf-incremental`)

Ctrl+S on a hybrid or archive PDF appends only what changed, as a standard incremental update (ISO 32000-1, 7.5.6),
the way Acrobat and Drawboard save: the file's bytes stay as they are, and a new revision follows them. On
pgfmanual (1,321 pages, notes on 53) a save after one stroke takes about 0.2 s instead of 7.5 s and appends about
22 KB (measurements below).

### The appender (`qt/src/session/IncrementalPdf.*`)

qpdf reads and changes the PDF but can only write whole files, so the update is written by our own small appender,
with every object serialised through qpdf (`unparseResolved`, a stream's dictionary and its raw data):

- The file is opened with qpdf and an `Update` is started. Each object of the file is `touch()`ed before it is
  changed (its text is remembered); at the end the touched objects whose text differs are written again under their
  numbers, with the new objects they reach. Nothing else of the file is read or written.
- New objects are made by the `Update` (`add`, `addStream`, `copy` of another PDF's objects, `copyStream`), numbered
  after the file's highest object. Not with qpdf's `makeIndirectObject`, `newStream` or `copyForeignObject`: the
  first new object makes qpdf read every object of the file to find a free number (pgfmanual: 1.1 s with qpdf 12.4,
  6.4 s with 10.6; the whole appended save takes about 0.2 s). A number is taken with a null object that is replaced
  later (qpdf 12 gives a handle of a number the file does not have that is bound to nothing). A new stream is a
  dictionary in the qpdf document (so other objects can refer to it) whose data the `Update` keeps.
- qpdf 12 or newer is needed; the desktop build compiles a pinned release ([releasing.md](releasing.md), "Which
  package for which system").
- The cross-reference section matches the file's style: a cross-reference stream after one (PDF 1.5; our full
  writes use them), with the new dictionaries in an object stream, else a classic table. The trailer has `/Size`,
  `/Root`, `/Info`, `/ID` (the file's first identifier, a new second one) and `/Prev` (the last `startxref`, the
  one thing read from the file's bytes directly). New streams without a filter are compressed (not XMP metadata);
  every `endstream` has an end of line before it (PDF/A).
- **Crash safety.** The update is written to a copy of the file (`.name.pdf.<pid>-<n>.part` next to it; copying uses
  `copy_file_range` or a reflink where the file system has one: about 10 ms for 10 MB here), flushed to the disk
  (`fsync`) and renamed over the file. A crash, a full disk or any failure at any point leaves the previous revision
  exactly as it was, and readers of the file meanwhile (the library's index and previews, other apps, a sync
  client) never see a half-written update: appending in place would leave a tail without `startxref` after a crash,
  which readers repair in different ways, and could be read half-written. The price is the copy; the new bytes on
  the disk are the update's. Temporary files a crash left behind are removed by the next save of that file (after
  ten minutes). The in-memory state of the session (the revision, below) changes only after the rename succeeded.
  Tested with an injected failure before the first byte and after the last one: the file is byte for byte what it
  was, no temporary file is left, and the next save succeeds.

### What changes, and how it is found without reading the pages

A long PDF written with object streams has its page dictionaries spread over the whole file, so reading every page
means reading most of the file (seconds). An incremental save reads only what it changes:

- **Which base page is which.** A `Revision` (in `HybridPdf.h`) maps the page numbers of the document's background
  PDF to the page objects of the file. After opening, it comes from the clean copy's cache entry: `pages.txt` lists
  the file's page objects in order (the clean copy's page *k* is the file's page *k*), valid while the file is that
  version (size and time) and was not edited in another app (`changed.txt` empty). After a save it comes from the
  save itself: the session keeps it (`DocumentSession::hybridRevision`) while the numbering of the background PDF
  stays. Pages pasted from other PDFs (the merged PDF grows, the numbers stay) are copied from the background PDF.
- **What each drawing shows.** Each layer and each generated background has a *sig*: a hash of its XML as the
  `.xopp` holds it (the layer's elements, or the background with an image's pixels), the page size and the app's
  version. `prepare()` computes the `.xopp` first and draws only what the file does not have yet. The sig is in
  our annotation's private dictionary (`/XournalQt /Sig`), in an archive page's ink stream (`/Sigs`), and in a
  drawn base page (`/XournalQt /Bg`); the marker lists the drawn pages (`/Drawn`).
- **The record.** The marker records each layer's sig, its drawing (Form XObject) and its annotation (`/Layers <<
  /xopp:p3-l1 [(sig) form annot] >>`). A page in its place whose layers and links are as recorded is not read at all.
  Only changed pages are: their annotations of other apps stay in place, ours are kept when unchanged (written again
  when only their name or page moved), a drawing the file has with the same sig is reused (a copy when it is placed
  differently), and only new drawings are added.
- **Per save** the update holds: the changed pages (their `/Annots` or, in an archive PDF, the `/Contents` and
  `/Resources`), the new or rewritten annotations and drawings, the page tree's root when pages were added,
  removed or moved, new base pages, the embedded `document.xopp` (a new stream in its file specification; for
  archive files with `/Params /ModDate`), the marker (hashes, record, `/Base` and `/Updates`), and `/Info` (and the XMP
  metadata of an archive PDF). A removed page is only taken out of the page tree (its objects stay, so undo can
  bring it back cheaply; the next full write drops it).
- Files of earlier versions (no record, no sigs) are appended to as well: the first save reads the pages with our
  notes and redraws all layers; it writes the record, so the next saves are fast.

### When the whole file is written anew (compaction)

- **Save as**, and a save that **shares** the file (Share → PDF with notes, Copy the PDF with notes: older
  revisions can still hold ink that was deleted, a privacy matter): `SaveRequest::compact`, and a hybrid PDF that holds
  earlier revisions counts as needing a save before it is shared (`AppController::shareStep`). A hybrid PDF card of
  the library shared as it is is first written anew in one piece by qpdf, the same content (`HybridPdf::compact`,
  on a worker). **Export** (plain PDF, archive, For Xournal++, a PDF copy) and the library's archive export always
  write fresh files from the document, as before.
- When the file would then have grown by more than **25%** since it was last written in full (`/Base` in the
  marker; `HybridPdf::compactAbove`). For small files that is soon: they are cheap to write.
- When many pages changed at once: more than a quarter of the pages are new base pages (pasted, or a new
  background drawn), or more than a quarter of the file's pages were removed (their dead weight would stay).
- When there is nothing to build on: the file changed since it was written or opened (another app saved it), it was
  edited in another app (its hash check found a change), the other app's version was imported, the file is
  encrypted, its page tree is not flat or passes attributes on, the embedded images changed, or anything unexpected
  (an exception): the save falls back to the full write, which is always correct. `XQT_HYBRID_TIMES=1` prints why.

### The clean copy and "edited in another app"

- **The clean copy is kept** across incremental saves: when the pages are the same pages in the same order, the
  cache entry of the new version gets the clean copy of the previous one as a hard link (a copy where links fail),
  with the new `document.xopp`, so opening the file again takes 0.4 s instead of 5 s. When pages were added, removed or
  moved, the next open makes a clean copy of that version (in the background right after the save, as before).
- **"Edited in another app"** works on incremental files as before: qpdf reads the latest revision, and the hashes
  in the latest marker describe our annotations as they are now. When another app appends its own update that moves
  or deletes one of ours, the check reports it; the next save writes the file anew. An update of another app that
  only adds its own annotations is kept, and the next save appends on top of it.

### PDF/A

PDF/A-2 and -3 allow incremental updates. An archive PDF saved again stays PDF/A-3b: the update has no encryption,
a cross-reference stream and object streams (allowed from PDF/A-2 on), `/ID` in its trailer, an end of line before
every `endstream`, and nothing after `%%EOF` but one end of line. The document information's `/ModDate` and the XMP
metadata's `xmp:ModifyDate` and `MetadataDate` get the same new date (the XMP stream is written again, uncompressed,
with its creation date and PDF/A identification kept; `ArchivePdf::update`). A new drawing is checked and repaired
like the source PDF was (`ArchivePdf::check`, e.g. an image's `/Interpolate`) before it is copied into the file; if it
cannot conform, the file is written anew instead (as every save of an archive PDF was before), which claims PDF/A
only when everything conforms. CI's veraPDF step also validates an archive
PDF saved incrementally three times (`IncrementalSaveTest.anArchivePdfStaysPdfAAfterIncrementalSaves`).

### Tests and measurements

Tests: `IncrementalPdfTest` (the appender: both cross-reference styles, the previous revision readable, poppler and
`qpdf --check`, new and copied objects, a failed write) and `IncrementalSaveTest` in `HybridPdfTest.cpp` (only what
changed is appended; eight saves in a row with every kind of change — strokes, an erased stroke, pages moved,
added, deleted, a background changed, a text, a page shown twice — each checked with `qpdf --check`, drawn by
poppler like a full write of the same document, and opened as the same document; pages pasted from another PDF;
the compaction rules; exports and shared files without earlier revisions; the clean copy kept; another app's
appended revision; a file of an earlier version; an archive PDF staying PDF/A). UI: sharing compacts
(`sharingThePdfWithNotes`, `shareFromALibraryCard`).

Measurements (2026-09-24, the 2-in-1, files on its SSD, load 1–5 from other builds; `XQT_BENCH_HYBRID=<pdf>` runs
`IncrementalSaveTest.benchCtrlS`, `XQT_BENCH_SAVE=<pdf with notes>` one save, `XQT_HYBRID_TIMES=1` prints the steps).
pgfmanual, 1,321 pages, 10.1 MB, notes on every 25th page (20 pressure strokes, a highlighter, a text); Ctrl+S after
one stroke on another page, as `DocumentSession::save()` waits for it:

| | hybrid PDF (10.3 MB) | archive PDF (10.2 MB) |
| --- | --- | --- |
| Ctrl+S written in full (as before) | 6.3 s | 24.6 s (the PDF/A check walks every page) |
| Ctrl+S appended | 0.20–0.24 s, 22–23 KB | 0.18–0.36 s, 25–26 KB |
| opened again after appending (clean copy kept) | 0.44 s (5.5 s when it is made) | 0.72 s (5.7 s) |

An appended save: opening the file for the update 0.08 s, the `.xopp` (the whole document, gzipped) and the new
drawing 0.1–0.15 s, the changed page 0.01 s, the copy, update and `fsync` 0.01–0.04 s, the clean copy's cache entry
0.02–0.06 s. The appended bytes are mostly the embedded `.xopp` (about 20 KB here), which is always written whole.
The first save of a file written by an earlier version reads the pages with notes once (0.7 s here). At 25% growth
the file is written in full again: here after about 110 such saves.

Validation of the files after several incremental saves (`XQT_INCREMENTAL_SAMPLES=<folder>` writes them, with a full
write of the same document): `qpdf --check` passes after every save; poppler (in the tests), pdfium (Chrome's
renderer, through pypdfium2, with annotations) and Ghostscript 9.55 draw every page of the lecture after eight saves,
and pages 1, 2, 11, 26, 41, 51, 71, 76, 101, 126, 131, 501, 1001 and 1321 of pgfmanual (hybrid and archive),
identically to the full write (no pixel differs). veraPDF 1.28.2 passes the archive PDF saved incrementally three
times (PDF/A-3b); pgfmanual's archive PDF is not PDF/A with or without increments (its source is not, and the file
does not claim it). MuPDF (`mutool` is not installed; the `qt/mupdf` branch was not merged for this) and pdf.js (not
installed; it needs a download) were not tried.

## PDF-only mode (`qt/pdf-only`)

The author decided (2026-09-24, TODO.md "a PDF-only mode") that people can work with PDFs only, the Drawboard way:
every document is a single PDF, with no sidecars.

### The setting and the first-start question

- `documentMode` in the `xournalQt` part of `settings.xml` (`session/DocumentMode.h`): `pdf` ("PDF files") or `xopp`
  ("Xournal++ files", the behaviour before). While it is not stored the app works as Xournal++ files.
- The first start of the main window where it is not stored asks once (`DocumentModeDialog.qml`): "How do you want to
  keep your documents?", two cards (`DocumentModeCards.qml`):
  - **PDF files** (like Drawboard PDF, GoodNotes, Xodo): every document is one PDF that any app opens; notes on a
    PDF are saved into that PDF, nothing else is written next to the files. *Recommended for most people*; chosen
    to start with.
  - **Xournal++ files** (like Xournal++): `.xopp` notes next to their PDFs, fully compatible with Xournal++.
    *Recommended if you also use Xournal++*.

  A line says it can be changed in Settings → Documents and that copies for Xournal++ stay available through
  Share → "For Xournal++". Only **Continue** closes it (Escape and a tap outside do not); the choice is stored then.
  Existing installs are asked too. After a crash the recovery question follows it.
- Settings → Documents shows the same cards at the top ("Keep documents as"); a tap changes the mode at once. In PDF
  files mode the switch "Save notes into the PDF itself" is hidden: it is always so.
- Tests and scripts: `XQT_DOCUMENT_MODE=xopp|pdf` stands in for a choice that is not stored and keeps the question
  away (a stored choice wins). The UI tests set `xopp` in `tests/ui/main.cpp`; `FirstStartTest` unsets it to test the
  question, and the tests of PDF files mode store the mode and set it back.

### What PDF files mode does

- **New documents** are PDFs with notes: the library's New document saves `name.pdf` at once
  (`AppController::createDocument`); a new tab's Save as starts on "PDF with notes" with `name.pdf`
  (`AppController::saveFormat`, also used by the window's Save as for every document: a hybrid PDF stays a PDF, a
  `.xopp` a `.xopp`, everything without a file of its own takes the mode's type).
- **Annotating a PDF writes into that PDF.** Ctrl+S (and closing with Save) saves without a dialog, as with "Save notes
  into the PDF itself": there is no "where to save the .xopp" step. The first save turns the plain PDF into a PDF with
  notes (written in full, atomically: a temporary file renamed over it); from then on Ctrl+S appends incremental
  updates (above). Save as suggests the PDF itself.
  - *The original pages are never rewritten*: their content streams are copied as they are (`qpdf_dl_none`), our ink
    is annotations on top (`PdfOnlyMode.annotatingAPdfSavesIntoItThenAppends` compares the raw content streams of
    every page before and after both saves).
  - *No `name.original.pdf`* next to it (nothing is written next to the files). Instead the original is kept once in
    the app cache, `~/.cache/xournal-qt/originals/<hash of its path>/name.pdf`, as a hard link where the file system
    allows (it costs nothing: the file is replaced by a rename, so the link keeps the original bytes), else a copy.
    Entries older than 30 days are removed at the next such save. No UI; it is a safety net for a writer bug.
  - *A one-time notice* after the first time notes go into a PDF of the user's: "Your notes are saved in
    lecture.pdf. Its pages stay as they were; other PDF apps show the notes as annotations." (a snackbar, setting
    `pdfOnlyIntoPdfNoticed`). Not a dialog before the save: the user chose PDF files with that explanation at the first
    start, and a question on every first save of a PDF would be friction the chosen mode is meant to remove. The notice
    makes sure nobody is surprised that the PDF itself changed.
- **Pasted pages go into the PDF.** A PDF with notes never writes `.name.pages.pdf` or `.name.next.pdf`: pasted
  PDF pages live in the merged PDF in the cache until the save copies them into the file (as for every hybrid PDF).
  The paste note says "it goes into the PDF when saved".
- **Images are inside.** An image written on (`photo.png` opened) is saved as `photo.pdf`; on every save of a PDF with
  notes in this mode, an image file shown as a page background becomes an attached image of the embedded `.xopp`
  (embedded in the PDF), so the PDF does not depend on the image file. Images inserted on pages were always inside
  the `.xopp` data.
- **Autosave and recovery stay in the app cache.** Upstream (and Xournal++ files mode) autosaves a saved document as
  `.name.autosave.xopp` next to it; in PDF files mode every autosave goes to `~/.cache/xournal-qt/autosaves/<pid>-<tab
  serial>.autosave.xopp`, the name unsaved tabs always had (`DocumentSession::autosavePath`). Recovery after a crash
  looks there for saved documents too (`SessionRecovery::findCandidates`, whatever the mode is now), recovers the
  document as its PDF, and its next save writes into it. Crash (emergency) saves were already in the cache; so are the
  clean copies, the merged PDFs of pasted pages and the kept originals.
- **Existing `.xopp` files keep their format**: they open and save as `.xopp` (with their sidecars, which are part of
  that format); Save as starts on `.xopp` for them. Save as → "PDF with notes" and its old-`.xopp` question work as
  before.
- **Xournal++ files mode** is exactly the behaviour before the question existed.
- Not changed by the mode: Share (a hybrid PDF, "For Xournal++" exports into a chosen folder), the archive export, the
  "edited in another app" check, and the explicit per-document "Keep it updated for Xournal++" and the global "also
  write a .xopp" setting (both write a `.xopp` next to the PDF because the user asked for it).
- **Windows, not verified:** the first save renames the new file over the user's PDF. The document itself reads its
  pages from a copy in the cache before that (as before), but another program or a preview worker of the app that
  has the PDF open without `FILE_SHARE_DELETE` makes the rename fail on Windows; the save then reports an error and
  the file stays as it was. Incremental saves rename over the file too. A retry, or `ReplaceFileW`, may be needed
  there.
