# ADR-0008: Plugins are JavaScript in a QJSEngine of their own, acting through a checked operations layer

- Status: **Accepted** (the author's decisions of 2026-10-09, in two rounds). The research behind it:
  [review/2026-10/js-plugins-research.md](../review/2026-10/js-plugins-research.md); the collaboration research it
  shares the operations with: [research/collaboration.md](../../research/collaboration.md). How it works and the API:
  the feature doc `features/plugins.md` (with the block `qt/js-plugins`).

## Context
Xournal++ has Lua plugins: commands in menus and the tool bar, shortcuts, dialogs, and an `app.*` API of about 56
functions bound to its GTK window. The Qt build never compiled them (`ENABLE_PLUGINS OFF`). The upstream API cannot
be reused (it includes GTK and the GUI classes), it hands plugins the whole Lua standard library (`io`, `os`), and a
plugin command is not one undo step. xournal-qt wants plugins "similar to Xournal++'s plugin features, but cleaner",
with a function plotter for teachers and students as the first one, on the desktop and on Android. Later, remote
peers (collaboration) and agents (an MCP server) will change documents too, under permissions.

## Decision
- **JavaScript only, in `QJSEngine`** (QtQml, linked already; no new library): one engine per plugin, made on the
  plugin's first use, on the UI thread. Not the app's `QQmlEngine`: that one reaches `app` and everything QML can do.
  Lua is dropped; upstream's Lua system stays unbuilt, and the feature doc has a migration guide for Lua plugin
  authors.
- **A plugin is a folder** with `plugin.json` (id, name, version, `api`, the permission classes it may ask for, its
  commands with their titles, shortcuts and where they show) and ES modules. Commands are declared, so menus, toolbox
  entries and shortcuts exist before any JavaScript runs. Bundled plugins are installed like the handwriting models
  (`share/xournal-qt/plugins/<id>/`); the user's are in the app's data folder (`<AppData>/plugins/<id>/`).
- **Nothing but the API**: a plain `QJSEngine` has no file, network, process or timer API, and none is added. The API
  is the module `"xournal"`: frozen JavaScript objects made by a bootstrap script; each function calls one native
  bridge object that the plugin never sees (every slot of an exposed `QObject` is callable, `deleteLater` included).
  Module imports are checked before loading: only files inside the plugin's folder, no dynamic `import()`. Files are
  reached only through a file dialog the user answers, as handles.
- **The operations layer is a module of its own** (`qt/src/ops`, target `xqt-ops`; Qt Core and the session only).
  Every change goes through it as an **operation**: a name from the collaboration vocabulary (`stroke.insert`,
  `text.insert`, `markdown.insert`, `element.delete`, `element.data`, `layer.add`, `page.insert`, `page.delete`,
  `background.set`, …), plain data as arguments, and an **operation class**. An operation is applied for a
  **principal** (the local user, a plugin; later a remote peer or an agent), checked by the principal's **authority**
  (allow, deny, or ask the user), applied through the session's undo machinery, and part of a **transaction** (one
  undo step, or nothing when it fails). An operation validates everything before it changes anything, so a refused
  or invalid one has no side effects. The table of operations is per environment: a window adds its own (the
  selection, the tool in hand, the view, dialogs, files) to the document's; the CLI only the document's.
- **Permissions are asked on first use** of an operation class (not at install), remembered per plugin and revoked in
  Settings → Plugins. A plugin's capabilities are the classes its manifest names, each granted by the user's first-use
  answer; a class the manifest does not name is refused without asking. Reading the open document, moving the view
  and showing dialogs need no permission: a plugin can send nothing anywhere.
- **One command, one undo step; a failure rolls back**: everything a command (or a callback of its dialog) changes is
  one transaction, a `SequenceUndoAction` titled with the command. An exception, a refused permission, or the
  **watchdog** (`QJSEngine::setInterrupted` from a helper thread after 2 s of running JavaScript; the time a dialog is
  open does not count) undoes what was done so far, and the user is told. A successful command that changed the
  document shows the window's toast with **Undo**.
- **Gathering the undo steps is a seam**: `UndoRedoHandler::addUndoAction` hands the action to a sink while one is set
  (about 15 lines, `xournal-qt:`, ADR 0002). So existing commands and upstream's own undo actions can be used by the
  operations and still end as one step.
- **Plugin input is dialogs, described as data and drawn by the app** (no plugin QML, no panels in v1): fields (text,
  number, color from the palette, choice, checkbox, slider, button) in the app's own dialog, a bottom sheet on
  phones. A dialog is modal (its answer returns to the command) or **live**: non-modal, placed beside the page so the
  page stays visible, calling the plugin on every change and drawing the shapes it returns as a **preview** on the page
  (with upstream's views, as they will look when inserted) inside a frame the user drags and sizes, until Insert.
- **Plugin data on elements**: a plugin may keep a JSON value on an element (the plotter keeps the plot's description
  on its groups), saved as the element attribute `xqt-data` (a seam like `xqt-group`; upstream ignores and drops it,
  the elements stay).
- **The function plotter is a bundled plugin** (the test case of the whole surface: a live dialog, computing points,
  inserting ink and Markdown boxes through the operations layer) but **surfaced like a core feature**: "Plot a
  function…" where things are inserted (⋮ → Tools and the toolbox's "+" catalog), not only under Plugins. Its labels
  are Markdown boxes with math (`$…$`, MicroTeX), so axis names, tick numbers (`$\frac{\pi}{2}$`) and the functions'
  formulas look like a textbook's; a plot is two groups (its ink, and its boxes in the page's Markdown layer).
- **Headless-ready**: the engine, the operations layer and transactions depend on QtQml, QtCore and the session
  only; the window's part is a set of operations the app registers. `xournal-qt-cli plugin run` runs a plugin command
  on documents with permissions given by flags and dialog fields by `--set`.

## The operations, and how collaboration and agents use them later
The collaboration research checks **operations against capabilities** (§4): every peer checks each incoming
operation against the signed capability of its author, by operation patterns. The plugin layer uses the same names:

| Class (manifest `permissions`) | Operations (patterns) | Asked as |
| --- | --- | --- |
| `read`, `view`, `ui` (always) | `document.read`, `page.read`, `element.list`, `selection.read`, `tool.read`, `view.*`, `ui.*` | — |
| `edit` | `element.*`, `stroke.*`, `text.*`, `markdown.*`, `image.*` | "change what is on the pages" |
| `pages` | `page.insert/delete/move/size`, `background.*`, `layer.add/delete/rename/visible/select` | "add, remove and change pages and layers" |
| `tools` | `tool.select`, `tool.color`, `tool.width` | "change the tool in hand" |
| `files` | `file.*` | "open and save files you choose" |

The extension path, built only as far as plugins need it now:
1. **Principals**: `Principal::Kind` already has `Peer` and `Agent`. A peer's authority checks a signed delegation
   (`{to: device key, allowed: [patterns], until}`) instead of a plugin's grants; an agent's is the user's delegation
   to it (a role preset such as "agent: `markdown.*` and `element.data` on its own layer").
2. **Capabilities by pattern**: `Patterns` matches `stroke.insert`, `element.*`, `*`; a plugin's class maps to the
   patterns above, so a plugin's grant, a peer's capability and an agent's role are the same kind of thing. Operations
   that check their parts (`element.insert` checks `stroke.insert`, `text.insert`, `markdown.insert` per element) make
   finer capabilities possible without new operations.
3. **Remote operations**: a peer's signed operation is decoded into the same `{name, args}` and applied with
   `Context::apply` for the peer principal, inside a transaction per received batch. What the layer lacks for that:
   stable element ids (`xqt-id`, collaboration §1) instead of the per-context references, and inverse operations by id
   for undo of one's own operations only.
4. **Agents**: an MCP server (`xournal-qt-cli mcp`) offers operations (and plugin commands) as tools; each tool call is
   `Context::apply` for the agent principal, a session's calls a transaction the user can undo.

## Consequences
- Plugins cannot slow the app at start (no engine until used), and a stuck plugin stops after 2 s with its changes
  undone; the UI thread is blocked for that time at worst (plugins never run during a stroke, only on commands).
- A plugin cannot read or write files, the network or the library behind the user's back; a document change always
  arrives as one undo step, through the one layer that later serves peers and agents.
- Two small upstream seams (the undo sink, `xqt-data`). `.xopp` files stay readable by Xournal++; a plot there is
  plain strokes and texts (the boxes show their Markdown source; Xournal++ drops the groups and the plot's description
  when it saves).
- The API is ours and versioned (`"api": "1.0"`: a plugin runs when the major matches and the minor is not newer);
  Lua plugins must be ported by hand.

## Considered
- Upstream's Lua API with a compatibility layer: GTK-bound, grows with every upstream release, needs `io`/`os` for
  half of its plugins; about three blocks plus work at every upstream merge (research §4).
- Lua and JavaScript both: the cost of both.
- The app's `QQmlEngine`, and plugin QML for dialogs or panels: a sandbox would be needed around everything QML reaches
  (`Qt.openUrlExternally`, `XMLHttpRequest`, `app`); described dialogs cover the plotter.
- The checks inside the plugin host instead of a layer of its own: then peers and agents would need a second one.
- Building every operation's undo steps without the seam: works for a handful of operations, but repeats the code of
  each command and breaks when one changes.
- Re-running a command after a permission was granted instead of asking in the middle (a nested event loop): repeats
  side effects outside the document (the plugin's own state, the tool in hand).
- Promises for dialogs: QJSEngine runs promise jobs only from the event loop, after the command returned, so the
  changes of a dialog would not be part of the command's step; `async`/`await` (ES2017) is not in Qt 6.7's engine.
- Plain Xournal++ texts for the plot's labels: one group with the ink, but no math (π, fractions, the formulas);
  the author chose Markdown boxes with math.
