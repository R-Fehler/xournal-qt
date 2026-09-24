# The `.md` editor

Markdown files (`.md`) are documents of their own: opened from the library (or with Open, or on the command line),
they are written in on their pages, and saving writes the text back to the file. Never a `.xopp`: ink never goes
into a `.md`. Plain text files (`.txt`) are edited the same way, as plain text (below).

## Writing
- The file's text flows over plain A4 pages as the page's Markdown text (see
  [markdown-boxes.md](markdown-boxes.md), "Flowing onto pages"). It is written as a Markdown text on the page: the
  block with the cursor shows its Markdown (the marks dimmed), the others are shown formatted, and the pages follow
  while typing. All keys of the Markdown text written on the page work (Enter continues a list, Ctrl+B / I / E / K,
  Ctrl+1 / 2 / 3 / 0, Tab / Shift+Tab, …).
- **Whatever the tool**, the pen and the mouse put the cursor where they press, and a drag selects. A finger
  scrolls; a tap with a finger puts the cursor there too. Ctrl + click (and a finger tap) on a link follows it.
- Typing without a cursor starts writing at the top of the page in view.
- The tool bar has no ink tools for a text file (pen, eraser, shapes, colors, sizes, add page); page operations
  (insert, delete, move, paste pages, backgrounds, images, chapters) do nothing: the pages are the text's.
- Backspace right after the mark of an empty list item or quote line (`- `, `1. `, `- [ ] `, `> `) removes the
  whole mark (as in Ghostwriter).
- Undo / redo (Ctrl+Z / Ctrl+Shift+Z and the undo button) go step by step through the text being written (a word,
  a line break, a deletion). The editor keeps changes, not copies of the text, so a long file stays cheap.

## New Markdown file, new text file
The library's new button (the file with a plus) opens a menu: **New document…** (notes, as before), **New
Markdown file…** and **New text file…**. The last two ask for a name and make an empty `name.md` / `name.txt` in the
current folder (a name that is taken becomes "name (2)"; a `.md` also avoids the name of a `.xopp` or PDF there),
open it and put the cursor in it.

## Pages or one continuous page
Text files are on A4 pages by default (the native feel; the pagination of the Markdown boxes). The layout menu of
the page / zoom pill (press and hold, or right-click, the layout button) has **Text on pages** and **Text on one
continuous page**: the whole text on one page of A4 width that grows and shrinks with it (at least A4 high), with
no page breaks. It is a setting for all text files (`textContinuous` in the `xournalQt` part of the settings);
switching it lays the current text document out again (the text being written ends and its undo history starts
anew, the text stays as it is); other open text documents keep their layout until they are opened again. The menu
shows how the current one is laid out. Print and PDF export print what is shown (one long page when continuous).

The continuous page does not split the text (`md::onePage`), and its whole text is parsed and laid out again on
every key. The Pango layouts are shared through a per-thread cache in `md::layout` (keyed by a block's text,
formatting and options; two generations of 1,500), so only the block that changed is shaped anew: 20 keys in a
13-page file took 1.4–1.8 s before and 0.56–0.62 s after, against 0.36–0.42 s on pages (under load 9–11, so
ranges). `XQT_BENCH_TEXT=1 xqt-canvas-tests --gtest_filter='TextDocumentTest.bench*'` measures it. What is left is
md4c's parse and the layout pass over the whole text.

## Plain text (`.txt`)
A `.txt` opens as plain text, like a notepad: no Markdown, no highlighting. Every line is shown as it is, in a
monospaced font (10 pt, so columns line up and code or LaTeX reads as written), wrapped at the page's width, and
the pages follow the text as for a `.md`. Enter starts a line indented as the one before; Tab types a tab
(Shift+Tab takes one tab or up to four spaces of indentation away); Ctrl+B / I / E / K and the heading keys do
nothing. Saving has the same guarantees as for a `.md`.

In the engine, a plain text is a page's slice that starts with the line `<!-- xqt:plain -->` (a continuing page:
`<!-- xqt:cont plain -->`), a Markdown comment that Xournal++ shows as it is: `md::parse` then makes a paragraph of
one run per line (no md4c), `md::layout` puts the lines below each other in the box's font, and the pages split
between lines (or within a line longer than a page). `md::join` takes the marker away again. The empty line after
the last line break takes room only while the cursor is on it.

## Other text files (code, LaTeX, JSON, …)
They open read-only, as before (one code block, highlighted by its extension), with a note that offers **Edit
anyway** (also in ⋮). The first time for a file it warns: "This file is edited as plain text; the app does not know
its format…" (OK / Cancel). OK edits it as plain text (as a `.txt`, in the same tab), and the file opens for
editing from then on without asking; the accepted files are listed in the config folder
(`~/.config/xournal-qt/edit-as-text.json`, the last 500). A file that is not UTF-8, is over 2 MB or cannot be
written says why and stays read-only.

## Saving
- Save (Ctrl+S, the save button, the question when closing) writes the text back to the file, in the background
  like every save. Unchanged text is written byte for byte as it was:
  - the bytes before the first change and after the last one are the file's own: a byte order mark, `\r\n` line
    ends, a missing newline at the end stay;
  - the changed part is written with the line ends the file mostly has (`\r\n` in a Windows file);
  - a file with both kinds of line ends and changes far apart can get the main kind in the lines between them.
- It is written atomically: to another name first, then renamed over the file (Qt's `QSaveFile`; a symbolic link
  stays and its target is written; a folder that cannot be written in, with a file that can, is written in place).
- The modified dot follows the text: typing and undoing back to the saved text is not modified.
- Autosave (the setting's interval) and crash recovery keep the **text**, in the app's cache
  (`~/.cache/xournal-qt/autosaves/<pid>-<n>.autosave.text`, `….emergency.text`), never next to the file. After a
  crash the tab is offered for recovery like any document; recovered, it is the file with the text it had
  (modified). Saving removes the autosave.
- A file that cannot be edited opens read-only, as before, with a note saying why: not UTF-8, bigger than 2 MB, or
  a file that cannot be written.

## Open externally
Every file the app does not keep as a `.xopp` or PDF (a `.md`, a `.txt`, another text file, an image being written
on) has **Open externally**: a button in the tool bar (an arrow out of a box), an entry in ⋮, and "Open externally"
in the library card's menu (Markdown, text and other files, images; a `.xopp` that annotates an image hands over
the image). It hands the file to the app the system has for it (`SystemApps`: `xdg-open` on Linux). A text file
with unsaved changes asks first: **Save and open**, **Open without saving** or Cancel. When the other app changed
the file, the tab reads it again (below) as soon as the window is looked at again.

## Edit as notes
A `.md` has **Edit as notes** (a button in the tool bar, a notebook with a pen, and ⋮): it makes a new document of
notes from the text as it is now, in a new tab next to the `.md`: the same A4 pages with the text as the page's
Markdown text flowing over them (a Markdown box per page, drawn formatted; the text tool edits it as in any
`.xopp`), and the pen writes on top of it. The `.md` stays as it is; ink never goes into a `.md`. The new tab is
titled `name.xopp`, counts as unsaved until it is saved (closing asks), and saving suggests `name.xopp` next to the
`.md`.

**In the library the two are two cards** ("MD" and the notes), not one: after "Edit as notes" they go their own
ways (the `.md` is edited as text, the `.xopp` with ink and its own Markdown text), so neither is the other's
background, unlike a PDF or an image and its `.xopp`.

## Share
Share… (⋮, the tab menu, a `.md` or text file card in the library) offers a text file as **the file itself**:
shown in the file manager, or copied to the clipboard as a file; unsaved changes are saved first. There is no "PDF
with notes" and no "For Xournal++" for a text file, and no Save as (with its file types): a text file is saved as
itself (the library's Rename and Copy give it another name).

## Changed by another program
The file is watched (and looked at when the window becomes active again and when its tab is shown). If another
program changed it (its bytes differ from what was read or saved last; our own saves are not changes):
- without changes here, it is read again at once, and a note says so; the cursor stays where it was;
- with changes here, the window asks: **Reload** (the file as it is now; Undo brings your version back) or **Keep
  mine** (not asked again for that version; saving writes over it).

## Code
- `qt/src/session/TextFile.*`: the file (bytes, byte order mark, line ends, its stamp), `encode` (the bytes for a
  text), the atomic write, `changedOnDisk`.
- `DocumentSession::setTextFile` and the text parts of `DocumentSession.cpp` / `DocumentSave.cpp`
  (`beginTextSave`): the modified state by the text, saving, autosave.
- `qt/src/canvas/MarkdownFile.*`: the document of a text file, `setText` (a new text as one undo step),
  `plainStyle`.
- `qt/src/markdown/MdDocument.*` (`isPlain`, the plain parse), `MdLayout.cpp` (`runPlain`), `MdPaginate.cpp`
  (`Style::plain`): plain text.
- `CanvasView::textMode` / `textPress` / `ensureTextEditor`, `CanvasInput` (`textPress`): the input.
- `qt/src/app/AppTextFiles.cpp`: opening, the file watcher, reloading; `SessionRecovery`: the journal's `text` flag.

## Patterns taken from other editors
- Ghostwriter (KDE, Qt): Backspace on an empty list item removes its mark; reload keeps the cursor position;
  the file watcher with a guard against our own saves.
- QOwnNotes: a note changed on disk without local changes is reloaded silently, with local changes the user
  chooses (Reload / keep).
- Typora / Obsidian's live preview (already in the Markdown boxes): the block with the cursor as source.

## Not yet
- Math (MicroTeX), images pasted into `<name>.assets/`, Obsidian vault detection and its warnings.
- "Save as" for a text file (use the library's Rename / Copy).
