# The review of 2026-10 and the refactoring plan

The author (2026-10-07): "run a critical review and a subsequent refactoring where you see fit to make this codebase
workable and better maintainable for humans and future agents. Remove all code that tries to keep backwards
compatible to older versioned data. We are in alpha stage and have 0 users." And: split the huge QML files; an
architecture overview for human developers, browsable on GitHub, with links into the source.

Five reviews, one per area (read-only, 2026-10-07):

| Area | File | Verdict in one line |
| --- | --- | --- |
| App C++ (`qt/src/app`, `qt/src/quick`) | [app-cpp.md](app-cpp.md) | `AppController` is one class of ~17k lines over 24 files, 179 properties, 324 invokables, used 2,208 times from QML through an untyped context property |
| QML (`qt/src/app/qml`) | [qml.md](qml.md) | the small components are fine; `Main.qml` (5,551 lines, `win` with 145 members, 99 Shortcuts) is the god file; split map included |
| Shell (`qt/src/shell`) | [shell.md](shell.md) | not a module but eight roles; `Library.cpp` 2,473 lines; six image caches with copied plumbing; ~15 compatibility paths |
| Session, render, audio, hwr, markdown | [session.md](session.md) | the layering is right; `HybridPdf.cpp` 4,549 lines; a PDF is written 12 ways; two lock-rule breaks |
| Canvas, tests, build, CI, docs | [infra.md](infra.md) | `CanvasView` is a god object; UI tests ~1,700 s with ~1,000 fixed waits and a fixture copied 23 times; docs describe history, not the system |

## Real bugs found on the way (each gets a failing test first)

1. The reference view copies PDF text that the PDF forbids copying (app-cpp §1.4: `ReferenceMode.cpp:502/583` vs
   `AppController.cpp:4948`).
2. Writing tags races with a save; on a PDF with versions it adds a revision counted as "another app" (session §6).
3. Render threads read `CanvasView::pdfCache` while the UI thread replaces it (infra §1.1/§6).
4. Several Escape shortcuts enabled at once are ambiguous and none acts (qml §3: full screen with a selected note,
   an armed snip, the to-do stamp, the replay; presenting likewise).
5. Shutdown stops 3 of 6 image worker pools; background jobs on the global pool are not awaited (shell §6,
   app-cpp §6).
6. A whole attached PDF is written, and a PDF page rendered, on the UI thread under the document lock (session §6).

## Decisions taken for this round (the author may overrule)

- **Compatibility removed** with xournal-qt's own older data: cache/index format upgrades, the toolbox v1 → v2
  upgrade, the classic tool bar → toolbox migration, the 0.7.0 `layout/*/chrome` choice, one-time settings flags,
  old text-document bookmarks, "files from older versions" in HybridPdf. **Kept**: Xournal++'s formats (.xopp, .xoj,
  the shared settings.xml), PDFs of other apps, Qt 6.7+.
- **The deprecated text mode is removed** (`TextFlowPanel`, `TextFlowEditor`, most of `TextFlow`; `TextFlow::styleFor`
  stays for Markdown): it can no longer be opened.
- **The text tool is always Markdown** (the `textMarkdown` setting could only be switched on). Plain Xournal++ text
  boxes in .xopp/.xoj files are still read, rendered and handled as before (the author: "still want to be able to read
  and render normal xournal++ text boxes as intended. But my app always uses markdown.").
- **main.cpp's unused screenshot hooks** (77 lines) and the actions only they use are removed if nothing calls them.
- **Docs**: describe how the system works now; history keeps only the big decisions and turning points (the author:
  "do not keep irrelevant old history on some obscure details … keep big blocks such as the classic toolbar"); git
  keeps the rest.
- `app` and `win` stay context properties (undocked windows share one engine, each with its own `app`); a QML-API
  test checks every `app.*` name QML uses against the C++ meta-objects instead.

## The plan, in waves

A wave's blocks run in parallel (separate worktrees); they were chosen so that they touch different files. Every
block: full suite before merging, Qt ≤ 6.7 API, UI tests also on the Qt 6.8 build where popups/keys/windows change.

**Wave 1**
- `qt/compat-dead`: remove every compatibility path and the dead code listed in the five reviews (shell blocks 1, 2,
  11; session block 2; app-cpp block B; qml block B1; infra blocks B6, B7), the deprecated text mode, the screenshot
  hooks; the tests that only pin removed behaviour go with them.
- `qt/docs-structure`: `qt/docs/README.md` as the entry point; history (ROADMAP's changelog, done TODO items, the
  device checklist's done parts) moved under `qt/docs/history/`; feature docs under `features/`, decisions under
  `decisions/`, development (build, tests, CI, agents) under `development/`; a README per `qt/src/<module>/`;
  AGENTS.md and TODO.md current (infra B14, B15). No code.

**Wave 2**
- `qt/qml-split`: the mechanical split of `Main.qml` by qml.md §1.2 (qml B2; one agent alone in `Main.qml`).
- `qt/test-support`: one shared UI fixture and test helpers (`waitFor` that fails on timeout, `makeTextPdf`, …),
  fewer fixed waits (infra B1, B3; app-cpp A's QML-API test; qml B10); tests only.
- `qt/session-io`: `FileIo` helpers (one atomic write with fsync, one hash, one stamp), the tag/save race, the UI-thread
  I/O under the lock (session blocks 1, 6; shell block 9) — bugs 2 and 6 with failing tests first.
- `qt/canvas-race`: the PDF cache race (bug 3) and the clock injection for the canvas tests (infra B10's race part,
  B11's clock).

**Wave 3**
- `qt/qml-split-2`: HomeView and SettingsPage splits, the ordered Escape/Back dispatcher (bug 4), state objects out of
  `win` (qml B4, B5, B7, B8).
- `qt/library-split`: `Library.*` split, shared image-provider infrastructure and shutdown (bug 5) (shell 3, 4, 5).
- `qt/hybridpdf-split`: `HybridPdf.cpp` split and the shared marker writer (session 3, 5).
- `qt/app-services`: `AppServices` / `OpenDocuments` / `CurrentDocument`, the reference's shared `CanvasActions` and
  bug 1 (app-cpp C, D).

**Wave 4**
- `qt/architecture`: the architecture overview for humans: `ARCHITECTURE.md` (GitHub renders it: an SVG diagram and
  tables linking to the source on GitHub) generated from a machine-readable `qt/docs/architecture/architecture.yaml`,
  and the same as an interactive HTML page published to GitHub Pages by a workflow.

**Later rounds** (written down here so a fresh session can pick them up): the feature objects out of `AppController`
(app-cpp E–N, `app.versions` first as the template), `CanvasView` steps (infra B8, B9), `CanvasInput` split (B11),
non-view code out of canvas (B12), `DocumentSession` split (session 4), one PDF-writing entry point (session 7),
`LibraryService` and the list-model base (shell 6, 7, 8), typed settings (shell 10, infra B13), module-qualified
includes and the CMake module layout (infra B4, B5; shell 13), `DocumentCanvasItem` split (app-cpp O), shared small
QML components (qml B11).
