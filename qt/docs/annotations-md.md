# Annotations as Markdown

Status: **stage 1 built in `qt/annotations-md`** (an experiment, 2026-09-26). The author's goal (2026-09-25): collect a
document's highlights and notes as Markdown, with links back to their places.

## The Annotations panel

The page sidebar has a fourth button next to Pages / Layers / Contents (the highlighter icon). It lists the current
document's annotations by page, top to bottom:

| Kind | What is listed |
| --- | --- |
| Highlight | The highlighter's strokes over PDF text, with the text under them. Strokes drawn one after another in one color on the same or the next lines are one highlight. Highlight, underline and strike through made from selected PDF text are highlighter strokes too. |
| Highlight in the PDF | The PDF's own markup annotations (highlight, underline, squiggly, strike out) made in other apps, with the text under them and their note. The annotations our hybrid PDF writes for its own layers (`xopp:` names) are not listed twice. |
| Text / Markdown | Text boxes and Markdown boxes with their text. The panel shows a Markdown box formatted. |
| Handwriting | The pen's strokes in the margins and free areas, as a small picture. A stroke joins the piece written just before it when it is within 18 pt (about 6 mm); pieces that overlap are one. Ink mostly over PDF text lines (underlines, circles, marks) is left out: it marks text, it is not a note. A highlighter stroke over no text (neither PDF text nor a text box) counts as handwriting. |
| Link | A link marker: a Markdown box that is only `[title](target)` ([links.md](links.md)). |
| Note | Sticky notes: another block builds them and plugs in with `annotations::setNoteSource` (a function called for each page read, under the document's read lock). The filter shows "Notes" only when there are any. |

- A tap scrolls to the item (its page, with the item in view) and remembers the place for Back, like other jumps.
- The filter button chooses the kinds shown; the counter says "N of M" while the filter hides some.
- **It follows the document.** Page changes are collected and read 600 ms after the writing pauses, on one worker
  thread at low priority, and not while the canvas has visible pages to draw. Each page's items are kept by its
  revision (`DocumentSession::pageRevision`), so an edit reads the page it changed, not the whole document. Nothing
  is read while the panel is hidden. The items of each open document are kept while it is open, so switching tabs
  back shows them at once.
- The PDF's text is read (with a poppler instance of its own, `PdfLayoutReader`) only for pages that need it: pages
  with highlighter strokes, handwriting or markup annotations. Most pages of a big PDF have none.
- The pictures of handwriting come from the `image://annotation/<session>/<revision>/<rect>` provider on a worker of
  their own, only for the rows on screen, and only the page's layers (no background).

Code: `qt/src/shell/Annotations.*` (what counts, the Markdown), `qt/src/shell/AnnotationsModel.*` (the panel's
model, the worker, the pictures), `qt/src/app/AppAnnotations.cpp` (export), `qt/src/app/qml/AnnotationList.qml`.

## Export as Markdown

The download button of the panel writes a `.md`:

```markdown
# Annotations: lecture one

[lecture one.xopp](../Lectures/lecture%20one.xopp)

## Filtering

> Kalman filters estimate the hidden state. ([p. 1](../Lectures/lecture%20one.xopp#page=1&pdfpage=1))

- Ask about the Q matrix ([p. 1](../Lectures/lecture%20one.xopp#page=1&pdfpage=1))

- [p. 1](../Lectures/lecture%20one.xopp#page=1&pdfpage=1) (handwriting)

## Update

> The update step weighs the measurement. ([p. 2](../Lectures/lecture%20one.xopp#page=2&pdfpage=2))
>
> Check the gain

> **Summary**: the gain balances model and measurement.
>
> ([p. 2](../Lectures/lecture%20one.xopp#page=2&pdfpage=2))

- [Kalman notes](../Lectures/notes.xopp#page=2) ([p. 2](../Lectures/lecture%20one.xopp#page=2&pdfpage=2))
```

- A title, and a link to the document, relative to the `.md`.
- A heading per chapter: the PDF outline, else the chapters written in the document (the Contents sidebar's), with
  the chapters above a chapter written once. Without chapters, a heading per page.
- Highlights as quotes (with the PDF annotation's note under them), text boxes and notes as bullets, Markdown boxes
  as quotes that stay Markdown (their relative links rewritten for the `.md`'s folder), link markers as links.
- Each item ends with a link to its page in the app's link format ([links.md](links.md)): `#page=N`, plus
  `pdfpage=N` for a page showing a PDF page, else the page's text fingerprint (`text=`), so the link finds the page
  after pages were inserted before it. Plain text is escaped (`*`, `_`, `[`, `$`, a `#` or `1.` at a line's start).
- **Handwriting: a page link with "(handwriting)", not a picture.** Our Markdown editor and Markdown boxes do not draw
  images yet ([markdown-boxes.md](markdown-boxes.md), "Not yet"), so a picture would show as its alt text there.
  The picture export is built and tested: `XQT_ANNOTATION_PICTURES=1` writes each piece as
  `<name>.assets/p<page>-<n>.png` (Typora's folder convention) and links it as `![Handwriting, page N](…)`. Turn it
  on by default once the editor draws images.

**Where it goes.**
- Xournal++ files mode: `<name>.annotations.md` next to the document, without asking. If it exists, a dialog asks:
  Replace (it says that text added by hand is lost), Save as…, or Cancel.
- PDF files mode ([DocumentMode](../src/session/DocumentMode.h)): nothing may be written next to files, so a save
  dialog asks, suggesting the same name.
- A document never saved: the export asks to save it first (the links need its file).

After the export the snackbar offers to open the file.

## Stage 2: keep it updated (not built)

The idea: write the generated part between two markers and, on the next export, replace only that part, keeping what
the user wrote around it.

```markdown
My summary of the lecture, written by hand.

<!-- xournal-qt:annotations begin source="lecture one.xopp" hash=3f9a… -->
…generated…
<!-- xournal-qt:annotations end -->

Questions for the tutorial.
```

- **Markers as HTML comments**: invisible in every Markdown viewer (Obsidian, Typora, GitHub, our editor's preview),
  and they survive editors that reformat Markdown. `source=` says which document the section belongs to, so one
  `.md` can hold the sections of several documents.
- **Guard against losing edits inside the section**: the begin marker keeps a hash of the generated text. Before
  replacing, the section is hashed again; if it differs, the user edited inside it and the app asks (replace, keep,
  or write a new section below).
- **When**: on Export when the file exists with markers (Replace becomes "Update the section"), and, as an option
  per document, after each save (debounced, only when the file exists and has the markers; never creating the file
  by itself). A file without markers: the section is appended at the end.
- **Pictures** (once they are on): the section lists the picture files it wrote; pictures no longer listed are
  removed, others in the folder are left alone.
- **Writing safely**: read, splice and write with `QSaveFile`; if the file changed on disk between reading and
  writing (open in Obsidian, synced), read it again.
- **PDF files mode**: only for a file the user picked once in the save dialog (remembered per document in the
  library's index, not next to the file).

### My view

Section-level replacement is worth building: it is small (a parser for two comment lines, a hash, the splice), it
fits how people use such files (a summary above, questions below), and the hash guard makes it safe. I would not go
further, to per-item anchors that keep a user's comment under each highlight: items have no stable identity across
edits (a highlight re-drawn or extended is a new stroke), so matching comments to items again would be guesswork, and
wrong guesses move a user's text under the wrong quote. If comments per item are wanted, the better place is the
document itself (a text box next to the highlight, or a sticky note), which the export then lists. Updating after
each save should stay opt-in: a file that changes by itself surprises people who also sync or edit it elsewhere.

## Not yet

- Handwriting as pictures by default (needs images in the Markdown editor).
- Recognised handwriting text (no handwriting recognition in the app).
- Links to the exact place on a page (the link format has pages, chapters and headings; the panel jumps to the place,
  the Markdown links to the page).
- The strokes' times: upstream strokes carry no time of writing, so "written close in time" is the order in which
  they were written (the layer's order).
