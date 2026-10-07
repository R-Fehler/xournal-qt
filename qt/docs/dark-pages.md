# Dark pages and page colors (qt/dark-pages)

The author (round of 2026-10-05 evening): dark mode for pages, inverted on the GPU with pictures kept, ink and
highlighter shown as their dark equivalents (palette roles) and readable in both; curated page colors (black, grey,
illustration paper, textured paper) for `.xopp` and PDFs, with a printing warning for dark pages. Idea A5 of
[the ideas of 2026-10](history/README.md).

Two things that look alike and are not:

- **Dark pages** is a way of *showing* pages (a view setting). The document does not change; exports and printing
  stay as the document is.
- **Page colors** are what the pages *are*: upstream's background color, saved in the file, printed and exported.

## Dark pages

⋮ → View → Dark pages: Off, On, or "With the system's dark mode" (setting `darkPages` in the `xournalQt` part of the
settings: `off`, `on`, `system`; one setting for all windows; `system` follows `QStyleHints::colorScheme`).
`AppController::darkPagesMode` / `darkPagesShown`, the canvas item's `darkPages` property.

What is shown dark: the canvas of each window (the notes and the reference beside them) and the page pictures of the
lists (sidebar, page grid, overview: `PagePicture.qml` asks the providers for `…~dark`). Not: the audience's screen of
the presenter view (it shows the document as it is), exports, printing, sharing.

### How (no page is drawn again)

The page's picture is the one the canvas has (`PageRaster`); where `DocumentCanvasItem` composes its tiles, a shader
(`qt/src/quick/shaders/darktile.frag`, `DarkTileMaterial`) looks each pixel up in a table and writes its dark
equivalent. Toggling dark pages only changes the tiles' material: no render, no upload (the pixel test checks that
`PageRaster::stats().renders` does not change). On the software renderer (no shaders) and in builds without Qt Shader
Tools, the same table is applied on the CPU when a tile is composed (`dark::apply`); toggling then composes the tiles
in view again (still no page render).

The table (`qt/src/canvas/DarkPages.{h,cpp}`) is 33 × 33 × 33 colors (the grid has 0 and 1, so white and black are
exact), looked up trilinearly, made once on the CPU (about 30 ms). Each entry is `dark::map(color)`:

1. **Palette roles.** A color on the way from white to a role's color of a light palette (`ColorPalettes::darkPairs`:
   every role of Classic, Marker, Pastel study, Colorblind-safe 8 and 6) becomes the Dark palette's color of the same
   role, at the same share: antialiased edges stay smooth. Classic's warnings red shows as Dark's warnings red, a
   pastel heading as Dark's heading blue. Two roles' colors on one line from white (two greys) go to the one the color
   is.
2. **Highlighters.** A role's highlight color shows as Dark's highlight color at 0.8 (the palettes' opacity on dark
   paper) where upstream drew it at 0.47 on white: the pair carries `opacity = 0.8 / 0.47`.
3. **Every other color** keeps its hue and chroma (OKLab) and flips its lightness: white becomes the Dark palette's
   background (#1E1F22, with its tint near the paper), black its body ink (#ECECEC); mid tones are lifted
   (`L' = Ld + (Li − Ld)·t(2 − t)`, t = 1 − L) so colored ink stays readable on the dark paper; out of gamut, the
   chroma is reduced until it fits.
4. **Pale tints with chroma** (a highlighter of one's own on white, a colored box in a PDF) are lifted to a visible
   marker (`L ≥ Ld + 1.8·C`, at most 0.62) instead of becoming a shade of the dark paper: no "multiply to black".

A page whose paper is already dark (relative luminance below 0.18, `ColorPalettes::isDarkPaper`) is shown as it is.
A PDF page's paper is what its picture shows in most of its corners, so dark slides stay as they are. A page of a light
color (illustration paper, kraft) is first balanced to white by its paper (a uniform in the shader), so the paper
becomes the dark background too.

### Pictures kept

The rectangles to keep are a uniform of each tile (at most 8 per tile; more are merged): there the shader leaves the
pixel as it is.

- Images of the document (upstream's Image elements on visible layers): from the document, kept per page revision.
- Pictures of a PDF page (its raster images): poppler-glib's image mapping (`PdfPictures`, `PagePictures.h`), read on
  one background worker at the lowest priority with a poppler instance of its own, only for pages shown dark; until a
  page's pictures are known it is shown dark whole. Owner: the `CanvasView` (a few rectangles per PDF page, at most the
  PDF's pages); the document lock is never held while poppler reads.
- A picture covering more than 60 % of the page is a scan: it is turned dark like any page (`dark::SCAN_SHARE`).
- Ink drawn over a kept picture keeps its light colors there. Vector figures of a PDF are not pictures: they turn dark.
- LaTeX formulas (TexImage) are not kept: they are black text.
- Thumbnails and sketches are turned dark whole (pictures included): they are small.

### Ink in progress, the selection, stand-ins

The stroke being drawn is composed into its tiles and passes the same shader. The selection's picture (moved or
turned) is turned dark on the CPU when it is drawn (it is drawn only when it changes). A page's stand-in (shown before
it is rendered) gets the same material; the white placeholder becomes the dark paper.

## Page colors

Where pages get their background (the background dialog, a new document, Insert pages, Settings → New pages) a row of
swatches (`PaperSwatches.qml`, `AppController::paperSwatches`): white, illustration paper (#F2E6CB, the warm cream of
sketch paper), kraft (#C9A77C), soft green (#E3ECD9), soft blue (#DDE7F0), grey (#5C5F66), dark grey (#2B2D31) and
black (#161616, not pure black: ink has room to be darker), and a switch **Textured paper**. A color none of them has
(set in Xournal++) is shown as one more swatch. The pattern previews are drawn on the chosen paper.

- **The color** is upstream's background color (`<background type="solid" color="#161616ff" …>`): Xournal++ shows it.
- **The ruling** stays visible: on a paper other than white the lines take a color of the paper's hue a step darker
  (light paper) or lighter (dark paper), as upstream's own config keys `f1`/`af1` (and `f2`/`af2`, the margin line of
  lined paper, a muted red), with `xqt-lines=1` saying they are the fork's (so a page type's own colors are kept, and
  choosing another paper replaces them). Xournal++ draws the same lines.
- **Textured paper** is `xqt-texture=paper` in upstream's page type `config` (Xournal++ keeps keys it does not know and
  writes them back; it shows the plain color). The texture is a small grain (fine tooth, a little mottling, long
  fibres) as an A8 mask of 256 pixels repeated every 120 points, darker specks on light paper and lighter ones on dark
  paper, at 8 % and 7 %: subtle. Drawn wherever a page is drawn (canvas, thumbnails, PDF and PNG exports, printing,
  the CLI) through upstream's background views (`xoj::view::backgroundDecorator`, a seam: ADR 0002). Deterministic
  (hashed noise, no random numbers): a page prints the same every time. In a PDF the grain is one small image mask.
- Page types are compared without these keys (`paper::baseConfig`): a graph page on black paper is still "Graph".
- **PDF pages** (a PDF's own pages) have no paper of their own: the dialog's paper applies to the pages that get a
  generated background; a PDF page that is given a pattern loses its PDF page, as before. Space for notes around a
  slide stays white.
- New pages copy the paper of the page they follow (upstream's "copy the current page"); the settings' paper is the
  one of new documents and of Insert pages without a page to follow.

### Ink on dark paper

- **A new document on dark paper** takes the Dark palette (`AppController::inkForPaper`): the tools whose colors came
  from a palette role follow it (the highlighter's key terms yellow becomes Dark's), a pen or text color of one's own
  that does not read on the paper (contrast below 3:1) becomes the Dark palette's body ink. A new document on light
  paper goes back to the light palette chosen before (setting `lightPalette`). Existing documents and changing a
  page's color later do not change the tools.
- **The highlighter on dark paper** lightens instead of darkening (upstream multiplies: on black paper a highlight is
  invisible): `CAIRO_OPERATOR_SCREEN` at 0.8, the palettes' opacity on dark paper (`view/PaperTone.h`, a seam: the
  page's drawer says which paper it draws on; every view, export and the stroke being drawn). Xournal++ still
  multiplies such a highlight (it shows nothing on black paper); the file is the same.

## Printing

The print dialog says "Some of these pages have dark paper: printing them uses a lot of ink…" when the pages printed
have a dark page color (`AppController::printUsesDarkPaper`, the banner `darkPaperWarning`). There is no "print with
white pages": ink chosen for dark paper (light grey, the Dark palette) would not show on white, and turning the ink
dark for printing needs every stroke drawn in other colors (a renderer seam), not cheap. Exports are not warned
about (they are for screens).

## Builds

Qt Shader Tools (`qt6-shadertools-dev` and `qt6-shader-baker` on Debian and Ubuntu, `qt6-shadertools` in MSYS2,
`qtshadertools` in Homebrew; part of aqt's and conda-forge's Qt) compile the shader (`qt_add_shaders`, `BATCHABLE`).
Without them the build says so and dark pages are drawn on the CPU.

## Tests

- `xqt-canvas-tests` `DarkPages.*`: white → the Dark background, black → its body ink; a role's color, its antialiased
  edge and its highlight at 0.8; hue kept and lightness flipped for other colors, greys monotonic, a pale highlighter
  visible; the table equals the mapping; apply keeps pictures; dark paper stays; a scan is not kept; a PDF page's
  pictures found by poppler (and through the worker).
- `xqt-quick-tests` `DarkPagesCanvas.*`: a page with black ink, a role's red, a highlighter and a picture, shown dark:
  every pixel is the light one through the table (±4), the picture keeps its colors, no page render; off again as it
  was; a page with dark paper as it is. Plain ctest on the software renderer; `DarkPagesCanvas.quick@gl` the same on
  OpenGL (Mesa llvmpipe under `xvfb-run`, `-platform offscreen:enable_glx`, `QT_QUICK_BACKEND=rhi`), asserting that
  the shader drew it.
- `xqt-shell-tests` `ColorPalettes.darkPages*`, `aNewDocumentOnDarkPaper…`, `pagesGetAPaperColorAndTexture`: every
  role of the light palettes shows as the Dark palette's color of the role; the setting; ink for a new document's
  paper; swatches, background and insert with paper, the print check.
- `xqt-session-tests` `PageColorsTest.*`: color and texture through a `.xopp` and a PDF with notes (the XML has
  upstream's color attribute and the texture in upstream's config), the grain subtle and the same on every draw,
  ruling visible on dark papers, a highlighter on dark paper visible and yellow (and upstream's on white).
- `xqt-ui-tests` `DarkPagesUiTest.*`: the View menu, the background dialog's swatches and texture, the print warning.

Device checks: [device-checklist.md](testing/device-checklist.md).
