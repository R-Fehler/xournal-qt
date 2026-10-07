# Decisions

Architecture decision records: why the fork is built the way it is. One file per decision, numbered; a decision that
is replaced keeps its file and says by which.

| ADR | Decision |
| --- | --- |
| [0000](0000-fork-policy.md) | Fork policy: a git fork of Xournal++ with the frontend in `qt/`, upstream files left as they are |
| [0001](0001-ui-host.md) | The canvas is hosted in Qt Quick, not Qt Widgets |
| [0002](0002-upstream-seams.md) | How the Qt build reuses upstream's code: the `qt/compat` shadow headers, the seams in upstream files (each marked `xournal-qt:`), the ports, what is not compiled. A living list: every edit outside `qt/` is recorded there |
| [0003](0003-app-services.md) | What the windows share (`AppServices`, `OpenDocuments`, `BackgroundJobs`) and one `CanvasActions` for every canvas |

Decisions recorded in the feature docs (each where it applies):

- the PDF with notes keeps the drawing as annotations and the Xournal data embedded:
  [features/hybrid-pdf.md](../features/hybrid-pdf.md), "The file";
- a sticky note is a layer of its own, and several notes are selected by a selection of their own:
  [features/sticky-notes.md](../features/sticky-notes.md);
- memory has owners (`CanvasMemory`, `ImageMemory`) and image workers one owner (`ImageWorkers`):
  [architecture/image-caches.md](../architecture/image-caches.md), and [AGENTS.md](../../../AGENTS.md), "What the
  moving parts assume";
- the compatibility with xournal-qt's own older data removed, the text mode removed, the text tool always Markdown:
  [review/2026-10/README.md](../review/2026-10/README.md), "Decisions taken for this round";
- the turning points of the project (the toolbox replacing the classic tool bar, PDFs as documents, …):
  [history/README.md](../history/README.md).

A new ADR: the next number, a title that states the decision, then the context, the decision, what it costs and what
was considered instead. Short.
