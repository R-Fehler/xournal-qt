# Working in this repository

xournal-qt is a **fork of Xournal++** (see [FORK.md](FORK.md)): upstream's C++ core with a new Qt 6 / Qt Quick
frontend. Everything of the fork lives under `qt/`; the rest of the tree is upstream and is touched as little as
possible. `master` follows upstream, **`master-qt` is the fork's branch** and the one to work on.

## Build and test

```sh
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # once
cmake --build build-qt -j8
ctest --test-dir build-qt -j8            # 441 tests, about a minute - run it before every commit
ctest --test-dir build-qt -j8 -L shell   # labels: unit session canvas markdown quick shell ui golden
```

`qt/scripts/linux-deps.sh` installs what the build needs (Debian, Ubuntu, KDE neon). Qt 6.5 or newer (`find_package(Qt6 6.5)`).

- `build-qt` is for the tests. `build-release` is the build the author tries on the device: rebuild it after a
  feature (`cmake --build build-release -j8`), and do not leave it broken.
- Tests run off-screen and use temporary config and cache folders (see each `tests/*/main.cpp`). They must never
  write into `test/files` (upstream's fixtures) or into the author's real configuration.
- Some tests are benchmarks that skip unless an environment variable is set (`XQT_BENCH_PDF=<file>`, `XQT_BENCH_SCROLL`).
  `XQT_PERF=1` makes the running application write a line a second about the canvas work
  ([qt/docs/testing/performance-logging.md](qt/docs/testing/performance-logging.md)); `XQT_SHOTS=<dir>` regenerates
  the README's pictures from the UI tests.

## Rules that are easy to break

1. **Upstream files stay upstream.** Never delete, move or reformat anything outside `qt/`. Where a seam is
   unavoidable, keep it tiny, mark it with a `xournal-qt:` comment, and list the file in
   [qt/docs/adr/0002-upstream-seams.md](qt/docs/adr/0002-upstream-seams.md). Everything else belongs under `qt/`.
2. **The author's data is not yours.** Never touch `~/.config/xournalpp`, `~/.config/xournal-qt` or their documents.
3. **Ask before anything leaves the machine**: pushing, tagging, publishing a release, building the `.deb`.
4. **One feature, one commit**, with the tests green, the device checklist updated
   ([qt/docs/testing/device-checklist.md](qt/docs/testing/device-checklist.md)) and a short plain report afterwards -
   not a batch of features at the end. Commit messages are plain prose: what was wrong, what changed, why.
5. **A bug gets a failing test first.** Show it fails for the stated reason, then fix it.
6. Keep the routine test run **under a minute**. Long suites go behind a label or an environment variable.

## Where things are

| Path | What |
| --- | --- |
| `qt/src/app` | `AppController` (the QML API), `main.cpp`, `qml/` (the whole UI) |
| `qt/src/canvas` | `CanvasView` (a document in a view), `CanvasPage`, input, geometry tools, `CanvasMemory` |
| `qt/src/render` | `PageRaster`, `RenderService` (worker threads, priorities) |
| `qt/src/session` | `DocumentSession` (one open document, undo, autosave), `DocumentSearch`, `AppContext` |
| `qt/src/shell` | tabs, library, previews, thumbnails, models for the QML lists |
| `qt/src/markdown` | the Markdown engine: md4c, layout, pagination (the editor and its session sit in `canvas`) |
| `qt/src/quick` | `DocumentCanvasItem`: the scene graph of the canvas |
| `qt/tests` | by layer: `unit session canvas markdown quick shell ui`, plus `golden` (opt-in image comparison) |

## What the moving parts assume

- **Pages are drawn on many threads at once** (visible renders, two background renders, two preview workers). Cairo
  and Pango objects belong to one thread (`thread_local`), poppler draws one page of an instance at a time (its own
  mutex), and the document is read under `std::shared_lock`. Never hold the document lock while drawing a PDF.
- **Every page has a revision** (`DocumentSession::pageRevision`) that changes when its picture does. Thumbnails,
  previews and their files on disk are named by it; a page keeps its revision when pages before it come or go.
- **Memory has owners**: `CanvasMemory` for rendered pages (a setting, shared by all tabs), `PageSketches` for the
  previews of every page, `ThumbnailProvider` for the sharp thumbnails. Adding a cache without an owner and a limit
  is how this got slow before.
- **Work that is not for right now goes to a background worker** at idle priority, and nothing is ever drawn in front
  of the page the reader is looking at.
- **QML items that a test needs carry an `objectName`.** UI tests drive the real window off-screen.

## Documents to read when they matter

[FORK.md](FORK.md) (branches, fork rules) · [qt/docs/ROADMAP.md](qt/docs/ROADMAP.md) (what exists, what is planned,
what was measured) · [qt/docs/adr/](qt/docs/adr/) (why the fork is built this way) ·
[qt/docs/markdown-boxes.md](qt/docs/markdown-boxes.md) · [qt/docs/library.md](qt/docs/library.md) ·
[qt/docs/releasing.md](qt/docs/releasing.md) (CI, packages, the known flaky test)
