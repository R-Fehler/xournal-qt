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
  has no incremental save; measure on a 1,300-page PDF to see whether the full rewrite is fast enough.
- Pages removed from the document are removed from the file. Base pages are copied once and kept across saves.
- Autosave and crash saves stay `.xopp` files in the cache (as today). The hybrid file is written only on a real
  save.

## How it appears in the app

- **"Save as hybrid PDF…"** for any document. For an annotated PDF it suggests `lecture.notes.pdf`, or
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
3. **UI** (done).
   - More → **Save as hybrid PDF…** for any document. The suggestion: the document's own hybrid PDF; for an
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

   **Not fast enough for a long PDF:** the save runs on the UI thread, like the `.xopp` save, and blocks the window
   for about 6 s at 1,300 pages (1 s at 50 pages). Next step: write in the background (the drawing and the `.xopp`
   under the document's lock first, then qpdf on a worker; the saved state of the undo stack taken at the start).
   qpdf has no incremental save; appending an incremental update ourselves would make saves of long PDFs cheap.
