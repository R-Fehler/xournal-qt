# The pointer over the page

What the mouse and the hovering pen show over the canvas, and why it is built the way it is
(`qt/src/canvas/HoverPointer.*`, `DocumentCanvasItem::refreshPointer`).

## Why the pointer is a cursor of the platform

A pointer drawn by the app comes late; a cursor of the platform does not:

- **A cursor of the platform** (`setCursor(…)` on the canvas item) is never drawn by the app. The compositor (KWin, Mutter, the X server, Windows' DWM) puts it on the hardware cursor plane, a
  small overlay that the display controller lays over the screen's picture at scan-out. It moves it as soon as it has
  the pen's position, before the app even sees the event, and it does not wait for the app's next frame.
- **A dot in the canvas's scene graph** goes this way for every move of the pen: the compositor passes the tablet
  event to the app; the canvas updates; at the next vsync the render thread draws the window and swaps; the compositor
  shows that buffer in its next frame. So such a dot is at least one frame of the app plus one of the compositor
  behind the cursor: two to three frames, 30 to 50 ms at 60 Hz, more whenever the app's frame has other work (tiles to
  compose right after a stroke).

## The pointers

- **The tool's pointer is a cursor of its own**: a small dot by default (a dark core with a light ring, so it shows on
  white and on dark paper), drawn for the screen's device pixel ratio (`hover::dotImage`) and set as a `QCursor` on
  the canvas. It takes the same path as the crosshair, so it is just as fast. **Settings → Pen → Pointer over the
  page**: "Dot" (the default) or "Crosshair" (`hoverPointer` in the `xournalQt` part of the settings). The special
  cursors stay as they were: the pointing hand on a link that a click follows, and the arrows on the width handle of
  a Markdown box.
- **The hovering pen**, where the platform shows a cursor for it (`hover::platformShowsPenCursor`): X11 (the pen
  moves the pointer), Windows Ink, macOS, and Wayland's tablet protocol from Qt 6.7 on. There the pen shows the
  window's cursor. Qt Quick sets the window's cursor from the item under the *mouse*, and the pen's events go to the
  canvas before Qt Quick sees them. So the window could still have the arrow of a control the mouse was last over.
  While the pen hovers the canvas, the canvas makes the window's cursor its own, and gives the old one back when the
  pen leaves or the mouse moves (`showCursorForPen`, `giveBackWindowCursor`). `XQT_PEN_CURSOR=0` or `1` overrides the
  guess, to check on a device.
- **A pen without a cursor of the platform** (Android, iOS): the canvas draws the dot, as cheaply as it can.
  The dot is a small item of its own over the pages (`HoverMarkItem`, its picture a small texture made once). Following
  the pen only moves it: the scene graph gets a new position, the canvas's `updatePaintNode` does not run, and no tile
  is composed or uploaded (`CanvasItemInputTest.aPenWithoutACursorOfThePlatformGetsADrawnDotThatDrawsNoPage`). What
  remains: Qt Quick draws the whole window again for each move (the pages' textures are drawn, not made anew), and
  the dot is still a frame or two behind the pen tip. Only a cursor of the platform avoids that. Qt 6 does not offer
  Android's pointer icon for the stylus.

## The eraser shows what it will erase

While a press would erase, the pointer is the eraser itself (`hover::eraserMark`): the tool bar's eraser, a side
button set to erase while it is held, and the pen's eraser end while it hovers. The tool changes only when the eraser
end touches, so its preview comes from the eraser button's setting and the tool bar eraser's size and kind. The
outline is gray, with a faint gray fill and a light halo so that it shows on dark paper. Its size is the eraser's real
size at the current zoom, and it changes with the zoom and the eraser's size:

| Eraser (Settings → Pen → Eraser) | Shape | Size |
| --- | --- | --- |
| Standard (cuts strokes) | square, solid line | 2 × the eraser's width a side (upstream's `EraseHandler`: a box reaching the width from the pointer) |
| Whole strokes | the same square, **dashed** line | the same |
| Whiteout | circle, solid line | the eraser's width across (a white stroke that wide, round) |

Upstream's cursor is a square for every kind. Whiteout is round here because its ink is a round brush.

The preview is a cursor whenever it fits: at most 256 device pixels a side (`hover::MAX_CURSOR_PX`, a size every
platform takes). A bigger eraser (a large own width at a high zoom) keeps the dot (or the crosshair) as the cursor in
its middle, and the canvas draws the outline around it. The outline is geometry, not a picture, so no texture is
needed however big it is, and following the pointer only moves it. A pen without a cursor of the platform gets the
same drawn outline at its eraser end. An outline smaller than 5 px is drawn at 5 px, so that it stays
visible. No preview is shown where nothing is erased: a document open only for reading, or a text file being edited.

Known limits:

- With Qt's software renderer (no GPU), the drawn outline of an oversized eraser is not shown. The cursors are.
- A size or kind of its own for the eraser button, set in Xournal++'s settings, shows once the eraser end has
  touched. Before that, the tool bar eraser's size and kind are shown. This app's settings always use the tool bar
  eraser's.
