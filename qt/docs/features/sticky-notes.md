# Sticky notes

Permanent notes on a page that the user writes on and moves around, with the ink and text on them staying attached:
not a PDF popup note that other viewers minimise; usable for self-testing by moving them over solutions; adaptable in
size. The author on containers: "if I put text, ink, a pasted image, a shape, whatever into the sticky note, I want it
to be movable as a containerised unit. When starting a Markdown text box, it should just use the sticky note's extent
as the frame."

## What a note is

A sticky note is an opaque, coloured rectangle on a page, with its own content: strokes, texts and pictures on it,
clipped to it. It lies above the page's ink.

- **Moving** a note moves its content along.
- **Resizing** (a handle at its bottom-right corner) changes only the paper: the content keeps its size and place
  (relative to the note's top left) and is clipped to the note. Scaling would stretch handwriting and text, and the
  usual reason to resize a note is to cover more (or less) of a solution. Content outside the paper is kept and shows
  again when the note is made larger.
- **Writing on it:** a press with the pen, the highlighter, a shape tool or the text tool on a note writes on the
  note (the topmost note under the pen). The eraser erases on the note. Strokes that go beyond its edge are kept whole
  and clipped where they are drawn.
- **Selecting:** a tap with a select tool (rectangle, lasso, object) on a note selects the whole note: an outline, a
  round handle at the bottom-right corner, and a pill above the note (`NotePill.qml`) with its colours, "Cover",
  "Text", "Image…", copy, cut, delete and deselect (Ctrl+C, Ctrl+X, Del and Esc work too). A drag on the selected note
  moves it (pen, mouse, or a finger); the handle resizes it (with any tool). A rectangle or lasso drawn on a note that
  is not selected selects what is on it ("Selecting in a note"); the object select tool, and any select tool on a
  covering note, take the whole note at the press, and a drag moves it right away. While dragged a note stays inside
  its page; let go over another page, it goes there. Choosing a tool that is not a select tool ends the selection, so
  that the pen then writes on the note.
- **Colours:** five pastel presets (yellow, pink, blue, green, orange). A new note is yellow.
- **Placing:** the toolbox's **Sticky note** entry ("+" → Sticky note; [toolbox.md](toolbox.md)). The note goes in the
  middle of the visible part of the current page, selected, and the rectangle select tool is chosen (as for an
  inserted image), so that it can be moved and resized right away.

## Copy, cut, paste; moving to another page

- **Copy / cut:** the pill's Copy and Cut, Ctrl+C / Ctrl+X, and Copy / Cut in the long-press (right-click) pill act
  on the selected note: the whole note goes onto the clipboard (its place, size, colour, cover, and everything on
  it). Cut removes the note's layer as one undo step ("Cut sticky note").
- **Paste:** Ctrl+V, and Paste in the long-press pill (and in the pill on PDF text), put a copied note on the
  current page of the view pasted in: the page in view (the page pressed, for the long-press pill), the second
  view's own page when pasting there (self-reference, while it is written in: `CanvasView::actingScope`). Another
  tab or another window works the same (the system clipboard). The note goes where it was on its old page when it
  fits; else it is moved inside the page (and made as large as the page only if it is larger). If it would lie
  exactly on a note of that page (a paste on the original's page, a second paste), it goes 16 points further down
  and right each time (up and left in the bottom right corner). It is placed on top of the page's layers, selected,
  as one undo step ("Paste sticky note"). A covering note stays covering; peeking is not copied.
- **Keys that must not go elsewhere:** with a note selected, Ctrl+C/X/V are not taken by a text being written
  (`DocumentCanvasItem`), there is no selection of elements at the same time (selecting one ends the other), and in
  the page sidebar or grid (a page clicked there has the keys) Ctrl+C/X copy or cut the note, not the pages. Ctrl+V
  there pastes a copied note onto the current page when the note was copied after the last page copy (or a note is
  selected, or no pages are copied); else the pages (`AppController::pastesNoteBeforePages`; `PageKeys.qml`).
- **The clipboard format** is the app's own, `application/x-xournal-qt-sticky-note`: the layer's name (cover or
  not) and every element of the layer, the paper first, each in upstream's element serialization (the one of its
  `application/xournal` clipboard) in a stream of its own (upstream's `ObjectInputStream::readData` copies the
  stream's whole buffer for every stroke it reads, so one stream for many strokes is quadratic). After each element
  its group number ([groups.md](groups.md)). The object is named `StickyNote3`. A picture of the note (PNG, twice the
  page resolution, drawn as in an export: no folded corner, no shade) is offered too, so pasting into another app
  gives a picture; it is drawn only when an app asks for it (`NoteMimeData`). Xournal++ does not read the note's
  format; the note is not also put there as upstream's elements, since pasting those here would give loose elements
  instead of a note.
- A note coming onto a page or leaving it (place, paste, cut, delete, the drop on another page, and their undo) draws
  only its part of the page again (`sticky::NoteLayerChange`, read by `CanvasView::layerChanged`).
- **Moving to another page:** cut, go to the page, paste. Or drag the selected note past its page's edge and let go
  over another page: it goes there, the point it was held by under the pointer (inside that page), on top, still
  selected. While dragged it stays at the edge of its page (a page's picture cannot show it beyond the page); it
  jumps on release. The drag and the change of page are one undo step ("Move sticky note to another page",
  `sticky::NotePageUndoAction`).

## Notes as containers

**One Markdown text per note** (the note is its text frame), and **a rectangle or lasso drawn inside a note selects
its contents**; a tap selects the whole note.

### What goes into a note
Everything started inside a note that shows and can be written on (not hidden, not covering) goes into it; the
topmost note at the point counts. A covering note takes nothing (and nothing goes under it through it: a paste or an
image there goes onto the page, below it).
- **Ink, shapes, plain text** (the pen, the highlighter, the shape tools).
- **The note's Markdown text**: a tap with the text tool anywhere on the note, or the pill's **Text** button. See
  below.
- **Paste** (Ctrl+V, the pills' Paste) of elements, a picture or plain text goes into a note when a note is selected,
  or when the paste point lies on a note: the pointer (Ctrl+V with the mouse over the note), the place pressed (the
  long-press pill) or, without either, the middle of the visible part of the page. With a note selected the pasted
  things go to the middle of the note. They are pasted selected and stay in the note when the selection ends. A
  copied link (a link marker, a Markdown text of its own) and a copied note are not put into a note.
- **Insert image** (the image button, the pill's **Image…**): into the selected note, or into the note in the middle
  of the visible part of the page; fitted into the note (at most 80 % of its width and height, never enlarged) and
  centred on it.
- **Pictures and links dropped** on Markdown being written go into that text; while the note's text is written, that
  is the note's text.
- One undo step each (a paste or an inserted image is one step, wherever it goes).

### The note's Markdown text
- It is an ordinary Markdown text (a Xournal++ text element whose text is the Markdown source) in the **note's
  layer**, at the note's top left plus a padding of 10 points on each side: its wrap width is the note's width minus
  twice the padding. It is written on the page (formatted while typing, with the formatting bar) like the other
  Markdown texts.
- **One per note**: a tap with the text tool anywhere on the note edits the same text; the cursor goes where the tap
  was on the text (at its end when the tap is below it). The pill's **Text** starts it, or edits it with the cursor
  at its end. Ink written first stays where it is: the text is drawn over it (the text is added after what is there).
- **Resizing the note** changes the text's wrap width: the text flows again. Ink and pictures keep their size and
  place. The width follows the note in the undo of a resize too.
- What goes beyond the note's bottom is clipped. On the screen a small triangle in the paper's edge colour at the
  bottom right says that there is more below.
- **While it is written** the editor draws the text over the page and clips it, its frame and the cursor to the
  note's paper, with the triangle when the text goes on below. When the cursor is below the note's bottom, a small
  label below the note says "The text is longer than the note: make the note bigger" (`CanvasView::noteTextHintBox`,
  the canvas's `noteTextHint`, `noteTextHint` in `Main.qml`). It is a label, not a dialog: typing goes on. The note is
  not made bigger by itself: a note covering a solution must not grow over the page.
- It moves, is copied, cut, pasted and dragged to another page with the note. It is **not** taken by a rectangle or
  lasso inside the note (it is the note's frame, like the paper).
- **How the text is told apart** (the file has no attribute of our own): in a note's layer, the text whose top left is
  the note's top left plus the padding (within half a point) and which has a wrap width is the note's Markdown text.
  Any other text on a note is a plain text. Upstream's model flags Markdown texts by their layer (`Text::isMarkdown`,
  set by `Layer`); a small seam lets the frontend flag other texts too (`xoj::markdown::classifier` in
  `model/MarkdownText.h`, set by `sticky::installDrawer`), so the note's text is drawn formatted everywhere a page is
  drawn (the canvas, thumbnails, the exports, the hybrid PDF). Without the frontend (upstream Xournal++) it is a text
  that wraps at the note's width and shows its source.
- **Where Markdown boxes are**: "a layer that holds Markdown boxes" is the page's Markdown layer or a note's layer
  (`md::holdsBoxes`), and a page's boxes are the texts flagged as Markdown in those (`md::boxesOf`). Editing, hit tests
  (links, check boxes, formulas' errors, the "Load image" button), the search, the annotations panel (the note's text
  as its caption), the chapters, the pictures carried in the file ([md-images.md](md-images.md)) and the exports see
  the note's text. The page's own text (at the page's margins, flowing over pages: `pageBoxOf`, pagination) is the
  page's Markdown layer's only.

### Selecting in a note
- **A tap** with a select tool on a note selects the whole note.
- **A rectangle or a lasso started inside a note** (on its paper, the note not selected) selects the note's elements
  inside it: never its paper, never its Markdown text. A drag started on an unselected note therefore does not move
  it: tap it first, then drag it. (A covering note is the exception: its content cannot be changed, so a drag on it
  moves it right away.)
- **The object select tool** always takes the whole note.
- **Moving the selection**: where it ends decides where the elements go. When a move ends, the note that shows (not
  covering) under the middle of the selection takes them; no note there: the page (the layer that was selected
  before). So elements dragged within their note stay in it, dragged out of it onto the page they leave it, dragged
  from the page (or from another note) onto a note they join it. The move and the change of layer are one undo step
  ("Move into sticky note", "Move out of sticky note", "Move to another sticky note"). A move to another page goes
  into the note under it there, or onto that page. Resizing and rotating the selection never change its layer. A
  selection of the page's Markdown boxes is not put into a note.
- A rectangle or lasso that starts outside every note selects the page's elements (the page's layer; a multi-layer
  tool skips notes), and the notes it encloses whole ("Several notes at once").
- While a selection of a note's elements lives, the note is the page's selected layer; when it ends, the layer
  selected before is again.

## Several notes at once

The author: "Selecting multiple notes would be good, either with Ctrl during select-clicking or with the rectangle
selection tool etc., so we can select them together with other annotations to copy-paste somewhere else."

### What selects them
- **Ctrl + click** (the mouse; the pen with Ctrl held; Shift works too) with a select tool on a note adds it to what
  is selected on that page, or takes it away when it is selected. On an element of the page (the selected layer's,
  or a Markdown text of the page; a multi-layer tool: any layer that is no note) the same, while notes are selected.
  With elements selected Ctrl + click on a note makes one selection of them and the note. Without notes selected,
  Ctrl + click on elements adds them as Shift + click does upstream.
- **A rectangle or lasso started beside the notes** selects the notes it encloses whole and the page's elements in it
  (the selected layer, else the page's Markdown texts; a multi-layer tool: the topmost layer with elements in it that
  is no note). Ctrl or Shift: added to what is selected.
- **A rectangle or lasso started inside a note** selects that note's elements; **a tap** on a note selects that note
  alone; the object select tool takes one note. One note alone is always the note's own selection (its outline, its
  handle, its pill); elements alone are an ordinary selection. Only two or more notes, or notes with elements, make
  the selection described here.
- **Touch: "Select more"** in the selection's pill (next section): the touch way of Ctrl + click.

### Select more

The author: "once I clicked on an annotation or sticky note I want a button in the selection pill to appear that when
clicked enables multiselect mode. Then I can add selections by touching the items and remove by touching again, and
the pill keeps track of the number of items and offers the usual copy, cut, paste etc. options".

- **The button**: with the rectangle or lasso select tool (also the multi-layer ones) the pills of a selection show
  "Select more" (a dashed selection with a plus, `xqt-select-more`): the selection's pill (elements, notes selected
  together) and the note's pill (one note). It is highlighted while on; a tap on it again turns it off. It is greyed
  for a selection of elements inside a note, and not shown with the other tools or in a view for reading only. The
  selection's pill shows **how many** notes and elements are selected (a note counts as one); the note's pill shows
  "1" while select more is on.
- **While it is on**, a tap (finger, pen or mouse) on a note or an element adds it to the selection, or takes it away
  when it is selected (`CanvasView::toggleSelected`, through the same `selectTogether`). What a tap finds
  (`CanvasView::toggleAt`): a selected element, else the topmost layer with a note or an element there: a note's
  paper takes the whole note; elements of the selected layer, of the page's Markdown texts and of the layers of what
  is selected (a multi-layer tool: of any layer that is no note), within 5 points as a tap selects.
  - A finger that draws (touch drawing): a tap is a tap of the tool. A finger that scrolls: a tap toggles too (a drag
    scrolls).
  - **A tap on empty paper does nothing**: the selection stays (missing a thin stroke must not lose a selection of
    many things).
  - **A drag on the selection moves it** (one undo step). It begins once the pointer went further than a tap does
    (8 pixels for the pen, 16 for a finger).
  - **A rectangle or lasso started beside the selection adds what it encloses**, as with Ctrl. It never selects inside
    a note while select more is on.
  - A tap on the selected note's handle still resizes it.
- **Taking the last one away** ends the selection and select more.
- **It ends** when the selection ends any other way (Deselect, Esc, Delete, Cut, an undo), when the tool changes
  (even rectangle → lasso), when a tap or rectangle goes to another page (what is tapped there is selected alone),
  when the document shown changes, and with the button.
- **Undo**: adding and taking away are no undo steps; copy, cut, paste, delete and the moves are.
- **The view beside (self-reference)** works the same while it is written in (its edit switch). For reading only it
  keeps selecting to copy: the selection's pill and the note's pill offer copy and deselect, no "Select more".

### What it does
- It is drawn over its page: an outline around each note, a thin box around each element (up to 200), a dashed box
  around all of it. The **selection's pill** shows (not the note's): copy, cut, paste, delete, deselect. Colour, cover
  and size stay per note.
- **Moving**: a drag in its box moves everything together; it stays on its page while dragged. One undo step ("Move
  selection"). Let go over another page, it all goes there (the point held under the pointer, moved inside that page,
  the layout kept): the notes on top of that page's layers in their order, the elements into its own layer (Markdown
  texts into its Markdown layer); one undo step ("Move selection to another page").
- **Delete**, **Cut**: one undo step each.
- **Copy**: the clipboard format `application/x-xournal-qt-selection` (`sticky::GROUP_CLIPBOARD_MIME`): its bounds,
  each note as a copied note is, each element in a stream of its own with whether it was a Markdown text of its page.
  A picture of it all (PNG at twice the page's resolution) is drawn only when another app asks for it.
- **Paste** (Ctrl+V, the pills, the page sidebar's Ctrl+V): onto the current page of the view pasted in, in the same
  layout: where it was when it fits, else moved inside the page as a whole (never made smaller); moved 16 points on
  while one of its notes would lie exactly on a note of that page. The notes go on top of the page's layers, the
  elements into the page's own layer (Markdown texts into its Markdown layer, made if needed), all selected together;
  one undo step ("Paste").
- In the page sidebar with notes selected together, Ctrl+C / Ctrl+X take them, not the pages.

### Why a selection of its own
Upstream's `EditSelection` takes the selected elements out of their one layer while they are selected. A note is a
layer of its own: taken apart into an EditSelection it would stop being a note, and an EditSelection has one source
layer, so notes (several layers) and page ink cannot be in one. So `MixedSelection` (`qt/src/canvas`) holds note
layers and elements where they are, each note a unit: a drag moves each element (`Element::move`) and each note
(`sticky::applyLook`) directly and draws only that area again. Every change is one undo step composed of upstream's
actions and ours, in `sticky::UndoSteps`, undone in reverse order. `CanvasView::selectTogether` decides what a set of
notes and elements becomes (one note: `StickyNotes`; elements of one layer: an EditSelection; else a
`MixedSelection`). An undo that takes away one of its notes or elements takes it out of the selection
(`MixedSelection::validate`, on every change of the undo stack and of a page's layers).

## Cover mode (self-testing)

A note can be switched to **cover** (the pill's "Cover" button). A covering note:
- shows a folded corner (on the screen only), so it can be told from a note to write on;
- is not written on: the pen, the highlighter, the eraser and the text tool leave it alone (a stroke that starts on
  it is not made);
- **peeks** when tapped (pen, finger, mouse, the hand): it turns see-through (a quarter opaque, with a dashed edge),
  so the answer under it can be checked. Another tap covers again. Peeking is a view state of the open document: not
  saved, not undoable, not in exports or thumbnails; a document opens with its notes covered. With a select tool a
  tap selects the note instead.

**Hide / show the notes of a page**: a button in the page pill (`ViewPill.qml`), shown on pages with notes. Like the
eye in the layer panel it is not saved in the file (Xournal++ does not save layer visibility), and a hidden note is
left out of exports, as hidden layers are.

## File format (Xournal++ compatible)

A note is **a layer of its own**, named `Sticky note` (covering: `Sticky note (cover)`). The layer's first element is
the **paper**: a closed rectangle stroke (tool pen, width 0.5, the note's colour, `fill="255"`, so opaque). The other
elements of the layer are the note's content, in their order; the note's Markdown text is a text element with a wrap
width there. A new note goes on top of the page's layers.

```xml
<layer name="Sticky note">
  <stroke tool="pen" color="#fff59dff" width="0.5" fill="255">100 100 300 100 300 240 100 240 100 100</stroke>
  <stroke tool="pen" color="#000000ff" width="1.41" pressures="…">…</stroke>
  <text font="Sans" size="12" x="110" y="110" color="#000000ff">Answer?</text>
</layer>
```

- The note's rectangle is the bounding box of the paper's points; its colour is the paper's colour; cover mode is the
  layer's name. No attributes of our own; a layer name and the order of elements are kept by both programs.
- A layer named like a note whose first element is not a filled stroke is an ordinary layer.
- **In Xournal++** the file opens without errors and looks right: the filled rectangle hides what is below it, and the
  content is drawn on it. What differs there: the content is not clipped; cover mode is only a layer name (no
  peeking); each note is a layer in its layer list, and Xournal++ selects the top layer when it opens a file, so what
  is drawn then goes onto a note. Deleting or moving the paper alone there, or merging layers, turns the note into
  ordinary ink.
- **Why a layer** and not a group of elements inside a layer: upstream's selection takes elements out of their layer
  while they are selected. A layer keeps the note's elements together through every upstream operation the app
  reuses (undo, erase, text editing, save and load, page copies) without any change to upstream's model, and "above
  the page's ink" is simply the layer order.

## The selected layer

Upstream tools work on the page's selected layer. The rule: **the selected layer is never a note**, except for the
duration of an action on a note (a stroke, an erase, a text being typed on it). Before any press on a page, a
selected note layer is replaced by the topmost layer that is not a note (a new empty layer below the notes if the
page has none). This covers files from Xournal++ (which selects the top layer) and the undo of a placed or deleted
note. The layer panel does not list notes (`LayersModel`).

## Drawing

A note is drawn by `xqt::sticky::draw` in place of upstream's `LayerView::draw` for its layer: a function pointer the
frontend sets in `src/core/view/LayerView.{h,cpp}` (a seam, [ADR 0002](../decisions/0002-upstream-seams.md)). Every
picture of a page goes through `LayerView`, so the note is the same on the screen (`PageRaster`), in thumbnails and
previews, in the PDF export and print, in the archive PDF and in the hybrid PDF. Screen renders (and only they) add
the folded corner of a covering note and the peeking look.

In the **hybrid PDF** a note is its layer's annotation, flattened in the appearance stream like the ink: a `/Stamp`
(not `/Ink`, so viewers that redraw ink from `/InkList` do not draw the paper as a line) whose `/AP` is the note as
drawn here, clipped content included; its `/Rect` is the note and its shade (`sticky::drawnRect`). Never a `/Text`
popup note.

In this order, in page coordinates (`sticky::draw`):
1. **The shade**: a soft shadow a little to the bottom right (0.8 pt right, 1.4 pt down), fading out over about
   2.4 pt: three rectangles of black at 5.5 % opacity, each reaching 0.8 pt further out at the bottom and the right,
   only the strips the paper does not hide. No bitmap and no blur: plain boxes are cairo's quickest fill, and the
   shade costs about 2 % of a page's render and nothing while writing on a note (an area inside the paper draws
   neither shade nor edge).
2. **The paper**: its outline filled with its colour.
3. **The content**, clipped to the paper.
4. **The edge**: the paper's outline stroked 0.8 pt wide (`EDGE_WIDTH`) in the paper's colour times 0.78
   (`EDGE_SHADE`, `sticky::edgeColor`): an olive edge on yellow, a darker pink on pink; never black or grey.
5. On the screen only: the folded corner of a covering note, and the peeking look.

The shade and the edge are a drawing effect of the renderer, the same everywhere (vector paths in PDFs). The file does
not change: the paper stroke is saved with width 0.5 in the note's colour, and upstream Xournal++ draws a flat
rectangle. A note's picture reaches `DRAWN_MARGIN` (4 pt) beyond its rectangle: what is drawn again when the note
moves, changes or peeks.

## Undo

- Place, paste, delete and cut: upstream's `InsertLayerUndoAction` / `RemoveLayerUndoAction`.
- To another page by a drag: `sticky::NotePageUndoAction` (the page, the position among its layers and the look
  before and after).
- Move, resize, colour and cover: `sticky::NoteUndoAction` (a change of the top left moves the content along).
- Writing and erasing on a note, the note's Markdown text, pasting or inserting into a note: upstream's actions and the
  Markdown edit's step (they remember the layer).
- Elements dragged into a note, out of it or to another note on the same page: `sticky::ContentMoveUndoAction`. Onto
  another page: upstream's move.
- Several notes (with elements) moved, moved to another page, deleted, cut, pasted: `sticky::UndoSteps`.
- Peeking and hiding are view states, not undo steps.

## Code

- `qt/src/session/StickyNote.*`: the format (recognising a note, its look, making one), drawing, peeking, the undo
  actions, the selected-layer rule, the clipboard formats (`serialize`, `deserialize`, `serializeGroup`,
  `deserializeGroup`) and where a paste goes (`pastePlace`, `groupPastePlace`); the note's Markdown text (`textOf`,
  `isNoteText`, `textOrigin`, `textWidth`, the "more below" mark), the note that takes content (`openNoteAt`),
  `holdLayer`, `ContentMoveUndoAction`, `UndoSteps`.
- `qt/src/canvas/StickyNotes.*`: the canvas side (selection with its outline and handle, moving, resizing, writing on
  a note, cover taps, copy / cut / paste and the picture for other apps, the drop on another page), used by
  `CanvasPage`, `CanvasInput` and `CanvasView`.
- `qt/src/canvas/MixedSelection.*`: several notes and elements selected together.
- Containers: `md::holdsBoxes` / `md::boxesOf` (`MdBox`); `MarkdownSession::pageOf`; `CanvasView::startText`,
  `writeNoteText`, `pasteElements`, `pasteText`, `insertImage`, `noteTarget`, `noteSelectionMade`,
  `endSelectionDrag`; `CanvasPage::selectInNote`; `DocumentCanvasItem` (where the mouse rests, for Ctrl+V).
- Select more and several notes: `CanvasView::selectTogether`, `toggleSelected`, `takeSelected`, `toggleAt`,
  `offersSelectMore`, `canSelectMore`, `setSelectingMore`, `selectedCount`; `CanvasPage::selectNotesAndElements`,
  `onButtonPressEvent` / `onButtonReleaseEvent`; `CanvasInput` (`toggleOnTap`).
- The window: `CanvasActions` (`app.edit`, `app.reference.edit`: the note's and the selection's pill act through it;
  [reference-view.md](reference-view.md)), `NotePill.qml`, `SelectionPill.qml`, `PageKeys.qml` (the sidebar's keys),
  `ToolboxMenus.qml` (the catalog's entry), `ViewPill.qml` (the page pill's eye), `AppController`
  (`insertStickyNote`, `noteSelected`, `notesSelectedTogether`, `pageNotesHidden`, …).

## Tests

- `StickyNoteTest` (session): the format, the round trip, upstream Xournal++ opening the file
  (`upstreamXournalppOpensTheFileAndShowsTheNotes`), every export, the clipboard format, where a paste goes, the edge
  and the shade in every picture and nothing of them in the file (`XQT_STICKY_SHOTS=<dir>` saves the pictures),
  `aNotesMarkdownTextIsToldByItsPlaceAndFlowsInTheNotesWidth`; `benchmarkTheLook` (`XQT_BENCH_STICKY=1`).
- `CanvasReplayTest` (canvas, `*StickyNote*`): writing on a note and the clipping, moving and resizing with undo,
  cover mode and peeking, placing and deleting, copy and paste, the drag onto another page;
  `aStickyNoteHoldsOneMarkdownTextThatFlowsInItsWidth`, `pastedAndInsertedThingsGoIntoTheStickyNoteThere`,
  `aRectangleInAStickyNoteSelectsItsElementsThatLeaveAndJoinItByADrag`,
  `aNotesTextIsClippedToTheNoteWhileItIsWrittenAndAHintSaysWhenTheCursorIsBelow`; `benchmarkStickyNoteClipboard`
  (`XQT_BENCH_STICKY=1`).
- `StickySelectTest` (canvas): Ctrl + click, a rectangle beside the notes, delete, copy and paste, the drag onto
  another page of notes with ink. `SelectMoreTest` (canvas): taps with a finger, the pen and the mouse.
- `AnnotationsTest.aStickyNotesMarkdownTextIsItsCaptionAsShown` (shell).
- UI: `MainWindowTest.theToolboxHasAStickyNote`, `theStickyNoteButtonPlacesANoteWithItsPill`,
  `aStickyNoteIsMovedToAnotherPageByCutAndPaste`, `theNotePillWritesTheNotesTextAndPutsAnImageOnIt`,
  `thePillsOfASelectionOfferSelectMoreAndCountWhatIsSelected`, `severalStickyNotesSelectedTogetherHaveTheSelectionsPill`;
  `ReferenceWindowTest.selectingInTheSameDocumentBesideItselfWorksAsOnTheNotes`.

## Not yet

- Showing a dragged note over the other page while it is dragged (it waits at its page's edge and jumps on release).
- Pasting a note into upstream Xournal++ (it does not know the note's clipboard format).
- Clipping a plain text box while it is typed on a note: it shows beyond the note's edge until it is done.
- A rectangle that starts beside a note and reaches only part of it selects nothing on the note.
- A selection of elements from inside several notes, or from inside a note and the page, at once.
- Several notes selected together: no colour or cover for all of them at once, no resize.
- Select more: no long press for it (the finger's long press opens the context pill); a lasso or rectangle that takes
  away what it encloses; select more in a view for reading only.
- Pictures dropped on a note that is not being written.
- Scrolling the note's text (what goes below the note shows when the note is made larger).
- Hiding the notes of the whole document at once (the eye hides those of the current page).
