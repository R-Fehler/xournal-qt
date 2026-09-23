# Releasing xournal-qt

## What the CI does

| Workflow | When | What |
| --- | --- | --- |
| `.github/workflows/xqt-build.yml` | every push and pull request to `master-qt` | builds in a Debian 13 container (Qt 6.8) and runs all tests |
| `.github/workflows/xqt-release.yml` | a tag `v1.2.3`, or started by hand | builds, tests, packages, and opens a **draft** release with the packages |

The upstream Xournal++ workflows in the same folder stay dormant here: they only run for pull requests to `master`
or carry `if: github.repository == 'xournalpp/xournalpp'`.

Qt 6.5 or newer is needed, which the GitHub runners' own Ubuntu 24.04 does not have (6.4). Every job therefore builds
in a container; `qt/scripts/linux-deps.sh` installs the packages (the same script works on a developer machine).

## Cutting a release

1. Everything green on `master-qt`, and the device checklist walked through (`qt/docs/testing/device-checklist.md`).
2. Set the version in `qt/CMakeLists.txt` (`project(xournal-qt VERSION x.y.z ...)`). The release job refuses a tag
   that says something else.
3. Write `qt/docs/release-notes/x.y.z.md` (the draft takes it as its text; without it GitHub writes a list of commits).
4. Commit, then tag and push:
   ```sh
   git tag -a vx.y.z -m "xournal-qt x.y.z"
   git push origin master-qt vx.y.z
   ```
5. Watch the run. It produces:
   - `xournal-qt_x.y.z_amd64_neon-jammy.deb` — built on Ubuntu 22.04 with KDE neon's packages (Qt 6.7). For KDE
     neon and other Ubuntu 22.04 systems that have Qt 6.5 or newer.
   - `xournal-qt_x.y.z_amd64_debian13.deb` — built on Debian 13. Also for Ubuntu 25.04 and newer.
   - `xournal-qt-x.y.z-x86_64.AppImage` — runs from Ubuntu 22.04 on, whatever Qt the system has. The recipe is new,
     so the job may not produce it (`continue-on-error`); the release does not wait for it.
6. Install a package and try it, then write the notes and publish the draft on GitHub.

Every job also keeps what it built as an artifact of the run (the "Artifacts" box at the bottom of the run's page,
a zip). That is where a package is when a job after it did not manage; the draft gets whatever was packaged, even
when one of the jobs failed.

A build without a tag: start the workflow by hand ("Run workflow"); it uses the version from `qt/CMakeLists.txt`.

## Known flaky test

`LibraryTest.renamedAndMovedDocumentsKeepTheirIndex` fails about once in 13 runs (3 of 40 measured), here and in a
container. It guards that renaming or moving a document in the library does not read its PDF text again: the library
model moves the files on a worker, and its file system watcher can ask the index to look at the folder again before
the move has been told to it (`LibraryIndex::moved`), so the document is sometimes indexed anew. The results stay
right; it only costs time. The workflows therefore give every test one more try (`--repeat until-pass:2`).

A fix for the cause: when the index finds no entry for a document, look for a known entry whose files have the same
size and time and take that one over under the new path, instead of reading the document again. Then the order of the
two messages does not matter any more and the retry can go.

## Which package for which system

The fork needs Qt 6.5 or newer, cairo, pango, poppler-glib, libzip, qpdf and gdk-pixbuf, and optionally
KSyntaxHighlighting (for highlighted code blocks in Markdown boxes).

| System | What to use |
| --- | --- |
| KDE neon (Ubuntu 22.04 base), Kubuntu with Qt 6.5+ | the `neon-jammy` package |
| Debian 13, Ubuntu 25.04 and newer | the `debian13` package |
| Ubuntu 24.04 (Qt 6.4 only) | the AppImage, or build with Qt from KDE neon |
| anything else | the AppImage, or build from source (`qt/scripts/linux-deps.sh`, then the two cmake lines in `FORK.md`) |

## Windows and macOS

They do not need another machine: GitHub runs `windows-latest` and `macos-14` runners, and both jobs would live in
the same release workflow. What is missing is the environment and the packaging, not the hardware:

- **macOS**: Homebrew has everything the core needs (`glib`, `cairo`, `pango`, `poppler`, `libzip`, `qpdf`,
  `gdk-pixbuf`, `qt@6`, and `kf6-syntax-highlighting` for code blocks). The app bundle and the `.dmg` come from
  `macdeployqt` (it takes the QML modules with `-qmldir=qt/src/app/qml`). Upstream's `mac-setup/` does the same for
  the GTK build and shows the shape of the job.
- **Windows**: MSYS2 (UCRT64) has all of them as `mingw-w64-ucrt-x86_64-…` packages, including Qt 6 and
  KSyntaxHighlighting. `windeployqt --qmldir qt/src/app/qml` collects the Qt parts; upstream's `windows-setup/`
  builds an NSIS installer that can be reused.
- Open questions on both: the pen and touch input (Qt's tablet events on Windows Ink and on macOS), the file
  associations, and the places where the fork writes its settings and cache (`Util::getCacheSubfolder`).

Until then the fork is Linux only, and the CI says so.
