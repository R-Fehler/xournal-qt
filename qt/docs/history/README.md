# How xournal-qt came to be

The turning points of the fork and why they went the way they did. What the app does now is in
the feature docs in [qt/docs/](../); the recorded decisions are in [../adr/](../adr/); what each release brought
is in [../release-notes/](../release-notes/). Everything else (the per-block changelog, measurements, research notes,
done tasks) is in git history: see "Where the old notes went" at the end.

## The fork (2026-09)

- **Why a fork.** The goal is a pen-first app for notes and PDF annotation that keeps what Xournal++ does well (its
  file formats, stroke rendering, pressure, stabiliser, palm rejection, undo and tools) and adds what it lacks:
  several documents as tabs in one window, an interface that works on a tablet without a keyboard, iPad-like
  scrolling and zoom, Wayland first, and a way to mobile. See [VISION.md](../../../VISION.md).
- **A real git fork with upstream history**, so upstream's fixes can still be merged. All new code lives under `qt/`
  with its own build root (`qt/CMakeLists.txt`); upstream files are not deleted, moved or reformatted, only left
  unbuilt. The few unavoidable edits are small, marked `xournal-qt:` and listed
  ([0000](../adr/0000-fork-policy.md), [0002](../adr/0002-upstream-seams.md)).
- **A Qt-free core without editing upstream.** Upstream's GTK glue is replaced by shadow headers with upstream's
  names (`qt/compat/include/control/Control.h` and friends) and a tiny `gtk/gtk.h` shim with GDK's types, so reused
  upstream files (model, file I/O, undo, tools, rendering) compile unchanged and any real GTK call fails to compile.
  The per-tab `DocumentSession` implements upstream's `Control` interface.
- **Pixel-identical rendering.** Pages are still drawn with cairo through upstream's view code (`PageRaster` is a
  port of upstream's `RenderJob` and page buffer), so ink looks exactly as in Xournal++. The CLI mirrors upstream's
  export flags so golden tests could compare both programs' output on upstream's fixtures.
- **Qt Quick, not Widgets** ([0001](../adr/0001-ui-host.md)). A throw-away spike drew with the same canvas in
  a `QQuickItem` and a `QWidget` on the target 2-in-1 (KDE Plasma, Wayland, Wacom pen); both met the latency and
  input criteria and felt the same, so Qt Quick won: it is touch-first and the only realistic way to Android and iOS.
  Krita's experience (a QML canvas abandoned after years of tablet bugs) shaped one rule that still holds: **all
  canvas input goes through C++** (an event filter on the window), never through QML handlers.
- **Tabs**: one process-wide `AppContext` (settings, tools, render workers) and one session plus one view per tab.
- **GLib stays** for now: Qt runs on GLib's event loop on Linux, so upstream's `g_idle_add` code keeps working.

## The PDF as the document (2026-09-24 onwards)

- **PDF engine**: poppler stays. MuPDF was tried on a branch (faster at 1×, scales with threads, but AGPL, about
  twice the memory and not faster at high zoom); the decision is still open (TODO.md). qpdf does all PDF writing.
- **The hybrid PDF** (0.2.0): a document can be one ordinary PDF that any viewer shows with its ink (one annotation
  per layer with the exact appearance drawn by cairo) and that still carries the full Xournal data (an embedded
  `.xopp`). This made "PDF files" a way of keeping documents equal to "Xournal++ files"; the first start asks which
  ([hybrid-pdf.md](../hybrid-pdf.md)). Saves append incremental updates as Acrobat and Drawboard
  do, so a save of a big PDF takes milliseconds and the original pages are never rewritten.
- **Archive PDF**: PDF/A-3b with the ink flattened and the data embedded, readable for decades.
- **Version history inside the PDF** (0.6.0): the incremental updates are kept as versions (one per day plus
  milestones), older ones stored as deltas of their `.xopp`, so a lecture's progression travels in the file itself.
- **Pasted PDF pages stay PDF pages** (searchable text) through one merged background PDF, because the `.xopp`
  format allows one background PDF per document and must stay readable by Xournal++.

## Writing text

- **Markdown with a native engine** (md4c, our own layout and pagination with Pango; MicroTeX for formulas), not a
  web view: it works the same on Android and iOS, pagination and PDF export reuse what exists, and ink and text
  share one coordinate system. Mermaid stays a code block (it needs a browser engine).
- **The text mode was removed.** A word-processor-like "text mode" existed briefly; from 2026-09-26 the writing
  button writes Markdown on the page instead (formatted while typing), which does what the text mode did and more.
  Its code went in the 2026-10 refactoring. Text written with it is ordinary text.
- **Text documents** are either `.md` files (with `name.assets/` beside them) or PDFs that carry the Markdown and its
  pictures as attachments, following the first-start choice ([md-pdf.md](../md-pdf.md)).

## One interface for every screen (0.4.0 to 0.8.0)

- **Adaptive layout** (0.4.0): an audit of how the UI copes with narrow, short and portrait windows led to size
  classes, a touch profile, adaptive menus and dialogs, a phone chrome and safe areas
  ([adaptive-layout.md](../adaptive-layout.md)).
- **The toolbox replaced the classic tool bar.** The toolbox (0.5.0): the user's own pens, each with its color and
  width, picked up like pens from a box (as in Drawboard). The classic bar was kept for a release next to it, then
  removed in 0.8.0 to stop carrying two UIs.
- **Scrolling instead of folding** (0.8.0): the rail first folded sections into one button when room ran out; on an
  unfolded phone the user's own tools collapsed while the fixed ones stayed. Folding went away: the rail and the top
  bar scroll, keep one order on every screen, and are one stored arrangement the user edits and groups
  ([toolbox.md](../toolbox.md)).
- **Zen replaced the reader chrome** (0.8.0): reading had its own chrome, then a read-only full screen; both were
  replaced by three independent switches (full screen, Zen, read only), Zen showing nothing but the page and a faint
  dot ([zen.md](../zen.md)).

## Search, sound and time

- **Handwriting search, never conversion** (0.5.0): a recogniser reads the ink in the background at idle priority
  and the search looks at its readings; the ink stays ink. TrOCR-small in ONNX Runtime (loaded at run time, so the
  app does not link it) found 97 % of English words; German needed a model of its own, so several models run side by
  side ([research](../research/handwriting-recognition.md), [the feature](../handwriting-search.md)).
- **Audio compatible with Xournal++** (0.5.0): recordings tied to strokes as upstream does it (Ogg Vorbis, vendored),
  and in a PDF with notes the recordings are attachments named by page, findable without the app.
- **The timeline** (0.6.0): every element remembers when it was made (an attribute upstream ignores), so a document
  can be replayed together with its recordings.

## The 2026-10 review

A critical review of the whole code base ([../review/2026-10/](../review/2026-10/README.md)) after six weeks of
feature blocks. With the app in alpha and no users, every path that kept xournal-qt's own older data readable was
removed (Xournal++'s formats stay), and the code and these docs are being restructured in waves.

## Where the old notes went

They were deleted from the tree in the docs restructure; read them with `git show 5c6402d:<path>`:

| Path at 5c6402d | What it was |
| --- | --- |
| `qt/docs/ROADMAP.md` | the changelog of every block (2026-09-19 to 2026-10-06), measurements, the original M0–M7 plan |
| `qt/docs/ideas-2026-10.md` | a comparison with other apps and the ideas not chosen (A3–A9, A15, B1–B8) |
| `qt/docs/ui-adaptive-audit.md` | the audit behind the adaptive layout |
| `qt/docs/platform-research.md` | native libraries and PDF engines compared |
| `qt/docs/md-columns.md` | two-column Markdown: doable but not easy, not built |
| `qt/docs/text-mode.md` | the removed text mode |
| `qt/docs/research/collaboration.md` | research on E2E-encrypted collaboration and agents as collaborators (nothing decided) |
| `qt/docs/research/version-history.md` | the plan of version history inside PDFs (built: hybrid-pdf.md) |
| `TODO.md` | the done tasks of every block |
| `qt/docs/testing/device-checklist.md` | 1546 manual checks, one section per block |
