# Fractional scaling (125 %, 150 %, 175 %)

How xournal-qt behaves when the screen is scaled by a fraction, what was checked and fixed (`qt/hidpi-fractional`,
2026-10-04), what each platform hands the app, and how to test it. The device steps are in
[testing/device-checklist.md](testing/device-checklist.md), "Fractional scaling".

## In short

- Qt 6 passes the platform's factor through unchanged (`Qt::HighDpiScaleFactorRoundingPolicy::PassThrough`, the
  default since Qt 6.0). **The app sets no policy, no `AA_` high-DPI attribute and no `QT_*` scaling variable**
  (`main.cpp` only turns off event compression for the pen). Nothing in the app rounds the device pixel ratio
  (dpr) to a whole number.
- The window's dpr is fractional where the platform gives a fraction: Wayland with `wp_fractional_scale_v1`
  (KDE Plasma 5.27 and 6, GNOME with fractional scaling, Sway, Hyprland), X11 through `Xft.dpi` or
  `QT_SCREEN_SCALE_FACTORS`, Windows (per-monitor DPI awareness v2, Qt's default), Android (densities such as
  2.625). macOS only ever gives 1 or 2 (a "scaled" resolution is drawn at 2x and the system scales the picture).
- The canvas (page tiles, selection, setsquare, curtain, the pointer) draws with the screen's pixels and places
  its pictures on whole device pixels, so it is as sharp at 125 % as at 100 %. The pictures of pages in lists
  (sidebar, grids, overviews) are drawn for the screen's pixels once. The app's own thin lines and page frames are
  whole device pixels.
- Tests: `FractionalScaleCanvas` (quick) and `FractionalScale` (ui) run as they are and, through CTest, with
  `QT_SCALE_FACTOR` 1.25 and 1.67 (canvas) and 1.5 (window). The whole quick suite passes at 1, 1.25 and 1.5. The
  whole UI suite at 1.5 on a big enough off-screen screen (see "Running the tests at a scale"), in one process: 266 of
  270 pass; of the other four, three pass when run alone (state left by earlier tests in the same process) and one
  (`zoomPercentageTapDoubleTapAndHold`) depends on the window's size: there "fit the height" happens to be 100 %.

## What the audit found

| Part | At a fractional dpr | Status |
| --- | --- | --- |
| `main.cpp` high-DPI setup | No rounding policy, no `AA_EnableHighDpiScaling` (a no-op in Qt 6), no environment variable forced. | Right as it is |
| Canvas view dpr (`CanvasView::setDevicePixelRatio`) | The window's `effectiveDevicePixelRatio()`, unrounded; a change (another screen) renders the pages again. | Right |
| Page rasters (`PageRaster`, upstream's `Mask`) | Cairo surfaces with a fractional device scale: `int(ceil(w * zoom) * dpr)` pixels (cairo cuts the last fraction). A big page drawn in part starts on a whole device pixel (`pixelStep`). | Right; `pageTilesAreShownPixelForPixel` compares a tile with the window's pixels at 1 to 2 |
| Page tiles (`DocumentCanvasItem`) | The page's top left is snapped to a device pixel (`snap(r, dpr)`); tiles are `TILE` buffer pixels at `px / dpr`, so 1:1 with the screen. | Right |
| Selection picture | Placed at the page's unsnapped position (blurred at any scale), drawn `int(w * dpr)` pixels for `w * dpr` shown, not redrawn for another screen. | **Fixed** (b81b41a) |
| Setsquare / compass (`GeometryToolPicture`) | Pictures at `zoom * dpr` covering whole pixels; page and angle display snapped; dpr in the redraw key. | Right (turned tools are filtered anyway) |
| Canvas turned by a quarter ([canvas-rotation.md](canvas-rotation.md)) | The root node's translation is put on a whole device pixel at multiples of 90°, so the turned tiles land pixel for pixel (`pageTilesAreShownPixelForPixelTurnedByAQuarter`, also at 1.25 and 1.67; the off-screen software renderer does not blend either way). Free angles are filtered. | Built that way |
| Curtain | Handles (rect nodes) at fractional places: frames 2 or 3 pixels thick; the knob placed off-pixel and made once for the first screen. | **Fixed** (f174aff) |
| Pointer: cursor pixmaps (`HoverPointer`) | `ceil(side * dpr)` pixels with `setDevicePixelRatio(dpr)`; the cursor key includes the dpr; refreshed on `ItemDevicePixelRatioHasChanged`. | Right (`theDotCursorHasTheScreensPixels`) |
| Pointer: the dot drawn for pens without a platform cursor | Picture squeezed into 8 logical pixels where `8 * dpr` is not whole (1.1, 1.33, 1.67). | **Fixed** (910cd42) |
| Thumbnails, page grid, overviews, page hits, annotation pictures | QML gave `sourceSize = width * Screen.devicePixelRatio`, and Qt Quick multiplies an image provider's `sourceSize` by the dpr again: drawn at dpr² (2.25x as wide at 150 %, 4x at 200 %). | **Fixed** (27d9c1d) |
| Sketches and previews (`PageSketches`, `PreviewCache`) | Fixed widths (they are placeholders and library cards); the dpr is not in their keys, nor needs to be. | Right; library card previews (360 px) are a little soft on 2x screens (not fractional-specific) |
| Thumbnail cache keys | The requested width (in steps of 64) is the key, and it now carries the dpr once. | Right |
| QML icons | SVG `Image`s with a `sourceSize` in logical pixels: Qt renders SVG at `sourceSize * dpr`. | Right |
| QML 1 px lines and square frames | Drawn without antialiasing: 1.25 or 1.5 device pixels cover 1 or 2 rows depending on the position. | **Fixed** for the app's own separators and page frames (2a32f03, `Hairline.qml`, `DevicePixels.js`) |
| Qt's `ToolSeparator`, `MenuSeparator` (Material style) | Same unevenness (Qt's own style). | Left: see "Open" |
| Rounded frames (`radius`) | Antialiased: soft at a fractional width, not uneven. | Left as they are |
| Text | Qt Quick's default text rendering (distance fields) scales smoothly at any factor; the page's text is drawn by cairo/Pango at the device scale. | Right |
| Menus a whole number of logical pixels wide (2026-09-27 CI fix) | A logical width; at 125 % that is not whole device pixels, but nothing depends on it (Qt Layouts place in logical pixels). | Right |
| Screen calibration (`ScreenCalibration`) | `zoom100 = ppi / dpr / 72` with the fractional dpr; Settings shows "scaled 125 %". | Right |

## Platforms

**KDE Plasma, Wayland** (Kubuntu 24.04 has Plasma 5.27, 25.04 and later Plasma 6). KWin offers
`wp_fractional_scale_v1`; Qt 6.5 and newer use it, so the app gets 1.25 or 1.5 and draws at that size. With an
older Qt (6.4, Ubuntu 24.04's own) the app gets 2 and KWin scales the window down (slightly soft). The app needs Qt
6.5 anyway. Nothing to set.

**KDE Plasma, X11.** Plasma sets `Xft.dpi` (120 at 125 %, 144 at 150 %) and, depending on the version, also
`QT_SCREEN_SCALE_FACTORS`. Qt 6 takes the factor from them, fractional. X11 has one factor for all screens
(unless `QT_SCREEN_SCALE_FACTORS` names each). If the app comes up at 100 % or 200 % on a 125 % desktop:
`xrdb -query | grep dpi` and `env | grep QT_` tell which value it saw.

**GNOME, Wayland.** Fractional scaling is a setting (Settings → Displays; behind the experimental feature
`scale-monitor-framebuffer` in older releases). With it Mutter offers `wp_fractional_scale_v1` (recent releases)
and the app gets e.g. 1.25 or GNOME's odd values (1.33, 1.67, 1.75, chosen so that the logical size is whole).
Without the protocol the app gets 2 and Mutter scales it down. Under XWayland (`QT_QPA_PLATFORM=xcb`, or if Qt
chose xcb) GNOME scales the window as a bitmap (soft) unless its newer native scaling for XWayland is on; start
with `QT_QPA_PLATFORM=wayland` to be sure which one is tested.

**Windows** (Surface: 150 % or 200 % by default; 125 %, 175 % common). Qt 6 is per-monitor DPI aware (v2) by
default: the app gets the monitor's factor and a new one when the window moves to another monitor (pages, the
selection, the curtain's knob and the cursor are drawn anew: `aScreenOfAnotherScaleGetsPicturesForItsPixels`).

**macOS.** 1 or 2 only. Nothing fractional reaches the app.

**Android.** Densities give fractional ratios (2.625, 2.75, 3.5; 1.33 for tvdpi). The same code applies; the
drawn pen dot is the part Android uses that other platforms do not.

### Settings that change it (documented, not forced)

| Variable | What it does |
| --- | --- |
| `QT_SCALE_FACTOR_ROUNDING_POLICY` | `PassThrough` (default), `Round`, `Ceil`, `Floor`, `RoundPreferFloor`. Someone who prefers whole factors (crisper Qt controls, sizes off by up to 25 %) can set `Round`. The app does not set it: the default is right. |
| `QT_SCREEN_SCALE_FACTORS` | Per-screen factors (`DP-1=1.25;HDMI-1=1`), X11 mostly. |
| `QT_SCALE_FACTOR` | A factor on top of the platform's (also how the tests simulate a scaled screen). |
| `QT_ENABLE_HIGHDPI_SCALING=0` | Turns scaling off (everything tiny on a HiDPI screen). |
| `QT_FONT_DPI` | Changes only the fonts' size. |

## Open

- **Qt's `ToolSeparator` and `MenuSeparator`** (pills, menus) are 1 logical pixel: at 125 % / 150 % some come out
  one pixel thick, some two. The app could replace their `contentItem` with a `Hairline` (about 45 places, or one
  shared component). Left for the author to decide (see the device checklist's note).
- **Rounded frames** stay antialiased and slightly soft at fractional widths; snapping cannot help a curved edge.
- **Positions inside lists and layouts** are logical: a frame at x = 10.4 logical is 13 device pixels at 125 %, so
  the content inside it (filtered images, icons) is not always on whole device pixels. Only the canvas snaps its
  pictures.
- **Wayland cursor buffers**: a cursor pixmap with a fractional ratio is handed to the compositor as Qt Wayland
  chooses (an integer buffer scale, or the cursor-shape protocol for shaped cursors). The dot may be slightly
  soft there; a device check.
- **`CurtainCanvasTest` at `QT_SCALE_FACTOR=2`** fails off-screen (it passes at 1 to 1.75, also before this block): one
  logical pixel in the middle of the curtain shows the page. The curtain's hidden spotlight parts are rectangles of
  zero size at its middle; Qt Quick's software renderer (used off-screen) seems to count their pixel as covered and
  then paints nothing there. GPU renderers draw nothing for them. Worth a look on a device at 200 % and in the
  software renderer (`QT_QUICK_BACKEND=software`).

## Running the tests at a scale

```sh
QT_SCALE_FACTOR=1.25 build-qt/xqt-quick-tests                    # the whole quick suite (all pass at 1, 1.25, 1.5)
ctest --test-dir build-qt -R FractionalScale                     # the focused tests, also at 1.25, 1.67 and 1.5
QT_QPA_PLATFORM=offscreen:configfile=qt/tests/ui/offscreen-hidpi.json QT_SCALE_FACTOR=1.5 build-qt/xqt-ui-tests
```

The off-screen platform's own screen is 800 x 600 device pixels, 533 x 400 at 150 %: too small for the desktop
layout, so the UI tests use `qt/tests/ui/offscreen-hidpi.json` (3200 x 2400) at a scale. The quick tests convert
between logical and device pixels where Qt hands them device pixels (`tests/quick/DevicePixels.h`: events given to
`QWindowSystemInterface`, `grabWindow()`).
