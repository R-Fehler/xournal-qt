# Color palettes (qt/color-palettes)

The author's role-based palettes as tabs in the color chooser, next to the colors of the tool bar and "Add a color…"
(the picker with the hex field). The spec is kept verbatim in
[`qt/resources/palettes/palettes.json`](../resources/palettes/palettes.json) (sources and licenses: its
[README](../resources/palettes/README.md)).

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

One popup (a bottom sheet in the phone classes) with a tab bar: "Colors" (the colors one has, the ones used lately,
"Add a color…") and one tab per palette. A palette's tab shows its roles as small pieces of the current page's paper
with the color on it and the role's name under it (also the tool tip): the ink as a dot, or with the highlighter the
highlight color as a marker stroke at the opacity the rule gives on this page. The palette's credits (`source`) are
shown under it; a dark palette on light paper (or the other way round) says what paper it is made for.

It opens on the tab of the palette the color in hand came from, else on the tab shown last.

Where it is:
- the tool bar's colors (`ColorStrip.qml`): "+" in the full form, the palette button in the recent form, a long press
  on the cycling color button in the single form (the phone dock: as a sheet);
- the pen pill of the compact chrome (`PenPill.qml`): its "+", with the pill's colors in the first tab.

## What is not done

- Strokes are still drawn with upstream's fixed highlighter opacity (0.47, multiplied with the page), which is the
  light-paper rule. Drawing them at 0.8 on dark paper needs an opacity per stroke, which upstream's renderer and file
  format do not have (the color's alpha is discarded on load; `StrokeView` uses a constant). Left for the author to
  decide (a seam in `StrokeView`, `SaveHandler` and `XmlParserHelper`); the chooser already shows the rule.
- The tool bar's own colors do not follow a palette (they are colors, not roles).
