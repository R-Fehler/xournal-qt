/*
 * xournal-qt: dark pages (qt/docs/features/dark-pages.md): pages shown dark without drawing them again.
 *
 * A view setting (⋮ → View → Dark pages). The page's picture stays what it is; where the canvas composes its tiles a
 * shader (or, on the software renderer, the same table on the CPU) turns every color into its dark equivalent:
 *  - colors of the palettes' roles (palettes.json) become the Dark palette's color of the same role: a pen in Classic's
 *    "warnings" red shows in Dark's red; a highlighter in a role's highlight color shows in Dark's highlight color at
 *    the opacity the palettes give dark paper (0.8 instead of upstream's 0.47);
 *  - every other color keeps its hue and chroma and flips its perceptual lightness (OKLab L): white paper becomes the
 *    Dark palette's background, black ink its body ink, mid tones are lifted so colored ink stays readable, pale
 *    tints (highlighters, colored boxes) become a visible marker instead of a near-black shade.
 * All of this is one table (LUT x LUT x LUT colors, trilinear), made once on the CPU: the shader does one lookup.
 *
 * A page whose paper is dark already is shown as it is. A page whose paper is a light color (cream, kraft) is first
 * balanced to white by its paper (so the paper itself becomes the dark background). Pictures (images on the page, the
 * pictures of a PDF page) keep their colors: the rectangles to keep are given per tile. The file never changes.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QRgb>
#include <QString>
#include <cstdint>
#include <vector>

namespace xqt::dark {

/// The table's size per channel (33: the grid includes 0 and 1, so white and black are exact)
constexpr int LUT = 33;

/// A palette role's color on light paper and its equivalent on dark paper. `opacity`: how much more opaque it shows
/// (the highlighter: upstream draws it at 0.47; the palettes want 0.8 on dark paper; ink: 1)
struct RolePair {
    QRgb light;
    QRgb dark;
    double opacity = 1.0;
};
/// The pairs the mapping knows (the app: ColorPalettes::darkPairs(), at start). UI thread; the table is made anew.
void setRoles(std::vector<RolePair> pairs);
const std::vector<RolePair>& roles();

/// The color white paper becomes (the Dark palette's background)
QRgb darkPaper();
/// The color black ink becomes (the Dark palette's body ink)
QRgb lightInk();

/// A color as a dark page shows it (exact; the table is made of these). Opaque colors, on white paper.
QRgb map(QRgb c);
/// The same without the palette roles: lightness flipped, hue and chroma kept
QRgb mapGeneric(QRgb c);

/// The table as the shader reads it: LUT slices (blue) side by side, each LUT x LUT (red across, green down);
/// RGBA8888. Made once per set of roles.
const QImage& table();
/// Changes when the table does (new roles)
quint64 tableGeneration();
/// A color looked up in the table as the shader does (trilinear), after balancing it by `paper` (white: none).
QRgb lookup(QRgb c, QRgb paper = 0xffffffff);

/// Whether a page with this paper is shown dark (its paper is not dark already: relative luminance at least 0.18,
/// ColorPalettes::isDarkPaper)
bool turnsDark(QRgb paper);
/// The paper of a picture of a page (a thumbnail): the color most of its corners have (white if they disagree)
QRgb paperOfImage(const QImage& img);

/// Turns an image dark in place (ARGB32 premultiplied or RGB32; others are converted): every pixel except those in
/// `keep` (pixel rectangles). `paper`: the page's paper (balanced to white first). Transparent pixels stay; partly
/// transparent ones keep their alpha.
void apply(QImage& img, QRgb paper = 0xffffffff, const std::vector<QRect>& keep = {});

/// A picture's address (a thumbnail's, a sketch's) asks for it dark: it ends in "~dark", which this takes off
bool takeSuffix(QString& id);

/// Pictures covering more than this share of a page are not kept: a scanned page is one picture, and it is turned
/// dark like any page.
constexpr double SCAN_SHARE = 0.6;
/// Of the pictures on a page (page coordinates), those kept as they are: not a scan (SCAN_SHARE), at most `max` (the
/// rest merged into the last one's bounds)
std::vector<QRectF> keptPictures(std::vector<QRectF> pictures, double pageWidth, double pageHeight, size_t max = 8);

}  // namespace xqt::dark
