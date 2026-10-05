# Pen gestures

Gestures of the pen while it writes, as in GoodNotes, Notability, Apple Notes and Samsung Notes. They work with the
pen of a tablet or a 2-in-1 (not the mouse or a finger), and with the pen and the highlighter drawing freehand.
Code: `qt/src/canvas/PenGestures.{h,cpp}` (the stroke handler and the settings), `CanvasInput` (the pen at rest),
`CanvasPage::straightenStroke`. Tests: `qt/tests/canvas/PenGesturesTest.cpp` (label `canvas`).

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
