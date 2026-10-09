# Plugins (JavaScript)

Plugins add commands to xournal-qt: in menus, the toolbox and on keys; they act on the document, ask with dialogs, and
show a preview on the page. They are JavaScript, each in an engine of its own with nothing but the plugin API: no
files, no network, no programs. Every change a plugin makes is checked against what the user allowed it and is one
undo step; when a command fails, nothing of it stays. Why it is built this way: [ADR 0008](../decisions/0008-js-plugins.md).

## Using plugins

- **Commands**: ⋮ → Tools → **Plugins** lists the commands of the plugins that are on; a command that puts something on
  the page (`"place": "insert"`, like "Plot a function…") is also in ⋮ → Tools beside Image and Sticker, and in the
  toolbox's **+** catalog under Insert (the others under Plugins), to be placed on a bar like the app's own commands
  ([toolbox.md](toolbox.md)). A command that needs a selection is offered only while something is selected.
- **Keys**: a command's key from its manifest is used when it has Ctrl, Alt or Meta and no other action has it;
  Settings → Shortcuts lists the commands in the group **Plugins** (change or add a key there; conflicts are shown as
  for the app's actions).
- **The first time** a command needs a permission, the window asks ("Allow Function plotter? … wants to change what is
  on the pages"); the answer is kept. A command that changed the document shows a note with **Undo** (the whole
  command is one step); one that failed shows why.
- **Dialogs**: a plugin's modal dialog is the app's own dialog (a full-screen form on a phone). A **live dialog**
  stays open beside the page (at the right; a bottom sheet on a phone, the page above it) while the page shows a
  preview in a blue frame: drag the frame to move what will be inserted, its round corner to size it (unless the
  plugin fixed the size). Every change of a field updates the preview; **Insert** puts it on the page as one undo
  step, Cancel (or ×) leaves the page as it was. Another tab or the home screen closes it.
- **Settings → Plugins**: every plugin found (bundled ones and your own), a switch for each, what it may do (each
  permission: *Ask first*, *Allowed*, *Not allowed*), its log, its folder, and **Reload** (the folders read again and
  every plugin's engine made anew: for writing plugins). A plugin that cannot run says why (a broken `plugin.json`, a
  newer API, a module that imports outside its folder).

## The function plotter

A plugin that comes with the app (`qt/resources/plugins/function-plotter`, on by default), offered like the app's own
insert commands: **Plot a function…** in ⋮ → Tools and in the toolbox's "+" catalog (Insert), on **Ctrl+Alt+P**.

- **The dialog** opens beside the page (a bottom sheet on a phone) and the plot is previewed on the page at once, in
  the middle of what is in view, in a frame: drag it to move the plot, its corner to size it. Every change redraws
  the preview. **Insert** puts the plot on the page; the note "Plot a function… · Undo" takes it away again.
- **Functions** are typed as on paper: `x^2 - 2x + 1`, `2x`, `3(x+1)`, `2pi`, `sin x`, `sin 2x` (= sin(2x)),
  `sin^2 x`, `sqrt x`, `|x - 1|`, `exp`, `ln`, `log` (base 10; `log(2, x)`), `lg`, `abs`, `floor`, `min`, `max`,
  `root(x, n)`, `pi`/`π`, `e`; `^` or `²`/`³` for powers, `·` `×` `÷` `−` as typed on a phone; a decimal comma (`1,5x`)
  where it cannot separate arguments (`max(1,5)` is max(1, 5); `max(1,5; 2)` is max(1.5, 2)). A mistake is named under
  the field ("unknown function "sni" - did you mean sin?") and that curve is left out until it is right.
- **+ Function** adds another, **+ Curve x(t), y(t)** a parametric curve (a t range from, to; `2pi` works); ✕ removes
  one. Each has its color (the palette's), its line width and "dashed".
- **Parameters**: any other single letter (`a*x^2 + b`) gets a slider, with its range (from, to): the preview follows
  the slider.
- **Axes**: the x range (`-2pi` … `2pi` works), the y range automatic (fitted to the curves, the far ends of poles left
  out, rounded to ticks) or typed; **π steps** labels the x axis in multiples of π (π/4, π/2, π, …); the axes' names
  (`x`, `y`, or words). **Look**: grid, numbers along the axes (steps of 1, 2, 5 × 10ⁿ, at least a finger apart), arrows,
  the 0 at the origin, the decimal comma (from the language of the system), and the **formulas** next to their curves,
  in a legend, or none. **Mark** the zeros, extrema and intersections (a dot with its coordinates).
- **Exact scale**: "1 unit = 10 mm" (any mm): the plot is as large as its ranges at that scale, printed true to size at
  100 % (a page is in points, 1/72 inch); the frame then keeps its size (the dialog says "Printed at 100 %: 7 × 6 cm").
- **The curves** are sampled adaptively (more points where they bend), broken at poles and jumps (1/x, tan x, floor x:
  no vertical lines), cut exactly at the plot's border and thinned where straight.
- **What it inserts**: ink and Markdown boxes with math, editable as anything else: strokes for the grid, the axes,
  the arrows, the ticks and the curves; boxes (`$…$`, drawn by MicroTeX) for the numbers (`$-2$`, `$\frac{\pi}{2}$`,
  `$1{,}5$`), the axes' names and the formulas (`$f(x) = x^{2} - 2x + 1$`, `\sqrt{x}`, `\frac{a}{b}`, `\sin`) in the
  curve's color. All of it is **one group** in the page's selected layer (the boxes are marked as Markdown in their
  data, `{"xqt:markdown": true}`, so they stay with the ink: a group lies in one layer), so the plot moves, scales and
  deletes as one.
- **Edit plot**: the plot keeps its description (functions, ranges, options, and where its frame was) as the plugin's
  data on the group (`xqt-data`); select the plot and choose **Edit plot…** (⋮ → Tools): the dialog opens with it and
  the frame where the plot is; **Update** replaces it, one undo step. In Xournal++ the plot is plain strokes and texts
  (the boxes show their `$…$` source); saving there drops its description and group.
- Not yet (TODO.md, "Plotting"): polar curves r(θ), an empty coordinate system, points and value tables, shading,
  tangents and derivatives, number lines, piecewise functions, function families.

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
`qt/src/session/SequenceUndoAction.h` (one undo step); in the window `qt/src/app/PluginControl.*` (`app.plugins`: the
commands, the window's operations, the questions, dialogs and the live dialog), the preview in
`CanvasView::setPluginPreview` and `DocumentCanvasItem` (a node above the pages), and the QML parts
`PluginDialogs.qml`, `PluginLiveDialog.qml`, `PluginFrame.qml`, `PluginFields.qml`, `PluginCommandItem.qml`,
`SettingsPlugins.qml`; the commands as shortcuts (`ShortcutsModel::setPluginActions`) and as toolbox items
(`ToolboxModel::setPluginItems`). The function plotter: `qt/resources/plugins/function-plotter` (`main.mjs`; `lib/parse.mjs` the expression language,
evaluation and LaTeX; `lib/ticks.mjs`; `lib/sample.mjs` the adaptive sampling, roots and extrema; `lib/plot.mjs` the
shapes; `lib/spec.mjs` the description and the dialog's fields), copied to `<share>/plugins/` by `XqtPlugins.cmake`.
Tests: `OperationsTest` (label `ops`), `PluginHostTest` and `PlotterTest` (label `plugins`: the parser, LaTeX, ticks,
sampling at poles and jumps, the exact scale, insert and edit as one step each, a plot with a mistake),
`PluginsUiTest` (label `ui`: the menu, the question, the note with Undo, the key, the live dialog with its preview
and frame, Settings → Plugins).

## On the device

- Android: a live dialog is a bottom sheet over the lower part of the window; the page above it scrolls, the frame
  can be dragged with a finger and the pen; the soft keyboard for a field pushes the sheet up and the page stays
  visible above it.
- A tablet in portrait and landscape: the live dialog at the right does not cover the frame where it starts (the
  middle of what is in view); the frame's corner is big enough for a finger.
- The permission question and a plugin's modal dialog close with Android's back key (as "Don't allow" / Cancel).
