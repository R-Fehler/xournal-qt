# Android

The desktop app, built for Android (arm64-v8a) as a debug-signed APK that installs with `adb install` or by opening
the file on the phone. No mobile UI yet: the first target is the author's Galaxy Fold 7 (Android 16, API 36) with a
mouse and a keyboard. What is left for later is in [android-roadmap.md](android-roadmap.md).

## Build

```sh
qt/scripts/android-build.sh          # C dependencies (vcpkg), configure, APK
qt/scripts/android-build.sh deps     # only the dependencies (vcpkg, then KSyntaxHighlighting)
qt/scripts/android-build.sh ksyntax  # only KSyntaxHighlighting
qt/scripts/android-build.sh apk      # only configure + build (after deps)
```

The APK lands in `build-android/android-build/build/outputs/apk/debug/android-build-debug.apk` (about 94 MB: the
native libraries are stored uncompressed, as Android wants for loading them in place).

The script wraps the preset `android-arm64-debug` in [qt/CMakePresets.json](../CMakePresets.json)
(`cmake --preset android-arm64-debug && cmake --build build-android --target apk` does the same with the default
paths). Every heavy step runs with at most 4 jobs (`XQT_JOBS`), at `nice 15`, and in a systemd user scope with
`MemoryMax=6G` (`XQT_MEM`) and a CPU quota of 4 cores, so that the machine stays usable. Gradle gets at most 4
workers and no daemon.

Measured on the 8-thread 2-in-1 (2026-09-24), with 4 jobs:

| Step | Time |
|---|---|
| Dependencies, first build (55 vcpkg packages, host tools included) | about 17 min |
| Dependencies again from the binary cache (`~/.cache/vcpkg/archives`), e.g. in a new worktree | 7 s |
| KSyntaxHighlighting (download, ECM, the host indexer, the Android library; 2026-09-24) | about 1 min |
| App, clean native build + APK, without ccache | 8 min 14 s (Gradle: 22 s) |
| After changing one `.cpp` file (compile, link, Gradle) | 15 s |
| After changing only the manifest (Gradle) | 80 s |

### Tools

| Tool | Where (defaults of the script and the preset) | Why this version |
|---|---|---|
| Qt 6.11.2 for Android (`android_arm64_v8a`) and desktop (`gcc_64`, the host tools) | `~/Qt/6.11.2` (installed with `aqt`) | |
| Android NDK r27c (27.2.12479018) | `~/Android/Sdk/ndk` | Qt 6.11's binaries are built with it (Clang 18) |
| Android SDK: platform 36, build-tools 36.0.0, platform-tools | `~/Android/Sdk` | AGP 9.0 (Qt 6.11's Gradle template) wants build-tools 36 |
| JDK 17 | `~/.local/jdk-17` (`XQT_JAVA_HOME`) | Gradle 9.3 / AGP 9.0 run on 17; Qt documents 21. A newer desktop JDK in `JAVA_HOME` (GraalVM 25 here) breaks `jlink` of the Android platform, so the script does not use `JAVA_HOME` |
| vcpkg | `<workspace>/vcpkg` (`VCPKG_ROOT`), cloned by the script if missing | pinned by `builtin-baseline` in `qt/vcpkg.json` |
| bison, autoconf-archive | `<workspace>/vcpkg/host-tools` | built by the script when missing (no root needed): gettext's and gperf's host builds need them |

Other paths: set `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT`, `QT_ANDROID`, `QT_HOST`, `VCPKG_ROOT`, `XQT_JAVA_HOME`,
`XQT_ANDROID_BUILD` for the script, or write a `CMakeUserPresets.json` that inherits `android-arm64-debug`.

## Install and run

```sh
adb install -r build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
adb shell am start -n org.xournalqt.app/.XournalActivity
adb logcat --pid=$(adb shell pidof org.xournalqt.app)       # the app's log (Qt warnings have the tag "default")
```

Or copy the APK to the phone and open it (allow installing from the file manager). The app is "Xournal Qt",
package `org.xournalqt.app`. A document can be opened at start from adb (debug builds only):
`adb shell am start -S -n org.xournalqt.app/.XournalActivity -e applicationArguments <path>`.

**Where the documents are.** The default library is the app's own folder on the shared storage:
`/storage/emulated/0/Android/data/org.xournalqt.app/files/Documents/Xournal_Libraries/Default`. Files can be put
there with `adb push <file> <that folder>/` or over USB. New documents are saved there. Uninstalling the app deletes
this folder.

**Files from other apps.** "Open with" (a PDF, a `.xopp` or `.xoj`, a `.md` or `.txt`, an image in a file
manager, a mail or a browser's downloads) and the share sheet (one file or several) hand the app `content://` URIs.
A copy of each goes into the library, in the folder **"Opened"** (created when needed), and opens as a tab; a small
note at the bottom says where the copy is. A file with the same name and size already there is that copy: it opens
again instead of being copied twice (another file of the same name is copied as "name (2)"). Text shared without a
file (a link from a browser) is refused with a message. Files handed over while the app runs open in the running
window (the activity is `singleTask`). The activity is Qt's with this added: `org.xournalqt.app.XournalActivity`
([qt/packaging/android/src](../packaging/android/src/org/xournalqt/app/XournalActivity.java)), the native side is
[AndroidActivity.cpp](../src/app/AndroidActivity.cpp) and `AppController::receiveFiles`; the copy is made by
[ContentFiles](../src/shell/ContentFiles.h), so nothing after it sees a `content://` URI. Testing from adb:

```sh
adb shell am start -a android.intent.action.VIEW -d content://media/external/file/<id> -t application/pdf \
    --grant-read-uri-permission -n org.xournalqt.app/.XournalActivity    # the id: adb shell content query ...
adb shell am start -a android.intent.action.SEND -t application/octet-stream \
    --eu android.intent.extra.STREAM file:///storage/emulated/0/Android/data/org.xournalqt.app/files/<file> \
    -n org.xournalqt.app/.XournalActivity
```

(`am` grants no read access to a `content://` URI in `EXTRA_STREAM`, only to the intent's data; real share sheets
put the URI into the clip data, which carries the grant. So test SEND with a file of the app's own folder.) While
the phone is locked the app's event loop is paused: a file handed over then opens when it is unlocked.

**Opening and importing through Android's pickers.** "Open…" (tool bar, Recent), "Import files…" and "Import a
folder…" (library) show Android's system pickers (the Storage Access Framework: `ACTION_OPEN_DOCUMENT` and, for
folders, `ACTION_OPEN_DOCUMENT_TREE`, which Qt's `FileDialog` and `FolderDialog` use on Android). They return
`content://` URIs: "Open…" copies the file into "Opened" as above and opens it; the imports copy into the library's
current folder, a folder with all its subfolders (hidden ones stay behind) and every file the library shows. The
copies go through a staging folder in the app's cache first, in the background ([LibraryModel](../src/shell/LibraryModel.cpp)
`importUrls`). An image picked for "Insert image" is read the same way. "Open a folder as library" cannot take a
picked folder yet (no path to scan): it says so and points to "Import a folder".

**Drawing with the finger.** The Fold 7 has no pen, so on Android the finger draws from the first start when the
device reports no stylus (Android's input devices: no `SOURCE_STYLUS`; checked once, at the first start, through
`XournalActivity.hasStylus()`; a tablet with an S Pen starts with it off, as the desktop does). The finger button
in the tool bar (next to the hand) and Settings → Touch → "Draw with the finger" turn it on and off on every
platform: one finger draws with the current tool, two fingers scroll and zoom (a stroke the first finger began is
taken back), the hand tool still scrolls, and while a pen is near the finger is ignored (palm rejection).

**Going to the background.** Android may end an app in the background without warning (a force stop, swiping it
away in Recents, low memory). When the app leaves the screen (Qt's `ApplicationInactive`, then `ApplicationSuspended`)
the autosave of every document changed since its last autosave is written at once, and the session journal too
(`AppController::applicationStateChanged`); the next start offers them in the recovery dialog. The documents' own
files are not saved then: that is still the user's Save. Autosaves of saved documents are kept in the app's cache on
Android (`cache/xournal-qt/autosaves/`), never next to the document (`.name.autosave.xopp`), where a sync app would
upload them. Autosaving off (Settings) writes nothing.

**What the app keeps privately** (`/data/user/0/org.xournalqt.app/`, `adb shell run-as org.xournalqt.app ls files`):
settings in `files/settings/xournal-qt/`, the resources in `files/share/xournal-qt/` (copied from the APK at start),
`files/fonts.conf` and `files/fonts/` (fonts of your own for text boxes), caches in `cache/`. The library cache (previews, search
index) is in `cache/xournal-qt/libraries/` too: on Android a library keeps its cache in the app cache by default, not
in hidden `.xournal_library` folders next to the documents that a sync app would upload ([library.md](library.md),
"Where the cache is kept"; Settings → Storage switches it per library). Android may clear the app cache when storage
runs low; the library then reads its documents once again.

## How it is built

- **Dependencies through vcpkg** ([qt/vcpkg.json](../vcpkg.json), manifest mode, pinned baseline). Everything is a
  static library (triplet [qt/vcpkg/triplets/arm64-android.cmake](../vcpkg/triplets/arm64-android.cmake): vcpkg's
  arm64-android, release only, API 28, `c++_shared` like Qt), linked into the one app library
  `libxournal-qt_arm64-v8a.so`. `androiddeployqt` then only has Qt's own libraries to bundle, and nothing can be
  missing at run time (checked: every undefined symbol of the app library resolves in the bundled Qt libraries or the
  system's; only lsan's weak hooks stay open).
- **Toolchains**: Qt's `qt.toolchain.cmake` chains vcpkg's `vcpkg.cmake` (`QT_CHAINLOAD_TOOLCHAIN_FILE`), which
  chains the NDK's `android.toolchain.cmake` (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`), as QField does.
- **Dependency lookup** ([qt/cmake/XojDeps.cmake](../cmake/XojDeps.cmake), one target `xoj::deps`): libxml2, libzip
  and qpdf as CMake packages first (vcpkg), pkg-config for the GNOME libraries, which only ship `.pc` files (vcpkg
  installs them). A cross build reads only the target's `.pc` files (`PKG_CONFIG_LIBDIR`). Static builds use
  `pkg-config --static`. The Linux desktop build stays on pkg-config alone, as before (Ubuntu 22.04's libzip CMake
  files are broken and fail even a `QUIET` lookup).
- **Packaging** ([qt/cmake/XqtAndroid.cmake](../cmake/XqtAndroid.cmake), [qt/packaging/android/](../packaging/android)):
  Qt's manifest template with the app's id, name and icon (the desktop SVG as PNGs), min SDK 28 (Qt 6.11's minimum),
  target SDK 36, no permissions, resizable activity, intent filters for "Open with" and the share sheet. The APK is debug-signed
  (`QT_ANDROID_DEPLOYMENT_TYPE=Debug`, the SDK's debug keystore) while the native code is `RelWithDebInfo`, so that
  pages draw at full speed.
- **KSyntaxHighlighting** (the colours of code blocks in Markdown): vcpkg's `syntax-highlighting` port builds
  against vcpkg's own Qt and does not support Android, so the script builds KDE's release 6.30.0 itself
  (`ksyntax` step, into `<build>/kf6`): ECM (CMake files), the syntax definition indexer for this machine against the
  desktop Qt (it runs during the build), and the library for Android against the official Qt, static, with the
  definitions inside. Two changes to its sources, made by the script: its resources are compiled without zstd
  (`--no-zstd`; the official Qt for Android cannot read zstd resources), and its command line tool is left out.
  The configure step passes `KF6SyntaxHighlighting_DIR`, so `XqtMarkdown.cmake` finds it as on the desktop.
- **Resources**: page templates, palettes and icons, which the core reads as plain files, are Qt resources in the APK
  and are copied to the app's data folder at start ([AndroidSetup.cpp](../src/app/AndroidSetup.cpp)).
- **Fonts**: vcpkg's fontconfig knows no configuration on the phone. The app writes its own `fonts.conf` at start
  (`FONTCONFIG_FILE`): `/system/fonts`, `/product/fonts`, the app's `files/fonts`, a cache in the app's cache
  folder, and the generic families mapped to Android's fonts ("Sans" → Roboto, "Serif" → Noto Serif, "Monospace" →
  Droid Sans Mono). The first start scans the system fonts (about 1 MB of cache). Poppler uses its Android font
  backend for PDFs with fonts that are not embedded.
- **Folders**: `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `HOME` and `TMPDIR` point to
  the app's own folders before GLib first reads them, so upstream's `Util::getConfigFolder()` and friends work.

## Left out or changed on Android

| What | Why |
|---|---|
| Tests, the CLI (`xournal-qt-cli`, `xoj-imgdiff`), the golden tests, the spikes | desktop tools (`XQT_BUILD_TESTS/CLI/SPIKES` default OFF on Android) |
| `.deb` packaging (`XqtPackage.cmake`) | replaced by `XqtAndroid.cmake` |
| D-Bus ("Show in file manager") | no Qt D-Bus on Android (already optional) |
| Single instance (local socket per library) | Android starts one activity (`singleTop`) |
| Crash handlers (`SessionRecovery::installCrashHandlers`) | they replace the system's handlers, and a crash would leave no backtrace in logcat; to be chained later |
| Audio, Lua plugins, X11, gtksourceview | already off in the Qt build |
| Floating point `std::from_chars` | missing in the NDK's libc++; upstream's `g_ascii_strtod` fallback is used (the same check as upstream's CMake) |

## Checked so far (without the phone)

- `aapt2 dump badging`: `org.xournalqt.app`, version 0.1.0 (100), min SDK 28, target SDK 36, arm64-v8a, debuggable,
  label and icon, no permissions.
- `llvm-readelf`: the app library needs only system libraries and bundled Qt libraries; LOAD segments are aligned to
  16 KB (Android 15+ devices with 16 KB pages).
- A headless x86_64 emulator (Android 15 image, which runs arm64 apps through ARM translation) in
  `build-android/avd` (the author's own AVD is untouched): the app starts in about 7 s (a minute the first time),
  shows the library, creates and saves a document in the default library, draws with the mouse, and renders text
  boxes with Roboto, Noto Serif and Droid Sans Mono (no boxes), umlauts included, and a PDF through poppler. Use
  `-gpu swangle_indirect`: with `swiftshader_indirect` every other triangle of the window is missing.

Not checked: the Fold 7 itself (the author's test), stylus input, the Fold's posture changes.
