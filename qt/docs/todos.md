# To-dos (`qt/todos`)

Decided by the author (2026-10-04): a library-wide list of to-dos from the Markdown task lines of the documents,
grouped and filtered; a check-box stamp for handwritten to-dos; "Add to calendar" as a one-way hand-off. No scripting.
In the author's words: *"Add a way to sort todos by document source or somehow group them and filter them. … Calendar
is cool. Maybe add the setting to only query checkboxes with todo: in the markdown line or something like that to not
flood the todo list with all per default."*

## What a to-do is

A task line of Markdown, anywhere the app has Markdown: a Markdown box of a page, a sticky note's text, a page of a PDF
text document, a `.md` file.

```markdown
- [ ] todo: call the lab 📅 2026-10-12
* [x] todo: send the slides
1. [ ] TODO: book the room due:2026-10-20
```

- Bullets (`-`, `*`, `+`) and numbers (`1.`, `1)`), nested ones too. They are found by the parser (md4c's task lists,
  as the boxes draw them), so an example in a code block is none (`qt/src/markdown/MdTasks.*`).
- **Which lines are to-dos** is a setting (Settings → To-dos → "Collect to-dos from"):
  - **Lines marked as to-dos** (the default): the line has the marker anywhere, upper or lower case. The marker is
    `todo:` unless set otherwise (Settings → To-dos → Marker). The list does not show it. An empty marker lists every
    check box.
  - **Every check box**.
  - A **check-box stamp** (below) always counts.
- **Due date**: Obsidian Tasks' `📅 2026-10-12` (also with the emoji's variation selector), or `due:2026-10-12` /
  `due: 2026-10-12` as an ASCII alternative (`due` as a word of its own: "overdue:2026-…" is none). An impossible date
  (month 13) is none. The list does not show the token; it shows "Today", "Tomorrow", a weekday this week, else the
  date.

## The index

Every task line is read into the library index (`LibraryIndex::todos()`, `todoChanges()`), into each folder's small
"notes" pack beside the bookmarks: document, page (‑1 for a `.md`, which has no pages in the index), its box on the
page (the order of `md::boxesOf`: the Markdown layer's boxes, then the sticky notes' texts), the line in the box,
the text after the check box as written, done or open, the due date, whether it is a stamp, and where its box is
(for a stamp: where its check box is drawn). All task lines are stored, whatever the setting, so changing the
setting needs no new read.

A to-do is found again in its document by its **text and occurrence** (how many task lines of the document before
it have the same text): page and box numbers change when pages and boxes move, the text rarely does. If it is not
there any more, the app says so and the list is made again.

## The To-dos view

The library home's switch: Library, Recent, Favourites, Bookmarks, **To-dos** (`TodosView.qml`,
`LibraryTodosModel`). It follows the library's "Show" filter and Favourites star, as the Bookmarks view does.

- **Grouped** by document (the default: its name, its folder, the count), by folder (the library's top is the
  library's name), or not at all.
- **Sorted** by due date (the soonest first, those without one after them), by document (by name, then in the order
  of the document), or by when the document was last changed. Done ones come after the open ones; groups come in the
  order of their first to-do.
- **Filters**: Open / Done / Open and done; Any due date / Overdue (open ones before today) / Due today / Due this
  week (today until the last day of the locale's week) / No due date; a text (the to-do's text, its document's name or
  folder); "Only in <folder>": the library's current folder and its subfolders.
- On a phone the filters are one row that scrolls sideways, the rows are 52 px high.
- A **tap** opens the document at the to-do's page with its line in view (a `.md`: the page its line is laid out
  on). Press and hold / right-click: Open at this line, Mark as done / open, Add to calendar.
- The **check box** ticks the to-do in its document:
  - **open in a tab** (any window): changed there, one undo step, as a tap on the box's check box; a document without
    other changes is saved at once (the list reads the file), one with unsaved changes stays unsaved (the list shows
    the new state until the index has it);
  - **not open**: a `.md` through its text (byte for byte elsewhere: a BOM and CRLF stay); a `.xopp` or a PDF with
    notes (also a PDF text document) loaded in the background, changed and saved the way the app saves (a PDF with
    notes: an incremental update), without opening a tab;
  - **refused with a message**: a read-only file, an archive PDF ("open it to tick the to-do"), an old `.xoj`, a file
    another app changed (our annotations of a PDF with notes edited elsewhere) or that could not be read whole.

## Handwritten to-dos: the check-box stamp

Image button, press and hold (right-click) → "Check box for a handwritten to-do". The hand tool is taken meanwhile
(the tap writes nothing); the next tap on a page puts a tiny Markdown box there whose text is `- [ ] ` (no text) with
its check box under the tap, then the tool before (the pen) comes back to write the to-do beside it. Escape, or
another tool, ends it without a stamp. Placing it is an undo step (two when the page gets its Markdown layer).

- It is an ordinary Markdown task: a tap on its check box ticks it; Xournal++ shows `- [ ]` as text.
- "Is a stamp" is not stored in the file: a box whose whole text is one task without text is one
  (`md::tasks::isStamp`). So a stamp a user typed by hand is one too, and it counts whatever the marker setting.
- In the list it shows **the handwriting beside it** as a picture: the region renderer of `qt/snip`
  (`qt/src/render/RegionRender`) draws the area from its check box to the page's right side (96 % of its width),
  4.5 check boxes high, through `image://hitpage/…/<page>/area/x,y,w,h` (the hit-page provider's kept documents).
- When handwriting search has read that line (`qt/docs/handwriting-search.md`), its words are the to-do's text
  (filterable, exported); else the picture alone.

## Calendar (one way)

"Add to calendar" on a to-do with a due date:

- **Android**: the calendar app's "new event" screen, filled in (`ACTION_INSERT` on `CalendarContract.Events`: all
  day, the text, "To-do in <document>, page N" and the file link); the user saves it there.
- **Elsewhere** (or when no Android app takes the intent): an `.ics` with one all-day `VEVENT` in the app's cache
  (`<cache>/calendar/`, files older than a day removed, at most 20 kept) opened with the system's app for `.ics`
  (`SystemApps::openWithSystemApp`). If none opens it, the message gives its path.

Why `VEVENT` and not `VTODO`: Google Calendar, Outlook and most phone calendars import events from an `.ics` but
ignore `VTODO`; only Apple Reminders, Thunderbird and some task apps read those. The event is transparent (not
"busy"), has a stable `UID` per to-do (apps that go by it replace an earlier import) and a `URL` back to the page
(`file:///…/name.xopp#page=N`).

The view's ⋮ menu: **Export open to-dos** of the list (its filters apply; done ones never) as an `.ics` (those with a
due date) or as Markdown (`# To-dos of <library>`, a heading per document, `- [ ] text 📅 date ([page N](link))`).

Nothing comes back: no sync, no reminders, no account access (decided by the author).

## Files

| What | Where |
| --- | --- |
| Task lines, due dates, stamps | `qt/src/markdown/MdTasks.*` |
| The index | `qt/src/shell/Library.*` (`Todo`, `todos()`, `todoChanges()`) |
| The setting, finding a to-do again, writing a closed `.md` | `qt/src/shell/Todos.*` |
| The view's model | `qt/src/shell/LibraryTodos.*` |
| Ticking, opening, the stamp, the calendar (window side) | `qt/src/app/AppTodos.cpp` |
| The stamp on the canvas | `qt/src/canvas/TodoStamp.*` (`CanvasView::addTodoStamp`, its tap in `tapAt`) |
| `.ics` and Markdown | `qt/src/shell/TodoCalendar.*`; Android: `XournalActivity.insertCalendarEvent` |
| The view | `qt/src/app/qml/TodosView.qml` (in `HomeView.qml`) |
| Tests | `qt/tests/shell/TodosTest.cpp`, `qt/tests/ui/TodosTest.cpp` |

## Open

- The calendar's Android intent and the `.ics` hand-off on Android (a file in the app's cache: needs Qt's file
  provider) are untested on a device.
- A stamp's text from handwriting search needs the recogniser; without it the picture alone.
- Pages of a PDF text document: a to-do toggled in a closed one is saved into the PDF; its carried `name.md` follows
  with the save.
