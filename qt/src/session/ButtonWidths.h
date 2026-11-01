/*
 * xournal-qt: the width of the eraser the pen's side buttons and its eraser end give (Settings → Pen, next to what
 * each button does).
 *
 * Upstream gives a button's tool one of the five sizes or nothing at all ("no change": the tool bar eraser's size and
 * kind, see ButtonConfig). The eraser of a side button is then the tool bar eraser's size whatever the tool bar's
 * eraser looks like now, and the tool bar's eraser has a width of its own besides the five sizes (ToolHandler's
 * custom width, which the toolbox writes when an eraser entry is taken). So the author could not set the size of the
 * eraser a side button gives.
 *
 * Here each of these three buttons has a width of its own in the xournalQt part of the settings ("eraserWidth",
 * "stylusWidth", "stylus2Width": points, or nothing for "the tool bar eraser's", which is what the buttons had
 * before). It is the eraser in hand while the button is held (and, for the eraser end, after it touched), and what its
 * pointer shows while it hovers (HoverPointer.h).
 *
 * It is the tool's own width (upstream's custom width, as an eraser taken from the toolbox has one), not one of its five
 * sizes: picking another size for the tool bar eraser leaves it as it is (ToolHandler::setButtonSize and
 * ButtonConfig's "no change" only touch the sizes).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <array>

#include "control/settings/SettingsEnums.h"

class Settings;
class ToolHandler;

namespace xqt::buttonwidths {

/// The smallest and the largest width a button's eraser may have (points), as a tool's own width in the toolbox
constexpr double MIN_WIDTH = 0.1;
constexpr double MAX_WIDTH = 150;

/// The width a button's eraser gets when it is given one: the tool bar eraser's medium size (8.5 pt, 3 mm), the
/// eraser of the toolbox's first start
constexpr double DEFAULT_WIDTH = 8.5;

/// The three buttons of the pen that erase: its eraser end, the lower and the upper side button
inline constexpr std::array<Button, 3> BUTTONS{BUTTON_ERASER, BUTTON_STYLUS_ONE, BUTTON_STYLUS_TWO};

/// The width the button's eraser has (points); 0: the tool bar eraser's (none set)
double width(Settings& settings, Button button);
/// Sets it (0: the tool bar eraser's again; else kept within MIN_WIDTH and MAX_WIDTH), writes the settings, and gives
/// the button's tool of this session the width at once. False (and nothing changes) for another button.
bool setWidth(Settings& settings, ToolHandler& tools, Button button, double points);

/// The button's tool has a width of its own (0: the tool bar eraser's again)
void applyToTool(Settings& settings, ToolHandler& tools, Button button, double points);

/// Gives the buttons' tools their widths again (at the start, and after the settings of a button changed)
void apply(Settings& settings, ToolHandler& tools);

}  // namespace xqt::buttonwidths
