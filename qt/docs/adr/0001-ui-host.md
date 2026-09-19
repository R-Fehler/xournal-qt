# ADR-0001: Canvas host (Qt Quick vs Qt Widgets)

- Status: **Accepted (2026-09-19): Qt Quick.** Based on the M0 spike (`qt/spikes/inkpad`) on the target device.

## Context
The UI shell should be Qt Quick: touch-first, modern, and portable to mobile later.

Krita, the reference Qt tablet application, moved its canvas and touch UI away from QML after tablet-input and lifetime bugs, mostly in the Qt 5 era. It also patches Qt 6.8 for several Wayland tablet issues.

We therefore built the canvas independent of the host and compared two thin hosts on the real device:
- a `QQuickItem` with scene-graph texture tiles, taking input through an event filter on the `QQuickWindow`;
- a raster `QWidget`.

## Evaluation
Device: ThinkPad Yoga, Plasma 6.2 Wayland, Qt 6.7.2 (system), AES pen with one side button, 2-point touchscreen. Evidence: six JSONL logs from `xqt-inkpad`, summarized with `analyze_log.py`, plus the user's hands-on test.

| # | Criterion | Quick | Widget |
|---|-----------|-------|--------|
| 1 | Pen press/move/release with float positions, pressure and tilt | ✅ pressure 0.01–0.83, tilt x −55…0 / y −23…+12, 100 % sub-pixel | ✅ same |
| 2 | Eraser detected (`pointerType == Eraser`) | ✅ (the side button, see below) | ✅ |
| 3 | Barrel buttons in `buttons()` | ✅ MiddleButton (mask 4) seen | ✅ |
| 4 | Hover moves while in proximity | ✅ | ✅ |
| 5 | Enter/LeaveProximity (delivered to qApp) | ✅ | ✅ |
| 6 | No synthesized mouse events on the canvas after accepting tablet events | ✅ 0 synthesized | ⚠️ synthesized events seen (from toolbar buttons and touch) |
| 7 | 1–2 finger touch; smooth pinch, pan and fling | ✅ | ✅ |
| 8 | Touchpad pinch as `QNativeGestureEvent` | not tested | not tested |
| 9 | Latency / ink lag | "indistinguishable" (user); event→handler ≈ 0.9 ms | same |
| 10 | Maliit on-screen keyboard for a text field | ✅ | ✅ |
| 11 | GLib event dispatcher (`g_idle_add` fires) | ✅ `QPAEventDispatcherGlib` | ✅ |

The user found both hosts indistinguishable and exactly as intended: writing, side-button erase, touch keyboard, undo/redo taps, fit, clear, fling, pan and zoom.

## Decision
**Qt Quick** hosts the canvas (a custom `QQuickItem`) and the whole UI shell.
- It meets every tested criterion.
- It delivers no synthesized mouse events on the canvas.
- Pinch and fling are GPU transforms.
- It keeps the mobile path open.

The widget host is kept only in the spike, for diagnostics.

## Facts about input on this platform (the M4 input port must handle them)
- **Device identity:** Qt 6.7 on Wayland names every tablet `"fake tablet"` (type `Stylus`), the touchscreen `"some touchscreen"` and the touchpad `"touchpad"`. Classify devices by `QInputDevice::DeviceType` plus `QPointingDevice::PointerType`, never by name. This differs from upstream's per-name settings.
- **Side button = tool switch:** pressing the AES side button while hovering produces `LeaveProximity(Pen)` then `EnterProximity(Eraser)`. After that the pen reports `pointerType == Eraser`, so it maps naturally to upstream's `ButtonConfig` "eraser" button.
  - In some sequences the button arrives as `MiddleButton` (mask 4) on a Pen tool instead, including while the tip is down, and proximity toggles mid-press. The port must end or cancel a running stroke on a proximity or tool change, and map MiddleButton/RightButton to upstream's stylus buttons 1/2.
- **Event rate:** tablet moves arrive at 100–250 Hz (median 4–10 ms), touch at 100 Hz. Event timestamps are CLOCK_MONOTONIC milliseconds (receive − event ≈ 0.9 ms), so they are usable for latency and velocity.
- **Proximity:** proximity events are reliable on this setup (KWin tablet-v2). They can drive palm rejection, keeping the timeout fallback.
- **Touch contact size:** touch points report `ellipseDiameters`, usable later for a contact-size palm heuristic.
