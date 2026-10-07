/*
 * xournal-qt: page colors beyond white and textured paper (qt/docs/features/dark-pages.md, "Page colors").
 *
 * The page's color is upstream's background color (Xournal++ shows it). Textured paper is a key of upstream's page
 * type config, `xqt-texture=paper` (upstream keeps the keys it does not know and writes them back; Xournal++ shows the
 * plain color): a subtle grain drawn over the background wherever a page is drawn (canvas, thumbnails, exports,
 * printing), always the same (no randomness: the same page prints the same), as a small repeated mask.
 *
 * Ruling on colored paper: the lines take a color of the paper's hue a step lighter (dark paper) or darker (light
 * paper), as upstream's own config keys f1/af1 (and f2/af2, the margin line of lined paper), so Xournal++ draws them
 * the same.
 *
 * Qt-free (the render library).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include <cairo.h>

#include "util/Color.h"

class XojPage;

namespace xqt::paper {

/// The page type config's key of the texture, and its value for paper grain
constexpr const char* TEXTURE_KEY = "xqt-texture";
constexpr const char* TEXTURE_PAPER = "paper";

/// The curated page colors (the author: black, dark grey, grey, illustration paper, kraft, a soft green and blue)
struct Swatch {
    const char* id;
    uint32_t rgb;  ///< 0xRRGGBB
};
const std::vector<Swatch>& swatches();

/// Whether a page type config asks for textured paper
bool textured(const std::string& config);
/// The config with or without the texture
std::string withTexture(const std::string& config, bool on);
/// The config with ruling colors that stay visible on this paper (none on white: upstream's own)
std::string withLineColors(const std::string& config, Color paper);
/// The config without what the fork adds (texture, the ruling colors it chose): to tell page types apart
std::string baseConfig(const std::string& config);

/// Paper this dark wants light ink (relative luminance below 0.18, as ColorPalettes::isDarkPaper)
bool isDark(Color paper);
/// The ruling's color on this paper (for f1/af1)
Color lineColor(Color paper);

/// Draws the texture over the background (page coordinates, `width` x `height`), for paper of this color
void drawTexture(cairo_t* cr, Color paper, double width, double height);

/// Pages with textured paper show it everywhere they are drawn (xoj::view::backgroundDecorator)
void install();

}  // namespace xqt::paper
