# JavaScript plugins (QJSEngine) for xournal-qt: research report

Status: research only. Nothing in the repository was changed.
Repository: `/home/fehler/xournal_qt_workspace/xournal-qt`, branch `master-qt`. Paths below are relative to it.
Qt headers checked: `~/xqt-env68` (6.8.4), `~/xqt-env` (6.9.3), `~/xqt-env611` (6.11.2). There is no 6.7 install on
the machine, so 6.7 availability was checked against the archived Qt docs (doc.qt.io/archives/qt-6.2 and 6.4, and qt-6.7).
**[inferred]** marks claims I reasoned out but did not verify in code or docs.

---

## TL;DR

- **xournal-qt has no plugin system today.** The Qt build sets `ENABLE_PLUGINS OFF` (`qt/cmake/XojCore.cmake:40`),
  `qt/cmake/XojSources.cmake` lists nothing from `src/core/plugin/`, Lua is not a dependency, and `qt/` has no plugin
  UI. The docs say so too: `qt/docs/development/android.md:301` ("Lua plugins … already off in the Qt build") and
  the release notes from `0.1.0.md:46` on ("the plugin console … not ported").
- **Upstream's Lua API cannot be reused as is.** `src/core/plugin/luapi_application.h` (4,333 lines, 56 `app.*`
  functions, table at `:4220-4277`) includes GTK and upstream's GUI classes (`gui/MainWindow.h`, `gui/XournalView.h`,
  `gui/sidebar/Sidebar.h`, the GTK file dialogs: `:20-48`). Plugins call `app.*` and are called back by the name of a
  global function. They also get the full Lua standard library, `io` and `os` included (`luaL_openlibs`,
  `Plugin.cpp:303`).
- **QJSEngine fits well.** QtQml is already linked (`qt/cmake/XqtApp.cmake:26`), so it adds no library and almost no
  binary size. Every API we would need exists in Qt 6.7 (several since 6.1 or 6.2). It has no file, network or
  process access unless the host adds it, and it runs on all four target platforms. On iOS it runs without the JIT
  **[inferred]**.
- **Recommendation: (b) JavaScript only, no Lua.** Use a small, permission-gated API of fork-owned facades, one
  `QJSEngine` per plugin on the UI thread, commands declared in a JSON manifest, and one undo step per command.
  Bring the useful shipped Lua plugins over as JS examples or note where a native feature replaces them. Write the
  C++ facades so they do not depend on the engine. A Lua binding could then be added later without redoing the
  API, but I would not plan for it.
- **Effort:** about **4 blocks** (host, UI integration, API v1 with undo, examples/docs/hot reload). Adding Lua
  compatibility costs about **+2–3 blocks**, plus upkeep at every upstream merge.

---

## 1. Status quo

### 1.1 Does xournal-qt build or run upstream's plugin system?

| Question | Answer | Evidence |
| --- | --- | --- |
| `src/core/plugin` in `XojSources.cmake`? | No. The file has no `plugin` entry (grep, 223 lines). | `qt/cmake/XojSources.cmake` |
| `ENABLE_PLUGINS`? | Forced off for the Qt build. | `qt/cmake/XojCore.cmake:40` (`set(ENABLE_PLUGINS OFF)`) |
| Lua a dependency? | No. Only upstream's root build runs `find_package(Lua REQUIRED)`. | `CMakeLists.txt:187-197` (upstream, not used by `qt/`) |
| Plugin UI in `qt/`? | None. The only "plugin" hits in `qt/src` are Qt/QML plugins (`main.cpp:22,60` `Q_IMPORT_QML_PLUGIN`) and Qt image plugins at shutdown (`AppController.cpp:590`). | grep |
| Documented? | "Lua plugins … already off in the Qt build" | `qt/docs/development/android.md:301` |
| | "the plugin console … not ported" | `qt/docs/release-notes/0.1.0.md:46` (repeated in 0.2–0.4) |
| `activateAction` plumbing | The fork's `ActionDatabase` shadow exists (`qt/compat/include/control/actions/ActionDatabase.h`, implemented by `qt/src/session/SessionActions.cpp`). Its `onActivate` hook (`SessionActions.h:30`) is never assigned in `qt/src`, so upstream `Action`s activated through it do nothing today. | `SessionActions.cpp:36-40`, grep for `onActivate =` (empty) |
| Shared settings | Upstream's `Settings` still has `pluginEnabled`/`pluginDisabled` (`src/core/control/settings/Settings.h:448-449`). The fork uses its own config folder (`~/.config/xournal-qt`, ADR 0002, `PathUtil.cpp`). Xournal++ users' plugins in `~/.config/xournalpp/plugins` are never seen. | |

### 1.2 How upstream's plugin system works

- **Discovery**: every subfolder of `<share>/plugins` and of `<config>/plugins` (`PluginController.cpp:107-117`),
  sorted by name and path. Enabled or disabled through settings (`:119-139`); enabled plugins are loaded at
  start (`:141-145`).
- **Manifest**: `plugin.ini` with `[about] author, description, version`, `[default] enabled`,
  `[plugin] mainfile` (`Plugin.cpp` `loadIni`, about `:180-246`). All shipped plugins are `enabled=false`.
- **Engine**: one `lua_State` per plugin (`Plugin.h:204`). `luaL_openlibs` loads the whole standard library,
  `io`/`os` included (`Plugin.cpp:303`). The plugin's folder is added to `package.path` (`:256-280`), and `app` is
  registered (`:39,248-254`). There is no sandbox, no timeout and no permission model.
- **UI registration**: `initUi()` calls `app.registerUi{menu, callback, mode, accelerator, toolbarId, iconName,
  parentPath}` (`luapi_application.h:526-…`). This makes GTK menu entries (`Plugin.cpp:79-…`, `GMenu`/
  `GSimpleAction`), toolbar buttons (`Plugin.h:66-83`, `toolbar.ini` ids) and accelerators in GTK syntax
  (`<Control>t`, `Plugin.h:57-61`).
- **Callbacks** are names of global Lua functions, called with `lua_pcall` (`Plugin.cpp:338-375`). Errors go to
  `XojMsgBox::showPluginMessage` (`:310,329,351`). The fork's shadow of it exists:
  `qt/compat/include/util/XojMsgBox.h:78`.
- **Undo**: only the `add*` functions push undo actions, controlled by `allowUndoRedoAction = "grouped" |
  "individual" | "none"` (`luapi_application.h:974-1003, 1444-1461`). Everything else, such as `activateAction`
  sequences and `setPageSize`, makes separate undo steps or none. A plugin command is not one undo step.

### 1.3 The shipped plugins (`plugins/`, 723 lines of Lua in total)

| Plugin | What it does | `app.*` (and other) calls used | In xournal-qt today |
| --- | --- | --- | --- |
| BeamerPresentation | Steps through LaTeX beamer animations: same PDF page label → next background page, else next page. "Clean" deletes repeated overlay pages. | `registerUi`, `getDocumentStructure`, `getPageLabel`, `changeBackgroundPdfPageNr`, `refreshPage`, `scrollToPage`, `setCurrentPage`, `activateAction("delete-page")` (`BeamerPresentation/main.lua:7-37`) | No PDF page labels found in `qt/src` (grep). Needs a new API. |
| ColorCycle | Alt+C cycles the tool color through a list. | `registerUi`, `changeToolColor{color, selection}` (`ColorCycle/main.lua:3,34`) | `AppController::setColor` (`AppController.h:1465`). Trivial port. |
| Example | Minimal example: a menu entry opens a Yes/No dialog. | `registerUi`, `openDialog`, `require "var_dump"` (`Example/main.lua`) | Trivial port. |
| Export | Exports to PDF, SVG or PNG next to the `.xopp` without a dialog (`/tmp/temp` if unsaved). | `registerUi`, `export{outputFile, backend}`, `getDocumentStructure` (`Export/main.lua:12-31`) | `exportPdf` (`AppController.h:1483`), `exportPageImages` (`:1398`). No SVG export found. Writing beside the document needs a file permission. |
| FitToContent | Fits the page to the layer's or selection's strokes: moves them to a new layer and resizes the page. | `getStrokes`, `setLayerVisibility`, `activateAction("layer-new-above-current")`, `setCurrentLayerName`, `addStrokes`, `setPageSize`, `openDialog` (`FitToContent/main.lua:13-57`) | Page size exists (`applyPageSize`, `AppController.h:1264`; `qt/src/canvas/PageResize.cpp`). Port needs elements, layers and page size. |
| HighlightPosition | Alt+X toggles the cursor position highlight. | `changeActionState("position-highlighting")` (`HighlightPosition/main.lua:9`) | No such feature in the fork (grep). Not applicable. |
| ImageActions | Inverts, scales, flips, rotates, trims and splits selected images. | `getImages`, `addImages`, `clearSelection`, `addToSelection`, `activateAction("delete")`, plus **lua-vips from luarocks and LuaJIT `ffi`** (`ImageActions/main.lua:47-58,122`) | Not portable mechanically: depends on a native image library. Would need an image API in the host (QImage). |
| LayerActions | Clones layers to the next PDF page, hides all but the first layer, adds a top layer on every page. | `getDocumentStructure`, `setCurrentPage`, `setCurrentLayer`, `activateAction("duplicate-page"/"goto-next"/"delete-page"/"layer-goto-top"/"layer-new-above-current")`, `changeBackgroundPdfPageNr`, `openDialog` with a callback (`LayerActions/main.lua`) | `LayersModel` has select, setVisible, addLayer, rename (`qt/src/shell/LayersModel.h:52-63`). Portable with a layers API. |
| QuickScreenshot | Captures a screen region with external tools (`maim`, `scrot`, …) and saves a PNG. | `io.popen`, `os.execute`, `os.tmpname`, `os.remove`, `fileDialogSave`, `glib_rename` (`QuickScreenshot/main.lua:25-128`) | Shells out, which VISION's "self-contained" rules out (`VISION.md:17-18`). The fork has the native **snip** (`qt/docs/features/snip.md`). Drop. |
| SpaceForNotes | Inserts an empty page after every PDF page. | `getDocumentStructure`, `setCurrentPage`, `activateAction("new-page-after")` (`SpaceForNotes/main.lua`) | **Native already**: `insertBlankAfterPages` (`AppController.h:1250`, `NoteSpaceDialog.qml:182`) and note space. |
| ToggleGrid | Toggles graph paper and grid snapping. | `changeActionState("grid-snapping")`, `changeCurrentPageBackground` (`ToggleGrid/togglegrid.lua`) | `changePageBackground` (`AppController.h:1223`). Grid snapping as a setting. Easy port. |

**How the API is used, by area** (the full surface is `luapi_application.h:4220-4277`; a typed reference is in
`plugins/luapi_application.def.lua`, 1,324 lines):

| Area | Lua functions | xournal-qt concept it maps onto |
| --- | --- | --- |
| UI registration | `registerUi` (menu, accelerator, toolbar button), `registerPlaceholder`/`setPlaceholderValue` (toolbar labels) | ⋮ menu (`qt/src/app/qml/MoreMenu.qml`, `CommandItem` at `:15`), toolbox app items (`ToolboxModel::place/unplaced/idOfApp`, `ToolboxModel.h:166-175`; `qt/docs/features/toolbox.md`), shortcuts (`ShortcutsModel`, static list at `ShortcutsModel.cpp:34`; keys via `win.keysOf`, `Main.qml:1021`; `Shortcut`s in `WindowShortcuts.qml`) |
| Actions | `activateAction`, `changeActionState`, `getActionState`, deprecated `uiAction`/`sidebarAction`/`layerAction` (upstream `Action` enum, `src/core/enums/Action.enum.h`, 198 lines; old names via `ActionBackwardCompatibilityLayer.cpp`) | No 1:1 equivalent. Fork commands are `AppController`/`CanvasActions` invokables and QML handlers; `SessionActions::onActivate` is unused |
| Tools and colors | `changeToolColor`, `getToolInfo`, `getColorPalette`, `getFont`/`setFont`/`getFonts` | `AppController::selectTool/setColor/setSize` (`AppController.h:1304,1465,1467`), properties `tool`, `color`, `size` (`:196-228`), palettes (`colorPalettes`, `setPaletteColor`, `:237-239,479`), toolbox entries (`ToolboxModel::use`, `:166`; `applyToolEntry`, `AppController.h:495`) |
| Document, pages | `getDocumentStructure`, `setCurrentPage`, `scrollToPage`/`scrollToPos`/`getScrollPos`, `setPageSize`, `changeCurrentPageBackground`, `changeBackgroundPdfPageNr`, `setBackgroundName`, `getPageLabel`, `refreshPage` | `DocumentSession` (one per tab; page ops at `DocumentSession.cpp:468,758,867,1010`), `AppController::insertPages/changePageBackground/applyPageSize/goToPage` (`AppController.h:1219,1223,1264,1183`), `CanvasActions::goToPage` (`CanvasActions.h:147`) |
| Layers | `setCurrentLayer`, `setLayerVisibility`, `setCurrentLayerName` | upstream `LayerController` through the session, `LayersModel` (`LayersModel.h:52-63`) |
| Elements | `getStrokes/addStrokes/addSplines`, `getTexts/addTexts`, `getImages/addImages`, `getLinks/addLinks` | upstream model (`Stroke`, `Text`, `Image`, `Link`) under the document lock. The fork's pattern: `qt/src/canvas/TodoStamp.cpp:94-99` (lock, `addElement`, `InsertUndoAction`, `firePageChanged`) |
| Selection | `clearSelection`, `addToSelection` | `CanvasActions` (`hasSelection`, `clearSelection`, `selectAllOnPage`: `CanvasActions.h:33,101,105`). No "add these elements" yet |
| View | `getZoom`/`setZoom`, `getDisplayDpi` | `AppController::setZoomPercent` (`:1179`), `CanvasActions` zoom (`:149-153`) |
| Files | `fileDialogOpen`, `fileDialogSave`, `openFile`, `export`, `getFolder`, `glib_rename`, `saveAs` | QML `FileDialog` (QtQuick.Dialogs), `ContentFiles` (Android `content://`), `AppController::openFile` (`:889`), `exportPdf` (`:1483`), `exportPageImages` (`:1398`) |
| Dialogs | `openDialog(msg, buttons, callback)`, `msgbox`, `showFloatingToolbox` | `AdaptiveDialog`/`MenuSheet` QML, the snackbar (`Main.qml:780,881`) |
| Undo | implicit, only in `add*` (`allowUndoRedoAction`) | `DocumentSession::getUndoRedoHandler` (`DocumentSession.cpp:371`), `addPageUndoAction` (`DocumentSession.h:531`) |

---

## 2. QJSEngine as the host

### 2.1 What it offers (Qt 6.7 or older; the methods were checked in `qjsengine.h` of 6.8/6.9/6.11 and their version notes in the docs)

| Capability | API | Since | Notes |
| --- | --- | --- | --- |
| Run scripts | `evaluate(program, fileName, line, &stackTrace)` | 5.0 | Returns an error `QJSValue` (`isError()`, properties `lineNumber`, `fileName`, `stack`). |
| ES modules | `importModule(fileName)` | ≤6.2 (in the 6.2 docs) | Loads a file as an ECMAScript module and returns its namespace object (exports). Imports are resolved on the file system or in `qrc:`. |
| Host modules | `registerModule(name, value)` | ≤6.2 (in the 6.2 docs; checked in the archived 6.2 and 6.4 pages) | `import { doc } from "xournal"` without a file. Docs warning: a registered QObject's properties are *snapshotted*, so export functions or wrapper objects, not live properties. |
| Expose C++ | `newQObject(obj)` (properties, signals, slots, `Q_INVOKABLE`), `newQMetaObject` (enums, constructors) | 5.0 | `newQObject` gives **JavaScriptOwnership** unless the object has a parent or `setObjectOwnership(obj, CppOwnership)` is set. A QObject returned from a `Q_INVOKABLE` without a parent is also JS-owned **[inferred from the QML ownership rules]**. |
| Values | `QJSValue`, `QJSManagedValue` (`toManagedValue`), `newObject`, `newArray`, `newSymbol` (6.2), `newErrorObject` | `QJSManagedValue` 6.1 | `QJSManagedValue` is faster for repeated property access in C++. |
| Errors from C++ | `throwError(...)`, `hasError()`, `catchError()` | 5.12 / 6.1 | The API throws JS `TypeError`/`RangeError` for bad arguments. |
| Interrupt | `setInterrupted(bool)` | 5.14 **[inferred version]**; present in 6.7 docs | Thread-safe: "any JavaScript executed by this engine immediately aborts and returns an error object". This is how timeouts work: a watchdog thread interrupts a script that runs too long. |
| Extensions | `installExtensions(Console | GarbageCollection | Translation)` | 5.6 | "By default, familiar utilities like logging are not available." Install `console` (and send it to the plugin's log), not `gc`. |
| GC | `collectGarbage()` | | Per engine. |
| Language | "The QML runtime implements the 7th edition of the standard" (ES2016), plus `??` (5.15), `?.` (6.2), numeric separators (6.8, **not** in 6.7) | | Qt doc "JavaScript Host Environment". `let/const`, classes, arrow functions, Promise, Map/Set, typed arrays, modules work. Whether `async`/`await` works is **not verified** (it is ES2017). The v1 API should not depend on it. |
| Threading | "All functions in this class are reentrant." | | One engine is used from one thread (the one it lives in). QObjects exposed to it are called directly, so they must live in that thread too. |

**What a plain `QJSEngine` does not have** (only `QQmlEngine` adds these): the `Qt` global object
(`Qt.openUrlExternally`, `Qt.createQmlObject`), `XMLHttpRequest`, `LocalStorage`, timers (`setTimeout`), and any file,
network or process API. The docs list no such APIs for `QJSEngine`. A plugin can only do what the host exposes,
which is the opposite of upstream's `luaL_openlibs` (`Plugin.cpp:303`). Two holes remain:

1. **ES module imports reach the file system.** `importModule` and static `import` resolve paths on disk, and
   QJSEngine has no import hook. A plugin can therefore import any `.js`/`.mjs` file it can name. Imports are parsed
   as JavaScript, so reading arbitrary data this way is not practical **[inferred]**. To keep it tight: load the main
   file through `importModule`, review imports at install time (warn on absolute paths or paths outside the plugin
   folder), and serve the host API through `registerModule`.
2. **Every slot of an exposed QObject is callable.** That includes `QObject::deleteLater()`, a public slot
   (`qobject.h:355-356`). Never hand `AppController` or any long-lived object to a plugin. Expose small facade
   objects (`CppOwnership`), and wrap them in a frozen JS object built by a bootstrap module. The plugin then sees
   only whitelisted functions, never the raw wrapper.

### 2.2 Separate `QJSEngine` per plugin, or the app's `QQmlEngine`?

The app has **one** `QQmlApplicationEngine` (`qt/src/app/main.cpp:295`). Undocked windows get child contexts with
their own `app` (`main.cpp:299-301`). `app` is a context property of the **root context** (`EngineSetup.cpp:27`), so
any QML loaded into this engine sees the entire `AppController` (about 2,000 lines of header with saving, the
library, file operations; `AppController.h`).

| | Own `QJSEngine` per plugin (recommended) | Shared app `QQmlEngine` |
| --- | --- | --- |
| Isolation | Separate heap, globals, GC. One plugin cannot break another or the UI's JS. Unloading means deleting the engine. | Shared global object and heap. A plugin's QML or JS reaches `app` and `win` through the context chain, with no sandbox at all. |
| Capabilities | Only what we expose. | Everything QML has: `Qt.openUrlExternally`, `XMLHttpRequest`, `FileDialog`, `Qt.createQmlObject`, … |
| Plugin UI | No QML. Dialogs are described as data and drawn by the app's own QML (`AdaptiveDialog`). | Plugins could ship QML panels: powerful and familiar, but this is the hole above. |
| Hot reload | Drop the engine, make a new one. | Component cache, `trimComponentCache`, leaking objects, hard to get right. |
| Startup cost | Created lazily on a plugin's first command (manifest-declared commands need no engine to appear). | None extra. |
| Memory | Each engine has its own heap. Expected in the low MB per engine **[inferred, measure in the host block]**; fine for a handful of plugins. | Smaller. |
| Tests | Plain unit tests without a window (label `plugins`). | Need the UI fixture. |

If plugins ever need their own QML UI (v2+), use a **second, separate `QQmlEngine`** for plugin UI, without `app`,
showing its windows/items in its own `QQuickWindow` or as an overlay. Do not use the app's engine.

### 2.3 Threading, performance, platforms, size

- **Thread**: run plugin engines on the **UI thread**. The document model, `DocumentSession` (a `QObject`,
  `DocumentSession.h:61`), undo and the views are UI-thread objects. AGENTS.md: the document is read under
  `std::shared_lock` and "never hold the document lock while drawing a PDF". The API takes and releases the lock
  inside each call; JS never runs with the lock held. A busy script blocks the UI, so we need the watchdog
  (`setInterrupted` from a helper thread, e.g. after 2 s, with a message "Plugin X was stopped"). Heavy computation
  in a worker engine with message passing is a later option.
- **Performance**: plugin commands are user-triggered and small. The cost is data conversion: an `addStrokes`
  with thousands of points through `QVariantList` is slow **[inferred]**. Pass points as flat arrays
  (`[x0,y0,p0,x1,…]`) or typed arrays (`ArrayBuffer` ↔ `QByteArray`), and read them in C++ with `QJSManagedValue`.
  The V4 JIT is on for desktop and Android. macOS needs the `com.apple.security.cs.allow-jit` entitlement, which the
  QML engine already requires (`qt/docs/development/macos.md:186`). On iOS (VISION's iPad) V4 runs interpreted,
  because iOS does not allow a JIT **[inferred]**. Fine at this scale.
- **Platforms**: QtQml is part of every build already (Linux, Windows MSYS2, macOS Homebrew, Android). No new
  dependency, no new deploy step (windeployqt/macdeployqt already ship QtQml for the UI).
- **Binary size**: `Qt6::Qml` is already linked (`qt/cmake/XqtApp.cmake:26`; `libQt6Qml.so` is 8.3 MB in the 6.9
  env and already shipped). The plugin host is the only new code, probably under about 200 KB **[inferred]**. Lua
  for comparison: vendoring Lua 5.4 is small (about 250 KB of C, MIT). Lua's cost is the API binding and its upkeep,
  not the library (§4).
- **Qt version spread** (6.7 KDE neon, 6.8 Debian 13, 6.11 Android): V4 changes behaviour a little between
  versions (6.8 added numeric separators). Test plugins on 6.7 in CI. Document "ES2016 + `??`/`?.`" as the language
  level for plugin authors.

---

## 3. Design proposal

### 3.1 Plugin format

A folder (or a `.xqtplugin` zip, which is how plugins get installed on Android/iOS, where users can't drop folders:
the app unpacks it with the existing `ZipFile`/`ContentFiles` code) holding:

```
color-cycle/
  plugin.json
  main.mjs
  icons/cycle.svg        (optional)
  README.md               (optional)
```

```json
{
  "id": "org.example.color-cycle",
  "name": "Color cycle",
  "version": "1.2.0",
  "author": "…",
  "description": "Cycle the pen's color through a list.",
  "api": "1.0",
  "main": "main.mjs",
  "permissions": ["tools"],
  "commands": [
    { "id": "cycle", "title": "Cycle color", "shortcut": "Alt+C", "icon": "icons/cycle.svg",
      "when": "document", "menu": true, "toolbox": true }
  ]
}
```

```js
// main.mjs
import { tools, ui } from "xournal";
const colors = ["#000000", "#008000", "#00c0ff", "#ff0000"];
let i = 0;
export function cycle(ctx) {            // the command id is the export name
  i = (i + 1) % colors.length;
  tools.setColor(colors[i]);
}
```

Why declare commands in the manifest rather than in an `initUi()`?

- Menus, toolbox items and shortcuts exist before any JS runs. The engine is made on first use, so startup costs
  nothing.
- The settings page can show, and the user can approve, what a plugin will add and what it may touch before
  enabling it.
- Hot reload and rebinding a shortcut in Settings work without running plugin code.

Locations: a `plugins` folder in the app's config folder (`~/.config/xournal-qt/plugins`, inside the fork's own
folder by `XOJ_CONFIG_FOLDER_NAME`, ADR 0002), plus bundled examples in `qrc:` (off by default, like upstream's).
Enabled state lives in the fork's part of `settings.xml`.

### 3.2 API v1 (`import { … } from "xournal"`), mapped to existing classes

The surface is small and covers what the shipped Lua plugins need. Everything is synchronous except dialogs, which
take callbacks or return Promises. Promise jobs need checking in the host block **[inferred]**.

| Module object | v1 members | Backed by | Permission |
| --- | --- | --- | --- |
| `xournal` | `apiVersion`, `platform`, `notify(text)` (snackbar), `log/console` | `Main.qml:780` snackbar; per-plugin log | always |
| `doc` | `available`, `pageCount`, `currentPage`, `title`; `page(i)` → `{width, height, background:{type, color, pdfPage}, layers:[{name, visible}], noteSpace}` | `DocumentSession` (current tab via `CurrentDocument`/`WindowContext`), `AppController::pageSizeOf` (`:1256`), `currentPageFormat` (`:1229`) | `document.read` |
| `doc` (write) | `insertPages(after, count, background)`, `deletePages([…])`, `duplicatePage(i)`, `setPageSize(pages, w, h)`, `setBackground(pages, type, color)`, `setPdfPage(i, n)` | `AppController::insertPages/deletePages/duplicatePages/applyPageSize/changePageBackground` (`:1219,763,766,1264,1223`); `DocumentSession` page ops | `document.write` |
| `layers` | `list(page)`, `add(page, {above, name})`, `setVisible(page, i, on)`, `rename`, `select` | upstream `LayerController` through the session (as `LayersModel` does, `LayersModel.h:52-63`) | `document.read`/`write` |
| `elements` | `strokes(scope)` / `addStrokes(page, layer, [{points, width, color, tool, fill, lineStyle}])`, `texts`/`addTexts`, `images`/`addImage(page, {png ArrayBuffer, rect})`, `remove(refs)`; scope: `"selection" | "layer" | "page"` | upstream model, the fork's insert pattern (`TodoStamp.cpp:94-99`), `ElementTimes` (stamp `xqt-created`) | `document.read`/`write` |
| `selection` | `has`, `clear()`, `select(refs)`, `bounds()` | `CanvasActions` (`CanvasActions.h:33,101,105`); "select these" is new | `document.read` |
| `tools` | `current` (`{type, color, size, width}`), `select(type)`, `setColor`, `setSize`, `setWidth`, `entries()`, `use(id)`; palette roles | `AppController::selectTool/setColor/setSize/setCustomWidth` (`:1304,1465,1467,230`), `ToolboxModel::tools/use` (`:120,166`), palettes (`:237-241,479-488`) | `tools` |
| `view` | `goToPage(i)`, `zoom`, `setZoom(pct)`, `fitWidth()` | `CanvasActions` (`:147-153`), `AppController::setZoomPercent` (`:1179`) | always |
| `ui` | `message(text, buttons) → index`, `prompt(label, default)`, `choose(label, options)` | a generic `PluginDialog.qml` on `AdaptiveDialog` | always |
| `files` | `chooseSave({name, filters}) → handle`, `chooseOpen(filters) → handle`; `exportPdf(handle, range)`, `exportImages(handle, pages, dpi)`, `readBytes(handle)`, `writeBytes(handle, ArrayBuffer)` | QtQuick.Dialogs `FileDialog`, `ContentFiles` (Android), `AppController::exportPdf/exportPageImages` (`:1483,1398`), `FileIo` | `files` (only through a dialog the user saw; handles, not paths) |
| `commands` (v1.1) | `setEnabled(id, on)`, `setLabel(id, text)` (upstream's placeholders) | the plugin command model | always |

Not in v1: network (`NetFetch` exists for a later `network` permission), arbitrary paths, processes (never: VISION
"self-contained"), plugin QML, library access (the library is the user's data: AGENTS.md rule 2 makes us careful),
listening to drawing events.

**Do not** expose `AppController`, `CanvasActions`, `ToolboxModel` or sessions directly. Write one fork-owned facade
per area (`PluginDocApi`, …) in a new module. They take `WindowContext`/`CurrentDocument` and call the classes
above. The facades take and return plain `QVariant`/`QJSValue` data, independent of the engine, so a Lua binding
could sit on them later.

### 3.3 Menus, toolbox, shortcuts in the Qt Quick UI

- **Model**: `PluginCommands` (a `QAbstractListModel` in `AppServices`, process-wide like `ShortcutsModel`,
  `AppServices.h:55`) with id `plugin:<pluginId>/<commandId>`, title, icon URL, enabled, `when`.
- **Menu**: a "Plugins" submenu in `MoreMenu.qml` (its `CommandItem`, `:15`). `Instantiator` over the model,
  offered only when it has rows. Each item carries an `objectName` for UI tests.
- **Toolbox**: plugin commands become *app items* that the "+" catalog offers but nobody places by default
  (`ToolboxModel::unplaced/place/idOfApp`, `ToolboxModel.h:170-175`; `toolbox.md` "The app's tools"). The user
  places them like other commands. Commands in groups open their list (toolbox rule), so nothing runs by accident.
- **Shortcuts**: `ShortcutsModel` gets a dynamic group "Plugins". Today its list is static (`ShortcutsModel.cpp:34`).
  Manifest shortcuts become *defaults*, user changes go to the existing `shortcuts` setting, and conflicts show
  through `ConflictRole` (`ShortcutsModel.h:39`). A manifest key that clashes with an app key is **not bound** until
  the user picks one. QML: an `Instantiator` of `Shortcut` items in `WindowShortcuts.qml` with
  `sequences: win.keysOf(id)` (`Main.qml:1021`). Keep in mind its Esc/Back dispatcher note: two enabled
  `Shortcut`s with the same key are ambiguous, so plugins may never bind Esc/Back (`WindowShortcuts.qml:10-14`).
- **Window**: a command acts on the window that triggered it (each window has its own `AppController`,
  `main.cpp:299-301`). The host is process-wide, and the facades are created per call with the triggering window's
  `WindowContext`.
- **Settings → Plugins** page: list, enable/disable, permissions shown on enabling, version and API level, "Open
  folder" (desktop), "Install from file…" (zip), the log, "Reload" (developer mode).

### 3.4 Undo: each plugin command is one undo step

Findings:

- `UndoRedoHandler::addUndoAction` is non-virtual and pushes straight onto a private deque
  (`src/core/undo/UndoRedoHandler.h:43,62-63`; `.cpp:158-168`). Fork code and reused upstream code push through
  `session->getUndoRedoHandler()->addUndoAction(…)` (30 sites in `qt/src`, grep). `getUndoRedoHandler` is the fork's
  override (`DocumentSession.cpp:371`), but it cannot intercept the push.
- Upstream's `GroupUndoAction` exists and the fork uses it (`qt/src/canvas/PageResize.cpp:92`, `FindReplace.cpp:211`,
  `MarkdownSession.cpp:301`). But its `undo()` runs the parts **in the same order as redo** and stops at the first
  failure (`GroupUndoAction.cpp:32-44`). That is fine for independent same-type actions (its header says "a list of
  undo actions of the same type", `GroupUndoAction.h:4`). It is wrong for dependent steps, such as "new layer, then
  strokes into it", which must be undone in reverse.

Proposal:

1. A fork class **`SequenceUndoAction`** (in `qt/src/session/`): undo in reverse order, redo in order, `getPages()`
   the union, `getText()` the command's title. No upstream edit.
2. A **plugin transaction** around each command call. Two ways to fill it:
   - **(i) Facade-only (no seam)**: every write facade builds its own upstream undo actions (`InsertUndoAction`,
     `PageSizeChangeUndoAction`, `PageBackgroundChangedUndoAction`, `InsertLayerUndoAction`, …) and appends them to the
     open transaction instead of the handler. This is the `MarkdownSession::recordInto(GroupUndoAction*)` pattern
     (`MarkdownSession.h:82`). It works for v1 but means re-implementing each mutation in the facades instead of
     calling `AppController`'s existing commands.
   - **(ii) A tiny seam** in `UndoRedoHandler::addUndoAction`: under `#ifdef XOJ_NO_GTK`, marked `xournal-qt:`, a
     pluggable sink (a function pointer or a `std::function` member set by the session) that receives the action
     instead of the deque while a transaction is open. It is about 6 lines and in the style of the other hooks in
     ADR 0002. Then **every** existing command (page ops, layers, `CanvasActions`) can be reused and still ends up as
     one step. Page-structure actions also go through `addPageUndoAction` (`DocumentSession.h:528-531`), which emits
     `pageActionUndone`; the transaction must keep that behaviour for page actions inside it **[inferred, to check]**.
   - Recommendation: start with **(i)** for v1's small write surface and record **(ii)** as the planned seam when the
     API grows. Or take (ii) at once if the author accepts one more seam; it is simpler and more robust.
3. At the end of the command: an empty transaction is dropped. A non-empty one is pushed once
   (`addUndoAction(SequenceUndoAction)`), so the toolbox's undo button and the snackbar "Undo" undo it whole.
4. **On an exception or interrupt mid-command**: roll back by undoing the sequence, then report. (Alternative:
   keep what was done as one undoable step. That is the author's decision; rollback is the safer default.)
5. Writes are refused (a JS `Error`) while the document is read-only or a timeline replay runs
   (`DocumentSession::isReadOnly`, `isReplaying`, `DocumentSession.h:315-319`), and while a modal editor (Markdown
   editing, text editing) is active **[inferred: needs a "can edit now" check in the facade]**.
6. One `firePageChanged` per touched page at commit (page revisions drive every cache: AGENTS.md "Every page has a
   revision").

### 3.5 Sandboxing and security

- **Default deny**: plain `QJSEngine`, only `console` installed (sent to the log), host API through
  `registerModule`, facades behind a frozen JS wrapper (no `deleteLater`, no `objectName`). No `QQmlEngine`.
- **Permissions in the manifest**, shown when the plugin is enabled. A facade checks its permission on every call
  and throws `PermissionError` otherwise:
  `document.read`, `document.write`, `tools`, `files` (dialog-mediated handles only), later `network`,
  `library.read`. No `process`, no free file paths, ever.
- **Ownership**: facades are `CppOwnership` and owned by the host. Data goes out as plain JS objects. Element refs
  are opaque ids (an index plus the page revision), never pointers. A stale ref throws.
- **Timeouts**: a watchdog thread arms on each call into JS and calls `setInterrupted(true)` after N seconds
  (default 2 s; configurable in developer mode). The engine then gets `setInterrupted(false)` again and the
  transaction is rolled back.
- **Recursion and memory**: V4's stack limit raises a `RangeError`. Heap growth cannot be capped per engine in the
  public API **[inferred]**. A runaway allocation can still hurt; this risk is accepted and documented.
- **Trust**: plugins are code the user installs. Show the folder/zip origin. No auto-update and no online catalog in
  v1.
- **Imports**: see §2.1 (static check on install; main file through `importModule`).

### 3.6 Error reporting

- Calls into JS go through one helper. A returned `QJSValue::isError()` or a C++ `catchError()` gives
  `message`, `fileName:lineNumber`, `stack`. A snackbar "Color cycle: TypeError … (main.mjs:12)" with **Details**
  opens the plugin's log in Settings.
- **Log**: a ring buffer per plugin (an owner and a limit, as AGENTS.md wants for any store; for example 500 lines),
  holding `console.*`, errors and permission refusals. Developer mode also mirrors it to stderr.
- **Load errors**: a broken manifest or syntax error marks the plugin "failed" in Settings with the error. Its
  commands are not shown.

### 3.7 Hot reload for development

A "Developer mode" switch in Settings → Plugins watches the plugin folders with `QFileSystemWatcher` (desktop).
When something changes, after a short debounce:

1. Stop dispatching to the plugin and let a running call finish (or interrupt it).
2. Drop its commands from the model; shortcuts disappear through the model.
3. `deleteLater` the engine.
4. Re-read the manifest, recreate the engine lazily, and re-add the commands.

The plugin's enabled state and the user's shortcut overrides are kept, because they are keyed by plugin and
command id.

A "Reload plugins" command and `xournal-qt --plugin-dev <folder>` (load one folder from outside the config) help
authors **[a proposal]**.

### 3.8 Versioning of the API

- `"api": "1.0"` in the manifest is **major.minor**. The host runs a plugin when the major matches and the minor is
  ≤ its own. Otherwise the plugin shows "needs a newer xournal-qt" in Settings.
- Within a major version, only additions. Removing or changing anything means a new major. The host can serve two
  majors for a while through two module names (`"xournal"` → v2, `"xournal@1"` → v1 shims).
- `xournal.apiVersion` at run time, and feature detection (`"addImage" in elements`).
- The API reference is generated from the facades' doc comments into the feature doc (`qt/docs/features/plugins.md`).
  The `QmlApiTest` style check (`qt/src/app/README.md:26`) can be repeated: a test that the documented API names
  exist on the facades.

---

## 4. Migration and Lua

### 4.1 Can upstream's Lua plugins be ported mechanically?

Mostly no. A transpiler could convert syntax, but the APIs differ in kind:

- **Callbacks by global name** (`callback = "cycle"`, `Plugin.cpp:338-358`) and `initUi()` registration become
  exported functions plus manifest commands. This step is mechanical.
- **`activateAction("…")`** strings name upstream GTK actions (198-line `Action.enum.h`, old names in
  `ActionBackwardCompatibilityLayer.cpp`). Six of the eleven plugins rely on them. Each needs a hand mapping to a fork
  command, and some have none (`position-highlighting`).
- **Accelerators** use GTK syntax (`<Control><Shift>c`); Qt uses `Ctrl+Shift+C`. Mechanical.
- **1-based Lua indexing** (`struct.pages[p]`, `setCurrentPage(1)`) versus 0-based JS. Mechanical but error-prone.
- **"Current page/layer" as state** (`setCurrentPage(i)` then act) versus explicit page arguments in the proposed
  API. Ports are easy but not literal.
- **Native Lua modules** (lua-vips, LuaJIT `ffi` in ImageActions) and **`io`/`os`** (QuickScreenshot, Export's
  `/tmp`) have no equivalent by design.

A hand port of the useful ones is small: ColorCycle, ToggleGrid, Example, Export (with a dialog), FitToContent,
LayerActions, Beamer (once page labels are exposed) together come to about 300 lines of JS. SpaceForNotes and
QuickScreenshot are already native (note space, blank pages after each; snip). HighlightPosition has no feature to
toggle. ImageActions needs a host image API (QImage ops) first.

### 4.2 Options compared

| | (a) Keep upstream Lua | (b) JS only (recommended) | (c) Both |
| --- | --- | --- | --- |
| What it means | Re-implement upstream's 56 `app.*` functions against the fork (upstream's file can't be compiled: GTK and GUI includes, `luapi_application.h:20-48`; 33 GTK/GVariant uses, 26 calls into `getWindow()/getXournal()/getScrollHandler()/…`). Vendor Lua 5.4 on all platforms. | The design in §3. | (b), plus a Lua binding over the same facades and a compat layer for `app.*`. |
| Dependencies | +Lua (small, MIT, vendorable like md4c). | none (QtQml present). | +Lua. |
| Effort | ~3 blocks for a credible compat layer (UI registration into menus/toolbox/shortcuts, about 56 functions, an `Action` name mapping, sandboxing `io`/`os` away). | ~4 blocks (§5). | ~4 + 2–3 blocks. |
| Ongoing cost | Upstream's Lua API keeps changing (deprecations marked `Todo(gtk4)`, `:4221-4264`). Each upstream merge needs a check against the compat layer: the "upstream merges stay cheap" principle suffers (`VISION.md:19`). | Ours; versioned. | Both. |
| Security | Upstream plugins expect `io`/`os` (QuickScreenshot). Sandboxing breaks them, not sandboxing breaks "self-contained" and safety. | Default deny. | Two models to keep consistent. |
| Fidelity for users | Partial at best: toolbar ids (`toolbar.ini`), GTK accelerators, `showFloatingToolbox`, `sidebarAction`, placeholders, luarocks modules on Android/iOS impossible. | None; ports needed. | Same partial fidelity as (a). |
| Risk | High effort for an API that maps poorly to touch-first UI; tests for a foreign API. | Ecosystem starts at zero; the API has to be designed well. | Highest. |

**What Xournal++ users would lose with (b):** their existing Lua plugins do not run. Today they don't run in
xournal-qt either, because the fork never had plugins, so nothing that works now is taken away. For the shipped
plugins: three are covered natively or have no target (SpaceForNotes, QuickScreenshot, HighlightPosition), six are
straightforward JS ports that can be bundled as examples, ImageActions waits for an image API, and Beamer waits for
PDF page labels. Third-party Lua plugins (outside this repo) need a manual port. A short migration guide
("`app.registerUi` → manifest command; `app.getDocumentStructure` → `doc.page(i)`; …") is the mitigation.

---

## 5. Effort, risks, open questions, recommendation

### 5.1 Block plan (each block: branch `qt/<block>`, own worktree, one feature per commit; `qt/docs/development/workflow.md`)

| # | Block | Contents | Tests | Size |
| --- | --- | --- | --- | --- |
| 0 | (integrator) ADR 0008 "Plugins: JavaScript in QJSEngine" | The decision, the permission model, the undo choice (facade sink vs. seam). If the seam is chosen: the ADR 0002 entry. | none | small |
| 1 | `qt/plugin-host` | New module `qt/src/plugins` (target `xqt-plugins`, between `shell` and `app`; `architecture.yaml` updated). `PluginManifest` (JSON, validation, api check), `PluginRegistry` (discovery in config + `qrc:`, enabled state in settings), `PluginEngine` (one `QJSEngine` per plugin, lazy, `registerModule("xournal")`, frozen wrappers, console → log ring buffer), watchdog (`setInterrupted`), error capture, `SequenceUndoAction` and the transaction (facade-sink variant). | New label `plugins`: manifest parsing, permission refusal, interrupt of `while(true){}`, error with line number, module import, reload. | 1 block |
| 2 | `qt/plugin-ui` | `PluginCommands` model in `AppServices`; Plugins submenu in `MoreMenu.qml`; toolbox app items (catalog); dynamic shortcuts in `ShortcutsModel` + `Instantiator` in `WindowShortcuts.qml`; Settings → Plugins (list, permissions, log, install from zip, developer mode + hot reload); `PluginDialog.qml` (message/prompt/choose); snackbar errors. | `ui` tests (objectNames), Qt 6.8 UI run (keys/popups changed: workflow.md "Merging"). | 1 block |
| 3 | `qt/plugin-api-v1` | Facades `doc`, `layers`, `elements`, `selection`, `tools`, `view`, `files` (dialog handles, Android `content://`), each write in the transaction, read-only/replay guards, page revisions. | `session`/`canvas` tests per facade: one undo step undoes a multi-step command; rollback on exception. | 1–1.5 blocks |
| 4 | `qt/plugin-examples` | Bundled examples (ColorCycle, ToggleGrid, LayerActions, FitToContent, Export-with-dialog, Example). Feature doc `qt/docs/features/plugins.md` (how it works, API reference, migration guide from Lua). Device checks (Android install from zip, macOS signed build JIT, Windows). Release notes. | example plugins run in tests (headless). | 0.5–1 block |
| | **Total** | | | **≈4 blocks** (3.5–4.5) |
| opt | `qt/plugin-lua` | Only if the author wants (c): vendored Lua, `app.*` compat over the facades, `Action`-name map, no `io`/`os`. | | +2–3 blocks, plus upkeep at each upstream merge |
| later | v1.1/v2 | Image API (ImageActions), PDF page labels (Beamer), event hooks (page added, tool changed), `network` permission, a separate `QQmlEngine` for plugin panels, headless scripts in `xournal-qt-cli` (VISION "the app as a tool", `VISION.md:51-53`). | | |

Blocks 1 → 2/3 → 4. Blocks 2 and 3 can run in parallel: different files, both after block 1.

### 5.2 Risks

1. **Undo grouping** without a seam forces facades to re-implement mutations (§3.4 (i)). Page-structure actions
   inside a group (`addPageUndoAction` side effects) need care. **[inferred]**
2. **UI-thread blocking**: a slow plugin freezes pen input. The watchdog limits this, but 2 s is still a visible
   stall. This conflicts with "pen feel first" (`VISION.md:13`), so plugins never run during a stroke, only on
   commands.
3. **API surface creep**: `AppController` is being split (TODO.md "Later rounds", `TODO.md:68-73`). Facades must sit
   on stable lower classes (session, canvas, `WindowContext`), or the refactoring keeps breaking the API.
4. **Security holes in exposure** (slots like `deleteLater`, JS ownership of returned objects, file-system imports):
   handled by the wrapper design; tests needed.
5. **Behaviour differences across Qt 6.7/6.8/6.11** in V4 (language features, error messages). Mitigation: CI on
   6.7 and 6.8 and a documented language level.
6. **Multi-window**: a command must act on the window it came from. Undocked windows share the process-wide host.
7. **Mobile**: installing plugins on Android/iOS (zip import), shortcuts are irrelevant there, toolbox space is
   limited. iOS: no JIT (fine).
8. **Maintenance and support**: a public API is a promise. The project says it is alpha with "no compatibility
   promises for its own data" (`qt/docs/README.md:71`); the plugin API needs its own, versioned promise.

### 5.3 Open questions for the author

1. **Do you want plugins at all, and for whom?** VISION.md does not mention plugins or scripting. Is this for your
   own automation, for Xournal++ users coming over, or as groundwork for "agents and CLIs … the app as a tool"
   (`VISION.md:51-53`)?
2. **Lua compatibility**: drop it (recommended) or keep a compat layer for Xournal++ users' plugins?
3. **Undo**: accept one more upstream seam (a ~6-line sink in `UndoRedoHandler::addUndoAction`) for robust "one
   command = one step", or keep plugins to facade-built edits?
4. **On error mid-command**: roll back automatically, or keep the partial change as one undoable step?
5. **Permissions UX**: ask on enable (once), or per first use?
6. **Plugin UI**: are declarative dialogs (message, prompt, choice) enough for v1, or are QML panels from plugins
   needed (a separate engine, more work and more risk)?
7. **Distribution**: only local folders/zip, or a curated list later? Should example plugins be bundled (off by
   default) in the app?
8. **Mobile**: should plugins be offered on Android/iOS in v1, or desktop first?
9. **CLI**: should `xournal-qt-cli` run plugin scripts headless (batch edits/exports)? That reuses the same host
   with no window.

### 5.4 Recommendation

Build **(b) JavaScript plugins on a per-plugin `QJSEngine`**:

- Use manifest-declared commands, a small permission-gated API of fork-owned facades, one undo step per command,
  the watchdog, the log, and hot reload in developer mode. Start with the four blocks above.
- **Do not port upstream's Lua system.** The fork never shipped it. Its API is GTK-bound and grows with upstream,
  which would make merges more expensive. Its plugins expect unsandboxed `io`/`os` and native luarocks modules that
  cannot follow to Android/iOS. Half of the shipped plugins are native features in xournal-qt already or easy to
  hand-port.
- Write the facades so they do not depend on the engine. A Lua binding then stays possible later at moderate cost
  if users ask for it.
- Decide question 1 first. If plugins are not a goal for the author, the right amount of work is none. The research
  shows it is feasible and cheap in dependencies, not that it is needed.
