#include "SettingsModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <QColor>
#include <QScreen>
#include <QVariantMap>
#include <QWindow>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/pagetype/PageTypeHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "control/settings/SettingsEnums.h"
#include "control/tools/StrokeStabilizerEnum.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/FuzzyQuery.h"
#include "session/WordMatch.h"
#include "shell/Thumbnails.h"

#include "CanvasMemory.h"
#include "ScreenCalibration.h"

namespace xqt {

namespace {
struct PaperFormat {
    const char* name;
    double width, height;  ///< portrait, in points
    bool wide = false;     ///< meant to be landscape (choosing it turns the page)
};
/// Millimetres in points (upstream stores page sizes in points, 1/72 inch)
constexpr double mm(double v) { return v * 72.0 / 25.4; }
// ISO A0 (posters) down to A7 (flashcards), then the common formats of upstream's page format dialog (GtkPaperSize)
// in points, and a 16:9 slide as PowerPoint makes it (13.33 x 7.5 in): a plain page size in the .xopp, as upstream
// stores any other. The settings keep a size, not an index: the order can change.
constexpr std::array<PaperFormat, 11> PAPER_FORMATS{{{"A0", mm(841), mm(1189)},
                                                      {"A1", mm(594), mm(841)},
                                                      {"A2", mm(420), mm(594)},
                                                      {"A3", mm(297), mm(420)},
                                                      {"A4", mm(210), mm(297)},
                                                      {"A5", mm(148), mm(210)},
                                                      {"A6", mm(105), mm(148)},
                                                      {"A7", mm(74), mm(105)},
                                                      {"Letter", 612, 792},
                                                      {"Legal", 612, 1008},
                                                      {QT_TRANSLATE_NOOP("SettingsModel", "16:9 (presentation)"), 540, 960,
                                                       true}}};

/// How long touch waits once the pen is away ("touch" / "timeout"), in milliseconds. Upstream waits a second; with a
/// pen that tells when it is near, touch is ignored while it is anyway, so here it does not wait at all.
constexpr int DEFAULT_PALM_TIMEOUT_MS = 0;
/// Up to which height a pen that tells its height counts as near ("touch" / "nearHeight"), percent; 100: all of it.
constexpr int DEFAULT_NEAR_HEIGHT_PERCENT = 100;
}  // namespace

int SettingsModel::previewMemory(Settings& s) {
    int mb = static_cast<int>(ThumbnailProvider::DEFAULT_CACHE_MB);
    s.getCustomElement("xournalQt").getInt("previewMemory", mb);
    return mb;
}

int SettingsModel::canvasMemory(Settings& s) {
    int mb = 0;  // (not set: follows the machine)
    s.getCustomElement("xournalQt").getInt("canvasMemory", mb);
    const int maxMb = static_cast<int>(CanvasMemory::maxLimit() / (1024 * 1024));
    return mb > 0 ? std::min(mb, maxMb) : static_cast<int>(CanvasMemory::defaultLimit() / (1024 * 1024));
}

void SettingsModel::applyCanvasMemory(Settings& s) {
    CanvasMemory::instance().setLimit(static_cast<qint64>(canvasMemory(s)) * 1024 * 1024);
}

int SettingsModel::fuzzyTypos(Settings& s) {
    int typos = wordmatch::DEFAULT_TYPOS;
    s.getCustomElement("xournalQt").getInt("fuzzyTypos", typos);
    return std::clamp(typos, 0, wordmatch::MAX_TYPOS);
}

void SettingsModel::applyFuzzyTypos(Settings& s) { FuzzyQuery::setTypoTolerance(fuzzyTypos(s)); }

bool SettingsModel::handWhenOpening(Settings& s) {
#ifdef Q_OS_ANDROID
    bool on = true;
#else
    bool on = false;
#endif
    s.getCustomElement("touch").getBool("handWhenOpening", on);
    return on;
}

int SettingsModel::systemMemory() const { return static_cast<int>(CanvasMemory::systemMemory() / (1024 * 1024)); }

void SettingsModel::applyPreviewMemory(Settings& s) {
    ThumbnailProvider::setCacheLimit(static_cast<qint64>(previewMemory(s)) * 1024 * 1024);
}

QSizeF SettingsModel::paperSize(int index) {
    if (index < 0 || index >= static_cast<int>(PAPER_FORMATS.size())) {
        return {};
    }
    return {PAPER_FORMATS[static_cast<size_t>(index)].width, PAPER_FORMATS[static_cast<size_t>(index)].height};
}

SettingsModel::SettingsModel(AppContext& app, QObject* parent):
        QObject(parent), app(app), settings(*app.getSettings()) {
    Settings& s = settings;

    // --- pen ---
    add("pressureSensitivity", [&s] { return QVariant(static_cast<bool>(s.isPressureSensitivity())); },
        [&s](const QVariant& v) { s.setPressureSensitivity(v.toBool()); });
    add("minimumPressure", [&s] { return QVariant(s.getMinimumPressure()); },
        [&s](const QVariant& v) { s.setMinimumPressure(std::clamp(v.toDouble(), 0.01, 1.0)); });
    add("pressureMultiplier", [&s] { return QVariant(s.getPressureMultiplier()); },
        [&s](const QVariant& v) { s.setPressureMultiplier(std::clamp(v.toDouble(), 0.5, 4.0)); });
    add("pressureGuessing", [&s] { return QVariant(s.isPressureGuessingEnabled()); },
        [&s](const QVariant& v) { s.setPressureGuessingEnabled(v.toBool()); });
    // Tool of the pen's eraser end / side button (upstream button "eraser") and of the first barrel button.
    auto buttonTool = [this](Button button) {
        return std::pair{[this, button] {
                             return QVariant(QString::fromUtf8(
                                     toolTypeToString(settings.getButtonConfig(button)->getAction()).data()));
                         },
                         [this, button](const QVariant& v) {
                             ButtonConfig* cfg = settings.getButtonConfig(button);
                             ButtonConfig replacement(toolTypeFromString(v.toString().toStdString()), Colors::black,
                                                      TOOL_SIZE_NONE, DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
                             replacement.device = cfg->device;
                             *cfg = replacement;
                             this->app.initButtonTools();  // the button needs its tool (else it does nothing)
                             settings.customSettingsChanged();
                         }};
    };
    auto [eraserGet, eraserSet] = buttonTool(BUTTON_ERASER);
    add("eraserButtonTool", eraserGet, eraserSet);
    auto [stylusGet, stylusSet] = buttonTool(BUTTON_STYLUS_ONE);
    add("stylusButtonTool", stylusGet, stylusSet);
    auto [stylus2Get, stylus2Set] = buttonTool(BUTTON_STYLUS_TWO);
    add("stylusButton2Tool", stylus2Get, stylus2Set);
    add("eraserMode",
        [&app] { return QVariant(QString::fromUtf8(eraserTypeToString(app.getToolHandler()->getEraserType()).data())); },
        [&app](const QVariant& v) {
            const EraserType type = eraserTypeFromString(v.toString().toStdString());
            if (type != ERASER_TYPE_NONE) {
                app.getToolHandler()->setEraserType(type);
            }
        });

    // Open documents at the page they were left at (off: at their first page)
    add("resumeAtLastPage",
        [&s] {
            bool resume = false;
            s.getCustomElement("xournalQt").getBool("resumeAtLastPage", resume);
            return QVariant(resume);
        },
        [&s](const QVariant& v) {
            s.getCustomElement("xournalQt").setBool("resumeAtLastPage", v.toBool());
            s.customSettingsChanged();
        });
    // A tapped link to another document (qt/docs/links.md): "ask" (the default: a popup offers the three), or opened
    // at once in a new "tab", as the "reference" or "here" (in place of the current document)
    add("linkOpening",
        [&s] {
            std::string v;
            s.getCustomElement("xournalQt").getString("linkOpening", v);
            return QVariant(v == "tab" || v == "reference" || v == "here" ? QString::fromStdString(v)
                                                                          : QStringLiteral("ask"));
        },
        [&s](const QVariant& v) {
            const QString how = v.toString();
            s.getCustomElement("xournalQt")
                    .setString("linkOpening", how == "tab" || how == "reference" || how == "here" ? how.toStdString()
                                                                                                 : std::string("ask"));
            s.customSettingsChanged();
        });
    // Looking up selected text (qt/docs/citations.md): ask before a web address opens (it is shown whole), the
    // translator ("google", "deepl", "bing" or an address with {text} and {lang}) and the language translated into
    // ("": the system's)
    add("webConfirm",
        [&s] {
            bool on = true;
            s.getCustomElement("xournalQt").getBool("webConfirm", on);
            return QVariant(on);
        },
        [&s](const QVariant& v) {
            s.getCustomElement("xournalQt").setBool("webConfirm", v.toBool());
            s.customSettingsChanged();
        });
    // arXiv's search and its PDFs (the app's own networking, opt-in): "ask" (not decided yet), "on", "off"
    add("networkAccess",
        [&s] {
            std::string v;
            s.getCustomElement("xournalQt").getString("networkAccess", v);
            return QVariant(v == "on" || v == "off" ? QString::fromStdString(v) : QStringLiteral("ask"));
        },
        [&s](const QVariant& v) {
            const QString how = v.toString();
            s.getCustomElement("xournalQt")
                    .setString("networkAccess", how == "on" || how == "off" ? how.toStdString() : std::string("ask"));
            s.customSettingsChanged();
        });
    // (webSearch: an engine's key or a custom address with {text}, cite::searchEngines)
    for (const auto& [key, fallback]: {std::pair{"translateService", "google"}, std::pair{"translateLanguage", ""},
                                       std::pair{"webSearch", "google"}}) {
        add(key,
            [&s, key = key, fallback = fallback] {
                std::string v = fallback;
                s.getCustomElement("xournalQt").getString(key, v);
                return QVariant(QString::fromStdString(v));
            },
            [&s, key = key](const QVariant& v) {
                s.getCustomElement("xournalQt").setString(key, v.toString().trimmed().toStdString());
                s.customSettingsChanged();
            });
    }
    // Hybrid PDFs (qt/docs/hybrid-pdf.md): notes of an annotated PDF go into the PDF itself (off: "name.notes.pdf");
    // whether that was explained; a .xopp for Xournal++ written next to a hybrid PDF on every save
    for (const char* key: {"hybridIntoPdf", "hybridIntoPdfExplained", "hybridExportXopp"}) {
        add(key,
            [&s, key] {
                bool on = false;
                s.getCustomElement("xournalQt").getBool(key, on);
                return QVariant(on);
            },
            [&s, key](const QVariant& v) {
                s.getCustomElement("xournalQt").setBool(key, v.toBool());
                s.customSettingsChanged();
            });
    }
    // A document saved as "name.xopp" is saved as a hybrid PDF: what happens to the .xopp ("ask" each time, the
    // default; "trash", "update": kept up to date for Xournal++, "keep": left as it is)
    add("hybridOldXopp",
        [&s] {
            std::string v;
            s.getCustomElement("xournalQt").getString("hybridOldXopp", v);
            return QVariant(v == "trash" || v == "update" || v == "keep" ? QString::fromStdString(v)
                                                                         : QStringLiteral("ask"));
        },
        [&s](const QVariant& v) {
            const QString choice = v.toString();
            s.getCustomElement("xournalQt")
                    .setString("hybridOldXopp",
                               choice == "trash" || choice == "update" || choice == "keep" ? choice.toStdString()
                                                                                           : std::string("ask"));
            s.customSettingsChanged();
        });
    // How documents are kept (DocumentMode.h): "xopp" (Xournal++ files: .xopp next to their PDFs) or "pdf" (PDF files:
    // every document one PDF with notes). Asked at the first start; while not chosen, "xopp".
    add("documentMode", [&s] { return QVariant(DocumentMode::nameOf(DocumentMode::effective(s))); },
        [&s](const QVariant& v) {
            if (const auto mode = DocumentMode::fromName(v.toString()); mode != DocumentMode::Mode::Unset) {
                DocumentMode::store(s, mode);
            }
        });
    // What a new text document is (qt/docs/md-pdf.md): "pdf" (a PDF text document) or "md" (a Markdown file); while it
    // is not chosen, as the way documents are kept says
    add("newTextDocuments",
        [&s] {
            return QVariant(DocumentMode::newTextDocuments(s) == DocumentMode::TextKind::Pdf ? QStringLiteral("pdf")
                                                                                               : QStringLiteral("md"));
        },
        [&s](const QVariant& v) {
            DocumentMode::setNewTextDocuments(
                    s, v.toString() == "pdf" ? DocumentMode::TextKind::Pdf : DocumentMode::TextKind::Markdown);
        });
    add("canvasMemory", [&s] { return QVariant(canvasMemory(s)); },
        [&s](const QVariant& v) {
            const int maxMb = static_cast<int>(CanvasMemory::maxLimit() / (1024 * 1024));
            s.getCustomElement("xournalQt").setInt("canvasMemory", std::clamp(v.toInt(), std::min(256, maxMb), maxMb));
            s.customSettingsChanged();
            applyCanvasMemory(s);
        });
    add("previewMemory", [&s] { return QVariant(previewMemory(s)); },
        [&s](const QVariant& v) {
            s.getCustomElement("xournalQt").setInt("previewMemory", std::clamp(v.toInt(), 64, 1024));
            s.customSettingsChanged();
            applyPreviewMemory(s);
        });
    // The fuzzy search's typo tolerance (the toggle itself is the library model's: app.library.fuzzySearch)
    add("fuzzyTypos", [&s] { return QVariant(fuzzyTypos(s)); },
        [&s](const QVariant& v) {
            s.getCustomElement("xournalQt").setInt("fuzzyTypos", std::clamp(v.toInt(), 0, wordmatch::MAX_TYPOS));
            s.customSettingsChanged();
            applyFuzzyTypos(s);
        });
    add("snapGrid", [&s] { return QVariant(s.isSnapGrid()); }, [&s](const QVariant& v) { s.setSnapGrid(v.toBool()); });

    // --- touch ---
    add("palmRejectionTimeout",
        [&s] {
            int ms = DEFAULT_PALM_TIMEOUT_MS;
            s.getCustomElement("touch").getInt("timeout", ms);
            return QVariant(ms);
        },
        [&s](const QVariant& v) {
            s.getCustomElement("touch").setInt("timeout", std::clamp(v.toInt(), 0, 5000));
            s.customSettingsChanged();
        });
    add("palmNearHeight",
        [&s] {
            int percent = DEFAULT_NEAR_HEIGHT_PERCENT;
            s.getCustomElement("touch").getInt("nearHeight", percent);
            return QVariant(percent);
        },
        [&s](const QVariant& v) {
            s.getCustomElement("touch").setInt("nearHeight", std::clamp(v.toInt(), 1, 100));
            s.customSettingsChanged();
        });
    add("zoomGestures", [&s] { return QVariant(s.isZoomGesturesEnabled()); },
        [&s](const QVariant& v) { s.setZoomGesturesEnabled(v.toBool()); });
    // One finger draws with the tool, two scroll and zoom (upstream's setting; also the tool bar's toggle)
    add("touchDrawing", [&s] { return QVariant(s.getTouchDrawingEnabled()); },
        [&s](const QVariant& v) { s.setTouchDrawingEnabled(v.toBool()); });
    add("handWhenOpening", [&s] { return QVariant(handWhenOpening(s)); },
        [&s](const QVariant& v) {
            s.getCustomElement("touch").setBool("handWhenOpening", v.toBool());
            s.customSettingsChanged();
        });

    // --- stabilizer (ranges as in upstream's settings dialog) ---
    add("stabilizerAveraging", [&s] { return QVariant(static_cast<int>(s.getStabilizerAveragingMethod())); },
        [&s](const QVariant& v) {
            const auto m = static_cast<StrokeStabilizer::AveragingMethod>(v.toInt());
            if (StrokeStabilizer::isValid(m)) {
                s.setStabilizerAveragingMethod(m);
            }
        });
    add("stabilizerPreprocessor", [&s] { return QVariant(static_cast<int>(s.getStabilizerPreprocessor())); },
        [&s](const QVariant& v) {
            const auto p = static_cast<StrokeStabilizer::Preprocessor>(v.toInt());
            if (StrokeStabilizer::isValid(p)) {
                s.setStabilizerPreprocessor(p);
            }
        });
    add("stabilizerBuffersize", [&s] { return QVariant(static_cast<int>(s.getStabilizerBuffersize())); },
        [&s](const QVariant& v) { s.setStabilizerBuffersize(static_cast<size_t>(std::clamp(v.toInt(), 2, 100))); });
    add("stabilizerSigma", [&s] { return QVariant(s.getStabilizerSigma()); },
        [&s](const QVariant& v) { s.setStabilizerSigma(std::clamp(v.toDouble(), 0.05, 5.0)); });
    add("stabilizerDeadzoneRadius", [&s] { return QVariant(s.getStabilizerDeadzoneRadius()); },
        [&s](const QVariant& v) { s.setStabilizerDeadzoneRadius(std::clamp(v.toDouble(), 0.1, 50.0)); });
    add("stabilizerDrag", [&s] { return QVariant(s.getStabilizerDrag()); },
        [&s](const QVariant& v) { s.setStabilizerDrag(std::clamp(v.toDouble(), 0.0, 1.0)); });
    add("stabilizerMass", [&s] { return QVariant(s.getStabilizerMass()); },
        [&s](const QVariant& v) { s.setStabilizerMass(std::clamp(v.toDouble(), 1.0, 30.0)); });
    add("stabilizerCuspDetection", [&s] { return QVariant(s.getStabilizerCuspDetection()); },
        [&s](const QVariant& v) { s.setStabilizerCuspDetection(v.toBool()); });
    add("stabilizerFinalizeStroke", [&s] { return QVariant(s.getStabilizerFinalizeStroke()); },
        [&s](const QVariant& v) { s.setStabilizerFinalizeStroke(v.toBool()); });

    // --- documents ---
    add("autosaveEnabled", [&s] { return QVariant(s.isAutosaveEnabled()); },
        [&s](const QVariant& v) { s.setAutosaveEnabled(v.toBool()); });
    add("autosaveMinutes", [&s] { return QVariant(s.getAutosaveTimeout()); },
        [&s](const QVariant& v) { s.setAutosaveTimeout(std::clamp(v.toInt(), 1, 60)); });
    add("defaultSaveName",
        [&s] {
            const std::u8string& n = s.getDefaultSaveName();
            return QVariant(QString::fromUtf8(reinterpret_cast<const char*>(n.data()), static_cast<qsizetype>(n.size())));
        },
        [&s](const QVariant& v) {
            const QByteArray utf8 = v.toString().trimmed().toUtf8();
            if (!utf8.isEmpty()) {
                s.setDefaultSaveName(std::u8string(reinterpret_cast<const char8_t*>(utf8.constData()),
                                                   static_cast<size_t>(utf8.size())));
            }
        });

    add("restoreSession",
        [&s] {
            bool restore = true;
            s.getCustomElement("xournalQt").getBool("restoreSession", restore);
            return QVariant(restore);
        },
        [&s](const QVariant& v) {
            s.getCustomElement("xournalQt").setBool("restoreSession", v.toBool());
            s.customSettingsChanged();
        });

    // --- new pages (upstream's page template) ---
    auto withTemplate = [&s](auto&& change) {
        PageTemplateSettings tpl = s.getPageTemplateSettings();
        change(tpl);
        s.setPageTemplateSettings(tpl);
    };
    add("pageBackground",
        [this] {
            const PageType current = settings.getPageTemplateSettings().getBackgroundType();
            const auto& types = this->app.getPageTypes()->getPageTypes();
            for (size_t i = 0; i < types.size(); ++i) {
                if (types[i]->page == current) {
                    return QVariant(static_cast<int>(i));
                }
            }
            return QVariant(-1);
        },
        [this, withTemplate](const QVariant& v) {
            const auto& types = this->app.getPageTypes()->getPageTypes();
            const int i = v.toInt();
            if (i >= 0 && i < static_cast<int>(types.size())) {
                withTemplate([&](PageTemplateSettings& tpl) { tpl.setBackgroundType(types[static_cast<size_t>(i)]->page); });
            }
        });
    add("paperFormat",
        [&s] {
            const auto& tpl = s.getPageTemplateSettings();
            const double w = std::min(tpl.getPageWidth(), tpl.getPageHeight());
            const double h = std::max(tpl.getPageWidth(), tpl.getPageHeight());
            for (size_t i = 0; i < PAPER_FORMATS.size(); ++i) {
                if (std::abs(PAPER_FORMATS[i].width - w) < 1 && std::abs(PAPER_FORMATS[i].height - h) < 1) {
                    return QVariant(static_cast<int>(i));
                }
            }
            return QVariant(-1);
        },
        [withTemplate](const QVariant& v) {
            const int i = v.toInt();
            if (i < 0 || i >= static_cast<int>(PAPER_FORMATS.size())) {
                return;
            }
            withTemplate([&](PageTemplateSettings& tpl) {
                const PaperFormat& f = PAPER_FORMATS[static_cast<size_t>(i)];
                const bool landscape = f.wide || tpl.getPageWidth() > tpl.getPageHeight();
                tpl.setPageWidth(landscape ? f.height : f.width);
                tpl.setPageHeight(landscape ? f.width : f.height);
            });
        });
    add("landscape",
        [&s] { return QVariant(s.getPageTemplateSettings().getPageWidth() > s.getPageTemplateSettings().getPageHeight()); },
        [withTemplate](const QVariant& v) {
            withTemplate([&](PageTemplateSettings& tpl) {
                const double w = tpl.getPageWidth(), h = tpl.getPageHeight();
                if ((w > h) != v.toBool()) {
                    tpl.setPageWidth(h);
                    tpl.setPageHeight(w);
                }
            });
        });
    add("pageColor",
        [&s] { return QVariant(QColor::fromRgb(uint32_t(s.getPageTemplateSettings().getBackgroundColor()))); },
        [withTemplate](const QVariant& v) {
            const QColor c = v.value<QColor>();
            if (c.isValid()) {
                withTemplate([&](PageTemplateSettings& tpl) { tpl.setBackgroundColor(Color(c.rgb() | 0xff000000u)); });
            }
        });
    add("copyLastPageSettings", [&s] { return QVariant(s.getPageTemplateSettings().isCopyLastPageSettings()); },
        [withTemplate](const QVariant& v) {
            withTemplate([&](PageTemplateSettings& tpl) { tpl.setCopyLastPageSettings(v.toBool()); });
        });
    add("copyLastPageSize", [&s] { return QVariant(s.getPageTemplateSettings().isCopyLastPageSize()); },
        [withTemplate](const QVariant& v) {
            withTemplate([&](PageTemplateSettings& tpl) { tpl.setCopyLastPageSize(v.toBool()); });
        });
}

void SettingsModel::add(const QString& key, std::function<QVariant()> get, std::function<void(const QVariant&)> set) {
    entries.emplace(key, Entry{std::move(get), std::move(set)});
}

QStringList SettingsModel::pageBackgrounds() const {
    QStringList names;
    for (const auto& info: app.getPageTypes()->getPageTypes()) {
        names << QString::fromStdString(info->name);
    }
    return names;
}

QStringList SettingsModel::pageBackgroundFormats() const {
    QStringList formats;
    for (const auto& info: app.getPageTypes()->getPageTypes()) {
        formats << QString::fromStdString(PageTypeHandler::getStringForPageTypeFormat(info->page.format));
    }
    return formats;
}

QStringList SettingsModel::paperFormats() const {
    QStringList names;
    for (const auto& f: PAPER_FORMATS) {
        names << tr(f.name);
    }
    return names;
}

QString SettingsModel::templatePaperSize() const {
    const auto& tpl = settings.getPageTemplateSettings();
    const auto inMm = [](double pt) { return QString::number(std::lround(pt * 25.4 / 72.0)); };
    //: A page size, width x height
    return tr("%1 × %2 mm").arg(inMm(tpl.getPageWidth()), inMm(tpl.getPageHeight()));
}

bool SettingsModel::paperIsWide(int index) const {
    return index >= 0 && index < static_cast<int>(PAPER_FORMATS.size()) && PAPER_FORMATS[static_cast<size_t>(index)].wide;
}

QStringList SettingsModel::keys() const {
    QStringList k;
    for (const auto& [key, e]: entries) {
        k << key;
    }
    return k;
}

QVariant SettingsModel::get(const QString& key) const {
    auto it = entries.find(key);
    if (it == entries.end()) {
        qWarning("SettingsModel: unknown key %s", qPrintable(key));
        return {};
    }
    return it->second.get();
}

bool SettingsModel::set(const QString& key, const QVariant& value) {
    auto it = entries.find(key);
    if (it == entries.end()) {
        qWarning("SettingsModel: unknown key %s", qPrintable(key));
        return false;
    }
    if (it->second.get() == value) {
        return true;
    }
    it->second.set(value);
    ++rev;
    Q_EMIT changed();
    Q_EMIT app.settingsChanged();
    return true;
}

namespace {
ScreenCalibration::Display displayOf(QWindow* window) {
    return window ? ScreenCalibration::displayOf(window->screen(), window->devicePixelRatio())
                  : ScreenCalibration::Display{};
}
}  // namespace

QVariantMap SettingsModel::screenCalibration(QWindow* window) const {
    const auto d = displayOf(window);
    const auto stored = ScreenCalibration::storedPpi(settings, d.key);
    const double ppi = ScreenCalibration::ppi(settings, d);
    return {{"key", d.key},
            {"name", d.name},
            {"dpr", d.dpr},
            {"reportedDpi", d.reportedDpi},
            {"reportedPlausible", ScreenCalibration::plausible(d.reportedDpi)},
            {"defaultDpi", ScreenCalibration::logicalDpi(ScreenCalibration::defaultPpi(d), d.dpr)},
            {"dpi", ScreenCalibration::logicalDpi(ppi, d.dpr)},
            {"ppi", ppi},
            {"calibrated", stored.has_value()}};
}

void SettingsModel::calibrateScreen(QWindow* window, double logicalDpi) {
    const auto d = displayOf(window);
    if (d.key.isEmpty() || !(logicalDpi > 0)) {
        return;
    }
    ScreenCalibration::store(settings, d.key, logicalDpi * d.dpr);
    ++rev;
    Q_EMIT changed();
    Q_EMIT app.settingsChanged();  // (the views take the new 100 %)
}

void SettingsModel::resetScreenCalibration(QWindow* window) {
    const auto d = displayOf(window);
    if (!ScreenCalibration::storedPpi(settings, d.key)) {
        return;
    }
    ScreenCalibration::forget(settings, d.key);
    ++rev;
    Q_EMIT changed();
    Q_EMIT app.settingsChanged();
}

void SettingsModel::begin() {
    if (!open) {
        open = true;
        settings.transactionStart();
    }
}

void SettingsModel::end() {
    if (open) {
        open = false;
        app.getToolHandler()->saveSettings();  // the eraser mode is part of the tool state
        settings.transactionEnd();
    }
}

}  // namespace xqt
