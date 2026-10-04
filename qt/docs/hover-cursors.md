# The pointer over the page

What the mouse and the hovering pen show over the canvas, and why it is built the way it is
(`qt/src/canvas/HoverPointer.*`, `DocumentCanvasItem::refreshPointer`).

## Why the crosshair was faster than the dot

Until 2026-10-04 the canvas showed two things for a hovering pen:

- **The crosshair** was a cursor of the platform (`setCursor(Qt::CrossCursor)` on the canvas item). The app never
  draws a cursor. The compositor (KWin, Mutter, the X server, Windows' DWM) puts it on the hardware cursor plane, a
  small overlay that the display controller lays over the screen's picture at scan-out. It moves it as soon as it has
  the pen's position, before the app even sees the event, and it does not wait for the app's next frame.
- **The dot** was a 6 × 6 rectangle in the canvas's own scene graph (`QSGSimpleRectNode`). Every move of the pen went
  this way: the compositor passes the tablet event to the app; the canvas called `update()` on the whole item; at the
  next vsync the render thread ran the canvas's `updatePaintNode` (every visible page, its tiles, search hits, the
  setsquare, the selection), drew the whole window and swapped. The compositor then showed that buffer in its next
  frame. So the dot was at least one frame of the app plus one of the compositor behind the cursor: two to three
  frames, 30 to 50 ms at 60 Hz. It fell further behind whenever the app's frame had other work, for example tiles to
  compose right after a stroke.

The cursor moves at no cost to the app. The dot cost a frame of the canvas for each move, and it always came late.

## What it is now

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
- **A pen without a cursor of the platform** (Android, iOS): the canvas still draws the dot, now as cheaply as it can.
  The dot is a small item of its own over the pages (`HoverMarkItem`, a `QQuickPaintedItem` painted once). Following
  the pen only moves it: the scene graph gets a new position, the canvas's `updatePaintNode` does not run, and no tile
  is composed or uploaded (`CanvasItemInputTest.aPenWithoutACursorOfThePlatformGetsADrawnDotThatDrawsNoPage`). What
  remains: Qt Quick draws the whole window again for each move (the pages' textures are drawn, not made anew), and
  the dot is still a frame or two behind the pen tip. Only a cursor of the platform avoids that. Qt 6 does not offer
  Android's pointer icon for the stylus.
