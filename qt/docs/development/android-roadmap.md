# Android: UI/UX roadmap

What is left for later on Android ([android.md](android.md) has the build and what works). Seen on a Galaxy Fold 7
(Android 16) and in the emulator. Items are observed (O) on a device or expected
(E) from the code and the research in `../cross-platform-qt-research/`.

## Seen on the Fold 7 and in the emulator, not fixed yet

- (O) The **selection bar** of the library (items selected) and long **breadcrumbs** are still one row that does not
  scroll: on the cover screen they can run past the right edge.
- (O) **Markdown file being edited** in the emulator: the text shows overlapping, smeared glyphs while the cursor is
  in it (the same file reads fine). Probably the emulator's ARM translation, like the dark bars below; check on the
  phone.
- (O) The **recovery dialog** ("Recover unsaved changes?") appears after every `am start -S`/force stop with a
  changed document open: Android kills without warning. The autosaves are written when the app goes to the background
  (Lifecycle), so the dialog brings back everything drawn before.

## Seen in the emulator

A headless tablet emulator (2560×1600, Android 15, arm64 through ARM translation; see [android.md](android.md)).

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
- (E) **Palm rejection and touch vs. pen**: the canvas already separates them on Linux; Android reports
  `TOOL_TYPE_STYLUS` / `TOOL_TYPE_ERASER`, which Qt maps to `QPointingDevice` types. Check the eraser end and the
  side button.
- (E) **Right click and hover** exist with a mouse (the author's first test), not with a finger: context menus
  (page menu, library cards) need a long press everywhere.
- (E) **Keyboard shortcuts** work with a hardware keyboard. The soft keyboard
  ([adaptive-layout.md](../features/adaptive-layout.md), "The soft keyboard") is not checked on the Fold 7 yet
  (device checklist).

## Files

- (E) **Saving and exporting to a place the user picks** ("Save as", "Export as PDF", the archive export, the
  library archive): Android's save picker returns a `content://` URI too, which the core cannot write. Write to a
  file in the cache and copy it through `QFile` on the URI, the other way round.
- (E) **Writing back**: a document opened from another app is a copy; changes do not go back to the original (e.g.
  a PDF in a cloud app). A later step could keep the URI grant (`takePersistableUriPermission`) and offer "Save back".
- (E) **Printing** calls `lp`; on Android use the print framework or hide Print.

## Screen and windows

- (E) **Fold posture and split screen**: the activity is resizable (manifest), and Qt gets `screenSize` changes
  without a restart. Check that the canvas keeps the page and zoom when the Fold opens or closes, and in split
  screen / pop-up view. Consider `WindowManager` fold features (hinge position) for a two-page layout later.
- (E) **Density**: Qt scales by the device pixel ratio; the Material style's touch targets are fine, the canvas
  zoom levels and the pen widths in pixels need checking at DPR 2.6–3.

## Lifecycle

- (E) **Crash handlers**: `SessionRecovery::installCrashHandlers()` is off on Android, because replacing the signal
  handlers hides the backtrace in logcat. Chain to the previous handler (`sigaction`) and turn it on again.
- (E) **Memory**: the canvas memory budget (`CanvasMemory`) is a desktop default. Android's per-app limit and
  `onTrimMemory` should lower it.

## Look

- (E) Fonts: text and Markdown boxes use Android's fonts through fontconfig (Roboto for "Sans"). Documents made on
  Linux with other fonts show Roboto/Noto instead. Users can add fonts to `<app data>/fonts`.
- (E) The app icon is the desktop SVG rendered to PNG; an adaptive icon (foreground + background layers) would suit
  launchers that mask icons.
