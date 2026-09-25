# Markdown boxes

Write Markdown onto a page. The page shows it formatted as you type: headings, **bold** / *italic* / ~~struck~~,
`code`, links, lists (nested, numbered, task lists), quotes, code blocks, tables, rules and math formulas. The
dialect is CommonMark with GitHub's extensions (tables, strikethrough, task lists, bare web addresses),
`[[wiki links]]` and `$…$` / `$$…$$` formulas (as in Obsidian, Zettlr and GitHub).

## Two kinds of Markdown text, two ways of writing
- **The page's Markdown text** starts at the top-left margin and goes to the right margin. It flows onto the next
  pages (see below). The writing button's "Markdown" (or Ctrl+Alt+M) opens it beside the page. With the text tool,
  a tap on it writes it on the page.
- **Markdown text boxes** go anywhere on a page. Turn on "Markdown" in the text tool's font menu (hold the text
  button, or tap it again), then tap where the text should go. A tap on a box (text tool) edits it again.

Writing:
- **On the page** (the default): as in Typora or Obsidian's live preview. The text is shown formatted while it is
  typed, and the block with the cursor shows its Markdown with the marks dimmed. A heading keeps its size and bold
  stays bold, so `**` around a bold word is grey. Taps put the cursor where they are, in the text as drawn, and a
  drag selects. The page's text flows over its pages while it is written, and the cursor goes with it.
  - Keys:
    - Enter starts a new paragraph. In a list it starts the next item; on an empty item the list ends. In code it
      starts a new line (also at the end of the text, while its fence is not closed yet). After a code block's
      closing fence, Enter leaves the code: the code is shown finished and the cursor is where the next paragraph
      goes.
    - Shift+Enter continues the paragraph on a new line.
    - Moving: the arrows (with Ctrl, by words; Up and Down go by the lines as drawn), Home and End (with Ctrl, the
      whole text). Shift selects.
    - Editing: Backspace and Delete, Ctrl+A / C / X / V.
    - Formatting: Ctrl+B / I / E / K (bold, italic, code, link), Ctrl+1 / 2 / 3 / 0 (headings), Tab and Shift+Tab
      (list levels).
  - Ctrl+Z / Ctrl+Shift+Z undo and redo in the text being written; once it is done, the whole edit is one undo step.
  - Escape (or a tap elsewhere) is done.
  - Ctrl+Alt+M opens the same text beside the page.
- **Beside the page** (the text tool's font menu: "Write Markdown beside the page"): the source in an editor beside
  the page, and the page shows it formatted while typing.

## Flowing onto pages
The page's Markdown text goes on on the next pages when it is longer than the page: while typing, it is split onto
the pages again, pages are added after them (the same size and background; after a PDF page a plain one), and the
pages added go again when the text gets shorter. After editing, pages at the end that only held an emptied part of
the text go as well (a page with anything else on it stays). The whole edit, pages included, is one undo step.
Opening any of its pages (the text tool on the text, or the writing button there) edits the whole text.

Every page holds a part that is a Markdown text of its own, so each page is drawn from its own box (and Xournal++
shows each page's source). A page is split only where the rest reads the same on its own:
- between blocks, and a heading goes with the block after it;
- between the lines of a paragraph, not inside **bold**, a link and the like, with two lines on each page where
  possible;
- between the lines of a code block: the page closes the fence and the next page opens it again;
- between the items of a list: a numbered list goes on with its numbers;
- between the rows of a table: the next page repeats the header.

A part that continues the page before starts with a comment, `<!-- xqt:cont … -->` (not shown), which says what was
added for the page (a fence, a table header), so the parts give exactly the text again. A block that cannot be split
and is higher than a page (a long quote, a big image) stays on its page and goes below its bottom margin; the editor
says so.

Code: `qt/src/markdown/MdPaginate.*`, `qt/src/canvas/MarkdownSession.*`.

## Moving
Markdown text boxes (and the page's text) are selected and moved like everything else: a rectangle or a lasso
around them, or a tap with the object select tool, when nothing of the selected layer is there. Then they can be
moved (also to another page: they go into that page's Markdown layer), deleted, copied or cut. The selection is
the box as it is drawn. While they are selected, the layer "Markdown" is the selected layer; when the selection
ends, the layer selected before is again (the pen writes where it did).

## Size
The body text is drawn at the text's font size: the size Xournal++ shows the source in is the size it is drawn at.
Headings, code and the rest scale from it. New Markdown text gets the Markdown size, which is 60 % of the text font
by default (a 16 pt text font gives 10 pt). It can be set in the text tool's font menu (with "Markdown" on) and in
the editor beside the page ("Size", which also changes the text being edited).

## Using it
- Open the editor with the writing button in the tool bar (hold or right-click it and choose "Markdown"; after that a
  tap opens Markdown again) or with Ctrl+Alt+M. The editor opens beside the page and shows the source.
- The buttons insert Markdown: H1-H3, lists, quotes and task lists change the mark of the current line; B, I, S,
  code and Link put marks around the selection. Enter continues a list, and Enter on an empty item ends it.
- Keys: Ctrl+B / I / E (code) / K (link), Ctrl+1-3 for headings, Tab / Shift+Tab to indent list items.
- Done (or Esc) keeps the text, and the whole edit is one undo step. Cancel puts the page back.
- With the text tool, a tap on a box opens its Markdown again.
- A tap on a link opens it; `[text](#Page:12)` goes to page 12 of the document.
- Headings 1-3 are chapters in the contents.
- The search finds text in boxes and marks it where it is drawn: on the formatted words (headings, lists, code,
  the pages the text flows onto), a phrase over a line break on each of its lines, and while the text is written on
  the page, the block with the cursor where its source is shown. `md::sourceRects` gives where any range of the
  source is drawn.
- A tap on a task's check box switches it (`- [ ]` / `- [x]`): with the text tool, the hand, a select tool or a
  finger, also while writing on the page. One undo step (while writing: in the text being written).
- Code blocks with a language (```` ```python ````, `cpp`, `js`, `bash`, ...) are syntax highlighted (Kate's
  highlighter, KSyntaxHighlighting; optional at build time).

## Math
`$…$` is a formula in the text, `$$…$$` a formula block of its own (display style: big sums and fractions), centered
in the box. The `$$` may stand on lines of their own. As in md4c (the parser) and GitHub, an opening `$` does not
follow a letter or digit and a closing one is not followed by one: `costs $5 and $10` stays text, and `\$` is a
dollar sign.

- **Drawn by MicroTeX** (vendored, `qt/3rdparty/microtex`, MIT) with the Latin Modern Math font, which is compiled
  into the program: no LaTeX, no external program, the same on Android. Formulas are paths (vector): sharp at any
  zoom, and in the PDF export and the hybrid PDF as vector drawing (not as text: the TeX is not selectable there).
  They are drawn 1.2 times the size of the text around them (Latin Modern's letters are smaller than a sans
  text's; KaTeX does the same) and in its color (in a link, a quote, a heading); `\textcolor{red}{x}` colors a part.
- **In the text**: a formula takes the place of one character (U+FFFC) of the Pango layout, with a shape as big as
  the formula, on the text's baseline. So lines break around it, pages are split around it (never inside a
  formula, and a `$$` block keeps its `$$` lines), and a tap on it is a place in the text. A formula wider than the
  box is made smaller to fit.
- **Writing on the page**: the block with the cursor shows its Markdown, formulas included (their source in a
  monospaced font), as for the other marks; a `$$` block being written also shows the formula below its source.
  The other blocks show the formulas drawn. A tap on a drawn formula puts the cursor into its source (its start or
  end, by the half tapped). In a `$$` block that is not closed yet, Enter starts a line of the formula (as in a code
  block), not a new paragraph.
- **Errors**: a formula that MicroTeX cannot read is shown as its source, in red. Nothing a formula says can crash
  the app: MicroTeX gets no source longer than 8,000 bytes or nested deeper than 64 braces, its exceptions are
  caught, and the crashes found by fuzzing it are fixed in the vendored copy (its README).
- **Search**: the TeX stays searchable text. The search and the library's index search a box's texts with each
  formula's source in place of its character (`md::searchText`), and a hit inside a formula marks the formula
  (a display formula: the formula, not its whole line).
- **Cache**: formulas are laid out once per source (and style, inline or display) and kept as their paths in em,
  shared by every thread that draws (the canvas, thumbnails, previews, the export). Size and color are applied when
  drawing, so zooming and a heading's size need no new layout. The cache owns at most 16 MB (the least recently used
  go; a formula takes 2–10 KB). MicroTeX with its font takes about 12 MB once the first formula is drawn (28 ms). A
  page of 50 formulas: laid out in about 5 ms the first time, drawn in about 9 ms, then from the cache
  (`MdMathText.PageOfFormulas`; `XQT_BENCH_MATH=1` prints the times). A character that the math font does not have
  (Chinese, emoji in `\text{}`) is drawn by Pango: the first of a font takes 60–100 ms (loading it).
- **Not supported by MicroTeX**: `\color{…}` outside arrays (use `\textcolor`), and `\newcommand` is shared by all
  formulas (MicroTeX keeps macros globally), so a macro defined in one formula is only known in the others once that
  one was laid out.

Code: `qt/src/markdown/MdMath.*` (MicroTeX, the recording as paths, the cache), `MdLayout.cpp` (the shapes in the
text, `searchText`, `mathAt`).

## How it is stored (Xournal++ compatible)
A box is an ordinary Xournal++ text element in a layer named "Markdown" at the bottom of the page. Ink written with
the pen goes on top of it, into the layer it went into before.
- **Text:** the Markdown source.
- **Font:** the body text's family and size.
- **Color:** the text color.
- **Wrap width:** the width of the box.
- **Position:** the top left of the box.

Xournal++ shows the source as plain text and keeps it unchanged. xournal-qt draws it formatted everywhere a page is
drawn (canvas, thumbnails, previews, PDF export) through small seams in upstream's `Text`, `Layer` and `TextView`: a text in a Markdown layer knows it is one, is drawn formatted and is as big as it is drawn (ADR-0002).

The drawing is vector (Pango / Cairo, as upstream's texts). In the PDF the text stays text. The layout does not
depend on the zoom, so lines break at the same places on the canvas, in the thumbnails and in the PDF.

Code: `qt/src/markdown/` (parser `MdDocument`, layout `MdLayout`, boxes `MdBox`), `qt/src/canvas/MarkdownSession.*`
(editing, pages), `qt/src/canvas/MarkdownEditor.*` (on the page), `qt/src/app/qml/MarkdownPanel.qml` (beside it). The parser is md4c (vendored, `qt/3rdparty/md4c`).

`.md` files are edited the same way, on their own pages: [md-editor.md](md-editor.md).

## Not yet
- Images.
- Flattening into Text mode.
- Math: why a formula cannot be drawn (MicroTeX's error); per-formula editing inside a block (the whole block shows
  its source, as for the other marks).
