# ADR-0005: Memory and background threads have owners

- Status: **Accepted.** Recorded 2026-10-08, distilled from [architecture/image-caches.md](../architecture/image-caches.md),
  the header of `CanvasMemory.h`, [ADR 0003](0003-app-services.md) and the shell review of 2026-10
  ([review/2026-10/shell.md](../review/2026-10/shell.md), §5 and §6).

## Context
The app is fast because it caches ([VISION.md](../../../VISION.md)): rendered pages, sketches, thumbnails, stand-ins,
covers, pages with search hits, annotation pictures, snippet cards. Each cache had grown its own limit (a fraction of
a setting, a constant in its file, a count of documents) and its own thread pool or the global one. Nobody could say
how much memory the app might take, and shutdown stopped only some of the pools: a sketch could still be written as a
JPEG through Qt's image plugin while the application was taking the plugins away.

## Decision
- **`CanvasMemory`** (`qt/src/canvas`) owns the memory for rendered pages: one limit (the setting `canvasMemory`,
  default a quarter of the RAM), shared by every view of every window. It plans which pages each document keeps and
  which are rendered ahead (the document read last first, the visible pages always kept, pages ahead of the reader
  before those behind).
- **`ImageMemory`** (`qt/src/shell`) owns the limits of all image caches, in one table: the page previews follow the
  setting `previewMemory` (three quarters thumbnails, a quarter sketches), the stand-ins a tenth of `CanvasMemory`,
  the others fixed limits sized for what one view of the library shows. Within its limit a cache drops what was used
  least recently.
- **`ImageWorkers`** (`qt/src/shell`) owns the thread pools of the image providers, one per kind of work, sized in one
  table, all at idle priority. A picture QML no longer wants is not drawn. `ImageWorkers::shutdown()` drops what is
  queued and waits for what runs before Qt's plugins, cairo and poppler go away.
- **`BackgroundJobs`** (`qt/src/app`, owned by `AppServices`) owns the rest of the app's work off the UI thread;
  `RenderService` (`qt/src/render`) owns the canvas's render workers.
- The rule for new code ([AGENTS.md](../../../AGENTS.md), "What the moving parts assume"): **a cache gets an owner and a
  limit; a background job gets a pool with an owner that shutdown stops**, at idle priority unless the reader waits
  for it.

## Consequences
- The app's memory is two settings plus a table of fixed limits; the canvas never competes with the previews for its
  pages, and the canvas's own renders come before every image worker.
- Quitting is quick and safe while pictures are still being drawn (tests: `ImageWorkers.*`,
  `Sketches.nothingIsDrawnOrStoredAfterShutdown`).
- A new kind of picture means a row in `ImageMemory` and one in `ImageWorkers`, not a new constant.
- Not all of it is there yet: some jobs of the library and the canvas still start on `QThreadPool::globalInstance()`
  (the library's imports, moves and shares, cover pruning, the version list, the PDF cache's eviction), which
  shutdown does not wait for.

## Considered
- A limit and a pool per cache, each in its own file (how it was before): met the rule "a cache has a limit" in the
  letter only; no one could see the whole, and shutdown missed pools.
- The global thread pool: no owner, no priority per kind of work, and no way to stop it at shutdown before the
  plugins go.
