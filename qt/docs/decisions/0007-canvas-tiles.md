# ADR-0007: Pages reach the screen as texture tiles of the scene graph

- Status: **Accepted.** Recorded 2026-10-08, distilled from the header of `qt/src/quick/DocumentCanvasItem.h`,
  [features/hidpi.md](../features/hidpi.md), [features/dark-pages.md](../features/dark-pages.md) and
  [ADR 0001](0001-ui-host.md) (which chose Qt Quick with a tiled item in the spike).

## Context
Pages are drawn with cairo through upstream's view code, so ink looks exactly as in Xournal++, on worker threads into
a page raster (`PageRaster`) at a zoom and a device pixel ratio. Qt Quick shows pictures as GPU textures. Uploading a
whole page raster for every change (a stroke being drawn is a change every few milliseconds) costs megabytes per
frame; a page bigger than the GPU's texture limit cannot be one texture at all; and scrolling must stay smooth while
pages are still being rendered.

## Decision
- **Each visible page is a transform node with texture tiles** of 256 buffer pixels (`DocumentCanvasItem`), composed
  from the page raster and the overlay views (the stroke being drawn). Only dirty tiles are composed and uploaded
  again.
- **A budget per frame**: while the view moves, at most 12 tiles are composed and uploaded per frame (about 0.26 MB
  each), when it stands still 64. The rest of a page shows its stand-in and follows in the next frames.
- **Zooming scales the tiles on the GPU** until the page has been rendered at the new zoom; `CanvasMemory` decides
  which pages are rendered again ([ADR 0005](0005-memory-owners.md)).
- **Pixel for pixel**: a page's top left is snapped to a device pixel and a tile is `256 / dpr` logical pixels, so the
  tiles meet the screen's pixels at fractional scales and on a canvas turned by a quarter.
- **What only changes the look is done on the GPU**: dark pages are a material of the tiles (a lookup table in a
  shader), turning the canvas is the root node's matrix, the setsquare, the compass and the curtain are nodes of their
  own under a transform. None of these renders a page again. The software renderer, which has no shaders, gets the same
  table applied on the CPU when a tile is composed.
- The render thread (`updatePaintNode`) only composes what the workers rendered; it never draws a page.

## Consequences
- A stroke being drawn uploads a few tiles per frame, not the page; scrolling a long PDF stays smooth while renders
  catch up (stand-ins show first).
- Turning dark pages on and off, and turning the canvas, are instant.
- Tests can count what a frame did (`mostTilesInAFrame`, `frameStats`) and compare a tile with the window's pixels
  (`pageTilesAreShownPixelForPixel`).

## Considered
- One texture per page: too much upload per stroke, and impossible beyond the texture limit at high zoom.
- A raster `QWidget` as the host (the other half of the spike, ADR 0001): as good for input on the device, but no
  way to mobile.
- Drawing strokes with the GPU (a scene graph of geometry): ink would no longer be cairo's, so not identical to
  Xournal++, and PDFs need a raster anyway.
