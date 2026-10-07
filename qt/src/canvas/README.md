# canvas: a document in a view

Target `xqt-canvas` (`qt/cmake/XqtSession.cmake`). A `DocumentSession` shown in a view, the input on it, and the tools
and editors that work on the page. Qt Gui, but no Qt Quick: the scene graph side is `quick`.

| Class | Job |
| --- | --- |
| `CanvasView` | a document session shown in a canvas (one per tab, a second one for the reference or the presenter's audience): visible pages, rendering, selection, clipboard, text editing, links, PDF text |
| `CanvasPage` | one page as a view shows it (a port of upstream's `XojPageView`, behind the shadow `gui/PageView.h`): the tools' dispatch |
| `CanvasInput` | input of a canvas: pen, touch, mouse; palm rejection, gestures, momentum (a port of upstream's input handlers) |
| `DocumentLayout`, `ViewController` | where pages lie (columns, pairs, sideways; upstream's `Layout`); zoom and scroll with anchored pinch and momentum |
| `Clock` | the time the canvas goes by (input timing, animations), injectable in tests |
| `CanvasMemory` | the memory for rendered pages, shared by every view of every window (a setting) |
| `TextEditor`, `MarkdownEditor`, `CanvasTextInput`, `MarkdownSession`, `MarkdownBoxResize`, `FindReplace`, `EmojiCompletion` | typing on the page: text boxes, Markdown on the page and beside it, find and replace |
| `MarkdownFile`, `MarkdownImages`, `MarkdownBookmarks`, `MdImageDecoder`, `ImageFile` | `.md`/text files and image files as documents, their pictures and bookmarks |
| `StickyNotes`, `MixedSelection`, `CanvasGroups`, `CanvasStickers`, `Snip`, `TodoStamp`, `CurtainLayer`, `GeometryToolLayer`, `GeometryToolPicture`, `LaserPointerHandler` | tools and selections on the page: sticky notes, several things selected together, groups, stickers, snip, the to-do stamp, curtain, setsquare and compass, the laser pointer |
| `PenGestures`, `ScratchOut`, `PenHover`, `HoverPointer` | pen gestures, the hover height, the pointer over the page |
| `DarkPages`, `PagePictures`, `PageResize`, `PageRotate` | dark pages; page size and rotation changes |
| `TimelineReplay`, `AudienceRegion`, `ScrollLock`, `ScreenCalibration`, `Perf` | the replay, the presenter's audience screen, locked scrolling, calibration, `XQT_PERF` counters |

**May depend on**: `xqt-session`, `xoj-tools` and below, Qt Gui. From upstream (unmodified, through `qt/compat`'s
shadows of `Control`, `XojPageView`, `Layout`, `ZoomControl`, `XournalppCursor`): the input handlers and tools
(`control/tools`: `StrokeHandler`, `EraseHandler`, `EditSelection`, …), `control/shaperecognizer`, the overlay views
(`view/overlays`, `SetsquareView`, `CompassView`), undo, `control/layer`, `control/settings`. Not on quick, hwr, shell
or app.

**Threads**: the UI thread, plus `RenderService`'s workers drawing for it; they hold the view's PDF cache as a
`std::shared_ptr` taken under `pdfCacheMutex`, so the UI thread can replace it meanwhile.

**Known debt** (review 2026-10, [infra.md](../../docs/review/2026-10/infra.md) §1): `CanvasView` is a god object and
`CanvasInput` has a long `touchEvent`; non-view code (`MarkdownFile`, `PageResize`, `DarkPages`, `ImageFile`, …) moves
out in later rounds (TODO.md).

**Tests**: `qt/tests/canvas` (label `canvas`; `CanvasReplayTest` replays input). **Docs**: the feature docs named in
each header, e.g. [pen gestures](../../docs/features/pen-gestures.md), [sticky notes](../../docs/features/sticky-notes.md),
[canvas rotation](../../docs/features/canvas-rotation.md).
