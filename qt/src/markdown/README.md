# markdown: the Markdown engine

Target `xqt-markdown` (`qt/cmake/XqtMarkdown.cmake`). Parses, lays out, paginates and draws Markdown text with the same
text stack as upstream's text elements (Pango, cairo). No views and no editing UI: the editors and their sessions live
in `canvas` (`MarkdownEditor`, `MarkdownSession`, `MarkdownFile`).

| Class | Job |
| --- | --- |
| `MdDocument` | a parsed text (CommonMark + GitHub extensions via md4c) as blocks of formatted runs that know their source range |
| `MdLayout` | a parsed text laid out for a width (Pango, zoom-independent) and drawn with cairo |
| `MdPaginate` | a text split into pages, each a Markdown text of its own |
| `MdBox` | Markdown boxes on pages: texts flagged as Markdown (the page's "Markdown" layer, a sticky note's text); registers the renderer and sizer upstream's `TextView` and `Text` ask |
| `MdText` | lines of a Markdown source (internal helpers) |
| `MdFormat` | the formatting tools as edits of the source (one undo step each) |
| `MdMath` / `MdTexDelimiters` | formulas (`$…$`, `$$…$$`, `\( \)`, `\[ \]`) laid out by MicroTeX |
| `MdImages` | the pictures of a text (`![alt](path)`) and where they are found |
| `MdHighlight` | code highlighting (KSyntaxHighlighting, optional) |
| `MdTasks`, `MdBookmarks`, `MdPassages` | task lines (to-dos), bookmark comments, passages for the library's search |
| `EmojiData`, `EmojiFont`, `Grapheme` | emoji by shortcode, the bundled colour emoji font, stepping by grapheme cluster |

**May depend on**: `xoj-core` (upstream's `model/Text`, `model/MarkdownText.h`: the hooks it sets; `util`), Pango,
cairo, the vendored md4c and MicroTeX (`qt/3rdparty`). No Qt Gui.

**Tests**: `qt/tests/markdown` (label `markdown`). **Docs**: [markdown boxes](../../docs/features/markdown-boxes.md),
[the .md editor](../../docs/features/md-editor.md), [images](../../docs/features/md-images.md),
[text documents as PDF](../../docs/features/md-pdf.md).
