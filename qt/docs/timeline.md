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
