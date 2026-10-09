# Plugins (JavaScript)

Plugins add commands to xournal-qt: in menus, the toolbox and on keys; they act on the document, ask with dialogs, and
show a preview on the page. They are JavaScript, each in an engine of its own with nothing but the plugin API: no
files, no network, no programs. Every change a plugin makes is checked against what the user allowed it and is one
undo step; when a command fails, nothing of it stays. Why it is built this way: [ADR 0008](../decisions/0008-js-plugins.md).

## How a command runs

- The plugins are found in two folders: the bundled ones (`<share>/xournal-qt/plugins/<id>/`, beside the handwriting
  models) and the user's (`<AppData>/plugins/<id>/`: `~/.local/share/xournal-qt/plugins` on Linux; the paths of the
  other platforms in [data-on-disk.md](../architecture/data-on-disk.md)). A user's plugin with the id of a bundled one
  replaces it (for working on a bundled plugin).
- A plugin's engine is made on its first command (nothing runs at start), on the UI thread: a plain `QJSEngine` with
  the API module `"xournal"` and a `console` that writes into the plugin's log (500 lines).
- A command is one **transaction** of the operations layer (`qt/src/ops`): everything it changes is one undo step,
  titled with the command; when the command throws, is refused a permission, or runs longer than **2 s** (the
  watchdog; the time a dialog or question is open does not count), what it did is undone and the user is told why
  ("Function plotter: TypeError: … (main.mjs:12) (its changes were undone)"). A command that changed the document shows
  the window's note with **Undo**.
- **Permissions** are asked the first time a command needs them, per class, and the answer is kept per plugin:

  | Class | Allows | Asked as |
  | --- | --- | --- |
  | (always) | reading the document, the selection and the tool; moving the view; dialogs and notes | — |
  | `edit` | adding, changing and removing ink, texts and Markdown boxes on pages | "change what is on the pages" |
  | `pages` | adding and removing pages and layers, backgrounds, layer names and visibility | "add, remove and change pages and layers" |
  | `tools` | the tool in hand, its color and width | "change the tool in hand" |
  | `files` | reading and writing files the user chooses in a file dialog | "open and save files you choose" |

  A plugin can only be asked for the classes its manifest names; anything else is refused without a question. A "no"
  is kept as well (until changed in Settings → Plugins).

## Writing a plugin

A folder with `plugin.json` and its modules (`.mjs`, ES modules):

```json
{
  "id": "org.example.color-cycle",
  "name": "Color cycle",
  "version": "1.0.0",
  "api": "1.0",
  "author": "Ada",
  "description": "Cycles the pen's color through a list.",
  "main": "main.mjs",
  "enabled": false,
  "permissions": ["tools"],
  "commands": [
    { "id": "cycle", "title": "Cycle the pen's color", "shortcut": "Alt+C", "icon": "cycle.svg",
      "menu": true, "toolbox": true, "place": "plugins", "when": "document" }
  ]
}
```

- `id`: lower-case letters, digits, `.`, `-`, `_` (reverse domain names are a good idea). `api`: the API the plugin
  needs, `major.minor`; it runs when the major is the app's and the minor not newer (this app: **1.0**).
- `permissions`: the classes above it may ask for. `enabled`: on before the user switched it (default off).
- A command's `id` is the name of the function the main module exports; it gets `{command}`. `shortcut`: Qt's key
  names (`Ctrl+Alt+P`); a key the app uses already is not bound. `icon`: an SVG or PNG in the folder, or an icon of the
  app (`xqt-…`). `place`: `"plugins"` (the Plugins menu) or `"insert"` (also with the commands that put things on the
  page). `when`: `"always"`, `"document"` (a document is open) or `"selection"` (elements are selected).
- Modules may import `"xournal"` and files of the plugin's folder (`./lib/x.mjs`); anything else, and `import()`, is
  refused when the plugin loads.
- The language is what Qt 6.7's engine runs: ES2016 with `let`/`const`, classes, arrow functions, modules, `Map`,
  `Set`, typed arrays, `??` and `?.`; **not** `async`/`await` and not `globalThis`. There are no timers.

```js
import { elements, ui } from "xournal";

export function cycle(ctx) {
    const v = ui.dialog({ title: "Lines", fields: [{ id: "n", type: "number", label: "How many", value: 3 }] });
    if (v === null) return;  // cancelled
    const shapes = [];
    for (let i = 0; i < v.n; ++i) shapes.push({ type: "stroke", points: [50, 50 + 20 * i, 300, 50 + 20 * i] });
    elements.insert(undefined, shapes, { group: true });  // (the current page; one undo step with the rest)
}
```

## The API (`import { … } from "xournal"`)

Pages and layers are 0-based indices; `page` left out (or `undefined`) is the current page, `layer` the selected one.
Coordinates are points (1/72 inch) on the page. Errors are thrown as JavaScript errors with a `name`
(`PermissionError`, `TypeError` for bad arguments, `StaleReferenceError`, `ReadOnlyError`).

| Object | Functions |
| --- | --- |
| `xournal` | `apiVersion`, `platform` (`linux`, `windows`, `macos`, `android`, `ios`), `locale` (`de_DE`), `decimalPoint` (`,`), `plugin` ({id, name, version}), `apply(op, args)` (any operation by name), `log(…)` |
| `doc` | `info()` → {pageCount, currentPage, readOnly, file}; `page(i)` → {index, width, height, background: {type, color}, layers: [{name, visible, elements, markdown}], selectedLayer} |
| `elements` | `list(page, {layer, onlyWithData})` → [{ref, type, layer, group, x, y, width, height, color, data}]; `insert(page, shapes, {layer, group, data})` → [ref]; `remove(refs, {withGroups})`; `setData(ref, value)` |
| `layers` | `add(page, {name, above})`; `rename(page, layer, name)`; `setVisible(page, layer, on)`; `select(page, layer)` |
| `pages` | `insert({at, count, background})`; `remove([i, …])`; `setBackground([i, …] or null, type, color)` (types: plain, lined, ruled, graph, dotted, staves, isodotted, isograph) |
| `selection` | `get()` → {page, elements: [{ref, type, group, x, y, width, height, color, data}]} or null; `clear()` |
| `tools` | `get()` → {type, color, width}; `select(type)`; `setColor("#rrggbb")`; `setWidth(points)` |
| `view` | `get()` → {page, zoom, visible: {page, x, y, width, height}}; `goToPage(i)`; `setZoom(percent)` |
| `ui` | `notify(text)`; `dialog(spec)` → values or null; `form(spec)` (a live dialog); `palette()` → [{key, name, color}] |
| `files` | `chooseOpen({title, filters})` / `chooseSave({title, name, filters})` → a handle {id, name} or null; `readText(h)`, `writeText(h, text)`, `exportPdf(h)` |
| `console` | `log`, `info`, `warn`, `error` → the plugin's log |

**References** (`ref`) name an element for the call they were handed out in (a command, one callback of a dialog);
an element that is gone meanwhile gives a `StaleReferenceError`. **Data** on elements: `insert(…, {data})` and
`setData` keep a JSON value under the plugin's id (the element attribute `xqt-data`, which Xournal++ ignores and drops
when it saves); `list` shows a plugin only its own.

### Shapes

```js
{ type: "stroke", points: [x0, y0, x1, y1, …], color: "#1a5fb4", width: 1.4,
  style: "plain" | "dash" | "dot" | "dashdot", closed: false, fill: -1 /* or 0…255 */, cap: "round" | "butt" | "square" }
{ type: "text", x, y, text: "plain text", size: 12, font: "Sans", color, anchor: "top-left" }
{ type: "markdown", x, y, text: "$\\frac{\\pi}{2}$", size: 12, font: "Sans", color, anchor: "top" }
```

`anchor` is the point of the text's box that lands at (x, y): `top-left`, `top`, `top-right`, `left`, `center`,
`right`, `bottom-left`, `bottom`, `bottom-right`. A Markdown box is as wide as its text and goes into the page's
Markdown layer (made when there is none); `$…$` is math. With `{group: true}` the inserted elements of each layer are
one group (ink and boxes are two groups: a group lies in one layer).

### Dialogs

```js
const v = ui.dialog({
    title: "Grid", ok: "Apply", cancel: "Cancel",
    fields: [
        { id: "kind",  type: "choice",   label: "Paper", options: [{ value: "graph", label: "Squared" },
                                                                   { value: "plain", label: "Plain" }], value: "graph" },
        { id: "all",   type: "checkbox", label: "All pages", value: false },
        { id: "size",  type: "number",   label: "Size", value: 5, min: 1, max: 20, step: 1, unit: "mm" },
        { id: "color", type: "color",    label: "Color", value: "#1a5fb4" },
        { id: "note",  type: "text",     label: "Note", value: "", placeholder: "…" },
        { id: "k",     type: "slider",   label: "k", value: 1, min: -5, max: 5, step: 0.1 },
        { type: "label", text: "A line of explanation" }
    ]
});
```

A live dialog (`ui.form`) stays open beside the page and shows a **preview**: `change(values, ctx)` is called when it
opens, on every edit, when a button field (`{type: "button", id, label}`) is pressed (`ctx.action` is its id) and when
the frame on the page is moved or sized (`ctx.frame` = {page, x, y, width, height}); it returns `{shapes, errors:
{fieldId: "message"}, fields, values, frame, message}` (all optional: new `fields` replace the form, `frame` sets the
frame's size). It may not change the document. `insert(values, ctx)` runs on Insert as one transaction. Fields may
carry `row` (fields with the same row share a line) and `width`.

```js
ui.form({ title: "Plot", insertLabel: "Insert", fields: […], values: {…},
          frame: { width: 240, height: 180 /* , page, x, y, resizable */ },
          change: function (values, ctx) { return { shapes: draw(values, ctx.frame) } },
          insert: function (values, ctx) { elements.insert(ctx.frame.page, draw(values, ctx.frame), { group: true }) } })
```

## From Xournal++'s Lua plugins

Xournal++'s Lua plugins do not run here; porting one is mostly mechanical:

| Xournal++ (Lua) | xournal-qt (JavaScript) |
| --- | --- |
| `plugin.ini` (`[about]`, `[plugin] mainfile`) | `plugin.json` (`name`, `version`, `main`, `permissions`, `commands`) |
| `function initUi() app.registerUi{menu=…, callback="f", accelerator="<Control>t"} end` | a command in `plugin.json` (`{"id": "f", "title": …, "shortcut": "Ctrl+T"}`) and `export function f() {…}` |
| `app.getDocumentStructure()` | `doc.info()`, `doc.page(i)` (0-based pages and layers; Lua counts from 1) |
| `app.addStrokes{strokes=…, allowUndoRedoAction="grouped"}` | `elements.insert(page, [{type: "stroke", points: […]}])` (always one undo step with the rest of the command) |
| `app.addTexts{…}` | `elements.insert(page, [{type: "text", …}])` or `{type: "markdown", …}` |
| `app.getStrokes("selection")`, `app.getTexts` | `selection.get()`, `elements.list(page)` |
| `app.changeToolColor{color=0xff0000}` | `tools.setColor("#ff0000")` |
| `app.changeCurrentPageBackground("graph")` | `pages.setBackground(null, "graph")` |
| `app.layerAction("ACTION_NEW_LAYER")`, `app.setCurrentLayerName` | `layers.add(page, {name})`, `layers.rename(…)` |
| `app.setPageSize`, `app.activateAction("…")` | an operation where one exists (`xournal.apply("page.insert", …)`); actions of the GTK app have no equivalent |
| `app.openDialog(msg, buttons, cb)`, `app.msgbox` | `ui.dialog({title, fields})` (returns the values), `ui.notify(text)` |
| `app.fileDialogSave`, `io.open` | `files.chooseSave()` then `files.writeText(handle, text)` (no other file access) |
| `io`, `os`, `require` of luarocks modules | none: plugins are sandboxed; split your code into modules of the folder (`import … from "./x.mjs"`) |

## Code and tests

`qt/src/plugins` (the host, the engine, the bridge, `api.js`), `qt/src/ops` (the operations, shapes, transactions),
`qt/src/session/SequenceUndoAction.h` (one undo step). Tests: `OperationsTest` (label `ops`), `PluginHostTest`
(label `plugins`).
