# Adopting annotations from other apps

Status: research written 2026-10-05 (before the code), built as `qt/adopt-annotations` (B8 of
[the ideas of 2026-10](history/README.md)). Scope (the author through the integrator, 2026-10-05): real sample PDFs of the
apps come later; until then only what is clear without them is built: the standard annotation types as ISO 32000
defines them, placement, consent, the layer, undo and the round trip, and the app's name as a label. No guessing of
app-specific encodings (flattened GoodNotes ink, Drawboard pressure); the place where such handling plugs in is
`Converter::convert` in `AdoptAnnotations.cpp`. The author (2026-10-05): "B8 sounds very good, I am especially interested in
importing annotations that come from the GoodNotes PDF exports (no. 1) and Drawboard PDF (no. 2); the rest like Mac
Preview or iOS Preview and other PDF annotations hopefully work as well."

The goal: a PDF that another app marked up (ink, highlights, text boxes, shapes, notes, signatures) opens with those
marks **editable** here: strokes, highlighter strokes, text boxes, sticky notes and pictures in a layer of their own,
instead of a picture inside the page that can only be looked at. With consent, once per file, and undoable.

## Research: how the apps write their marks

Web search was available only in part (the proxy blocks support.goodnotes.com and forums.zotero.org; GitHub search is
not reachable from the session). No sample file of any app was available. What is below is marked:

- **verified**: read in a source during this research (the source named), or fixed by the PDF standard (ISO 32000);
- **assumed**: from general knowledge of these apps and of PDFs seen before, not checked against a file now. The
  fixtures in the tests are built to match the assumptions; real exports must confirm them (see "Samples wanted").

### The standard (ISO 32000-1/-2, verified)

- Annotations sit in a page's `/Annots`. Markup types: `/Text` (a note icon with `/Contents`), `/FreeText` (a text
  box: `/Contents`, plain; `/RC` rich text; `/DA` like `/Helv 12 Tf 1 0 0 rg`; `/DS` like
  `font: 12pt Helvetica; color:#FF0000`; `/C` its background; `/CL` a callout line), `/Line` (`/L`, line endings
  `/LE`), `/Square`, `/Circle` (`/Rect`, inset by `/RD`; `/IC` the interior colour), `/Polygon`, `/PolyLine`
  (`/Vertices`), `/Highlight`, `/Underline`, `/Squiggly`, `/StrikeOut` (`/QuadPoints`, 8 numbers per piece; Acrobat
  writes them upper left, upper right, lower left, lower right, which is not the order the standard draws),
  `/Caret`, `/Stamp` (`/Name`, the look in `/AP`), `/Ink` (`/InkList`: an array of point arrays, one per stroke),
  `/Popup` (the window of another annotation, `/Parent`), `/FileAttachment`, `/Sound`, `/Redact`.
- Common keys: `/Rect` (in default user space, i.e. the page before `/Rotate`, not clipped to the crop box), `/C`
  (0, 1, 3 or 4 numbers: none, grey, RGB, CMYK), `/CA` (opacity), `/BS << /W width /S /S|/D|/B|/I|/U /D [dash] >>` or
  the older `/Border [h v w]` (default width 1), `/F` flags (2 hidden, 32 no view), `/NM` a name, `/T` the author,
  `/Contents`, `/IRT` a reply to another annotation, `/AP << /N appearance >>` the look as a Form XObject that a viewer
  draws instead of building it from the keys.
- **There is no pressure in the standard.** `/InkList` holds points without widths; one `/BS /W` holds for all strokes
  of an annotation. An app that writes pressure strokes can only show them through `/AP` (each stroke a filled
  outline, or many segments of different widths).
- `/Rotate` turns the shown page; annotation coordinates do not turn with it. A viewer places what it shows relative
  to the crop box.

### GoodNotes 5 / 6 (the author's no. 1)

- **verified** (goodnotes.com and the search engine's quotes of support.goodnotes.com, 2026-10-05): exporting as PDF
  has "Include annotations" and "Include page background" switches, and a "PDF Data Format" of **Editable** or
  **Flattened**. Flattened: "everything becomes one layer and can't be edited, clicked or modified after exporting";
  "bakes ink/highlighter into the page". Editable keeps links and the outline and "movable annotations".
- **verified** (Zotero forum thread "Can't extract annotations from GoodNotes 5 PDFs", as quoted by the search
  engine): GoodNotes highlights "are closer to inking rather than actual highlights": they are **not** `/Highlight`
  annotations, and Zotero/ZotFile cannot list them; GoodNotes **text boxes are "saved as part of the pdf file rather
  than comments"**, i.e. drawn into the page content, not `/FreeText`.
- **assumed**: an *Editable* export writes the handwriting as `/Ink` annotations (one per stroke or per group of
  strokes), `/InkList` with the points, `/C` the colour, `/BS /W` a width, and an `/AP` that draws the stroke as
  GoodNotes shows it (for the fountain and ball pens a filled outline: that is where the pressure is); the
  **highlighter as `/Ink` with an opacity** (`/CA` around 0.3–0.5, or the opacity in the appearance's graphics state)
  and a wide `/BS /W`. Shapes drawn with the shape tool are ink too. Images and text boxes are page content.
- **assumed**: `/Producer` or `/Creator` names GoodNotes ("GoodNotes", "Goodnotes 6" or similar).
- *Flattened* exports have the ink in the page content as vector paths (or images). Nothing in such a file marks which
  paths are ink and which are the document's own drawing, so **flattened GoodNotes ink is left as page content**
  (shown as before; not editable). No reliable signature exists to tell them apart; guessing would cut paths out of
  the user's lecture slides.

### Drawboard PDF (the author's no. 2)

- **verified** (drawboard.com): Drawboard writes standard annotations that "appear in any PDF viewer with the
  markups"; its ink is pressure sensitive. The hybrid PDF research of this fork ([hybrid-pdf.md](hybrid-pdf.md),
  "Faster PDF saves") already noted that Drawboard saves by appending incremental updates, like Acrobat.
- **assumed**: ink as `/Ink` with `/InkList`, `/C`, `/BS /W`, `/CA`, and an `/AP`; the pressure only in the `/AP`
  (variable width); its highlighter pen as `/Ink` with opacity (and `/BM /Multiply` in the appearance); text
  highlights as `/Highlight` with `/QuadPoints`; text as `/FreeText` with `/DA`; shapes as `/Square`, `/Circle`,
  `/Line` (with `/LE` arrows), `/Polygon`, `/PolyLine`; notes as `/Text`. No custom keys known.
- **assumed**: `/Producer` or `/Creator` mentions Drawboard after a full save; after an incremental save the original
  producer may stay, so the app is often not known by name.

### Apple Preview (macOS) and Markup (iOS, iPadOS: Files, Mail, Photos; the PDFKit apps)

- **verified** (KrankyBearReader release notes v0.5.0, which handles Preview files): Preview attaches private editing
  data to the annotations it writes, `/AAPL:AKExtras` with `/AAPL:AKAnnotationObject` inside (an archived
  AnnotationKit object). Rewriting those annotations loses Preview's ability to edit them. A real Preview arrow had
  its `/L` end points ~145 pt outside its own `/Rect` (so lines are placed by `/L`, never by `/Rect`).
- **assumed**: the PDFKit types: sketches as `/Ink`, highlights / underline / strike through as markup annotations
  with `/QuadPoints`, text as `/FreeText` (`/DA` with a font like `/HelveticaNeue 12 Tf`), shapes as `/Square`,
  `/Circle`, `/Line`, notes as `/Text` with a `/Popup`, **signatures as `/Stamp`** whose `/AP` holds the signature
  (vector paths), loupes and speech bubbles as `/Stamp` or `/FreeText` with an appearance. Preview rewrites the whole
  file on save; `/Producer` is "macOS Version 14.x (Build …) Quartz PDFContext" (iOS: "iOS Version …").

### Others

- **Acrobat** (assumed): every standard type with `/AP`, `/NM` a UUID, `/T` the author, `/RC` rich text, `/Subj`
  ("Highlight", "Pencil" for ink); replies are `/Text` with `/IRT`; signatures as `/Stamp` or `/Ink`.
- **Xodo** (Apryse/PDFTron, assumed): standard types with `/AP`; highlights may carry the highlighted text in
  `/Contents`; ink without pressure. `/Producer` mentions PDFTron or Apryse when it wrote the file in full.
- **Zotero** (assumed): exports highlights (`/Highlight`, or `/Underline`), notes (`/Text`), ink and images (as a
  `/Square` area) with the comment in `/Contents`.
- **Okular** (assumed): `/NM` like `okular-{uuid}`.
- **Notability** (assumed): exports flattened, like GoodNotes' flattened format.

### Samples wanted

Real exports turn the assumptions into facts. Each on a page of a text PDF (a lecture page), in the app's defaults:

1. **GoodNotes 5 or 6**, the same page exported three times: Editable with annotations, Flattened with annotations,
   Editable without page background. On it: a few words with the fountain pen (pressure), the ball pen, the
   highlighter over a line of text, a straight line and a rectangle made with the shape tool, a text box, a pasted
   image, the lasso-moved stroke, a stroke with the eraser through it.
2. **Drawboard PDF**, saved (Ctrl+S, its incremental save) and once with "Save as": pen with pressure (light and hard),
   the highlighter pen, a text highlight with the text tool, a text box, a rectangle, an ellipse, an arrow, a note
   (comment), a stamp or image if it has one.
3. **Preview (macOS)** and **Markup (iOS/iPadOS)**: a sketch, a highlight with a note, underline, strike through, a
   text box, a rectangle, an arrow, a speech bubble, a signature, a loupe.
4. If at hand: **Acrobat** (pencil, highlight, a reply to a comment, a stamp), **Xodo**, **Zotero** (an export with
   annotations), **Notability**.

`qpdf --qdf --object-streams=disable in.pdf out.pdf` makes them readable; the files go into a test fixture folder of
the fork (never `test/files`).

## What the app does

### Which annotations count

All annotations of the pages the document shows, except:
- ours (`/NM` starting with `xopp:`, or the private `/XournalQt` key);
- hidden ones (`/F` hidden or no view);
- links, form fields (`/Widget`), file attachments, sounds, movies, screens, printer marks, trap nets, watermarks,
  3D, redactions and carets: they are not marks of the kind this is about and stay in the PDF as they are;
- `/Popup` (they go with the annotation they belong to).

Those are counted and named on opening, and converted on request.

### Which app

From the annotations first, then from the file: `/AAPL:AKExtras` on an annotation → Preview; `/NM` `okular-…` →
Okular; else `/Producer` and `/Creator` (GoodNotes, Drawboard, Preview / Quartz, Acrobat, Xodo / PDFTron / Apryse,
Zotero, Notability, Foxit, PDF Expert, Okular, Samsung Notes). Unknown: "another app".

### The conversion

Everything is placed through the page's crop box and its `/Rotate` (the same placement as our hybrid PDF uses for
its own annotations, inverted), and the page's space for notes ([note-space.md](note-space.md)).

| PDF | Becomes |
| --- | --- |
| `/Ink` | one stroke per `/InkList` path; width `/BS /W` (or `/Border`), colour `/C`; **a translucent ink** (`/CA`, or the opacity in its appearance's graphics state, below 0.8) **is a highlighter stroke**, else a pen stroke; a dashed border a dashed stroke. No pressure (none in the file; see above). |
| `/Highlight` | a highlighter stroke over each piece of `/QuadPoints`, as wide as the piece is high, in its colour (as marking selected PDF text does) |
| `/Underline`, `/StrikeOut` | a thin highlighter stroke at the bottom, through the middle |
| `/Squiggly` | a zigzag highlighter stroke at the bottom |
| `/FreeText` | a text box: `/Contents`, font size and colour from `/DA` (or `/DS`), wrapped at the box's width |
| `/Square`, `/Circle` | a closed stroke (a rectangle, an ellipse), `/IC` its fill |
| `/Line` | a stroke from `/L`, with its arrow heads (`/LE`) as strokes |
| `/Polygon`, `/PolyLine` | a closed / an open stroke through `/Vertices` |
| `/Text` (a note) | a sticky note at the icon with the note's text |
| `/Stamp` | a picture: its appearance drawn by poppler (signatures from Preview, Acrobat's stamps) |
| a comment (`/Contents`) of any other converted annotation | a sticky note beside it (a highlight's note stays with it) |

### Where they go

A layer **"From GoodNotes"** (the app's name; "From another app") on each page that had some, above the page's
layers (below its sticky notes); notes are sticky notes of their own. One layer keeps them apart from the user's
ink (hide it to compare, delete it to drop them) and makes one annotation of ours per page in the PDF.

### The original annotations

They are **removed** from the document's background PDF at once (a copy in the app cache without them, the same
pages), so they are not shown twice; the file changes on the next save, which writes our annotations in their place
(a PDF with notes) or puts the pages next to the `.xopp`. **Undo** brings the original annotations back (the
document's earlier background PDF) and takes the converted ones away. Other apps then see our annotations instead of
theirs; the original of a user's PDF that becomes a PDF with notes is kept (`name.original.pdf`, or in the app cache
in PDF files mode), so the other app's own annotations are not lost.

### Asking

Opening a PDF (plain, or with notes) that has such annotations asks once: "This PDF has N annotations from GoodNotes.
Make them editable?" — **Make editable** / **Not now**. The question comes once per file (asked again only when the
file has more of them than when it was asked). ⋮ → Document → **Adopt annotations from other apps…** does it any
time; the Annotations panel shows the same offer at its top while there are some.

## What is built (`qt/adopt-annotations`)

Code: `qt/src/session/AdoptAnnotations.*` (scan, convert, the copy without them: qpdf, poppler for stamps),
`qt/src/session/DocumentAdopt.cpp` (the document takes them; the undo step), `PdfPageKeeper::takeBackground`,
`qt/src/app/AppAdopt.cpp` (scan on opening at idle priority, the question once per file, the conversion on a worker),
`DocumentNotices.qml` (the dialog), `MoreMenu.qml` (⋮ → Document), `AnnotationList.qml` (the panel's line). "Asked once" is remembered with the
document's places (`DocumentPlaces::adoptionOffered`: the number of annotations when asked; more later: asked again).

- **Which document pages**: every page showing a PDF page of the background (a PDF page shown twice gets the marks
  twice), and the generated pages of a PDF with notes whose page of the clean copy had them.
- **The copy**: of a user's PDF or a merged PDF, a merged PDF (`WithSource`) in the merged-PDF cache, owned by the
  document's `PdfPageKeeper` (so the document still stands for the user's PDF: its title, "Save as" suggestions); of
  the clean copy of a PDF with notes, a copy beside it in the hybrid cache. Popups of converted annotations go with
  them; annotations that are not converted stay.
- **The undo step** (`AdoptUndoAction`): the layers and the background swap. Undone, the earlier background comes
  back: the file itself while it is unchanged, else a hard link to its bytes taken when they were adopted (notes saved
  into the PDF itself write over it). It cannot be undone after a save that renumbered the PDF's pages (pages removed
  and a `.xopp` saved), and not redone after a `.xopp` save moved the copy beside the `.xopp` and the step was then
  undone: upstream's "could not undo/redo" message then, nothing changes.
- **Times**: each element gets the annotation's `/CreationDate` (else `/M`) as its creation time (timeline).
- **Comments**: a converted annotation's `/Contents` (a highlight's note) becomes a sticky note to its right.

Tests: `qt/tests/session/AdoptAnnotationsTest.cpp` (9; fixtures built with qpdf in a temporary folder): the count,
what is skipped and kept, the app's name; each standard type and its element; undo and redo; placement on pages
turned 0/90/180/270° with a crop box away from the origin, against where poppler draws a mark of the page; space for
notes; round trips as a PDF with notes (other apps see ours, `qpdf --check`, reopened editable, nothing left to
adopt) and as a `.xopp` (the copy beside it, the user's PDF untouched); a PDF with notes marked up in Preview; undo
after the notes went into the PDF itself; GoodNotes-like (highlighter opacity only in the appearance) and
Preview-like (a signature stamp, an arrow outside its rectangle) files. `MainWindowTest.annotationsOfAnotherAppAre
OfferedOnceAndMadeEditable`: the question, "Not now" remembered, ⋮ → Document, undo.

## Decided here (for the author to confirm)

1. **Removed, not hidden.** The originals leave the PDF on the next save (ours replace them); kept hidden they would
   come back in other apps only as invisible clutter. The original stays: `name.original.pdf` (or in the app cache)
   when the notes go into the user's PDF, and the user's PDF itself with a `.xopp`.
2. **One layer per page, "From <app>"**, not the current layer: easy to hide, compare and delete; one annotation of
   ours per page in the PDF.
3. **Translucent ink is a highlighter** (`/CA` below 0.8, or an opacity / multiply blend in its appearance's graphics
   state); it is then drawn with the highlighter's own opacity, not the exact `/CA`.
4. **No pressure**: the standard has none; widths are even (`/BS /W`). Reading widths from appearance streams waits
   for samples.
5. **Flattened ink stays page content** (GoodNotes "Flattened", Notability): no reliable way to tell it from the
   page's own drawing.
6. **Comments become sticky notes** (yellow, 180 pt wide, beside the mark), also the note a highlight carries; notes
   of Xodo that only repeat the highlighted text would be notes too (to be seen with samples).
7. **Asked once per file** (again only when it has more of them); never converted without being asked.
8. **Text boxes**: plain `/Contents` (rich text `/RC` ignored), the font family mapped to Sans / Serif / Monospace,
   wrapped at the box's width; their border and background are not drawn.
