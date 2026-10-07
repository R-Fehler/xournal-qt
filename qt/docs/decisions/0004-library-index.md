# ADR-0004: The library index is a cache of packs per folder, kept by file name

- Status: **Accepted.** Recorded 2026-10-08, distilled from [features/library.md](../features/library.md), "The
  library cache", where the details are.

## Context
A library is a plain folder of documents that the user also opens with other programs and often syncs with another
machine (Syncthing, Nextcloud, a cloud drive). The library search has to search the text of every document (PDF
pages, text elements, Markdown, recognised handwriting) at once, and the home screen has to show a cover per document
at once, also for libraries of thousands of PDFs. Reading every PDF at every start is far too slow; one database file
for the whole library breaks when a subfolder is opened as a library of its own, when folders are moved by another
program, and it conflicts in sync clients ([VISION.md](../../../VISION.md): keep the user's folders clean, moving folders
stays cheap).

## Decision
- **Cache, never data.** Everything in the index can be deleted at any time and is read again from the documents.
  What is not cache (the reading positions, title pages, a library's settings) lives in the config folder.
- **One cache folder per folder with documents**, holding only the documents directly in it, keyed by **file name**:
  a folder moved or renamed by any program keeps its cache, and a subfolder opened as a library finds its caches
  there. Opening a library reads the caches of all its folders and merges them.
- **Where it is kept is a setting of each library**: in the folders (hidden `.xournal_library/`, the desktop's
  default) or in the app's cache folder (the default on Android, and for folders a sync client uploads). Folders that
  cannot be written keep it in the app cache either way.
- **Packs split by how often they change**: `notes.pack` (small, written when a document is saved), `pdf-text.pack`
  (big, written only when a PDF changes; over 1 MB of text a file of its own), `previews.pack` plus the few bytes of
  `preview-stamps.pack` (a cover that looks the same is not written again), `ink-text.pack` (recognised handwriting).
  Each is CBOR compressed with zlib behind a format number, always written whole under another name first, a few
  seconds after the last change: a sync client never sees half a file and uploads as little as possible.
- **Found again, not read again**: a document is read only when its file's size and time changed. A document moved by
  another program takes over the entry of a file that is gone with the same size, time and sample; a copied library
  (other times) takes over entries whose content hash (BLAKE2b) matches. The index is updated on a background thread,
  one document at a time; a document saved in the app hands its entry over.
- **A pack of another format is not read**: the documents are read once and the packs written over. There is no
  conversion of older caches.

## Consequences
- Opening a big library is instant after the first time, and moving or renaming folders costs nothing.
- The app writes into the user's folders unless the library keeps its cache in the app cache; Settings → Storage
  shows the size and removes every cache folder of a library.
- Unsaved changes are not searchable through the index (it reads files).
- The library code must keep the cache folders out of every listing, share and archive.

## Considered
- One cache for the whole library: a subfolder opened as a library of its own would not find it, a folder moved by
  another program would lose its entries, and every save would rewrite one big file that a sync client uploads.
- The covers in `notes.pack`: a cover is drawn when its card is shown, often long after the index wrote the pack,
  which would then be written twice, and the index would depend on the covers.
- Keeping the index only in the app cache: the cache would not travel with a library copied or synced to another
  machine, so every machine reads every PDF again. Offered as the per-library setting instead.
