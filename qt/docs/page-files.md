# Pages as files (`qt/page-files`)

The author (2026-10-05): "A6-A8 sound good" (ideas-2026-10.md: insert pages from a PDF file, extract or split selected
pages, export pages as PNG), and later: "once exported put it into the system clipboard so the png can easily get
pasted somewhere. Copy pasting high res pages (dpi higher than the native screen, which is what the system screenshot
tool produces) into the clipboard is the power move here."

Code: `qt/src/session/PageFiles.*` (ranges, split plans, a new document of some pages, picture names; Qt-free), the
window's part `qt/src/app/AppPageFiles.cpp`, the dialogs `qt/src/app/qml/PageFiles.qml`; `RegionRender`'s `paper`
option; the CLI's `--png-dir`. Tests: `PageFilesTest` and `PageFilesRanges` (shell), `PageFilesUiTest` (ui),
`Cli.exportsPagesAsPicturesNamedAsTheAppNamesThem`.

## Where

| what | where |
| --- | --- |
| Insert pages from a file… | the add-page button's list (hold or right-click), ⋮ → Page, the page menu (sidebar and grid: the import icon beside "Insert pages…"), the page grid's selection bar (⋯) |
| Extract to a new document… | ⋮ → Page, the page menu (an icon beside "Select all pages"), the grid's selection bar (⋯) |
| Split the document… | ⋮ → Page, the page menu (an icon beside "Select all pages"), the grid's selection bar (⋯) |
| Export pages as pictures… | ⋮ → Export, the page menu (the picture icon beside "Select all pages"), the grid's selection bar (⋯) |
| Copy page as image | Ctrl+Shift+C (a shortcut of its own, changeable), ⋮ → Page, the page menu (the copy icon beside "Save page as template"), the grid's selection bar (⋯) |

The page menu acts on the selection when the page pressed is selected, else on that page (as all its entries).

## Inserting pages from a file (A6)

- The file: a PDF, a PDF with notes, or a `.xopp`/`.xoj`. It is read on a worker (`DocumentSession::loadFile`, as
  opening it would) and kept while the dialog is open (`AppController::pageFile`, one at a time, let go when the
  dialog closes or the pages are inserted).
- Which pages: all, a range typed ("1-3, 5, 8-"; `pagefiles::parseRange`, checked as it is typed), or pages ticked on
  their pictures (drawn by the library search's page provider, `HitPageProvider`, with its own limits). Before or
  after the page the dialog was opened for (the current page, or the page of the menu).
- Inserted as pasted pages are (`PageClipboard`, copied on a worker): PDF pages stay PDF pages in the document's
  merged PDF, so their text stays searchable and selectable; ink, text, pictures and Markdown boxes come along as they
  are. One undo step; the pages are selected and shown.
- **A protected PDF** asks for its password in the dialog (wrong: "The password is not right. Try again."). The
  password lives in memory while the file is held (`LoadResult::passwordHold`). No pictures of its pages are shown
  (the provider reads files without passwords), its pages are ticked by number. Inserted into a document that is not
  protected, its pages are no longer protected there, as pasting pages from a protected tab (decided).

## Extract and split (A7)

- **Extract**: the pages (the selection, else the page) become a new document named "lecture (pages 2-3)" (editable)
  next to the document (else in the library's current folder, else the last folder opened), opened in a tab. As a
  PDF with notes (the default in PDF files mode and for a PDF) or a `.xopp` with its PDF pages in "name.pdf" next to it
  (the default for a `.xopp`; upstream's naming, ".name.pages.pdf" if taken; no PDF when no page shows one). "Remove
  the pages from this document": one undo step (a document keeps at least one page); otherwise it is unchanged.
- **Split**: every N pages, at the selected pages (each starts a part; the first part starts at page 1), or at the
  top-level chapters of the table of contents (the PDF's outline, else the headings written in the document). The
  dialog lists the parts. The parts are written next to the document ("lecture (part 1)", or "lecture - <chapter>"),
  not opened; the document is unchanged.
- How: the pages are copied as a save copies them (`pagefiles::subset`): a document whose background PDF is the
  source's own file. Written on a worker: `HybridPdf::write` (in full) for a PDF with notes, `HybridPdf::exportXopp`
  for a `.xopp`; only the PDF pages used are copied (qpdf), as text. Pasted pages still being merged are waited for.
- **A protected document**: the new PDF is protected with the same password (the save's encryption,
  `encryptionForSave`), and it opens in its tab without asking. A `.xopp` is refused (it cannot be encrypted), as Save
  as `.xopp` is.

## Pages as pictures (A8)

- **Export pages as pictures…**: this page, the selected pages, or all; the resolution (72–600 dpi or the screen's;
  the remembered one, 300 at first); PNG or JPEG; "Transparent background" (PNG): no paper colour and no ruling
  behind the ink (`region::Request::paper`); PDF pages and background pictures are still drawn. Into a folder chosen
  (remembered; the Pictures folder at first). Files `lecture-p003.png`: the page's number in the document, at least
  three digits (more for a longer document) so they sort in page order; existing files of these names are replaced.
  Drawn one page at a time on a worker from copies of the pages; at most 64 megapixels a picture (a poster at
  600 dpi gets less). The DPI is written into the files.
- **One page exported** is also put on the clipboard (several pages: the clipboard is left alone, decided).
- **Copy page as image** (Ctrl+Shift+C): the page (of a selection, the first) as a PNG on the clipboard at the
  resolution of Settings → Storage → "Pages as pictures" (300 dpi at first; also changed by the export dialog), on
  white paper, with its DPI in the PNG so that apps paste it at the page's size. At most 32 megapixels (an A4 page at
  600 dpi is 35): then less, and the toast says so. Toast: "Page 3 copied as an image (2480×3509)".
- Always the normal colours: dark pages are a way of showing pages, never of exporting them.
- **A protected document** is not exported as pictures (they cannot keep its password; the dialog says why). Copying
  a page as an image is allowed, as the snip is: the clipboard is not a file.
- **The command line**: `xournal-qt-cli --png-dir=DIR [--export-range=RANGE] [--export-png-dpi=N] FILE…` writes
  `DIR/<name>-p001.png`, … for every file (300 dpi when not given, as in the app). Upstream's `--create-img` stays as it is
  (`name-1.png`, …).

## Other platforms

Nothing platform-specific in the code. Android: the folder picker returns `content://` folders, which the export does
not write into yet (the default folder, Pictures, works); the file picker for inserting gives local paths where the
app has "All files access" (as opening files).
