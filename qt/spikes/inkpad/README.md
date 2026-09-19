# M0 spike: inkpad (canvas host evaluation)

This throwaway app checks what Qt 6.7.2 on Plasma Wayland delivers from the pen and the touchscreen. It runs the same canvas logic under two hosts:
- **quick**: a `QQuickItem` with GPU texture tiles and an event filter on the window;
- **widget**: a raster `QWidget`.

The results decide [ADR-0001](../../docs/adr/0001-ui-host.md).

## Build and run
```sh
cmake -S qt -B build-qt -G Ninja && cmake --build build-qt
cd build-qt/spikes/inkpad
./xqt-inkpad --host quick     # writes inkpad-quick-<date>.jsonl in the current directory
./xqt-inkpad --host widget    # writes inkpad-widget-<date>.jsonl
QT_QPA_PLATFORM=xcb ./xqt-inkpad --host quick   # optional: XWayland comparison
```
The header shows live values: device, pointer type, pressure, tilt, buttons, event rate, proximity, palm-rejection counters, and latency.

## Test script (about 3 minutes per host, in tablet mode)
1. **Write** a few lines of text, slowly and fast. Check the pressure variation and look for gaps at speed.
2. **Hover** the pen above the screen without touching. Does the square dot follow the pen?
3. **Eraser end** (if your pen has one): rub across strokes. Does the dot turn red and do strokes disappear?
4. **Barrel buttons**: hold each button while touching down and dragging. It should pan. Watch `btns=` in the header.
5. **Rest your palm** on the screen while writing, and while hovering. The page must not move; `palm-rejected` increases.
6. **One finger**: drag, then flick. The page should keep gliding and slow down smoothly.
7. **Two fingers**: pinch-zoom. The point under your fingers should stay put. After about 300 ms the page re-renders sharp.
8. **Taps**: a quick two-finger tap undoes the last stroke; a three-finger tap redoes it.
9. **Touchpad** (laptop mode): two-finger scroll, then pinch.
10. Tap the **text field**. Does the on-screen keyboard appear (tablet mode)?
11. The **pen on the buttons** (Undo/Fit) should click them normally.
12. Compare how the ink trails the pen tip with upstream Xournal++ (`../xournalpp/build/xournalpp`). A 240 fps phone slow-motion video is the best measurement.

Then summarize the logs with:
```sh
python3 ../../../qt/spikes/inkpad/analyze_log.py inkpad-*.jsonl
```
