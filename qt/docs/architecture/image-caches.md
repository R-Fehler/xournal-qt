# Image caches: the pictures the app draws ahead and keeps

A sub-page of the [architecture overview](README.md): its block [Pictures and caches](README.md#pictures-and-caches)
in detail.

Besides the rendered pages of the canvas ([CanvasMemory](../../src/canvas/CanvasMemory.h)), the app draws smaller
pictures of pages and documents and keeps them, so that lists and grids show them at once. They have one vocabulary,
one owner of their memory and one owner of their worker threads.

## The words

| Word | What it is | Shown in | Code |
| --- | --- | --- | --- |
| **page preview** | what the page sidebar, the page grid and the overview of open documents show of a page: its sketch, then its sharp thumbnail | sidebar, page grid, tab overview | the two below |
| **sketch** | a small picture of every page of the open documents, drawn in advance (128 px; 96 or 64 when many pages are open) | under the sharp thumbnail | `PageSketches`, `image://sketch` |
| **thumbnail** | the sharp picture of a page at the width the list asks for (steps of 64 px) | sidebar, page grid, overview | `ThumbnailProvider`, `image://thumbnail` |
| **stand-in** | a bigger picture of a page (768 px; 512 or 384), shown on the canvas until the page is rendered; stored on disk for the next opening (JPEG under `~/.cache/xournal-qt/pages`) | canvas | `PageSketches::standIn`, `CanvasView::setStandInSource` |
| **cover** | the picture of a document's title page on its card (360 px PNG), stored in the folder's `previews.pack` (outside a library: `~/.cache/xournal-qt/previews`; see below) | library and recent-files grids, stickers | `DocumentCovers`, `image://cover` |
| **hit page** | a page with the hits of a library search marked | library search, bookmarks, to-dos | `HitPageProvider`, `image://hitpage` |
| **snippet** | a passage of a Markdown file with the hits marked | library search | `MdSnippetProvider`, `image://mdsnippet` |
| **annotation picture** | the handwriting of an annotation | annotations panel | `AnnotationImageProvider`, `image://annotation` |

"Preview" means the page previews only. Two names are older than this vocabulary and stay: the model role `preview`
of the library, recent-files and sticker cards (QML reads the cover's URL under that name), and the preview image
stored inside a `.xopp` file (Xournal++'s format, `DocumentSession::previewOf`). On disk the covers keep the file names
they had (`previews.pack`, `preview-stamps.pack`, `~/.cache/xournal-qt/previews`): new names would leave the old
files behind in every folder of a library, which sync clients upload.

## Memory: one owner

[ImageMemory](../../src/shell/ImageMemory.h) sets what each cache may keep, in one table:

- the page previews take the setting **Settings → Documents → "Page previews (sidebar, overviews)"**
  (`previewMemory`, 64–1024 MB, default 256 MB): three quarters for the thumbnails, a quarter for the sketches;
- the stand-ins take a tenth of the memory for rendered pages (`CanvasMemory::standInBudget`, the setting "Rendered pages of the open documents");
- covers 48 MB of PNG (the folders used least recently go first), hit pages 128 MB and 12 loaded documents,
  annotation pictures 24 MB, snippets 8 parsed files.

Within its limit, each cache drops the pictures used least recently first (`LruImageCache` in
[AsyncImage.h](../../src/shell/AsyncImage.h) for the thumbnails, hit pages and annotation pictures).

## Workers: one owner

[ImageWorkers](../../src/shell/ImageWorkers.h) owns the thread pools that draw, read and store these pictures: one pool
per kind of work (thumbnails, sketches, stand-ins on disk, covers, the covers' packs, hit pages, snippets,
annotations, annotation pictures), sized in one table, **all at idle priority**: the canvas's own renders come first.

- A picture QML no longer wants (its item scrolled away) is not drawn: the response is cancelled and its worker
  skips it (`ImageWorkers::respond`). For covers this means a card flung past does not load its document.
- **Shutdown**: `AppController::shutdown` calls `ImageWorkers::shutdown()` before the application takes Qt's image
  plugins, Cairo and poppler away: what is queued is dropped, what runs is waited for, nothing starts afterwards
  (a response is finished without a picture), then the covers not written yet are written. Tests:
  `Sketches.nothingIsDrawnOrStoredAfterShutdown`, `ImageWorkers.*`.

## Code

`qt/src/shell/`: `ImageMemory.*`, `ImageWorkers.*`, `AsyncImage.*` (the response base, the LRU, the URL encoding),
`SessionRegistry.*` (the open documents the providers draw from, by the id in their URLs; only `TabManager`
registers, the models look the id up), `Thumbnails.*`, `PageSketches.*`, `DocumentCovers.*`, `HitPages.*`, `MdSnippets.*`, `AnnotationsModel.*`
(`AnnotationImageProvider`); the providers are registered in `qt/src/app/EngineSetup.cpp`.
