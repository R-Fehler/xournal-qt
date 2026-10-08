# CI

The fork's workflows are the `xqt-*.yml` files in `.github/workflows/`. The upstream Xournal++ workflows in the same
folder stay dormant here: they only run for pull requests to `master` or carry
`if: github.repository == 'xournalpp/xournalpp'`.

| Workflow | When | What |
| --- | --- | --- |
| `xqt-build.yml` | a push to `master-qt` or `claude/**` that touches `qt/`, `src/`, `test/`, `cmake/` or the workflow; pull requests to `master-qt`; by hand | **Doc links**: `qt/scripts/check-doc-links.py` (below). **Architecture overview**: `qt/scripts/architecture/generate.py --check` (below). **Linux**: the release configuration (QML compiled ahead of time) in two containers, Debian 13 (Qt 6.8) and Ubuntu 22.04 with KDE neon (Qt 6.7), the full suite in each, and veraPDF on the archive PDFs the tests write |
| `xqt-block-tests.yml` | every push to a block branch `qt/**` (no path filter) and to `master-qt`; by hand | **Block tests**: one development build (`XQT_FAST_DEV`, Debian 13, Qt 6.8), then the full suite in four shards side by side (`ctest -I <shard>,,4`) |
| `xqt-release.yml` | a tag `v1.2.3`; by hand | builds, tests, packages, and opens a **draft** release with the packages ([releasing.md](releasing.md)) |
| `xqt-android.yml` | by hand, a push to `qt/android-build`, the release | the APK ([android.md](android.md)) |
| `xqt-windows.yml` | by hand, a push to `qt/windows-build` or `qt/windows-feel`, the release | the portable zip ([windows.md](windows.md)) |
| `xqt-macos.yml` | by hand, a push to `qt/macos-build`, the release | the unsigned `.dmg` ([macos.md](macos.md)) |
| `xqt-pages.yml` | a push to `master-qt` that touches `qt/docs/architecture/` or its generator; by hand | publishes the [architecture page](../architecture/README.md) to GitHub Pages (below) |

Qt 6.5 or newer is needed, which the runners' own Ubuntu 24.04 does not have (6.4): every Linux job builds in a
container, and `qt/scripts/linux-deps.sh` installs the packages (the same script as on a developer machine). The
compiler cache of the last run of each container is restored (`actions/cache`), so a build that changed a few files
takes minutes. A failed test runs up to twice more (`--repeat until-pass:3`; each repeat is listed in the log), and a
failed run keeps the test output as an artifact.

## Block tests

A block branch (`qt/<block>`, [workflow.md](workflow.md)) gets its full suite on GitHub, so the machine the work is
done on stays free for editing and building: push the branch, then look at the run of `xqt-block-tests.yml` for it
(the GitHub UI, `gh run list --workflow xqt-block-tests.yml --branch qt/<block>`, or the GitHub tools of an agent).
A failure: reproduce it locally with `ctest -R`, fix, push again. The block tests build without QML compiled ahead of
time and with one Qt version; the integration branch's `xqt-build.yml` adds both Linux Qt versions with QML compiled.

## The link check

`qt/scripts/check-doc-links.py` (the job "Doc links" of `xqt-build.yml`; a checkout and Python only, seconds) checks
that every relative link in the fork's Markdown resolves, anchors (`file.md#heading`) included, and that every
`qt/docs/...` path cited in code (the fork's C++, QML, CMake, scripts and workflows, and the `xournal-qt:` seams in
upstream files) exists. Run it after moving or renaming a doc:

```sh
python3 qt/scripts/check-doc-links.py      # prints file:line: target for each broken one; exit code 1 then
```

Paths cited in the dated records (`review/`, `history/`, `release-notes/`) are not checked (they describe the tree as
it was); their links are.

## The architecture overview

[architecture/README.md](../architecture/README.md), its diagram `architecture.svg` and the interactive page
`architecture/site/index.html` are generated from `qt/docs/architecture/architecture.yaml` by
`qt/scripts/architecture/generate.py` (Python 3, standard library only). The job "Architecture overview" of
`xqt-build.yml` runs it with `--check`: it fails when the generated files are out of date, a path in the YAML does
not exist (an upstream merge that moves a file in `src/` shows up here too), or the YAML's `links` edges and CMake's
`target_link_libraries` between the targets it names disagree. Fix: edit the YAML, then

```sh
python3 qt/scripts/architecture/generate.py      # regenerates README.md, architecture.svg, site/index.html
```

## The architecture page (GitHub Pages)

`xqt-pages.yml` publishes `qt/docs/architecture/site/index.html` (and the SVG) to GitHub Pages with
`actions/upload-pages-artifact` and `actions/deploy-pages`, at `https://r-fehler.github.io/xournal-qt/`. Two things
only the repository's owner can do:

- **Enable Pages once**: Settings → Pages → Build and deployment → Source: **GitHub Actions**. Until then the
  workflow's deploy job fails (the build job still checks the page). Then run the workflow by hand once (Actions →
  "xournal-qt architecture page" → Run workflow), or push a change to the architecture.
- **A private repository needs a paid plan** (GitHub Pro, Team or Enterprise) for Pages; on the free plan Pages
  works for public repositories only. Even from a private repository the published page is public (anyone with
  the address can read it); only GitHub Enterprise Cloud can restrict a page to the repository's readers.

The README version of the overview needs neither: GitHub shows `qt/docs/architecture/README.md` with its diagram on
any branch, and its links (relative) lead into the source. The page also works offline: open
`qt/docs/architecture/site/index.html` from a checkout.

## A test that fails only on GitHub

Run it in the same container: `qt/scripts/ci-container.sh` builds and tests like the CI does, in `debian` (Debian 13,
Qt 6.8) or `neon` (Ubuntu 22.04 with KDE neon, Qt 6.7): root, C locale, the fonts the packages bring, the checkout
mounted read-only, the build folder in `~/.cache/xqt-ci/<name>`. Docker if it runs, else podman; the container gets 3
CPUs and 4 GB, and on a shared machine it goes through `qt/scripts/build-slot.sh` as well.

```sh
qt/scripts/build-slot.sh qt/scripts/ci-container.sh debian build xqt-ui-tests     # about 30 min the first time
qt/scripts/build-slot.sh qt/scripts/ci-container.sh debian test -R 'AdaptiveLayoutTest\.menus' --repeat until-fail:3
qt/scripts/ci-container.sh debian run build/xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.*'
qt/scripts/ci-container.sh neon shell            # look around; `clean` deletes the build folder
```

What makes tests fail there and not on a desktop: other fonts (other text widths, so a menu a fraction of a pixel
narrower), and Qt 6.8, whose own file dialogs are windows of their own (off-screen the app's window does not get the
keys back when one closes) and whose menus take a click on their button only after they have faded out.

## The packages' smoke tests

Each package job tries what it built as a user would get it, and a failure fails the job (the package is published
as an artifact first, so it can be looked at):

| Job | What |
| --- | --- |
| `xqt-release.yml`, `.deb` (both containers) | installs the package in the container (`apt-get install ./…deb`); handwriting: every model of `qt/resources/hwr/` in `/usr/share/xournal-qt/hwr-models/` (`qt/scripts/hwr-package-check.sh models`), `/usr/lib/xournal-qt/libonnxruntime.so.1`, `xournal-qt --hwr-info` |
| `xqt-release.yml`, AppImage | the models in the AppDir, then the AppImage itself: `--hwr-info` (off-screen) |
| `xqt-windows.yml` | `qt/scripts/windows-smoke.sh`: CLI exports, the app, recording (`--audio-info`), handwriting (`onnxruntime.dll` and its Visual C++ DLLs in `bin\`, the models, `--hwr-info`) |
| `xqt-macos.yml` | `qt/scripts/macos-smoke.sh`, with Homebrew hidden: CLI exports, the app, recording, handwriting (`Frameworks/libonnxruntime.1.dylib`, the models, `--hwr-info`) |
| `xqt-android.yml` | no device: `qt/scripts/hwr-package-check.sh apk` looks into the APK for `lib/arm64-v8a/libonnxruntime.so`, ONNX Runtime's licence files and the model files |

`xournal-qt --hwr-info` exits with 1 when ONNX Runtime or a model is missing or its built-in sample is not read. What
each package carries for the handwriting search, and how to bump ONNX Runtime:
[releasing.md](releasing.md#handwriting-onnx-runtime-and-the-model).

## Archive PDFs: veraPDF

The Debian job downloads veraPDF's greenfield CLI from Maven Central (checked by SHA-1), runs the archive tests with
`XQT_ARCHIVE_SAMPLES=<folder>` and fails when an archive PDF that claims PDF/A is not PDF/A-3b
([features/hybrid-pdf.md](../features/hybrid-pdf.md), "Archive PDF"). veraPDF is a test tool only; the app never runs
it.
