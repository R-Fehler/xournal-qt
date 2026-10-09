# ops: the permission-checked operations layer

Target `xqt-ops` (`qt/cmake/XqtOps.cmake`). Every change a plugin makes to a document goes through here
([ADR 0008](../../docs/decisions/0008-js-plugins.md)); later the operations of remote peers and agents too. Headless:
Qt Core and the session, no window, no script engine.

| Class | Job |
| --- | --- |
| `Operations` | the table of an environment: name → operation class, whether it writes, handler. A window adds its own operations (selection, tool, view, dialogs, files) to the document's |
| `Context` | one principal acting on one document: `apply()` checks the operation against the principal's `Authority` (allow, deny, ask on first use), refuses writes to a read-only document, and runs it inside the open transaction (`begin`/`commit`/`rollback`: an `UndoGathering`, one undo step) or in one of its own; the element references it handed out |
| `Principal`, `Authority`, `Patterns` | who acts (the user, a plugin; later a peer, an agent), what it may do, operations named by patterns (`element.*`) |
| `DocumentOps` | the document's operations: `document.read`, `page.read`, `element.list/insert/delete/data`, `layer.add/rename/visible/select`, `page.insert/delete`, `background.set` |
| `Shapes` | elements described as data (stroke, plain text, Markdown box with an anchor), for inserting and for previews |

**Rules**: an operation validates everything before it changes anything (a refused or invalid operation has no side
effects); every change pushes an undo action (upstream's or the fork's), so a transaction can be undone and rolled back
whole; the document lock is held only while the model changes.

**May depend on**: `xqt-session`, `xqt-markdown`, from upstream the model, undo, `control/layer`, `control/pagetype`.
Nothing depends on it but the plugin host and the app.

**Tests**: `qt/tests/ops` (label `ops`). **Docs**: [ADR 0008](../../docs/decisions/0008-js-plugins.md).
