# ADR-0003: What the windows share, and what acts on a canvas

- Status: **Accepted (2026-10-07)**, `qt/app-services` (review 2026-10, app-cpp blocks C and D).

## Context
`AppController` (the QML API `app`, one per window) had grown into the owner of everything:

- A window of undocked documents was a second `AppController` built by a second constructor. It borrowed the main
  window's settings view, toolbox, shortcuts, library, recent files, page clipboard and handwriting search through raw
  pointers, yet was destroyed after them (a QObject child of the main window).
- "This file, wherever it is open" was answered three ways, each walking the main window's list of windows.
- Every per-document property was relayed by hand in `currentTabChanged` (41 connections made again on every tab
  change, 25 change signals emitted blindly).
- The reference view (`ReferenceMode`) and the window carried the same 51 members for the pills of a canvas, written
  twice; they drifted apart (the reference copied text of PDFs that forbid it).
- 18 background jobs went to the global thread pool, and quitting did not wait for them.

## Decision
- **`AppServices`** (`src/app/AppServices.h`): one per process, held by `main()`, made before the first window and gone
  after the last. It owns the process-wide objects (AppContext, palette, settings view, toolbox, shortcuts, library and
  its lists, recent files, page clipboard, handwriting search, the library's ink job), the window factory and
  **`OpenDocuments`** (the windows, main window first; `find(file, {except, textFiles, plainPdf})`, `all()`). A window
  is an `AppController` on the services: the first one is the main window, the next ones are windows of undocked
  documents. An `AppController` made without services (the tests) makes its own.
- **`BackgroundJobs`** (owned by `AppServices`): the app's work off the UI thread. `AppController::shutdown` waits for
  every window's saves, then for the jobs. Work nobody waits for runs at idle priority, work the reader waits for
  (exports, shares, pages read) at normal priority.
- **`CurrentDocument`** (`src/app/CurrentDocument.h`): the current tab's `DocumentSession` and `CanvasView` with their
  signals relayed. The window calls `follow()` on a tab change and `announce()` once it has set itself up; each state
  signal then fires once. What cares about the current document connects to it once.
- **`CanvasActions`** (`src/shell/CanvasActions.h`): what acts on one canvas (selection, groups, notes, PDF text, the
  clipboard, page, zoom, Back), with a policy (the reference changes nothing unless it is written in; read-only
  documents and a text file's pages are checked for both) and hooks for the window's ways (choosing a tool, finishing
  the Markdown being written). `app.edit` and `app.reference.edit`; the pills take one as their target.
  `app.keyTarget` is the one of the side with the keys (the reference while it has the focus).

## Consequences
- Adding a per-document property to the window: a getter and one connection to a `CurrentDocument` signal.
- The feature objects split off `AppController` (review 2026-10, app-cpp blocks E–N) get a **`WindowContext`**
  (`src/app/WindowContext.h`: the services, the window's tabs and its current document) instead of reaching into the
  controller; `AudioControl` and `TimelineControl` do already.
- The QML calls the canvas actions on `app.edit` and `app.keyTarget`. `AppController` still carries the names with
  rules of their own (`app.copySelection`, `app.pasteElements`, `app.selectAllOnPage`, Back and Forward across
  documents) and the per-document properties (`app.pageNumber`, `app.zoomPercent`, …); they move with their
  features.
