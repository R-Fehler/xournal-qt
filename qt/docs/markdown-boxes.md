# Markdown boxes

Write Markdown onto a page. The page shows it formatted as you type: headings, **bold** / *italic* / ~~struck~~,
`code`, links, lists (nested, numbered, task lists), quotes, code blocks, tables and rules. The dialect is CommonMark
with GitHub's extensions (tables, strikethrough, task lists, bare web addresses) and `[[wiki links]]`.

## Two ways
- **The page's Markdown text** is written in the editor beside the page, from the top-left margin to the right
  margin. With the next step it flows onto new pages.
- **Markdown text boxes** go anywhere on a page. Turn on "Markdown" in the text tool's font menu (hold the text
  button, or tap it again), then tap where the text should go. A box is edited on the page like any text box: the
  source is shown while editing, and it is drawn formatted afterwards. A tap on a box edits it again.

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
drawn (canvas, thumbnails, previews, PDF export) through a small seam in upstream's `LayerView` (ADR-0002).

The drawing is vector (Pango / Cairo, as upstream's texts). In the PDF the text stays text. The layout does not
depend on the zoom, so lines break at the same places on the canvas, in the thumbnails and in the PDF.

Code: `qt/src/markdown/` (parser `MdDocument`, layout `MdLayout`, boxes `MdBox`), `qt/src/canvas/MarkdownSession.*`,
`qt/src/app/qml/MarkdownPanel.qml`. The parser is md4c (vendored, `qt/3rdparty/md4c`).

## Not yet
- Text longer than the page does not yet flow onto new pages. The editor says how much is below the margin.
- Images.
- Editing directly on the page (live preview).
- Flattening into Text mode.
- Math.
