# Turning the canvas

The canvas turns like Krita's: the view of the pages, not the pages themselves (turning a page for good is
[page-rotation.md](page-rotation.md)). The author: "rotate canvas only (like krita does for drawing) … it should use
the rotate gesture and reset with double tap or fit to X buttons on the layout pill." The device steps are in
[testing/device-checklist.md](../testing/device-checklist.md), "Pen" and "Touch and palm rejection".

## Using it

| How | What |
| --- | --- |
| Two fingers twist | The canvas turns with them once they turned about 12° (so pinching and scrolling never turn it by accident), behind the fingers by those 12° so that nothing jumps. Within 6° of 0°, 90°, 180° or 270° it snaps there. The point between the fingers stays under them while it turns and zooms. |
| Touchpad rotate gesture | The same (`Qt::RotateNativeGesture`: macOS, Wayland with libinput's pinch-rotate). |
| Ctrl+] / Ctrl+[ | A quarter turn clockwise / counter-clockwise (from a free angle: to the next quarter that way). Changeable in Settings → Shortcuts. |
| The chip "↺ 37°" in the layout pill | Shown while the canvas is turned (-90° rather than 270°); a tap turns it upright. |
| Two taps on the page (the middle button too) | Turned: upright again, the point tapped staying where it is; nothing else (the next two taps zoom as before). |
| Fit the width / height / whole page (the zoom button's menu, Ctrl+0, a double tap on the zoom) | Upright again, then the fit. "Real size (Ctrl+1)" keeps the angle: it is a zoom, not a fit. |
| Settings → Touch → "Turn the canvas with two fingers" | Off: the fingers and the touchpad never turn it; the keys and the chip still work. |

Not turned (and turned upright when it starts): presenting (F5), a text file (`.md`, `.txt`) and a text document of
notes (their text runs across the screen), and the reference beside the notes (`DocumentCanvas.rotatable: false`).
Reading (⋮ → View → Read), the curtain, the snip tool, the setsquare and the compass work turned; see "What works
turned" below.

## How it is built: an upright virtual view

`ViewController` keeps an angle θ (degrees clockwise, [0, 360)). Everything that worked in view coordinates keeps
working in an **upright virtual view** whose size is the bounding box of the turned screen
(`|w cos θ| + |h sin θ|` × `|w sin θ| + |h cos θ|`): the layout, the scroll position and its clamping, anchors,
fits, snapping, the visible pages, `pageViewRect`, every tool's page coordinates. Only the edge of the canvas maps:

- `screenToView` / `viewToScreen` (one 2×2 matrix and a translation per point, exact at multiples of 90°, and the
  identity, untouched by rounding, while upright), `screenDeltaToView` for deltas, `viewToScreen(QRectF)` (the
  bounding box), `viewToScreenEnds` (a rectangle that stands for two points: the ends of a PDF text selection).
- `setRotation(θ, anchor)` keeps the document point under the anchor (the middle, a tap, the fingers) where it is on
  the screen.

**Drawing.** `DocumentCanvasItem`'s root node carries `translate(itemCentre) · rotate(θ) · translate(−viewCentre)`
(the identity while upright). The pages, tiles, search hits, selection, setsquare and curtain are drawn in the
virtual view as before; the GPU turns the composed tiles. Page rasters stay axis-aligned, so turning costs no render.
At multiples of 90° the root's translation is put on a whole device pixel, so the tiles still land pixel for pixel on
the screen (as [hidpi.md](hidpi.md) wants); at free angles the GPU's linear filtering smooths them. The pointer the
canvas draws itself (the hover dot, a big eraser) is an item of its own in screen coordinates, upright.

**Input.** `DocumentCanvasItem::eventFilter` maps every position through the inverse before `CanvasInput` sees it:
pen, mouse, touch, wheel, native gestures, hovering (links, the width handle of a Markdown box), the formula error
under the mouse, the mouse's place for a paste. The wheel's and the touchpad's scroll deltas turn too, so the pages
move the way the wheel or the fingers go on the screen. Touch points keep their screen position and are mapped anew
at each event (the canvas may turn during the gesture); the pinch is anchored in screen coordinates. One matrix per
event: the pen's latency does not change.

**What QML gets.** Positions and rectangles the window shows things at are mapped forward: `contextRequested`,
`linkTapped`, `pdfTextSelected`, `pdfSelectionEnds` (both points), `pdfSelectionBox`, `noteBox`, `noteTextHint`,
`mathErrorRect`, the input method's cursor rectangle (the on-screen keyboard, the emoji suggestions), and what QML
hands back (`pasteAt`, `selectPdfTextAt`, `dragPdfSelection`) is mapped back. Rectangles become their bounding box on
the screen, so the pills sit beside the turned thing.

**Scroll bars.** `contentWidth/Height/X/Y` and `scrollTo` are the screen's: at 90° and 270° the bar at the right
scrolls the virtual view sideways (the other way round where the axis is reversed), at 180° both run backwards; at a
free angle there are no scroll bars (no content size).

**Culling.** A tile is composed only when it meets the turned screen: its rectangle against the virtual view and its
bounding box on the screen against the screen (the separating axes of two rectangles). Pages wholly in a corner of the
bounding box are still rendered (`visiblePages` is the bounding box's), but none of their tiles is composed.

**The setsquare's readout** (the angle display) is turned back by θ, so its number stays upright on the screen.

## What works turned

- The curtain: it lives in page coordinates; its handles are drawn turned with it; the spotlight reaches the corners
  of the virtual view.
- The snip: dragged in page coordinates, so a rectangle snip on a turned canvas is a rectangle of the page (turned on
  the screen).
- Text boxes and Markdown boxes on a page can be written in (the text runs along the page); the on-screen keyboard
  gets the cursor's place on the screen.

## Limits and ideas

- Edge scrolling while dragging a selection uses the virtual view's edges; at free angles those are beyond the
  screen's corners.
- A page wholly in a corner of the virtual view is rendered though not seen (memory and a render, no tiles).
- Pills next to a selection sit at its bounding box on the screen, not along the turned edge.
- The PDF text selection's knobs are where the ends are; their "drop" shape is not turned.
- The angle is per view and not stored with the document or the tab.
