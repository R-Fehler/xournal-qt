/*
 * xournal-qt: the settings screen's view of upstream's Settings (the same settings.xml keys as Xournal++).
 *
 * QML reads and writes values by key: `settings.revision, settings.get("minimumPressure")` (the revision makes the
 * binding re-evaluate after a change) and `settings.set("minimumPressure", v)`. Changes apply immediately; they
 * are written to disk once when the screen closes (upstream's settings transaction), like upstream's dialog.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <map>

#include <QObject>
#include <QSizeF>
#include <QStringList>
#include <QVariant>

class QWindow;
class Settings;

namespace xqt {

class AppContext;

class SettingsModel final: public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList pageBackgrounds READ pageBackgrounds CONSTANT)
    Q_PROPERTY(QStringList paperFormats READ paperFormats CONSTANT)
    /// The pattern of each page background ("plain", "ruled", "graph", ...), for previews
    Q_PROPERTY(QStringList pageBackgroundFormats READ pageBackgroundFormats CONSTANT)
    /// Physical memory in MB (the memory for canvas pages may be set up to a third of it)
    Q_PROPERTY(int systemMemory READ systemMemory CONSTANT)
public:
    explicit SettingsModel(AppContext& app, QObject* parent = nullptr);
    /// Portrait size (points) of the paper format at `index` of paperFormats (invalid: none).
    static QSizeF paperSize(int index);
    /// Memory for kept page thumbnails (MB, setting "previewMemory"); applyPreviewMemory hands it to them.
    static int previewMemory(Settings& settings);
    static void applyPreviewMemory(Settings& settings);
    /// Memory for rendered canvas pages (MB, setting "canvasMemory"; default a quarter of the RAM, at most a third)
    static int canvasMemory(Settings& settings);
    static void applyCanvasMemory(Settings& settings);
    /// The fuzzy search's typo tolerance ("fuzzyTypos" in the xournalQt part: 0 none, 1 one typo in words of 5+
    /// letters, 2 two in words of 8+; WordMatch.h), and applying it to the queries parsed from then on.
    static int fuzzyTypos(Settings& settings);
    static void applyFuzzyTypos(Settings& settings);
    /// Documents open with the hand tool, so that one finger scrolls and the pen is chosen when wanted
    /// ("handWhenOpening" in the touch part). On by default on Android, where one finger is expected to scroll; off
    /// on the desktop, where the tool stays as it was.
    static bool handWhenOpening(Settings& settings);
    int systemMemory() const;

    int revision() const { return rev; }
    /// Names of the page backgrounds for new pages (upstream's page types: plain, lined, ruled, graph, ...).
    QStringList pageBackgrounds() const;
    QStringList pageBackgroundFormats() const;
    QStringList paperFormats() const;
    /// The paper format at `index` is meant to be landscape (16:9): choosing it turns the page.
    Q_INVOKABLE bool paperIsWide(int index) const;
    /// The size of new pages as text ("100 × 150 mm"): for a size that is none of the paperFormats (paperFormat -1,
    /// e.g. set in Xournal++), which the dialogs offer as it is.
    Q_INVOKABLE QString templatePaperSize() const;
    /// All keys, for tests.
    QStringList keys() const;

    Q_INVOKABLE QVariant get(const QString& key) const;
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);
    // --- screen calibration (ScreenCalibration.h): 100 % is the size of the paper on the window's screen ---------
    /// The screen `window` is on: { key, name, dpr, reportedDpi (logical, 0: none), reportedPlausible, defaultDpi,
    /// dpi (logical pixels per inch now: calibrated, else the default), ppi (the panel's device pixels per inch),
    /// calibrated }. DPIs in logical pixels per inch (what the ruler on the page is drawn in).
    Q_INVOKABLE QVariantMap screenCalibration(QWindow* window) const;
    /// Calibrates the screen of `window`: `logicalDpi` logical pixels make an inch there (stored as the panel's
    /// pixels per inch, so that another scaling of the system keeps it). Every view on that screen follows.
    Q_INVOKABLE void calibrateScreen(QWindow* window, double logicalDpi);
    /// Takes the calibration of the screen back: what the system reports (or 96 dpi) again.
    Q_INVOKABLE void resetScreenCalibration(QWindow* window);

    /// The settings screen opened / closed: changes are saved once, at close.
    Q_INVOKABLE void begin();
    Q_INVOKABLE void end();

Q_SIGNALS:
    void changed();

private:
    struct Entry {
        std::function<QVariant()> get;
        std::function<void(const QVariant&)> set;
    };
    void add(const QString& key, std::function<QVariant()> get, std::function<void(const QVariant&)> set);

    AppContext& app;
    Settings& settings;
    std::map<QString, Entry> entries;
    int rev = 0;
    bool open = false;
};

}  // namespace xqt
