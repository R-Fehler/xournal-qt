# Page templates (`qt/templates`)

The author (2026-10-04, A12 of [the ideas of 2026-10](history/README.md)): "Save page as template", chosen when adding
pages; "the template should include the pdf page / background if wanted by the user so effectively it's the same as
copy pasting a pdf page."

Built on the stickers ([stickers.md](stickers.md): the same folder model, own order, last used, picker) and on page
copy and paste (`PageClipboard`: copied pages pasted into any tab, their PDF pages joining the document's merged PDF,
`qt/pdf-pages`).

## Where templates are

- **The library's set:** the visible folder `Templates/` at the root of the library (a fixed English name, the same in
  every language), with subfolders for topics ("Lecture 3"), one level or more.
- **The app-wide set** ("In all libraries"): `<AppDataLocation>/templates/` (Linux: `~/.local/share/xournal-qt/templates/`).
- **A template** is one `.xopp` file of one page (its name is the template's name). A picture is not a template.
- **Own order:** a hidden `.template-order.json` in each template folder (it syncs with the folder). **Last used:** per
  library in its config folder (`templates.json`), by the template's path.

## A template file

A normal `.xopp` of one page that Xournal++ opens as it is (upstream's LoadHandler reads it without a warning):

- **With the page's background** (the default): the page's background as it is: ruled, graph, dotted, plain with its
  colour, a picture, or **a PDF page**. A PDF page goes along as upstream's attached PDF of the `.xopp`:
  `name.xopp.bg.pdf` next to it, holding only that one PDF page (copied with qpdf, as copying the page copies it:
  `PageClipboard::copy`, then `copied()`), referred to as domain `attach`, file `bg.pdf`. Xournal++ opens the pair
  wherever it is put. If the PDF page cannot be copied, the page gets a picture of it (200 dpi), as a pasted page does.
- **Without its background:** plain white paper whose background carries the name
  `xournal-qt: template without background` (saved; Xournal++ keeps it and does not show it). Added to a document, the
  page gets the background new pages get there: the current page's paper and colour, or, where that is a PDF page or a
  picture, the background chosen for new pages.
- **With its content** (the default): every layer as it is: ink, text, pictures, LaTeX, Markdown boxes (the page's
  `Markdown` layer), sticky notes. **Without it:** the layers stay, empty; no notes, no Markdown layer.
- The page keeps its size and its space for notes beside a slide ([note-space.md](note-space.md)); its bookmark does
  not go along. Written with the app's `PictureSaveHandler` (the pictures of its Markdown boxes travel inside it) with a
  preview of its page.

## Saving a page as a template

- **Where:** ⋮ → Page → "Save page as template…" (the current page); the page sidebar's and the page grid's page menu
  (the template icon beside "Start a chapter here…": that page); the add-page button's list, "Save this page as
  template…"; the template picker's "+ Save this page". All open the window's one dialog.
- **The dialog:** the name (the document's name and the page number, "Lecture, page 3"), the folder (the set's root,
  one of its folders, or a new one typed), "With the page's background" (it says what that is: its PDF page, its
  picture, its paper), "With its content", "In all libraries" (the app-wide set instead of the library's). Save needs
  a name and at least one of background and content.
- A name that is taken becomes "name (2)" (also when only an old `name.xopp.bg.pdf` is left there). The file is
  written off the UI thread; "Saved template “name”" says it worked. Saving changes nothing in the document (it works
  in a document opened for reading only too). Not in a Markdown or text document.

## Using a template

Adding a template's page is pasting a copy of that page: one undo step, at the current position. A PDF page joins the
document's merged background PDF (its text stays searchable and selectable; in a `.xopp` it is saved next to it, in a
PDF with notes inside it), exactly as for a page copied from another tab ([PageClipboard](../src/shell/PageClipboard.h),
`qt/pdf-pages`). Added several times, the PDF page is added to the merged PDF once.

- **The add-page button's list** (press and hold, or right-click): "Background, size, several pages…" (the Insert pages
  dialog), the five templates used last (a tap adds that page after the current one), "From a template…" (the
  picker), "Save this page as template…". A tap on the button still adds a page like the current one.
- **⋮ → Page → "Add a page from a template…"**: the picker; a tap adds the page after the current one.
- **The Insert pages dialog:** "New pages" or "From a template" (Choose… opens the picker to pick one); how many and
  before or after which page as for new pages; Insert adds them, one undo step.
- **New document:** "Blank" or "From a template": the new document's first page is the template's page (nothing to
  undo); saved in the library at once as any new document (as a PDF with notes in PDF files mode).
- **The picker** is the sticker picker in its template mode (`StickerPicker.qml`, `mode: "templates"`): "This library"
  / "All libraries", folders as chips, search, Last used / Own order / Name / Date added, the previews of the files
  (the library's preview cache), the same card menu (rename, reorder, move to a folder, open to change it, copy to all
  libraries or another library, delete). Renamed, moved, copied or deleted, a template's `name.xopp.bg.pdf` goes along.
- Not offered in a document opened for reading only, a text file's pages, a Markdown or text document.

## In the library

`Templates/` is an ordinary folder of the library: its templates are documents (cards with previews, found by the
search, opened, shared). Its folder card has a template mark. The attached PDF is part of its `.xopp` (not a card of
its own), as for any `.xopp` with an attached PDF.

## Code

| Where | What |
| --- | --- |
| `qt/src/session/TemplateFile.*` | The template's page from a copy of a page (`makePage`: options background, content), writing it with its attached PDF (`write`), `withoutBackground`. Any thread. |
| `qt/src/shell/PageClipboard.*` | `copy(Document&, …)` (a document not open: the template file read on a worker), `copied(i)` (the copied page, its PDF page, the one-page PDF). |
| `qt/src/shell/Stickers.*` | The sets of both kinds (`stickers::Kind::Templates`: `Templates/`, `templates/`, `.template-order.json`, `.xopp` only, the attached files following a `.xopp`: `companionsOf`); `StickersModel(Kind)`, `recent(n)`. |
| `qt/src/app/AppTemplates.cpp` | `app.templates`, `templateDraft`, `saveTemplate`, `insertTemplate`, `createDocumentFromTemplate`; reading a template off the UI thread (`readTemplate`), the pages for a document (`templatePagesFor`). |
| `StickerPicker.qml` (`mode: "templates"`, `pickOnly`), `TemplateSaveDialog.qml` | The picker, the dialog. `AppButtons.qml`: the add-page button's list; `MoreMenu.qml`: ⋮ → Page; `PageMenu.qml`; `InsertPagesDialog.qml`, `NewDocumentDialog.qml`: "From a template". |

## Not built

- A template of several pages (a whole lecture sheet set). One page per template for now.
- Previews of the templates in the Insert pages and New document dialogs (they show the name of the one chosen; the
  picker shows the previews).
- Templates as page backgrounds for the setting "new pages" (Add page always copies the current page's paper).
