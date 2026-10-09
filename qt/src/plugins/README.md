# plugins: JavaScript plugins

Target `xqt-plugins` (`qt/cmake/XqtPlugins.cmake`). The plugins of the process and how their commands run
([ADR 0008](../../docs/decisions/0008-js-plugins.md), [plugins.md](../../docs/features/plugins.md)). QtQml and QtCore,
no Qt Quick: the window is an interface the app implements (`PluginUi`, and the window's operations), so the CLI can
run plugins too.

| Class | Job |
| --- | --- |
| `PluginHost` | finds the plugins (the bundled folder, then the user's; a user's plugin replaces a bundled one of the same id), keeps which are enabled and what each was allowed (settings, JSON), the log of each, runs a command for a window as one transaction (`Environment`: the window's operations, its document, its `PluginUi`) and reports what went wrong |
| `PluginManifest` | `plugin.json`: id, name, version, `api`, permission classes, commands (title, shortcut, icon, where they show, when) |
| `PluginScript`, `PluginBridge` | one plugin's `QJSEngine` (made on its first command): the API module `"xournal"` built by `api.js` as frozen objects over the bridge, which only the bootstrap holds; each call becomes an operation (`ops::Context::apply`) or a dialog of the window |
| `LiveDialog` | a plugin's non-modal dialog with a preview: the window calls `change()` on every edit (the plugin returns shapes; it may not write) and `insert()` (one transaction) |
| `Watchdog` | interrupts JavaScript that runs longer than 2 s (`QJSEngine::setInterrupted` from its thread); paused while a dialog is open |
| `ImportCheck` | the modules a plugin loads stay inside its folder (static imports only) |

**Rules**: plugins run on the UI thread, never during a stroke; nothing of the engine is exposed but the frozen API;
every change is an operation of `qt/src/ops` inside the command's transaction; a failure rolls back.

**May depend on**: `xqt-ops` (and through it the session), Qt Qml. Nothing depends on it but the app and the CLI.

**Tests**: `qt/tests/plugins` (label `plugins`). **Docs**: [plugins.md](../../docs/features/plugins.md).
