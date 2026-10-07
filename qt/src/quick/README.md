# quick: the canvas in Qt Quick

Target `xqt-quick` (`qt/cmake/XqtApp.cmake`). The bridge between a `CanvasView` (canvas) and the Qt Quick scene graph,
plus the window-wide helpers the QML needs from C++ (`import XournalQt.Canvas`).

| Class | Job |
| --- | --- |
| `DocumentCanvasItem` | the `QQuickItem` that shows one `CanvasView`: page tiles in the scene graph, and an event filter on the window that takes tablet, touch, mouse, wheel and gesture events inside the canvas (all canvas input goes through C++) |
| `DarkTileMaterial` (+ `shaders/`) | dark pages on the GPU: a tile's texture looked up in the dark table, pictures kept |
| `AdaptiveLayout` | the window's size class and touch profile (`win.adaptive`), one place instead of thresholds in every QML file |
| `TouchGestures` | four- and five-finger gestures for the whole window |
| `EmojiNames` | emoji for the QML UI (`Emoji` singleton) |
| `InputLog` | `XQT_LOG_INPUT=1`: what the pen, touch and mouse send |

**May depend on**: `xqt-canvas` and below, Qt Quick. Not on shell or app.

**Threads**: the UI thread and Qt Quick's render thread (`updatePaintNode`), which only composes tiles the workers
rendered.

**Tests**: `qt/tests/quick` (label `quick`). **Docs**: [adaptive layout](../../docs/features/adaptive-layout.md),
[dark pages](../../docs/features/dark-pages.md), [hidpi](../../docs/features/hidpi.md),
[canvas rotation](../../docs/features/canvas-rotation.md), [the pointer](../../docs/features/hover-cursors.md).
