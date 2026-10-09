# On-device checklist

What only a real device, a real screen or another app can show. Everything else is covered by the tests that run
off-screen ([README.md](README.md)). Walk through it before a release, and after a change to input, rendering,
scaling, windows or a platform build.

This page is the short list to walk. The checks of one feature are in that feature's doc, in its section "On the
device", and this list links to them (last section). A new check: in the feature doc when it belongs to one feature,
here when it is about the device, the platform or the app as a whole; one line each, in the area it belongs to.

The per-block checklist of before 2026-10-07 (1546 checks, one section per block, most of them covered by tests
since) is in git: `git show 5c6402d:qt/docs/testing/device-checklist.md`.

```sh
./build-release/xournal-qt                     # or the package of the release
./build-release/xournal-qt ~/some/notes.xopp   # a .xoj, .pdf or .md opens the same way
```

## Pen

- [ ] Writing: pressure visible, looks like Xournal++, no gaps when writing fast, the ink follows the tip without lag
      (also on a canvas turned by 90° and by a free angle).
- [ ] Highlighter translucent and multiplied with what is under it.
- [ ] The pen's side buttons and its eraser end erase while held; the pen draws again afterwards.
- [ ] A stroke from one page into the next continues on the next page.
- [ ] The pointer over the page, pen gestures, the setsquare and the compass: [hover-cursors.md](../features/hover-cursors.md#on-the-device),
      [pen-gestures.md](../features/pen-gestures.md#on-the-device), [toolbox.md](../features/toolbox.md#on-the-device).

## Touch and palm rejection

- [ ] One finger pans, a flick has momentum; two fingers pinch-zoom anchored under the fingers, sharp again ~0.3 s
      after they stop; two-finger tap undoes, three-finger tap redoes.
- [ ] A palm on the screen while writing or hovering never moves the page; the pen touching down while a finger pans
      stops the pan and draws; right after writing, pinch zoom works at once.
- [ ] Two taps zoom on what was tapped (and back); a tap on a PDF link follows it and never zooms.
- [ ] Four and five fingers: all pages / all open documents, and back.
- [ ] The touchpad: two-finger scroll and pinch go the way they go on the screen, also with the canvas turned.
- [ ] Draw with the finger (the finger button): one finger writes, two fingers scroll and zoom.
- [ ] A finger or the pen held still for half a second offers what can be done there, a quick tap does not; the
      wheel and a touchpad fling come to rest on a page when snapping (the canvas's timing goes by its own clock).

## Screens and scaling

- [ ] Fractional scales, screens of different scales, the calibration: [hidpi.md](../features/hidpi.md#on-the-device).
- [ ] Presenting with a projector: [presenter-view.md](../features/presenter-view.md#on-the-device); Zen's dot on a
      projector: [zen.md](../features/zen.md#on-the-device).

## The app's windows

- [ ] Drag a tab off the strip into a window of its own, quit from the main window: no crash, the other window goes
      with it; again with unsaved changes in the second window (they come back as recovered tabs next start).
- [ ] Killed with an unsaved document: the next start offers to recover it.
- [ ] Quitting while pictures are still being drawn (the library grid's covers right after opening a big library,
      the page sidebar of a long PDF, the library search's pages) closes at once and leaves no crash report.

## Linux

- [ ] The `.deb` (neon and Debian 13) and the AppImage install and start; the launcher's right-click "Quick note"
      ([quick-note.md](../features/quick-note.md#on-the-device)).
- [ ] Opening a file from the file manager while the app runs opens it as a tab of the running window.
- [ ] Printing on a real printer: page range, copies, landscape pages turned onto the sheet.
- [ ] Recording and playing ([audio.md](../features/audio.md#on-the-device)).

## Windows (the portable zip, Surface)

- [ ] `bin\xournal-qt.exe` starts without a console window (SmartScreen: "More info" → "Run anyway").
- [ ] Pen pressure; if not: `xournal-qt-debug.bat` writes `input-log.txt`.
- [ ] Text with ä ö ü ß; folders and documents with umlauts in their names open and save.
- [ ] "Show in file manager" selects the file in Explorer; "Open with the system app" opens the right program.
- [ ] Recording ([audio.md](../features/audio.md#on-the-device)).
- [ ] Writing into a PDF that another program holds open.

## macOS (the `.dmg`, Apple Silicon)

- [ ] The `.dmg` opens; the app starts after "Open Anyway" (unsigned) and takes nothing from Homebrew.
- [ ] Pen (a Wacom tablet), trackpad pinch and scroll.
- [ ] The first recording ([audio.md](../features/audio.md#on-the-device)).

## Android (the APK, Galaxy Fold 7)

- [ ] Install over the previous version (`adb install -r`): the libraries stay; "All files access" and the move of
      the libraries to `Documents/Xournal_Libraries` work and keep file times.
- [ ] "Open with" and the share sheet for a PDF, a `.xopp`, a `.md` and photos: each opens, a copy lands in "Opened".
- [ ] Import files and folders through Android's picker; a library in a folder synced by another app (Syncthing).
- [ ] Folded and unfolded, upright and sideways; the soft keyboard: [adaptive-layout.md](../features/adaptive-layout.md#on-the-device).
- [ ] Back and the back gesture: [zen.md](../features/zen.md#on-the-device).
- [ ] Recording with the screen off ([audio.md](../features/audio.md#on-the-device)); the launcher's "Quick note"
      ([quick-note.md](../features/quick-note.md#on-the-device)); "Add to calendar"
      ([todos.md](../features/todos.md#on-the-device)).
- [ ] Plugins: "Plot a function…" as a bottom sheet, the preview's frame dragged with a finger and the pen, Insert;
      after an update of the app a changed bundled plugin is the new one ([plugins.md](../features/plugins.md#on-the-device)).
- [ ] `adb logcat --pid=$(adb shell pidof org.xournalqt.app)` shows no crash.

## Other apps

- [ ] A `.xopp` saved here opens in Xournal++ 1.2/1.3 and looks the same (with groups, creation times and
      recordings; a plot of the function plotter as strokes and its labels' `$…$` source); Xournal++'s files open here.
- [ ] PDFs with notes, archive PDFs, version history and encrypted PDFs in other viewers:
      [hybrid-pdf.md](../features/hybrid-pdf.md#on-the-device).
- [ ] Other apps' annotations made editable: [adopt-annotations.md](../features/adopt-annotations.md#on-the-device);
      tags as PDF keywords: [tags.md](../features/tags.md#on-the-device); the Markdown inside a PDF text document:
      [md-pdf.md](../features/md-pdf.md#on-the-device).

## The checks in the feature docs

Each link is that doc's section "On the device".

| Area | Docs |
| --- | --- |
| Pen and tools | [hover-cursors](../features/hover-cursors.md#on-the-device), [pen-gestures](../features/pen-gestures.md#on-the-device), [toolbox](../features/toolbox.md#on-the-device) |
| Screens and the window | [hidpi](../features/hidpi.md#on-the-device), [adaptive-layout](../features/adaptive-layout.md#on-the-device), [zen](../features/zen.md#on-the-device), [presenter-view](../features/presenter-view.md#on-the-device), [reference-view](../features/reference-view.md#on-the-device) |
| Documents and files | [hybrid-pdf](../features/hybrid-pdf.md#on-the-device), [library](../features/library.md#on-the-device), [adopt-annotations](../features/adopt-annotations.md#on-the-device), [tags](../features/tags.md#on-the-device), [todos](../features/todos.md#on-the-device), [quick-note](../features/quick-note.md#on-the-device) |
| Markdown | [markdown-boxes](../features/markdown-boxes.md#on-the-device), [md-pdf](../features/md-pdf.md#on-the-device) |
| Search and sound | [handwriting-search](../features/handwriting-search.md#on-the-device) (with the real models), [audio](../features/audio.md#on-the-device) |
