# Quick note

One action makes a note to write on at once, as Apple's Quick Note, OneNote's quick notes and Obsidian's daily notes
do (idea A11 of [the ideas of 2026-10](../history/README.md)).

## What it makes

Settings → Documents → **Quick note** (`quickNote` in the `xournalQt` part of the settings):

- **A new note** (`note`, the default): a new document in the library's folder `Inbox/`, named by the date and time,
  `2026-10-04 21-30.xopp`, or `2026-10-04 21-30.pdf` (a PDF with notes) when documents are kept as PDF files
  (Settings → Documents → Keep documents as; the same rule as New document). A second note in the same minute is
  `2026-10-04 21-30 (2)`. It is saved at once, opens in a new tab, and the pen is taken (the toolbox's pen used last;
  a pen in hand stays).
- **A line in today's Markdown note** (`daily`): `- 21:30 ` is added at the end of `Inbox/2026-10-04.md` (after a line
  break when the file does not end with one), which opens with the cursor after the time. When that note is open in a
  tab already, the line goes into the open text (one undo step; its unsaved changes stay); otherwise into the file
  on disk, so it is there however the app ends.

`Inbox/` has a fixed English name (as `Stickers/`), at the top of the library, and is made on first use. Names by
date sort in order. Without a library (an off-screen run, or none chosen yet on Android) a quick note is a new
document, not saved yet.

## Where

| Where | How |
| --- | --- |
| Home screen | a button beside New where the header has room for every button (`quickNoteButton`); else the first entry of "+" (`quickNoteItem` in `newMenu`), also the phone's floating "+" |
| A document | ⋮ → Document → Quick note (`documentQuickNoteItem` in `moreDocumentMenu`), in every chrome (classic, toolbox, phone). Not at the top of ⋮ (it keeps to 10 entries there), not in the toolbox or the command bar's buttons. |
| Keys | **Ctrl+Alt+N** (`quickNote` in `ShortcutsModel`, listed in the shortcut sheet, changeable in Settings) |
| Command line | `xournal-qt --quick-note` (with files: they open first). With the app running, the request goes to it through `SingleInstance` as the entry `--quick-note` after the files (never an absolute path), and the window comes to the front. |
| Linux desktop | the right-click menu of the app's launcher or task-manager entry: the `.desktop` file's action `QuickNote` runs `xournal-qt --quick-note` |
| Android | a launcher shortcut (long press on the icon → Quick note; `res/xml/shortcuts.xml`). Its intent (`org.xournalqt.app.QUICK_NOTE`) reaches the native side with the incoming files as the entry `xournal-qt:quick-note`, at start and while the app runs. |

## Code

- `AppController::quickNote()` / `quickNoteAt(when)` (`qt/src/app/AppQuickNote.cpp`); `createDocumentAt(path)` is the
  part of `createDocument` that saves a new document as a given file.
- `SingleInstance::QUICK_NOTE`, `quickNoteRequested()`; `main.cpp` adds `--quick-note`.
- Android: `XournalActivity.ACTION_QUICK_NOTE` / `QUICK_NOTE`, `xqt::android::QUICK_NOTE`.
- Tests: `qt/tests/shell/QuickNoteTest.cpp` (names, formats, the daily file, the hand-over through `SingleInstance`),
  `qt/tests/ui/QuickNoteTest.cpp` (the home button and "+", Ctrl+Alt+N, ⋮, the daily note's cursor).
