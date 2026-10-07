# Color palettes (qt/color-palettes)

The author's role-based palettes as tabs in the color chooser, next to the colors of the tool bar and "Add a color…"
(the picker with the hex field). The spec is kept verbatim in
[`qt/resources/palettes/palettes.json`](../../resources/palettes/palettes.json) (sources and licenses: its
[README](../../resources/palettes/README.md)).

## The model (`qt/src/shell/ColorPalettes.h`)

- Six palettes: Classic, Marker (Open Color, MIT), Pastel study, Colorblind-safe (8) (Okabe-Ito + Paul Tol),
  Colorblind-safe (6) (Okabe-Ito), Dark.
- Eight **roles** with names for people: Body, Warnings, Key terms, Examples, Definitions, Headings, Questions, Ideas.
  Every palette uses the same keys, so a role keeps its meaning when the palette changes. A palette may leave roles
  out; the chooser shows only those it defines (Colorblind-safe (6): no definitions, no ideas).
- Each role has an **ink** color (pen, text box) and a **highlight** color (highlighter).
- **Highlighter opacity:** 0.5 on light paper, 0.8 on dark paper. "Dark" is judged from the current page's background
  color, not from the palette's mode: paper whose relative luminance (WCAG) is below about 0.18, where white text would
  have more contrast on it than black text (`ColorPalettes::isDarkPaper`).
- The JSON is compiled in as a Qt resource (`:/xqt-palettes/palettes.json`); a broken spec is refused whole.

## The setting and the API (`AppController`)

- `colorPalettes` (the palettes for QML), `colorPalette` (the chosen one; setting `colorPalette` in the `xournalQt`
  part of upstream's settings, default the first, "classic"). Settings → Pen → Colors chooses it.
- `paperColor()`, `highlighterOpacity()`: the current page's paper and the opacity rule on it.
- **A color taken from a palette remembers its role**: `setPaletteColor(palette, role)` gives the tool in hand the
  role's color (ink; the highlight color with the highlighter) and stores `palette:role` for that tool (setting
  `colorRoles`: `pen=marker:warnings;highlighter=classic:keyTerms`; tools: pen, highlighter, text). A role counts only
  while the tool still has that color; `setColor` (a color of one's own) clears it. `colorRole` / `colorRoleOf(tool)`
  read it.
- **Following a palette switch**: choosing a palette (in Settings, or by taking a color from another palette's tab)
  gives every tool whose color came from a palette its role's color in the new palette. A palette that leaves the role
  out keeps the color (and the role, so it follows again later).
- For tool presets (`qt/toolbox`): `paletteColor(palette, role, highlight)` and `followPalette(ref, palette,
  highlight)` (invalid: the palette has no such role, keep the color). A preset stores `palette:role` next to its color
  and asks `followPalette` when `colorPaletteChanged` fires.

## The chooser (`ColorChooser.qml`)

Since 0.8.0 the colors are chosen in a tool's **editor** (the toolbox, [toolbox.md](toolbox.md)): the palette (a combo
box; app-wide, as Settings → Pen → Colors), its roles with their names (the ink, or with a highlighter the highlight
color), the colors used lately, a hex code and the picker; a bottom sheet in the phone classes. Settings → Pen → Colors
shows the chosen palette's colors and its credits (`source`, `colorPaletteSource`).

The classic tool bar's chooser (`ColorChooser.qml`: a popup with a tab per palette, from the bar's colors and the pen
pill of the compact chrome) was removed with the classic tool bar in 0.8.0.

## Dark pages and dark paper ([dark-pages.md](dark-pages.md))

- **Dark pages** (a view setting) show each role's colors of the light palettes as the **Dark** palette's colors of the
  same role: `ColorPalettes::darkPairs()` gives the pairs (ink → ink; highlight → highlight with `opacity = 0.8 / 0.47`,
  the dark-paper rule over upstream's 0.47), the canvas's table maps them; other colors flip their lightness and keep
  their hue.
- **On dark paper** (a page color whose luminance is below 0.18) a highlighter is drawn at 0.8 and lightens
  (`CAIRO_OPERATOR_SCREEN`) instead of multiplying (which showed nothing on black paper): the opacity follows the
  paper of the page being drawn (`view/PaperTone.h`), so no opacity per stroke is needed. On light paper it stays
  upstream's 0.47, multiplied.
- **A new document on dark paper** takes the Dark palette (the tools with a role follow; a pen of one's own that does
  not read on it gets the body ink); one on light paper goes back to the light palette chosen before.

## What is not done

- On light paper strokes keep upstream's highlighter opacity (0.47, not the spec's 0.5): the difference is small, and
  the same file looks the same in Xournal++.
- The tool bar's own colors do not follow a palette (they are colors, not roles).
