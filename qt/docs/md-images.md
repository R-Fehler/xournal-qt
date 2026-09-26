# Images in Markdown (`qt/md-images`)

Status: plan agreed with the author 2026-09-26 (TODO.md, "Ideas round of 2026-09-25/26"); built as `qt/md-images`.
Related: [markdown-boxes.md](markdown-boxes.md) (drawing, formulas as inline shapes), [md-editor.md](md-editor.md)
(the `.md` editor), [md-pdf.md](md-pdf.md) (PDF text documents).

## Drawing

- `![alt](path "title")` is one run of the parsed text (`md::Run` with `Image`): its text is the alt text, its source
  the whole `![…](…)` (as a formula's run is its whole `$…$`), its link the image's path. The layout puts an image in
  the text as one character, U+FFFC, with a Pango shape as big as the picture, as it does for formulas
  (`MdLayout.cpp`). So lines break around it, a tap on it is a place in the text, pages are split around it and never
  inside it.
- **Sizes** (points; a picture's pixel is 0.75 pt, i.e. its natural size at 96 dpi):
  - an image alone in its paragraph is a block: its natural size, made smaller to the column's width, never bigger;
    no higher than 1.4 times the column's width (so that it fits an A4 page's text, 482 × 729 pt, with its margins);
  - an image in a line of text is as high as the line (1.2 em), never bigger than its natural size, on the baseline.
- **Where the files are** (`md::images`, Qt-free): every open document registers a *root*: the folder its relative
  links are relative to, and where its `name.assets/` really is (next to a `.md`; in the app cache for a PDF text
  document, which carries its images inside). A relative link is looked for in the roots, newest first; the first
  file that exists is taken, also with `%20` and the like decoded. The renderer draws a page's box without knowing its
  document (upstream's `TextView` seam gives only the text element), so the roots are process-wide; two open
  documents of the same name share `name.assets/…` names, and the first existing file wins (the names pasted are
  time stamps, so they do not collide in practice).
- **Kinds**: PNG, JPEG, GIF (first frame), WebP where Qt reads it, SVG (Qt SVG), read by the app's decoder
  (`QImageReader`, `QSvgRenderer`, installed by `AppContext`); without it (the CLI, the Markdown tests) gdk-pixbuf.
- **Cache**: a picture's size (the header only) is cached by path, modification time and file size; the decoded
  pixels by that and the size decoded (the pixels needed where it is drawn, rounded up to halvings of the natural
  size, so zooming reuses them). The pixel cache owns at most 64 MB, least recently used first out. On a vector
  surface (PDF export, the hybrid PDF, print) the picture is drawn at its natural size, a JPEG as the file itself
  (Cairo embeds it as it is), each picture once per PDF (`CAIRO_MIME_TYPE_UNIQUE_ID`).
- **Missing or unreadable file**: the alt text and the path, in red, in the text.
- **While the block is written** (the block with the cursor shows its Markdown): the `![…](…)` is shown as its source,
  and the picture is drawn below the block, as the preview of a `$$` formula.
- **Everywhere**: the canvas, the `.md` editor, full-page mode, thumbnails, previews, PDF export, the hybrid PDF and
  print all draw Markdown through `md::layout` / `md::draw`.
- **Pagination**: an image is one character of its line: it never splits, and the aspect limit above keeps a block
  image within an A4 page. (A box does not know its page, so the page height cannot be the limit: the canvas, the
  thumbnails and the PDF draw a page's part from its box alone, and they must agree with the pagination.)

## Adding images

- Paste (Ctrl+V with a picture on the clipboard: on the page, in a `.md`, and in the editor beside the page), a
  dropped image file, or the formatting bar's image button (a file picker) saves the picture and inserts
  `![](name.assets/image-YYYY-MM-DD-HHMMSS.png)` at the cursor (Typora's names; a dropped or picked file keeps its
  name and kind, made unique in the folder, and its name is the alt text). A pasted picture's alt text is empty. It is
  one undo step of the text; undo leaves the file (see clean-up). A copied text that also carries a picture (a
  spreadsheet's cells) is pasted as text.
- Where the file goes, by document (`MarkdownImages::placeOf`: the document's root):
  - a `.md`: `name.assets/` next to it;
  - a PDF text document, any PDF with notes, a `.xopp`: `name.assets/` in its work folder in the app cache (carried
    inside the file from the next save on, below);
  - notes not saved yet: none ("Pictures can be added once the document is saved.").

## `.md` and `name.assets/`: one document

- The library shows one card for the pair and does not list its `name.assets` folder (the index and "all files"
  neither; `DocumentFiles::scan`). A `.assets` folder without a `.md` of its name is shown as a folder: whatever
  is in it stays visible.
- Move, rename, delete (to the trash) and share take both. A rename rewrites the links in the `.md`: the prefix
  `oldname.assets/` becomes `newname.assets/` (links written as `./oldname.assets/…` and `<oldname.assets/…>` too).
- Sync conflicts: the conflict copies of the `.md` are shown on its card as before (they link to the same
  `name.assets/`); the folder is hidden and goes with the `.md` it belongs to.
- An open `.md` renamed in the library follows in its tab: the path, the links in its text (one undo step), and the
  file's new bytes are taken as read (no question about a change on disk).
- Clean-up: images in `name.assets/` that the text no longer links to are not deleted automatically (undo may bring
  a link back). ⋮ → **Remove unused images…** lists the files of the folder that the text as it is now does not link
  to (images, links, reference definitions, HTML `src`: a linked PDF there counts as used) and moves them to the
  trash when asked (`DocumentImages::unusedPictures`, `UnusedImagesDialog.qml`).

## PDF text documents

- The images are attachments next to `name.md`, under the paths the links name (`name.assets/<file>`;
  `TextDocument::attachments`). Nothing is written next to the PDF.
- Opening extracts them with the clean copy (the cache entry of the file's version, `pictures/`) and copies them into
  the document's **work folder** in the app cache, `<cache>/md-assets/<hash of the PDF's path>/` (a folder per
  document, so that pictures pasted and not saved yet are still there after a crash). While the document (or a
  `LoadResult` of it: a library preview) is open, its root is that folder (every relative link is looked for there,
  and pictures added go into `name.assets/` in it), after it the folder the PDF is in. Saved under another name, the
  document takes its work folder's pictures along.
- A full write packs the pictures the text links to (unreferenced ones are dropped). An incremental save adds the new
  ones and keeps what the file has (a picture's data never changes under its name), also pictures the text no longer
  links to, until the next full write. (A small file grows by more than a quarter with a picture: the policy of
  incremental saves then writes it in full anyway.)
- **Open as PDF document** copies the `.md`'s linked pictures into the new PDF's work folder, so its first save
  packs them; **Export as Markdown** writes `name.md` and the pictures into `name.assets/` next to it (links into
  another `….assets/` folder, from an older name, are rewritten to `name.assets/`).

## Web images

`https://…` pictures are never fetched unasked: they show as the alt text (the address when it has none) and a small
**Load image** button. A tap on it (`md::imageButtonAt`; with any tool, also while the text is written) shows the
whole address and its host (`WebImageConfirm.qml`); while connecting to the web was not decided (`networkAccess`
"ask", the setting of `qt/citations`), it also says what that means, and **Load** is the opt-in (the setting becomes
"on", as arXiv's "Allow"). With the setting "off" nothing is sent (a message says so). The picture is fetched through
`NetFetch` (tests: a fake; 20 s, at most 40 MB), checked to be a picture, and kept in the app cache only
(`<cache>/web-images/<hash of the address>.img`); from then on the address shows it from there, in every document.
The texts that show it are laid out again (`AppController::relayoutPictures`); in a `.md` the pages follow at the next
edit.

## Markdown boxes in a `.xopp` (decided when built)

The pictures of the Markdown texts of notes are inside the `.xopp`, as extra `<preview>` elements at the end of the
document, each with an attribute naming the picture and its data in base64 (as a TeX image's):

```xml
<preview xqt-file="lecture.assets/image-2026-09-26-090000.png">iVBORw0KGgo…</preview>
```

- Links in the Markdown are `name.assets/…` as elsewhere; opening the `.xopp` copies its pictures into its work folder
  in the app cache, where they resolve (`DocumentImages::unpackXopp`), as for a PDF text document.
- Written by `PictureSaveHandler` (upstream's `SaveHandler` with the nodes added after `prepareSave`: no upstream file
  changed) wherever the fork writes a `.xopp`: save, save as, autosave (a recovered document has its pictures), and the
  library's rewrite of a moved `.xopp`. Only the pictures some Markdown text links to; a document without pictures is
  written byte for byte as before. The document's own preview stays the first `<preview>` (thumbnails read the first).
- A PDF with notes carries the pictures of any of its Markdown (not only a text document's) as attachments.
- **Xournal++**: 1.3.4 and its current master (built from `../xournalpp`) open such a file silently and show the
  Markdown source as text (checked with `xournalpp --create-pdf`, in a separate config folder). They ignore the contents
  of `<preview>` and unknown attributes; 1.2's loader ignores `<preview>` altogether. Saving the file in Xournal++ keeps
  one preview of its own: the pictures are gone then, their links stay (shown as missing in xournal-qt).
- Options not taken:
  - Upstream's zip container for `.xopp` (`mimetype`, `META-INF/version`, `content.xml`, attachments): its loader has
    the mimetype check inverted (`if (!strcmp(mimetype, "application/xournal++")) throw "Mimetype wrong"`, 1.1 to
    master), so a correct file is refused; a wrong mimetype would only work until that bug is fixed.
  - An element of our own under `<xournal>`: 1.3.4 ignores it with a warning on the console, but master's new parser
    reports "Ignoring unexpected … tag" as a loading error (shown to the user).
  - Upstream `<image>` elements (base64 too) in a hidden place: Xournal++ would draw or lose them (layers have no
    saved visibility), and they have no name to link to.
  - A folder next to the `.xopp`: files next to a document are what the plan avoids for notes.

## Code

| File | What |
| --- | --- |
| `qt/src/markdown/MdImages.*` | links to files (roots), sizes, the decoder, the pixel cache, drawing |
| `qt/src/markdown/MdDocument.cpp` | the image run (`Builder::endImage`) |
| `qt/src/markdown/MdLayout.cpp` | the image shape, block and inline sizes, placeholders, the preview while writing |
| `qt/src/canvas/MdImageDecoder.*` | the app's decoder (Qt) and the web cache folder |
| `qt/src/session/DocumentImages.*` | a document's root and `name.assets`, the work folder, carried pictures, the `.xopp`'s pictures, renaming links, unused pictures |
| `qt/src/session/PictureSaveHandler.h` | a `.xopp` with its pictures |
| `qt/src/session/HybridPdf.cpp`, `TextDocument.cpp` | pictures as PDF attachments (extracted with the clean copy; incremental saves add new ones) |
| `qt/src/canvas/MarkdownImages.*`, `qt/src/app/AppMarkdownFormat.cpp` | paste, drop, the picker |
| `qt/src/app/AppMarkdownImages.cpp`, `qml/WebImageConfirm.qml`, `qml/UnusedImagesDialog.qml` | web pictures, removing unused ones |
| `qt/src/shell/DocumentFiles.cpp` | the `.md` and its `name.assets` in the library |
| `DocumentSession::updateImageRoot`, `MarkdownFile::textDocument` | the root while a document is open, and while its pages are made |
| `qt/tests/markdown/MdImagesTest.cpp`, `qt/tests/canvas/TextDocumentTest.cpp` | the tests (`XQT_MD_IMAGES_SHOTS=<dir>` draws a picture to look at) |
