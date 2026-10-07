# Working in this repository

xournal-qt is a **fork of Xournal++** ([FORK.md](FORK.md)): upstream's C++ core with a new Qt 6 / Qt Quick frontend.
Everything of the fork lives under `qt/`; the rest of the tree is upstream and is touched as little as possible.
`master` follows upstream, **`master-qt` is the fork's branch** and the one to work on.

## Build and test

```sh
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXQT_FAST_DEV=ON -DXQT_BUILD_SPIKES=OFF  # once
cmake --build build-qt -j8 --target <the test binary you need>
ctest --test-dir build-qt -j8 -L canvas    # labels: unit session canvas markdown audio hwr quick shell ui golden
ctest --test-dir build-qt -j8              # the full suite: 1865 tests
```

- `XQT_FAST_DEV=ON` is for development builds only: QML not compiled ahead of time (a QML edit rebuilds in seconds),
  `-g1`, lld. CI and releases leave it off. The CI builds with Qt 6.7 (KDE neon) and 6.8 (Debian 13): **no Qt API
  newer than 6.7** in C++ or QML (e.g. `AbstractButton.click()` is 6.8), and no QML property named like a JS global.
- **Tiers**: while working, only the labels or `-R` filters of what changed, and only the targets you need; the full
  suite of a block **on GitHub**: push `qt/<block>` and `xqt-block-tests.yml` builds it once and runs the suite in four
  shards (the working machine stays free for editing and building); the CI with both Linux Qt versions and QML
  compiled ahead of time (`xqt-build.yml`) on every push of `master-qt` and `claude/**`; the author tests by hand at
  the end. On a big machine the full suite can of course run locally too (`ctest -j$(nproc)`). Don't rebuild or retest after edits to docs or QML text alone.
- Tests run off-screen with temporary config and cache folders. They never write into `test/files` (upstream's
  fixtures), the working directory or the author's configuration. Flaky tests: rerun alone first; the known ones are
  in [TODO.md](TODO.md), "Flaky tests".
- The testing guide (binaries, fixtures, writing UI tests, env variables such as `XQT_SHOTS`, `XQT_BENCH_*`,
  `XQT_PERF`): [qt/docs/testing/README.md](qt/docs/testing/README.md).
- **Cloud container** (Ubuntu 24.04, Qt 6.4 only, no GitHub downloads): `qt/scripts/cloud-env.sh` installs Qt 6.9
  and the libraries from conda-forge into `/opt/xqt-env`. In every shell that builds or tests:
  `source /opt/xqt-env/cloud-env.env`, configure with `$XQT_CMAKE_ARGS`. Use that env's `ctest` (tests are listed
  when ctest runs; another CMake's ctest aborts). **Qt 6.8**: `source /opt/xqt-env68/cloud-env.env`, build dir
  `/home/user/build-qt68`; run the UI tests there when you touch popups, keys or window states.
- `qt/scripts/linux-deps.sh` installs the build's packages on Debian, Ubuntu and KDE neon.

## Rules that are easy to break

1. **Upstream files stay upstream.** Never delete, move or reformat anything outside `qt/`. An unavoidable seam is
   tiny, marked `xournal-qt:`, and listed in [qt/docs/adr/0002-upstream-seams.md](qt/docs/adr/0002-upstream-seams.md).
2. **The author's data is not yours.** Never touch `~/.config/xournalpp`, `~/.config/xournal-qt` or their documents.
3. **Ask before anything leaves the machine**: pushing branches or tags, publishing a release, building the `.deb`.
4. **One feature, one commit.** Before committing: the tests you ran for it pass; its feature doc in `qt/docs/` says
   how it works now; checks that only a real device can make go into
   [qt/docs/testing/device-checklist.md](qt/docs/testing/device-checklist.md) (short, by area). Commit messages are
   plain prose: what was wrong, what changed, why.
5. **A bug gets a failing test first.** Show it fails for the stated reason, then fix it.
6. Keep the routine test run **under a minute**. Long suites go behind a label or an environment variable.
7. **Vendored code must be committed whole.** A global gitignore here ignores `lib/`, `env/`, `build/`, `out/`,
   `dist/`: after adding code under `qt/3rdparty/`, check `git status --ignored qt/3rdparty` and `git add -f`.

## How work is organised

- Open work is in [TODO.md](TODO.md), in **blocks**. A block is a branch `qt/<block>` in its own worktree
  `../xournal_qt-<block>` with its own `build-qt`; ccache (4.7 or newer) shares objects between worktrees. Blocks are
  usually done by agents; the main session (the integrator) works on architecture and integration and merges them
  into the integration branch after their full suite passed, with an annotated tag `ms/<date>-<block>` on the merge
  commit (`qt/scripts/agents/merge-block.sh`). The brief every block agent gets, and the integrator's routine:
  [qt/docs/agents/block-brief.md](qt/docs/agents/block-brief.md).
- Parallel builds: a 4-core machine takes two or three agents at once; with several agents on a small machine, run
  builds and tests through `qt/scripts/build-slot.sh`. A bigger machine takes more (each build dir ~1.2 GB with
  FAST_DEV).
- **The refactoring of 2026-10**: the reviews and the plan in waves are in
  [qt/docs/review/2026-10/README.md](qt/docs/review/2026-10/README.md); a refactoring changes structure, not behaviour.
- **Where to record what**: open work → TODO.md (done items are deleted at the merge); how a feature works now →
  its doc in `qt/docs/`; why → an ADR in `qt/docs/adr/`; a user-visible change → the next draft in
  `qt/docs/release-notes/`; device-only checks → the device checklist; turning points → `qt/docs/history/README.md`.
- [VISION.md](VISION.md) holds the author's goals. Read it before planning; add nothing the author did not say.

## Where things are

Dependencies point down this list only (`xqt-shell` also compiles `src/app`; the review plans to split them):

| Path | Target | What |
| --- | --- | --- |
| `src/util`, `src/core` | `xoj-util`, `xoj-core`, `xoj-tools` | upstream's Qt-free core, built through the shadow headers of `qt/compat` |
| `qt/src/render` | `xoj-render` | `PageRaster`, `RenderService` (worker threads); Qt-free |
| `qt/src/markdown`, `qt/src/audio` | `xqt-markdown`, `xqt-audio` | the Markdown engine (md4c, layout, pagination); recording and playing |
| `qt/src/session` | `xqt-session` | `DocumentSession` (one open document, undo, autosave), `AppContext`, the PDF formats, search |
| `qt/src/canvas` | `xqt-canvas` | `CanvasView` (a document in a view), `CanvasPage`, `CanvasInput`, tools, editors, `CanvasMemory` |
| `qt/src/hwr` | `xqt-hwr` | handwriting search (ONNX Runtime loaded at run time) |
| `qt/src/quick` | `xqt-quick` | `DocumentCanvasItem`: the canvas in the scene graph, the input filter |
| `qt/src/shell` | `xqt-shell` | library, tabs, image providers, models for the QML lists, settings models |
| `qt/src/app` | `xqt-shell`, `xqt-ui`, `xournal-qt` | `AppController` (the QML API `app`), `main.cpp`, `qml/` (the whole UI) |
| `qt/cli`, `qt/tools` | `xournal-qt-cli`, `xoj-imgdiff` | headless export (upstream's flags), developer tools |
| `qt/tests` | one binary per label | `unit session canvas markdown audio hwr quick shell ui`, plus `golden`; shared helpers in `support/` (`xqt-test-support`), the UI fixture in `ui/UiFixture.h` |
| `qt/3rdparty`, `qt/packaging`, `qt/scripts` | | vendored libraries; packaging; build, deploy and environment scripts |

## What the moving parts assume

- **Pages are drawn on many threads at once** (visible renders, two background renders, two sketch workers, the
  image providers' workers). Cairo and Pango objects belong to one thread (`thread_local`), poppler draws one page of
  an instance at a time (its own mutex), and the document is read under `std::shared_lock`. Never hold the document
  lock while drawing a PDF.
- **Every page has a revision** (`DocumentSession::pageRevision`) that changes when its picture does. Thumbnails,
  sketches, stand-ins and their files on disk are named by it; a page keeps its revision when pages before it come or go.
- **Memory has owners**: `CanvasMemory` for rendered pages (a setting, shared by all tabs), `ImageMemory` for the
  limits of all image caches (thumbnails, sketches, stand-ins, covers, …; [qt/docs/image-caches.md](qt/docs/image-caches.md)),
  `ImageWorkers` for their threads. A cache without an owner and a limit is how this got slow before.
- **Work that is not for right now goes to a background worker** at idle priority, and nothing is ever drawn in front
  of the page the reader is looking at.
- **QML items that a test needs carry an `objectName`.** UI tests drive the real window off-screen.

## Documents

[VISION.md](VISION.md) (goals) · [TODO.md](TODO.md) (open work) · [FORK.md](FORK.md) (branches, fork rules) ·
[qt/docs/](qt/docs/) (one doc per feature; [adr/](qt/docs/adr/) the decisions; [releasing.md](qt/docs/releasing.md)
CI and packages; [history/README.md](qt/docs/history/README.md) how it came to be) ·
[qt/docs/review/2026-10/docs-plan.md](qt/docs/review/2026-10/docs-plan.md) (the docs structure planned for wave 4).
