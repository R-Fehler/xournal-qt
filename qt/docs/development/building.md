# Building

The fork's build root is `qt/CMakeLists.txt`, not the repository's root (that is upstream's GTK build, which the fork
leaves untouched and does not use).

## On a Linux machine

```sh
qt/scripts/linux-deps.sh                                   # Debian, Ubuntu, KDE neon: the packages the build needs
cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXQT_FAST_DEV=ON
cmake --build build-qt -j$(nproc) --target <what you need>  # e.g. xournal-qt, xqt-canvas-tests
./build-qt/xournal-qt
```

- **Qt**: `find_package(Qt6 6.5)` is declared, but the CI and the releases build with Qt 6.7 (KDE neon,
  `XQT_NEON=1 qt/scripts/linux-deps.sh` on Ubuntu 22.04) and 6.8 (Debian 13). **Use no Qt API newer than 6.7** in
  C++ or QML (e.g. `AbstractButton.click()` is 6.8), and no QML property named like a JS global (`console`, …).
  Ubuntu 24.04's own Qt (6.4) is too old.
- **qpdf** is built with the app (`qt/cmake/XqtQpdf.cmake` downloads its pinned source when the build is configured);
  `-DXQT_SYSTEM_QPDF=ON` takes the system's (12 or newer).
- **`XQT_FAST_DEV=ON`** is for development builds only (CI and releases leave it off): QML is not compiled ahead of
  time (an edit of a QML file rebuilds in seconds instead of minutes), debug info is line tables only (`-g1`), and lld
  links when installed. A build folder is about 1.2 GB.
- Other options (`qt/CMakeLists.txt`, `qt/cmake/*.cmake`): `XQT_BUILD_TESTS`, `XQT_BUILD_CLI` (on on the desktop),
  `XQT_AUDIO` / `XQT_REQUIRE_AUDIO` (Qt Multimedia for recordings), `XQT_HWR_ONNX` (handwriting search),
  `XQT_BUILD_SPIKES` (off: the input spike of ADR 0001 in `qt/spikes/`).
- **ccache** is used when installed (`XQT_USE_CCACHE`), with the workspace as its base directory, so a new worktree
  reuses the objects the other checkouts compiled. It needs ccache 4.7 or newer.
- Build only the targets you need; the test binaries and their labels are in [testing/README.md](../testing/README.md).

### A big machine

A build of everything without a compiler cache takes about a quarter of an hour on the CI's runners and scales with
the cores. On a machine with many cores and plenty of memory: `-j$(nproc)` for the build, and the full suite locally (`ctest --test-dir build-qt
-j$(nproc)`; 1865 tests, the `ui` label is most of the time). Several worktrees can build side by side (each build
folder about 1.2 GB with FAST_DEV); ccache shares their objects.

### A small machine shared by several builds

On a 4- or 8-thread machine, run at most two or three builds at a time, and with several agents at once run every
build and test through `qt/scripts/build-slot.sh` (two slots, each capped at 3 CPUs and 4 GB, low priority) with
`-j3`: five unlimited builds once ran a 16 GB machine out of memory.

```sh
qt/scripts/build-slot.sh cmake --build build-qt -j3 --target xqt-ui-tests
```

Leave the full suite to GitHub there ([ci.md](ci.md), "Block tests").

## In a Claude Code cloud container

The container (Ubuntu 24.04) has only Qt 6.4 and cannot download from GitHub. `qt/scripts/cloud-env.sh` sets up Qt and
the libraries from conda-forge (several GB the first time; seconds when it is there):

| Environment | Qt | Use | Build folder |
| --- | --- | --- | --- |
| `/opt/xqt-env` | 6.9 | the default | `build-qt` in the worktree |
| `/opt/xqt-env68` | 6.8 | UI tests of popups, keys and window states (Qt 6.7 and 6.8 differ: e.g. a closing popup still takes keys) | a second build folder (`build-qt68`) |

```sh
source /opt/xqt-env/cloud-env.env                         # in every shell that builds or tests
cmake -S qt -B build-qt $XQT_CMAKE_ARGS -DXQT_FAST_DEV=ON
```

Use the `ctest` of the CMake that configured the build (the one the environment puts on `PATH`): the tests are listed
when ctest runs (`PRE_TEST` discovery), and a `ctest` of another CMake version aborts on every label.

## The CI's containers on a desktop

`qt/scripts/ci-container.sh` builds and tests in the CI's containers (Debian 13 with Qt 6.8, or KDE neon with Qt
6.7), to reproduce a failure that shows only on GitHub: [ci.md](ci.md).

## Other platforms

[android.md](android.md), [windows.md](windows.md), [macos.md](macos.md). Their workflows run when a release is cut or
when started by hand ([ci.md](ci.md)); [releasing.md](releasing.md) has the packages.

## Vendored code

`qt/3rdparty/` holds vendored libraries (md4c, MicroTeX, libogg, libvorbis, ONNX Runtime's headers, gemoji); each has
a `README.xournal-qt.md` with its version and what was changed. A global gitignore on some machines ignores common
folder names (`lib/`, `env/`, `build/`, `out/`, `dist/`): after adding code there, check
`git status --ignored qt/3rdparty` and add what is missing with `git add -f` (plus a `.gitignore` there with
`!name/`). A build in the worktree works with the files uncommitted; a clean checkout does not.
