# Pen gestures

Gestures of the pen while it writes, as in GoodNotes, Notability, Apple Notes and Samsung Notes. They work with the
pen of a tablet or a 2-in-1 (not the mouse or a finger), and with the pen and the highlighter drawing freehand.
Code: `qt/src/canvas/PenGestures.{h,cpp}` (the stroke handler and the settings), `CanvasInput` (the pen at rest),
`CanvasPage::straightenStroke`, `qt/src/canvas/ScratchOut.{h,cpp}` (the zigzag and what it covers). Tests: `qt/tests/canvas/PenGesturesTest.cpp` (label `canvas`).

## Hold to straighten

Finish a stroke and keep the pen still on the screen for a moment (0.5 s; it may shake by a few pixels): the stroke
becomes the shape upstream's `ShapeRecognizer` sees in it, the same recogniser as the "Recognize shapes" variant of
the shapes button: a line (snapped to horizontal or vertical when close), a triangle, a rectangle, a circle or an
ellipse. Upstream's recogniser has no arrows. When it finds nothing, nothing happens and the pen writes on.

- The shape is in the document at once, while the pen still rests; lifting the pen ends the stroke, and what the pen
  does in between is not drawn.
- Undo is upstream's shape recognizer undo: the first undo brings the stroke back as it was drawn, the second removes
  it.
- The pen must have moved first: a press held still where it went down is the long press (the context menu).
- Settings → Pen → Gestures: "Hold to straighten" (on by default) and "Hold for" (0.25–2 s). In `settings.xml`:
  `holdToStraighten` and `holdToStraightenTime` (ms) in the `xournalQt` part.
- Upstream's settings apply: the minimum size of a shape (`strokeRecognizerMinSize`) and snapping recognised shapes to
  the grid.

How it works: the pen's moves restart nothing; `CanvasInput` keeps where and when the pen last moved further than
6 view pixels, and a timer looks at that when the time is up. Then the page's `GestureStrokeHandler` (upstream's
`StrokeHandler`, unmodified, with one more method) finishes the stroke and replaces it the way upstream's shape
recognizer mode does on release (`InsertUndoAction`, then `RecognizerUndoAction`).

## Scratch out to erase (opt-in)

Scribble quickly back and forth over handwriting, as when crossing out on paper: the strokes the zigzag covers are
deleted, and the zigzag is not kept. One undo step brings them back (upstream's `DeleteUndoAction`, "Erase stroke").
Off by default: Settings → Pen → Gestures → "Scratch out to erase" (`scratchOut` in the `xournalQt` part).

- Only the pen (not the highlighter), only strokes: typed text, pictures, Markdown boxes, sticky notes and the rest
  stay. On a sticky note it erases in the note (the layer the pen writes in).
- A zigzag over nothing, or drawn slowly, is a stroke like any other.

What counts as a zigzag (`scratchout::analyse`, on the stroke's points in page coordinates):
- its main axis (principal component) within 40° of the horizontal, at least 8 pt (3 mm) long;
- at least 3 turns, each going back at least 40 % of its length along that axis;
- every sweep between two turns within 30° of the axis and nearly straight (path ≤ 1.3 × the distance along the
  axis; loops like a cursive "eee" are 1.5 and more), the median sweep across at least 55 % of the length;
- drawn at 200 view pixels a second or faster on average (from the pen's timestamps; unknown counts as fast).

What it covers (`scratchout::covered`): a stroke of the same layer that the zigzag crosses or touches, with at least
60 % of its points inside the zigzag's band (its extent along and across the axis, a little wider). A long line
crossed at its end stays.

Handwriting moves on along the line: "mmm" and "www" turn up and down, not back, and digits such as "3" or "8" have a
vertical main axis and slanted, curved sweeps. No stroke of the handwriting fixture
(`test/files/benchmark/handwritten-text.xopp`, 13 064 strokes, at most 3 turns) passes the shape test alone
(`PenGesturesTest.noStrokeOfTheHandwritingFixtureIsAScratchOut`), and the zigzag must also cover ink.
