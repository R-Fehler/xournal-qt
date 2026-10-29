# Windows: roadmap

What the Windows build ([windows.md](windows.md)) leaves for later. The first build
only proves that the app builds on GitHub Actions, runs from a portable folder and passes a smoke test. Items are
observed (O) on a machine or expected (E) from the code and the research in `../cross-platform-qt-research/`.

## Text

- (O) **Pango's win32 backend dies drawing text into an image surface** (CI smoke test, 2026-09-24: exit code 127, a
  fatal NTSTATUS without a message; into a PDF it works). The app uses the fontconfig backend instead
  ([windows.md](windows.md), "Text and fonts"). To do: get the backtrace (the smoke test runs `text-probe` with
  Pango's default under gdb), reproduce it with MSYS2's Pango and Cairo alone, and report it upstream (Pango or
  Cairo's DirectWrite font code). Then `XQT_WIN_PANGO_WIN32=1` tries the win32 backend again; the smoke test says
  when it works.
- (E) **Font fallback**: fontconfig does not know DirectWrite's fallback lists; check CJK, Arabic, emoji and math
  symbols in text boxes and Markdown.
- (E) **First start**: measure the font cache build on the Surface (a scan of `C:\Windows\Fonts` and the user's
  fonts), and whether the background warm-up hides it.

## Pen and touch (the Surface)

- (E) **Windows Ink**: Qt 6 reads the pen through `WM_POINTER` (Windows Ink) by default, as `QTabletEvent`s with
  pressure and tilt. Check on the Surface: pressure, the eraser end of the Surface Pen (`QPointingDevice::Eraser`),
  the barrel button (right click?), hover (the pen cursor), and that a pen does not also arrive as mouse events.
- (E) **Latency**: Qt draws after the event; Windows Ink's own low-latency "ink presenter" is not used. Measure
  against the Linux build on the same device (pen latency is an open experiment in TODO.md).
- (E) **Palm rejection and touch**: touch arrives as `QTouchEvent` through `WM_POINTER`; Windows' own palm rejection
  applies before Qt. Check that two-finger zoom and scroll work and that the palm does not draw.
- (E) **Precision touchpads**: pinch and two-finger scroll arrive as native gestures / high-resolution wheel events;
  check the momentum (TouchpadMomentum.qml) against Windows' own.
- (E) Windows' **"press and hold" right click** and the pen flicks may get in the way of long presses; Qt can turn
  them off per window.

## Installer and updates

- **Installer**: done, per user, NSIS, adapted from upstream's ([windows.md](windows.md#the-installer)); it updates an
  installed version in place. Later perhaps MSIX (the Store, clean install and removal, but sandboxed file access and
  package identity to handle).
- (E) **Update check in the app**: none yet. A look at the GitHub releases (at most daily, off in Settings), then
  download the new `…-windows-setup-x64.exe` and run it with `/S` after the app quit; the installer already updates
  silently and refuses to while the app runs.
- (E) **Size**: the folder carries all of Qt Quick Controls' styles and every image format plugin; trim to what the
  app uses (Material, Basic, SVG, PNG, JPEG) once it runs.

## Signing and trust

- (E) **Code signing**: unsigned programs get SmartScreen's "Windows protected your PC". Options: an OV/EV
  certificate, Azure Trusted Signing (cheap, needs an identity check), or SignPath's free offer for open source
  projects. Sign the `.exe`, the DLLs of our own, the installer and its uninstaller in CI (NSIS: `!uninstfinalize`
  signs the uninstaller while the installer is built).

## Integration with Windows

- **File associations and the program icon**: done by the installer and `xournal-qt.rc.in`
  ([windows.md](windows.md#the-installer)). Open: `.md` files (the app opens them; not registered yet), and a
  "Set as default PDF app" in the app's settings (the same Settings page as the installer's component).
- (E) **Application manifest**: `activeCodePage` UTF-8 (then the ANSI APIs that libraries use take UTF-8 too, not
  only the C library, see WindowsSetup.cpp), `longPathAware`, and the supported OS versions. MinGW links a default
  manifest, which must be replaced rather than doubled.
- (E) **"Open as library" from Explorer**: the Linux build has a Dolphin service menu; Windows would need a context
  menu entry for folders (installer, registry).
- (E) **Jump lists / recent documents** in the taskbar.
- (E) **Dark mode and accent colour**: the Material style follows the app's own setting; check the title bar.

## High-DPI and displays

- (E) Qt 6 is per-monitor DPI aware (v2) by default: check fractional scales (150 %, 175 %, 200 % on the Surface),
  moving a window between monitors of different scale, the canvas tiles' sharpness and the thumbnails.
- (E) Tablet mode and rotation of the Surface: the window resizes; check the pages and the tool bar.

## Files and paths

- (E) **Renaming over an open file**: hybrid PDFs, the pasted-pages PDF and some caches are written by renaming a
  new file over the old one. Windows refuses that while the file is open (poppler keeps its PDF open). Needs
  `MoveFileEx(MOVEFILE_REPLACE_EXISTING)` after closing the reader, or `ReplaceFile`, and a test.
- (E) **Paths as strings**: places that compare or split path strings on `/` (library folders, `.assets`, links
  between documents, recent libraries) need checking with `\`, drive letters and UNC paths (`\\server\share`).
- (E) **OneDrive's "files on demand"**: documents in `Documents` may be placeholders; opening and indexing a library
  there downloads every file. The library index should skip files that are not local
  (`FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`).
- (E) **Trash**: `QFile::moveToTrash` uses the Recycle Bin; check files on network drives (no Recycle Bin there).

## Build and tests

- (E) **Unit tests on Windows**: they include `<unistd.h>`/`<fcntl.h>` and read `/proc` in a few places
  (LibraryTest, DocumentSearchTest). Port those, then build the tests in the Windows job and run the fast labels.
- (E) **KSyntaxHighlighting** for Markdown code blocks: MSYS2 packages KDE Frameworks 6; add it to the job once
  the base build is green.
- (E) **Debug information**: the release build has none. A separate `RelWithDebInfo` artifact, or split debug files,
  would make crash reports useful; `cpptrace` (upstream's) could print a stack trace in the crash handler.
- (E) **MSVC route**, if MSYS2 ever blocks: vcpkg with `qt/vcpkg.json` (the Android manifest) and QField's CI as the
  model (`../QField`).
