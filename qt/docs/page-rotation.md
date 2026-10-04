# Rotating pages

The author (ideas round of 2026-10-04): "rotate all pages / selected pages / current page by 90 degree left or right."

## Where
- The page menu (sidebar and page grid: ⋮, right click, and the sidebar's selection bar): two icons beside
  "Insert pages…", for the page or, when it is selected, the selection.
- The page grid's action bar (selection mode): rotate left / right for the selected pages.
- ⋮ → Page → Rotate: this page left / right, all pages left / right.

Every command is one undo step (Ctrl+Z in the sidebar or grid, the canvas's undo). Code:
`qt/src/canvas/PageRotate.*` (`pagerotate::apply`, `PageRotateUndoAction`), `AppController::rotationOf` /
`rotatePages`; tests `PageRotateTest` (canvas) and `MainWindowTest.rotatingPagesFromTheMenus`.

## What turns
- The page's width and height swap. A quarter turn to the right takes a point (x, y) to (h − y, x), to the left to
  (y, w − x) (w × h: the size before).
- Strokes turn point by point; texts, images, TeX images and links turn by their transformation (upstream's
  `RectangularElement` matrix, saved as the `matrix` attribute of a text and image, as upstream writes a turned
  selection). Upstream's `EditSelection` rotates with `Element::rotate` (cosine and sine of the angle); a quarter turn
  is composed here exactly (0, 1, −1), so nothing gets rounding noise.
- Markdown boxes and sticky notes stay upright: they are laid out, tapped and edited unturned, so their middle goes
  where the turned page puts it. What is on a sticky note goes with it.
- The page's own Markdown text stays at the margins and flows anew on the new size (as a page size change does,
  `pagesize::TextReflow`).
- Space for notes beside a slide turns with it (the space on the right is below after a turn to the right).
- Backgrounds: plain, ruled, graph and the others are drawn for the new size; an image background is turned (a new
  PNG picture, attached to the document; the user's picture file stays as it is); PDF pages: below.
- Undo puts every point and transformation back exactly as it was (they are kept in the undo step); redo turns again
  from that same state. Turned pages get a new revision, so the canvas, the thumbnails and the previews are drawn again.

## PDF pages
The author has not decided yet how a turned PDF page is kept in a `.xopp` (upstream draws a PDF page only as the PDF
has it). So, for now (`pagerotate::PdfPages`, chosen by `AppController` from the document's save format):
- **A PDF with notes, and PDF files mode** (the document is or will be saved as a PDF with notes): the PDF page itself
  turns. A copy of the page with its `/Rotate` changed goes into the document's merged PDF in the cache (the way a
  pasted PDF page does, `PdfPageKeeper`), and the page shows that copy. Saving copies it into the file as the page's
  base page: every PDF app shows it turned, our ink annotations are placed on it as for any page with a `/Rotate`
  (`HybridPdf`), the embedded `.xopp` has the turned size and turned ink, and the PDF's text is found at its turned
  place. A PDF that is annotated but not saved yet is not touched until its first save. Undo shows the page from
  before again (while the merged PDF's page numbers stay; after a save renumbered them, the PDF page is turned back
  the same way). A page turned twice in a row waits for the first copy to be merged (a moment on a long PDF).
- **A `.xopp`** (Xournal++ files mode, or a `.xopp` that is open): PDF pages are left as they are; the other pages of a
  selection or of "all pages" turn. The page menu and ⋮ → Page → Rotate say why ("PDF pages can only be rotated in
  PDF files with notes"), and the message after turning says how many PDF pages stayed.

A later decision to keep a turned PDF page in a `.xopp` as an attribute only xournal-qt reads would be another value
of `PdfPages`: `blockedOf` lets the pages through, the page keeps its PDF page and records the turn, and the PDF
background drawing (`notespace::renderPdf` and the views), the PDF text (search, selection, links) and the writers
apply it. The same turned-copy route also works in a `.xopp` (its merged PDF is the hidden `.name.pages.pdf`), if the
author prefers that: one line in `pdfRotation` (`AppController.cpp`).

## Not done
- Turning by other angles, or the canvas view (a separate item in TODO.md).
- Markdown boxes and sticky notes turning with the page (they would need a turned layout, hit test and editor).
- A haptic tick: the app has none anywhere yet.
