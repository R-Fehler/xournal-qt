#include "HoverPointer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <QColor>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPainter>
#include <QPen>
#include <QVersionNumber>

#include "control/Tool.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/Settings.h"
#include "control/settings/SettingsEnums.h"

namespace xqt::hover {

namespace {
constexpr const char* SETTING = "hoverPointer";
/// The dot: a dark core with a light ring, so it shows on white and on dark paper alike
const QColor DOT_CORE(0x1d, 0x2b, 0x8f);
constexpr double DOT_CORE_RADIUS = 2;
constexpr double DOT_RADIUS = 3;
/// Room around the eraser's outline for its light halo (logical pixels a side)
constexpr int ERASER_MARGIN = 3;

std::optional<bool>& penCursorOverride() {
    static std::optional<bool> shows;
    return shows;
}

std::optional<EraserMark> markOf(double thickness, EraserType type, double zoom) {
    if (thickness <= 0 || zoom <= 0) {
        return std::nullopt;
    }
    EraserMark mark;
    // Upstream's eraser is a square reaching `thickness` from the pointer (EraseHandler, halfEraserSize), also when it
    // deletes whole strokes; the whiteout eraser draws a white stroke `thickness` wide (a round brush)
    mark.round = type == ERASER_TYPE_WHITEOUT;
    mark.wholeStrokes = type == ERASER_TYPE_DELETE_STROKE;
    mark.size = std::max(MIN_ERASER_PX, (mark.round ? thickness : 2 * thickness) * zoom);
    return mark;
}
}  // namespace

Pointer pointerSetting(Settings& settings) {
    std::string v;
    settings.getCustomElement("xournalQt").getString(SETTING, v);
    return v == "crosshair" ? Pointer::Crosshair : Pointer::Dot;
}

void setPointerSetting(Settings& settings, Pointer pointer) {
    settings.getCustomElement("xournalQt").setString(SETTING, pointer == Pointer::Crosshair ? "crosshair" : "dot");
    settings.customSettingsChanged();
}

std::optional<EraserMark> eraserMark(ToolHandler& tools, Settings& settings, bool eraserEnd, double zoom) {
    if (eraserEnd) {
        // The eraser end takes its button's tool when it touches (CanvasInput::changeTool): until then the active
        // tool is still the pen's. Its eraser has the tool bar eraser's size and kind ("no change" in the settings).
        const ButtonConfig* cfg = settings.getButtonConfig(static_cast<unsigned int>(Button::BUTTON_ERASER));
        const ToolType action = cfg ? cfg->getAction() : TOOL_NONE;
        if (action != TOOL_NONE && action != TOOL_ERASER) {
            return std::nullopt;
        }
        if (action == TOOL_ERASER && tools.getToolType() != TOOL_ERASER) {
            const Tool& eraser = tools.getTool(TOOL_ERASER);
            const double custom = tools.getCustomThickness(TOOL_ERASER);
            const double thickness = tools.isCustomThicknessActive(TOOL_ERASER) && custom > 0
                                             ? custom
                                             : eraser.getThickness(eraser.getSize());
            return markOf(thickness, eraser.getEraserType(), zoom);
        }
    }
    if (tools.getToolType() != TOOL_ERASER) {
        return std::nullopt;
    }
    return markOf(tools.getThickness(), tools.getEraserType(), zoom);
}

int dotSide() { return 2 * static_cast<int>(std::ceil(DOT_RADIUS + 1)); }

int eraserSide(const EraserMark& mark) {
    const int side = static_cast<int>(std::ceil(mark.size)) + 2 * ERASER_MARGIN;
    return side + side % 2;
}

bool eraserFitsCursor(const EraserMark& mark, double dpr) {
    return std::ceil(eraserSide(mark) * dpr) <= MAX_CURSOR_PX;
}

void paintDot(QPainter& p, QPointF center) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(HALO);
    p.drawEllipse(center, DOT_RADIUS, DOT_RADIUS);
    p.setBrush(DOT_CORE);
    p.drawEllipse(center, DOT_CORE_RADIUS, DOT_CORE_RADIUS);
    p.restore();
}

void paintEraser(QPainter& p, const EraserMark& mark, QPointF center) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF shape(center.x() - mark.size / 2, center.y() - mark.size / 2, mark.size, mark.size);
    const auto draw = [&] {
        if (mark.round) {
            p.drawEllipse(shape);
        } else {
            p.drawRect(shape);
        }
    };
    // A light halo under the line (dark paper), a faint gray inside, the gray outline (dashed: whole strokes)
    p.setBrush(ERASER_FILL);
    p.setPen(QPen(HALO, HALO_WIDTH));
    draw();
    p.setBrush(Qt::NoBrush);
    QPen line(ERASER_LINE, LINE_WIDTH);
    if (mark.wholeStrokes) {
        line.setDashPattern({DASH / LINE_WIDTH, DASH / LINE_WIDTH});
        line.setCapStyle(Qt::FlatCap);
    }
    p.setPen(line);
    draw();
    p.restore();
}

namespace {
QImage picture(int side, double dpr) {
    const int px = static_cast<int>(std::ceil(side * dpr));
    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);
    return image;
}
}  // namespace

QImage dotImage(double dpr) {
    QImage image = picture(dotSide(), dpr);
    QPainter p(&image);
    paintDot(p, QPointF(dotSide() / 2.0, dotSide() / 2.0));
    return image;
}

QImage eraserImage(const EraserMark& mark, double dpr) {
    const int side = eraserSide(mark);
    QImage image = picture(side, dpr);
    QPainter p(&image);
    paintEraser(p, mark, QPointF(side / 2.0, side / 2.0));
    return image;
}

bool platformShowsPenCursor() {
    if (penCursorOverride()) {
        return *penCursorOverride();
    }
    static const bool shows = [] {
        if (const QByteArray env = qgetenv("XQT_PEN_CURSOR"); !env.isEmpty()) {
            return env != "0";
        }
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("xcb") || platform == QLatin1String("windows") ||
            platform == QLatin1String("cocoa")) {
            return true;
        }
        if (platform.startsWith(QLatin1String("wayland"))) {
            // (the tablet tool's cursor: the window's cursor where the pen is, from Qt 6.7 on)
            return QLibraryInfo::version() >= QVersionNumber(6, 7);
        }
        return false;
    }();
    return shows;
}

void setPlatformShowsPenCursorForTests(std::optional<bool> shows) { penCursorOverride() = shows; }

}  // namespace xqt::hover
