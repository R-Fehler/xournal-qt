#include "ButtonWidths.h"

#include <algorithm>
#include <array>
#include <string>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/Settings.h"

namespace xqt::buttonwidths {

namespace {
constexpr const char* ELEMENT = "xournalQt";

std::string keyOf(Button button) {
    switch (button) {
        case BUTTON_ERASER:
            return "eraserWidth";
        case BUTTON_STYLUS_ONE:
            return "stylusWidth";
        case BUTTON_STYLUS_TWO:
            return "stylus2Width";
        default:
            return {};
    }
}

double clampWidth(double points) { return std::clamp(points, MIN_WIDTH, MAX_WIDTH); }
}  // namespace

double width(Settings& settings, Button button) {
    const std::string key = keyOf(button);
    if (key.empty()) {
        return 0;
    }
    double points = 0;
    if (!settings.getCustomElement(ELEMENT).getDouble(key, points) || points <= 0) {
        return 0;  // (none: the tool bar eraser's)
    }
    return clampWidth(points);
}

void applyToTool(ToolHandler& tools, Button button, double points) {
    // (the tool's width of its own, as an eraser of the toolbox has one: the five sizes never override it, and picking
    // another size for the tool bar eraser cannot make the button's eraser smaller either)
    tools.setButtonCustomThickness(button, points, points > 0);
}

void apply(Settings& settings, ToolHandler& tools) {
    for (const Button button: BUTTONS) {
        const double points = width(settings, button);
        if (points <= 0) {
            continue;
        }
        const ButtonConfig* cfg = settings.getButtonConfig(static_cast<unsigned int>(button));
        if (!cfg || cfg->getAction() != TOOL_ERASER) {
            continue;  // (the button does not erase: nothing to size)
        }
        applyToTool(tools, button, points);
    }
}

bool setWidth(Settings& settings, ToolHandler& tools, Button button, double points) {
    const std::string key = keyOf(button);
    if (key.empty()) {
        return false;
    }
    // (the tool bar eraser's width goes in place of the button's: its tool takes the five sizes again, as when a new
    // settings file is made; without a width of its own the button's tool is the tool bar eraser's, as before)
    const ButtonConfig* cfg = settings.getButtonConfig(static_cast<unsigned int>(button));
    if (points > 0) {
        const double kept = clampWidth(points);
        settings.getCustomElement(ELEMENT).setDouble(key, kept);
        if (cfg && cfg->getAction() == TOOL_ERASER) {
            applyToTool(tools, button, kept);
        }
    } else {
        settings.getCustomElement(ELEMENT).attributes().erase(key);
        // (the button's tool is made anew: a tool in hand that is that very button's must not be left pointing at the
        // old one, which goes away here)
        tools.pointActiveToolToToolbarTool();
        tools.resetButtonTool(cfg ? cfg->getAction() : TOOL_NONE, button);
    }
    settings.customSettingsChanged();
    return true;
}

}  // namespace xqt::buttonwidths
