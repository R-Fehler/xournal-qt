# render: pages drawn by worker threads

Target `xoj-render` (`qt/cmake/XojCore.cmake`). **Qt-free**: no moc, no Qt types, so the CLI and the tests can use it.
It turns a page of upstream's model into pixels with cairo, as upstream's GTK view did, on worker threads.

| Class | Job |
| --- | --- |
| `PageRaster` | the CPU raster of one page as one view shows it: a port of upstream's `RenderJob` and `XojPageView` buffer (partial re-render, the stale buffer scaled while zooming, a windowed buffer for big pages) |
| `RenderService` | the worker pool that renders rasters (replaces upstream's single-threaded scheduler): priorities, a raster never rendered by two workers at once |
| `RegionRender` / `RegionImage` | a picture of an area of a page as the screen shows it (snip, stickers, the to-dos' handwriting); `RegionImage.h` wraps it as a `QImage` for the Qt layers (header only) |
| `ElementFilter` | which elements a drawing shows (the timeline's replay), set per drawing thread |
| `PaperTexture` | the grain of textured paper, through upstream's `backgroundDecorator` hook |

**May depend on**: `xoj-core` only. From upstream: `view/DocumentView`, `view/LayerView`, `view/background/*`,
`view/Mask`, the model (`model/*`), `control/PdfCache` and `pdf/base` (poppler); cairo. Nothing of `qt/src`.

**Threads**: cairo and Pango objects are per thread (`thread_local`); the document is read under `std::shared_lock`;
the PDF is drawn outside the document lock.

**Tests**: `qt/tests/unit` (`PageRasterTest`, `RegionRenderTest`; label `unit`). Pixel identity with upstream: the
golden tests (`qt/tests/golden`). **Docs**: [performance logging](../../docs/development/performance-logging.md),
[hidpi](../../docs/features/hidpi.md), [ADR 0002](../../docs/decisions/0002-upstream-seams.md) ("Ported, not reused").
