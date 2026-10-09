# session: one open document, headless

Target `xqt-session` (`qt/cmake/XqtSession.cmake`). Everything about a document that needs no view: loading, saving,
undo, the fork's file formats and the PDF machinery. The CLI and the tests use it without a window.

| Class | Job |
| --- | --- |
| `DocumentSession` | one open document (one tab): implements upstream's `Control` (the shadow in `qt/compat`); load, new, annotate a PDF, save (in the background: `DocumentSave`, `DocumentSaveTask`), autosave, undo, page revisions |
| `AppContext` | process-wide state shared by all sessions: upstream's `Settings` (`settings.xml`), the tool in hand (`ToolHandler`), page templates, render workers, the UI-thread dispatcher |
| `SessionActions`, `HeadlessViews` | the session's side of upstream's shadow interfaces (actions, views) |
| `SequenceUndoAction`, `UndoGathering` | several undo steps as one (undone last first); what is pushed onto a session's undo stack meanwhile, gathered into one step or rolled back (a plugin command, [ADR 0008](../../docs/decisions/0008-js-plugins.md)) |
| `FileIo`, `PageCopy`, `DetachedDocument` | one atomic write with fsync, file locks, stamps and hashes; a deep copy of a page for a save; the one event handler of documents no session owns |
| `HybridPdf` (+ `Hybrid*.cpp`) | the PDF with notes: base pages, the ink as annotations, the embedded `.xopp`, the marker |
| `IncrementalPdf`, `PdfObjectSink`, `PdfRevisions`, `PdfHistory`, `ByteDelta`, `VersionCache`, `VersionDiff` | incremental saves, where a write's objects go (a file written in full or an incremental update), the revisions of a file, version history, comparing versions |
| `ArchivePdf`, `PdfEncryption`, `MergedPdf`, `PdfPageKeeper` | PDF/A-3b; password-protected PDFs; the merged background PDF of pasted pages |
| `PdfBookmarks`, `PdfKeywords`, `PdfTitle`, `InkTextLayer` | the outline's bookmarks, keywords as tags, the title, handwriting as invisible text |
| `AdoptAnnotations`, `DocumentAdopt` | annotations of other apps made editable |
| `DocumentSearch`, `DocumentTextIndex`, `TextMatch`, `TextReplace`, `FuzzyQuery`, `FuzzyMatch`, `WordMatch`, `Vocabulary`, `InkText` | search in one document, the fuzzy search, recognised handwriting |
| `TextFile`, `TextDocument`, `DocumentImages`, `PictureSaveHandler`, `DocumentLink`, `DocumentMode`, `PageFiles` | text files and text documents, the pictures of Markdown, links, how documents are kept, pages as files |
| `StickyNote`, `ElementGroups`, `ElementData`, `ElementTimes`, `Timeline`, `PageNoteSpace`, `PageMargins`, `PageBookmarks`, `StickerFile`, `TemplateFile`, `PenFill`, `Tags`, `Citation` | the fork's features in the document model |

**May depend on**: `xoj-render`, `xoj-core`, `xqt-markdown`, `xqt-audio`, Qt Core. From upstream: the model, `undo`,
`control/xojfile` (`LoadHandler`, `SaveHandler`), `control/settings`, `control/layer`, `control/ToolHandler`,
`pdf/base` (export), `view/*` (drawing for exports and PDFs), and the shadows of `control/Control`, `gui/MainWindow`,
`gui/XournalView` in `qt/compat`. No Qt Gui, no views.

**Rules**: never hold the document lock while drawing a PDF; every page has a revision that changes when its picture
does ([AGENTS.md](../../../AGENTS.md), "What the moving parts assume"); every file is written atomically (`FileIo`).

**Tests**: `qt/tests/session` (label `session`). **Docs**: [the PDF with notes](../../docs/features/hybrid-pdf.md) (also
the archive PDF, encryption and version history), and the feature docs named in each header.
