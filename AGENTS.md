# Working in this repository

xournal-qt is a **fork of Xournal++** ([FORK.md](FORK.md)): upstream's C++ core with a new Qt 6 / Qt Quick frontend.
Everything of the fork lives under `qt/`; the rest of the tree is upstream and is touched as little as possible.
`master` follows upstream, **`master-qt` is the fork's branch** and the one to work on. The developer docs start at
[qt/docs/README.md](qt/docs/README.md).

## Build and test

```sh
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXQT_FAST_DEV=ON   # once
cmake --build build-qt -j8 --target <the test binary you need>
ctest --test-dir build-qt -j8 -L canvas    # labels: unit session canvas markdown audio hwr quick shell ui golden
ctest --test-dir build-qt -j8              # the full suite: 1865 tests
```

- `XQT_FAST_DEV=ON` is for development builds only (QML not compiled ahead of time, `-g1`, lld); CI and releases
  leave it off. The CI builds with Qt 6.7 (KDE neon) and 6.8 (Debian 13): **no Qt API newer than 6.7** in C++ or QML
  (e.g. `AbstractButton.click()` is 6.8), and no QML property named like a JS global.
- **Tiers**: while working, only the labels or `-R` filters of what changed, and only the targets you need; the full
  suite of a block on GitHub (push `qt/<block>`: `xqt-block-tests.yml`) or locally on a big machine
  (`ctest -j$(nproc)`); both Linux Qt versions with QML compiled (`xqt-build.yml`) on `master-qt` and `claude/**`.
  Don't rebuild or retest after edits to docs or QML text alone. After moving a doc:
  `python3 qt/scripts/check-doc-links.py`.
- Tests run off-screen with temporary config and cache folders and never write into `test/files`, the working
  directory or the author's configuration. Flaky tests: rerun alone first; the known ones are in [TODO.md](TODO.md).
- **Cloud container**: `source /opt/xqt-env/cloud-env.env` in every shell that builds or tests (set up by
  `qt/scripts/cloud-env.sh`), configure with `$XQT_CMAKE_ARGS`, use that env's `ctest`. Qt 6.8:
  `/opt/xqt-env68/cloud-env.env`. Details: [building.md](qt/docs/development/building.md).
- Guides: [building](qt/docs/development/building.md), [testing](qt/docs/testing/README.md) (binaries, fixtures,
  writing UI tests, `XQT_*` variables), [CI](qt/docs/development/ci.md).

## Rules that are easy to break

1. **Upstream files stay upstream.** Never delete, move or reformat anything outside `qt/`. An unavoidable seam is
   tiny, marked `xournal-qt:`, and listed in [ADR 0002](qt/docs/decisions/0002-upstream-seams.md).
2. **The author's data is not yours.** Never touch `~/.config/xournalpp`, `~/.config/xournal-qt` or their documents.
3. **Ask before anything leaves the machine**: pushing branches or tags, publishing a release, building the `.deb`
   (block agents may push their own `qt/<block>` branch for its tests).
4. **One feature, one commit.** Before committing: the tests you ran for it pass; its feature doc in
   [qt/docs/features/](qt/docs/features/README.md) says how it works now; checks that only a real device can make go
   into the [device checklist](qt/docs/testing/device-checklist.md) (short, by area). Commit messages are plain prose:
   what was wrong, what changed, why.
5. **A bug gets a failing test first.** Show it fails for the stated reason, then fix it.
6. Keep the routine test run **under a minute**. Long suites go behind a label or an environment variable.
7. **Vendored code must be committed whole.** A global gitignore here ignores `lib/`, `env/`, `build/`, `out/`,
   `dist/`: after adding code under `qt/3rdparty/`, check `git status --ignored qt/3rdparty` and `git add -f`.

## How work is organised

Open work is in [TODO.md](TODO.md), in **blocks**: a branch `qt/<block>` in its own worktree `../xournal_qt-<block>`,
usually done by an agent with [the block brief](qt/docs/agents/block-brief.md), merged by the integrator
(`qt/scripts/agents/merge-block.sh`). Blocks, merging and **where to record what** (TODO, feature doc, ADR, release
notes, device checklist, history): [workflow.md](qt/docs/development/workflow.md). The refactoring of 2026-10 (a
refactoring changes structure, not behaviour): [qt/docs/review/2026-10/README.md](qt/docs/review/2026-10/README.md).
[VISION.md](VISION.md) holds the author's goals: read it before planning; add nothing the author did not say.

## Where things are

Dependencies point down this list only (`xqt-shell` also compiles `src/app`; the review plans to split them). Each
`qt/src/<module>/` has a README (its classes, what it may depend on, its tests and docs). The diagram and the
overview, with the Xournal++ core and every connection to it: [qt/docs/architecture/](qt/docs/architecture/README.md),
generated from `architecture.yaml` (a change that adds, moves or removes a module, a target or a key class updates
it and runs `python3 qt/scripts/architecture/generate.py`; CI checks it).

| Path | Target | What |
| --- | --- | --- |
| `src/util`, `src/core` | `xoj-util`, `xoj-core`, `xoj-tools` | upstream's GTK-free core (model, `.xopp` I/O, undo, tools, views), built through the shadow headers of `qt/compat` from the list in `qt/cmake/XojSources.cmake` |
| `qt/src/render` | `xoj-render` | `PageRaster`, `RenderService` (worker threads); Qt-free |
| `qt/src/markdown`, `qt/src/audio` | `xqt-markdown`, `xqt-audio` | the Markdown engine (md4c, layout, pagination); recording and playing |
| `qt/src/session` | `xqt-session` | `DocumentSession` (one open document, undo, autosave, saving), `AppContext`, the PDF formats, search, `FileIo` |
| `qt/src/canvas` | `xqt-canvas` | `CanvasView` (a document in a view), `CanvasPage`, `CanvasInput`, tools, editors, `CanvasMemory` |
| `qt/src/hwr` | `xqt-hwr` | handwriting search (ONNX Runtime loaded at run time) |
| `qt/src/quick` | `xqt-quick` | `DocumentCanvasItem`: the canvas in the scene graph, the input filter; `AdaptiveLayout` |
| `qt/src/shell` | `xqt-shell` | library, tabs, `CanvasActions`, image providers and their owners (`ImageMemory`, `ImageWorkers`), models for the QML lists, settings models |
| `qt/src/app` | `xqt-shell`, `xqt-ui`, `xournal-qt` | `AppServices` (what the windows share), `AppController` (the QML API `app`), `main.cpp`, `qml/` (the whole UI: `Main.qml` and its parts) |
| `qt/cli`, `qt/tools` | `xournal-qt-cli`, `xoj-imgdiff` | headless export (upstream's flags), developer tools |
| `qt/tests` | one binary per label | `unit session canvas markdown audio hwr quick shell ui`, plus `golden`; shared helpers in `support/` (`xqt-test-support`), the UI fixture in `ui/UiFixture.h` |
| `qt/3rdparty`, `qt/packaging`, `qt/scripts` | | vendored libraries; packaging; build, deploy, CI and environment scripts |

## What the moving parts assume

- **Pages are drawn on many threads at once** (visible renders, two background renders, two sketch workers, the
  image providers' workers). Cairo and Pango objects belong to one thread (`thread_local`), poppler draws one page of
  an instance at a time (its own mutex), and the document is read under `std::shared_lock`. Never hold the document
  lock while drawing a PDF.
- **Every page has a revision** (`DocumentSession::pageRevision`) that changes when its picture does. Thumbnails,
  sketches, stand-ins and their files on disk are named by it; a page keeps its revision when pages before it come or
  go.
- **Memory has owners**: `CanvasMemory` for rendered pages (a setting, shared by all tabs), `ImageMemory` for the
  limits of all image caches ([image-caches.md](qt/docs/architecture/image-caches.md)), `ImageWorkers` for their
  threads. A cache without an owner and a limit is how this got slow before.
- **Work that is not for right now goes to a background worker** at idle priority, on a pool with an owner that
  shutdown stops (`ImageWorkers`, `BackgroundJobs`), and nothing is ever drawn in front of the page the reader is
  looking at.
- **QML items that a test needs carry an `objectName`.** UI tests drive the real window off-screen.

## Documents

[VISION.md](VISION.md) (goals) · [TODO.md](TODO.md) (open work) · [FORK.md](FORK.md) (branches, fork rules) ·
[qt/docs/README.md](qt/docs/README.md) (the developer docs: features, decisions, development, testing, history) ·
[qt/docs/architecture/](qt/docs/architecture/README.md) (the architecture overview).
