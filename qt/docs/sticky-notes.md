# Sticky notes

Status: built in `qt/sticky-notes` (2026-09-26); copy, cut, paste and moving to another page in
`qt/sticky-clipboard` (2026-09-26); the darker edge and the shade, cheaper copy / cut / paste in `qt/sticky-look`
(2026-09-26). The author's request (2026-09-25): permanent sticky notes that the
user can write on and move around, with the ink and text on them staying attached; not a PDF popup note that other
viewers minimise; usable for self-testing by moving them over solutions; adaptable in size.

## What a note is

A sticky note is an opaque, coloured rectangle on a page, with its own content: strokes and texts drawn on it and
clipped to it. It lies above the page's ink.

- **Moving** a note moves its content along.
- **Resizing** (a handle at its bottom-right corner) changes only the paper: the content keeps its size and place
  (relative to the note's top left) and is clipped to the note. Why not scale: handwriting and text would be stretched
  and squeezed (the pen width and the font size with them), and the usual reason to resize a note is to cover more
  (or less) of a solution, not to change what is written on it. Nothing is lost: content outside the paper is kept and
  shows again when the note is made larger. This is how paper sticky notes and OneNote-style containers behave.
- **Writing on it:** a press with the pen, the highlighter, a shape tool or the text tool on a note writes on the
  note (the topmost note under the pen). The eraser erases on the note. Strokes that go beyond its edge are kept whole
  and clipped where they are drawn.
- **Selecting:** a select tool (rectangle, lasso, object) on a note selects the whole note: an outline, a round
  handle at the bottom-right corner, and a pill above the note with its colours, "Cover", copy, cut, delete and
  deselect (Ctrl+C, Ctrl+X, Del and Esc work too). A drag on the note moves it right away (pen, mouse, or a finger on
  the selected note); the handle resizes it (with any tool). While dragged a note stays inside its page; let go over
  another page, it goes there (see below). Choosing a tool that is not a select tool ends the selection, so that the
  pen then writes on the note.
- **Colours:** five pastel presets (yellow, pink, blue, green, orange). A new note is yellow.
- **Placing:** "Sticky note" in the shapes menu (next to the setsquare and the compass: things put on the page) and
  "Insert sticky note" in the More menu (next to "Insert image…"). The note goes in the middle of the visible part of
  the current page, selected, and the rectangle select tool is chosen (as for an inserted image), so that it can be
  moved and resized right away.

## Copy, cut, paste; moving to another page

The author's request (2026-09-26): copy, cut and paste a selected note (also Ctrl+C, Ctrl+X, Ctrl+V), mainly to copy
and move notes to other pages.

- **Copy / cut:** the pill's Copy and Cut, Ctrl+C / Ctrl+X, and Copy / Cut in the long-press (right-click) pill act
  on the selected note: the whole note goes onto the clipboard (its place, size, colour, cover, and everything on
  it). Cut removes the note's layer as one undo step ("Cut sticky note"). Copying with nothing selected does what it
  did before (nothing).
- **Paste:** Ctrl+V, and Paste in the long-press pill (and in the pill on PDF text), put a copied note on the
  current page of the view pasted in: the page in view (the page pressed, for the long-press pill), the second
  view's own page when pasting there (self-reference, while it is written in: `CanvasView::actingScope`). Another
  tab or another window works the same (the system clipboard). The note goes where it was on its old page when it
  fits; else it is moved inside the page (and made as large as the page only if it is larger). If it would lie
  exactly on a note of that page (a paste on the original's page, a second paste), it goes 16 points further down
  and right each time (up and left in the bottom right corner). It is placed on top of the page's layers (the new
  top note), selected, as one undo step ("Paste sticky note"). A covering note stays covering; peeking is not
  copied (the copy covers).
- **Keys that must not go elsewhere:** with a note selected, Ctrl+C/X/V are not taken by a text being written
  (`DocumentCanvasItem`), there is no selection of elements at the same time (selecting one ends the other), and in
  the page sidebar or grid (a page clicked there has the keys) Ctrl+C/X copy or cut the note, not the pages. Ctrl+V
  there pastes a copied note onto the current page when the note was copied after the last page copy (or a note is
  selected, or no pages are copied); else the pages, as before (`AppController::pastesNoteBeforePages`).
- **The clipboard format** is the app's own, `application/x-xournal-qt-sticky-note`: the layer's name (cover or
  not) and every element of the layer, the paper first, in upstream's element serialization (the one of its
  `application/xournal` clipboard: strokes with their pressure, texts, images). A picture of the note (PNG, twice
  the page resolution, drawn as in an export: no folded corner) goes along, so pasting into another app gives a
  picture. Xournal++ itself does not read the note's format; the note is not also put there as upstream's
  elements, since pasting those here would give loose elements instead of a note.
- **Moving to another page:** cut, go to the page, paste. Also: drag the selected note past its page's edge and let
  go over another page: it goes there, the point it was held by under the pointer (inside that page), on top, still
  selected. While dragged it stays at the edge of its page (a page's picture cannot show it beyond the page); it
  jumps on release. The drag and the change of page are one undo step ("Move sticky note to another page",
  `sticky::NotePageUndoAction`: the same layer leaves one page and is inserted in the other).
- **File format:** unchanged. A pasted or moved note is a note layer like any other.

## Cover mode (self-testing)

A note can be switched to **cover** (the pill's "Cover" button). A covered note:
- shows a folded corner (on the screen only), so it can be told from a note to write on;
- is not written on: the pen, the highlighter, the eraser and the text tool leave it alone (nothing is drawn under
  it either: a stroke that starts on it is not made);
- **peeks** when tapped (pen, finger, mouse, the hand): it turns see-through (a quarter opaque, with a dashed edge),
  so the answer under it can be checked. Another tap covers again. Peeking is a view state of the open document:
  not saved, not undoable, not in exports or thumbnails; a document opens with its notes covered. With a select tool
  a tap selects the note instead (to move it, or to switch "Cover" off).

**Hide / show the notes of a page**: a button in the page pill (next to the page number), shown on pages with notes.
It hides every note of the page (and shows them again). Like the eye in the layer panel it is not saved in the file
(Xournal++ does not save layer visibility), and a hidden note is also left out of exports, as hidden layers are.

## File format (Xournal++ compatible)

A note is **a layer of its own**, named `Sticky note` (covered: `Sticky note (cover)`). The layer's first element is
the **paper**: a closed rectangle stroke (tool pen, width 0.5, the note's colour, `fill="255"`, so opaque). The
other elements of the layer are the note's content, in their order. A new note goes on top of the page's layers.

```xml
<layer name="Sticky note">
  <stroke tool="pen" color="#fff59dff" width="0.5" fill="255">100 100 300 100 300 240 100 240 100 100</stroke>
  <stroke tool="pen" color="#000000ff" width="1.41" pressures="…">…</stroke>
  <text font="Sans" size="12" x="110" y="110" color="#000000ff">Answer?</text>
</layer>
```

- The note's rectangle is the bounding box of the paper's points; its colour is the paper's colour; cover mode is the
  layer's name. There are no attributes of our own: our app loads files with upstream's parser, which drops
  attributes it does not know (checked: Xournal++ opens a file with an unknown attribute on a stroke without an error
  and ignores it), so an attribute could not carry the grouping anyway. A layer name and the order of elements are
  kept by both programs.
- A layer named like a note whose first element is not a filled stroke is an ordinary layer (drawn normally).
- **In Xournal++** the file opens without errors and looks right: the filled rectangle hides what is below it, and
  the content is drawn on it. What differs there:
  - the content is not clipped: a stroke that goes beyond the note's edge (or content left outside after the note
    was made smaller) shows outside it;
  - cover mode is only a layer name (no peeking): the note simply covers;
  - each note is a layer in its layer list. Xournal++ selects the top layer when it opens a file, which is a note if
    the page has one: what is drawn then goes onto that note (in our app it is clipped to the note). Moving the
    whole layer's content in Xournal++ keeps the note together; deleting or moving the paper alone, or merging layers,
    turns the note into ordinary ink. That is acceptable; the grouping is ours.
- Why a layer and not a group of elements inside a layer: upstream's model has no groups and no place for extra
  attributes, and a group within a layer would be taken apart by upstream's selection (which removes elements from
  their layer while they are selected). A layer keeps the note's elements together through every upstream
  operation our app reuses (undo, erase, text editing, save and load, page copies) without any change to upstream's
  model, and "above the page's ink" is simply the layer order. Upstream's layer controller and its undo actions do
  the placing and deleting.

## The selected layer

Upstream tools (pen, eraser, selection, text) work on the page's selected layer. The rule: **the selected layer is
never a note**, except for the duration of an action on a note (a stroke, an erase, a text being typed on it).
Before any press on a page, a selected note layer is replaced by the topmost layer that is not a note (a new empty
layer below the notes if the page has none). This also covers files from Xournal++ (which selects the top layer)
and the undo of a placed or deleted note (upstream's layer undo selects the inserted layer). The layer panel does
not list notes.

## Where it is drawn

A note is drawn by `xqt::sticky::draw` in place of upstream's `LayerView::draw` for its layer (see "How a note is
drawn" below). The hook is one seam in `src/core/view/LayerView.{h,cpp}` (a function pointer the
frontend sets, like the Markdown renderer; ADR-0002). Every picture of a page goes through `LayerView`, so the note
is the same on the screen (`PageRaster`), in thumbnails and previews, in the PDF export, in print (the PDF export),
in the archive export (PDF/A) and in the hybrid PDF. Screen renders (and only they) add the folded corner of a
covered note and the peeking look.

In the **hybrid PDF** a note is its layer's annotation, flattened in the appearance stream like our ink: a `/Stamp`
(not `/Ink`, so viewers that redraw ink from `/InkList` do not draw the paper as a line) whose `/AP` is the note as
drawn here, clipped content included; its `/Rect` is the note and its shade (`sticky::drawnRect`). Never a `/Text`
popup note.

### How a note is drawn

The author's request (2026-09-26): notes looked flat; a slightly darker edge or a shade at the edge, like paper notes,
but only a shadow if it is really fast to draw. In this order, in page coordinates (`sticky::draw`):

1. **The shade**: a soft shadow a little to the bottom right (0.8 pt right, 1.4 pt down), fading out over about
   2.4 pt. No bitmap and no blur: three rectangles of black at 5.5 % opacity, each reaching 0.8 pt further out at
   the bottom and the right and beginning 0.8 pt nearer the bottom-left and top-right corners, so the shade is
   darkest (about 16 %) next to the paper and fades outwards and towards those corners. Only the strips that the
   paper does not hide are filled (a box at the right and one at the bottom per rectangle). Nothing is drawn above
   or left of the note.
2. **The paper**: its outline (the paper stroke's points) filled with its colour (at the stroke's fill opacity,
   255 in our files).
3. **The content**, clipped to the paper.
4. **The edge**: the paper's outline stroked 0.8 pt wide (`EDGE_WIDTH`) in the paper's colour times 0.78
   (`EDGE_SHADE`, `sticky::edgeColor`): an olive edge on yellow, a darker pink on pink, and so on; never black or
   gray. It lies over the content, as a paper's edge would.
5. On the screen only: the folded corner of a covering note, and the peeking look (the whole note, shade included,
   at a quarter opacity, and a dashed edge in a darker shade still).

The shade and the edge are a drawing effect of our renderer, the same on the screen, in thumbnails and previews, in
the PDF export, print, the archive and the hybrid PDF (vector paths there, no images). The file does not change:
the paper stroke is still saved with width 0.5 in the note's colour, and upstream Xournal++ draws it as it did (a
flat rectangle). A note's picture reaches `DRAWN_MARGIN` (4 pt) beyond its rectangle: what is drawn again when the
note moves, changes or peeks.

**Cost** (`StickyNoteTest.benchmarkTheLook`, `XQT_BENCH_STICKY=1`; a page with 1, 5 and 20 notes of 30 strokes
each, the machine loaded by other builds, so a few percent is noise):

| | 1 note | 5 notes | 20 notes |
| --- | --- | --- | --- |
| whole page at 2 px/pt, flat / edge / edge + shade (ms) | 2.70 / 2.70 / 2.76 | 11.1 / 11.0 / 11.2 | 42.6 / 42.3 / 43.6 |
| thumbnail at 0.25 px/pt | 0.99 / 0.99 / 0.99 | 4.78 / 4.77 / 4.83 | 19.1 / 19.0 / 19.1 |
| a stroke's area inside a note (writing on it) | 0.21 / 0.21 / 0.21 | 0.21 / 0.20 / 0.20 | 0.21 / 0.20 / 0.20 |

The edge costs nothing measurable. The shade costs 1–2.3 % of a whole page's render, under 1 % of a thumbnail, and
nothing while writing on a note: an area drawn again that lies inside the paper draws neither the shade nor the
edge. The first tries cost more: five rounded rectangles up to 9 %, three rectangles filled as even-odd rings
(cairo's slower path) 4–6 %; plain boxes are cairo's quickest fill.

## Undo

- Place, paste, delete and cut: upstream's `InsertLayerUndoAction` / `RemoveLayerUndoAction` (named after what was
  done).
- To another page by a drag: `sticky::NotePageUndoAction` (the page, the position among its layers and the look
  before and after).
- Move, resize, colour and cover: `sticky::NoteUndoAction` (the note's rectangle, colour and cover before and after;
  a change of the top left moves the content along).
- Writing and erasing on a note: upstream's stroke and eraser undo actions (they remember the layer).
- Peeking and hiding are view states, not undo steps.

## Code

- `qt/src/session/StickyNote.*`: the format (recognising a note, its look, making one), drawing, peeking, the undo
  actions, the selected-layer rule, the clipboard format (`serialize`, `deserialize`) and where a paste goes
  (`pastePlace`).
- `qt/src/canvas/StickyNotes.*`: the canvas side (selection with its outline and handle, moving, resizing, writing
  on a note, cover taps, copy / cut / paste and the picture for other apps, the drop on another page), used by
  `CanvasPage`, `CanvasInput` and `CanvasView` (`copySelection`, `cutSelection` and `pasteElements` hand a note to
  it).
- `qt/src/app/qml/NotePill.qml`: colours, cover, copy, cut, delete, at the note. `PageKeys.qml`: the sidebar's keys
  with a note. The menu entries and the page pill's eye in
  `Main.qml`; `AppController` (`insertStickyNote`, `noteSelected`, `noteColor`, `noteCovers`, `pageNotesHidden` ...).
- `LayersModel` leaves the notes out of the layer panel.
- Tests: `StickyNoteTest` (session: the format, the round trip, upstream Xournal++ opening the file, every export,
  the clipboard format, where a paste goes, the edge and the shade of every colour in every picture and nothing of
  them in the file; `XQT_STICKY_SHOTS=<dir>` saves the palette's pictures), `CanvasReplayTest` (`*StickyNote*`: writing on a note and the clipping
  on the screen, moving and resizing with the content and undo, cover mode and peeking, placing and deleting, copy
  and paste on another page, cut, the offset over the original, covering stays covering, paste into another
  document, the drag onto another page, the edge on the screen and while peeking, the note's area drawn again
  after a paste, a cut and its undo), `MainWindowTest.theShapesMenuPlacesAStickyNote…`,
  `MainWindowTest.aStickyNoteIsMovedToAnotherPageByCutAndPaste` (the pill's Copy and Cut, Ctrl+X on page 1 and
  Ctrl+V on page 3, the sidebar's keys).

## Not yet

- Showing a dragged note over the other page while it is dragged (it waits at its page's edge and jumps on
  release).
- Pasting a note into upstream Xournal++ (it does not know the note's clipboard format).
- A note selected in the second view (self-reference) has no pill of its own: Ctrl+C / X / V work there.
- Clipping the text box while it is typed (the text being typed shows beyond the note's edge until it is done).
- Markdown text boxes on notes (a Markdown box goes to the page's Markdown layer, below the notes).
- A multi-layer selection rectangle that reaches a note selects nothing on the note (a tap selects the note).
- Hiding the notes of the whole document at once (the eye in the page pill hides those of the current page).
