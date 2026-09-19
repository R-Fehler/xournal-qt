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
| `qt/compat/include/control/actions/ActionDatabase.h` | Abstract action sink with upstream's API (`enableAction`, `setActionState<T>`, `fireChangeActionState<T>`, `fireActivateAction`). States are a small `std::variant` instead of `GVariant`. |

Shadow headers keep the upstream names and method signatures. Upstream code that calls them compiles unmodified. If upstream starts using a new function, the Qt build fails to compile, and the missing function is added to the compat header.

## 2. Guarded seams in upstream files (`#ifdef XOJ_NO_GTK`, tagged `xournal-qt:`)
The Qt build defines `XOJ_NO_GTK=1`. The upstream GTK build is unaffected by these edits.

| File | Change | Why |
|------|--------|-----|
| `src/util/include/util/Util.h`, `src/util/Util.cpp` | `execInUiThread` goes through a pluggable `Util::setUiThreadDispatcher()`; the default is a GLib idle source. `paintBackgroundWhite` is guarded. | `gdk_threads_add_idle_full` / `GtkWidget` |
| `src/core/model/Document.h`, `.cpp` | PDF table of contents as `DocumentOutline` (plain C++) instead of a `GtkTreeStore`: `getOutline()` replaces `getContentsModel()`. | `GtkTreeModel` |
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

## Not compiled in the Qt build
- `src/util`: `GtkUtil.cpp`, `gtk4_helper.cpp`, `gdk4_helper.cpp`, `XojMsgBox.cpp`, `VersionInfo.cpp` (replaced by `qt/compat`).
- `src/core`: everything not listed in `qt/cmake/XojSources.cmake`.
  - For M1 that includes all of `gui/`, except `LayoutMapper.cpp`, `GladeSearchpath.cpp` and `toolbarMenubar/model/ColorPalette.cpp`.
  - It also includes `control/tools/`, `view/overlays/` and the geometry tools, which come in later milestones.
  - `undo/*` and `control/layer/*` **are** compiled, unmodified, against the shadow interfaces.

## Verification
- `qt/tests/golden/run_golden.sh`: `xournal-qt-cli` PNG and PDF export are pixel identical to upstream `xournalpp` built at the merge base, and `.xopp` round trips keep the document structure.
  - `golden-quick`, part of plain `ctest` (about 2 s): six representative fixtures (strokes, text, images, layers, attached PDF background) at 72 dpi.
  - `golden-full`, opt-in (several minutes): every fixture in `test/files` at 72 and 150 dpi. Run `ctest -C Full -L golden-full` for upstream merges (done by `qt/tools/merge-upstream.sh`) and milestone sign-off.
- `xoj-unit-tests`: the upstream unit tests (`test/unit_tests`, all suites except the GTK-only `ActionDatabaseTest`) run against the Qt-free core.
  - Also `qt/tests/unit/UndoRedoTest.cpp`: upstream undo actions and `UndoRedoHandler` driven through a test implementation of the shadow `Control`.
- The upstream GTK build (`cmake -S . -B build-gtk`) must keep compiling with all seams applied.
