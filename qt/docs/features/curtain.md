# The curtain and the spotlight

For teaching and presenting: a black sheet over part of the page, like a sheet of paper on an overhead projector. The
audience sees only what is not covered; the next step of a calculation, the answer of an exercise stays hidden until
the curtain is moved.

The **spotlight** is its inverse: everything is black (the page, the other pages, the space around them: the whole
canvas) but a rectangle with rounded corners, the part the class is to look at. Only one of the two is out at a time.

## Putting it out

- **B** (Settings → Shortcuts: "Curtain"), again B takes it away (also the spotlight). **Shift+B**: the spotlight
  (again: away).
- The setsquare button (the rail, the top bar), held (or right-clicked): **Curtain (B)** and **Spotlight (Shift+B)** in its
  list. A tap on the button still goes from the setsquare to the compass: they are not tools of their own, they lie
  over the page whatever tool is in hand.
- ⋮ → View → **Curtain (B)**, **Spotlight (Shift+B)**.
- A button of its own for each, on the rail or the top bar: on neither at a first start, **"+"** (the catalog) offers
  them under View (`curtainButton`, `spotlightButton`; checked while it is out). Testers found no button for them
  (xournalpp/xournalpp#7770).
- Full screen and presenting: the setsquare button of the floating toolbox, held.
- On a phone: My tools → Setsquare, compass and curtain.

The curtain comes over the lower half of the part of the page in view, down to the bottom of the page and a little
beyond its sides; the spotlight's hole in the middle of the part of the page in view (six tenths of its width, three
tenths of its height); both with their handles shown. A pill at the top right of the canvas (below the setsquare's)
shows or hides the handles, puts out the other one instead, and takes it away (×).

## Moving, turning and sizing it

Like the setsquare (the spotlight by its hole: its handles are on the hole's edges, the black around it is what one
takes hold of):
- **A tap on the black** shows its handles (pen, mouse or finger). **Esc**, a tap beside it or the pill hides them.
- While the handles are shown: a **drag on the black** moves it; the **corners** and the **middles of the edges** size
  it (the opposite side stays where it is); the **knob** above its top edge turns it (it stays straight within 4° of a
  right angle). They look like a selection's handles: white discs in a ring of the selection color, the knob red with
  a turning arrow. **Two fingers** on the black carry it, turn it and size it (as the setsquare: only once the fingers
  clearly turn or spread).
- While they are hidden, a finger on the black scrolls the page as usual, and the hand tool too. Only **close to an
  edge** (within the reach of a handle: 14 pixels, 24 for a finger) a drag on the black **pushes that edge**: the
  curtain is pulled back to reveal the next line, the spotlight's hole made wider, without showing the handles first.
  For the spotlight that is the black just outside the hole: inside it, close to its edge, the pen still writes.
- Taken away and put out again, it **comes back where it was** (the curtain and the spotlight each, on the page the
  view is at), for as long as the tab is open.

## How dark it is

Settings → Pen → Presenting → **Darkness of the curtain and the spotlight**: 100 % (black, the default) down to 20 %
(the setting `curtainOpacity` of ours, `CurtainLayer::opacitySetting`). Below 100 % the page shows through it dimmed,
for the audience and the one writing alike; nothing is written on it either way. The handles are not dimmed.

## What it does not do

- **Nothing is written on it.** Pen, eraser, select tools, a text box, a link: a press that starts on the black does
  nothing (only its handles act, and a tap shows them). No invisible ink, nothing erased that one cannot see, no link
  followed under it; the mouse shows no link target under it either. A stroke that starts beside it may run under it.
- The spotlight's hole is the page as usual: written, erased, selected in as always.
- **It is not part of the document.** It belongs to the view (each tab has its own) and is drawn only on the screen, by
  the canvas: it is never saved, printed or exported, and never in the thumbnails, previews or the page grid. It is gone
  when the tab is closed.

## Where it lies

On a page, in the page's coordinates: it scrolls and zooms with what it covers. When the view goes to another page (the
next slide while presenting, scrolling on, a page chosen in the sidebar) it goes along, to the same place of that page:
flipping through a presentation keeps each page covered as far. When its page is deleted it goes onto the page the view
is at.

## How it is built

- `qt/src/canvas/CurtainLayer.*`: the curtain of a view (CanvasView::curtain()): where it lies, its handles, the hit tests
  and the drags; two fingers as in GeometryToolLayer.
- `qt/src/canvas/CanvasInput.cpp`: a press on it goes to it before anything else (actionStart; for touch at the start
  of a touch).
- `qt/src/quick/DocumentCanvasItem.cpp` (CurtainNode): black rectangles under a transform of their own, over the pages,
  the selection and the setsquare (only the pen's hover dot is above it), cut at the canvas' edges; the black under an
  opacity node (the setting); its handles and knob as small pictures like the selection's (made once for each pixel
  ratio and selection color, on whole device pixels). The spotlight is four black rectangles around its hole, reaching past the farthest corner
  of the canvas, and four small pictures of its rounded corners (made once, 128 pixels, scaled on the GPU). Moving,
  turning or sizing it changes only that node: nothing is drawn on the CPU, no page tile is composed again.
- `qt/src/app/qml/CurtainPill.qml`, `AppController::toggleCurtain`, `curtain`, `curtainHandles`.

Tests: `CurtainTest.*` (canvas: placing, input, handles, fingers, pages, the spotlight) and `CurtainCanvasTest.*`
(quick: the window's picture, the spotlight black all around, no page drawn again while it moves, nothing in the
page's own picture, `theCoverIsAsDarkAsTheSettingSays`); `ToolboxTest.theCatalogOffersTheCurtainAndTheSpotlight`.

## On the device

What only a real device, screen or another app can show; walked before a release from the [device checklist](../testing/device-checklist.md).

- [ ] The handles and the knob look like a selection's and are as easy to take with a finger and the pen.
- [ ] The darkness at 50 % on a projector: the class sees the page dimmed, the black still hides at 100 %.
