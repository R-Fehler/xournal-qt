# Space for notes beside slides

Status: built in `qt/note-space` (2026-09-26). The design was written before the code; the sections below say what
was built.

The author (2026-09-26): "Space for notes beside slides: an option for inserting on the current page and on all pages,
with adjustable amounts to add to the margin of choice. They can be combined, e.g. space at the bottom and on the
left side. Make sure this is somewhat efficient and does not break the PDF structure and search."

A lecture slide (a PDF page, often 16:9) gets blank writing space around it **on the same page**. The slide and its
notes stay together when scrolling, in the page grid and in every export. This is different from a blank page after
each slide, which the dialog also offers.

## The model: a page that is larger than its slide

- A page gets **note space**: four amounts in points, `left`, `top`, `right`, `bottom`
  (`NoteSpace`, `src/core/model/NoteSpace.h`, held by `XojPage`).
- The page's size includes the space: width = slide width + left + right, height = slide height + top + bottom.
- The PDF background is drawn at `(left, top)`, at its own scale. Nothing is scaled, and the PDF is never rendered at
  another size: `PdfCache` keeps one picture per PDF page and zoom, and the page draws it at the offset.
- Other backgrounds (plain, ruled, graph, ...) fill the whole page as before; they have no offset.
- The "slide" of a page is the rectangle `(left, top, width - left - right, height - top - bottom)`. For a PDF page it
  is the PDF page.

Why the four amounts and not just an offset: the offset (left, top) is all that drawing needs, and for a PDF page the
right and bottom amounts could be derived from the PDF's size. A page with a plain background has no such reference,
and "set everything to 0 restores the page exactly" must hold for it too. Four numbers cover every page the same way,
and the dialog can show what a page has.

Why in upstream's `XojPage` (a small seam, ADR-0002) and not in a table of our own: a page is copied (duplicate, copy
and paste, undo of a deletion, the page clipboard), saved, loaded and drawn by upstream code. A table keyed by page
would be lost or wrong in each of those places; a field of the page travels with it. Upstream's `BackgroundView` and
exports need the offset too, so the drawing seam is needed anyway.

Pages with an **image background** are left out: upstream stretches the image over the page, so a larger page would
distort it.

### In the `.xopp`

One attribute on the page, written only when there is space:

```xml
<page width="1008" height="405" notespace="72 0 216 0">
  <background type="pdf" domain="absolute" filename="lecture.pdf" pageno="3"/>
```

`notespace="left top right bottom"` in points. Upstream's parser ignores attributes it does not know, so the file
stays valid for Xournal++.

### What upstream Xournal++ shows

Upstream reads the page at its enlarged size. It draws the PDF at its natural size at the page's top left (`PdfCache`
never scales it to the page). So:

- space only on the **right and bottom** (the presets): upstream shows exactly what we show;
- space on the **left or top**: upstream shows the slide at the top left and the free space on the right and at the
  bottom. Ink written on the slide appears shifted by the offset relative to the slide. Nothing is lost; saving the
  file in Xournal++ drops the attribute, and the page stays enlarged with the slide at the top left.

(Checked with the GTK build of `../xournalpp`; see "Checked" below.)

## Where the offset applies

Every place that maps between a PDF page and our page coordinates adds `(left, top)`:

| What | Where |
|---|---|
| The canvas (and so the reference view) | `PageRaster` gives `PdfBackgroundView` the offset (upstream seam: white paper, the cached PDF picture at the offset, moved by a whole number of pixels so it stays sharp). The `PdfCache` picture is the same as without space: nothing is rendered at another size. |
| Thumbnails, page sketches (page grid, sidebar), the preview in the file, pasted PDF pages | `notespace::renderPdf` (`qt/src/session/PageNoteSpace.*`): `ThumbnailProvider::renderPage`, `DocumentSave::previewOf`, `DocumentSession`'s preview, the pending page of `PageRaster` |
| Plain PDF export, print (it prints that export), PNG/SVG export | `XojCairoPdfExport::exportPage`, `ImageExport::exportImagePage` (seams); the export factory takes the cairo backend for documents with note space, because upstream's qpdf backend lays the drawing over the PDF page's own box. The PDF's text stays text (poppler draws it into the cairo PDF). |
| PDF text selection, highlight/underline/strike marking, copy | `PdfElemSelection` (seam): poppler is asked in PDF coordinates; bounds, rectangles and the drawn region are page coordinates |
| PDF links, text columns (double tap, middle click) | `CanvasView::linkAt`, `textColumnAt` |
| Search hits and their boxes | `DocumentSearch::place` (the text index's character boxes), `findOnPage` (poppler), `termRects` (the library's pictures of pages with hits) |
| Document text index | unchanged: it keeps text and character boxes per **PDF** page, in PDF coordinates; the offset is added where boxes are placed on a page |
| Annotations panel | `annotations::read` carries the offset; the PDF's character boxes and the PDF's own highlights are moved by it before they are compared with our highlighter strokes |
| Hybrid PDF, archive PDF, incremental save | see below |
| A PDF page turned into an image (`PdfPageKeeper::toImageBackground`, when its PDF is gone) | the image is the whole page, slide at the offset; the page's note space is cleared then (the space is part of the picture) |
| A new page like the current one (`insertNewPage`) | a PDF page copies the note space with the size |

## Hybrid PDF, archive PDF and incremental saves

The base page of a slide with note space gets a **larger MediaBox and CropBox**. The page's content is not touched:
the crop box grows around it (by `left` on the left, `top` on the top, and so on, as the page is shown: with a
`/Rotate` the sides are mapped onto the PDF's box), and the media box takes the grown crop box in. That places the
original content at the offset without a form XObject or a `cm` operator, and everything in PDF space keeps working in
any viewer: text search and selection, the PDF's links, other apps' annotations. Our ink annotations are placed on the
crop box as before (`placementOf`), so they land at the right places; an archive PDF merges them there.

- The page's own boxes are kept on it in a private key (`/XournalQtBoxes`). The clean copy (the background of a hybrid
  PDF we open) gets them back, so the background has the slide's own size and the embedded `.xopp` adds the space
  again. Setting the boxes always starts from the originals: saving again never grows a page twice.
- The marker lists the pages with larger boxes (`/Spaces`, their places). An incremental save reads only those pages
  and the pages that have space now (not every page of a long PDF); a page whose boxes change is written again with its
  annotations. Taking the space away restores the boxes and removes the key.
- The base pages of the export for Xournal++ (`exportXopp`) are not enlarged: its `.xopp` says where the PDF goes.
- Limit: a PDF page whose crop box is smaller than its media box (print bleed) shows what lies outside its crop box in
  the space, in other viewers. Slides practically never have one; clipping it would need a content stream of ours.

## Applying it

`notespace::apply(session, pages, amounts)` (`qt/src/session/PageNoteSpace.*`):

- sets a page's space to the given amounts (not added to what it had): the page's size becomes
  slide + new amounts, and every element on the page (strokes, texts, images, Markdown boxes, links, sticky notes)
  moves by the change of (left, top), so ink stays where it was relative to the slide;
- amounts are in points, or relative: a fraction of the slide's width (left, right) or height (top, bottom), worked
  out per page; they are rounded to whole points (so the selection region of the PDF text, which is in whole points,
  moves exactly);
- one undo step for all pages (`NoteSpaceUndoAction`: per page the old and new space and size; undo moves the
  elements back);
- the size change is announced per page. The canvas renders again only the pages in view and those that have a
  picture; the others are drawn when they come into view (before, every page whose size changed was queued at the
  priority of the visible pages). A render that ran while its page changed size renders again. 300 pages: about 1 ms
  in the session, 5 ms with the document open in a view (tests below);
- all amounts 0 restore the slide's size and the elements' places exactly; image backgrounds are left out.

`notespace::insertBlankAfter` inserts a plain page the size of the slide after each chosen page (one undo step).

## The dialog "Space for notes"

`NoteSpaceDialog.qml`, from the page menu (an icon in its grid: this page, or the selected pages) and from the More menu
("Space for notes…": all pages; all pages with a PDF background when there are any).

- Four amounts (left, right, top, bottom) in **% of the slide** (the default: the presets are relative) or in **cm**
  (the unit the app shows elsewhere, e.g. the geometry marks); switching converts them.
- Presets: "Half the width on the right", "Below: as high as the slide", "None".
- A preview to scale: the page, the slide in it, changing as the amounts change.
- For: this page (or the selected pages) / all pages / all pages with a PDF background, with the number of pages.
- "A blank page after each instead" as a secondary button.

## Tests

- `NoteSpaceTest` (`-L session`): the `.xopp` round trip (space and size); ink moves with the slide and no space
  restores the page exactly; relative amounts per page, image pages left out; 300 pages in one quick undo step; blank
  pages after slides; drawing (the PDF at the offset, pixel for pixel the same picture moved, the ink on it, the space
  white; the same for `renderPdf`); PDF search, the index's boxes and `termRects` at the offset; the plain PDF export;
  the hybrid PDF (boxes, poppler draws the slide at the offset and finds the word where it is drawn, opened again with
  the space and a clean background); the archive PDF; a rotated page; incremental saves after changing the space and
  after taking it away; upstream Xournal++ (below).
- `CanvasReplayTest` (`-L canvas`): PDF text selected and highlighted on the slide; PDF links where they are drawn;
  300 pages with the document in view.
- `PageRaster.aPageWithSpaceForNotesMatchesUpstreamsDrawing` (`xoj-unit-tests`): the canvas raster equals upstream's
  drawing of the page.
- `Annotations.withSpaceForNotesHighlightsFindTheTextUnderThem` (`-L shell`).
- `MainWindowTest.spaceForNotesFromThePageMenuAndForAllPages` (`-L ui`): the dialog from the page menu and for all
  pages, presets, preview, cm, apply, one undo.

## Checked

- **Upstream Xournal++** (the GTK build at `../xournalpp`, `NoteSpaceTest.upstreamXournalpp…`, through its PNG export):
  it opens the file without a warning and shows the page at its larger size with the PDF at the top left. Space only
  on the right and below looks exactly as here; with space on the left or at the top the slide is at the top left and
  the ink is shifted against it by the offset. Saving in Xournal++ drops the attribute (the page stays larger).
- Other PDF viewers: poppler draws the hybrid, archive and plain PDFs with the slide at the offset and finds its text
  there (tests); qpdf's check passes in the hybrid PDF tests that already run it.
