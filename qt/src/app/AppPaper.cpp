/*
 * xournal-qt: dark pages and page colors in the application controller (qt/docs/features/dark-pages.md).
 *
 * @license GNU GPLv2 or later
 */
#include <QGuiApplication>
#include <QStyleHints>
#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include "control/ToolHandler.h"
#include "control/pagetype/PageTypeHandler.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "render/PaperTexture.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/ColorPalettes.h"
#include "shell/SettingsModel.h"

#include "AppController.h"
#include "DarkPages.h"

using namespace xqt;

namespace {
const char* const CUSTOM = "xournalQt";

QColor qcolor(Color c) { return QColor(c.red, c.green, c.blue); }
Color toXojColor(const QColor& c) { return Color(static_cast<uint32_t>(c.rgb() | 0xff000000u)); }

/// Every window's controller hears of a new setting through this (its object name is the setting)
QObject& darkRelay() {
    static QObject* relay = new QObject;
    return *relay;
}

double luminance(const QColor& c) {
    const auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}
double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}  // namespace

void AppController::setUpDarkPages() {
    // The canvas maps the palettes' roles to the Dark palette's (one table for the whole application)
    static const bool rolesSet = [] {
        std::vector<dark::RolePair> pairs;
        for (const auto& p: ColorPalettes::builtIn().darkPairs()) {
            pairs.push_back({p.light.rgb(), p.dark.rgb(), p.opacity});
        }
        dark::setRoles(std::move(pairs));
        return true;
    }();
    (void)rolesSet;
    connect(&darkRelay(), &QObject::objectNameChanged, this, &AppController::darkPagesChanged);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (darkPagesMode() == "system") {
            Q_EMIT darkPagesChanged();
        }
    });
}

QString AppController::darkPagesMode() const {
    std::string mode;
    app->getSettings()->getCustomElement(CUSTOM).getString("darkPages", mode);
    return mode == "on" || mode == "system" ? QString::fromStdString(mode) : QStringLiteral("off");
}

void AppController::setDarkPagesMode(const QString& mode) {
    const QString m = mode == "on" || mode == "system" ? mode : QStringLiteral("off");
    if (m == darkPagesMode()) {
        return;
    }
    app->getSettings()->getCustomElement(CUSTOM).setString("darkPages", m.toStdString());
    app->getSettings()->customSettingsChanged();
    darkRelay().setObjectName(m);  // (every window: darkPagesChanged)
}

bool AppController::darkPagesShown() const {
    const QString m = darkPagesMode();
    return m == "on" || (m == "system" && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}

QVariantList AppController::paperSwatches() const {
    // (names for people; the ids are paper::swatches()')
    const QHash<QString, QString> names{{"white", tr("White")},        {"illustration", tr("Illustration paper")},
                                        {"kraft", tr("Kraft")},        {"sage", tr("Soft green")},
                                        {"mist", tr("Soft blue")},     {"grey", tr("Grey")},
                                        {"darkgrey", tr("Dark grey")}, {"black", tr("Black")}};
    QVariantList out;
    for (const paper::Swatch& s: paper::swatches()) {
        const QColor c = QColor::fromRgb(s.rgb);
        out << QVariantMap{{"id", QString::fromLatin1(s.id)},
                           {"name", names.value(QString::fromLatin1(s.id), QString::fromLatin1(s.id))},
                           {"color", c},
                           {"dark", ColorPalettes::isDarkPaper(c)}};
    }
    return out;
}

bool AppController::printUsesDarkPaper(const QString& range) const {
    DocumentSession* s = session();
    if (!s) {
        return false;
    }
    Document* doc = s->getDocument();
    std::shared_lock lock(*doc);
    // The pages of the range ("" all; "2-5,7")
    std::vector<bool> printed(doc->getPageCount(), range.trimmed().isEmpty());
    for (const QString& part: range.split(',', Qt::SkipEmptyParts)) {
        const int from = part.section('-', 0, 0).trimmed().toInt();
        const int to = part.contains('-') ? part.section('-', 1, 1).trimmed().toInt() : from;
        for (int p = std::max(1, from); p <= to && p <= static_cast<int>(printed.size()); ++p) {
            printed[static_cast<size_t>(p - 1)] = true;
        }
    }
    for (size_t i = 0; i < printed.size(); ++i) {
        const PageRef p = doc->getPage(i);
        if (printed[i] && !p->getBackgroundType().isSpecial() && paper::isDark(p->getBackgroundColor())) {
            return true;
        }
    }
    return false;
}

PageType AppController::paperTypeOf(int background, const QColor& paperColor, int textured, Color& c) const {
    const auto& types = app->getPageTypes()->getPageTypes();
    const auto& tpl = app->getSettings()->getPageTemplateSettings();
    c = paperColor.isValid() ? toXojColor(paperColor) : tpl.getBackgroundColor();
    const bool grain = textured < 0 ? paper::textured(tpl.getBackgroundType().config) : textured > 0;
    return SettingsModel::paperType(types[static_cast<size_t>(background)]->page, c, grain);
}

void AppController::inkForPaper(const QColor& paper) {
    // A new document on dark paper takes the Dark palette (the tools whose colors are a palette's role follow it), and
    // one on light paper the light palette chosen before; a pen or text color of one's own that cannot be read on the
    // paper becomes the palette's body ink.
    const ColorPalettes& palettes = ColorPalettes::builtIn();
    const bool darkPaper = ColorPalettes::isDarkPaper(paper);
    const ColorPalette* current = palettes.palette(colorPalette());
    if (current && current->dark != darkPaper) {
        std::string light;
        app->getSettings()->getCustomElement(CUSTOM).getString("lightPalette", light);
        QString next;
        if (darkPaper) {
            app->getSettings()->getCustomElement(CUSTOM).setString("lightPalette", current->id.toStdString());
            const auto it = std::find_if(palettes.palettes().begin(), palettes.palettes().end(),
                                         [](const ColorPalette& p) { return p.dark; });
            next = it != palettes.palettes().end() ? it->id : QString();
        } else {
            const ColorPalette* back = palettes.palette(QString::fromStdString(light));
            next = back && !back->dark ? back->id : palettes.defaultId();
        }
        if (!next.isEmpty()) {
            setColorPalette(next);
        }
    }
    ToolHandler* th = app->getToolHandler();
    QMap<QString, QString> roles = colorRoles();
    bool changed = false;
    for (const auto& [tool, type]:
         {std::pair{QStringLiteral("pen"), TOOL_PEN}, std::pair{QStringLiteral("text"), TOOL_TEXT}}) {
        if (!colorRoleOf(tool).isEmpty()) {
            continue;  // (followed the palette)
        }
        const QColor now = qcolor(th->getTool(type).getColor());
        if (contrast(now, paper) >= 3.0) {
            continue;
        }
        const auto body = palettes.color(colorPalette(), "body", ColorPalettes::Kind::Ink);
        if (!body) {
            continue;
        }
        if (th->getToolType() == type) {
            th->setColor(toXojColor(*body), false);
        } else {
            th->getTool(type).setColor(toXojColor(*body));
        }
        roles.insert(tool, ColorRef{colorPalette(), "body"}.toString());
        changed = true;
    }
    if (changed) {
        storeColorRoles(roles);
        Q_EMIT toolChanged();
    }
}
