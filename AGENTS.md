# Working in this repository

xournal-qt is a **fork of Xournal++** (see [FORK.md](FORK.md)): upstream's C++ core with a new Qt 6 / Qt Quick
frontend. Everything of the fork lives under `qt/`; the rest of the tree is upstream and is touched as little as
possible. `master` follows upstream, **`master-qt` is the fork's branch** and the one to work on.

## Build and test

```sh
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # once
cmake --build build-qt -j8 --target <the test binary you need>
ctest --test-dir build-qt -j8 -L markdown   # labels: unit session canvas markdown quick shell ui golden
ctest --test-dir build-qt -j8               # full suite (441 tests): at integration only, not per commit
```

The machine is a slow 2-in-1, so every build and test run costs real time.
- Build only the targets you need.
- Run only the test labels, or `-R` filters, for the code you changed.
- The full suite runs when a block is merged into `master-qt`, or when the author asks. The author runs the long
  suites and tests the app by hand at the end.
- Don't rebuild or retest after edits to docs or QML text alone.

`qt/scripts/linux-deps.sh` installs what the build needs (Debian, Ubuntu, KDE neon). Qt 6.5 or newer (`find_package(Qt6 6.5)`).

- `build-qt` is for the tests. `build-release` is the build the author tries on the device. Rebuild it after
  integrating into `master-qt` (`cmake --build build-release -j8`), and do not leave it broken.
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
3. **Ask before anything leaves the machine**: pushing (branches or tags), publishing a release, building the `.deb`.
   Local milestone tags are wanted: every block merged into `master-qt` gets an annotated tag
   `ms/<date>-<block>` on its merge commit, with a one-line summary (`git tag -l 'ms/*' -n1` lists them).
4. **One feature, one commit.** Before committing:
   - the tests you ran for it pass;
   - the device checklist ([qt/docs/testing/device-checklist.md](qt/docs/testing/device-checklist.md)) is
     updated;
   - a short plain report follows, one per feature, not a batch at the end.

   Commit messages are plain prose: what was wrong, what changed, why.
5. **A bug gets a failing test first.** Show it fails for the stated reason, then fix it.
6. Keep the routine test run **under a minute**. Long suites go behind a label or an environment variable.
7. **Vendored code must be committed whole.** A global gitignore on this machine ignores common folder names
   (`lib/`, `env/`, `build/`, `out/`, `dist/`). After adding code under `qt/3rdparty/`, check
   `git status --ignored qt/3rdparty` and add what is missing with `git add -f` (plus a `.gitignore` there with
   `!name/`). A build in the worktree still works with the files uncommitted; a clean checkout does not.

## How work is organised

- The tasks are in [TODO.md](TODO.md), grouped into **blocks**. A block is one branch `qt/<block>` in its own
  worktree `../xournal_qt-<block>`, with its own `build-qt`. The build uses ccache when it is installed, with
  the workspace as its base directory, so a new worktree reuses the objects the other checkouts already compiled. This needs ccache 4.7 or newer:
  older versions put the object path in the key, and CMake names upstream `src/` objects after the worktree's
  absolute path. Ubuntu 22.04 ships 4.5; 4.14 is installed in `~/.local/bin`.
- Feature blocks are usually done by subagents. The main session works on architecture and integration, merging
  blocks into `master-qt`.
- Run at most two worktree builds at a time; the machine has 8 threads and 16 GB. When several agents work at once,
  run every build and test through `qt/scripts/build-slot.sh` (two slots, each capped at 3 CPUs and 4 GB, low
  priority) with `-j3`: five unlimited builds once froze the machine (RAM and swap full).
- [VISION.md](VISION.md) holds the author's goals. Read it before planning. Don't add anything the author did not
  say.

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

[VISION.md](VISION.md) (goals) · [TODO.md](TODO.md) (open tasks) · [FORK.md](FORK.md) (branches, fork rules) · [qt/docs/ROADMAP.md](qt/docs/ROADMAP.md) (what exists, what is planned,
what was measured) · [qt/docs/adr/](qt/docs/adr/) (why the fork is built this way) ·
[qt/docs/markdown-boxes.md](qt/docs/markdown-boxes.md) · [qt/docs/library.md](qt/docs/library.md) ·
[qt/docs/releasing.md](qt/docs/releasing.md) (CI, packages)
