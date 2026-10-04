# palettes.json

The color palettes of the color chooser (`qt/src/shell/ColorPalettes.cpp`): Classic, Marker, Pastel study,
Colorblind-safe (8), Colorblind-safe (6) and Dark. The file is the author's spec of 2026-10-04, kept verbatim. It is
compiled into the app as a Qt resource (`:/xqt-palettes/palettes.json`, `XqtApp.cmake`); nothing reads this file at
run time.

- **Roles:** every palette uses the same role keys (body, warnings, key terms, examples, definitions, headings,
  questions, ideas), so a role keeps its meaning when the palette changes. A palette may leave roles out
  (Colorblind-safe (6) has no definitions and no ideas). Each role has an `ink` color (pen, text) and a `highlight`
  color (highlighter).
- **Highlighter opacity:** 0.5 on light paper, 0.8 on dark paper (judged from the page's background color).

## Sources and licenses

- **Marker:** [Open Color](https://yeun.github.io/open-color/) by heeyeun, MIT license (Copyright (c) 2016 heeyeun).
  Ink = step 8, highlight = step 2; the key terms highlight is yellow 3. Open Color's red and pink highlights are
  nearly identical, so violet is used for questions and grape for ideas; yellow, not orange, is the key terms
  highlight because the orange and red highlights were too close.
- **Colorblind-safe (8):** the Okabe-Ito colors (Masataka Okabe and Kei Ito, "Color Universal Design", 2008) plus
  indigo and wine from Paul Tol's color schemes (https://personal.sron.nl/~pault/), some inks darkened for legibility,
  highlights derived. Okabe-Ito has only six colors that work as thin ink (sky blue and yellow are too light). The
  weakest pair is questions vs ideas, which is why the 6-color palette is the safer choice.
- **Colorblind-safe (6):** Okabe-Ito, with the original Okabe-Ito blue for headings; no definitions and no ideas.
- **Classic, Pastel study, Dark:** the author's own. Pastel study's highlights are the least distinct of all palettes
  by design: the trade-off for the soft look.

Color values are facts, not code; the MIT notice of Open Color is repeated in `qt/packaging/copyright`.
