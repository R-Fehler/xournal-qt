#include "ScreenCalibration.h"

#include <cmath>
#include <map>
#include <string>

#include <QScreen>
#include <QStringList>
#include <QUrl>

#include "control/settings/Settings.h"

namespace xqt::ScreenCalibration {

namespace {
constexpr auto CUSTOM = "xournalQt";
constexpr auto KEY = "screenCalibration";

std::map<QString, double> readAll(Settings& settings) {
    std::string stored;
    settings.getCustomElement(CUSTOM).getString(KEY, stored);
    std::map<QString, double> all;
    for (const QString& entry: QString::fromStdString(stored).split(';', Qt::SkipEmptyParts)) {
        const qsizetype eq = entry.lastIndexOf('=');
        if (eq <= 0) {
            continue;
        }
        bool ok = false;
        const double ppi = entry.mid(eq + 1).toDouble(&ok);
        if (ok && ppi > 0) {
            all[QUrl::fromPercentEncoding(entry.left(eq).toUtf8())] = ppi;
        }
    }
    return all;
}

void writeAll(Settings& settings, const std::map<QString, double>& all) {
    QStringList entries;
    for (const auto& [key, ppi]: all) {
        entries << QString::fromLatin1(QUrl::toPercentEncoding(key)) + '=' + QString::number(ppi, 'f', 2);
    }
    settings.getCustomElement(CUSTOM).setString(KEY, entries.join(';').toStdString());
    settings.customSettingsChanged();
}
}  // namespace

QString keyOf(const QScreen* screen) {
    if (!screen) {
        return {};
    }
    // The serial tells two monitors of the same model apart; without it, the connector does. The size in millimetres
    // keeps a replaced panel on the same connector from taking over the old one's calibration.
    QStringList parts;
    for (const QString& part: {screen->manufacturer(), screen->model(),
                               screen->serialNumber().isEmpty() ? screen->name() : screen->serialNumber()}) {
        if (!part.trimmed().isEmpty()) {
            parts << part.trimmed();
        }
    }
    const QSizeF mm = screen->physicalSize();
    parts << QStringLiteral("%1x%2mm").arg(std::lround(mm.width())).arg(std::lround(mm.height()));
    return parts.join(' ');
}

Display displayOf(const QScreen* screen, double dpr) {
    Display d;
    if (!screen) {
        d.dpr = dpr > 0 ? dpr : 1.0;
        return d;
    }
    d.key = keyOf(screen);
    d.name = screen->model().isEmpty() ? screen->name() : screen->manufacturer() + ' ' + screen->model();
    d.name = d.name.trimmed();
    d.reportedDpi = screen->physicalSize().isEmpty() ? 0.0 : screen->physicalDotsPerInch();
    d.dpr = dpr > 0 ? dpr : screen->devicePixelRatio();
    return d;
}

bool plausible(double logicalDpi) {
    return std::isfinite(logicalDpi) && logicalDpi >= FALLBACK_DPI / 2 && logicalDpi <= FALLBACK_DPI * 3 &&
           std::abs(logicalDpi - 100.0) > 1e-6;
}

double defaultPpi(const Display& display) {
    const double dpr = display.dpr > 0 ? display.dpr : 1.0;
    return (plausible(display.reportedDpi) ? display.reportedDpi : FALLBACK_DPI) * dpr;
}

std::optional<double> storedPpi(Settings& settings, const QString& key) {
    if (key.isEmpty()) {
        return std::nullopt;
    }
    const auto all = readAll(settings);
    if (const auto it = all.find(key); it != all.end()) {
        return it->second;
    }
    return std::nullopt;
}

void store(Settings& settings, const QString& key, double ppi) {
    if (key.isEmpty() || !(ppi > 0)) {
        return;
    }
    auto all = readAll(settings);
    all[key] = ppi;
    writeAll(settings, all);
}

void forget(Settings& settings, const QString& key) {
    auto all = readAll(settings);
    if (all.erase(key) > 0) {
        writeAll(settings, all);
    }
}

double ppi(Settings& settings, const Display& display) {
    return storedPpi(settings, display.key).value_or(defaultPpi(display));
}

double zoom100(Settings& settings, const Display& display) { return zoom100(ppi(settings, display), display.dpr); }

}  // namespace xqt::ScreenCalibration
