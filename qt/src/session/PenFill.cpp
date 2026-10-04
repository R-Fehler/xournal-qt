#include "PenFill.h"

#include <cinttypes>
#include <cstdio>
#include <string>

#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/Stroke.h"

namespace xqt::penfill {

namespace {
const char* const CUSTOM = "xournalQt";
const char* const KEY = "penFillColor";
}  // namespace

bool hasOwnColor(ToolType type) { return type == TOOL_PEN; }

std::optional<Color> color(Settings& settings, ToolType type) {
    if (!hasOwnColor(type)) {
        return std::nullopt;
    }
    std::string text;
    if (!settings.getCustomElement(CUSTOM).getString(KEY, text) || text.size() != 7 || text[0] != '#') {
        return std::nullopt;
    }
    try {
        return Color(static_cast<uint32_t>(std::stoul(text.substr(1), nullptr, 16)) | 0xff000000U);
    } catch (...) {
        return std::nullopt;
    }
}

void setColor(Settings& settings, ToolType type, std::optional<Color> c) {
    if (!hasOwnColor(type)) {
        return;
    }
    std::string text;
    if (c) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%06" PRIx32, uint32_t(*c) & 0xffffffU);
        text = buffer;
    }
    settings.getCustomElement(CUSTOM).setString(KEY, text);
    settings.customSettingsChanged();
}

void apply(Settings& settings, const ToolHandler& tools, Stroke& stroke) {
    if (stroke.getFill() == -1 || !hasOwnColor(tools.getToolType())) {
        return;
    }
    stroke.setFillColor(color(settings, tools.getToolType()));
}

}  // namespace xqt::penfill
