# ADR-0008: Plugins are JavaScript in a QJSEngine of their own, acting through checked operations

- Status: **Accepted** (the author's decisions of 2026-10-09). The research behind it:
  [review/2026-10/js-plugins-research.md](../review/2026-10/js-plugins-research.md). How it works and the API: the
  feature doc `features/plugins.md` (with the block `qt/js-plugins`).

## Context
Xournal++ has Lua plugins: commands in menus and the tool bar, shortcuts, dialogs, and an `app.*` API of about 56
functions bound to its GTK window. The Qt build never compiled them (`ENABLE_PLUGINS OFF`). The upstream API cannot
be reused (it includes GTK and the GUI classes), it hands plugins the whole Lua standard library (`io`, `os`), and a
plugin command is not one undo step. xournal-qt wants plugins "similar to Xournal++'s plugin features, but cleaner",
with a function plotter for teachers and students as the first one, on the desktop and on Android.

## Decision
- **JavaScript only, in `QJSEngine`** (QtQml, linked already; no new library): one engine per plugin, made on the
  plugin's first use, on the UI thread. Not the app's `QQmlEngine`: that one reaches `app` and everything QML can do.
  Lua is dropped; upstream's Lua system stays unbuilt, and the feature doc has a migration guide for Lua plugin
  authors.
- **A plugin is a folder** with `plugin.json` (id, name, version, `api`, the capabilities it may ask for, its
  commands with their titles, shortcuts, and where they show) and ES modules. Commands are declared, so menus,
  toolbox entries and shortcuts exist before any JavaScript runs. Bundled plugins are installed like the handwriting
  models (`share/xournal-qt/plugins/<id>/`); the user's are in the app's data folder (`<AppData>/plugins/<id>/`).
- **Nothing but the API**: a plain `QJSEngine` has no file, network, process or timer API, and none is added. The API
  is the module `"xournal"`: frozen JavaScript objects made by a bootstrap script; each function calls one native
  bridge object that the plugin never sees (every slot of an exposed `QObject` is callable, `deleteLater` included).
  Module imports are checked before loading: only files inside the plugin's folder, no dynamic `import()`. Files are
  reached only through a file dialog the user answers, as handles.
- **Every call goes through one operation layer**: an operation has a name from the vocabulary below, plain data as
  arguments and an **operation class**. The layer checks the class against the plugin's permissions, records the undo
  step and changes the document. The facades are thin (they build the arguments); the layer is independent of the
  engine (`QJSValue` in, `QJSValue` out at its edge, plain C++ inside), so a later Lua or CLI binding sits on it too.
- **Permissions are asked on first use** of an operation class (not at install), remembered per plugin and revoked in
  Settings → Plugins. The classes are the manifest's `permissions`; a class the manifest does not name is refused
  without asking. Reading the open document, moving the view and showing dialogs need no permission: a plugin can
  send nothing anywhere.
- **One command, one undo step; a failure rolls back**: everything a command (or a callback of its dialog or side
  form) changes is gathered into one `SequenceUndoAction` titled with the command. An exception, a refused permission,
  or the **watchdog** (`QJSEngine::setInterrupted` from a helper thread after 2 s of running JavaScript; the time a
  dialog is open does not count) undoes what was done so far, and the user is told. A successful command that changed
  the document shows the window's toast with **Undo**.
- **Gathering the undo steps is a seam**: `UndoRedoHandler::addUndoAction` hands the action to a sink while one is set
  (about 10 lines, `xournal-qt:`, ADR 0002). So every existing command and upstream's own undo actions can be used by
  the operations and still end as one step. Without the seam, every operation would have to rebuild its undo actions
  outside the code that already does them.
- **Plugin input without plugin QML**: (a) modal dialogs and (b) a non-modal side form with live preview, both
  described as data (fields: text, number, color from the palette, choice, checkbox, slider, button) and drawn by the
  app's own QML; the side form is the Markdown source panel's place (beside the page, below it on phones and portrait
  tablets). The preview is a set of shapes the plugin returns, drawn by the canvas with upstream's views (as they will
  look when inserted) inside a frame the user drags and sizes.
- **Plugin data on elements**: a plugin may keep a JSON value on an element (the plotter keeps the plot's
  description on its group), saved as the element attribute `xqt-data` (a seam like `xqt-group`; upstream ignores and
  drops it, the elements stay).
- **Headless-ready**: the engine, the operation layer and the transaction depend on QtQml, QtCore and the session
  only (no Qt Quick, no window); the window's part (selection, tools, dialogs, the side form) is an interface the app
  implements. `xournal-qt-cli` can run plugin commands later with an implementation of its own.

## Operations, and the link to collaboration
The collaboration research (E2E-encrypted peers, agents through MCP) checks **operations against capabilities**:
`stroke.insert`, `element.delete`, `element.transform`, `markdown.edit`, `page.insert/delete/move`, `background.set`,
`layer.add`, …. The plugin layer uses the same names and groups them into the permission classes:

| Class (manifest `permissions`) | Operations | Asked as |
| --- | --- | --- |
| (none: always) | `document.read`, `page.read`, `selection.read`, `tool.read`, `view.goto`, `view.zoom`, `ui.notify`, `ui.dialog`, `ui.form` | — |
| `edit` | `element.insert` (`stroke.insert`, `text.insert`), `element.delete`, `element.transform`, `element.data` | "change what is on the pages" |
| `pages` | `page.insert`, `page.delete`, `page.size`, `background.set`, `layer.add`, `layer.rename`, `layer.visible`, `layer.select` | "add, remove and change pages and layers" |
| `tools` | `tool.select`, `tool.color`, `tool.width` | "change the tool in hand" |
| `files` | `file.open`, `file.save`, `file.export` | "open and save files you choose" |

How the three meet later: the operation layer (`PluginOps`) is one table of `{name, class, handler}`; a plugin
command, an MCP tool call and a remote peer's operation each become a **principal** (a plugin with its permissions,
an agent with its delegation, a peer with its signed capability) and an operation call; the same table checks the
class against the principal and applies it with undo. What a plugin may do is then expressible as a capability
(a role preset like "agent: `edit` on its own layer"), and a plugin's command can be offered to an agent as a tool.
v1 only has the plugin principal; the names are chosen so nothing is renamed then.

## Consequences
- Plugins cannot slow the app at start (no engine until used), and a stuck plugin stops after 2 s with its changes
  undone; the UI thread is blocked for that time at worst (plugins never run during a stroke, only on commands).
- A plugin cannot read or write files, the network or the library behind the user's back; a document change always
  arrives as one undo step.
- Two small upstream seams (the undo sink, `xqt-data`). `.xopp` files stay readable by Xournal++; a plot there is
  plain strokes and texts (Xournal++ drops the group and the plot's description when it saves).
- The API is ours and versioned (`"api": "1.0"`: a plugin runs when the major matches and the minor is not newer);
  Lua plugins must be ported by hand.

## Considered
- Upstream's Lua API with a compatibility layer: GTK-bound, grows with every upstream release, needs `io`/`os` for
  half of its plugins; about three blocks plus work at every upstream merge (research §4).
- Lua and JavaScript both: the cost of both.
- The app's `QQmlEngine`, and plugin QML for the forms: a sandbox would be needed around everything QML reaches
  (`Qt.openUrlExternally`, `XMLHttpRequest`, `app`); described forms cover the plotter. Plugin QML in a separate engine
  can come later.
- Building every operation's undo steps in the facades (no seam): works for a handful of operations, but repeats the
  code of each command and breaks when one changes.
- Re-running a command after a permission was granted instead of asking in the middle (a nested event loop): repeats
  side effects outside the document (the plugin's own state, the tool in hand).
- Promises for dialogs: QJSEngine runs promise jobs only from the event loop, after the command returned, so the
  changes of a dialog would not be part of the command's step; `async`/`await` (ES2017) is not in Qt 6.7's engine.
