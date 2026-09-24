# Android: UI/UX roadmap

What the first APK (`qt/android-apk`, see [android.md](android.md)) leaves for later. The first APK only proves that
the desktop app builds, installs and starts on a phone (Galaxy Fold 7, Android 16) and is usable with a mouse and a
keyboard. Mobile UI work waits until mobile testing is a real concern. Items are observed (O) on a device or expected
(E) from the code and the research in `../cross-platform-qt-research/`.

## Seen on the Fold 7 and in the emulator (qt/android-basics, 2026-09-24), not fixed yet

- (O) The **selection bar** of the library (items selected) and long **breadcrumbs** are still one row that does not
  scroll: on the cover screen they can run past the right edge.
- (O) The **document tool bar** scrolls sideways on the cover screen (about half of the tools are off-screen); a
  compact tool bar for phones is the real fix.
- (O) **Markdown file being edited** in the emulator: the text shows overlapping, smeared glyphs while the cursor is
  in it (the same file reads fine). Probably the emulator's ARM translation, like the dark bars below; check on the
  phone.
- (O) The **recovery dialog** ("Recover unsaved changes?") appears after every `am start -S`/force stop with a
  changed document open: Android kills without warning. Done (`qt/android-libraries`): the autosaves are written when
  the app goes to the background (Lifecycle), so the dialog brings back everything drawn before.
- (O) "Open with" a 145 MB PDF (the author on the Fold 7) froze the window while it was copied: fixed (copied in the
  background, with a note); opening it again is instant.

## Seen in the emulator (2026-09-24)

A headless tablet emulator (2560×1600, Android 15, arm64 through ARM translation; see [android.md](android.md)).

- Done (`qt/android-basics`): **edge-to-edge**: the tab strip starts below the status bar (the window's safe area
  margin, `Main.qml` `safeTop`; the New document dialog keeps below it too). Only the top is handled: the bottom
  gesture bar and a side cut-out in landscape still overlap the canvas edge (harmless so far).
- Done: **missing symbols**: the tab close button and the sidebar's page menu button are the SVG icons now (as on
  the desktop), and a small symbol font (a DejaVu Sans subset, `qt/resources/fonts`) is Qt's fallback on Android
  for ✓ ✎ ☐ ● ⋮ ↵ and the arrows in texts. The phones have "Noto Sans Symbols" with them, but split over two
  files of that name, and Qt takes the one without them.
- Done: **the soft keyboard and the New document dialog**: the name field no longer takes the focus on Android (so
  the keyboard does not open by itself); when it is open, the dialog moves above it, gets only the room there and
  scrolls, so Create stays in reach; the keyboard's Enter key creates the document. The paper and orientation row
  wraps in a narrow window.
- Done: **the home screen on a phone-wide window**: the library header scrolls sideways (as the tool bar does), the
  search has a row of its own below 760 px, and the buttons of an empty folder stand one below the other (shared
  QML, so narrow desktop windows get the same).
- (O) **The image tool** opens Android's picker ("Recent images") when tapping the page; the chosen image comes back
  as a `content://` URI (see Files).
- (O) **Dark bars across text at the left edge of each 256 px canvas tile** (the page thumbnails, drawn in one
  piece, are clean). Probably the emulator's ARM translation of pixman's NEON code; check on the phone. If it shows
  there: try `PIXMAN_DISABLE=arm-neon` to find out, then look at how tiles clip text.
- (O) Startup takes about 7 s in the emulator (a minute on the very first start, while fontconfig scans
  `/system/fonts`). Measure on the phone.
- (O) The APK (94 MB) carries all Qt Quick Controls styles (Fusion, Imagine, Universal, FluentWinUI3) because the QML
  imports `QtQuick.Controls`; only Material and Basic are used. Excluding the others would save about 12 MB.


- (E) **Stylus samples.** Qt's Android plugin passes one sample per `MotionEvent`, without the historical
  (batched) samples and without tilt. Strokes drawn quickly get corners. Needs a small `QtActivity` subclass that
  replays `getHistorical*` samples through JNI (research 02-stylus-input.md). Measure first with an input logger
  (`xqt.input`). The Galaxy Fold 7 has no S Pen support, so stylus tests need another device (a Galaxy Tab with an
  S Pen) or the emulator's stylus.
- Done (`qt/android-basics`): **draw with the finger** (tool bar toggle, Settings → Touch), on by default on
  Android devices without a stylus. Open: a long press with the finger while drawing takes the dot back and shows
  the context menu only with the pen and highlighter (as the pen does); other tools just draw.
- (E) **Palm rejection and touch vs. pen**: the canvas already separates them on Linux; Android reports
  `TOOL_TYPE_STYLUS` / `TOOL_TYPE_ERASER`, which Qt maps to `QPointingDevice` types. Check the eraser end and the
  side button.
- (E) **Right click and hover** exist with a mouse (the author's first test), not with a finger: context menus
  (page menu, library cards) need a long press everywhere.
- (E) **Keyboard shortcuts** work with a hardware keyboard. The soft keyboard covers the lower half of the screen
  while typing in a text box or Markdown box; the manifest asks for `adjustResize`, but the canvas does not yet scroll
  the caret into view.

## Files

- Done (`qt/android-basics`): **"Open…", "Import files…", "Import a folder…" and "Insert image"** read what
  Android's pickers return (`content://`) and copy it into the library (android.md).
- (E) **Saving and exporting to a place the user picks** ("Save as", "Export as PDF", the archive export, the
  library archive): Android's save picker returns a `content://` URI too, which the core cannot write. Write to a
  file in the cache and copy it through `QFile` on the URI, the other way round.
- Done (`qt/android-basics`): **"Open with" and the share sheet** copy the file into the library's folder "Opened"
  and open it (android.md). Open: sharing several files at once (SEND_MULTIPLE) is handled but was only tested with
  one; `.xopp` files that other apps hand over as `application/octet-stream` make the app appear in "Open with" for
  every unknown file type.
- (E) **Writing back**: a document opened from another app is a copy; changes do not go back to the original (e.g.
  a PDF in a cloud app). A later step could keep the URI grant (`takePersistableUriPermission`) and offer "Save back".
- Done (`qt/android-libraries`): **libraries in the shared storage** (VISION: libraries anywhere) with "All files
  access": a picked folder's tree URI is mapped to its path, and the window switches to another library instead of
  starting a process (android.md). Open: folders only a cloud app's provider offers (`content://` trees of Nextcloud,
  Drive, OneDrive) stay out; they would need a library that reads through SAF. Google Play restricts
  `MANAGE_EXTERNAL_STORAGE`: a Play build would need another way (SAF trees, or Play's exception for file managers
  and document apps).
- (E) **"Show in file manager"** has no Android equivalent; hide it. "Open externally" becomes an intent with a
  `FileProvider` URI (the provider is already in the manifest).
- (E) **Printing** calls `lp`; on Android use the print framework or hide Print.

## Screen and windows

- (E) **Fold posture and split screen**: the activity is resizable (manifest), and Qt gets `screenSize` changes
  without a restart. Check that the canvas keeps the page and zoom when the Fold opens or closes, and in split
  screen / pop-up view. Consider `WindowManager` fold features (hinge position) for a two-page layout later.
- (E) **Tabs and tool bar on a phone-wide screen** (folded, 6.5"): the tab strip and the tool bar are made for
  desktop widths. Needs a compact layout.
- (E) **Undocked windows** (a document in a window of its own) make no sense on Android; the action should open a
  tab.
- (E) **Safe areas**: the status bar and the gesture bar overlap the window in edge-to-edge mode (target SDK 35+
  forces edge-to-edge). Qt 6.9+ exposes `SafeArea` margins in QML; the tool bar and the bottom sheets need them.
- (E) **Density**: Qt scales by the device pixel ratio; the Material style's touch targets are fine, the canvas
  zoom levels and the pen widths in pixels need checking at DPR 2.6–3.

## Lifecycle

- Done (`qt/android-libraries`): **autosave when the app goes to the background** (android.md). Only the autosave,
  not a save of the user's file: a save changes the file other apps and sync clients see, and a half-finished
  thought would be uploaded; the recovery dialog decides after a kill. Open: saving the files themselves as an option.
- (E) **Crash handlers**: `SessionRecovery::installCrashHandlers()` is off on Android, because replacing the signal
  handlers hides the backtrace in logcat. Chain to the previous handler (`sigaction`) and turn it on again.
- (E) **Memory**: the canvas memory budget (`CanvasMemory`) is a desktop default. Android's per-app limit and
  `onTrimMemory` should lower it.

## Look

- (E) Fonts: text and Markdown boxes use Android's fonts through fontconfig (Roboto for "Sans"). Documents made on
  Linux with other fonts show Roboto/Noto instead. Users can add fonts to `<app data>/fonts`.
- (E) The app icon is the desktop SVG rendered to PNG; an adaptive icon (foreground + background layers) would suit
  launchers that mask icons.
