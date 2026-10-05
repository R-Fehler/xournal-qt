# The document timeline (`qt/timeline`)

The author (2026-10-04): "a document timeline, which is like an audio playbar at the bottom, which replays the audio
and replays the document editing history in read-only mode." Decided (2026-10-05, TODO.md): levels 1 and 2 of
[idea B9](ideas-2026-10.md) as one design, on one clock: every element knows when it was made, recordings are tracks
placed by their start. Not in this block: erasing, moving and page changes (level 3, with the version history); times
per point.

## Creation times

`qt/src/session/ElementTimes.*`; the seam in `Element.h` and the `.xopp` reader and writer
([ADR 0002](adr/0002-upstream-seams.md)).

- Every element the user makes gets the time it was made: **milliseconds since 1970-01-01 UTC**, the element
  attribute `xqt-created="1791100800000"` on stroke, text, image, teximage and link, written only when known
  (`SaveHandler::visitLayer`, so every save path has it: autosave, recovery, sticker and template files, the `.xopp`
  inside a PDF with notes). Absolute and UTC so that elements copied between documents, or written on two devices,
  still sort right, and summer time changes nothing. Milliseconds because several strokes are written in a second
  and a recording's `ts` is in milliseconds too; a number, not a date text, because it is the smallest to write and
  to read.
- **Where it is set** (fork code only, upstream's tools are unchanged): a stroke when the pen touches (pen,
  highlighter, every shape, whiteout; `CanvasPage`), a text when its box opens (`TextEditor`; as upstream stamps
  `ts`), a Markdown text box when it is begun (`MarkdownSession`), the page's own text when it is begun (it is laid
  out anew on every change and keeps that time, `TextFlow`), an inserted image or snip, marks over PDF text, marks of
  the geometry tools, link markers, chapters, to-do stamps, sticky notes (`StickyNotes::place`), the elements of a
  page made from a template.
- **New, not copied**: pasted elements and stickers get the time they were pasted (upstream's clipboard data has no
  time; `Element::serialize` is unchanged, so Xournal++ pastes ours and we paste its). Cut and paste is a paste.
- **Kept**: moving, resizing, rotating, recolouring, editing a text (the edited text is a copy of the old one), the
  pieces the eraser leaves and the shape a stroke becomes (`Stroke::applyStyleFrom`), undo and redo, duplicated and
  pasted pages (page changes are level 3).
- **None** (0): elements of files written before, or by Xournal++, and of text files shown as pages. They stay so.
- Xournal++ looks attributes up by name and ignores this one (no message, `ElementTimesTest`); when it saves the file
  the times are gone, the elements stay.

**What it costs**: 28 bytes per element uncompressed (` xqt-created="1791100800000"`), **about 5 bytes per stroke in
the gzip-compressed `.xopp`** (measured by `ElementTimesTest.theirCostInTheFile`: 2000 strokes made 0.2 to 3 s
apart; a short stroke of 11 points is about 43 bytes there, a typical handwritten stroke about 330 bytes, so 1.5 %
to 10 %). The PDF with notes carries the same `.xopp`.

Tests: `ElementTimesTest` (label `session`): the attribute written only when known and read back with no loader
warning; a builder with only upstream's methods reads the file without an error; a broken value is ignored; the
`.xopp` inside a PDF with notes; copies, erased pieces and shapes keep the time, upstream's serialization unchanged;
the clock; the cost. `ElementTimesCanvasTest` (label `canvas`): pen, highlighter and shape strokes get the time the
pen touched, undo and redo keep it; erased pieces keep it; pasted elements are new; a text gets the time its box
opened, an image when inserted, a sticky note when placed; the page's text keeps the time it was begun.

## The timeline

`qt/src/session/Timeline.*`: a pure model (no UI, no drawing), built from the document under its read lock.

- **One clock, absolute times.** An element is at its creation time. An element tied to a recording (upstream's
  `fn`/`ts`) is at the recording's start plus its `ts`, so it appears exactly when it is heard. A recording starts
  where its ink says (the earliest `xqt-created − ts` of its elements: pausing the recorder only makes that
  difference larger, as the pause is not in the file), else at the time in its name (`2026-10-04_14-03-22.ogg`, local
  time, as Xournal++ and xournal-qt name recordings: so Xournal++ files with a recording are placed too), else
  nowhere: then its ink counts as having no time. A recording is a **track** from its start for its length (from its
  file; the span of its ink when the file is nowhere, which places the ink but plays nothing). Voice memos of a page
  are tracks too.
- **The bar** is that clock with the long pauses taken out: a pause of more than 8 s in which nothing is written and
  nothing recorded becomes 1.5 s; one of more than 20 minutes also starts a **session**, a mark on the bar. Shorter
  pauses play as they were, so the rhythm of writing stays.
- **Elements without a time** (older files, files saved by Xournal++) come first, page by page in the order of their
  layers, 120 ms each (at most 6 s together); then a pause, then the first session.
- **A stroke is drawn on** over its length at handwriting speed (160 points a second, 0.12 to 2.5 s), at most until the
  next element comes; points have no times, so it grows evenly along its length. Texts, images and links appear at
  once.
- Elements of hidden layers are not on it (they are not drawn either).
- **At a moment** (`frameAt`): how many elements are shown whole, and which one is being drawn and how much of it.
  `heardAt`: the recording heard then and where in it (the one started last when two overlap). `wallTime`: the clock
  time shown on the bar. `barOf`: where an absolute time is on the bar.

Tests: `TimelineTest` (label `session`): the order with the prelude first, the bar's pauses and session marks, the
times shown; strokes drawn on over a while and texts at once; a recording placed by its ink, its ink after a pause of
the recorder where it is heard; a Xournal++ recording placed by its name, a memo whose file is nowhere not on the
bar, a recording placed nowhere; the time in recording names; hidden layers and empty documents; a long prelude stays
short.

## Replay

`qt/src/canvas/TimelineReplay.*` (the drawing), `CanvasView::startReplay`, `qt/src/render/ElementFilter.*`,
`qt/src/app/TimelineControl.*` (`app.timeline`), `TimelineBar.qml`.

- **Entered** from ⋮ → View → **Replay the writing** (not in text files), or from the playback pill's replay button
  (the replay starts where the recording is heard and plays on). The document is replayed from the start, paused.
- **The play bar** at the bottom of the page (`timelineBar`): ✕ (`timelineClose`, also Esc), the start of this or the
  previous session and the next one (`timelinePreviousMark`, `timelineNextMark`), play/pause (`timelinePlay`, also
  Space), a slider (`timelineSlider`) with a mark where each session begins (its date on hover) and a red band where a
  recording is (grey: its file is not here), the bar's time ("1:23 / 4:56") and the clock time of the moment ("Sun 4
  Oct 2026, 14:03"; "Written before the times were kept" in the prelude), the speed (`timelineSpeed`: ½×, 1×, 2×, 4×,
  8×). ← and → go 5 s, Home and End to the ends. The tools, the tool bar and the pills are hidden, as in reading.
- **Audio** plays where it overlaps, through `app.audio` (the same player as the playback pill, which is hidden
  meanwhile): at 1× only (at other speeds the replay is silent); the clock follows what is heard when they drift
  apart by more than 250 ms; a recording that ended or cannot be played is not started again until the next jump.
- **Read-only**: the view is for reading (`CanvasView::isReadingOnly`): every tool scrolls, as the hand does; the
  session refuses changes (`DocumentSession::setReplaying`: `isReadOnly`, so no paste, stickers, notes, templates,
  bookmarks; no undo or redo), and the document's keys are off. **A tap on ink** goes to the moment it was written
  (less the audio's lead-in where a recording is heard) and the stroke is written again from there. **Leaving** draws
  the pages whole again; the document was never changed (no undo step, not modified, the same page revisions: the
  UI test compares the `.xopp` to the byte). Should it change all the same (a page deleted in the sidebar), the replay
  ends with a message; another tab ends it too.
- **Drawing** (`TimelineReplay.h`), so that playing stays smooth and the page pictures are kept:
  - The pages' pictures (`PageRaster`) are drawn with a filter (`RasterHost::rasterFilter`, a thread-local
    `render::FilterScope` around upstream's `DocumentView::drawPage`): only the first N elements of the timeline.
    Upstream's `LayerView` asks the fork's layer drawer first (an existing seam); the sticky notes' drawer leaves out
    what is not there yet, and draws other layers itself while a filter is set. Without a filter nothing changes.
  - What came since and the stroke being written are drawn over the picture when the canvas composes its tiles
    (`CanvasPage::composeTile`), as the stroke being written with the pen is: per frame only the tiles under the new
    part of the stroke are composed again, nothing is rendered. The overlay knows the filter the picture shown was
    drawn with (`PageRaster::withDrawnBuffer`), so it draws exactly what that picture lacks: nothing twice, nothing
    missing while a new picture is on its way.
  - The pictures catch up (the filter "committed", the pages whose content differs drawn again in the background) when
    the overlay would hold more than 48 elements or is older than 2.5 s, when the replay goes back (a picture cannot
    be drawn smaller), when a sticky note comes (its paper is drawn by its own drawer), when playing pauses and when
    the slider is let go. A page keeps its old picture until the new one is there. Pages without a picture are drawn
    with the filter when they come into view. The PDF background is drawn outside the document's lock as before.
  - The stroke being written: a copy of the stroke cut at the fraction of its length (the last point between two
    points, its pressure too), drawn by upstream's `StrokeView`.
- Thumbnails, previews, the page sidebar and exports show the whole document (they do not use the filter).

Tests: `TimelineReplayTest` (label `canvas`): the pages as of a moment, the stroke being written as far as it got,
back and forth, the whole document after leaving; playing forward draws over the picture without rendering until the
overlay is full, then commits; read-only (the pen writes nothing, a tap gives the ink's moment, nothing modified); a
sticky note's paper and ink come when they were made. `TimelineUiTest` (label `ui`): ⋮ → View → Replay the writing,
the slider, the speed, play, the pen writes nothing, ✕ and Esc, the document the same to the byte, not modified, the
same undo step and page revision; a tap on ink; another tab ends it; a recording heard where it is (the fake speaker),
the playback pill's replay button.

## Not built (later)

- Level 3 (erasing, moving, recolouring, page changes replayed): it waits for the version history (B6).
- Times per point (a stroke grows evenly along its length).
- Speech at other speeds than 1× (pitch-kept time stretching).
- A replay of the audience's screen while presenting (the presenter's view replays, the audience's shows the
  document).
