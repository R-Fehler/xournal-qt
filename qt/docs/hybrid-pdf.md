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
