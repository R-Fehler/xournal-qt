#include "HoverPointer.h"

#include <cmath>
#include <string>

#include <QColor>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPainter>
#include <QPen>
#include <QVersionNumber>

#include "control/settings/Settings.h"

namespace xqt::hover {

namespace {
constexpr const char* SETTING = "hoverPointer";
/// The dot: a dark core with a light ring, so it shows on white and on dark paper alike
const QColor DOT_CORE(0x1d, 0x2b, 0x8f);
const QColor HALO(255, 255, 255, 210);
constexpr double DOT_CORE_RADIUS = 2;
constexpr double DOT_RADIUS = 3;

std::optional<bool>& penCursorOverride() {
    static std::optional<bool> shows;
    return shows;
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

int dotSide() { return 2 * static_cast<int>(std::ceil(DOT_RADIUS + 1)); }

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
