# Two-column Markdown: research note (2026-10-04)

The author asked whether `.md` files and the page's Markdown text could be drawn in two columns, switched on by a
comment, without breaking tables and wide content, and how hard that would be. This note answers that from the code
as it is after `qt/md-tables` (table columns as a browser sizes them). **Verdict: doable, but not easy by the bar the
block set** (layout and pagination contained in the Markdown engine, editing working without special cases). It is
not built; the cheapest way to get the reading experience today is at the end.

## What other tools do
- **Browsers (CSS multi-column):** `column-count: 2` on a container. Content flows down the first column and on into
  the second (`column-fill: auto`), or the columns are made equally high (`column-fill: balance`, the default at the
  end of the container). `column-span: all` makes an element (a wide table, a figure) cross both columns: the columns
  above it are balanced, the element goes across, two columns start again below. `break-inside: avoid` keeps a block
  in one column. A paragraph can break between its lines from one column into the next.
- **LaTeX** (`\twocolumn`, `multicol`): the same model on pages; floats `figure*` / `table*` span both columns
  (and only go to the top of a page, which is why authors fight them).
- **Typora, Obsidian, VS Code's preview, GitHub:** no column mode. Obsidian has it through plugins (callout or
  fenced "multi-column" blocks, side by side, no flow from one column into the next). Pandoc gets it through LaTeX.

So "flow over two columns, with spanning elements" is the established model, and it is what the marker should mean.

## The marker
```markdown
<!-- xqt:columns 2 -->
… two columns from here …
<!-- xqt:columns 1 -->
```
- The same family as `<!-- xqt:cont … -->` and `<!-- xqt:bookmark … -->`: a comment, so VS Code, GitHub and
  Xournal++ show the text as usual (one column) and nothing is lost. The engine already hides comments.
- It starts a region; the region ends at `<!-- xqt:columns 1 -->` or at the end of the text. In a text box or a
  sticky note it would be honoured too (the box is the container), but there it is rarely wanted.
- Front matter (`columns: 2` at the top of a `.md`) would only fit a whole file: the engine has no front matter
  today (a `---` block at the top is drawn as a rule and a heading), page-wise text and boxes have no top of a
  file, and a comment can switch back. The comment is the better carrier; front matter could be added later as a
  shorthand for "the comment at the top".
- Pagination already writes continuation comments into page slices; a page that continues a two-column region
  would need the marker repeated in its slice (`<!-- xqt:cont columns=2 -->` or the marker itself), exactly as a
  continued table repeats its header. That part is small.

## What would have to change

### Layout (`MdLayout`): moderate, contained
- Inside a region, blocks are laid out at the column width, `(width − gap) / 2`, as one tall flow, then cut into
  columns. Everything below the region moves down by the region's height.
- **Cutting between blocks** is easy: the items of a block are moved by an offset (x by column, y up). The extents
  in `Layout::blocks` move with them.
- **Cutting inside a block** (a paragraph's lines going on in the next column, as CSS does) is not: an `Item` is a
  whole Pango layout. It would need either an item that draws only some lines of its layout (a clip and an offset,
  and then every user of items, from `linkAt` and `findText` to the editor's hit test, has to respect the clip) or
  splitting the paragraph's runs into two Pango layouts (the source map and the line breaks must then agree with the
  whole). Without it, a column ends at the last block that fits, and long paragraphs leave the first column short.
  Lists and tables can be cut between items / rows as pagination does (the parts are already known).
- **Balancing** (equal columns at the end of a region or above a spanning block): a search over the cut point,
  cheap once cutting works.
- **Spanning:** a table whose min-content width (the widest words, now measured by `measureColumns`) is wider
  than a column, a block image whose natural width is wider than a column, and a display formula that would be
  drawn smaller go across both columns: the columns above are balanced, the block is laid out at the full width,
  a new pair of columns starts below. Code blocks stay in their column (they wrap, as today); a narrow table stays
  in its column with the browser widths of `qt/md-tables` (and is drawn smaller there before it would break words).
  This is why the table widths had to be fixed first: without min-content widths there is no sound rule for "too
  wide for a column".
- The block being written is laid out as its source (`rawBlock`, `displayPreview`, `picturePreview`), today always at
  `x = 0` and `st.width`: these need the column's x and width (small).

### Pagination (`MdPaginate`): the hard part of the engine
`Splitter` finds a page's end from `lay.blocks[k].bottom`, `parts` and line positions, all assuming that y grows
through the text. In a two-column region the page holds about twice the height, but not exactly (column breaks
waste room, spanning blocks take full rows), and the y of the blocks is no longer in text order. The split search
has to become "the longest prefix whose column layout fits the frame": lay out the candidate slice in column mode
and test its height, a search over the split candidates the splitter knows (block ends, paragraph lines, list items,
table rows, code lines) with a layout per probe. Incremental re-pagination while typing (`before` / `beforeSource`)
keeps working as it compares slices, not geometry. The continuation marker must carry the column mode.

### Editing (`MarkdownEditor`, the canvas): special cases, which is the bar this fails
- **Hit testing** (`MarkdownEditor::hit`): the nearest text by `dy * 1000 + dx` works inside a column, but below a
  shorter left column a tap picks the right column's line at that height. It needs "which column is x in" first:
  the layout has to publish its column rectangles.
- **Up / Down** (`verticalMove`): goes to the point 2 pt above / below the cursor. At the bottom of the left column
  it must go to the top of the right column (and back), not to the right column's line at the same height or to
  the next page. A special case on the column rectangles.
- **Typing:** the block being written is shown as its source, usually taller; the column break moves while typing,
  so text jumps between columns more often than it jumps between pages today. Not wrong, but restless.
- **Selection painting, caret, search marks, links, PDF export, thumbnails:** work from items and need nothing.
- **Order by position:** the chapters of a page (`DocumentChapters`, sorted by `blocks[i].top`) and anything else
  that sorts by y would list a right-column heading before a lower left-column one; they need text order (the block
  index) instead.
- **The source panel** syncs by source offsets and block extents: works, but "scroll the page to the block" now has
  an x as well as a y.

### Xournal++ and files
Nothing changes in the `.xopp` or the `.md`: the marker is a comment in the source, each page still holds one box
whose text is its slice. Xournal++ shows the source as today (one column, the comment visible as text).

## Cost
| Part | Estimate |
| --- | --- |
| Marker, region parsing, continuation marker | 0.5 day |
| Layout: columns cut between blocks / list items / table rows, balancing, spanning tables and images | 2 days |
| Layout: a paragraph broken across columns (clipped items or split layouts, with all users of items) | 2 days |
| Pagination: split search on the column layout, tests for every split kind in columns | 2 days |
| Editor: column rectangles, hit test, Up / Down across columns, raw block at the column | 1 day |
| Chapters / bookmarks order, source panel scroll, UI tests, device pass | 1 day |
| **Total** | **about 8 to 9 days**, half of it in pagination and paragraph splitting |

Without paragraphs broken across columns (columns cut between blocks only) it is about 6 days, with visibly
uneven columns next to long paragraphs.

## Recommendation
- **Do not build it now.** It is not contained in the engine: pagination's split search changes in kind, and the
  editor needs column-aware hit testing and cursor movement. The tables fix it depended on is done.
- **Try the reading experience first, for free:** a `.md` shown as pages (not as one continuous page) with the
  view's "Two pages side by side" button (book spreads; hold it for the page layout) on a landscape screen shows two
  pages side by side, which reads like two columns, keeps every table and picture at full page width, and needs no
  change at all. If the author likes
  reading that way, smaller page sizes (A5 portrait on a landscape tablet) get closer still.
- **If two columns on one page are still wanted** (e.g. for printing handouts), build it in the engine as above,
  with the comment marker, spanning tables / images, and balanced columns, as one block of its own, about 1.5 weeks,
  and decide first whether paragraphs may break across columns (CSS does) or columns break only between blocks.
