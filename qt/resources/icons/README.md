# Additional icons

Icons from [Lucide](https://lucide.dev) (ISC license), in the style of upstream Xournal++'s Lucide icon theme
(`ui/iconsLucide-light`), for features the upstream theme has no icon for. Copied next to the upstream icons at
build time (`qt/cmake/XqtApp.cmake`).

The formatting bar's icons (`xqt-code`, `xqt-code-block`, `xqt-sigma`, `xqt-sigma-block`, `xqt-list-todo`, `xqt-quote`, `xqt-table`,
`xqt-plus`, `xqt-align-*`) are Lucide's; `xqt-rule`, `xqt-page-break`, `xqt-row-*`, `xqt-column-*` and `xqt-note-space` (a slide with space for notes
around it) are drawn in the same style for this app, as is `xqt-select-more` (a dashed selection with a plus: "Select
more" in the selection's pills).

`xqt-star` and `xqt-bookmark` are Lucide's (`star`, `bookmark`); `xqt-star-filled` and `xqt-bookmark-filled` are the
same shapes filled (a favourite, a bookmarked page).

The adaptive tool bar (qt/adaptive-toolbar; icons that must be clear without a tool tip, e.g. on a phone):

- Lucide's, as they are: `xqt-share` (`share-2`), `xqt-page-text` (`file-text`: writing
  on the page), `xqt-file-output` (`file-output`), `xqt-pencil` (`pencil`), `xqt-file-pen` (`file-pen-line`),
  `xqt-image-off` (`image-off`), `xqt-copy` (`copy`), `xqt-archive` (`archive`), `xqt-palette` (`palette`),
  `xqt-scaling` (`scaling`), `xqt-panel-top` (`panel-top`), `xqt-book-open` (`book-open`: two pages side by side),
  `xqt-page-single` (`rectangle-vertical`: one page), `xqt-eraser-whiteout` (`paint-roller`: the eraser paints
  white), `xqt-tools-more` (`chevrons-right`: "more tools").
- Drawn in the same style: `xqt-finger-draw` (Lucide's `pointer` with a stroke at the fingertip: the finger draws),
  `xqt-mark-text` (lines of text, one of them highlighted: mark PDF text), `xqt-text-box` (a dashed box with a T: a
  text box) and `xqt-eraser-stroke` (a small eraser over a stroke: erase whole strokes).

`xqt-sliders` is Lucide's `sliders-horizontal`: the library's **View** button (how the cards are shown).
