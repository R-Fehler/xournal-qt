/*
 * xournal-qt: screen calibration, so that a page at 100 % is as large as the paper (1 cm on the page is 1 cm on the
 * screen). Upstream Xournal++ has one number for it (Settings::displayDpi, its settings dialog's "zoom calibration");
 * here each screen keeps its own, since a tablet at 200 % and an external monitor differ.
 *
 * What is stored for a screen is its panel's pixels per inch (device pixels), not the logical DPI: changing the
 * system's scaling (125 % -> 150 % on KDE, 200 % on a Surface) changes the device pixel ratio, and the logical DPI
 * follows from it without calibrating again. The zoom of a view is in logical pixels per point, so 100 % is
 *
 *     zoom100 = ppi / dpr / 72
 *
 * with `dpr` the window's effective device pixel ratio (fractional on Wayland/KDE and Windows at 125 %, 150 %).
 * Without a calibration, a screen's ppi is what the system reports (QScreen::physicalDotsPerInch, in logical pixels,
 * times the ratio) if that is plausible, else 96 logical dpi (the old fixed 100 %).
 *
 * Stored in settings.xml, part "xournalQt", key "screenCalibration": "<key>=<ppi>;..." with each key
 * percent-encoded (keys name a screen by its maker, model, serial or connector, and its size in millimetres).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <QString>

class QScreen;
class Settings;

namespace xqt::ScreenCalibration {

/// What the calibration needs to know about the screen a view is shown on (tests make their own).
struct Display {
    QString key;             ///< names the screen in the settings ("": not known, nothing stored)
    QString name;            ///< for people ("DELL U2720Q", "eDP-1")
    double reportedDpi = 0;  ///< logical pixels per inch the system reports (0: not reported)
    double dpr = 1;          ///< device pixels per logical pixel of the window
};

/// Logical pixels per inch taken when the screen reports nothing plausible (and what 100 % was before calibration).
constexpr double FALLBACK_DPI = 96;
/// Range of the calibration slider, logical pixels per inch.
constexpr double MIN_DPI = 40;
constexpr double MAX_DPI = 400;

/// The screen and the window's device pixel ratio on it (dpr <= 0: the screen's).
Display displayOf(const QScreen* screen, double dpr = 0);
/// The key a screen is stored under.
QString keyOf(const QScreen* screen);

/// Whether a reported logical DPI can be believed: between half and three times the usual 96 (outside, it is a
/// broken EDID, a projector or TV that reports its aspect ratio as millimetres), and not Qt's own guess of exactly
/// 100 for platforms that know no size (QPlatformScreen::physicalSize, the offscreen platform).
bool plausible(double logicalDpi);
/// The panel's pixels per inch without a calibration (device pixels).
double defaultPpi(const Display& display);
/// The calibrated panel pixels per inch of a screen, if it was calibrated.
std::optional<double> storedPpi(Settings& settings, const QString& key);
/// Calibrates a screen (ppi: device pixels per inch) / takes the calibration back (it follows the system again).
void store(Settings& settings, const QString& key, double ppi);
void forget(Settings& settings, const QString& key);
/// The screen's panel pixels per inch: calibrated, else the default.
double ppi(Settings& settings, const Display& display);

/// Logical pixels per inch for a panel's ppi at a device pixel ratio.
inline double logicalDpi(double ppi, double dpr) { return ppi / (dpr > 0 ? dpr : 1.0); }
/// The zoom (logical pixels per point) that shows a page at its real size.
inline double zoom100(double ppi, double dpr) { return logicalDpi(ppi, dpr) / 72.0; }
double zoom100(Settings& settings, const Display& display);

}  // namespace xqt::ScreenCalibration
