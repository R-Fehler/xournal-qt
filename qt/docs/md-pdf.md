# Text documents as PDF (`qt/md-pdf`)

Status: design agreed with the author 2026-09-25/26 (TODO.md, "Ideas round of 2026-09-25/26", "Markdown inside the
PDF with notes"); built as `qt/md-pdf`. Images in Markdown (`name.assets/…`) are the next block, `qt/md-images`.
The user guide for getting the Markdown out again is [user/markdown-from-pdf.md](user/markdown-from-pdf.md).

## One model

- A document is pages. A Markdown text is a **flow** over a run of pages: the page's Markdown text of
  [markdown-boxes.md](markdown-boxes.md), one part per page, the parts after the first starting with
  `<!-- xqt:cont … -->` (`MdPaginate`).
- A **`.md` file** is a document with exactly one flow and nothing else ([md-editor.md](md-editor.md)).
- A **PDF text document** is the notes model: a flow that starts on page 1, plus any ink, saved as a PDF with notes
  (a hybrid PDF, [hybrid-pdf.md](hybrid-pdf.md)). The pages show the typeset text; the embedded `document.xopp`
  keeps it editable. Nothing marks such a file apart from that: **a notes document whose page 1 starts the page's
  Markdown text is a text document** (`TextDocument::isTextDocument`). "Edit as notes" of a `.md` makes one too.
- **Ink stays where it was drawn** when the text reflows (the author, 2026-09-26): there is no anchoring. Whoever
  writes on a text page and then changes the text above it moves the ink themselves (select and move it, or insert
  a page). This is documented, not engineered around.
- The page break `<div style="page-break-after: always"></div>` ends a page of the flow, as in the `.md` editor
  (`MdPaginate` honours it).

## Which kind a new text document is

- Setting `newTextDocuments` in the `xournalQt` part of `settings.xml`: `pdf` (a PDF text document, `name.pdf`) or
  `md` (a Markdown file, `name.md`). While it is not stored it follows the way documents are kept (DocumentMode):
  "PDF files" → `pdf`, "Xournal++ files" → `md`. Settings → Documents: **New text documents: PDF document /
  Markdown file** (`DocumentMode::newTextDocuments`).
- The library's new menu: **New text document…** (the setting says which; the name dialog shows `.pdf` or `.md`)
  and **New text file…** (`.txt`, as before). With the setting on `md` the entry reads "New Markdown file…" as
  before.
- A PDF text document is made in the current folder as `name.pdf` (a free name, as New document), saved at once as a
  PDF with notes, opened, and the cursor is put into its text.
- **Mixing works:** existing `.md` files are never converted unasked (they open in the `.md` editor, whatever the
  setting), and `.md` files and PDF text documents are separate cards side by side in the library.

## The portable `name.md` inside the PDF

- A PDF text document carries, next to `document.xopp`, a plain **`name.md`**: the flow's Markdown (the parts joined,
  without the `<!-- xqt:cont -->` lines: `md::join`), `name` being the PDF's file name without `.pdf` (an archive
  PDF's `name.archive.pdf` gives `name.md`). It is written again on every save, full and incremental, from the
  document as saved. Subtype `text/markdown`, description "The text of this PDF as Markdown".
- Whoever gets the PDF extracts it with any PDF viewer's attachment list or `qpdf --show-attachment=name.md`
  ([user/markdown-from-pdf.md](user/markdown-from-pdf.md)).
- **Archive export** (PDF/A-3): the same file is an associated file with `/AFRelationship /Alternative` (an
  alternative representation of the content), next to `document.xopp` (`/Source`).
- **The hook:** `TextDocument::attachments(document, pdfName)` gives the files a hybrid PDF carries for other apps;
  `HybridPdf` asks it while it prepares a write (under the document's lock) and writes them as embedded files. They
  are listed in the marker's `/Files` with the attached images, so the clean copy (the background when opening)
  never carries them and each write puts the current ones. `qt/md-images` adds `name.assets/…` there.
- An incremental save replaces the data of `name.md` in its file specification (a new stream, as for the
  `document.xopp`). When the set of attachments changes (the first save of a text document written by an older
  build, the file renamed, the text removed), the file is written anew in full once.

## Conversions

- **Export as Markdown** (⋮, shown for a PDF text document and for any notes document with a page Markdown text):
  writes `name.md`, the flows of the document in page order (several flows joined with a page break between
  them). In Xournal++ files mode it goes next to the document (asking before it replaces a file); in PDF files
  mode, and for a document not saved yet, a save dialog asks where (nothing is written next to files unasked).
  Images: `name.assets/` once `qt/md-images` is there.
- **Open as PDF document** (⋮ of a `.md`): a new PDF text document from the text
  as it is now (unsaved changes included), built as "Edit as notes" builds its notes, saved at once as a PDF with
  notes next to the `.md` (`name.pdf`, or `name (2).pdf` when that name is taken), and opened with the cursor in the
  text. The `.md` is not touched.

## Editing a PDF text document

- It opens in the notes model: the flow is the page's Markdown text written on the page (WYSIWYG), the pen writes
  ink on top, and the formatting bar (`qt/md-toolbar`) is shown as for a `.md`.
- **Typing goes into the flow without a click:** a key typed while nothing is being written starts writing the
  flow at the end of what the page in view holds of it (as the writing button starts it; on the flow's last page at
  the very end of the text, and when the page in view is after the flow, there too). The formatting bar's tools do
  the same. The pen, the mouse and a finger keep their tools (ink, select, scroll): the text is written with the
  keyboard, or placed with a tap of the text tool on it, and Escape ends writing. (Decision: the `.md` editor's
  "every tool puts the cursor" would take the pen away from the ink, which is the point of a text document in the
  notes model; and the end of the page's part, not its top as in a `.md`, so that typing again after Escape goes
  on where the text ended, and a new document's first key needs no cursor placed.)
- A new text document (New text document…, Open as PDF document) opens with the cursor in its text.

## The library

- A PDF text document is a hybrid PDF: one card, badge "PDF" (no separate badge: looking into every lone PDF to
  find its kind would cost a qpdf parse per file; the index could record it later).
- Search: the library's index reads a hybrid PDF's embedded document, whose Markdown boxes' text is indexed like
  every text element, so the words of the flow are found (tested).

## Files

| File | What |
| --- | --- |
| `qt/src/session/TextDocument.*` | the flow of a document (`isTextDocument`, `flowText`, `markdown`), the attachments, the name |
| `qt/src/session/HybridPdf.cpp` | the attachment hook: written with the data, replaced on incremental saves, stripped from the clean copy |
| `qt/src/session/DocumentMode.*` | the `newTextDocuments` setting |
| `qt/src/app/AppTextFiles.cpp` | `createTextDocument`, `openAsPdfDocument`, `exportMarkdown` |
| `qt/src/canvas/CanvasView.cpp`, `qt/src/quick/DocumentCanvasItem.cpp` | typing into the flow |
| `qt/src/app/qml/HomeView.qml`, `Main.qml`, `SettingsPage.qml` | the menus and the setting |
| `qt/tests/session/TextDocumentTest.cpp`, `qt/tests/ui/MainWindowTest.cpp` | the tests |

## Not yet

- Images (`qt/md-images`): `name.assets/…` attachments, packed by "Open as PDF document", unpacked by "Export as
  Markdown".
- A "text" badge on the library card.
- Ink anchored to text (decided against for now).
