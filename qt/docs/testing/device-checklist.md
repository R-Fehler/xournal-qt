# On-device checklist

What only a real device, a real screen or another app can show. Everything else is covered by the tests that run
off-screen ([README.md](README.md)). Walk through it before a release, and after a change to input, rendering,
scaling, windows or a platform build. Add a check here only when no off-screen test can make it, in the area it
belongs to, one line each; the feature's behaviour itself belongs in its feature doc.

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
- [ ] Hover: the pointer (dot or crosshair, Settings → Pen) follows the hovering pen as fast as the mouse arrow; over
      the bars the arrow; Android's S Pen shows the drawn dot under the tip. If no pointer shows: `XQT_PEN_CURSOR=0`.
- [ ] A stroke from one page into the next continues on the next page.
- [ ] Hold to straighten and scratch out (Settings → Pen) trigger with real handwriting, and never while writing
      normally (thresholds).
- [ ] The setsquare and the compass at 15 cm on the Surface at 200 % move and turn smoothly.

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

- [ ] At 125 %, 150 % and 175 % (Plasma Wayland and X11, GNOME, Windows): pages, thumbnails, the page grid, the
      selection's frame and knobs are as sharp as at 100 %; the UI's lines are even; Settings → Display's
      calibration page names the scale the app got.
- [ ] A window moved between screens of different scales draws its pages and selection anew for the new screen.
- [ ] Screen calibration: 1 cm on the page is 1 cm on the ruler at 100 % zoom.
- [ ] Presenting (F5) with a projector: the console on the laptop, the slide alone on the projector (also when the
      window was on the projector); a clicker turns pages; writing on the console shows on the projector at once;
      the audience follows the presenter's zoom smoothly; unplugging the projector ends the second screen cleanly.
- [ ] Zen's dot is faint but findable on a projector and in sunlight.

## Linux

- [ ] The `.deb` (neon and Debian 13) and the AppImage install and start; the launcher's right-click "Quick note".
- [ ] Opening a file from the file manager while the app runs opens it as a tab of the running window.
- [ ] Printing on a real printer: page range, copies, landscape pages turned onto the sheet.
- [ ] Recording with the built-in microphone and a headset; playing from a stroke at the right speed.
- [ ] Killed with an unsaved document: the next start offers to recover it.
- [ ] Quitting while pictures are still being drawn (the library grid's covers right after opening a big library,
  the page sidebar of a long PDF, the library search's pages) closes at once and leaves no crash report.

## Windows (the portable zip, Surface)

- [ ] `bin\xournal-qt.exe` starts without a console window (SmartScreen: "More info" → "Run anyway").
- [ ] Pen pressure; if not: `xournal-qt-debug.bat` writes `input-log.txt`.
- [ ] Text with ä ö ü ß; folders and documents with umlauts in their names open and save.
- [ ] "Show in file manager" selects the file in Explorer; "Open with the system app" opens the right program.
- [ ] Recording (`bin\xournal-qt.exe --audio-info` lists the microphones); with the microphone blocked in Windows'
      privacy settings the app says so.
- [ ] Writing into a PDF that another program holds open.

## macOS (the `.dmg`, Apple Silicon)

- [ ] The `.dmg` opens; the app starts after "Open Anyway" (unsigned) and takes nothing from Homebrew.
- [ ] Pen (a Wacom tablet), trackpad pinch and scroll.
- [ ] The first recording asks for the microphone with the app's text; refused, the app says where to allow it.

## Android (the APK, Galaxy Fold 7)

- [ ] Install over the previous version (`adb install -r`): the libraries stay; "All files access" and the move of
      the libraries to `Documents/Xournal_Libraries` work and keep file times.
- [ ] "Open with" and the share sheet for a PDF, a `.xopp`, a `.md` and photos: each opens, a copy lands in "Opened".
- [ ] Import files and folders through Android's picker; a library in a folder synced by another app (Syncthing).
- [ ] Folded (412 × 915) and unfolded (900 × 1000), upright and sideways: nothing to tap under the status bar, the
      cut-out or the gesture bar; the rail and the top bar use their room and scroll to their ends (TODO.md: the top
      bar's blank end).
- [ ] The soft keyboard: dialogs and the format bar move above it; the text cursor stays in view.
- [ ] Back leaves Zen first, then closes what is open; the back gesture over the replay's slider.
- [ ] Recording with the screen off: the notification with its clock, Pause/Resume and Stop.
- [ ] The launcher's "Quick note" shortcut; "Add to calendar" for a to-do opens the calendar's new event.
- [ ] `adb logcat --pid=$(adb shell pidof org.xournalqt.app)` shows no crash.

## Other apps

- [ ] A PDF with notes (and an archive PDF) shows its ink in Acrobat, Preview, Xodo, Drawboard, Chrome/pdf.js,
      Firefox, Okular and Evince; after a save in one of them, the app still opens the notes (or says what changed).
- [ ] A PDF with version history opens in other viewers as its latest version.
- [ ] An encrypted PDF made here opens with its password in Acrobat, Preview and pdf.js; its restrictions hold.
- [ ] Annotations exported by GoodNotes, Drawboard and Preview become editable here as the standard types promise.
- [ ] Tags written as PDF keywords show in Zotero and Acrobat.
- [ ] A `.xopp` saved here opens in Xournal++ 1.2/1.3 and looks the same (with groups, creation times and
      recordings); Xournal++'s files open here.
- [ ] The Markdown inside a PDF text document can be taken out with a PDF viewer's attachments
      ([user guide](../user/markdown-from-pdf.md)).

## Handwriting search, with the real models

- [ ] The models download once with consent; the author's own notes in English and German are found by words
      written in them, also in the library search and in other PDF viewers (the invisible text layer).

## No compatibility with earlier pre-releases; Markdown only (0.9.0)

- [ ] Start with the settings and a library of 0.8.0: the app starts; the library reads its documents once (the
      progress shows), the second start reads nothing; tags, to-dos and bookmarks are there after that first read.
- [ ] The text tool on an empty place makes a Markdown text box; on a text box of a Xournal++ file it edits that text
      as it is, and the file opens in Xournal++ afterwards with the edited text.
- [ ] A fresh profile: snapping off, the pen's side buttons erase; switch snapping on, restart: it stays on.

## The main window in parts (`Main.qml` split, 2026-10)

- [ ] On the Fold (folded and unfolded) and a desktop: the Zen dot and its pill, the view pill, the full-screen tab
      dots, the toasts and the read-only note sit where they did, over the page and under the home screen.
- [ ] ⋮, the toolbox's menus and the catalog open at their buttons (a sheet on the phone); Back closes a sheet, then
      leaves Zen, as before.

## Saving and tags (`qt/session-io`)

- [ ] A PDF with notes that keeps its versions, open in a tab: give it a tag (card menu → Tags…) and press Ctrl+S at
      once: both are in the file; the version list shows no "other app" entry; "Save with a message" works.
- [ ] A `.xopp` made from a big PDF with "attach the PDF": Save as into another folder while writing with the pen:
      the pen keeps drawing; the `name.xopp.bg.pdf` next to it opens, also in Xournal++.
