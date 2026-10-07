# xournal-qt for developers

Start here. This page says what the project is, how the repository is laid out, and where to read next.

## What xournal-qt is

xournal-qt is a **fork of [Xournal++](https://github.com/xournalpp/xournalpp)** with a new frontend in Qt 6 and Qt
Quick: a pen-first app for handwritten notes and PDF annotation on 2-in-1s, tablets, desktops and phones (Linux first;
Windows, macOS and Android builds exist). It keeps Xournal++'s tested core: the document model, the `.xopp`/`.xoj`
formats (a document written here opens in Xournal++ and the other way round), cairo rendering that is pixel-identical
to upstream's, pressure handling, undo and the drawing tools.

Around that core the fork adds what Xournal++ lacks: several documents as tabs, a library (a plain folder of documents
with an index and search), an interface that adapts from a desktop to a folded phone, a toolbox of the user's own pens,
Markdown written on the page, PDFs that carry their notes (the "PDF with notes", with version history inside the
file), handwriting search, audio recordings and a replay of how a page was written. The author's goals are in
[VISION.md](../../VISION.md); the project is in alpha, with no compatibility promises for its own data.

## How the repository is laid out

| Where | What |
| --- | --- |
| `src/`, `test/`, `ui/`, root `CMakeLists.txt`, … | **Upstream Xournal++**, kept as it is so upstream can be merged ([FORK.md](../../FORK.md)). The Qt build compiles only the GTK-free parts of `src/util` and `src/core`, from an explicit list (`qt/cmake/XojSources.cmake`): the model, the `.xopp` load and save (`control/xojfile`), undo, the tools' input handlers and overlays, the view code that draws pages, PDF access through poppler. Upstream's GTK GUI (`src/core/gui` and the GTK parts of `control`) is not built. Upstream's own GTK build is untouched and not used. |
| `qt/` | **Everything of the fork**, with its own build root `qt/CMakeLists.txt`. |
| `qt/src/<module>/` | The fork's C++ and QML, one folder per module, each with a README: [render](../src/render/README.md), [markdown](../src/markdown/README.md), [audio](../src/audio/README.md), [session](../src/session/README.md), [canvas](../src/canvas/README.md), [hwr](../src/hwr/README.md), [quick](../src/quick/README.md), [shell](../src/shell/README.md), [app](../src/app/README.md) (the whole UI in `app/qml/`). |
| `qt/compat/` | Shadow headers with upstream's names (`Control.h`, `MainWindow.h`, `XojMsgBox.h`, …) and a tiny GTK shim, so upstream's files compile without GTK and without edits ([README](../compat/README.md)). |
| `qt/cmake/` | The build: `XojCore.cmake` and `XojSources.cmake` (upstream's core as `xoj-util`, `xoj-core`, `xoj-tools`, plus `xoj-render`), `XqtMarkdown`, `XqtAudio`, `XqtSession` (session and canvas), `XqtHwr`, `XqtApp` (quick, shell, the UI and the app), `XojTests`, packaging. |
| `qt/tests/` | The tests, one folder per test binary and label, shared helpers in `support/` ([testing/README.md](testing/README.md)). |
| `qt/cli/`, `qt/tools/` | `xournal-qt-cli` (headless export with upstream's flags; the golden tests' driver) and developer tools (`imgdiff`, `merge-upstream.sh`, the TSan suppressions). |
| `qt/3rdparty/` | Vendored libraries (md4c, MicroTeX, libogg, libvorbis, ONNX Runtime's headers, gemoji). |
| `qt/packaging/`, `qt/scripts/` | Desktop files, Android and macOS packaging; build, deploy, environment and CI scripts. |
| `qt/research/` | Research code that is not part of the app (the handwriting models' trials and training). |
| `qt/resources/` | Icons, fonts, palettes, the tutorial. |

**The seams between the two.** Upstream files stay upstream: the fork reaches upstream's code by calling it directly
(the model, the loaders, the views, undo), through the shadow headers of `qt/compat/` where upstream code expects its
GTK application objects (`Control`, `XojPageView`, `Layout`, …, implemented by the fork's `DocumentSession`,
`CanvasPage` and `CanvasView`), and through a few small hooks in upstream files (function pointers a frontend may set:
Markdown texts, sticky notes, textured paper, the UI-thread dispatcher). Every edit of an upstream file is tiny, marked
`xournal-qt:` and listed in [decisions/0002-upstream-seams.md](decisions/0002-upstream-seams.md) (the overview shows
them by module: [architecture](architecture/README.md#how-the-qt-frontend-uses-the-xournal-core)), as are the
upstream classes the fork ported instead of reusing (`PageRaster` from `RenderJob`, `CanvasPage` from `XojPageView`,
…). Both programs share the data: `.xopp`/`.xoj` files (attributes upstream does not know, `xqt-group`,
`xqt-created`, `xqt-bookmark`, are written so Xournal++ ignores them) and upstream's `settings.xml` (the fork's keys in
its `xournalQt` part), in a config folder of its own (`~/.config/xournal-qt`).

## Where to start reading

1. [AGENTS.md](../../AGENTS.md): the build and test commands, the rules that are easy to break (upstream files, the
   author's data, one feature per commit, failing test first), the module table with the direction of dependencies,
   and what the moving parts assume (threads, page revisions, memory owners). They apply to humans too.
2. [architecture/](architecture/README.md): the architecture overview, a diagram of the layers from the QML UI down
   to the Xournal++ core with every block linked to its source, how the Qt frontend uses the core (direct calls,
   `qt/compat`, the seams, the shared files), three paths through the code and where to change what. The same as an
   interactive page: [site/index.html](architecture/site/index.html) (published to GitHub Pages).
3. The README of the module you are about to change (`qt/src/<module>/README.md`).
4. The feature doc of what you are changing: [features/](features/README.md) has one per user-facing feature (how it
   works now, its settings, its files, its code and tests).

## The rest of the docs

| Folder | What |
| --- | --- |
| [development/](development/README.md) | [building](development/building.md) (Linux, the cloud container, Android, Windows, macOS), [CI](development/ci.md) (the workflows, the block tests, the link check), [workflow](development/workflow.md) (blocks, worktrees, merging, where to record what), [releasing](development/releasing.md), [performance logging](development/performance-logging.md) |
| [testing/](testing/README.md) | the testing guide; the [device checklist](testing/device-checklist.md): what only a real device can show |
| [features/](features/README.md) | one doc per feature |
| [architecture/](architecture/README.md) | the architecture overview (generated from `architecture.yaml`), and [image caches](architecture/image-caches.md) |
| [decisions/](decisions/README.md) | the architecture decision records |
| [agents/](agents/block-brief.md) | the brief every block agent gets, and the integrator's routine |
| [user/](user/) | guides for users |
| [release-notes/](release-notes/) | what each release brought; the next one is drafted as work is merged |
| [review/2026-10/](review/2026-10/README.md) | the critical review of 2026-10 and the refactoring plan in waves |
| [history/](history/README.md) | how the project came to be: the turning points and why |

Open work is in [TODO.md](../../TODO.md).
