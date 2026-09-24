# Android: UI/UX roadmap

What the first APK (`qt/android-apk`, see [android.md](android.md)) leaves for later. The first APK only proves that
the desktop app builds, installs and starts on a phone (Galaxy Fold 7, Android 16) and is usable with a mouse and a
keyboard. Mobile UI work waits until mobile testing is a real concern. Items are observed (O) on a device or expected
(E) from the code and the research in `../cross-platform-qt-research/`.

## Seen in the emulator (2026-09-24)

A headless tablet emulator (2560×1600, Android 15, arm64 through ARM translation; see [android.md](android.md)).

- (O) **Edge-to-edge**: the status bar lies over the tab strip (the clock covers the library tab). Needs the safe
  area margins (see Screen and windows).
- (O) **Missing symbols in the UI font**: the tabs' close button "✕" shows an empty box. Android's fonts have no
  such glyph and Qt finds no fallback. The QML uses more of these (✎ ☐ ✓ ● ⋮ ↵ arrows); use SVG icons (Lucide has
  them) or check each glyph against Roboto/Noto.
- (O) **The soft keyboard** opens as soon as the New document dialog shows (its name field has the focus) and hides
  half of the dialog, including Create. With a hardware keyboard this does not happen.
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
- (E) **Keyboard shortcuts** work with a hardware keyboard. The soft keyboard covers the lower half of the screen
  while typing in a text box or Markdown box; the manifest asks for `adjustResize`, but the canvas does not yet scroll
  the caret into view.

## Files

- (E) **The system file picker** (QtQuick.Dialogs `FileDialog` → Android's `ACTION_OPEN_DOCUMENT`) returns
  `content://` URIs, not paths. The core opens paths. Until there is a Storage Access Framework layer (copy in,
  write back, or keep a persisted URI grant), documents live in the app's own folder:
  `/storage/emulated/0/Android/data/org.xournalqt.app/files/Documents/Xournal_Libraries/Default`
  (reachable with `adb push` and over USB).
- Done (`qt/android-basics`): **"Open with" and the share sheet** copy the file into the library's folder "Opened"
  and open it (android.md). Open: sharing several files at once (SEND_MULTIPLE) is handled but was only tested with
  one; `.xopp` files that other apps hand over as `application/octet-stream` make the app appear in "Open with" for
  every unknown file type.
- (E) **Writing back**: a document opened from another app is a copy; changes do not go back to the original (e.g.
  a PDF in a cloud app). A later step could keep the URI grant (`takePersistableUriPermission`) and offer "Save back".
- (E) **Libraries anywhere** (VISION): a library folder chosen by the user means a SAF tree URI
  (`ACTION_OPEN_DOCUMENT_TREE`); the library code (`qt/src/shell/Library*`) scans with `std::filesystem`.
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

- (E) **Saving when the app goes to the background**: Android may kill a background app without warning. Save
  (or write the recovery files) on `Qt::ApplicationSuspended`.
- (E) **Crash handlers**: `SessionRecovery::installCrashHandlers()` is off on Android, because replacing the signal
  handlers hides the backtrace in logcat. Chain to the previous handler (`sigaction`) and turn it on again.
- (E) **Memory**: the canvas memory budget (`CanvasMemory`) is a desktop default. Android's per-app limit and
  `onTrimMemory` should lower it.

## Look

- (E) Fonts: text and Markdown boxes use Android's fonts through fontconfig (Roboto for "Sans"). Documents made on
  Linux with other fonts show Roboto/Noto instead. Users can add fonts to `<app data>/fonts`.
- (E) The app icon is the desktop SVG rendered to PNG; an adaptive icon (foreground + background layers) would suit
  launchers that mask icons.
