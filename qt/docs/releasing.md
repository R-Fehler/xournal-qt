# Releasing xournal-qt

## What the CI does

| Workflow | When | What |
| --- | --- | --- |
| `.github/workflows/xqt-build.yml` | every push and pull request to `master-qt` | builds in a Debian 13 container (Qt 6.8) and runs all tests |
| `.github/workflows/xqt-release.yml` | a tag `v1.2.3`, or started by hand | builds, tests, packages, and opens a **draft** release with the packages |
| `.github/workflows/xqt-windows.yml` | started by hand, or a push to `qt/windows-build` | builds for Windows in MSYS2 UCRT64 and publishes a portable zip ([windows.md](windows.md)) |

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
   - `xournal-qt-x.y.z-x86_64.AppImage` — runs from Ubuntu 22.04 on, whatever Qt the system has; it carries Qt and
     the QML modules with it (about 80 MB), which linuxdeploy and its Qt plugin put there. Those two are tools, they
     are AppImages themselves, and they do not belong in the release.
6. Install a package and try it, then write the notes and publish the draft on GitHub.

Every job also keeps what it built as an artifact of the run (the "Artifacts" box at the bottom of the run's page,
a zip). That is where a package is when a job after it did not manage; the draft gets whatever was packaged, even
when one of the jobs failed.

A build without a tag: start the workflow by hand ("Run workflow"); it uses the version from `qt/CMakeLists.txt`.

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
- **Windows**: a first build exists (`xqt-windows.yml`, [windows.md](windows.md)): MSYS2 (UCRT64) packages,
  `windeployqt --qmldir qt/src/app/qml`, a portable zip, no installer yet. Upstream's `windows-setup/` builds an
  NSIS installer that can be reused ([windows-roadmap.md](windows-roadmap.md)).
- Open questions on both: the pen and touch input (Qt's tablet events on Windows Ink and on macOS), the file
  associations, and the places where the fork writes its settings and cache (`Util::getCacheSubfolder`).

Until then the releases are Linux only; the Windows zip is a test build.
