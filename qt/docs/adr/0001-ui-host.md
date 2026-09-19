# ADR-0001: Canvas host (Qt Quick vs Qt Widgets)

- Status: **Proposed**. It will be decided with the M0 spike (`qt/spikes/inkpad`) on the target device.

## Context
The UI shell should be Qt Quick: touch-first, modern, and portable to mobile later.

Krita, the reference Qt tablet application, moved its canvas and touch UI away from QML after tablet-input and lifetime bugs, mostly in the Qt 5 era. It also patches Qt 6.8 for several Wayland tablet issues.

We therefore build the canvas independent of the host and compare two thin hosts on the real device:
- a `QQuickItem` with scene-graph texture tiles;
- a raster `QWidget`.

## Decision criteria (all tested on the ThinkPad Yoga, Plasma 6.2 Wayland, Qt 6.7.2)
| # | Criterion | Quick | Widget |
|---|-----------|-------|--------|
| 1 | Pen press/move/release arrive with float positions, pressure and tilt | | |
| 2 | The eraser end is detected (`pointerType == Eraser`) | | |
| 3 | Barrel buttons are reported (`buttons()`) | | |
| 4 | Hover moves arrive while the pen is in proximity | | |
| 5 | Enter/LeaveProximity events arrive (delivered to qApp) | | |
| 6 | No duplicate synthesized mouse events after accepting tablet events | | |
| 7 | 1–3 finger touch arrives; pinch and pan stay smooth (about 60 fps) | | |
| 8 | Touchpad pinch arrives as `QNativeGestureEvent` | | |
| 9 | Input→frame latency (HUD) and 240 fps video: ink lag vs upstream Xournal++ | | |
| 10 | The on-screen keyboard (Maliit) appears for a text field in tablet mode | | |
| 11 | The event dispatcher is GLib-based (`g_idle_add` fires) | | |

Default: choose **Qt Quick**, unless it clearly loses on criteria 1–7 or 9.

## Decision
_TBD after the spike._
