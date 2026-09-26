# ADR-0002: Upstream seams (list of edited upstream files)

- Status: Living document

The Qt build compiles upstream Xournal++ sources from `src/` unchanged wherever possible. There are three mechanisms, in order of preference.

## 1. Compatibility headers (no upstream edit)
`qt/compat/` sits in front of the upstream include paths of the Qt build targets (see `qt/cmake/XojCore.cmake`):

| Path | Purpose |
|------|---------|
| `qt/compat/gtkshim/gdk/gdk.h`, `gtk/gtk.h` | Plain GDK/GTK *types* with GDK 3 values: `GdkRGBA`, `GdkRectangle`, `GdkInputSource`, `GdkModifierType`, opaque handles, `GtkMessageType`, `GtkResponseType`. Also pure-cairo helpers ported from GDK 3 (`gdk_cairo_set_source_rgba/region/rectangle/pixbuf`), with pixel-identical premultiplication. Any real GTK call does not compile. |
| `qt/compat/include/util/XojMsgBox.h` + `qt/compat/XojMsgBox.cpp` | Replaces (shadows) upstream `util/XojMsgBox.h` with the same static API. Messages go to a pluggable `xoj::compat::MessageSink`: QML dialogs in the app, stderr in the CLI. |
| `qt/compat/VersionInfo.cpp` | GTK-free implementation of upstream `util/VersionInfo.h`. |
| `qt/compat/include/control/Control.h` | **Shadow of the GTK application object.** An abstract *per-document session* interface with the subset of upstream `Control` methods that reused code calls (same names and signatures). It derives from `DocumentHandler`, so the `fire*` events are upstream's. The Qt app implements it once per tab. |
| `qt/compat/include/gui/MainWindow.h`, `gui/XournalView.h`, `gui/XournalppCursor.h`, `control/ScrollHandler.h` | Abstract shadows of the GTK glue classes that reused code reaches through `Control` (for example `control->getWindow()->getXournal()->recreatePdfCache()`). |
| `qt/compat/DeviceId.cpp` | GDK-free implementation of upstream `gui/inputdevices/DeviceId.h` (device identity = address of the `QPointingDevice`). |
| `qt/compat/include/gui/PageView.h` | `XojPageView` as seen by the selection code (page, pixel position, selection color, re-render); implemented by `CanvasPage`. |
| `qt/compat/include/gui/Layout.h` | Page at a point, total size, visible rectangle, relative scroll (moving a selection across pages, edge scrolling); implemented by `CanvasView`. |
| `qt/compat/include/control/zoom/ZoomControl.h` | Zoom values and zoom listeners for reused tools (spline, selection), fed by the canvas' `ViewController`. |
| `qt/compat/gtkshim/gdk/gdkkeysyms.h` | GDK key symbol values used by reused tools. |
| `qt/compat/include/control/actions/ActionDatabase.h` | Abstract action sink with upstream's API (`enableAction`, `setActionState<T>`, `fireChangeActionState<T>`, `fireActivateAction`). States are a small `std::variant` instead of `GVariant`. |

Shadow headers keep the upstream names and method signatures. Upstream code that calls them compiles unmodified. If upstream starts using a new function, the Qt build fails to compile, and the missing function is added to the compat header.

## 2. Guarded seams in upstream files (`#ifdef XOJ_NO_GTK`, tagged `xournal-qt:`)
The Qt build defines `XOJ_NO_GTK=1`. The upstream GTK build is unaffected by these edits.

| File | Change | Why |
|------|--------|-----|
| `src/util/include/util/Util.h`, `src/util/Util.cpp` | `execInUiThread` goes through a pluggable `Util::setUiThreadDispatcher()`; the default is a GLib idle source. `paintBackgroundWhite` is guarded. | `gdk_threads_add_idle_full` / `GtkWidget` |
| `src/core/model/Document.h`, `.cpp` | PDF table of contents as `DocumentOutline` (plain C++) instead of a `GtkTreeStore`: `getOutline()` replaces `getContentsModel()`. `readPdfKeepingOutline()`: load a PDF with the same first pages (pasted pages joined the merged PDF) without reading the outline again. | `GtkTreeModel` |
| `src/core/pdf/base/XojCairoPdfExport.cpp` | `populatePdfOutline()` walks `DocumentOutline`, in the same traversal order as upstream. | `GtkTreeModel` |
| `src/core/model/PaperSize.h`, `.cpp` | The `GtkPaperSize` constructor and the `gui/PaperFormatUtils.h` include are guarded. | `GtkPaperSize`, `GtkComboBox` |
| `src/core/control/settings/Settings.h`, `.cpp` | The `GdkDevice*` overloads of `get/setDeviceClassForDevice` are guarded. | `gdk_device_get_*` |
| `src/util/PathUtil.cpp` | `CONFIG_FOLDER_NAME` comes from `XOJ_CONFIG_FOLDER_NAME` when defined (the Qt build uses `xournal-qt`), so settings, cache and autosaves never touch upstream Xournal++'s folders. | own config dir |
| `src/core/control/DeviceListHelper.h`, `.cpp` | GDK seat enumeration (`getDeviceList`, `getSourceMapping`, `InputDevice(GdkDevice*)`) is guarded. `InputDevice` stays. | `GdkSeat` |

## 3. Small upstreamable refactorings (valid for both builds)
| File | Change |
|------|--------|
| `src/core/control/jobs/ExportBackgroundType.h` (new) | `ExportBackgroundType` moved out of `BaseExportJob.h`, which drags in GTK. |
| `BaseExportJob.h`, `ExportHelper.h`, `ImageExport.h/.cpp`, `pdf/base/XojPdfExport.h`, `XojCairoPdfExport.h` | Include `ExportBackgroundType.h` instead of `BaseExportJob.h`. |
| `src/core/model/DocumentOutline.h` (new) | Toolkit-independent outline type, used by the Qt build. |
| `src/core/control/tools/PdfElemSelection.cpp` | The GTK primary-selection call is guarded (`#ifndef XOJ_NO_GTK`); the Qt canvas sets `QClipboard::Selection` itself. |
| `src/core/model/Element.cpp` | `getBoundingBox()` / `getSnappedBounds()` set `sizeCalculated` after `calcSize()`, not before. With several render threads (shared document lock) another thread otherwise used the empty bounds and culled the element (text missing in thumbnails). Sizes are also computed once after loading. |
| `src/core/model/DocumentListener.cpp` | `unregisterListener()` forgets the handler, so a second call (e.g. from the destructor, after the handler is gone) is harmless. Needed by listeners that follow the current tab (`PagesModel`). |
| `src/core/model/Document.h` | `setDocumentHandler()`: re-target document events from the `LoadHandler` to the session that owns the document, instead of copying documents with `operator=` (upstream's `replaceDocument`, which relies on `try_lock` on an already locked mutex). |
| `src/core/view/background/PdfBackgroundView.cpp` | The PDF cache is asked for `matrix.xx` pixels per page unit, not `matrix.xx * deviceScale`: its buffer is a surface similar to the target and applies the device scale itself. On a 2x screen the PDF was rendered at 4x the pixels needed (about 100 MB per cached page at fit width) and scaled down. Upstream has the same bug on HiDPI screens. Test: `PageRaster.aPdfBackgroundIsRenderedOnceForTheScreenScale`. |
| `src/core/model/MarkdownText.h` (new), `model/Text.h/.cpp`, `model/Layer.h/.cpp`, `view/TextView.cpp` | Markdown texts. A text knows it is one while it is in a layer named "Markdown" (`Text::isMarkdown`, a runtime flag, not saved: `Layer` sets it when an element is added or inserted, and on renaming; so loading, dropping a selection, pasting and undo all keep it right). With a frontend renderer and sizer registered (`xoj::markdown::renderer` / `sizer`, atomic function pointers; xournal-qt: `qt/src/markdown/MdBox.cpp`), `TextView` draws such a text formatted and `Text::calcSize` makes its bounding box the drawn box, so selections, hit tests, culling and repaints fit what is drawn. Without them (upstream) nothing changes: the source is shown as text. `xoj::markdown::layoutPainter` (same header) lets `TextView` draw a text box's Pango layout through the frontend (xournal-qt: colour emoji as sharp pictures in PDFs, `qt/src/markdown/EmojiFont.h`); unset, it is `pango_cairo_show_layout`. `xoj::markdown::classifier` (same header, an atomic function pointer asked by `Layer::markdownFlag` for texts of other layers) lets the frontend flag other texts as Markdown texts too (xournal-qt: a sticky note's text, `sticky::isNoteText`, qt/sticky-containers); unset, only the layer "Markdown" counts. |
| `src/core/model/NoteSpace.h` (new), `model/XojPage.h/.cpp`, `control/xojfile/XmlAttrs.h`, `XmlParser.cpp`, `DocumentBuilderInterface.h`, `LoadHandler.h/.cpp`, `SaveHandler.cpp`, `view/background/PdfBackgroundView.h/.cpp`, `BackgroundView.cpp`, `control/tools/PdfElemSelection.h/.cpp`, `pdf/base/XojCairoPdfExport.cpp`, `control/jobs/ImageExport.cpp`, `pdf/base/XojPdfExportFactory.cpp` | Space for notes beside slides ([note-space.md](../note-space.md)). A page holds a `NoteSpace` (left, top, right, bottom in points, included in its size), saved as the page attribute `notespace="l t r b"` only when not empty (the parser passes it to the builder through a new virtual with an empty default). The PDF background is drawn at (left, top): `PdfBackgroundView` (white paper around it; on pixels the offset is a whole number of them, so the cached picture stays sharp), the PDF and image exports. `PdfElemSelection` asks poppler in PDF coordinates and gives its bounds, rectangles and region in page coordinates. The export factory takes the cairo backend for a document with note space (upstream's qpdf backend lays the drawing over the PDF page's own box). Without note space (and in upstream) nothing changes; upstream ignores the attribute and draws the PDF at the top left of the larger page. |
| `src/core/view/LayerView.h`, `LayerView.cpp` | Sticky notes: a frontend may draw a layer itself (`xoj::view::layerDrawer`, an atomic function pointer; `LayerView::draw` asks it first and draws the layer as before when it declines). xournal-qt: `qt/src/session/StickyNote.cpp` draws a note's layer (the paper, then its content clipped to it) on the canvas, in thumbnails and in every export. Without one (upstream) nothing changes. |

## Ported, not reused (the upstream original is GTK-bound)
Each port records its upstream origin in a comment. Re-check them after upstream merges touch the originals.

| Fork file | Upstream origin |
|-----------|-----------------|
| `qt/src/render/PageRaster.*` | `control/jobs/RenderJob.cpp`; buffer handling of `gui/PageView.cpp` (`rerenderPage`, `rerenderRect`). Additions: fractional DPI (scaled template surface for `Mask`), and the PDF background rendered outside the document lock. |
| `qt/src/render/RenderService.*` | `control/jobs/Scheduler.cpp` / `XournalScheduler.cpp` (render part, including `blockRerenderZoom`). Several worker threads instead of one. |
| `qt/src/canvas/CanvasPage.*` | `gui/PageView.cpp` (`XojPageView`): `onButtonPressEvent/onMotionNotifyEvent/onButtonReleaseEvent/onSequenceCancelEvent` (pen/highlighter/whiteout, eraser), `drawAndDeleteToolView`, `elementChanged`, `paintPage` (buffer plus overlays, now per tile). |
| `qt/src/canvas/CanvasInput.*` | `gui/inputdevices/PenInputHandler.cpp` (`actionStart/Motion/End`, `filterPressure`, `inferPressureValue`, page crossing), `StylusInputHandler.cpp` (barrel buttons, `changeTool`), `MouseInputHandler.cpp`, `AbstractInputHandler::getInputDataRelativeToCurrentPage`. Touch navigation and palm rejection follow the M0 spike and Krita (ADR-0001). |
| `qt/src/canvas/CanvasView.*`, `DocumentLayout.*`, `ViewController.*` | `gui/XournalView.cpp` (page views, `cleanupBufferCache`), `gui/Layout.cpp` + `gui/LayoutMapper.cpp` (row-major columns, paired pages with offset, fixed pixel paddings), `control/zoom/ZoomControl.cpp` (anchored zoom, fit width, zoom limits). |
| `qt/src/session/DocumentSession.*` | `Control::openXoppFile/openPdfFile/createNewDocument/addDefaultPage/insertPage/deletePage/duplicatePage/movePageTowardsBeginning/movePageTowardsEnd/updatePageActions/resetSavedStatus/setLastAutosaveFile`, `SaveJob::save/updatePreview`, `AutosaveJob::run`, `PageBackgroundChangeController::insertNewPage/copyBackgroundFromOtherPage` (default branch). |
| `qt/src/session/PageOrderUndoAction.*` | New (upstream has only one-page `InsertDeletePageUndoAction` / `SwapUndoAction`): several pages deleted, pasted or moved in one step. Page structure operations use a second `UndoRedoHandler` in the session (`getPageUndoRedoHandler`), upstream's single-page actions included. |
| `qt/src/canvas/TextEditor.*` | `control/tools/TextEditor.cpp` (initializeEditionAt, finalizeEdition; editing itself rewritten for Qt keys and input methods, as upstream's uses GtkTextBuffer/GtkIMContext), `view/overlays/TextEditionView.cpp` (drawing). |
| `qt/src/shell/SessionRecovery.*` | `control/CrashHandler.cpp` (`emergencySave` for every tab), `XournalMain::checkForEmergencySave` (recovery offer at start). Unsaved documents autosave to `<pid>-<serial>.autosave.xopp` instead of upstream's `<pid>.xopp` (one file per tab). |
| `qt/src/shell/Thumbnails.*` | `control/jobs/PreviewJob.cpp` / `SaveJob::updatePreview` (PDF background rendered directly, then `DocumentView::drawPage`). |

## Not compiled in the Qt build
- `src/util`: `GtkUtil.cpp`, `gtk4_helper.cpp`, `gdk4_helper.cpp`, `XojMsgBox.cpp`, `VersionInfo.cpp` (replaced by `qt/compat`).
- `src/core`: everything not listed in `qt/cmake/XojSources.cmake`.
  - For M1 that includes all of `gui/`, except `LayoutMapper.cpp`, `GladeSearchpath.cpp` and `toolbarMenubar/model/ColorPalette.cpp`.
  - `undo/*`, `control/layer/*` and the tool layer (`xoj-tools`: `InputHandler`, `StrokeHandler`, `StrokeStabilizer`, `SnapToGridInputHandler`, `EraseHandler`, the stroke overlay views, `InputUtils`, `LegacyRedrawable`) **are** compiled, unmodified, against the shadow interfaces.
  - Not yet: selection, text, shapes, spline, laser, vertical space, geometry tools.

## Verification
- `qt/tests/golden/run_golden.sh`: `xournal-qt-cli` PNG and PDF export are pixel identical to upstream `xournalpp` built at the merge base, and `.xopp` round trips keep the document structure.
  - `golden-quick`, part of plain `ctest` (about 2 s): six representative fixtures (strokes, text, images, layers, attached PDF background) at 72 dpi.
  - `golden-full`, opt-in (several minutes): every fixture in `test/files` at 72 and 150 dpi. Run `ctest -C Full -L golden-full` for upstream merges (done by `qt/tools/merge-upstream.sh`) and milestone sign-off.
- `xoj-unit-tests`: the upstream unit tests (`test/unit_tests`, all suites except the GTK-only `ActionDatabaseTest`) run against the Qt-free core.
  - Also `qt/tests/unit/UndoRedoTest.cpp`: upstream undo actions and `UndoRedoHandler` driven through a test implementation of the shadow `Control`.
  - Also `qt/tests/unit/PageRasterTest.cpp`: the render service matches a direct upstream `DocumentView` render pixel for pixel. This covers full renders (several fixtures, zooms, DPR 1.25, PDF backgrounds), partial re-renders, and a concurrent edit/render stress test (also run under ThreadSanitizer).
- `xqt-session-tests`: `DocumentSession` (new, open, annotate PDF, save/save-as/backup, autosave, page operations, undo). Upstream `LayerController` runs through the session.
- `xqt-canvas-tests`, `xqt-quick-tests`, `xqt-shell-tests`: replayed pen/touch/touchpad input through the upstream tools, input routing in a real Qt Quick window (dialogs, scroll bars, moved canvas), tabs, single instance, page sidebar model and thumbnails.
- Plain `ctest` (also with `-j`) runs everything except `golden-full` in a few seconds. Upstream suites that write fixed temp files share a `RESOURCE_LOCK`.
- The upstream GTK build (`cmake -S . -B build-gtk`) must keep compiling with all seams applied.
