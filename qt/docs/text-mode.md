# Text mode

A keyboard-oriented way to write the text of a page, like in a word processor (toolbar button "Text mode",
Ctrl+Alt+E). The editor opens beside the pages (on the right; the page is zoomed to fit next to it and gets its
zoom back afterwards). The page shows the text while you type, laid out from its top-left margin (2 cm; on a ruled
page with a margin line: beside the line) and broken into lines at the right margin.

## What it can do
- Kinds of paragraphs: text, headings 1–3 (24 / 18 / 15 pt, bold), bullet and numbered lists with levels
  (Tab / Shift+Tab).
- Per paragraph: bold, italic, size (A− / A+), color (the pen colors of the tool bar).
- Markdown shortcuts at the start of a line: `# `, `## `, `### `, `- ` / `* `, `1. `. Enter after a heading starts a
  paragraph; Enter on an empty list item ends the list. Ctrl+B / Ctrl+I, Ctrl+1/2/3 (headings), Ctrl+0 (text).
- Done (or Esc) keeps it; the whole edit is one undo step. Cancel puts the page back as it was.
- If the text is longer than the page, the editor says so (the text runs below the margin): insert a page and go on
  there — one text per page.

## Why only per paragraph (Xournal++ compatibility)
The text is stored as ordinary Xournal++ text elements, so Xournal++ shows and edits it and nothing is lost when
the file goes back and forth. A Xournal++ text element has one font (family, bold / italic in the font name, e.g.
"Sans Bold Italic"), one size, one color, a wrap width and an alignment — no mixed formatting inside a text and no
list or heading structure. So:
- every heading, paragraph and list item is one text element; a list item is two (the marker "•" / "1." and the
  text, which gives the hanging indent);
- all of them are in a layer named "Text" at the bottom of the page (ink written with the pen goes on top of it,
  into the layer it went into before);
- xournal-qt reads the layer back into paragraphs: headings by their size and bold font, list items by their
  marker, empty lines by the space between texts.

Long paragraphs are broken into lines with line breaks where Pango wraps them at the right margin (after the
spaces), not with a wrap width: Xournal++ got text wrapping only after 1.3 (1.3.4 ignores the wrap width and shows
one long line). With line breaks every version shows the same lines; reading the page back joins the lines into the
paragraph again. Checked with the installed Xournal++ 1.3.4 (`xournalpp --create-img`): the same page as in
xournal-qt. Editing such a paragraph in Xournal++ keeps its line breaks (no reflow there); xournal-qt reflows it the
next time the text mode opens.

Bold inside a sentence is not possible: formatting applies to the whole paragraph.

Code: `qt/src/canvas/TextFlow.*` (layout, reading back, the session with its undo step), `qt/src/quick/TextFlowEditor.*`
(the editor's document), `qt/src/app/qml/TextFlowPanel.qml`.

## Links in texts
A text can contain web addresses (`https://…`, `www.…`, `mailto:…`) and links to a page of the same document,
written `#Page:12`. Both are underlined and coloured, and a tap opens the address or goes to the page. ⋮ on a page
copies such a link. When pages are inserted, moved or deleted, the numbers in the links follow (undo too), so a
note that points at page 12 keeps pointing at the same page. Xournal++ shows this as the plain text it is - the
file format does not change - so the links survive going back and forth.
