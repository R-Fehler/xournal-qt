# Getting started: the introduction and the tutorial (`qt/onboarding`)

Two things help a new user find their way (the author, 2026-10-04: "make getting started easier"):

1. **The introduction**: a few short pages at the first start that say what the app is for and end in the question
   how documents are kept.
2. **The tutorial**: a document that asks the user to try the tools, modes, search and menus one by one, on a copy
   they can write on.

Both are reachable again from **Help**: ⋮ → Help in a document, and Settings → Help (the home screen has no ⋮; its
Settings button leads there).

## The introduction

`qt/src/app/qml/IntroDialog.qml`, an `AdaptiveDialog` (a dialog in the middle of a desktop or tablet window, the whole
screen on a phone, qt/docs/adaptive-layout.md).

| Page | Title | What it says |
| --- | --- | --- |
| 1 | Write on notes and PDFs | pen, highlighter, eraser; **PDFs are not read-only here** (write on them, mark their text, add pages and room for notes, move and delete pages); everything stays editable; pen writes, finger scrolls; **go back to earlier versions** (a PDF with notes keeps its versions when turned on with the sidebar's History button; Ctrl+Alt+S for a milestone; qt/pdf-history) |
| 2 | Markdown: text that stays text | Markdown shown formatted while typing; `.md` files as documents; **a text inside a PDF with notes** (write on it with the pen, keep editing the text; ⋮ → Document → Open as PDF document) |
| 3 | Your folders are your library | any folder is a library, nothing imported; search on three levels, Fuzzy; tabs and the reference |
| 4 | How do you want to keep your documents? | the two cards of `DocumentModeCards` (PDF files / Xournal++ files), as the first-start question and Settings → Documents show them |

- Next and Back step through the pages; they can be swiped too (a `SwipeView`); dots show where one is. The pages
  share one height (the highest), so the dialog does not jump.
- **At the first start** (`app.askIntro()`: no way to keep documents chosen yet, `XQT_DOCUMENT_MODE` not set, and the
  introduction not shown yet) it replaces the old question (`DocumentModeDialog`). Skip, on every page but the last,
  goes to the last page, because the choice has to be made there; Esc does nothing, and × of the phone's full-screen
  sheet goes to the last page too. Continue stores the card chosen (the recommendation, PDF files, to start with)
  and the window goes on as before (the recovery question, then on Android where the libraries are kept).
- **From Help** it starts at the first page, with the way chosen now on the last page. Skip, Esc and × close it;
  Done stores another way only when another card was tapped.
- **Once**: the setting `introSeen` (xournalQt part of `settings.xml`, `app.introSeen`) is set when it is finished
  or closed. An install that chose its way before the introduction existed does not see it (the way is stored, so
  the first-start question is not due); it is in Help. When the introduction was seen but no way was chosen (it
  cannot happen through the dialog, only through an edited settings file), the old question comes alone.
- Tests: `FirstStartTest.theIntroductionComesFirstAndEndsInTheChoice` (shown on a fresh config; Skip, Back, Next;
  Esc does not close it; the last page sets the mode; the second start shows nothing),
  `FirstStartTest.theIntroductionTakesTheRecommendation`, `MainWindowTest.theIntroductionIsInHelp` (⋮ → Help and
  Settings → Help; Skip and Esc close it; Done changes the mode), `ModeQuestionTest.*` (the question alone), and the
  introduction is one of the dialogs of `AdaptiveLayoutTest.dialogsFitTheWindow` (five window sizes).

The texts are the dialog's `qsTr` strings. Only what the app does is described: change a page when a feature
changes.

## The tutorial

`qt/resources/help/tutorial.md` (its pictures in `qt/resources/help/tutorial.assets/`), compiled into the program
as Qt resources (`:/xqt-help/…`, `qt/cmake/XqtApp.cmake`). Help → **Tutorial** opens it (`AppController::openTutorial`,
`qt/src/app/AppHelp.cpp`):

- **Where the copy lives.** In the app's data folder: `<AppDataLocation>/Tutorial/Tutorial.pdf`
  (Linux: `~/.local/share/xournal-qt/…`, Windows: `%APPDATA%\…`, Android: the app's own storage). Not in a library:
  the user's folders stay clean (VISION.md), and nothing new appears in their library or sync. It is in Recent
  once opened.
- **What the copy is.** A **PDF text document** (qt/docs/md-pdf.md) made from the Markdown, as "Open as PDF document"
  makes one: the text is typeset on pages, the pen writes ink on top, the keyboard keeps editing the text, and the
  formatting bar is there. So every exercise (pen, highlighter, select, pages, Markdown) works on the tutorial itself,
  in both ways of keeping documents. The pictures the text links to are copied out of the resources into the
  document's work folder first and packed into the PDF by its first save.
- **The next time** Help → Tutorial opens the same copy, with what was written on it (or switches to its tab).
- **Start the tutorial again** (Help, shown once a copy exists) asks first, closes its tab, deletes the copy and
  makes a fresh one.
- **Keeping it in a library** is up to the user: ⋮ → Save as… (a PDF with notes or a `.xopp`) into any folder. The
  tutorial's first page says so.
- Each section ends with a page break (`<div style="page-break-after: always"></div>`), so each exercise starts on a
  page of its own with room below it to write. Handwriting stays where it was drawn when text above it changes (as in
  every text document); the page breaks keep that to one page.
- Section 15, **Versions** (qt/pdf-history): turn on "Keep versions of this document" in the sidebar's History
  panel on the tutorial itself (it is a PDF with notes), save a milestone with Ctrl+Alt+S, show, restore or copy a
  version; the setting for new PDFs and Share without the versions.
- Test: `MainWindowTest.theTutorialOpensAsACopy` (the resource is there and is Markdown with sections and
  placeholders; Help → Tutorial makes and opens the copy in the app data folder as a text document; a second time
  opens the same file; "start again" makes a fresh one). The tests set `XDG_DATA_HOME` to a temporary folder
  (`qt/tests/ui/main.cpp`).

### Filling the placeholders (for the author)

Every spot that needs the author's ink or a screenshot is a quote whose first word is **PLACEHOLDER**, so it is
visible in the app, on GitHub and in any Markdown viewer, and `grep -n PLACEHOLDER qt/resources/help/tutorial.md`
lists them all:

```markdown
> **PLACEHOLDER · SCREENSHOT:** the ⋮ menu open, with Save as…, Share… and Export.

> **PLACEHOLDER · INK:** a handwritten "try me" arrow pointing at the empty space below.
```

**Screenshots** (and any picture):

1. Take the screenshot with the system's tool (the same window size for all of them looks calmer; a window about
   1280 px wide keeps the tool bar in one row).
2. Save it as `qt/resources/help/tutorial.assets/<name>.png` (short lowercase names, e.g. `more-menu.png`; PNG or
   JPEG; keep it under about 300 KB, it is compiled into the program).
3. Replace the placeholder quote with `![The ⋮ menu with Save as, Share and Export](tutorial.assets/more-menu.png)`.
   Every file in `tutorial.assets/` is picked up by the next CMake run (a glob with `CONFIGURE_DEPENDS`).
4. Rebuild and open Help → Start the tutorial again: the picture is in the copy (and inside its PDF once saved).

Pictures can also be added in the app: in an opened copy of the tutorial, paste or drop a picture into the text
(it lands in `Tutorial.assets/…` inside the PDF); then ⋮ → Export → Export as Markdown writes `Tutorial.md` and its
`Tutorial.assets/` folder, whose pictures go into `tutorial.assets/` here (rename the folder in the links).

**Ink** — two ways:

- **As pictures** (the text stays the master): write the ink on a blank page in the app, select it, and take a
  screenshot of it (or draw it in any app), then add it as a picture as above. Good for arrows and short notes
  that sit between paragraphs; it keeps working when the text changes.
- **As real ink on the finished tutorial** (the PDF becomes the master): open Help → Tutorial, write the ink on the
  pages where the INK placeholders are, remove the placeholder quotes by editing the text (text box tool, tap into
  the quote), save, and copy the saved `Tutorial.pdf` from the app data folder to
  `qt/resources/help/tutorial.pdf`. When that file is in the resources, Help → Tutorial copies it instead of making
  the copy from `tutorial.md` (`AppHelp.cpp`). Later text changes are then made in the app, on that PDF (its
  Markdown stays editable and travels inside it; ⋮ → Export → Export as Markdown brings `tutorial.md` up to date for
  review). Ink stays where it was drawn when text above it changes, so finish the text first.

The resource list is generated from the folder: `tutorial.md`, an optional `tutorial.pdf` and everything in
`tutorial.assets/`.

## Not done

- No hint for installs that chose their way before the introduction existed (they find it in Help).
- The tutorial is in English only, as the rest of the app.
- No screenshots or ink yet: the placeholders above.
