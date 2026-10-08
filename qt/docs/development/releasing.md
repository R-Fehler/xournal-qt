# Releasing xournal-qt

What the CI does on every push, the block tests and how to reproduce a CI failure: [ci.md](ci.md).

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

## Handwriting: ONNX Runtime and the model

Every package carries the handwriting search whole: **ONNX Runtime** (Microsoft's release build, which the app loads
at run time, [OrtRuntime.cpp](../../src/hwr/OrtRuntime.cpp)) and **the models**, one folder
`share/xournal-qt/hwr-models/<name>/` for each folder under `qt/resources/hwr/` (installed by `cmake --install`; the
packaging names no model). Each model folder keeps its `LICENCE.md`: the model shipped now is for **non-commercial use
only** (trained on IAM and CVL), which the `.deb`'s `copyright`, the README of the zip and of the `.dmg` say too.

| Package | ONNX Runtime | Its licence files | The models | Checked in the job |
| --- | --- | --- | --- | --- |
| `.deb` (both) | `/usr/lib/xournal-qt/libonnxruntime.so.1` → `libonnxruntime.so.<ver>` (CPack adds a staged folder: `cpack -D CPACK_INSTALLED_DIRECTORIES=…`) | `/usr/share/doc/xournal-qt/onnxruntime/`, and paragraphs in `copyright` | `/usr/share/xournal-qt/hwr-models/` | the package installed in the container, then `xournal-qt --hwr-info` |
| AppImage | `usr/lib/xournal-qt/` in the AppDir, before linuxdeploy | `usr/share/doc/xournal-qt/onnxruntime/` | `usr/share/xournal-qt/hwr-models/` | the AppImage itself: `--hwr-info` (it now carries the off-screen platform plugin for that) |
| Windows zip | `bin\onnxruntime.dll`, with the Visual C++ runtime DLLs it imports (`MSVCP140*.dll`, `VCRUNTIME140*.dll`) | `share\doc\xournal-qt\onnxruntime\` | `share\xournal-qt\hwr-models\` | `windows-smoke.sh`: the DLLs by name, `--hwr-info` |
| macOS `.dmg` | `Contents/Frameworks/libonnxruntime.1.dylib` | `Contents/Resources/share/doc/xournal-qt/onnxruntime/` | `Contents/Resources/share/xournal-qt/hwr-models/` | `macos-smoke.sh` (Homebrew hidden): `--hwr-info` |
| Android APK | `lib/arm64-v8a/libonnxruntime.so` (from the AAR on Maven Central) | `assets/share/doc/xournal-qt/onnxruntime/` | with the app's resources (copied to the data folder at start) | no device: `hwr-package-check.sh apk` looks into the APK |

`xournal-qt --hwr-info` prints whether ONNX Runtime was found (path, version) and the models found, reads a built-in
sample, and exits with 1 when the runtime or a model is missing or the sample is not read; a non-zero exit fails the
job. Next to it, `qt/scripts/hwr-package-check.sh models <share/xournal-qt>` checks that every model of
`qt/resources/hwr/` is in the package file for file (sha256). Only the runtime library is taken from Microsoft's
archives: not the headers, debug symbols or `onnxruntime_providers_shared` (only GPU and other execution providers
load it).

What it adds to each package (ONNX Runtime 1.30.0, the model of 2026-10 at 9.2 MB, which hardly compresses):

| Package | ONNX Runtime (as packed) | The model (as packed) | Growth, about |
| --- | --- | --- | --- |
| `.deb` (gzip) | 29.0 MB → 11.1 MB | 7.7 MB | +19 MB |
| AppImage | 29.0 MB → about 11 MB | about 7.7 MB | +19 MB |
| Windows zip | 16.5 MB → 6.1 MB, the Visual C++ runtime about 0.5 MB | 7.7 MB | +14 MB |
| macOS `.dmg` | 43.9 MB → 12.5 MB | 7.7 MB | +20 MB |
| Android APK | 33.0 MB (native libraries are stored uncompressed) | about 7.7-9 MB | +41 MB (installed: the same) |

These are estimates from compressing the files alone (`gzip -6`); the packages were not built for them.

**Bumping ONNX Runtime** is one file, [qt/packaging/onnxruntime.env](../../packaging/onnxruntime.env): the version,
then the sha256 of the three GitHub archives, of the AAR and of `LICENSE` and `ThirdPartyNotices.txt` at the release's
tag (download them and run `sha256sum`; GitHub's release page shows the digests too, Maven Central a `.sha1`). Check
before: the C API version stays 16 or newer (`OrtRuntime.cpp` asks for 16); the Linux library needs no glibc newer
than Ubuntu 22.04's 2.35 (`objdump -T libonnxruntime.so.1.* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`; 1.30.0
needs 2.28); the macOS library's minimum macOS is not above the bundle's. Then run the release workflow by hand (or
`xqt-windows.yml`, `xqt-macos.yml`, `xqt-android.yml`): every smoke test reads the sample with the new runtime.
`qt/scripts/onnxruntime-fetch.sh <platform> <folder>` downloads and checks one platform's files locally
(`XQT_DOWNLOAD_CACHE=<folder>` keeps the downloads, or serves them offline).

**Swapping the model**: replace or add the folder under `qt/resources/hwr/` (its `model.json`, the files it names, and
its `LICENCE.md`). Nothing in the packaging names a model, so the packages and their checks follow. If the new model's
licence differs, change the `Files: usr/share/xournal-qt/hwr-models/*` paragraph of
[qt/packaging/copyright](../../packaging/copyright) and the README lines in `windows-deploy.sh` and `macos-deploy.sh`.
