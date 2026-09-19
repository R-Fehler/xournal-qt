# xournal-qt roadmap

## Status (2026-09-19)
- **M0 fork and spike: done, except the on-device evaluation.**
  - The fork has upstream history. The build root is `qt/CMakeLists.txt`.
  - `qt/spikes/inkpad` (quick and widget hosts) still needs to be run on the device to decide [ADR-0001](adr/0001-ui-host.md).
- **M1 Qt-free core: done.**
  - `xoj-util` and `xoj-core` build without GTK; see [ADR-0002](adr/0002-upstream-seams.md).
  - `xournal-qt-cli` output is pixel identical to upstream `xournalpp` for PNG and PDF export on all 49 fixtures (golden tests), and round trips keep the document structure.
  - All 116 upstream unit tests pass.
  - Upstream undo and the layer controller compile unmodified against the shadow `Control` interface.
- **Refinement of R4: shadow headers instead of new interface names.**
  - `qt/compat/include/control/Control.h` and friends are abstract interfaces with upstream's names and signatures. Reused upstream files therefore need **no edits at all**.
  - The per-tab session in M2 implements `Control`.
- **Next: M2**, the session (`Control` implementation), render service and canvas, based on the ADR-0001 decision.

---

# Plan (as approved)

## Context
The goal is a pen-first app for notes and PDF annotation. It keeps Xournal++'s tested core:
- .xopp/.xoj compatibility
- stroke rendering, pressure handling, stabilizer and palm rejection
- file I/O, undo and tools

It adds what Xournal++ lacks:
- **several documents as tabs in one window**, the main difference from Xournal++
- a modern UI that works in tablet mode without a keyboard
- iPad-like momentum scrolling and pinch zoom
- Wayland first
- a path to faster PDF rendering with MuPDF, a GoodNotes-style library, PDF-native annotations and mobile

Target repo: `~/xournal_qt_workspace/xournal_qt` (git init'ed, no commits). References: `../xournalpp` (HEAD `283c367`, full history, built binary `build/xournalpp` supporting `--create-img` and `--create-pdf`) and `../krita`.

Test device:
- ThinkPad Yoga running KDE neon 6.2 (Plasma 6.2 **Wayland**)
- Iris Xe GPU, 1920×1200 display at scale 1
- input devices `Wacom HID 5286 Pen` and `Wacom HID 5286 Finger`

Installed: Qt 6.7.2 (Quick, Controls2, Wayland, RHI, VirtualKeyboard), cairo, pango, poppler-glib 24.02, qpdf 10.6, libzip, libxml2, MuPDF 1.19 (old), maliit on-screen keyboard.

**User decisions:**
- GPL now; AGPL-3.0 is acceptable once MuPDF is added.
- A **real git fork** with upstream history.
- The first usable version must include pen, highlighter and eraser, undo/redo, tabs, PDF annotation, .xopp save, **lasso select with move/resize, text, image insert, PDF export, PDF search, and page thumbnails**.

---

## Architecture decisions

1. **Qt 6 (system 6.7.2), C++20, CMake plus Ninja.**
   - A Qt Quick (QML) shell with a custom C++ canvas, because it is touch-first and the only realistic Qt mobile path.
   - Krita dropped its QML canvas and UI after years of tablet bugs, mostly in the Qt 5 era. So the canvas is built **independent of its host**: a `CanvasCore` plus thin hosts (`QQuickItem`, `QWidget`).
   - An on-device spike in **M0** decides which host wins. If the Qt Quick host loses on input fidelity or latency, the shell becomes Widgets with QML panels via `QQuickWidget`, and nothing else changes.

2. **All canvas input goes through C++, never QML handlers.**
   - A window-level event filter catches `QTabletEvent`, `QTouchEvent`, mouse, wheel and `QNativeGestureEvent` before Qt Quick delivers them.
   - Filters run in `QCoreApplication::notify` before `QQuickWindow::event`. Accepting a tablet event there also stops Qt from synthesizing mouse events.
   - The filter hit-tests the canvas rectangle, so the pen still works on the toolbars.
   - An app-wide filter tracks proximity (Qt delivers proximity events only to `qApp`).
   - Set `AA_CompressHighFrequencyEvents=false`, so Qt doesn't merge rapid pen moves (Xournal++ also turns off event compression).

3. **The fork keeps class names and replaces only the GTK "glue".**
   - Retained code (tools, undo actions, input handlers, overlays) calls a small API: about 28 `Control` methods (`getDocument` ×106, `getSettings`, `getUndoRedoHandler`, `getToolHandler`, `fire*`, `getCursor`, `getZoomControl`, …) and about 25 `XournalView`/`XojPageView` methods.
   - We write Qt-based replacements of **`Control`** (which becomes the *per-tab session*, with `using DocumentSession = Control;`), **`XournalView`**, **`XojPageView`**, **`XournalppCursor`**, and the GTK parts of **`ZoomControl`** and **`Layout`**, with the same names and methods.
   - About 30 undo actions and the tool and input handler logic then compile with almost no edits, so later upstream merges stay easy.
   - Every fork edit to an upstream file is tagged `// xqt:`.

4. **Rendering keeps cairo**, so strokes look pixel-identical and `core/view` is reused as-is.
   - A cairo `ARGB32` image surface wraps as a `QImage::Format_ARGB32_Premultiplied` without copying.
   - The new `RenderService` renders **tiles anchored to the page** (512 device px) on a `QThreadPool`, using `DocumentView::drawPage` under `std::shared_lock<Document>` as in upstream `RenderJob`. Jobs are tagged per session and cache generation, with a global LRU memory budget.
   - The PDF layer goes through upstream `PdfCache`, rendering the whole page at moderate zoom. Above a pixel cap it renders per tile, which avoids Xournal++'s ~100 MB full-page buffers at high zoom.
   - During pinch or fling, existing tiles are GPU-scaled. They are re-rendered sharp after a ~300 ms settle (Xournal++'s `blockRerenderZoom`), with low-res page previews as placeholders.
   - **Live stroke:** upstream `StrokeToolView` draws only new segments incrementally, as in Xournal++. Dirty tiles are composited on the CPU (clean tile plus overlays, so highlighter `MULTIPLY` stays correct) and only those tiles are uploaded.
   - On pen-up, `drawAndDeleteToolView` paints the stroke permanently into the cached clean tiles, so there is no re-render and no blink.
   - Tiles snap to device pixels.

5. **PDF engine:** poppler-glib, reusing `core/pdf/popplerapi`, until M6. Then a **MuPDF** backend behind `XojPdfDocumentInterface`, through a new backend factory, selectable at runtime. It uses display lists and one `fz_context` per thread for parallel tile rendering. That makes the app AGPL-3.0, which the user accepted.

6. **GLib stays for now.** Qt on Linux runs on the GLib event loop, so upstream `g_idle_add`/`g_timeout_add` code keeps working. `Util::execInUiThread` becomes a pluggable dispatcher, which the Qt app points at `QMetaObject::invokeMethod`. Removing GLib is a later mobile task.

7. **Tabs:** `AppContext` holds process-wide state, one `Control` (session) exists per tab, and each tab has its own `XournalView` plus canvas.
   - **Shared across tabs:** Settings, ToolHandler (the same pen in every tab), Palette, PageTypeHandler, MetadataManager, ActionRegistry, InputDeviceRegistry, PalmRejection, RenderService, and a CrashHandler that covers all sessions.
   - **Per tab:** Document, UndoRedoHandler, LayerController, ZoomControl/viewport, selection, search, autosave state and file path.
   - No `replaceDocument` copying: each tab builds its own `Document`, which also avoids the `try_lock`/`unlock` undefined behaviour in `Document::operator=`.

8. **Input feel:**
   - Port the Xournal++ handlers almost line for line, with a retyped, Qt-free `InputEvent`. This keeps `filterPressure`, `inferPressureValue`, the tap filter, splitting a stroke when it crosses to another page, one device per sequence, and the stabilizers.
   - Add tilt and rotation fields for future use.
   - Add Krita's synthetic-event filter (`KisInputEventsEater` logic, GPL-compatible, with attribution).
   - **Palm rejection:** pen near, hovering or pressed blocks touch, plus the HandRecognition timeout (on by default here), and pen-down cancels any touch gesture in progress.
   - **Touch drawing:** AUTO. It turns off once a pen is seen, so fingers navigate.
   - **Gestures:** two-finger tap is undo, three-finger tap is redo.
   - An optional pressure-curve lookup table, linear by default, so the feel matches Xournal++.
   - Draw our own pen hover dot, because the pen cursor is broken on Qt 6.7 Wayland.

9. **Viewport:** a new `ViewportController` replaces GtkAdjustment and GtkScrolledWindow.
   - It reuses the `Layout`/`LayoutMapper` grid math and the `ZoomControl` zoom-sequence math.
   - Pinch zoom keeps the document point under the fingers fixed (Krita-style).
   - iOS-like momentum (exponential decay) and rubber-band edges.
   - Touchpad pinch via `QNativeGestureEvent`, and Ctrl+wheel zoom.

10. **Future hooks, built now only as seams:**
    - a `DocumentSource` abstraction for the library later;
    - the backend factory for PDF engines;
    - export backends: the qpdf-based `QPdfExport` is the base for PDF-native `/Ink` annotations.

## Repository layout (fork)
```
xournal_qt/                      (git fork of xournalpp; remote "upstream")
  CMakeLists.txt                 fork-owned; explicit source lists, no GLOB
  cmake/XojCoreSources.cmake     allowlist of reused upstream files
  FORK.md                        rules, `xqt:` tag, merge procedure, replaced-file list
  docs/decisions/*.md            ADRs (UI host, PDF engine, ...)
  src/util/ ...                  upstream, de-GTK'd in place
  src/core/{model,undo,view,pdf,control/...}  upstream, de-GTK'd in place
  src/core/control/Control.{h,cpp}            REPLACED: per-tab session
  src/core/gui/{XournalView,PageView,Layout,...}  REPLACED or edited glue
  src/core/gui/inputdevices/*    upstream handlers, retyped events
  src/qt/app/                    main.cpp, AppContext, TabManager, ActionRegistry, CrashHandler
  src/qt/canvas/                 CanvasCore, RenderService, TileCache, ViewportController,
                                 QtInputTranslator, SyntheticEventFilter, PalmRejection, hosts
  src/qt/models/                 QML-exposed models (tabs, tools, pages, outline, search)
  src/qt/qml/                    QML UI (Main.qml, TabStrip, Toolbar, Sidebar, Settings…)
  src/tools/xqt-cli/             headless render/roundtrip/export/bench CLI (Qt-free)
  spikes/input_canvas/           M0 throwaway spike
  tests/                         ported upstream unit tests + new tests, golden harness
```
CMake targets:
- `xoj-util` and `xoj-core`: Qt-free. Model, xojfile, xml, view (non-overlay), pdf plus export, settings, shaperecognizer, PdfCache, pagetype.
- `xoj-app`: the Qt glue plus tools, undo, overlays, input and canvas.
- `xournal-qt`: the executable.
- `xqt-cli`, and the test targets.

---

## Milestones

### M0: Fork bootstrap and on-device input spike (decides the UI host)
**Fork:**
- In `xournal_qt`, fetch history from the local clone: `git fetch ../xournalpp master`, create `main` at `283c367`, then `git remote add upstream https://github.com/xournalpp/xournalpp.git`.
- Add `FORK.md`. Upstream files keep their GPL-2.0-or-later headers; new files are GPL-2.0-or-later too, so they can be upstreamed. The combined app ships as GPL-3.0+, and as AGPL once MuPDF is in.
- Replace the top-level `CMakeLists.txt`. Upstream `src/*/CMakeLists.txt` stay unused.

**Spike** (`spikes/input_canvas`, `--host=quick|widget`):
- Log every tablet, touch, mouse, native-gesture and proximity event: device name, `pointerType` (pen/eraser), pressure, xTilt/yTilt, rotation, buttons (barrel buttons), `QPointF` precision, timestamp. Logging category `xqt.input`.
- Draw pressure strokes into page-anchored cairo tiles with incremental segments (StrokeViewHelper-style).
- Pan and zoom by GPU or QPainter transform, with simple momentum.
- On-screen latency readout (event timestamp vs. frame swap).
- A `TextField`, to check that the Maliit on-screen keyboard appears through Wayland text-input.
- Print the event-dispatcher class, which should be `…Glib`.

**Exit:**
- `docs/decisions/0001-ui-host.md` records the choice, backed by a checklist:
  - pressure and tilt arrive
  - the eraser end is detected
  - barrel buttons work
  - hover events arrive
  - proximity events arrive, or don't
  - no double mouse-synthesis
  - 60 fps pinch
  - comparable latency
  - the on-screen keyboard appears
- Default: Qt Quick, unless it clearly loses.

### M1: Qt-free `xoj-core`, headless CLI, tests
**De-GTK edits** (all tagged `xqt:`). Fixing `Util.h` alone clears most of the include offenders.
- **util:**
  - `util/include/util/Util.h` and `util/Util.cpp`:
    - drop `<gtk/gtk.h>` and `paintBackgroundWhite`
    - make `execInUiThread` a pluggable dispatcher with a `g_idle_add_full` default
    - make `cairo_set_source_rgbi/argb` use `cairo_set_source_rgba`
  - `util/include/util/Color.h`: remove the `GdkRGBA` converters.
  - `raii/GObjectSPtr.h` and `raii/GLibGuards.h`: remove the gtk include and the `WidgetSPtr` alias.
  - `XojMsgBox`: replace it with a callback `MessageSink` (the CLI prints to stderr; the app shows a QML dialog).
  - `VersionInfo`: rewrite without GTK.
  - Leave out of the build: `GtkUtil`, `gtk4_helper`, `gdk4_helper`, `PopupWindowWrapper`, `GListView`, `GtkWindowUPtr`, `GtkPaperSizeUPtr`.
- **model:**
  - `Document.h/.cpp`: replace the `GtkTreeStore` table of contents with a plain C++ `PdfOutline` tree.
  - `LinkDestination`: drop the GObject `XojLinkDest`.
  - `PaperSize`: delete the GTK constructor.
  - `Element.h`: remove the stray gdk include.
  - `Image.cpp` and `view/background/ImageBackgroundView.cpp`: replace `gdk_cairo_set_source_pixbuf` with a small `pixbufToCairoSurface()` helper that needs only gdk-pixbuf.
  - Leave out `Compass`, `Setsquare` and `GeometryTool` (out of scope).
- **pagetype:** `PageTypeHandler` takes an `fs::path` instead of `GladeSearchpath` and reports errors through `MessageSink`.
- **view:** `View.h` and `Mask.h` include only cairo.
- **pdf:**
  - `XojPdfDocument.cpp`: `new PopplerGlibDocument()` becomes `XojPdfBackendFactory`.
  - `XojCairoPdfExport`: build the PDF outline from `PdfOutline`.
  - Move `ExportBackgroundType` from `control/jobs/BaseExportJob.h` to `pdf/base/ExportBackgroundType.h`.
- **settings** (`Settings.h/.cpp`):
  - The device map stores `InputDeviceTypeOption` plus an app enum `DeviceSource` instead of `GdkInputSource`.
  - Remove the `DeviceListHelper` include.
  - Compile the pure `gui/toolbarMenubar/model/ColorPalette` into core.
- **config headers:** generate them from upstream `src/config*.h.in` with `ENABLE_AUDIO=OFF`, `ENABLE_PLUGINS=OFF`, `ENABLE_QPDF=ON`.

Reused without changes: `control/xojfile/*` (GMarkup parser, LoadHandler, SaveHandler), `control/xml/*`, `core/view/*` except the overlays, `control/PdfCache`, `control/shaperecognizer/*`, `control/jobs/ImageExport`, and `pdf/base/{HybridPdfExport,QPdfExport,XojPdfExportFactory}`.

**`xqt-cli`** (links only `xoj-core`):
- `render <file> --dpi N --out dir`, reusing ImageExport logic
- `roundtrip in.xopp out.xopp`
- `export-pdf in out.pdf`, through qpdf hybrid export
- `bench-render <file> --zoom z`, the baseline for M6

**Tests:**
- Port the GTK-free upstream unit tests (`test/unit_tests/{util,model}/*`, `control/LoadHandlerTest`, `MetadataManagerTest`, and `SettingsTest` if feasible), using GoogleTest via FetchContent.
- `tests/golden/run.sh` renders each fixture in `../xournalpp/test/files` plus sample PDFs with both `xournalpp --create-img` and `xqt-cli render`, then pixel-diffs the results.

**Exit:**
- `ldd xqt-cli` shows no libgtk or libgdk.
- The unit tests pass.
- The golden diffs are 0, or within anti-aliasing tolerance.
- A round-trip keeps the element counts and renders identically.
- The exported PDF opens correctly in Okular.

### M2: Canvas and pen on one document (the Qt glue layer)
- **Glue replacements** (same names, the API subset listed in decision 3):
  - `src/core/control/Control.{h,cpp}` becomes the session. It owns Document, UndoRedoHandler, LayerController, ScrollHandler, ZoomControl and NavigationHistory, and implements `DocumentHandler`.
  - `getWindow()->getXournal()` call sites (about 20) become `control->getXournal()`, a mechanical edit.
  - `src/core/gui/XournalView.*`, `src/core/gui/PageView.*` (keep upstream's `onButtonPress/Motion/Release` tool dispatch logic almost verbatim; replace the whole-page buffer with tiles), and `XournalppCursor` (becomes `QCursor` plus the hover dot).
  - `src/core/gui/Layout.cpp`: switch the adjustment calls to the `ViewportController` interface. `LayoutMapper` stays.
  - `ZoomControl.cpp`: drop the GTK signal hookups (lines 24-73 and 214-233); `ActionDatabase` becomes `ActionRegistry`.
- **Input:**
  - Retype `gui/inputdevices/InputEvents.h`, `PositionInputData`, `DeviceId` and `KeyEvent` to Qt-free types: device ref, modifiers bitmask, 64-bit sequence id, timestamp, tilt.
  - New `QtInputTranslator` replaces `InputEvents::translateEvent`.
  - Keep `InputContext::handle` dispatch and the Stylus, Mouse, Touch, TouchDrawing and Pen handlers.
  - `HandRecognition` keeps its timer logic. `TouchDisableX11` is excluded.
  - Add `SyntheticEventFilter` and `PalmRejection` (Krita-style state machine).
  - Device classes are mapped by Qt device name and `QInputDevice::DeviceType`.
- **Canvas:**
  - `CanvasCore` plus the chosen host, `RenderService`/`TileCache`, and `ViewportController`.
  - The overlay path: CPU composite of dirty tiles, upload of those tiles only. The EditSelection and hover layer is drawn in viewport space.
- **Tools and data:**
  - Tools: pen, highlighter, whiteout, eraser (standard and delete-stroke), hand.
  - `StrokeHandler`, `StrokeStabilizer`, `EraseHandler` and `InputHandler` take the new `Control*`.
  - Undo and redo through `UndoRedoHandler`.
  - Open .xopp/.xoj/.pdf and save .xopp through a temporary minimal QML toolbar.
- **Exit:** the on-device checklist (`docs/testing/device-checklist.md`):
  - visible pressure and a matching look
  - eraser end and barrel buttons
  - the hover dot
  - resting a palm while writing never pans or zooms
  - smooth pinch at 60 fps
  - fling momentum
  - no gaps when writing fast
  - latency subjectively at least as good as Xournal++
  - A file saved here renders identically in Xournal++ (golden check).
  - Scrolling a 200-page PDF stays within the memory budget.

### M3: Tabs and the app shell
- **`AppContext`:** config in `~/.config/xournal-qt/` (separate from Xournal++), shared ToolHandler, `ActionRegistry` (keeps the upstream `Action` enum and names for shortcuts), `InputDeviceRegistry`, and `CrashHandler`, which emergency-saves every dirty session.
- **`TabManager` and `DocumentTabsModel`:**
  - new, open (switching to the tab if the file is already open), close with an unsaved-changes prompt, reorder
  - restore open tabs on start
  - background tabs drop their tile cache after 30 s
  - single instance: `xournal-qt file.pdf` opens a tab in the running window, via `QLocalServer`
- **QML shell (touch targets ≥ 44 px):**
  - tab strip
  - main toolbar: tools, colour swatches, width presets, undo/redo, add page, zoom/fit, overflow menu
  - contextual tool options
  - collapsible page-thumbnail sidebar (PreviewJob logic into `RenderService` low-res jobs) with page operations: insert, delete, duplicate, move, background template via `PageTypeHandler`
  - file dialogs through the xdg portal, recent files
  - autosave per session (AutosaveJob logic)
  - light and dark themes
  - desktop shortcuts (Ctrl+Z/Y/S/O/N/W/Tab)
  - `setDesktopFileName` for the Wayland app id
- **Exit:**
  - Five documents in tabs, and switching between them shows the page in under 100 ms.
  - Undo and edits stay isolated per tab.
  - The close prompts work.
  - After `kill -9`, the crash-recovery files reopen.

### M4: First usable version (the user's scope)
- **Lasso and rectangle selection:**
  - Reuse `Selector`, `EditSelection`, `EditSelectionContents` and `SelectorView`.
  - Enlarge handle hit areas for touch.
  - Move, scale and rotate.
  - Copy, cut, paste, delete and duplicate, with a `ClipboardHandler` rewritten on `QClipboard` and upstream `util/serializing` reused.
  - A long-press context menu.
  - Edge-panning through `QTimer`.
- **Text:**
  - New `TextEditor`: a port of the upstream editing logic, cursor, selection and `TextBoxUndoAction`.
  - The model and rendering stay pango, so text metrics match Xournal++.
  - Input comes from Qt input methods (`inputMethodEvent/Query`), which brings up the Maliit keyboard on Wayland.
  - `TextEditionView` is reused.
- **Images:** insert from a file or the clipboard, through `ImageHandler` and `ImageSizeSelection` logic plus a QML picker.
- **PDF:**
  - Text search with `SearchControl`, `SearchResultView` and a search bar.
  - Text selection and copy with `PdfElemSelection` and its view.
  - Links (URLs through `QDesktopServices`, internal destinations).
  - Outline sidebar built from `PdfOutline`.
- **Export:**
  - PDF via `XojPdfExportFactory`, where qpdf hybrid export keeps the original PDF intact.
  - PNG and SVG via `ImageExport`.
  - A QML dialog for page range and background options.
- **Exit:**
  - An end-to-end session: annotate a lecture PDF using text, an image and lasso-move, export it, and open the result in Okular.
  - Rasterize our export and Xournal++'s export of the same .xopp with `pdftoppm`; they should match.

### M5: Parity round 2 and polish
- Shapes: line, rectangle, ellipse, arrow, coordinate system, ruler, spline.
- Shape recognition, and grid and rotation snapping.
- A layers panel, vertical space and the laser pointer.
- A settings UI for:
  - device-class mapping
  - stylus and touch button config
  - the pressure-curve editor
  - the stabilizer
  - the palm-rejection timeout
  - touch-drawing mode
- Presentation and fullscreen mode, and inserting PDF pages.

### M6: MuPDF and performance
- Vendor MuPDF 1.26.x as `third_party/mupdf`, built as an ExternalProject with `make libs`, no X11 or GLUT. Try the system 1.19 first to prototype.
- `src/core/pdf/mupdf/` implements `XojPdfDocumentInterface`, `XojPdfPage`, `XojPdfBookmarkIterator` and `XojPdfAction`:
  - one `fz_context` per thread, via `fz_clone_context` plus lock callbacks
  - a display-list cache per page
  - tiles rendered with `fz_run_display_list` into a BGRA premultiplied pixmap that wraps the cairo surface memory
  - text search, selection and links through `fz_stext_page`
- A runtime setting chooses the backend.
- Compare backends with `xqt-cli bench-render`. The target is ≥ 2× faster tiles on heavy PDFs.
- Tune the RenderService threads and memory budget.
- Optionally tessellate the live stroke on the GPU if upload latency turns out to matter.

### M7+: Future (the seams exist; nothing is built yet)
- **PDF-native annotations:** a "save into PDF" mode that writes `/Ink` annotations whose appearance streams (`/AP`) come from cairo-pdf, using qpdf or the MuPDF `pdf_*` API, plus an embedded .xopp for a lossless round-trip. Also importing existing PDF annotations.
- **Library:** a SQLite catalog behind `DocumentSource` (QtSql), FTS5 search over PDF and typed text, a thumbnail cache, folders and tags, and a library screen.
- **Android:** the QML shell already fits. The C dependencies come through vcpkg. Reduce the GLib dependency.

## Out of scope until after M4
- Audio recording and playback. `AudioContent` is still kept when saving.
- Lua plugins.
- The LaTeX editor. TeX elements still render and round-trip.
- Setsquare and compass.
- Printing, toolbar customization, and translations (gettext `_()` stays in core; QML uses `qsTr`).
- X11 OS-level touch-disable.
- Shapes, ruler and recognition (M5).

## Verification (every milestone)
- `ctest`:
  - the ported upstream unit tests
  - new tests for `ViewportController` physics
  - `PalmRejection` and `SyntheticEventFilter`, fed scripted event sequences
  - `TileCache` eviction and invalidation
  - a `Control` session running undo/redo scripts
- A golden render harness against `xournalpp --create-img`, and a .xopp round-trip harness.
- Input diagnostics: `QT_LOGGING_RULES="xqt.input*=true"` and an in-app debug overlay showing pressure, tilt, device and latency.
- On the device: `docs/testing/device-checklist.md`, run on Wayland (default) and again with `QT_QPA_PLATFORM=xcb`.
- Cross-compatibility: files written by xournal_qt open in upstream Xournal++, and the reverse.

## Main risks and mitigations
1. **Qt 6.7.2 Wayland tablet gaps.** Krita patches these in its own Qt: no pen cursor, no Enter/Leave for accepted tablet events, possibly unreliable proximity.
   - Draw our own hover dot.
   - Detect pen activity from event timestamps and timeouts instead of proximity alone.
   - The M0 spike measures this; if needed, move to a newer Qt through aqtinstall.
2. **Qt Quick tablet delivery.** The host-agnostic `CanvasCore` and the M0 go/no-go point keep a Widgets fallback cheap.
3. **Live-stroke latency from texture uploads.** Upload dirty 256–512 px tiles only; GPU tessellation is the fallback.
4. **Upstream merge pain.** Keep upstream class and method names, tag edits `xqt:`, list replaced files in `FORK.md`, and merge `upstream/master` regularly.
5. **Poppler serializes renders per document** with a mutex. Acceptable until MuPDF (M6). Pango works per thread as in upstream `RenderJob`.
6. **Memory with many tabs.** A global tile budget, LRU across sessions, and background tabs release their tiles.
7. **Fractional scaling.** Snap to device pixels, and test at scales 1.25 and 1.5.

---

## Revisions after the design review (supersede the matching points above)

- **R1 Build root is `qt/CMakeLists.txt`** (`cmake -S qt -B build-qt -G Ninja`).
  - Upstream's root `CMakeLists.txt` stays untouched; it had 85 upstream commits in two years.
  - All new code lives under `qt/`: `qt/src/{session,render,canvas,input,quick,actions,app}`, `qt/qml`, `qt/cli`, `qt/tools`, `qt/spikes`, `qt/tests`, `qt/docs/adr`.
- **R2 GTK files are never deleted.** They simply aren't built, which avoids modify/delete merge conflicts.
- **R3 `qt/compat/gtkshim`** provides `gtk/gtk.h` and `gdk/gdk.h` headers:
  - glib, gio, cairo and gdk-pixbuf includes;
  - `GdkRGBA`, `GdkRectangle`, `GdkInputSource`, `GdkModifierType` and opaque types, all with GDK3 values;
  - an exact port of `gdk_cairo_set_source_pixbuf/rgba/region`.

  Many upstream files then compile unchanged, and any real GTK widget call fails at compile time.
- **R4 Seams instead of replacing `Control`.** New interfaces, `undo/UndoContext.h` and a `ToolContext`, use method names identical to `Control`'s.
  - Upstream `Control.h/.cpp` stay unbuilt.
  - Undo actions only change their signature and include lines.
  - The session (`DocumentSession`) implements both interfaces.
  - Seam guards use `#ifdef XOJ_NO_GTK` and the tag `// xournal-qt:`.
- **R5 Rendering v1.**
  - The CPU side keeps a per-page buffer (`PageRaster`), a near-verbatim port of `RenderJob` and the `XojPageView` buffer semantics: partial rect re-render, `drawAndDeleteToolView`, stale buffer scaled during zoom, the 300 ms re-render block.
  - Above a size cap it switches to a "windowed buffer" (visible region plus margin).
  - The GPU side uses 256–512 px display tiles only for partial uploads; dirty tiles are re-composed on the CPU (buffer plus overlays).
  - During pinch, one quad is drawn per page, because layout padding is in fixed pixels.
  - The PDF background is rendered outside the document lock, which avoids a stall at pen-up.
  - True page tiling comes with MuPDF display lists.
- **R6 Input on Qt 6.7 Wayland.**
  - Tablet devices come through without their real names, so devices are classified by `QInputDevice::DeviceType` plus `PointerType`.
  - The event filter must `accept()` and return `true` inside the canvas, and leave events unaccepted outside it.
  - An `OverlayRegistry` of QML rects (toolbars, popups) decides whether the canvas claims a point.
  - Touch ownership is decided at the first finger down.
  - `TouchInputHandler` becomes a new `GestureRecognizer`; `HandRecognition` becomes the new `PalmRejection`.
- **R7 Settings.** Upstream `Settings` (`settings.xml`) keeps the shared settings, with `XOJ_CONFIG_FOLDER_NAME=xournal-qt`. New app settings (tabs, gestures, UI) go in `AppSettings` (QSettings).
- **R8 `Util::execInUiThread` and `Job::afterRun`** go through `setUiDispatcher()`: `QMetaObject::invokeMethod` in the app, synchronous in the CLI.
- **R9 Milestone order.** M1 core+CLI → M2 sessions + render service (headless) → M3 tools (headless replay tests) → M4 canvas, Quick host and input (on device) → M5 tabs + shell + the user's first-usable scope. The first-usable scope is unchanged: lasso, text, image, PDF export/search, thumbnails. MuPDF and later milestones follow.
- **R10 The CLI mirrors upstream flags** (`--create-img`, `--create-pdf`, `--export-png-dpi`, …), so the golden harness can call both binaries the same way. `xoj-imgdiff` (C++/cairo) does the pixel diffs.
