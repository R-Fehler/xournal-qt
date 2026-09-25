# Sticky notes

Status: built in `qt/sticky-notes` (2026-09-26). The author's request (2026-09-25): permanent sticky notes that the
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
  handle at the bottom-right corner, and a pill with its colours, "Cover" and delete. A drag on the note moves it
  right away (pen, mouse or finger); the handle resizes it (with any tool). A note stays on its page and inside it.
- **Colours:** five pastel presets (yellow, pink, blue, green, orange). A new note is yellow.
- **Placing:** "Sticky note" in the shapes menu (next to the setsquare and the compass: things put on the page) and
  "Insert sticky note" in the More menu (next to "Insert image…"). The note goes in the middle of the visible part of
  the current page, selected, so that it can be moved and resized right away.

## Cover mode (self-testing)

A note can be switched to **cover** (the pill's "Cover" button). A covered note:
- shows a folded corner (on the screen only), so it can be told from a note to write on;
- is not written on: the pen, the highlighter, the eraser and the text tool leave it alone (nothing is drawn under
  it either: a stroke that starts on it is not made);
- **peeks** when tapped (pen, finger, mouse, the hand): it turns see-through (a quarter opaque, with a dashed edge),
  so the answer under it can be checked. Another tap covers again. Peeking is a view state: not saved, not undoable,
  not in exports, thumbnails or other windows; a document opens with its notes covered.

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

A note is drawn by `xqt::sticky::draw` in place of upstream's `LayerView::draw` for its layer: the paper, then the
content clipped to the paper. The hook is one seam in `src/core/view/LayerView.{h,cpp}` (a function pointer the
frontend sets, like the Markdown renderer; ADR-0002). Every picture of a page goes through `LayerView`, so the note
is the same on the screen (`PageRaster`), in thumbnails and previews, in the PDF export, in print (the PDF export),
in the archive export (PDF/A) and in the hybrid PDF. Screen renders (and only they) add the folded corner of a
covered note and the peeking look.

In the **hybrid PDF** a note is its layer's annotation, flattened in the appearance stream like our ink: a `/Stamp`
(not `/Ink`, so viewers that redraw ink from `/InkList` do not draw the paper as a line) whose `/AP` is the note as
drawn here, clipped content included; its `/Rect` is the note. Never a `/Text` popup note.

## Undo

- Place and delete: upstream's `InsertLayerUndoAction` / `RemoveLayerUndoAction`.
- Move, resize, colour and cover: `sticky::NoteUndoAction` (the note's rectangle, colour and cover before and after;
  a change of the top left moves the content along).
- Writing and erasing on a note: upstream's stroke and eraser undo actions (they remember the layer).
- Peeking and hiding are view states, not undo steps.

## Code

- `qt/src/session/StickyNote.*`: the format (recognising a note, its look, making one), drawing, peeking, the undo
  action, the selected-layer rule.
- `qt/src/canvas/StickyNotes.*`: the canvas side (selection with its outline and handle, moving, resizing, writing
  on a note, cover taps), used by `CanvasPage`, `CanvasInput` and `CanvasView`.
- `qt/src/app/qml/NotePill.qml`: colours, cover, delete. The menu entries in `Main.qml`.

## Not yet

- Moving a note to another page (it stays on its page; copy and paste of a note neither).
- Clipping the text box while it is typed (the text being typed shows beyond the note's edge until it is done).
- Markdown text boxes on notes (a Markdown box goes to the page's Markdown layer, below the notes).
- A multi-layer selection rectangle that reaches a note selects nothing on the note (a tap selects the note).
