# macOS

The desktop app for Macs with Apple Silicon, built on GitHub Actions and published as a disk image (`.dmg`) holding
`xournal-qt.app`. It is **not signed with an Apple Developer ID and not notarized**, so macOS asks before the first
start (below). Nobody has tried it on a real Mac yet: the CI builds it and starts it off-screen, nothing more.

First build: 2026-09-27, [run 36311706587](https://github.com/R-Fehler/xournal-qt/actions/runs/36311706587) (the
second CI run of the branch): Qt 6.11.2 from Homebrew, the app 161 MB, the `.dmg` 67 MB. The smoke test exported
text, strokes, images and a PDF background with the CLI, and the app opened a document off-screen, all from the
bundle with Homebrew moved away.

## Getting the .dmg

The workflow **xournal-qt macOS** ([.github/workflows/xqt-macos.yml](../../.github/workflows/xqt-macos.yml)) runs when
it is started by hand (Actions → "xournal-qt macOS" → "Run workflow", any branch), for every push to the branch
`qt/macos-build`, and when another workflow calls it (`workflow_call`, meant for the release workflow; not wired in
yet). Its run page has the artifacts:

| Artifact | What |
|---|---|
| `xournal-qt-macos-arm64` (a zip holding `xournal-qt-<version>-macos-arm64.dmg`, about 67 MB) | the disk image: `xournal-qt.app`, a link to Applications, `README.txt` |
| `smoke-test-macos-arm64` | what the smoke test wrote: `app.png` (the app's window, off-screen), the exports, a log per step |
| `build-logs-macos-arm64` (only when the run failed) | configure, build and deploy logs, `CMakeCache.txt`, CMake's configure log |

## Installing it

1. Open the `.dmg` and drag `xournal-qt.app` onto the Applications link.
2. Start it. The first time, macOS refuses: "xournal-qt.app" cannot be opened / "Apple could not verify
   'xournal-qt.app' is free of malware". The app only has an ad-hoc signature (Apple Silicon runs nothing without
   one), not a Developer ID, and Apple has not notarized it.
   - **macOS 15 (Sequoia) and newer**: click "Done", open System Settings → Privacy & Security, scroll down to
     "xournal-qt.app was blocked …" and click **Open Anyway**, confirm with the password. (The old way, right click →
     Open, no longer skips the check since macOS 15.)
   - **Or in a terminal**, which removes the quarantine flag that the browser put on the download:
     ```sh
     xattr -dr com.apple.quarantine /Applications/xournal-qt.app
     ```
3. It needs **macOS 15 or newer** (see "Why macOS 15" below); `Info.plist` says so (`LSMinimumSystemVersion`, the
   newest minimum of all the binaries in the bundle).

Where the program keeps things (GLib's XDG folders, as on Linux and as upstream Xournal++ does on macOS):

| What | Where |
|---|---|
| Settings, palettes, the session journal, autosaves | `~/.config/xournal-qt` |
| Caches (thumbnails, previews, library index) | `~/.cache/xournal-qt` |
| The default library | `~/Documents/Xournal_Libraries/Default` |

`xournal-qt.app/Contents/MacOS/xournal-qt-cli` is the command line tool (`xournal-qt-cli file.xopp
--create-pdf=out.pdf`, as upstream's `xournalpp --create-pdf`).

## How the build works

**Toolchain: Homebrew** on GitHub's `macos-15` runner (Apple Silicon, arm64) with Apple's clang. Everything the core
and the app need is a Homebrew bottle, so nothing is compiled but the app:

- `glib`, `cairo`, `pango`, `fontconfig`, `poppler`, `libzip`, `gdk-pixbuf`, `gettext` (libintl, which is not part
  of the C library on macOS; [XojDeps.cmake](../cmake/XojDeps.cmake) links it as on Windows);
- `qpdf` 12.4 (12 or newer is what [XqtQpdf.cmake](../cmake/XqtQpdf.cmake) asks for). The Linux packages compile a
  pinned qpdf into the program because the distributions have old ones; Homebrew's is current, so on macOS, as on
  Windows and Android, the package manager's qpdf is used (`XQT_SYSTEM_QPDF` is on by default for Apple);
- Qt 6.11 as Homebrew's split formulas `qtbase` (it has `macdeployqt`), `qtdeclarative` (the QML modules),
  `qtsvg` (the SVG icons) and `qtmultimedia` (the microphone and the speaker of the audio recordings,
  [audio.md](audio.md)), not the `qt` formula that pulls in every Qt module;
- `librsvg` only to draw the program icon ([qt/packaging/xournal-qt.svg](../packaging/xournal-qt.svg)) into the
  `.icns`;
- libxml2 and zlib come with macOS.

**No KSyntaxHighlighting**: Homebrew has no KDE Frameworks, so code blocks in Markdown boxes are not highlighted (it
is optional, as on Windows).

The workflow's steps:

1. **Homebrew** installs the packages above; the step after it prints the versions, where Qt's tools are and whether
   gdk-pixbuf has loader modules.
2. **Configure**: `cmake -S qt -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix)
   -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DXQT_BUILD_TESTS=OFF -DXQT_BUILD_SPIKES=OFF -DXQT_BUILD_CLI=ON
   -DXQT_REQUIRE_AUDIO=ON` (the deployment target is the runner's macOS, the one Homebrew's bottles are built for;
   without Qt Multimedia the configure step fails rather than build a `.dmg` without recording).
3. **Build** with ccache (its folder is cached between runs), `ninja -k 0`: one run lists every file that does not
   compile, repeated in the run's summary.
4. **Bundle and disk image** ([qt/scripts/macos-deploy.sh](../scripts/macos-deploy.sh)):
   - `cmake --install` into a staging folder; the program and the CLI go to `Contents/MacOS`, the resources (page
     templates, palettes, icons, fonts) to `Contents/Resources/share/xournal-qt`, where
     `AppContext::defaultResourceDir` looks in a bundle;
   - the `.icns` from the fork's SVG icon (`rsvg-convert` at 16 to 1024 pixels, `iconutil`);
   - `macdeployqt xournal-qt.app -qmldir=qt/src/app/qml`: Qt's frameworks and plugins, the QML modules the QML files
     import, and every Homebrew library the program and the CLI use (glib, pango, poppler, …) into
     `Contents/Frameworks`, with their references rewritten. Three plugins are added by hand (and handed to
     macdeployqt with `-executable`): the off-screen platform for scripted runs, and the SVG image and icon plugins
     (the program does not link Qt Svg itself);
   - Qt Multimedia's media plugins (`PlugIns/multimedia`: FFmpeg's and AVFoundation's players, which macdeployqt
     adds because the program links Qt Multimedia) are removed again, with every library only they used (FFmpeg and
     its codecs): the recordings need only `QtMultimedia.framework`, whose audio devices use Core Audio;
   - what macdeployqt leaves out: Homebrew's libraries name some of their own through `@rpath` with an rpath like
     `@loader_path/../lib` (`libpoppler-glib` → `libpoppler`, `libbrotlidec` → `libbrotlicommon`, `libwebp` →
     `libsharpyuv`), which macdeployqt neither copies nor rewrites (CI run 1: the app did not start). The script points
     every such reference at `Contents/Frameworks`, copying the library from Homebrew, until nothing is left, and
     removes the rpaths into Homebrew;
   - a check that every reference of every binary resolves inside the bundle (a failure otherwise: it would not
     start on a Mac without Homebrew);
   - `Info.plist` from [qt/packaging/macos/Info.plist.in](../packaging/macos/Info.plist.in): bundle id
     `org.xournalqt.app` (the Android app's id), the version from `qt/CMakeLists.txt`, the minimum macOS, the document
     types;
   - an ad-hoc signature (`codesign --sign -`: `install_name_tool` broke the linker's signatures, and Apple Silicon
     runs no unsigned code);
   - the `.dmg` (`hdiutil create`, compressed) with the app, a link to Applications and a README.
5. **Publish** the `.dmg` as the artifact `xournal-qt-macos-arm64`.
6. **Smoke test** ([qt/scripts/macos-smoke.sh](../scripts/macos-smoke.sh)), everything from the bundle and with
   **Homebrew moved away** (`/opt/homebrew` renamed for the step, and put back after it), so that a library missing
   from the bundle fails here and not on a Mac without Homebrew: the CLI's `--version`; exports that tell apart what
   fails (strokes to PNG, text to PDF, text to PNG, images to PDF, a PDF background to PDF); then the app, off-screen
   with Qt Quick's software renderer, opening a library and a document and saving a screenshot of its window after
   5 s (`app.png` in the artifact `smoke-test-macos-arm64`); last, recording: `QtMultimedia.framework` is in the
   bundle, no media plugin and no FFmpeg library is, and `xournal-qt --audio-info` says "recording: available (Qt
   Multimedia …)" (it lists the devices without opening the microphone, so macOS asks nothing). A step that fails runs again under **lldb**
   (`<step>.lldb.log`: the backtraces of every thread; the build has `-g1`).

### Why macOS 15, and not older Macs

Homebrew builds its bottles for the three newest macOS versions only, and each for its own version: on the
`macos-14` runner glib, cairo, pango and qpdf would have been compiled from source, and on `macos-15` the bottles
(and so the app) need macOS 15. Supporting older macOS would mean building every library ourselves with an older
deployment target, as upstream Xournal++ does with jhbuild ([mac-setup/](../../mac-setup)) or as the Android build
does with vcpkg.

### Intel Macs

The workflow is a matrix over the architecture (`arm64` on `macos-15`), so an Intel build is one more entry
(`x86_64` on `macos-15-intel`, GitHub's Intel runner, available until 2027). The entry is in the file, commented
out, and no Intel run was made: Homebrew no longer publishes bottles for Intel Macs (the current versions of
`qtbase`, `glib`, `pango`, `cairo`, `poppler` and `qpdf` list bottles for Apple Silicon and Linux only, checked
2026-09-27 at formulae.brew.sh), so the job would compile Qt and every library from source, for hours, past the
job's time limit.
An Intel build would need the libraries from elsewhere, as for older macOS versions above: vcpkg (the manifest
[qt/vcpkg.json](../vcpkg.json) of the Android build, with the `x64-osx` triplet) and Qt's official binaries
(aqtinstall). No universal binary.

## What the code does differently on macOS

- **Resources** in the bundle: `Contents/Resources/share/xournal-qt` (AppContext.cpp).
- **Documents from Finder**: a double click on a `.xopp`, "Open With" or a drop on the Dock icon arrive as
  `QFileOpenEvent`, not as arguments; main.cpp hands them to `AppController::openPaths`. `Info.plist` declares the
  types: `.xopp` and `.xoj` as the default app (the types Xournal++ declares, imported under its identifiers
  `com.github.xournalpp.xopp` and `net.sourceforge.xournal.xoj`), `.pdf` and `.md` as an alternative ("Open With").
- **Fonts**: Pango draws with Core Text on macOS, not fontconfig (CI run 1, `text-probe`: the font map is
  `PangoCairoCoreTextFontMap`, "Sans 20" is Helvetica), so the app's emoji font is not registered; macOS's own colour
  emoji are used by Pango and Qt alike. Font names in documents map as Core Text maps them, as in upstream Xournal++
  on macOS. No fontconfig configuration is bundled; poppler may want one for PDFs with fonts that are not embedded
  (untested).
- **Session recovery** asks the kernel for the name of a process (`proc_name`) instead of `/proc`.
- **Show in Finder**: `open -R` (SystemApps.cpp). No D-Bus.
- **The microphone** ([audio.md](audio.md), "macOS"): macOS asks the first time a recording starts, with the text of
  `NSMicrophoneUsageDescription` in `Info.plist`; refused, a dialog says where to allow it and opens System Settings →
  Privacy & Security → Microphone. With the hardened runtime (signing, below) the entitlement
  `com.apple.security.device.audio-input` is needed as well.

## What is missing

### Signing and notarization

Needed for a first start without "Open Anyway", and for Gatekeeper to trust updates:

1. An **Apple Developer Program** membership (99 USD a year).
2. A **Developer ID Application** certificate (created in the developer account), exported from the Keychain as a
   `.p12` with its private key and a password. As repository secrets: the `.p12` in base64
   (`XQT_MACOS_CERTIFICATE`) and its password (`XQT_MACOS_CERTIFICATE_PASSWORD`). The job imports it into a temporary
   keychain (`security create-keychain`, `security import`, `security set-key-partition-list`).
3. An **App Store Connect API key** for notarization (Users and Access → Integrations → Keys, role "Developer"): the
   `.p8` key file, its key id and the issuer id, as secrets (`XQT_MACOS_NOTARY_KEY` in base64,
   `XQT_MACOS_NOTARY_KEY_ID`, `XQT_MACOS_NOTARY_ISSUER`).
4. **codesign** with the hardened runtime, inside out (every dylib, framework and plugin, then the program, then the
   bundle), instead of the ad-hoc signature:
   `codesign --force --options runtime --timestamp --entitlements xournal-qt.entitlements --sign "Developer ID
   Application: <name> (<team id>)" …`. The entitlements must include `com.apple.security.cs.allow-jit` (the QML
   engine's JIT), `com.apple.security.device.audio-input` (the recordings' microphone) and probably `com.apple.security.cs.disable-library-validation` only if a plugin from outside the
   bundle is ever loaded (not now). `macdeployqt -codesign=<identity> -hardened-runtime -timestamp` can do the signing
   part.
5. **Notarize** the signed `.dmg`: `xcrun notarytool submit xournal-qt.dmg --key <p8> --key-id <id> --issuer <issuer>
   --wait`, then **staple** the ticket: `xcrun stapler staple xournal-qt.dmg` (and the app inside it before the dmg
   is made, so it works offline once copied).

### Other open points

- **Try it on a real Mac**: start, open and save documents, PDFs with text, Markdown boxes with emoji and formulas,
  the file dialogs, printing, the second window of another library, quitting with ⌘Q.
- **Pen and tablet input**: Qt reads Wacom tablets and Apple Pencil through Sidecar as tablet events (pressure,
  tilt); nothing is tested. Trackpad pinch and scroll gestures arrive as native gestures and pixel scroll deltas,
  which the canvas handles as on Linux, untested as well.
- **Keyboard shortcuts**: Qt maps Ctrl to ⌘ in `QKeySequence` on macOS; shortcuts written as "Ctrl+…" in QML
  become ⌘, which is what Mac users expect, but none has been checked.
- **Menus**: the app has its own menus in the window; there is no native menu bar (only the application menu macOS
  makes).
- **File associations**: declared in `Info.plist`; that Finder offers the app and that `QFileOpenEvent` opens the
  document is untested. Xournal++'s own `.xopp` icon is not in the bundle (Finder shows the app's icon).
- **poppler's encoding data** (`poppler-data`, for some CJK PDFs) is not in the bundle; poppler looks for it in
  Homebrew's folder.
- **gdk-pixbuf's loader modules** are not in the bundle; PNG and JPEG are built into Homebrew's gdk-pixbuf (the
  images export of the smoke test works without Homebrew), other formats in `.xopp` image elements (GIF, TIFF, …)
  would not load.
- **The release workflow** does not call `xqt-macos.yml` yet: a job `macos: uses: ./.github/workflows/xqt-macos.yml`
  and the `.dmg` renamed into the draft, as for Windows and Android.
