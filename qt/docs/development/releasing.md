# Releasing xournal-qt

## What the CI does

| Workflow | When | What |
| --- | --- | --- |
| `.github/workflows/xqt-build.yml` | every push and pull request to `master-qt` | builds in a Debian 13 container (Qt 6.8) and runs all tests |
| `.github/workflows/xqt-release.yml` | a tag `v1.2.3`, or started by hand | builds, tests, packages, and opens a **draft** release with the packages |
| `.github/workflows/xqt-windows.yml` | started by hand, or a push to `qt/windows-build` | builds for Windows in MSYS2 UCRT64 and publishes a portable zip ([windows.md](windows.md)) |
| `.github/workflows/xqt-macos.yml` | started by hand, or a push to `qt/macos-build` | builds for macOS (Apple Silicon) with Homebrew and publishes an unsigned `.dmg` ([macos.md](macos.md)) |

The upstream Xournal++ workflows in the same folder stay dormant here: they only run for pull requests to `master`
or carry `if: github.repository == 'xournalpp/xournalpp'`.

Qt 6.5 or newer is needed, which the GitHub runners' own Ubuntu 24.04 does not have (6.4). Every job therefore builds
in a container; `qt/scripts/linux-deps.sh` installs the packages (the same script works on a developer machine).

### A test that fails only on GitHub

Run it in the same container here: `qt/scripts/ci-container.sh` builds and tests like the CI does, in `debian`
(Debian 13, Qt 6.8) or `neon` (Ubuntu 22.04 with KDE neon, Qt 6.7): root, C locale, the fonts the packages bring,
the checkout mounted read-only, the build folder in `~/.cache/xqt-ci/<name>`. Docker if it runs, else podman; the
container gets 3 CPUs and 4 GB, and on a shared machine it goes through `qt/scripts/build-slot.sh` as well.

```sh
qt/scripts/build-slot.sh qt/scripts/ci-container.sh debian build xqt-ui-tests     # about 30 min the first time
qt/scripts/build-slot.sh qt/scripts/ci-container.sh debian test -R 'AdaptiveLayoutTest\.menus' --repeat until-fail:3
qt/scripts/ci-container.sh debian run build/xqt-ui-tests --gtest_filter='AdaptiveLayoutTest.*'
qt/scripts/ci-container.sh neon shell            # look around; `clean` deletes the build folder
```

What made tests fail there and not on a desktop (2026-09-27): other fonts (other text widths, so a menu a fraction
of a pixel narrower), and Qt 6.8, whose own file dialogs are windows of their own (off-screen the app's window does
not get the keys back when one closes) and whose menus take a click on their button only after they have faded out.

## Cutting a release

1. Everything green on `master-qt`, and the device checklist walked through (`qt/docs/testing/device-checklist.md`).
2. Set the version in `qt/CMakeLists.txt` (`project(xournal-qt VERSION x.y.z ...)`). The release job refuses a tag
   that says something else.
3. Write `qt/docs/release-notes/<x.y.z>.md` (the draft takes it as its text; without it GitHub writes a list of commits).
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

The fork needs Qt 6.5 or newer, cairo, pango, poppler-glib, libzip, qpdf 12 and gdk-pixbuf, and optionally
KSyntaxHighlighting (for highlighted code blocks in Markdown boxes).

**qpdf** is built with the app on Linux ([XqtQpdf.cmake](../../cmake/XqtQpdf.cmake)): a pinned release (12.4.1, checked
by SHA-256) is downloaded when the build is configured and linked statically, with its native crypto and the
system's zlib and libjpeg. The packages therefore do not depend on the distribution's `libqpdf` (Ubuntu 22.04 has
10.6, Debian 13 has 12.2), and the incremental save runs on the qpdf it is tested with. Distribution builds can use
their own qpdf 12 or newer with `-DXQT_SYSTEM_QPDF=ON` (`XQT_SYSTEM_QPDF=1 qt/scripts/linux-deps.sh` installs it);
offline builds point `-DXQT_QPDF_SOURCE_DIR=<unpacked qpdf-12.4.1>` at the source. Windows (MSYS2, qpdf 12.3) and
Android (vcpkg, qpdf 12.4) use their package manager's qpdf, which is new enough.

| System | What to use |
| --- | --- |
| KDE neon (Ubuntu 22.04 base), Kubuntu with Qt 6.5+ | the `neon-jammy` package |
| Debian 13, Ubuntu 25.04 and newer | the `debian13` package |
| Ubuntu 24.04 (Qt 6.4 only) | the AppImage, or build with Qt from KDE neon |
| anything else | the AppImage, or build from source (`qt/scripts/linux-deps.sh`, then the two cmake lines in `FORK.md`) |

## Windows and macOS

Since 0.4.0 every release also carries the **Windows** portable zip and the **Android** APK: the release workflow
calls `xqt-windows.yml` and `xqt-android.yml` (both also run by hand) and puts `xournal-qt-<version>-windows-x64.zip`
and `xournal-qt-<version>-android-arm64.apk` (signed with the release key from the repository secrets; Android 9 or
newer, arm64) into the draft, and (also since 0.4.0) the unsigned **macOS** `.dmg` from `xqt-macos.yml`:

- **macOS**: a first build exists (`xqt-macos.yml`, [macos.md](macos.md)): Homebrew's libraries and Qt on GitHub's
  `macos-15` runner (Apple Silicon), `macdeployqt -qmldir=qt/src/app/qml`, `xournal-qt.app` in a `.dmg`
  (`xournal-qt-<version>-macos-arm64.dmg`, artifact `xournal-qt-macos-arm64`). It has an ad-hoc signature only, so
  macOS asks before the first start ("Open Anyway"), and it needs macOS 15 or newer. The release workflow calls it.
  Missing: Apple's signing and notarization (a
  Developer ID certificate and an App Store Connect API key as secrets, see macos.md), an Intel build (Homebrew has
  no Intel bottles any more), and a try on a real Mac.
- **Windows**: a first build exists (`xqt-windows.yml`, [windows.md](windows.md)): MSYS2 (UCRT64) packages,
  `windeployqt --qmldir qt/src/app/qml`, a portable zip, no installer yet. Upstream's `windows-setup/` builds an
  NSIS installer that can be reused ([windows-roadmap.md](windows-roadmap.md)).
- **Recording** ([audio.md](../features/audio.md), "Platforms"; since `qt/audio-platforms`): every package offers it. Windows,
  macOS and Android are built with Qt Multimedia (`mingw-w64-ucrt-x86_64-qt6-multimedia`, Homebrew `qtmultimedia`,
  aqt `-m qtmultimedia`) and configured with `-DXQT_REQUIRE_AUDIO=ON`, so a Qt without it fails the job. What each
  package carries for it:

  | Package | For recording |
  | --- | --- |
  | Windows zip | `bin\Qt6Multimedia.dll` (WASAPI), no media plugins, no FFmpeg DLLs |
  | macOS `.dmg` | `QtMultimedia.framework` (Core Audio), no media plugins, no FFmpeg; `NSMicrophoneUsageDescription` in `Info.plist` |
  | Android APK | Qt Multimedia's library and Java part (AAudio/OpenSL ES), no media plugins; `RECORD_AUDIO`, the microphone foreground service and its notification |
  | Linux `.deb` | depends on the distribution's Qt Multimedia (`qt6-multimedia-dev` at build time) |

  The Windows and macOS smoke tests check the library is there, that no media plugin or FFmpeg library is, and
  that `xournal-qt --audio-info` says "recording: available". A release whose notes mention recording on these
  systems wants the device checklist's recording items walked through first.
- Open questions on both: the pen and touch input (Qt's tablet events on Windows Ink and on macOS), the file
  associations (declared in the macOS bundle, untested; none on Windows yet), and the places where the fork writes
  its settings and cache (`Util::getCacheSubfolder`: GLib's XDG folders, `~/.config` and `~/.cache` on macOS).
