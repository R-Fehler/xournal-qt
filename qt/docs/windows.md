# Windows

The desktop app, built for Windows x64 on GitHub Actions and published as a portable folder in a zip: unpack it and
start `bin\xournal-qt.exe`. No installer yet. What is left for later is in [windows-roadmap.md](windows-roadmap.md).

## Getting the zip

The workflow **xournal-qt Windows** ([.github/workflows/xqt-windows.yml](../../.github/workflows/xqt-windows.yml))
runs when it is started by hand (Actions → "xournal-qt Windows" → "Run workflow", any branch) and for every push
to the branch `qt/windows-build`, and for nothing else. Its run page has the artifacts:

| Artifact | What |
|---|---|
| `xournal-qt-windows-x64` (downloads as `xournal-qt-windows-x64.zip`) | the program folder `xournal-qt/` |
| `smoke-test` | what the smoke test wrote: `app.png` (the app's window, off-screen), the exports, a log per step |
| `build-logs` (only when the run failed) | configure, build and deploy logs, `CMakeCache.txt`, CMake's configure log |

Artifacts are kept for 90 days (GitHub's default).

## Running it

1. Unpack the zip anywhere (a folder of your own, not `C:\Program Files`, which needs admin rights to write).
2. Start `xournal-qt\bin\xournal-qt.exe`. The program is not signed: SmartScreen may ask first ("More info" → "Run
   anyway").

Keep the folder together; the program finds everything relative to `bin\`:

```
xournal-qt/
  bin/                xournal-qt.exe, xournal-qt-cli.exe, Qt and MinGW DLLs, Qt plugins (platforms/, styles/,
                      imageformats/, iconengines/, tls/), qml/ (QML modules), qt.conf
  share/xournal-qt/   page templates, palettes, icons
  share/poppler/      poppler's encoding data
  lib/gdk-pixbuf-2.0/ gdk-pixbuf's image loaders
  etc/fonts/          fontconfig's configuration
  README.txt
```

Where the program keeps things:

| What | Where |
|---|---|
| Settings, palettes, the session journal, autosaves | `%LOCALAPPDATA%\xournal-qt` |
| Caches (thumbnails, previews, library index) | `%LOCALAPPDATA%\cache\xournal-qt` |
| Fontconfig's configuration (written at every start) and font cache | `%LOCALAPPDATA%\cache\xournal-qt\fontconfig` |
| The default library | `Documents\Xournal_Libraries\Default` |

`bin\xournal-qt-cli.exe` is the command line tool (`xournal-qt-cli file.xopp --create-pdf=out.pdf`, as upstream's
`xournalpp --create-pdf`); it runs in a terminal.

## Text and fonts

**Pango draws with its fontconfig backend on Windows, not its Windows one.** The CI smoke test found (2026-09-24,
runs 2 and 3) that Pango's default backend on Windows, `win32` (DirectWrite), kills the process as soon as text is
drawn into an image surface: a PNG export of a document with text, the page rasters and thumbnails of the app. The
process ends with a fatal NTSTATUS and prints nothing (MSYS2 shows exit code 127). Text into a PDF worked, and so
did everything with `PANGOCAIRO_BACKEND=fc`, which is what Linux uses as well.

So the app and the CLI start with ([WindowsFonts.cpp](../src/app/WindowsFonts.cpp)):

- `PANGOCAIRO_BACKEND=fc`, and `FONTCONFIG_FILE` pointing to a `fonts.conf` of their own, written at every start to
  `%LOCALAPPDATA%\cache\xournal-qt\fontconfig\` (the cache folder, `XDG_CACHE_HOME` when set). It lists
  `C:\Windows\Fonts` and the user's own fonts (`%LOCALAPPDATA%\Microsoft\Windows\Fonts`), puts fontconfig's cache
  in the same folder, and includes the rules of the program folder's `etc\fonts\conf.d`.
- The generic names that Xournal++ files carry map to the fonts Pango's Windows backend and upstream Xournal++ on
  Windows use, so that text boxes keep their size and line breaks: Sans → Arial, Serif → Times New Roman,
  Monospace → Courier New.
- Paths in that file and in `FONTCONFIG_FILE` are 8.3 short names where the drive has them: fontconfig opens files
  with the ANSI functions, and a user name with an umlaut would otherwise break them.
- The app loads fontconfig's configuration and fonts on a background thread right at start. The first start builds
  the font cache (a scan of `C:\Windows\Fonts`, a few seconds), in parallel with the window coming up rather than
  in front of the first page with text; later starts read the cache.
- Nothing of this happens when `PANGOCAIRO_BACKEND` or `FONTCONFIG_FILE` is set already. `XQT_WIN_PANGO_WIN32=1`
  leaves Pango's default (the win32 backend) to try it again; the smoke test does so in every run, for information,
  and `text-probe` runs Pango's default without the app around it (under gdb when it dies, for a report upstream).

## How the build works

**Toolchain: MSYS2 UCRT64**, the one upstream Xournal++ builds its Windows installer with. GLib, Cairo, Pango,
poppler, qpdf, libxml2, libzip, gdk-pixbuf and Qt 6 are prebuilt `mingw-w64-ucrt-x86_64-*` packages, so nothing is
compiled but the app. The fallback, if MSYS2 ever gets in the way, is QField's route: MSVC with vcpkg, reusing the
manifest [qt/vcpkg.json](../vcpkg.json) of the Android build.

The workflow's steps:

1. **MSYS2** (`msys2/setup-msys2`, `UCRT64`, updated): `toolchain`, `cmake`, `ninja`, `ccache`, `glib2`, `cairo`,
   `pango`, `gdk-pixbuf2`, `poppler`, `qpdf`, `libxml2`, `libzip`, `zlib`, `qt6-base`, `qt6-declarative`,
   `qt6-svg`. The step after it prints the versions and where Qt's tools are.
2. **Configure**: `cmake -S qt -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXQT_BUILD_TESTS=OFF
   -DXQT_BUILD_SPIKES=OFF -DXQT_BUILD_CLI=ON`. The dependencies are found as on the other platforms
   ([XojDeps.cmake](../cmake/XojDeps.cmake)): libxml2, libzip and qpdf as CMake packages, the GNOME libraries through
   pkg-config, and on Windows also libintl (gettext is not part of the C library there).
3. **Build** with ccache (its folder is cached between runs), `ninja -k 0`: it keeps going after an error, so one run
   lists every file that does not compile. The errors are repeated at the end of the step and in the run's summary.
4. **Deploy** ([qt/scripts/windows-deploy.sh](../scripts/windows-deploy.sh)):
   - `cmake --install` (the program, the CLI, `share/xournal-qt`);
   - `windeployqt --qmldir qt/src/app/qml`: Qt's DLLs and plugins and the QML modules the QML files import. When
     windeployqt is missing or fails, the script copies Qt's plugin folders and the QML modules the app imports
     (`QtQml`, `QtQuick` with Controls, Material, Layouts, Dialogs, ...) by hand, and says so as a warning;
   - the SVG image and icon plugins (the icons are SVG files, and the program does not link Qt Svg itself, so
     windeployqt would leave them out), the off-screen platform plugin (for scripted runs);
   - `bin\qt.conf`, so that Qt looks for plugins and QML modules next to the program;
   - the data of poppler, gdk-pixbuf and fontconfig;
   - every DLL from MSYS2 that any `.exe` or `.dll` in the folder imports, recursively (read with `objdump -p`;
     nothing is run), and a final check that every import is in `bin\` or part of Windows.
5. **Publish** the folder as the artifact `xournal-qt-windows-x64`.
6. **Smoke test** ([qt/scripts/windows-smoke.sh](../scripts/windows-smoke.sh)), with a `PATH` of Windows alone so
   that a DLL missing from the folder shows here: the CLI's `--version`; exports chosen to tell apart what fails
   (strokes to PNG: raster without text; text to PDF: text without raster; text to PNG: both; images to PDF; a PDF
   background to PDF); then the app itself, off-screen with Qt Quick's software renderer, opening a library and a
   document and saving a screenshot of its window after 5 s. Last, `text-probe` ([qt/tools/text-probe.c](../tools/text-probe.c)),
   Pango and Cairo alone drawing text into a PNG and a PDF with the folder's DLLs, with each of Pango's font backends
   and with and without the UTF-8 C locale.

   A step that fails runs again under **gdb** (`<step>.gdb.log`: the backtraces of every thread, stopped at the
   crash, `abort()` or `exit()`, and the loaded DLLs; the build has `-g1` for function names). A failing text export
   also runs with `FC_DEBUG=1 G_MESSAGES_DEBUG=all`, with Pango's fontconfig backend (`PANGOCAIRO_BACKEND=fc`), and
   without the UTF-8 C locale (`XQT_NO_UTF8_LOCALE=1`); a failing app with `QT_DEBUG_PLUGINS=1 QML_IMPORT_TRACE=1`,
   the same variants, and without a document. MSYS2 reports a Windows program's fatal NTSTATUS as exit code 139
   (access violation) or 127 (anything else: stack overflow, heap corruption, `__fastfail` from `abort()` or an
   invalid C runtime parameter, a DLL that cannot be loaded).

The unit tests are not built on Windows yet: they use POSIX headers and `/proc` in places (see the roadmap).

### What the code does differently on Windows

| Where | Linux | Windows |
|---|---|---|
| [WindowsFonts.cpp](../src/app/WindowsFonts.cpp), at start of the app and the CLI | fontconfig is Pango's only backend | Pango's fontconfig backend with a configuration of our own (see "Text and fonts") |
| [WindowsSetup.cpp](../src/app/WindowsSetup.cpp), at start | – | The C library's character set becomes UTF-8 (`setlocale(LC_CTYPE, ".UTF-8")`): libstdc++'s `std::filesystem` converts narrow strings with it, and the app hands it UTF-8 everywhere (`QString::toStdString`, GLib, the core). Without it a path with an umlaut would be read in the ANSI code page. `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` point to Qt's generic folders unless set: GLib reads them on Windows too, and its default cache is the Internet Explorer cache folder |
| Single instance ([SingleInstance.cpp](../src/shell/SingleInstance.cpp)) | a local socket named after the uid | a named pipe named after a hash of the Windows user (pipes are shared by all users of a machine) |
| Crash recovery ([SessionRecovery.cpp](../src/shell/SessionRecovery.cpp)) | signal handlers; other instances are checked in `/proc` | also an unhandled-exception filter (access violations arrive as structured exceptions), chained to MinGW's; other instances are checked with `QueryFullProcessImageNameW`. No `SIGBUS` |
| Printing ([PdfPrinting.cpp](../src/shell/PdfPrinting.cpp)) | the PDF goes to `lp` as it is | no spooler takes a PDF: poppler draws each page into an image of at most 300 dpi, which Qt's print engine sends to the printer chosen in the Windows print dialog |
| "Show in file manager" ([SystemApps.cpp](../src/shell/SystemApps.cpp)) | D-Bus `FileManager1.ShowItems` | `explorer /select,<file>`. D-Bus is Linux only |
| Memory size ([CanvasMemory.cpp](../src/canvas/CanvasMemory.cpp)) | `sysconf` | `GlobalMemoryStatusEx` |
| Background render threads ([RenderService.cpp](../src/render/RenderService.cpp)) | `SCHED_IDLE` | `THREAD_PRIORITY_IDLE` |
| CLI arguments ([cli/main.cpp](../cli/main.cpp)) | `argv` | the UTF-8 command line (`g_win32_get_command_line`) |
| CMake | strict C++20 | gnu++20 (MinGW's headers hide `M_PI` and POSIX names under strict C++), `NOMINMAX`, 8 MB thread stacks as on Linux (MinGW's default is 2 MB), a GUI executable (`WIN32_EXECUTABLE`), `-Wa,-mbig-obj` (large translation units), no desktop files or `.deb` |

## Building it on a Windows machine

The same steps work in an MSYS2 UCRT64 shell (install MSYS2 from msys2.org, open "MSYS2 UCRT64"):

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,ccache,glib2,cairo,pango,gdk-pixbuf2,poppler,qpdf,libxml2,libzip,zlib,qt6-base,qt6-declarative,qt6-svg}
cmake -S qt -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXQT_BUILD_TESTS=OFF -DXQT_BUILD_SPIKES=OFF
cmake --build build
./build/xournal-qt.exe                                      # from the build tree, with MSYS2's DLLs on the PATH
qt/scripts/windows-deploy.sh build dist/xournal-qt          # the portable folder
qt/scripts/windows-smoke.sh dist/xournal-qt smoke           # its smoke test
```

## Known limitations of this first build

- **Not tested on a real machine yet**; the CI smoke test is all. The device checklist has a Windows section.
- **Pen input** goes through Qt's Windows Ink support (`WM_POINTER`) as it comes; pressure, eraser end, palm
  rejection and latency on the Surface are untested.
- **No installer**, no Start menu entry, no file associations, no program icon in Explorer (the window has its icon).
- **Not signed**: SmartScreen warns.
- **Printing** sends images (at most 300 dpi), not the PDF: larger print jobs and no vector output on the printer.
- **Replacing a file that is open**: saving a hybrid PDF, the pasted-pages PDF and a few caches renames a new file
  over the old one while it may still be open (poppler keeps its PDF open). Windows refuses that where Linux does
  not; saving such a document may fail until this is handled (see TODO.md, "rename semantics").
- **Paths**: the code joins paths with `std::filesystem`, which is fine, but a few places build or compare path
  strings with `/`. Library folders, the `.assets` folders of Markdown files and links between documents need a
  check with backslashes and with drive letters.
- **Markdown code blocks** have no syntax colours (KSyntaxHighlighting is not installed in the CI yet).
- **The unit tests** do not build on Windows yet.
- **Pango's Windows font backend is not used** (it dies drawing text into images, see "Text and fonts"); fontconfig
  does not know Windows' font fallback for scripts the chosen font lacks as well as DirectWrite does. The first
  start scans the Windows fonts for a few seconds.
