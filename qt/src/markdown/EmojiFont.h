/*
 * xournal-qt: colour emoji in the text drawn with Pango and Cairo (Markdown, text boxes, the PDF export).
 *
 * The app brings its own colour emoji font (qt/resources/fonts/XqtEmoji.ttf: Noto Color Emoji renamed, CBDT bitmaps,
 * which every Cairo in use can draw; COLRv1 needs Cairo 1.18 and Ubuntu 22.04 has 1.16). It is added to fontconfig
 * for this process only (nothing is installed), and a rule puts it first for the emoji Pango finds in a text.
 *
 * Cairo puts a colour glyph into a PDF as an image of the size it has on a 72 dpi page (about 16 pixels for 12 pt
 * text): blurred in any viewer and on paper. showLayout() draws them into vector surfaces (PDF, PostScript, SVG)
 * as images of the font's own resolution instead.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include <cairo.h>
#include <pango/pango.h>

namespace xqt::emoji {

/// The family name of the font that comes with the app.
constexpr const char* FONT_FAMILY = "Xournal Qt Emoji";
/// Its file name in the fonts folder of the resources (<resources>/fonts).
constexpr const char* FONT_FILE = "XqtEmoji.ttf";

/// Adds the font file to fontconfig's current configuration (this process only), with the rule that prefers it for
/// emoji (fontconfigRules()). Call it before the first text is laid out. Returns false if the file cannot be read.
bool registerFont(const std::string& fontFile);
/// Whether registerFont() added the font.
bool fontRegistered();

/// The rule of registerFont() as lines of a fonts.conf, for the platforms that write their own (Windows, Android;
/// they list the font's folder as a `<dir>`): emoji in the font's family first, and, with `scaleBitmaps`,
/// fontconfig's 10-scale-bitmap-fonts rule (bitmap glyphs drawn at the size asked, not at their own size; Android
/// has no conf.d).
std::string fontconfigRules(bool scaleBitmaps);

/// pango_cairo_show_layout(), with colour glyphs as sharp images on vector surfaces (see above). The layout is drawn
/// at the current point (or at 0, 0 without one), as pango_cairo_show_layout() does.
void showLayout(cairo_t* cr, PangoLayout* layout);

/// Whether a font of a laid-out text has colour glyphs (fontconfig's "color").
bool isColorFont(PangoFont* font);

}  // namespace xqt::emoji
