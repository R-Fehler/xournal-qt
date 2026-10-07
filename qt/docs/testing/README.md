# Testing

All tests are GoogleTest binaries (`qt/tests/<label>/`), discovered by ctest when it runs. They run off-screen
(`QT_QPA_PLATFORM=offscreen`) with temporary config, cache and data folders (see each `qt/tests/*/main.cpp`), so they
never touch the user's configuration. **They must never write into `test/files`** (upstream's fixtures) or into the
working directory.

## Binaries and labels

1865 tests (2026-10-07, `ctest -N`):

| Label | Binary | Folder | Tests | Time (serial) | What it covers |
| --- | --- | --- | --- | --- | --- |
| `unit` | `xoj-unit-tests` | `qt/tests/unit` + upstream's `test/unit_tests` | 140 | ~3 s | upstream's unit tests, `PageRaster`, region renders, undo |
| `markdown` | `xqt-markdown-tests` | `qt/tests/markdown` | 141 | ~5 s | the Markdown engine |
| `audio` | `xqt-audio-tests` | `qt/tests/audio` | 21 | | Ogg Vorbis, recorder, player (fake devices) |
| `session` | `xqt-session-tests` | `qt/tests/session` | 269 | ~26 s | `DocumentSession`, the PDF formats, file formats |
| `canvas` | `xqt-canvas-tests` | `qt/tests/canvas` | 302 | ~65 s | views, input replay, editors, tools |
| `hwr` | `xqt-hwr-tests` | `qt/tests/hwr` | 84 | | layout, recognisers (tiny stand-in models in `qt/tests/hwr/data`), search |
| `quick` | `xqt-quick-tests` | `qt/tests/quick` | 64 | ~74 s | `DocumentCanvasItem`: rendering, input, fractional scales |
| `shell` | `xqt-shell-tests` | `qt/tests/shell` | 424 | ~62 s | library, tabs, models, settings, CLI |
| `ui` | `xqt-ui-tests` | `qt/tests/ui` | 419 | ~1700 s | the real window (`Main.qml` + `AppController`) driven off-screen |
| `golden` | `run_golden.sh` | `qt/tests/golden` | 1 | | CLI output against upstream's (skips without an upstream build) |

A few tests run twice with another environment: `FractionalScaleCanvas.quick@125/@167`, `FractionalScale.ui@150`,
`ReadingPhone.ui@phone`, `TimelinePhone.ui@phone`, `PresenterView.ui@2screens` (off-screen screens from
`qt/tests/ui/offscreen-*.json`) and `DarkPagesCanvas.quick@gl` (OpenGL under Xvfb when it is installed).

```sh
cmake --build build-qt -j8 --target xqt-canvas-tests
ctest --test-dir build-qt -j8 -L canvas
ctest --test-dir build-qt -R 'CanvasReplayTest\.' --output-on-failure
build-qt/xqt-ui-tests --gtest_filter='ToolboxTest.*'          # one binary directly, for a debugger
```

## Tiers

1. **While working**: only the labels, or `-R` filters, of what changed. Build only those test binaries. Don't
   rebuild or retest after edits to docs or QML text alone.
2. **Before a block is merged**: the full suite once (`ctest --test-dir build-qt -j3`), and the UI tests of popups,
   keys and window states also on Qt 6.8 (below).
3. **The CI** (`.github/workflows/xqt-build.yml`) builds and runs everything on Debian 13 (Qt 6.8) and KDE neon
   (Qt 6.7) on every push of `master-qt` and `claude/**`. A test that fails only there: reproduce it in the same
   container with `qt/scripts/ci-container.sh` ([releasing.md](../releasing.md)).
4. **The author** runs the long suites and tests by hand on the devices ([device checklist](device-checklist.md)).

The routine run should stay **under a minute**: long tests go behind a label or an environment variable. The UI label
breaks this today (each test starts its own process and loads the whole UI); the refactoring's wave 2 works on it.

## Writing a test

- **A bug gets a failing test first.** Show that it fails for the stated reason, then fix it.
- Start from the closest existing test of the same layer: a session test needs no view; a canvas test builds a
  `CanvasView` on a `DocumentSession`; a UI test starts from the fixture of `MainWindowTest.cpp` (its `SetUp`,
  `find`/`findItem`, `click`, `key`, `until`). One shared fixture and shared helpers (`qt/tests/support/`) are being
  built in wave 2 (`qt/test-support`); until then helpers such as `waitFor` and `makeTextPdf` are copied per file.
- **QML items a test needs carry an `objectName`.** Find them by it, never by position or text.
- **Wait for a state, not for time.** Prefer `QTest::qWaitFor(condition)` (inside `ASSERT_TRUE`) over a fixed
  `wait(N)`; a fixed wait is either too long or flaky under load.
- **Canvas tests do not sleep.** The view and its input run on a `ManualClock` (`qt/src/canvas/Clock.h`; give it with
  `view->setClock(clock)` before the `CanvasInput` is made). The fixtures' `processEvents(ms)` move it on with
  `test::passTime` (`qt/tests/canvas/CanvasTime.h`): long presses, flicks, the momentum, the zoom settling and the page
  animations take no real time, and the renders are waited for. Timers outside the canvas (upstream's edge panning of a
  selection, the laser pointer's fading) still run in real time.
- **Only Qt ≤ 6.7 API**, in tests as in the app: e.g. `AbstractButton.click()` is 6.8, so tests emit
  `clicked`/`triggered` instead. No QML property names that are JS globals (`console`, …).
- Fixture files a test needs are written into a `QTemporaryDir` (or read from `qt/tests/<label>/` data folders);
  never write next to the sources.

## Flaky tests

Rerun a failure alone (`ctest -R '<name>' --repeat until-fail:5`) before calling it a flake. The known ones are listed
in [TODO.md](../../../TODO.md), "Flaky tests"; add a new one there with what was seen (load, Qt version, how often).
The CI still repeats a failed test up to twice (`--repeat until-pass:3`); that is to go once the suite is stable.

## Qt 6.8

The CI's Debian job runs Qt 6.8, whose popups, file dialogs and focus behave differently from 6.7 and 6.9 (a closing
popup still takes keys; a menu ignores a click on its button while it fades out; a file dialog is a window of its own).
In the cloud container a Qt 6.8 environment exists:

```sh
source /opt/xqt-env68/cloud-env.env
cmake -S qt -B /home/user/build-qt68 $XQT_CMAKE_ARGS -DXQT_FAST_DEV=ON
cmake --build /home/user/build-qt68 -j3 --target xqt-ui-tests && ctest --test-dir /home/user/build-qt68 -L ui -R '<area>'
```

## Environment variables

| Variable | Effect |
| --- | --- |
| `XQT_BENCH_PDF=<file>`, `XQT_BENCH_SCROLL`, `XQT_BENCH_HYBRID`, `XQT_BENCH_STICKY`, `XQT_BENCH_GEOMETRY` | turn on benchmarks that otherwise skip |
| `XQT_SHOTS=<dir>` | regenerates the README's pictures from the UI tests (also `XQT_TOOLBAR_SHOTS`, `XQT_STICKY_SHOTS`, `XQT_MATH_SHOTS`) |
| `XQT_UPSTREAM_BIN` | upstream's `xournalpp` for the golden tests and `StickyNoteTest` |
| `XQT_HWR_MODEL`, `XQT_HWR_CTC_MODEL` | run the handwriting tests with a real model folder |
| `XQT_PERF=1` | the running app writes a line a second about the canvas work ([performance-logging.md](performance-logging.md)) |
| `XQT_KEEP`, `XQT_KEEP_PDF=<file>` | keep the PDFs some Markdown tests write, to look at them |

`qt/tools/tsan.supp` holds the suppressions for a ThreadSanitizer build (no CI job uses it yet).
