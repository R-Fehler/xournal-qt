# Markdown boxes

Write Markdown onto a page. The page shows it formatted as you type: headings, **bold** / *italic* / ~~struck~~,
`code`, links, lists (nested, numbered, task lists), quotes, code blocks, tables and rules. The dialect is CommonMark
with GitHub's extensions (tables, strikethrough, task lists, bare web addresses) and `[[wiki links]]`.

## Two ways
- **The page's Markdown text** is written in the editor beside the page, from the top-left margin to the right
  margin. It flows onto the next pages (see below).
- **Markdown text boxes** go anywhere on a page. Turn on "Markdown" in the text tool's font menu (hold the text
  button, or tap it again), then tap where the text should go. With "Write Markdown beside the page" (on by
  default) a box is written in the editor beside the page like the page's text, and the page shows it formatted
  while typing. Off, a box is edited on the page like any text box: its source is shown while editing and it is
  drawn formatted afterwards. A tap on a box (text tool) edits it again, the same way.

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
- The search finds text in boxes and marks it where it is drawn.
- Code blocks with a language (```` ```python ````, `cpp`, `js`, `bash`, ...) are syntax highlighted (Kate's
  highlighter, KSyntaxHighlighting; optional at build time).

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

Code: `qt/src/markdown/` (parser `MdDocument`, layout `MdLayout`, boxes `MdBox`), `qt/src/canvas/MarkdownSession.*`,
`qt/src/app/qml/MarkdownPanel.qml`. The parser is md4c (vendored, `qt/3rdparty/md4c`).

## Not yet
- Images.
- Editing directly on the page (live preview).
- Flattening into Text mode.
- Math.
