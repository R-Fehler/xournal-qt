/*
 * xournal-qt: the fill color of the pen (qt/pen-styles).
 *
 * Upstream fills a stroke or a shape with its own color, at the tool's fill opacity (Tool fill / fill alpha, in its
 * tool settings). The author wants another color too: the pen keeps one in our settings ("xournalQt" /
 * "penFillColor", "#rrggbb"; empty: the stroke's color), and a stroke begun with it gets it
 * (Stroke::getFillColor, saved as xqt-fill-color; qt/docs/decisions/0002-upstream-seams.md). The highlighter fills with
 * its own color only: upstream draws a filled highlighter through a mask in the stroke's color.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include "control/ToolEnums.h"
#include "util/Color.h"

class Settings;
class Stroke;
class ToolHandler;

namespace xqt::penfill {

/// The tool can fill with a color of its own (the pen)
bool hasOwnColor(ToolType type);
/// The fill color of the tool (none: the stroke's color)
std::optional<Color> color(Settings& settings, ToolType type);
void setColor(Settings& settings, ToolType type, std::optional<Color> c);
/// A stroke just begun with the tool in hand (before its views are made): the tool's fill color, if it fills
void apply(Settings& settings, const ToolHandler& tools, Stroke& stroke);

}  // namespace xqt::penfill
