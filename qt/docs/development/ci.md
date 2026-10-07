# CI

The fork's workflows are the `xqt-*.yml` files in `.github/workflows/`. The upstream Xournal++ workflows in the same
folder stay dormant here: they only run for pull requests to `master` or carry
`if: github.repository == 'xournalpp/xournalpp'`.

| Workflow | When | What |
| --- | --- | --- |
| `xqt-build.yml` | a push to `master-qt` or `claude/**` that touches `qt/`, `src/`, `test/`, `cmake/` or the workflow; pull requests to `master-qt`; by hand | **Doc links**: `qt/scripts/check-doc-links.py` (below). **Linux**: the release configuration (QML compiled ahead of time) in two containers, Debian 13 (Qt 6.8) and Ubuntu 22.04 with KDE neon (Qt 6.7), the full suite in each, and veraPDF on the archive PDFs the tests write |
| `xqt-block-tests.yml` | every push to a block branch `qt/**` (no path filter) and to `master-qt`; by hand | **Block tests**: one development build (`XQT_FAST_DEV`, Debian 13, Qt 6.8), then the full suite in four shards side by side (`ctest -I <shard>,,4`) |
| `xqt-release.yml` | a tag `v1.2.3`; by hand | builds, tests, packages, and opens a **draft** release with the packages ([releasing.md](releasing.md)) |
| `xqt-android.yml` | by hand, a push to `qt/android-build`, the release | the APK ([android.md](android.md)) |
| `xqt-windows.yml` | by hand, a push to `qt/windows-build` or `qt/windows-feel`, the release | the portable zip ([windows.md](windows.md)) |
| `xqt-macos.yml` | by hand, a push to `qt/macos-build`, the release | the unsigned `.dmg` ([macos.md](macos.md)) |

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

## Archive PDFs: veraPDF

The Debian job downloads veraPDF's greenfield CLI from Maven Central (checked by SHA-1), runs the archive tests with
`XQT_ARCHIVE_SAMPLES=<folder>` and fails when an archive PDF that claims PDF/A is not PDF/A-3b
([features/hybrid-pdf.md](../features/hybrid-pdf.md), "Archive PDF"). veraPDF is a test tool only; the app never runs
it.
