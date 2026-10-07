# ADR-0006: A save writes a copy of the document on a worker

- Status: **Accepted.** Recorded 2026-10-08, distilled from [features/hybrid-pdf.md](../features/hybrid-pdf.md),
  "Saving", and the header of `qt/src/session/DocumentSave.cpp`. The plain autosave does not follow it yet (TODO.md,
  "Session").

## Context
Upstream Xournal++ saves on the UI thread: the `.xopp` is built and gzipped while the window waits. The fork's saves
do much more: a PDF with notes draws every changed layer as an annotation, embeds the `.xopp`, runs qpdf, may keep a
version and encrypt; a `.xopp` with pasted PDF pages writes a merged background PDF. On a big PDF that is seconds, and
the pen must keep drawing meanwhile.

## Decision
- **The document is copied, the file is written from the copy.** A save goes through steps
  (`DocumentSession::saveInBackground`, `DocumentSave.cpp`): what needs the document runs on its thread (planning the
  merged PDF, taking a copy of its pages under the read lock, a few milliseconds), the file work runs on a worker
  (drawing, the `.xopp`, qpdf), and the document can be edited in between. The undo stack's saved point is the copied
  state; the document stays modified until the file is written. The last step, back on the document's thread, tells
  the document what was written and starts the next waiting save.
- **Every file is written atomically** (`fileio::AtomicFile`: a temporary name next to it, fsync, rename over it) and
  **one writer per file at a time** in the process (`fileio::FileWriteLock`: a save and a tag change of the same PDF
  wait for each other).
- Where a write's PDF objects go (a file written in full or an incremental update) is one interface
  (`PdfObjectSink`), so incremental saves and full writes share the drawing.
- `waitForSaves()` runs the same steps without the event loop: closing a tab, quitting and the tests wait for them;
  `AppController::shutdown` waits for every window's saves before the background jobs.

## Consequences
- Saving never blocks the pen, also for a 1,000-page PDF with notes.
- A save sees the document as it was when the copy was taken; changes made while it runs are the next save's.
- Code that writes a document's file must go through the session's save (or hold the `FileWriteLock`), never write
  it directly.
- Not yet for the plain autosave: it still builds and writes its `.xopp` on the UI thread, because moving it needs a
  synchronous path for an app going to the background (Android), an order against saves and the session's destructor
  waiting for it (TODO.md lists the steps).

## Considered
- Saving on the UI thread with a progress dialog (upstream's way): seconds of a frozen window on big PDFs.
- Writing from the live document on a worker under the read lock: the pen would wait for the whole write.
